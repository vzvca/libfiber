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

#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>

#include "taskint.h"



/* forward declarations */
static int schedBoot( fiber_t *fiber );
static void schedDispatch(scheduler_t *sched);


/* ----------------------------------------------------------------------------
 * hashing pointer value
 * source: internet - to be check on 64-bit and 32-bit platform
 * I don't know if it has many collisions
 * ----------------------------------------------------------------------------*/
static uint32_t fiberHash(fiber_t *fiber)
{
  uintptr_t ad = (uintptr_t) fiber;
  return (size_t) ((13*ad) ^ (ad >> 15));
}

/* ----------------------------------------------------------------------------
 * Check existence of fiber
 * ----------------------------------------------------------------------------*/
static int fiberCheckExist(fiber_t *fiber)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->scheduler != NULL ) {
    /* check if part of hash table */
    if ( (fiber->fid < ARRAYSIZE) &&
	 (fiber->scheduler->fibers[fiber->fid] == fiber) ) {
      return FIBER_OK;
    }
    return FIBER_NO_SUCH_FIBER;
  }
  return FIBER_OK;
}

/* ----------------------------------------------------------------------------
 * check a predicate
 * and update its state depending on the result
 * ----------------------------------------------------------------------------*/
static int schedCheckPredicate( scheduler_t *sched, predicate_t *pred, uint32_t elapsed)
{
  int res = PREDICATE_ACTIVE;
  if ( (pred->deadline > 0) && (pred->deadline < elapsed) ) {
    pred->state = PREDICATE_FIRED;
    res = 1;
  }
  else if ( pred->pf_check != NULL ) {
    res = pred->pf_check( pred->fiber, pred->data );
    if ( res ) {
      pred->state = PREDICATE_REALIZED;
    }
  }
  return res;
}


/* ----------------------------------------------------------------------------
 * Process a predicate
 * ----------------------------------------------------------------------------*/
static void schedProcessPredicate(scheduler_t *sched, predicate_t *pp, uint32_t elapsed)
{
  /* get linked fiber and timer */
  fiber_t *pf = pp->fiber;
  int res;

  /* check if an error occured */
  if ( pf == NULL ) {
    error("Fiber linked to predicate %p is NULL\n", pf);
    return;
  }

  res = schedCheckPredicate( sched, pp, elapsed );
  if ( res ) {
    pf->state = FIBER_RUNNING;
  }
}

/* ----------------------------------------------------------------------------
 * Process all predicates
 * ----------------------------------------------------------------------------*/
static void schedProcessPredicates(scheduler_t *sched)
{
  uint32_t now;
  fiber_t *pf;

  /* current elapsed time in msec */
  now = sched_timestamp( sched );
  
  for( pf = sched->lists[FIBER_SUSPEND]; pf != NULL; pf = pf->next) {
    if ( pf->state != FIBER_SUSPEND ) {
      error( "fiber %d not in FIBER_SUSPEND state\n", pf->fid );
      continue;
    }
    
    if ( pf->predicate != NULL ) {
      schedProcessPredicate( sched, pf->predicate, now);
    }
    else {
      /* a suspended fiber with no predicate is a fiber
       * which called yield() explicitely to give processor
       * to other tasks. */
      pf->state = FIBER_RUNNING;
    }
  }
}

/* ----------------------------------------------------------------------------
 * Clean a list of fibers
 * ----------------------------------------------------------------------------*/
static void schedCleanList(scheduler_t *sched, uint32_t state )
{
  fiber_t *opf, *pf;
  fiber_t *head = NULL;
  
  for( pf = sched->lists[state] ; pf; pf = opf ) {
    opf = pf->next;
    
    if ( pf->state == state ) {
      /* note that the list is reversed doing this */
      pf->next = head;
      head = pf;

      debug( "fiber %p (fid = %d) still in state %d.\n",
	     pf, pf->fid, state);
    }
    else {
      /* insert current fiber at the head of the list
       * of target state */
      pf->next = sched->lists[pf->state];
      sched->lists[pf->state] = pf;

      debug( "moving fiber %p (fid = %d) from %d to %d.\n",
	     pf, pf->fid, state, pf->state);
    }
  }

  /* store new list */
  sched->lists[state] = head;
}

/* ----------------------------------------------------------------------------
 * Removes a fiber from scheduler.
 * Doesn't free memory associated with fiber
 * ----------------------------------------------------------------------------*/
