// Replacement for Klipper stepcompress.c: records the step times of itersolve.
#include "stepcompress_recorder.h"

#include <stdlib.h>
#include <string.h>

#include "stepcompress.h"

// Klipper stepcompress.c SDS_FILTER_TIME
#define SDS_FILTER_TIME .000750

static void push(struct recorded_steps *s, double time, int dir, double record_from) {
    if (time < record_from) {
        s->skipped_net += dir ? 1 : -1;
        return;
    }
    if (s->count == s->capacity) {
        s->capacity = s->capacity ? 2 * s->capacity : 1024;
        s->time = realloc(s->time, s->capacity * sizeof(*s->time));
        s->dir = realloc(s->dir, s->capacity * sizeof(*s->dir));
    }
    s->time[s->count] = time;
    s->dir[s->count] = (signed char)dir;
    s->count++;
}

static void free_steps(struct recorded_steps *s) {
    free(s->time);
    free(s->dir);
    memset(s, 0, sizeof(*s));
}

struct stepcompress *stepcompress_recorder_alloc(void) {
    struct stepcompress *sc = calloc(1, sizeof(*sc));
    return sc;
}

void stepcompress_recorder_free(struct stepcompress *sc) {
    free_steps(&sc->raw);
    free_steps(&sc->filtered);
    free(sc);
}

void stepcompress_recorder_finish(struct stepcompress *sc) {
    stepcompress_commit(sc);
}

int stepcompress_get_step_dir(struct stepcompress *sc) {
    return sc->next_step_dir;
}

int stepcompress_append(struct stepcompress *sc, int sdir, double print_time, double step_time) {
    const double time = print_time + step_time;
    push(&sc->raw, time, sdir, sc->record_from);
    if (sc->has_pending) {
        if (sdir != sc->next_step_dir && time - sc->pending_time < SDS_FILTER_TIME) {
            // Rollback last step to avoid rapid step+dir+step (as Klipper)
            sc->has_pending = 0;
            sc->next_step_dir = sdir;
            return 0;
        }
        push(&sc->filtered, sc->pending_time, sc->next_step_dir, sc->record_from);
    }
    sc->has_pending = 1;
    sc->pending_time = time;
    sc->next_step_dir = sdir;
    return 0;
}

int stepcompress_commit(struct stepcompress *sc) {
    if (sc->has_pending) {
        push(&sc->filtered, sc->pending_time, sc->next_step_dir, sc->record_from);
        sc->has_pending = 0;
    }
    return 0;
}
