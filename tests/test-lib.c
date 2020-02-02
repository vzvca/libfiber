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
#include <check.h>
#include <stdlib.h>
#include <unistd.h>

#include "taskint.h"

void pre_hook_func(scheduler_t *sched, void *extra)
{
  debug("pre-hook.\n");
}

void post_hook_func(scheduler_t *sched, void *extra)
{
  debug("post-hook.\n");
}

/* initialisation function called */
void init_func( fiber_t *fiber )
{
  info("Fiber %d init func called\n", fiber->fid);
}

/* term function called */
void term_func( fiber_t *fiber )
{
  info("Fiber %d term func called\n", fiber->fid);
}

/* done function called */
void done_func( fiber_t *fiber )
{
  info("Fiber %d done func called\n", fiber->fid);
}

/* fiber will stop after 1 yield */
void run_1iter( fiber_t *fiber )
{
  fiber_yield(fiber);
  return;
}

/* fiber will stop after 2 yield */
void run_2iter( fiber_t *fiber )
{
  fiber_yield(fiber);
  fiber_yield(fiber);
  return;
}

/* infinite loop */
void run_forever( fiber_t *fiber )
{
  while(1) fiber_yield(fiber);
}

/* does nothing returns immediatly */
void run_done( fiber_t *fiber )
{
  debug("fiber %d : done.\n", fiber->fid);
  return;
}

/* wait for 1 second */
void run_wait( fiber_t *fiber )
{
  info("fiber %d : waiting for 1 second.\n", fiber->fid);
  fiber_wait( fiber, 1000 );
  info("fiber %d : done waiting for 1 second.\n", fiber->fid);
}

/* wait for 1 sec max that the variable waitvar gets incremeted */
int waitvar = 0;
void run_wait_var( fiber_t *fiber )
{
  int res;
  debug("fiber %d : waiting for 1 second or waitvar (= %d) incremented.\n", fiber->fid, waitvar);
  res = fiber_wait_for_var( fiber, 1000, &waitvar, waitvar+1);
  debug("fiber %d : done waiting (result %d) waitvar is now %d.\n", fiber->fid, res, waitvar);
}

/* used for predicate testing */
static int pred_wait_cond(fiber_t *fiber, void *arg)
{
  int *iarg = (int*) arg;
  return (*iarg == 666);
}
void run_wait_cond( fiber_t *fiber )
{
  int res;
  debug("fiber %d : waiting for 1 second or for predicate.\n", fiber->fid);
  res = fiber_wait_for_cond( fiber, 1000, &pred_wait_cond, fiber_get_extra(fiber));
  debug("fiber %d : done waiting (result %d)..\n", fiber->fid, res);
}

/* fiber that spawn a new fiber each time it gets called
 * the new fiber uses the fiber_forever function */
void run_spawn_forever( fiber_t *fiber )
{
  while(1) {
    fiber_t *pf = fiber_new( run_forever, NULL );
    fiber_start( fiber->scheduler, pf );
    fiber_yield( fiber );
  }
}

/* fiber that spawn a new fiber each time it gets called
 * the new fiber uses the fiber_done function */
void run_spawn_done( fiber_t *fiber )
{
  while(1) {
    fiber_t *pf = fiber_new( run_done, NULL );
    fiber_start( fiber->scheduler, pf );
    fiber_yield( fiber );
  }
}


/* fiber that spawn a fiber that terminate immediatly (fiber_done used)
 * then wait for it to finish */
void run_spawn_join_done( fiber_t *fiber )
{
  fiber_t **ppf = (fiber_t**) fiber_get_extra( fiber );
  fiber_t *pf = fiber_new( run_done, NULL );
  *ppf = pf;
  fiber_start( fiber->scheduler, pf );
  fiber_yield( fiber );

  debug("Waiting ");
  fiber_join( fiber, 1000, pf);
  debug("Done waiting ");
}

/* fiber that spawn a fiber that terminate immediatly (fiber_done used)
 * then wait for it to finish */