static int schedRemoveFiber(scheduler_t *sched, fiber_t *fiber)
{
  if ( sched == NULL ) {
    return FIBER_ERROR;
  }
  if ( fiber == NULL || fiber->fid >= ARRAYSIZE) {
    return FIBER_NO_SUCH_FIBER;
  }

  if (sched->fibers[fiber->fid] == fiber) {
    sched->fibers[fiber->fid] = NULL;
    --sched->nfibers;
    free( fiber->stack );
    fiber->stack = NULL;
    return FIBER_OK;
  }

  error("Fiber %d not found.\n", fiber->fid);
  return FIBER_NO_SUCH_FIBER;
}

/*
 * ----------------------------------------------------------------------------
 *  sched_cycle --
 *
 *  Runs a scheduler cycle. The scheduler will process all the fibers that
 *  were added to it. Its processes the fibers according to their state :
 *
 *   - First fibers in FIBER_INIT state : they will be started and their state
 *     becomes FIBER_RUNNING. They will run later in the same cycle.
 *     A stack is allocated during this stage.
 *
 *   - Then fibers in FIBER_SUSPEND state : it will check if they need to be
 *     restarted, if so their state is changed to FIBER_RUNNING and they
 *     will be run later in the same cycle.
 *
 *   - Then fibers in FIBER_RUNNING state : they run which can trigger a change
 *     in their current state : it will change to FIBER_SUSPEND or
 *     FIBER_DONE. They can change the state of another fiber, calling
 *     fiber_term().
 *
 *   - Then fibers in FIBER_TERM state. Their state is changed to FIBER_DONE.
 *
 *   - Then fibers in FIBER_DONE state. These fibers are removed from the
 *     scheduler and their stack is deallocated.
 *  
 *  The 'timestamp' argument is a logical time that will keep this value 
 *  during the whole cycle. Usually this timestamp is the return value
 *  of sched_elapsed() which gives the number of milliseconds elapsed
 *  since the first call to sched_elapsed(). But any monotonic increasing
 *  value can be used like a counter or a microsecond resolution clock.
 * ----------------------------------------------------------------------------
 */
void sched_cycle(scheduler_t *sched, uint32_t timestamp )
{
  fiber_t *pf, *opf;

  debug("scheduler %p cycle %d\n", sched, timestamp);

  /* register timestamp */
  sched->timestamp = timestamp;
  
  /* invoke hook */
  if ( sched->pf_pre_hook ) {
    sched->pf_pre_hook( sched, sched->extra );
  }
  
  /* FIBER_INIT to FIBER_RUNNING */
  for( pf = sched->lists[FIBER_INIT]; pf != NULL; pf = pf->next) {
    /* check the state of fiber
     * skip all fibers that are not in expected state */
    if ( pf->state != FIBER_INIT ) {
      warn("Fiber %d not in FIBER_INIT !\n", pf->fid);
      continue;
    }
    
    /* Boot the fiber
     *  - it installs the stack
     *  - triggers a SIGUSR1 signal to jump in trampoline code
     *  - returns to */
    schedBoot(pf);

    /* Mark fiber as running
     * we do it before init in case yield() gets called from
     * init ! */
    pf->state = FIBER_RUNNING;

    /* Call init function pointer if supplied */
    if ( pf->pf_init ) {
      pf->pf_init(pf);
    }
  }

  /* Move fibers from init list depending on their new state */
  schedCleanList( sched, FIBER_INIT );
  
  /* process FIBER_SUSPEND fibers */
  schedProcessPredicates( sched );
  schedCleanList( sched, FIBER_SUSPEND );

  /* dispatch FIBER_RUNNING fibers */
  schedDispatch( sched );
  schedCleanList( sched, FIBER_RUNNING );
  
  /* FIBER_TERM to FIBER_DONE */
  for( pf = sched->lists[FIBER_TERM]; pf != NULL; pf = pf->next ) {
    /* check the state of fiber */
    /* skip all fibers that are not in expected state */
    if ( pf->state != FIBER_TERM ) {
      warn("Fiber %d not in FIBER_TERM !\n", pf->fid);
      continue;
    }

    /* call term function pointer if supplied */
    if ( pf->pf_term ) {
      pf->pf_term(pf);
    }

    /* update fiber state
     * force state to done */
    pf->state = FIBER_DONE;
  }
  schedCleanList( sched, FIBER_TERM );

  /* FIBER_DONE list */
  for( pf = sched->lists[FIBER_DONE]; pf != NULL; pf = opf ) {
    /* prepare next step */
    opf = pf->next;
    
    /* Check the state of fiber
     * Skip all fibers that are not in expected state */
    if ( pf->state != FIBER_DONE ) {
      error("Fiber %d not in FIBER_DONE state!\n", pf->fid);
      continue;
    }

    /* Remove from scheduler and free stack */
    schedRemoveFiber( sched, pf);
    
    /* Call done function pointer if supplied */
    if ( pf->pf_done ) {
      pf->pf_done(pf);
    }
  }
  sched->lists[FIBER_DONE] = NULL;
  
  /* Invoke hook */
  if ( sched->pf_post_hook ) {
    sched->pf_post_hook( sched, sched->extra );
  }
}

