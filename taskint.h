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

#ifndef __FIBER_TASK_INTERNAL_H__
#define __FIBER_TASK_INTERNAL_H__

#include "task.h"

#include <setjmp.h>

/* fiber data structure */
struct fiber
{
  scheduler_t *scheduler;   /* Scheduler */
  fiber_t *next;            /* Used by scheduler which keeps the fibers
			     * in a linked list. There is one list for
			     * each fiber state. */
  
  jmp_buf context;          /* context, used to resume execution */

  pf_init_t pf_init;        /* pointer to initializer function or NULL.
			     * This function is called when the fiber
			     * is in INIT state.
			     * This function is called only once.
			     * Afterwards the fiber enters RUNNNG state. */

  pf_run_t  pf_run;         /* Pointer to fiber execution function.
			     * This function is called when the fiber
			     * is in RUNNING state. This function can be called
			     * multiple time, since RUNNING is the most common
			     * state. */

  pf_term_t pf_term;        /* Pointer to function called
			     * when the fiber is in TERM state or NULL.
			     * This function is called only once.
			     * Afterwards the fiber enters DONE state. */
  
  pf_done_t pf_done;        /* destructor, called once from DONE state.
			     * Afterwards the fiber is removed from the
			     * scheduler. */

  uint8_t state;            /* Current state of fiber */

  uint32_t fid;             /* fiber identifier generated on creation */

  
  uint32_t stacksz;         /* Each fiber has its stack, this field gives
			     * the fiber's stack size */
  uint8_t*   stack;         /* pointer to the stack, which is dynamically
			     * allocated */

  void*    extra;           /* Extra info attached to task during creation. */

  predicate_t *predicate;   /* When a fiber enter's SUSPEND state, a predicate
			     * is attached to it. When the predicate will be true
			     * the fiber will wake up and resume execution,
			     * it will enter the RUNNING state again */
};

/*
 * ---------------------------------------------------------------------------
 *  Scheduler data structure
 *
 *  In order to run a fiber must be attached to a scheduler.
 *  
 * ---------------------------------------------------------------------------
 */
struct scheduler
{
  fiber_t *fibers[ARRAYSIZE];       /* All fibers are kept in an array
				     * Array is an hash table of fixed size. */
  fiber_t *lists[FIBER_NUM_STATES]; /* A linked list of fibers is kept for each
				     * possible fiber state. */
  fiber_t *running;                 /* Currently running fiber. */
  int nfibers;                      /* Total number of fibers */

  uint32_t time;                    /* scheduler notion of time */
  
  /* definition of 2 functions pointers that will be invoked 
   * before and after each scheduler cycle.
   * Both functions are invoked with the same "extra" argument. */
  pf_pre_hook_t  pf_pre_hook;       /* function called before the scheduler cycle */
  pf_post_hook_t pf_post_hook;      /* function called after the scheduler cycle */
  void *extra;                      /* argument passed to the hooks */

  jmp_buf context;                  /* main context: used by fibers to give back 
				     * control to scheduler when yielding */
};


/*
 * --------------------------------------------------------------------------
 * Predicate structure
 * While in SUSPEND state a fiber can point to a structure like this
 * --------------------------------------------------------------------------
 */
struct predicate {
  predicate_t *next;        /* next in linked list */
  uint32_t deadline;        /* when the timer expires */
  fiber_t     *fiber;       /* associated fiber */
  void        *data;        /* data to pass to predicate function */
  pf_check_t   pf_check;    /* predicate function called for fiber */
  uint8_t      state;       /* state of predicate (ACTIVE, EXPIRED, DEAD) */
};



enum predicate_state_e
  {
   PREDICATE_ACTIVE = 0,     /* predicate is active and must be checked */
   PREDICATE_REALIZED,       /* condition is now true and associated timer 
			      * (if any) is not expired */
   PREDICATE_FIRED,          /* associated timer has expired before 
			      * the predicate becomes true */
   PREDICATE_DEAD,           /* ready to remove, this is the predicate state
			      * after REALIZED or FIRED */
  };


#endif
