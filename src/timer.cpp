#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>

#include "timer.h"

void ptimer_reset(struct ptimer *t)
{
  t->start.tv_sec = 0;
  t->start.tv_usec = 0;
  t->end.tv_sec = 0;
  t->end.tv_usec = 0;
  t->elapsed_us = 0;
}

void ptimer_init(struct ptimer *t, const char* prefix)
{
  ptimer_reset(t);
  memset(t->prefix, 0, 64);
  snprintf(t->prefix, 64, "%s", prefix);
}

void ptimer_start(struct ptimer *t)
{
  ptimer_reset(t);
  ptimer_continue(t);
}

void ptimer_continue(struct ptimer *t)
{
  gettimeofday(&t->start, NULL);
}

void ptimer_stop(struct ptimer *t)
{
  gettimeofday(&t->end, NULL);
  t->elapsed_us += elapsed(&t->start, &t->end) * 1000000;
}

void ptimer_print(struct ptimer *t)
{
  fprintf(stderr, "[%s] elapsed: %.2lf ms\n", t->prefix, t->elapsed_us / 1000);
}

void ptimer_stop_and_print(struct ptimer *t)
{
  ptimer_stop(t);
  ptimer_print(t);
}


/* Useful for doing arithmetic on struct timevals. M*/
void timeDiff(struct timeval *d, struct timeval *a, struct timeval *b)
{
  d->tv_sec = a->tv_sec - b->tv_sec;
  d->tv_usec = a->tv_usec - b->tv_usec;
  if (d->tv_usec < 0) {
    d->tv_sec -= 1;
    d->tv_usec += 1000000;
  }
}

/* Return the no. of elapsed seconds between Starttime and Endtime. */
double elapsed(struct timeval *starttime, struct timeval *endtime)
{
  struct timeval diff;

  timeDiff(&diff, endtime, starttime);
  return tv_to_double(diff);
}

long clock_time_elapsed(struct timespec start, struct timespec end)
{
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    return seconds * 1000000000 + nanoseconds;
}