/* ----------------------------------------------------------------------------
 * Dispatching next fiber whose state is FIBER_RUNNING
 * Naive implementation running all fibers in turn without trying to share time 
 * between them
 * ----------------------------------------------------------------------------*/
static void schedDispatch(scheduler_t *sched)
{
  fiber_t *pf, *opf;

  /* scan ready to run fibers */
  for( pf = sched->lists[FIBER_RUNNING], opf = NULL; pf; pf = opf) {
    opf = pf->next;

    trace("Will run fiber %p (fid = %d)\n", pf, pf->fid);
    
    /* Save the current state */
    if ( setjmp( sched->context ) ) {
      /* none running */
      sched->running = NULL;
      
      /* The fiber yielded the context to us
       * the fiber run() method has returned */
      if ( pf->state == FIBER_DONE ) {
	/* If we get here, the fiber returned and is done! */
	debug( "Fiber %d returned, cleaning up.\n", pf->fid );
      }
      else if ( pf->state == FIBER_TERM ) {
	/* If we get here, the fiber returned and is done! */
	debug( "Fiber %d kill, cleaning up.\n", pf->fid );
      }
      else {
	trace( "Fiber %d yielded execution.\n", pf->fid );
      }
    }
    else {
      debug( "Switching to fiber %d\n", pf->fid );
      sched->running = pf;
      longjmp( pf->context, 1 );
    }
  }
  
  sched->running = NULL;
}

/* contains the currently booting fiber */
/* marked as volatile to prevent access optimization by the compiler */
static volatile fiber_t *bootingFiber = NULL;

/*
 * ----------------------------------------------------------------------------
 *  Start a fiber
 *  This function executes on the fiber stack
 * ----------------------------------------------------------------------------
 */
static void fiberStart(int ignored)
{
  /* ugly but needed ... retreive fiber from global */
  fiber_t *fiber = (fiber_t *) bootingFiber;
  
  debug("starting fiber %p (fid = %d).\n", fiber, fiber->fid);
    
  /* we are running on fiber stack
   * the stack was set up before by fiberBoot */
  if ( !setjmp(fiber->context) ) {
    /* return to boot code on the scheduler stack */
    return;
  }
  else {
    /* we re-enter here
     * the point is that the stack is still there even if the SIGUSR1
     * signal handler has already returned */
    debug("Reentering...\n");
  }

  /* start running */
  fiber->pf_run(fiber);

  /* fiber function returned
   * jump back to scheduler */
  debug("Fiber %d now in state FIBER_DONE.\n", fiber->fid);
  fiber->state = FIBER_DONE;
  longjmp(fiber->scheduler->context, FIBER_DONE);
}

/*
 * ----------------------------------------------------------------------------
 *  Code to boot a fiber 
 *  It executes on the scheduler stack.
 * ----------------------------------------------------------------------------
 */