void run_spawn_join_forever( fiber_t *fiber )
{
  fiber_t **ppf = (fiber_t**) fiber_get_extra( fiber );
  fiber_t *pf = fiber_new( run_forever, NULL );
  *ppf = pf;
  fiber_start( fiber->scheduler, pf );
  fiber_yield( fiber );

  debug("Waiting ");
  fiber_join( fiber, 1000, pf);
  debug("Done waiting ");
}

/* fiber that spawn a fiber that terminate immediatly (fiber_done used)
 * then wait for it to finish */
void run_spawn_join_wait_var( fiber_t *fiber )
{
  fiber_t **ppf = (fiber_t**) fiber_get_extra( fiber );
  fiber_t *pf = fiber_new( run_wait_var, NULL );
  *ppf = pf;
  fiber_start( fiber->scheduler, pf );
  fiber_yield( fiber );

  debug("Waiting ");
  fiber_join( fiber, 1000, pf);
  debug("Done waiting ");
}

/* --------------------------------------------------------------------------
 *   scheduler with a single never ending fiber  
 * --------------------------------------------------------------------------*/
START_TEST (test_no_fiber)
{
  scheduler_t *sched = sched_new();
  int n;

  ck_assert_int_eq(sched_numfibers(sched), 0);
  
  /* run the scheduler 1000 times */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }

  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
}
END_TEST


/* --------------------------------------------------------------------------
 *   scheduler with a single never ending fiber  
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_forever)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_forever, NULL);

  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);
  
  /* run the scheduler 1000 times */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }

  /* test fiber still there */
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a single immediatly ending fiber
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_done)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_done, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run a cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* run the scheduler 1000 times
   * and check the number of active fibers */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a single fiber that lasts 2 scheduling cycles
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_2iter)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_2iter, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* run the scheduler 1000 times
   * and check the number of active fibers */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a single never ending fiber
 *   in this test the fiber is stopped with fiber_stop
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_forever_stop)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_forever, NULL);

  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);
  
  /* run the scheduler 1000 times */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }

  /* test fiber still there */
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* stop the fiber */
  fiber_stop( f1 );
  ck_assert_int_eq(sched_numfibers(sched), 1);
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);
  
  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST


