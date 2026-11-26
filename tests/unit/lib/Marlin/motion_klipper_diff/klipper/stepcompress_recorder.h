#pragma once
// Replacement for Klipper stepcompress.c: records the step times of itersolve.

#ifdef __cplusplus
extern "C" {
#endif

struct recorded_steps {
    double *time; // print time of the step (s)
    signed char *dir; // 1 = positive, 0 = negative (Klipper sdir)
    int count, capacity;
    long long skipped_net; // net steps (positive - negative) before record_from
};

struct stepcompress {
    double record_from; // steps before this print time (s) are not recorded
    int next_step_dir;
    int has_pending;
    double pending_time;
    struct recorded_steps raw; // each step that itersolve generates
    struct recorded_steps filtered; // the steps after the rollback filter of stepcompress_append()
};

struct stepcompress *stepcompress_recorder_alloc(void);
void stepcompress_recorder_free(struct stepcompress *sc);
// Commits the pending step. Call after the last itersolve_generate_steps().
void stepcompress_recorder_finish(struct stepcompress *sc);

#ifdef __cplusplus
}
#endif