static int schedBoot( fiber_t *fiber )
{
  struct sigaction handler;
  struct sigaction oldHandler;
	
  stack_t stack;
  stack_t oldStack;

  volatile fiber_t *backup;

  debug("booting fiber %p (fid = %d)\n", fiber, fiber->fid);
  
  /* Create the new stack */
  stack.ss_flags = 0;
  stack.ss_size = fiber->stacksz;
  stack.ss_sp = fiber->stack;
  debug( "Stack address for fiber = 0x%x\n", stack.ss_sp );
	
  /* Install the new stack for the signal handler */
  if ( sigaltstack( &stack, &oldStack ) ) {
    error( "Error: sigaltstack failed.");
    return FIBER_SIGNALERROR;
  }
	
  /* Install the signal handler
   * Sigaction *must* be used so we can specify SA_ONSTACK */
  handler.sa_handler = &fiberStart;
  handler.sa_flags = SA_ONSTACK;
  sigemptyset( &handler.sa_mask );

  if ( sigaction( SIGUSR1, &handler, &oldHandler ) ) {
    error( "Error: sigaction failed.");
    return FIBER_SIGNALERROR;
  }

  /* Prepare for booting */
  backup = bootingFiber;
  bootingFiber = fiber;
  
  /* Call the handler on the new stack */
  if ( raise( SIGUSR1 ) ) {
    error( "Error: raise failed.\n");
    bootingFiber = backup;
    return FIBER_SIGNALERROR;
  }
    
  /* we enter here through a longjump from fiberStart
   * Restore the original stack and handler */
  sigaltstack( &oldStack, 0 );
  sigaction( SIGUSR1, &oldHandler, 0 );

  bootingFiber = backup;
  return FIBER_OK;
}

/* ----------------------------------------------------------------------------
 * Called by a fiber to give back the processor
 * ----------------------------------------------------------------------------*/
int fiberYield(fiber_t *fiber)
{
  /* If we are in a fiber, switch to the main context */
  if ( fiberCheckExist(fiber) == FIBER_OK ) {
    scheduler_t *sched = fiber->scheduler;

    /* xtra check - only the currently running fiber can yield */
    if ( (sched != NULL) && (sched->running == fiber)) {
      /* Store the current state */
      if ( setjmp( fiber->context ) ) {
	/* Returning via longjmp (resume) */
	debug( "Fiber %d resuming...\n", fiber->fid );
      }
      else {
	debug( "Fiber %d yielding the processor...\n", fiber->fid );
	/* Saved the state: Let's switch back to the main state */
	longjmp( sched->context, 1 );
      }
    }
    else {
      error("Yield : fiber %d called yield while not running (state %d)!\n", fiber->fid, fiber->state);
      return FIBER_ILLEGAL_STATE;
    }
  }
  else {
    error("Yield from NULL fiber");
    return FIBER_ERROR;
  }
  return FIBER_OK;
}

/* ----------------------------------------------------------------------------
 * fiber_yield
 * ----------------------------------------------------------------------------*/
int fiber_yield(fiber_t *fiber)
{
  return fiber_wait( fiber, 0);
}

/* ----------------------------------------------------------------------------
 * function to wait for a given amount of time
 * ----------------------------------------------------------------------------*/
int fiber_wait(fiber_t *fiber, uint32_t msec)
{
  predicate_t pred;

  /* clean predicate */
  memset( &pred, 0, sizeof(pred));

  /* if msec is 0 we just yield the processor
   * and will be called back in next step */
  if ( msec > 0 ) {
    /* fill predicate */
    pred.deadline = sched_timestamp( fiber->scheduler ) + msec;
    pred.fiber = fiber;
    pred.state = PREDICATE_ACTIVE;

    /* link fiber to predicate */
    fiber->predicate = &pred;

    /* change fiber state */
    fiber->state = FIBER_SUSPEND;
  }

  /* give processor */
  fiberYield( fiber );

  /* point reached when resuming fiber execution */
  if ( msec > 0 ) {
    return FIBER_TIMEOUT;
  }
  else {
    return FIBER_OK;
  }
}


/* ----------------------------------------------------------------------------
 * fonction to wait for a given predicate
 * ----------------------------------------------------------------------------*/
int fiber_wait_for_cond( fiber_t *fiber, uint32_t msec, pf_check_t pfun, void *pfunarg)
{
  predicate_t pred;

  /* check argument */
  if ( pfun == NULL ) {
    /* if bad - yield and return error */
    fiberYield( fiber );
    return FIBER_INVALID_PREDICATE;
  }

  /* clean predicate */
  memset( &pred, 0, sizeof(pred));

  /* fill predicate */
  pred.fiber = fiber;
  pred.state = PREDICATE_ACTIVE;
  if ( msec > 0 ) {
    pred.deadline = sched_timestamp( fiber->scheduler ) + msec;
  }
  else {
    pred.deadline = 0;
  }
  pred.data = pfunarg;
  pred.pf_check = pfun;

  /* link fiber to predicate */
  fiber->predicate = &pred;

  /* change fiber state */
  fiber->state = FIBER_SUSPEND;

  /* yield */
  fiberYield( fiber );

  /* execution resume here */
  if ( pred.state == PREDICATE_FIRED ) {
    return FIBER_TIMEOUT;
  }
  
  return FIBER_OK;
}

