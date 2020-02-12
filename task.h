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
#include <limits.h>

#include "logger.h"

/* constants */
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


/* ---------------------------------------------------------------------------
 *  This enumeration defines the states of a fiber.
 *
 *  Newly malloced fibers are in FIBER_EGG state
 *  Once attached to a scheduler they move to FIBER_INIT. Their stack
 *  is allocated when in FIBER_INIT state.
 *
 *  As long as they are attached to their scheduler, their state is between
 *  FIBER_INIT and FIBER_DONE.
 *
 *  When their state becomes FIBER_DEAD, their stack is deallocated and they
 *  are not attached to their scheduler any more and can be freed.
 * ---------------------------------------------------------------------------
 */
enum fiber_state_e 
  {
   FIBER_EGG,                /* Just malloced ! Still in its egg */
   FIBER_INIT,               /* Initial state of fiber after call to fiber_create */
   FIBER_SUSPEND,            /* suspended : waiting for a timer/predicate realization */
   FIBER_RUNNING,            /* ready to run, not waiting for anything */
   FIBER_TERM,               /* Fiber state when asked to terminate */
   FIBER_DONE,               /* Last state of a fiber just before disappearing */
   FIBER_DEAD,               /* Fiber has been unlinked from its scheduler */
   FIBER_NUM_STATES
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

   FIBER_NO_SUCH_SCHED,      /* scheduler is NULL */
};


/* --------------------------------------------------------------------------
 * fiber_new --
 *
 * This function allocates a new fiber. The allocated fiber is in EGG state
 * its stack is not allocated yet and it is not attached to any scheduler.
 *
 * It returns NULL on memory allocation failure.
 * ---------------------------------------------------------------------------*/
fiber_t *fiber_new(pf_run_t run_func, void *extra);

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
 * fiber_free--
 *
 * Free a fiber
 *
 * If FIBER_OK is returned the associated memory is freed.
 * Note that the stack allocated by the scheduler when the fiber was
 * running is freed by the scheduler not by this function.
 *
 * Note that it must be called once the scheduler
 *
 * This function can return error codes :
 *  - FIBER_ILLEGAL_STATE if the fiber isn't in FIBER_EGG or FIBER_DONE state
 *  - FIBER_NO_SUCH_FIBER if the fiber doesn't seem to exist
 * ---------------------------------------------------------------------------
 */
int fiber_free( fiber_t *fiber );


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


/*
 * ---------------------------------------------------------------------------
 * fiber_wait_for_cond --
 *
 * Using this function a fiber will give back control to the scheduler.
 * The scheduler will run the next fiber in RUNNING state.
 * The fiber which has called this function will restarts in `msec'
 * milliseconds. In this cas the function will return FIBER_TIMEOUT
 *
 * The fiber can restart earlier
 * ---------------------------------------------------------------------------
 */
int fiber_wait_for_cond( fiber_t *fiber, uint32_t msec, pf_check_t pfun, void *pfunarg);

/*
 * ---------------------------------------------------------------------------
 * fiber_join --
 *
 * Using this function a fiber will give back control to the scheduler.
 * The scheduler will run the next fiber in RUNNING state.
 * The fiber which has called this function will restarts in `msec'
 * milliseconds. In this cas the function will return FIBER_TIMEOUT.
 *
 * The fiber can restart earlier if the fiber_t defined in 'other' terminates.
 * In this case, the function will return FIBER_OK.
 *
 * This function can return some error codes :
 *  - FIBER_NO_SUCH_FIBER if either 'fiber' or 'other' are not valid fibers.
 *  - FIBER_ERROR if 'fiber' and 'other' are the same. 
 *
 * In these situations, and if 'fiber' is a valid fiber, 'fiber' will yield
 * and return the error code on next scheduler cycle.
 * ---------------------------------------------------------------------------
 */
int fiber_join( fiber_t *fiber, uint32_t msec, fiber_t *other);

