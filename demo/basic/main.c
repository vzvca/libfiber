/*
 * MIT License
 *
 * Copyright (c) 2020 vzvca
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "task.h"

#include <stdio.h>

void fiber1( fiber_t *fiber )
{
  int i;
  for ( i = 0; i < 5; ++ i )
    {
      printf( "Hey, I'm fiber #1: %d\n", i );
      fiber_yield( fiber );
    }
  return;
}

void fibonacchi( fiber_t *fiber )
{
  int i;
  int fib[2] = { 0, 1 };
	
  /* sleep( 2 ); */
  printf( "fibonacchi(0) = 0\nfibonnachi(1) = 1\n" );
  for( i = 2; i < 15; ++ i )
    {
      int nextFib = fib[0] + fib[1];
      printf( "fibonacchi(%d) = %d\n", i, nextFib );
      fib[0] = fib[1];
      fib[1] = nextFib;
      fiber_yield(fiber);
    }
}

void squares( fiber_t *fiber )
{
  int i;
	
  /* sleep( 5 ); */
  for ( i = 0; i < 10; ++ i )
    {
      printf( "%d*%d = %d\n", i, i, i*i );
      fiber_yield(fiber);
    }
}

int main( int argc, char **argv)
{
  scheduler_t *sched;
  fiber_t *f1, *f2, *f3;

  set_log_level(LOG_TRACE);
  
  /* create scheduler */
  sched = sched_new();

  /* create fibers */
  f1 = fiber_new(fiber1, NULL);
  f2 = fiber_new(fibonacchi, NULL);
  f3 = fiber_new(squares, NULL);

  /* start fibers in scheduler
   * this call just adds the fibers to the scheduler
   * they do not run at this point */
  fiber_start( sched, f1);
  fiber_start( sched, f2);
  fiber_start( sched, f3);

  /* run the scheduler for 3 times */
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());

  sched_free(sched);
  
  return 0;
}