struct join_check_data {
  fiber_t *other;
  int fid;
};

/* ----------------------------------------------------------------------------
 * Check function used during join operation
 * ----------------------------------------------------------------------------*/
static int fiber_join_check( fiber_t *fiber, void *data)
{
  struct join_check_data *jdata  = (fiber_t*) data;
  fiber_t** fibers = fiber->scheduler->fibers;
  return ( fibers[jdata->fid] != jdata->other );
}

/* ----------------------------------------------------------------------------
 * fonction to wait for a given fiber to finish
 * part of public API
 * ----------------------------------------------------------------------------*/
int fiber_join( fiber_t *fiber, uint32_t msec, fiber_t *other)
{
  predicate_t pred;
  struct join_check_data jdata;
  
  /* check argument */
  if ( other == NULL ) {
    /* if bad - yield and return error */
    fiberYield( fiber );
    return FIBER_NO_SUCH_FIBER;
  }

  /* clean predicate */
  memset( &pred, 0, sizeof(pred));

  /* fill predicate */
  pred.fiber = fiber;
  pred.state = PREDICATE_ACTIVE;
  if ( msec > 0 ) {
    pred.deadline = sched_timestamp( fiber->scheduler ) + msec;
  }
  else {
    pred.deadline = 0;
  }
  jdata.other = other;
  jdata.fid = other->fid;
  pred.data = (void*) &jdata;
  pred.pf_check = &fiber_join_check;

  /* link fiber to predicate */
  fiber->predicate = &pred;

  /* change fiber state */
  fiber->state = FIBER_SUSPEND;

  /* yield */
  fiberYield( fiber );

  /* execution resume here */
  if ( pred.state == PREDICATE_FIRED ) {
    return FIBER_TIMEOUT;
  }
  
  return FIBER_OK;
}


struct var_check_data
{
  int *addr;
  int  value;
};

static int fiber_var_check( fiber_t *fiber, void *data )
{
  struct var_check_data *vcd = (struct var_check_data*) data;
  return (*vcd->addr == vcd->value);
}

/* 
 * fonction to wait for a given predicate
 */
int fiber_wait_for_var( fiber_t *fiber, uint32_t msec, int *addr, int value)
{
  struct var_check_data vcd;
  predicate_t pred;
  
  /* check argument */
  if ( addr == NULL ) {
    /* if bad - yield and return error */
    fiberYield( fiber );
    return FIBER_INVALID_PREDICATE;
  }

  /* clean predicate */
  memset( &pred, 0, sizeof(pred));

  /* fill predicate */
  pred.fiber = fiber;
  pred.state = PREDICATE_ACTIVE;
  if ( msec > 0 ) {
    pred.deadline = sched_timestamp( fiber->scheduler ) + msec;
  }
  else {
    pred.deadline = 0;
  }
  pred.data = &vcd;
  pred.pf_check = &fiber_var_check;

  /* fill values used for checking */
  vcd.addr = addr;
  vcd.value = value;
  
  /* link fiber to predicate */
  fiber->predicate = &pred;

  /* change fiber state */
  fiber->state = FIBER_SUSPEND;

  /* yield */
  fiberYield( fiber );

  /* execution resume here */
  if ( pred.state == PREDICATE_FIRED ) {
    return FIBER_TIMEOUT;
  }
  
  return FIBER_OK;
}


/*
 * ---------------------------------------------------------------------------
 *  fiber_new --
 *  
 *  Allocates a fiber object with malloc and initializes it.
 *  It sets its state to FIBER_EGG.
 * ---------------------------------------------------------------------------
 */
fiber_t *fiber_new(pf_run_t run_func, void *extra)
{
  fiber_t *res;

  /* check if there are some fibers available */
  
  /* allocate */
  res = (fiber_t*) malloc(sizeof(*res));
  if ( res == NULL ) {
    return NULL;
  }

  /* init data structure */
  memset( res, 0, sizeof(*res));
  res->extra = extra;
  res->pf_run = run_func;
  res->state = FIBER_EGG;
  
  return res;
}

/*
 * ---------------------------------------------------------------------------
 *  fiber_set_init_func --
 * ---------------------------------------------------------------------------
 */