/*
 * ---------------------------------------------------------------------------
 * fiber_wait_for_var --
 *
 * Using this function a fiber will give back control to the scheduler.
 * The scheduler will run the next fiber in RUNNING state.
 * The fiber which has called this function will restarts in `msec'
 * milliseconds. In this cas the function will return FIBER_TIMEOUT.
 *
 * The fiber can restart earlier if the integer value at 'addr' contains
 * the value 'value'.
 * ---------------------------------------------------------------------------
 */
int fiber_wait_for_var( fiber_t *fiber, uint32_t msec, int *addr, int value);



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
 * fiber_get_scheduler --
 *
 * Returns the `scheduler' associated with a fiber.
 *
 * Returns NULL if `fiber' is NULL.
 * ---------------------------------------------------------------------------
 */
scheduler_t *fiber_get_scheduler(fiber_t *fiber);


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



/* ---------------------------------------------------------------------------
 * sched_new --
 *
 * Create a new scheduler. The created scheduler has no fiber.
 *
 * Returns NULL is memory allocation failed.
 * ---------------------------------------------------------------------------
 */
scheduler_t *sched_new();


/* ---------------------------------------------------------------------------
 * sched_free --
 *
 * Free a scheduler
 * 
 * will do nothing if the scheduler is currently
 * running a fiber.
 * ---------------------------------------------------------------------------
 */
int sched_free( scheduler_t *sched );


/*
 * ---------------------------------------------------------------------------
 * Stops all fibers.
 *
 * This function is used to stop all the fibers at once, for example when
 * the program exits.
 *
 * A call to sched_cycle() is required to really free the resources
 * used by fibers because the call to "fiber_term()" and "fiber_done()"
 * functions which are meant to release resources used by fibers.
 * ---------------------------------------------------------------------------
 */
void sched_stop( scheduler_t *sched );

/*
 * ---------------------------------------------------------------------------
 *  sched_elapsed --
 *
 * Returns elapsed milliseconds since first call to this function.
 *
 * This function relies on gettimeofday() to retreive the current time.
 * The return value of this function will be passed to sched_cycle.
 *
 * This function is not thread safe because on first call it needs
 * to initialize a static variable.
 * ---------------------------------------------------------------------------
 */
uint32_t sched_elapsed();

/* 
 * ---------------------------------------------------------------------------
 * sched_cycle --
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
 * The `timestamp' argument will can be anything, but usually it will receive
 * a monotonic timestamp like the return value of sched_elapsed().
 * Internally this value is kept in the scheduler data structure and will
 * be valid for the whole cycle. It can be retrieved with sched_timestamp()
 *
 * ---------------------------------------------------------------------------
 */
void sched_cycle(scheduler_t *sched, uint32_t timestamp);


/* ---------------------------------------------------------------------------
 * Sets the hooks function
 * ---------------------------------------------------------------------------
 */
int sched_set_hooks( scheduler_t *sched, pf_pre_hook_t pf_pre_hook,
		     pf_post_hook_t pf_post_hook, void *extra);

/*
 * ---------------------------------------------------------------------------
 * sched_numfibers --
 *
 * Return the number of fibers (all states considered) currently
 * registered in the given scheduler
 * ---------------------------------------------------------------------------
 */
int sched_numfibers( scheduler_t *sched);


/*
 * ---------------------------------------------------------------------------
 * sched_timestamp --
 * 
 * Return the current timestamp value stored in the scheduler.
 * If 'sched' is NULL, it returns 0.
 * ---------------------------------------------------------------------------
 */
uint32_t sched_timestamp( scheduler_t *sched );

/*
 * --------------------------------------------------------------------------
 * sched_get_extra --
 *
 * Returns the `extra' data associated with a fiber. The `extra' data can
 * be given on fiber's creation and changed later with fiber_set_extra_data().
 *
 * Returns NULL if `fiber' is NULL.
 * ---------------------------------------------------------------------------
 */
void *sched_get_extra(scheduler_t *sched);


/*
 * --------------------------------------------------------------------------
 * sched_set_extra --
 *
 * Sets the `extra' data associated with scheduler 'sched'.
 *
 * Returns FIBER_NO_SUCH_SCHEDULER if 'sched' is NULL and FIBER_OK otherwise.
 * ---------------------------------------------------------------------------
 */
int sched_set_extra( scheduler_t *sched, void *extra );


#endif
