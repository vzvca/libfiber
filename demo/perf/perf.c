#include <stdio.h>

#include "task.h"

volatile int count = 0;

void run( fiber_t *fiber)
{
  while(1) {
    count ++;
    fiber_yield(fiber);
  }
}

int main()
{
  scheduler_t *sched;
  fiber_t *fiber;
  uint32_t t;
  int i;

  /* create scheduler */
  sched = scheduler_new();
  
  /* create 100 fiber */
  for( i = 0; i < 100; ++i ) {
    fiber = fiber_new( run, NULL);
    fiber_set_stack_size( fiber, 1024 );
    fiber_start( sched, fiber );
  }

  /* do many cycles and measure time */
  sched_elapsed();
  for (i = 0; i < 500000; ++i) {
    sched_cycle( sched, 0 );
  }
  t = sched_elapsed();

  /* dump stats */
  printf("Number of context switch : %d\n", count);
  printf("Context switch / second  : %d\n", 1000*(count/t));

  return 0;
}