int fiber_set_init_func( fiber_t *fiber, pf_init_t init_func)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state != FIBER_EGG ) {
    return FIBER_ILLEGAL_STATE;
  }
  fiber->pf_init = init_func;
  return FIBER_OK;
}

/*
 * ---------------------------------------------------------------------------
 *  fiber_set_term_func --
 * ---------------------------------------------------------------------------
 */
int fiber_set_term_func( fiber_t *fiber, pf_term_t term_func)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state != FIBER_EGG ) {
    return FIBER_ILLEGAL_STATE;
  }
  fiber->pf_term = term_func;
  return FIBER_OK;
}

/*
 * ---------------------------------------------------------------------------
 *  fiber_set_done_func --
 * ---------------------------------------------------------------------------
 */
int fiber_set_done_func( fiber_t *fiber, pf_done_t done_func)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state != FIBER_EGG ) {
    return FIBER_ILLEGAL_STATE;
  }
  fiber->pf_done = done_func;
  return FIBER_OK;
}

/*
 * ---------------------------------------------------------------------------
 *  fiber_set_int_func --
 * ---------------------------------------------------------------------------
 */
scheduler_t *fiber_get_scheduler(fiber_t *fiber)
{
  if ( fiber == NULL ) {
    return NULL;
  }
  return fiber->scheduler;
}

/*
 * ---------------------------------------------------------------------------
 *  fiber_get_extra --
 *
 *  Retreive the pointer of custom data attached to the fiber.
 *  Returns NULL if no custom data were attached.
 * ---------------------------------------------------------------------------
 */
void *fiber_get_extra(fiber_t *fiber)
{
  if ( fiber == NULL ) {
    return NULL;
  }
  return fiber->extra;
}

/*
 * ---------------------------------------------------------------------------
 *  fiber_set_int_func --
 * ---------------------------------------------------------------------------
 */
int fiber_set_extra(fiber_t *fiber, void *extra)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  fiber->extra = extra;
  return FIBER_OK;
}

/* 
 * ---------------------------------------------------------------------------
 * must be called before starting the fiber
 * if not called a defult stack size of 64k is used
 * ---------------------------------------------------------------------------
 */
int fiber_set_stack_size( fiber_t *fiber, uint32_t stacksz )
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state != FIBER_EGG ) {
    return FIBER_ILLEGAL_STATE;
  }
  if ( stacksz < 2048 || stacksz >= 1024*1024*1024 ) {
    return FIBER_INVALID_STACK_SIZE;
  }
  fiber->stacksz = stacksz;
  return FIBER_OK;
}

/*
 * ---------------------------------------------------------------------------
 * starts a newly created fiber
 * ---------------------------------------------------------------------------
 */
int fiber_start( scheduler_t *sched, fiber_t *fiber )
{
  uint32_t h;
  
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state != FIBER_EGG ) {
    return FIBER_ILLEGAL_STATE;
  }
  if ( sched->nfibers >= MAXFIBERS ) {
    return FIBER_TOO_MANY_FIBERS;
  }
  
  /* allocate stack for fiber */
  if ( fiber->stacksz == 0 ) {
    fiber->stacksz = DEFAULTSTACKSIZE;
  }
  fiber->stack = malloc( fiber->stacksz );
  if ( fiber->stack == NULL ) {
    return FIBER_MEMORY_ALLOCATION_ERROR;
  }

  /* link fiber and scheduler */
  fiber->scheduler = sched;

  /* move fiber to init state */
  fiber->state = FIBER_INIT;
  
  /* insert in hash table */
  h = fiberHash( fiber ) % ARRAYSIZE;
  while( sched->fibers[h] != NULL ) {
    h = (h+1) % ARRAYSIZE;
  }
  fiber->fid = h;
  sched->fibers[h] = fiber;

  /* insert in init list */
  fiber->next = sched->lists[FIBER_INIT];
  sched->lists[FIBER_INIT] = fiber;

  /* increase fiber count */
  ++sched->nfibers;
  
  /* all good */
  return FIBER_OK;
}

/*
 * ---------------------------------------------------------------------------
 * explicit stop of a fiber
 * ---------------------------------------------------------------------------
 */
int fiber_stop( fiber_t *fiber )
{
  if ( fiberCheckExist(fiber) != FIBER_OK ) {
    return FIBER_NO_SUCH_FIBER;
  }
  /* can't stop a fiber that hasn't been started yet
   * can't stop a fiber that is already in TERM or DONE state
   */
  if ( fiber->state < FIBER_TERM && fiber->state > FIBER_INIT ) {
    fiber->state = FIBER_TERM;
    return FIBER_OK;
  }
  else {
    return FIBER_ILLEGAL_STATE;
  }
}