/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber should be still waiting
 *               at the end of the test !
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_1)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_wait, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler
   * and check the number of active fibers */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber stop after the sleep
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_2)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_wait, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* sleep for a while */
  sleep(2);
  
  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber should be still waiting
 *               at the end of the test !
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_var_1)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_wait_var, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler
   * and check the number of active fibers */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber stop after the sleep
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_var_2)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_wait_var, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* sleep for a while */
  sleep(2);
  
  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber stop after the variable has been set
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_var_3)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_wait_var, NULL);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* increment the variable */
  ++waitvar;
  
  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber should be still waiting
 *               at the end of the test !
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_cond_1)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  int arg = 111;
  
  f1 = fiber_new(run_wait_cond, (void*) &arg);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler
   * and check the number of active fibers */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber should be finished at end of test
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_cond_2)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  int arg = 111;
  
  f1 = fiber_new(run_wait_cond, (void*) &arg);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* sleep for a while */
  sleep(2);
  
  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   wait test : in this test the fiber should be finished at end of test
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_wait_cond_3)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  int arg = 111;
  
  f1 = fiber_new(run_wait_cond, (void*) &arg);

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* sleep for a while */
  arg = 666;
  
  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a fiber that spawn a new fiber and calls join
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_spawn_forever_1)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1, *f2;
  int n;
  
  f1 = fiber_new(run_spawn_join_forever, (void*) &f2);

  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* 1st cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_RUNNING);
  ck_assert_int_eq(f2->state, FIBER_INIT);
  
  /* 2nd cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_SUSPEND);
  ck_assert_int_eq(f2->state, FIBER_RUNNING);
  
  /* run the scheduler 1000 times */
  for( n = 0; n < 1000; ++n ) {
    sched_cycle( sched, sched_elapsed());
  }

  /* test fiber still there */
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_SUSPEND);
  ck_assert_int_eq(f2->state, FIBER_RUNNING);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
  fiber_free( f2 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a fiber that spawn a new fiber and calls join
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_spawn_forever_2)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1, *f2;
  int n;
  
  f1 = fiber_new(run_spawn_join_forever, (void*) &f2);

  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* 1st cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_RUNNING);
  ck_assert_int_eq(f2->state, FIBER_INIT);
  
  /* 2nd cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_SUSPEND);
  ck_assert_int_eq(f2->state, FIBER_RUNNING);
  
  /* sleep - this will force the join to return */
  sleep(2);

  /* test fiber still there */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);
  ck_assert_int_eq(f1->state, FIBER_DONE);
  ck_assert_int_eq(f2->state, FIBER_RUNNING);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
  fiber_free( f2 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a fiber that spawn a new fiber and calls join
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_spawn_forever_3)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1, *f2;
  int n;
  
  f1 = fiber_new(run_spawn_join_forever, (void*) &f2);

  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* 1st cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_RUNNING);
  ck_assert_int_eq(f2->state, FIBER_INIT);
  
  /* few cycles */
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_SUSPEND);
  ck_assert_int_eq(f2->state, FIBER_RUNNING);
  
  /* force f2 to stop */
  fiber_stop(f2);
  ck_assert_int_eq(f2->state, FIBER_TERM);
  
  /* next cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);
  ck_assert_int_eq(f1->state, FIBER_SUSPEND);
  ck_assert_int_eq(f2->state, FIBER_DONE);

  /* next cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);
  ck_assert_int_eq(f1->state, FIBER_DONE);
  ck_assert_int_eq(f2->state, FIBER_DONE);

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
  fiber_free( f2 );
}
END_TEST

/* --------------------------------------------------------------------------
 *   scheduler with a fiber that spawn a new fiber and calls join
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_spawn_done_1)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1, *f2;
  int n;
  
  f1 = fiber_new(run_spawn_join_done, (void*) &f2);

  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* 1st cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 2);
  ck_assert_int_eq(f1->state, FIBER_RUNNING);
  ck_assert_int_eq(f2->state, FIBER_INIT);
  
  /* 2nd cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);
  ck_assert_int_eq(f1->state, FIBER_SUSPEND);
  ck_assert_int_eq(f2->state, FIBER_DONE);
  
  /* 3rd cycle */
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 0);
  ck_assert_int_eq(f1->state, FIBER_DONE);
  ck_assert_int_eq(f2->state, FIBER_DONE);
  
  /* clean */
  sched_free( sched );
  fiber_free( f1 );
  fiber_free( f2 );
}
END_TEST


/* --------------------------------------------------------------------------
 *   test functions that set fiber attributes
 * --------------------------------------------------------------------------*/
