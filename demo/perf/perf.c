#include "task.h"

int count = 0;

void run( fiber_t *fiber)
{
  count ++;
}

int main()
{
  scheduler_t *sched;
  fiber_t *fiber;
  uint32_t t;
  int i;

  /* create 100 fiber */
  for( i = 0; i < 100; ++i ) {
    fiber = fiber_new( run, NULL);
    fiber_set_stack_size( fiber, 1024 );
    fiber_start( sched, fiber );
  }

  /* do many cycles and measure time */
  sched_elapsed();
  for (i = 0; i < 100000; ++i) {
    sched_cycle( sched );
  }
  t = sched_elapsed();

  /* dump stats */
  printf("Number of iteration : %d\n", i);
  printf("Duration            : %d\n", t);
  printf("Iteration / second  : %d\n", (i/1000)*t);

  return 0;
}