/* ---------------------------------------------------------------------------
 * free a fiber object
 * if FIBER_OK is returned the associated memory is freed
 * ---------------------------------------------------------------------------*/
int fiber_free( fiber_t *fiber )
{
  if ( fiberCheckExist(fiber) != FIBER_OK ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state > FIBER_EGG || fiber->state < FIBER_DONE ) {
    return FIBER_ILLEGAL_STATE;
  }
  if ( fiber->stack ) {
    free( fiber->stack);
    fiber->stack = NULL;
  }
  free(fiber);
  return FIBER_OK;
}

/* ---------------------------------------------------------------------------
 * create a new scheduler
 * ---------------------------------------------------------------------------*/
scheduler_t *sched_new()
{
  scheduler_t *res;
  res = (scheduler_t*) malloc(sizeof(scheduler_t));
  if ( !res ) {
    return NULL;
  }
  memset(res, 0, sizeof(*res));
  return res;
}

/* ---------------------------------------------------------------------------
 * free scheduler
 * ---------------------------------------------------------------------------*/
int sched_free( scheduler_t *sched )
{
  free( sched );
  return FIBER_OK;
}

/* ---------------------------------------------------------------------------
 * Stops all fibers.
 *
 * This function is used to stop all the fibers at once, for example when
 * the program exits.
 *
 * A call to sched_cycle() is required to really free the resources
 * used by fibers because the call to "fiber_term()" and "fiber_done()"
 * functions which are meant to release resources used by fibers.
 *
 * ---------------------------------------------------------------------------*/
void sched_stop( scheduler_t *sched )
{
  fiber_t *pf, *opf;
  int state;

  for (state = FIBER_INIT; state < FIBER_TERM; state++ ) {
    for( pf = sched->lists[state]; pf != NULL; pf = pf->next) {
      pf->state = FIBER_TERM;
    }
    schedCleanList(sched, state );
  }
}


/* --------------------------------------------------------------------------
 *   Returns number of msec elapsed since first call to this function
 * --------------------------------------------------------------------------*/
uint32_t sched_elapsed()
{
  static struct timeval start = {0,0};
  struct timeval now;
  
  gettimeofday(&now, NULL);
  if ( start.tv_sec == 0 && start.tv_usec == 0) {
    start = now;
    return 0;
  }
  
  return (now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000;
}


/* --------------------------------------------------------------------------
 *   Returns number of fibers in scheduler
 * --------------------------------------------------------------------------*/
int sched_numfibers( scheduler_t *sched)
{
  return sched->nfibers;
}

/* --------------------------------------------------------------------------
 *  It returns 0 if there are fibers in FIBER_RUNNING, FIBER_INIT, FIBER_TERM
 *  or FIBER_DONE state.
 *
 *  If no fibers are running it scans the list of fibers in FIBER_SUSPEND 
 *  state. Some will have set up an alarm at a specific time in the future. 
 *  This function returns the next pending deadline.
 *
 *  If all suspended fibers can wait indefinitly for an event of interest,
 *  the function returns UINT_MAX.
 * --------------------------------------------------------------------------*/
uint32_t sched_deadline( scheduler_t *sched )
{
  uint32_t res = UINT_MAX;
  fiber_t *fiber;
  
  if ( sched == NULL ) {
    return res;
  }
  if ( sched->lists[FIBER_INIT] != NULL ) return 0;
  if ( sched->lists[FIBER_RUNNING] != NULL ) return 0;
  if ( sched->lists[FIBER_TERM] != NULL ) return 0;
  if ( sched->lists[FIBER_DONE] != NULL ) return 0;

  for( fiber = sched->lists[FIBER_SUSPEND]; fiber; fiber = fiber->next ) {
    if ( fiber->predicate == NULL ) continue;
    if ( fiber->predicate->state != PREDICATE_ACTIVE ) continue;
    if ( fiber->predicate->deadline < res ) res = fiber->predicate->deadline;
  }

  return res;
}

/* --------------------------------------------------------------------------
 *  sched_timestamp --
 * --------------------------------------------------------------------------*/
uint32_t sched_timestamp( scheduler_t *sched )
{
  if ( sched == NULL ) return 0;
  return sched->timestamp;
}