START_TEST (test_single_fiber_set_parameters)
{
  scheduler_t *sched = sched_new();
  fiber_t *f1;
  int n;
  
  f1 = fiber_new(run_forever, NULL);

  /* extra parameter */
  ck_assert_int_eq( fiber_set_stack_size(f1, 32768), FIBER_OK);
  ck_assert_int_eq( fiber_get_extra(f1) == NULL, 1);
  ck_assert_int_eq( fiber_set_extra(f1, (void*) 1111 ), FIBER_OK );
  ck_assert_int_eq( fiber_get_extra(f1) == (void*) 1111, 1);

  /* function pointers */
  ck_assert_int_eq( f1->pf_init == NULL, 1);
  ck_assert_int_eq( f1->pf_term == NULL, 1);
  ck_assert_int_eq( f1->pf_done == NULL, 1);
  ck_assert_int_eq( fiber_set_init_func( f1, &init_func ), FIBER_OK );
  ck_assert_int_eq( fiber_set_term_func( f1, &term_func ), FIBER_OK );
  ck_assert_int_eq( fiber_set_done_func( f1, &done_func ), FIBER_OK );
  ck_assert_int_eq( f1->pf_init == &init_func, 1);
  ck_assert_int_eq( f1->pf_term == &term_func, 1);
  ck_assert_int_eq( f1->pf_done == &done_func, 1);

  /* check erroneous use of function */
  ck_assert_int_eq( fiber_set_stack_size(NULL, 32768), FIBER_NO_SUCH_FIBER );
  ck_assert_int_eq( fiber_set_extra(NULL, (void*) 1111 ), FIBER_NO_SUCH_FIBER );
  ck_assert_int_eq( fiber_set_init_func( NULL, &init_func ), FIBER_NO_SUCH_FIBER );
  ck_assert_int_eq( fiber_set_term_func( NULL, &term_func ), FIBER_NO_SUCH_FIBER );
  ck_assert_int_eq( fiber_set_done_func( NULL, &done_func ), FIBER_NO_SUCH_FIBER );

  /* start the fiber */
  ck_assert_int_eq(sched_numfibers(sched), 0);
  fiber_start( sched, f1);
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* check erroneous use of function */
  ck_assert_int_eq( fiber_set_stack_size(f1, 32768), FIBER_ILLEGAL_STATE );
  ck_assert_int_eq( fiber_set_extra(f1, (void*) 1111 ), FIBER_OK );
  ck_assert_int_eq( fiber_set_init_func( f1, &init_func ), FIBER_ILLEGAL_STATE );
  ck_assert_int_eq( fiber_set_term_func( f1, &term_func ), FIBER_ILLEGAL_STATE );
  ck_assert_int_eq( fiber_set_done_func( f1, &done_func ), FIBER_ILLEGAL_STATE );

  /* run the scheduler */
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());
  sched_cycle( sched, sched_elapsed());
  ck_assert_int_eq(sched_numfibers(sched), 1);

  /* check erroneous use of function */
  ck_assert_int_eq( fiber_set_stack_size(f1, 32768), FIBER_ILLEGAL_STATE );
  ck_assert_int_eq( fiber_set_extra(f1, (void*) 1111 ), FIBER_OK );
  ck_assert_int_eq( fiber_set_init_func( f1, &init_func ), FIBER_ILLEGAL_STATE );
  ck_assert_int_eq( fiber_set_term_func( f1, &term_func ), FIBER_ILLEGAL_STATE );
  ck_assert_int_eq( fiber_set_done_func( f1, &done_func ), FIBER_ILLEGAL_STATE );

  /* clean */
  sched_free( sched );
  fiber_free( f1 );
}
END_TEST


/* scheduler test suite */
Suite *sched_suite(void)
{
  Suite *s;
  TCase *tc_core;

  s = suite_create("Scheduling");

  /* Core test case */
  tc_core = tcase_create("Core");

  tcase_add_test(tc_core, test_no_fiber);
  tcase_add_test(tc_core, test_single_fiber_forever);
  tcase_add_test(tc_core, test_single_fiber_done);
  tcase_add_test(tc_core, test_single_fiber_2iter);
  tcase_add_test(tc_core, test_single_fiber_forever_stop);
  tcase_add_test(tc_core, test_single_fiber_wait_1);
  tcase_add_test(tc_core, test_single_fiber_wait_2);
  tcase_add_test(tc_core, test_single_fiber_wait_var_1);
  tcase_add_test(tc_core, test_single_fiber_wait_var_2);
  tcase_add_test(tc_core, test_single_fiber_wait_var_3);
  tcase_add_test(tc_core, test_single_fiber_wait_cond_1);
  tcase_add_test(tc_core, test_single_fiber_wait_cond_2);
  tcase_add_test(tc_core, test_single_fiber_wait_cond_3);
  tcase_add_test(tc_core, test_single_fiber_spawn_forever_1);
  tcase_add_test(tc_core, test_single_fiber_spawn_forever_2);
  tcase_add_test(tc_core, test_single_fiber_spawn_forever_3);
  tcase_add_test(tc_core, test_single_fiber_spawn_done_1);
  tcase_add_test(tc_core, test_single_fiber_set_parameters);
  
  suite_add_tcase(s, tc_core);

  return s;
}


int main(void)
{
  int number_failed = 0;
  Suite *s;
  SRunner *sr;

  s = sched_suite();
  sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

