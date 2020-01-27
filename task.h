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

#ifndef __FIBER_TASK_H__
#define __FIBER_TASK_H__

#include <stdint.h>
#include <setjmp.h>

#include "logger.h"

/* typedefs */
#define MAXFIBERS 503
#define ARRAYSIZE 512
#define DEFAULTSTACKSIZE (65536)

/* typedefs */
typedef struct scheduler scheduler_t;
typedef struct predicate predicate_t;
typedef struct fiber fiber_t;

/* function pointers types */
typedef void (*pf_init_t)(fiber_t *fiber);
typedef void (*pf_run_t)(fiber_t *fiber); 
typedef void (*pf_term_t)(fiber_t *fiber);
typedef void (*pf_done_t)(fiber_t *fiber);

typedef int  (*pf_check_t)(fiber_t *, void *);

typedef void (*pf_pre_hook_t)(scheduler_t *sched, void *extra);
typedef void (*pf_post_hook_t)(scheduler_t *sched, void *extra);


/* This enumeration defines the states of a fiber */
enum fiber_state_e 
  {
   FIBER_EGG,                /* Just malloced ! Still in its egg */
   FIBER_INIT,               /* Initial state of fiber after call to fiber_create */
   FIBER_SUSPEND,            /* suspended : waiting for a timer/predicate realization */
   FIBER_RUNNING,            /* ready to run, not waiting for anything */
   FIBER_TERM,               /* Fiber state when asked to terminate */
   FIBER_DONE,               /* Last state of a fiber just before disappearing */
   FIBER_NUM_STATES
};


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
 * This enumeration defines the return codes that can be returned
 * by the libraies functions. Despite its name, not all constants 
 * are errors.
 * ---------------------------------------------------------------------------
 */
