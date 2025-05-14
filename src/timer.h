#ifndef HEMEM_TIMER_H
#define HEMEM_TIMER_H

/* Returns the number of seconds encoded in T, a "struct timeval". */
#define tv_to_double(t) (t.tv_sec + (t.tv_usec / 1000000.0))

struct ptimer {
  struct timeval start;
  struct timeval end;
  double elapsed_us;
  char prefix[64];
};

#ifdef __cplusplus
extern "C" {
#endif
  void ptimer_init(struct ptimer *t, const char* prefix);
  void ptimer_start(struct ptimer *t);
  void ptimer_continue(struct ptimer *t);
  void ptimer_stop(struct ptimer *t);
  void ptimer_stop_and_print(struct ptimer *t);
  void ptimer_print(struct ptimer *t);
  void ptimer_reset(struct ptimer *t);

  void timeDiff(struct timeval *d, struct timeval *a, struct timeval *b);
  double elapsed(struct timeval *starttime, struct timeval *endtime);
  long clock_time_elapsed(struct timespec start, struct timespec end);

#ifdef __cplusplus
}
#endif

#endif /* HEMEM_TIMER_H */
