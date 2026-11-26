#include "klipper_oracle.h"

#include <stddef.h>
#include <stdlib.h>

#include "klipper/itersolve.h"
#include "klipper/trapq.h"

// Klipper functions without a header (declared in klippy/chelper/__init__.py)
struct stepper_kinematics *corexy_stepper_alloc(char type);
struct stepper_kinematics *cartesian_stepper_alloc(char axis);
struct stepper_kinematics *input_shaper_alloc(void);
int input_shaper_set_sk(struct stepper_kinematics *sk, struct stepper_kinematics *orig_sk);
int input_shaper_set_shaper_params(struct stepper_kinematics *sk, char axis, int n, double a[], double t[]);
struct stepper_kinematics *extruder_stepper_alloc(void);
void extruder_stepper_free(struct stepper_kinematics *sk);
void extruder_set_pressure_advance(struct stepper_kinematics *sk, double print_time, double pressure_advance, double smooth_time);

static int any_shaper(const struct kl_params *p, int first_axis, int last_axis) {
    for (int i = first_axis; i <= last_axis; ++i) {
        if (p->shaper[i].n > 0) {
            return 1;
        }
    }
    return 0;
}

// As klippy/extras/input_shaper.py: wrap the stepper kinematics in an input shaper.
static struct stepper_kinematics *wrap_shaper(const struct kl_params *p, struct stepper_kinematics *orig) {
    struct stepper_kinematics *is = input_shaper_alloc();
    input_shaper_set_sk(is, orig);
    for (int axis = 0; axis < 3; ++axis) {
        struct kl_shaper s = p->shaper[axis];
        input_shaper_set_shaper_params(is, 'x' + axis, s.n, s.a, s.t);
    }
    return is;
}

struct kl_session {
    struct trapq *tq, *eq;
    struct stepper_kinematics *orig[4], *used[4];
    double end_time;
    // The moves of each trapq in time order (with the null moves and the sentinels), for the
    // position queries.
    struct move **list[2];
    int list_count[2];
};

static void make_list(struct trapq *tq, struct move ***out, int *count) {
    int n = 0;
    struct move *m;
    list_for_each_entry(m, &tq->moves, node) {
        ++n;
    }
    *out = malloc(n * sizeof(**out));
    *count = 0;
    list_for_each_entry(m, &tq->moves, node) {
        (*out)[(*count)++] = m;
    }
}

struct kl_session *kl_session_create(const struct kl_params *p, const struct kl_move *moves, int move_count) {
    struct kl_session *s = calloc(1, sizeof(*s));
    s->tq = trapq_alloc();
    s->eq = trapq_alloc();

    // Without moves, trapq_check_sentinels() adds no sentinels, and kl_session_position() has no
    // move to read. One still move at the start position keeps the list valid.
    struct kl_move still = { .move_t = 1. };
    if (move_count == 0) {
        for (int i = 0; i < 3; ++i) {
            still.start_pos[i] = p->start_pos[i];
        }
        moves = &still;
        move_count = 1;
    }

    for (int i = 0; i < move_count; ++i) {
        const struct kl_move *src = &moves[i];
        struct move *m = move_alloc();
        m->print_time = src->print_time;
        m->move_t = src->move_t;
        m->start_v = src->start_v;
        m->half_accel = src->half_accel;
        m->start_pos.x = src->start_pos[0];
        m->start_pos.y = src->start_pos[1];
        m->start_pos.z = src->start_pos[2];
        m->axes_r.x = src->axes_r[0];
        m->axes_r.y = src->axes_r[1];
        m->axes_r.z = src->axes_r[2];
        trapq_add_move(s->tq, m);

        // As klippy/kinematics/extruder.py process_move(): x is the extruder position, y is the
        // "can pressure advance" flag.
        struct move *e = move_alloc();
        e->print_time = src->print_time;
        e->move_t = src->move_t;
        e->start_v = src->start_v * src->axes_r[3];
        e->half_accel = src->half_accel * src->axes_r[3];
        e->start_pos.x = src->start_pos[3];
        e->axes_r.x = 1.;
        e->axes_r.y = (src->axes_r[3] > 0. && (src->axes_r[0] != 0. || src->axes_r[1] != 0.)) ? 1. : 0.;
        trapq_add_move(s->eq, e);

        if (src->print_time + src->move_t > s->end_time) {
            s->end_time = src->print_time + src->move_t;
        }
    }
    trapq_check_sentinels(s->tq);
    trapq_check_sentinels(s->eq);
    make_list(s->tq, &s->list[0], &s->list_count[0]);
    make_list(s->eq, &s->list[1], &s->list_count[1]);

    if (p->corexy) {
        s->orig[0] = corexy_stepper_alloc('+');
        s->orig[1] = corexy_stepper_alloc('-');
    } else {
        s->orig[0] = cartesian_stepper_alloc('x');
        s->orig[1] = cartesian_stepper_alloc('y');
    }
    s->orig[2] = cartesian_stepper_alloc('z');
    for (int i = 0; i < 3; ++i) {
        itersolve_set_position(s->orig[i], p->start_pos[0], p->start_pos[1], p->start_pos[2]);
    }
    s->orig[3] = extruder_stepper_alloc();
    if (p->pressure_advance > 0.) {
        extruder_set_pressure_advance(s->orig[3], 0., p->pressure_advance, p->smooth_time);
    }

    for (int i = 0; i < 4; ++i) {
        s->used[i] = s->orig[i];
    }
    if (any_shaper(p, 0, 1)) {
        s->used[0] = wrap_shaper(p, s->orig[0]);
        s->used[1] = wrap_shaper(p, s->orig[1]);
    }
    if (any_shaper(p, 2, 2)) {
        s->used[2] = wrap_shaper(p, s->orig[2]);
    }
    for (int i = 0; i < 4; ++i) {
        itersolve_set_trapq(s->used[i], i == 3 ? s->eq : s->tq, p->step_dist[i]);
    }
    return s;
}

void kl_session_generate(struct kl_session *s, struct stepcompress *out[4]) {
    // The tail sentinel keeps the last position for ever, so one flush past the end (plus the
    // largest shaper or smoothing window) generates all steps.
    const double flush_time = s->end_time + 1.;
    for (int i = 0; i < 4; ++i) {
        itersolve_generate_steps(s->used[i], out[i], flush_time);
        stepcompress_recorder_finish(out[i]);
    }
}

double kl_session_position(struct kl_session *s, int axis, double time) {
    const int l = axis == 3 ? 1 : 0;
    struct move **list = s->list[l];
    // The last move that starts at or before `time` (skip the head sentinel at -1 s)
    int lo = 1, hi = s->list_count[l] - 1;
    if (time < list[lo]->print_time) {
        time = list[lo]->print_time;
    }
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (list[mid]->print_time <= time) {
            lo = mid;
        } else {
            hi = mid - 1;
        }
    }
    struct move *m = list[lo];
    struct stepper_kinematics *sk = s->used[axis];
    return sk->calc_position_cb(sk, m, time - m->print_time);
}

void kl_session_free(struct kl_session *s) {
    for (int i = 0; i < 3; ++i) {
        if (s->used[i] != s->orig[i]) {
            free(s->used[i]);
        }
        free(s->orig[i]);
    }
    extruder_stepper_free(s->orig[3]);
    free(s->list[0]);
    free(s->list[1]);
    trapq_free(s->tq);
    trapq_free(s->eq);
    free(s);
}