enum fiber_error_e
  {
   FIBER_OK = 0,             /* no error */
   FIBER_ERROR,              /* unspecified generic error */
   FIBER_TIMEOUT,            /* a wait function ended with a timeout */
   FIBER_TOO_MANY_FIBERS,    /* cannot link fiber to scheduler because scheduler is full */
   FIBER_INVALID_TIMEOUT,    /* invalid timeout specified */
   FIBER_NO_SUCH_FIBER,      /* returned when looking for a non existent fiber
			      * (using its ID or pointer) */
   FIBER_ILLEGAL_STATE,      /* trying to do an illegal transition */
   FIBER_MEMORY_ALLOCATION_ERROR, /* malloc failed */
   FIBER_SIGNALERROR,        /* failure during sigaltstack/sigaction */
   FIBER_INVALID_STACK_SIZE, /* invalid stack size */
   FIBER_INVALID_PREDICATE,  /* invalid test predicate */
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


/*
 * ---------------------------------------------------------------------------
 * fiber_yield --
 *
 * Using this function a fiber will give back control to the scheduler.
 * The scheduler will run the next fiber in RUNNING state.
 *
 * If this function is called while the fiber is not running or not
 * attached to a schedule, it returns immediately and the execution continue.
 *
 * This function always return FIBER_OK.
 * ---------------------------------------------------------------------------
 */
int fiber_yield(fiber_t *fiber);


/*
 * ---------------------------------------------------------------------------
 * fiber_wait --
 *
 * Using this function a fiber will give back control to the scheduler.
 * The scheduler will run the next fiber in RUNNING state.
 * The fiber which has called this function will restarts in `msec'
 * milliseconds.
 *
 * If this function is called while the fiber is not running or not
 * attached to a schedule, it returns immediately and the execution continue.
 *
 * This function always return FIBER_TIMEOUT.
 * ---------------------------------------------------------------------------
 */
int fiber_wait(fiber_t *fiber, uint32_t msec);

// fonction to wait for a given predicate
// part of public API
int fiber_wait_for_cond( fiber_t *fiber, uint32_t msec, pf_check_t pfun, void *pfunarg);

// fonction to wait for a given fiber to finish
// part of public API
int fiber_join( fiber_t *fiber, uint32_t msec, fiber_t *other);

// fonction to wait for a given predicate
// part of public API
int fiber_wait_for_var( fiber_t *fiber, uint32_t msec, int *addr, int value);


/* --------------------------------------------------------------------------
 * fiber_new --
 *
 * This function allocates a new fiber. The allocated fiber is in EGG state
 * its stack is not allocated yet and it is not attached to any scheduler.
 *
 * It returns NULL on memory allocation failure.
 * ---------------------------------------------------------------------------*/
fiber_t *fiber_new(pf_run_t run_func, void *extra);


/* --------------------------------------------------------------------------
 * fiber_set_init_func --
 *
 * If possible, this function sets the function that will be called
 * when `fiber' is in INIT state.
 *
 * It returns FIBER_OK in most cases, otherwise :
 *  - FIBER_NO_SUCH_FIBER is returned if `fiber' is NULL.
 *  - FIBER_ILLEGAL_STATE if `fiber' is not in EGG state.
 * ---------------------------------------------------------------------------*/
int fiber_set_init_func( fiber_t *fiber, pf_init_t init_func);


/* --------------------------------------------------------------------------
 * fiber_set_term_func --
 *
 * If possible, this function sets the function that will be called
 * when `fiber' is in TERM state.
 *
 * It returns FIBER_OK in most cases, otherwise :
 *  - FIBER_NO_SUCH_FIBER is returned if `fiber' is NULL.
 *  - FIBER_ILLEGAL_STATE if `fiber' is not in EGG state.
 * ---------------------------------------------------------------------------*/
int fiber_set_term_func( fiber_t *fiber, pf_term_t term_func);

/* --------------------------------------------------------------------------
 * fiber_set_done_func --
 *
 * If possible, this function sets the function that will be called
 * when `fiber' is in DONE state.
 *
 * It returns FIBER_OK in most cases, otherwise :
 *  - FIBER_NO_SUCH_FIBER is returned if `fiber' is NULL.
 *  - FIBER_ILLEGAL_STATE if `fiber' is not in EGG state.
 * ---------------------------------------------------------------------------*/
int fiber_set_done_func( fiber_t *fiber, pf_done_t done_func);

/*
 * --------------------------------------------------------------------------
 * fiber_get_extra --
 *
 * Returns the `extra' data associated with a fiber. The `extra' data can
 * be given on fiber's creation and changed later with fiber_set_extra_data().
 *
 * Returns NULL if `fiber' is NULL.
 * ---------------------------------------------------------------------------
 */
void *fiber_get_extra(fiber_t *fiber);


/*
 * --------------------------------------------------------------------------
 * fiber_set_extra --
 *
 * Sets the `extra' data associated with a `fiber'.
 *
 * Returns FIBER_NO_SUCH_FIBER if `fiber' is NULL and FIBER_OK otherwise.
 * ---------------------------------------------------------------------------
 */
int fiber_set_extra( fiber_t *fiber, void *extra );

/*
 * --------------------------------------------------------------------------
 *
 * fiber_set_stack_size --
 *
 * Sets the size of the stack to allocate for `fiber'. It must be a positive
 * integer value expressed in bytes. The minimum allowed value is 2048 and 1G
 * is the maximum.
 *
 * It returns FIBER_OK in most cases, otherwise :
 *  - FIBER_NO_SUCH_FIBER is returned if `fiber' is NULL.
 *  - FIBER_ILLEGAL_STATE if `fiber' is not in EGG state.
 *  - FIBER_INVALID_STACK_SIZE if `stacksz' is outside allowed range.
 *
 * ---------------------------------------------------------------------------
 */
int fiber_set_stack_size( fiber_t *fiber, uint32_t stacksz );


/*
 * --------------------------------------------------------------------------
 * fiber_get_stack_size --
 *
 * Returns the stack size if fiber.
 *
 * Returns FIBER_NO_SUCH_FIBER (which is not in allowed stack size range)
 * if the supplied `fiber' doesn't exist.
 * ---------------------------------------------------------------------------
 */
int fiber_get_stack_size( fiber_t *fiber );

/*
 * --------------------------------------------------------------------------
 * fiber_start --
 *
 * Sets the scheduler that will run the fiber. The fiber must be in EGG
 * state when this function is called. It will move to INIT state, its stack
 * will be allocated.
 *
 * Returns FIBER_OK on most case and :
 *  - FIBER_NO_SUCH_FIBER is `fiber' is NULL.
 *  - FIBER_ILLEGAL_STATE if the `fiber' is not in EGG state.
 *  - FIBER_TOO_MANY_FIBERS if there are too many fibers already attached
 *       to the scheduler `sched'.
 *  - FIBER_MEMORY_ALLOCATION_ERROR if stack allocation fails.
 * ---------------------------------------------------------------------------
 */
int fiber_start( scheduler_t *sched, fiber_t *fiber );


/*
 * ---------------------------------------------------------------------------
 * fiber_stop --
 *
 * Change fiber's state to TERM which will end it on next scheduler cycle
 * in most cases. If this function is called during the fiber's scheduler
 * execution cycle, the fiber will end during this cycle.
 *
 * There are two `normal' ways for a fiber to end :
 *  - when its `run' method returns, its state becomes DONE
 *  - when this function is called, its state becomes TERM (and after DONE)
 * ---------------------------------------------------------------------------
 */
int fiber_stop( fiber_t *fiber );

/* ---------------------------------------------------------------------------
 * Free a fiber
 *
 * If FIBER_OK is returned the associated memory is freed
 * and the allocated stack too.
 *
 * Note that it must be called once the scheduler
 *
 * This function can return error codes :
 *  - FIBER_ILLEGAL_STATE if the fiber isn't in FIBER_EGG or FIBER_DONE state
 *  - FIBER_NO_SUCH_FIBER if the fiber doesn't seem to exist
 * ---------------------------------------------------------------------------
 */
int fiber_free( fiber_t *fiber );


/* ---------------------------------------------------------------------------
 * Create a new scheduler
 *
 * The created scheduler has no fiber.
 *
 * Returns NULL is memory allocation failed.
 * ---------------------------------------------------------------------------
 */
scheduler_t *scheduler_new();

/*
 * ---------------------------------------------------------------------------
 * Start running a scheduler at the given frequency
 * ---------------------------------------------------------------------------
 */
void scheduler_run( uint32_t period );

/*
 * ---------------------------------------------------------------------------
 *
 * Returns elapsed msec since first call to this function
 *
 * This function relies on gettimeofday() to retreive the current time.
 * The return value of this function will be passed to sched_cycle.
 * ---------------------------------------------------------------------------
 */
uint32_t sched_elapsed();

/* 
 * ---------------------------------------------------------------------------
 *
 * Runs a scheduler cycle.
 *
 * Used to run the scheduler from an external event loop.
 *
 * A scheduler cycle will examine all fibers starting
 * from fibers in the INIT state, proceeding with fibers in SUSPEND, RUNNING, 
 * TERM and DONE state in that order.
 *
 * While being processed a fiber can advance from one state
 * to the next. For example a fiber in INIT state will be moved
 * to RUNNING state and can enter the DONE state, meaning that the whole
 * fiber's life will last one scheduler cycle.
 * 
 * The `elapsed' argument will can be anything, but usually it will receive
 * a monotonic timestamp like the return value of sched_elapsed()
 *
 * ---------------------------------------------------------------------------
 */
void sched_cycle(scheduler_t *sched, uint32_t elapsed);

/* ---------------------------------------------------------------------------
 * Free a scheduler
 * 
 * will do nothing if the scheduler is currently
 * running a fiber.
 * ---------------------------------------------------------------------------
 */
int scheduler_free( scheduler_t *sched );

/* ---------------------------------------------------------------------------
 * Sets the hooks function
 * ---------------------------------------------------------------------------
 */
int scheduler_set_hooks( scheduler_t *sched, pf_pre_hook_t pf_pre_hook,
			 pf_post_hook_t pf_post_hook, void *extra);

/*
 * ---------------------------------------------------------------------------
 * Return the number of fibers (all states considered) currently
 * registered in the given scheduler
 * ---------------------------------------------------------------------------
 */
int sched_get_num_fibers( scheduler_t *sched);

#endif
