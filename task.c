#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <malloc.h>
#include <string.h>

#include <stdio.h>

#include "task.h"



// forward
static int schedBoot( fiber_t *fiber );
static void schedDispatch(scheduler_t *sched);


// hashing pointer value
// source: internet - to be check on 64-bit and 32-bit platform
// I don't know if it has many collisions
static uint32_t fiberHash(fiber_t *fiber)
{
  uintptr_t ad = (uintptr_t) fiber;
  return (size_t) ((13*ad) ^ (ad >> 15));
}

// check existence of fiber
static int fiberCheckExist(fiber_t *fiber)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->scheduler != NULL ) {
    // check if part of hash table
    if ( (fiber->fid < ARRAYSIZE) && (fiber->scheduler->fibers[fiber->fid] == fiber) ) {
      return FIBER_OK;
    }
    return FIBER_NO_SUCH_FIBER;
  }
  return FIBER_OK;
}

// check a predicate
// and update its state depending on the result
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


// Process a predicate
static void schedProcessPredicate(scheduler_t *sched, predicate_t *pp, uint32_t elapsed)
{
  // get linked fiber and timer
  fiber_t *pf = pp->fiber;
  int res;

  // check if an error occured
  if ( pf == NULL ) {
    error("Fiber linked to predicate %p is NULL\n", pf);
    return;
  }

  res = schedCheckPredicate( sched, pp, elapsed );
  if ( res ) {
    pf->state = FIBER_RUNNING;
  }
}

// process all predicates
static void schedProcessPredicates(scheduler_t *sched)
{
  uint32_t now;
  fiber_t *pf;

  now = sched_elapsed();  // current elapsed time in msec
  
  for( pf = sched->lists[FIBER_SUSPEND]; pf != NULL; pf = pf->next) {
    if ( pf->state != FIBER_SUSPEND ) {
      error( "fiber %d not in FIBER_SUSPEND state\n", pf->fid );
      continue;
    }
    
    if ( pf->predicate != NULL ) {
      schedProcessPredicate( sched, pf->predicate, now);
    }
    else {
      // a suspended fiber with no predicate is a fiber
      // which called yield() explicitely to give processor
      // to other tasks.
      pf->state = FIBER_RUNNING;
    }
  }
}

// clean a list of fibers
static void schedCleanList(scheduler_t *sched, uint32_t state )
{
  fiber_t *opf, *pf;
  fiber_t *head = NULL;
  
  for( pf = sched->lists[state] ; pf; pf = opf ) {
    opf = pf->next;
    
    if ( pf->state == state ) {
      // note that the list is reversed doing this
      pf->next = head;
      head = pf;

      debug( "fiber %p (fid = %d) still in state %d.\n",
	     pf, pf->fid, state);
    }
    else {
      // insert current fiber at the head of the list
      // of target state
      pf->next = sched->lists[pf->state];
      sched->lists[pf->state] = pf;

      debug( "moving fiber %p (fid = %d) from %d to %d.\n",
	     pf, pf->fid, state, pf->state);
    }
  }

  // store new list
  sched->lists[state] = head;
}

// removes a fiber from scheduler
// doesn't free memory associated with fiber
static int schedRemoveFiber(scheduler_t *sched, fiber_t *fiber)
{
  if ( sched == NULL ) {
    return FIBER_ERROR;
  }
  if ( fiber == NULL || fiber->fid >= ARRAYSIZE) {
    return FIBER_NO_SUCH_FIBER;
  }

  if (fiber->scheduler->fibers[fiber->fid] == fiber) {
    fiber->scheduler->fibers[fiber->fid] = NULL;
    --sched->nfibers;
    return FIBER_OK;
  }

  error("Fiber %d not found.\n", fiber->fid);
  return FIBER_NO_SUCH_FIBER;
}

// scheduler cycle
void sched_cycle(scheduler_t *sched, uint32_t elapsed)
{
  fiber_t *pf, *opf;

  debug("scheduler %p cycle %d\n", sched, elapsed);
  
  // invoke hook
  if ( sched->pf_pre_hook ) {
    sched->pf_pre_hook( sched, sched->extra );
  }
  
  // FIBER_INIT to FIBER_RUNNING
  for( pf = sched->lists[FIBER_INIT]; pf != NULL; pf = pf->next) {
    // check the state of fiber
    // skip all fibers that are not in expected state
    if ( pf->state != FIBER_INIT ) {
      warn("Fiber %d not in FIBER_INIT !\n", pf->fid);
      continue;
    }
    
    // boot the fiber
    //  - it installs the stack
    //  - triggers a SIGUSR1 signal to jump in trampoline code
    //  - returns to 
    schedBoot(pf);

    // mark fiber as running
    // we do it before init in case yield() gets called from
    // init !
    pf->state = FIBER_RUNNING;

    // call init function pointer if supplied
    if ( pf->pf_init ) {
      pf->pf_init(pf);
    }
  }

  // Move fibers from init list depending on their new state
  schedCleanList( sched, FIBER_INIT );
  
  // process FIBER_SUSPEND fibers
  schedProcessPredicates( sched );
  schedCleanList( sched, FIBER_SUSPEND );

  // dispatch FIBER_RUNNING fibers
  schedDispatch( sched );
  schedCleanList( sched, FIBER_RUNNING );
  
  // FIBER_TERM to FIBER_DONE
  for( pf = sched->lists[FIBER_TERM]; pf != NULL; pf = pf->next ) {
    // check the state of fiber
    // skip all fibers that are not in expected state
    if ( pf->state != FIBER_TERM ) {
      warn("Fiber %d not in FIBER_TERM !\n", pf->fid);
      continue;
    }

    // call term function pointer if supplied
    if ( pf->pf_term ) {
      pf->pf_term(pf);
    }

    // update fiber state
    // force state to done
    pf->state = FIBER_DONE;
  }
  schedCleanList( sched, FIBER_TERM );

  // FIBER_DONE list
  for( pf = sched->lists[FIBER_DONE]; pf != NULL; pf = opf ) {
    // prepare next step
    opf = pf->next;
    
    // check the state of fiber
    // skip all fibers that are not in expected state
    if ( pf->state != FIBER_DONE ) {
      error("Fiber %d not in FIBER_DONE state!\n", pf->fid);
      continue;
    }
    
    // call done function pointer if supplied
    if ( pf->pf_done ) {
      pf->pf_done(pf);
    }

    // destroy fiber
    schedRemoveFiber( sched, pf);
  }
  sched->lists[FIBER_DONE] = NULL;
  
  // invoke hook
  if ( sched->pf_post_hook ) {
    sched->pf_post_hook( sched, sched->extra );
  }
}

// dispatching next fiber whose state is FIBER_RUNNING
// naive implementation running all fibers in turn without trying to share time between them
static void schedDispatch(scheduler_t *sched)
{
  fiber_t *pf, *opf;

  // FIBER_INIT to FIBER_RUNNING
  for( pf = sched->lists[FIBER_RUNNING], opf = NULL; pf; pf = opf) {
    opf = pf->next;

    trace("Will run fiber %p (fid = %d)\n", pf, pf->fid);
    
    // Save the current state
    if ( setjmp( sched->context ) ) {
      // none running
      sched->running = NULL;
      
      // The fiber yielded the context to us
      // the fiber run() method has returned
      if ( pf->state == FIBER_DONE ) {
	// If we get here, the fiber returned and is done!
	debug( "Fiber %d returned, cleaning up.\n", pf->fid );
      }
      else if ( pf->state == FIBER_TERM ) {
	// If we get here, the fiber returned and is done!
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

// contains the currently booting fiber
// marked as volatile to prevent access optimization by the compiler
static volatile fiber_t *bootingFiber = NULL;

// code that run a fiber
static void fiberStart(int ignored)
{
  // ugly but needed ... retreive fiber from global
  fiber_t *fiber = (fiber_t *) bootingFiber;
  
  debug("starting fiber %p (fid = %d).\n", fiber, fiber->fid);
    
  // we are running on fiber stack
  // the stack was set up before by fiberBoot
  if ( !setjmp(fiber->context) ) {
    // return to boot code
    return;
  }
  else {
    // we re-enter here
    // the point is that the stack is still there even if the SIGUSR1
    // signal handler has already returned
    debug("Reentering...\n");
  }

  // start running
  fiber->pf_run(fiber);

  // fiber function returned
  // jump back to scheduler
  debug("Fiber %d now in state FIBER_DONE.\n", fiber->fid);
  fiber->state = FIBER_DONE;
  longjmp(fiber->scheduler->context, FIBER_DONE);
}

// code to boot a fiber
static int schedBoot( fiber_t *fiber )
{
  struct sigaction handler;
  struct sigaction oldHandler;
	
  stack_t stack;
  stack_t oldStack;

  volatile fiber_t *backup;

  debug("booting fiber %p (fid = %d)\n", fiber, fiber->fid);
  
  // Create the new stack
  stack.ss_flags = 0;
  stack.ss_size = fiber->stacksz;
  stack.ss_sp = fiber->stack;
  debug( "Stack address for fiber = 0x%x\n", stack.ss_sp );
	
  // Install the new stack for the signal handler
  if ( sigaltstack( &stack, &oldStack ) ) {
    error( "Error: sigaltstack failed.");
    return FIBER_SIGNALERROR;
  }
	
  // Install the signal handler
  // Sigaction *must* be used so we can specify SA_ONSTACK
  handler.sa_handler = &fiberStart;
  handler.sa_flags = SA_ONSTACK;
  sigemptyset( &handler.sa_mask );

  if ( sigaction( SIGUSR1, &handler, &oldHandler ) ) {
    error( "Error: sigaction failed.");
    return FIBER_SIGNALERROR;
  }

  // prepare for booting
  backup = bootingFiber;
  bootingFiber = fiber;
  
  // Call the handler on the new stack
  if ( raise( SIGUSR1 ) ) {
    error( "Error: raise failed.\n");
    bootingFiber = backup;
    return FIBER_SIGNALERROR;
  }
    
  // we enter here through a longjump from fiberStart
  // Restore the original stack and handler
  sigaltstack( &oldStack, 0 );
  sigaction( SIGUSR1, &oldHandler, 0 );

  bootingFiber = backup;
  return FIBER_OK;
}

// called by a fiber to give back the processor
int fiberYield(fiber_t *fiber)
{
  // If we are in a fiber, switch to the main context
  if ( fiberCheckExist(fiber) == FIBER_OK ) {
    scheduler_t *sched = fiber->scheduler;

    // xtra check - only the currently running fiber can yield
    if ( (sched != NULL) && (sched->running == fiber)) {
      // Store the current state
      if ( setjmp( fiber->context ) ) {
	// Returning via longjmp (resume)
	debug( "Fiber %d resuming...\n", fiber->fid );
      }
      else {
	debug( "Fiber %d yielding the processor...\n", fiber->fid );
	// Saved the state: Let's switch back to the main state
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

// --------------------------------------------------------------------------
int fiber_yield(fiber_t *fiber)
{
  return fiber_wait( fiber, 0);
}

// fonction to wait for a given amount of time
// part of public API
int fiber_wait(fiber_t *fiber, uint32_t msec)
{
  predicate_t pred;

  // clean predicate
  memset( &pred, 0, sizeof(pred));

  // if msec is 0 we just yield the processor
  // and will be called back in next step
  if ( msec > 0 ) {
    // fill predicate
    pred.deadline = sched_elapsed() + msec;
    pred.fiber = fiber;
    pred.state = PREDICATE_ACTIVE;

    // link fiber to predicate
    fiber->predicate = &pred;

    // change fiber state
    fiber->state = FIBER_SUSPEND;
  }

  // give processor
  fiberYield( fiber );

  // point reached when resuming fiber execution
  if ( msec > 0 ) {
    return FIBER_TIMEOUT;
  }
  else {
    return FIBER_OK;
  }
}

// --------------------------------------------------------------------------

// fonction to wait for a given predicate
// part of public API
int fiber_wait_for_cond( fiber_t *fiber, uint32_t msec, pf_check_t pfun, void *pfunarg)
{
  predicate_t pred;

  // check argument
  if ( pfun == NULL ) {
    // if bad - yield and return error
    fiberYield( fiber );
    return FIBER_INVALID_PREDICATE;
  }

  // clean predicate
  memset( &pred, 0, sizeof(pred));

  // fill predicate
  pred.fiber = fiber;
  pred.state = PREDICATE_ACTIVE;
  if ( msec > 0 ) {
    pred.deadline = sched_elapsed() + msec;
  }
  else {
    pred.deadline = 0;
  }
  pred.data = pfunarg;
  pred.pf_check = pfun;

  // link fiber to predicate
  fiber->predicate = &pred;

  // change fiber state
  fiber->state = FIBER_SUSPEND;

  // yield
  fiberYield( fiber );

  // execution resume here
  if ( pred.state == PREDICATE_FIRED ) {
    return FIBER_TIMEOUT;
  }
  
  return FIBER_OK;
}


// --------------------------------------------------------------------------

// check function used during join operation
static int fiber_join_check( fiber_t *fiber, void *data)
{
  fiber_t*  other  = (fiber_t*) data;
  fiber_t** fibers = fiber->scheduler->fibers;

  // try to find the fiber in hashtable
  uint32_t h = fiberHash(other) % ARRAYSIZE;
  while ( fibers[h] ) {
    if ( fibers[h] == other ) {
      return 0;
    }
    h = (h+1) % ARRAYSIZE;
  }
  
  return 1;
}

// fonction to wait for a given fiber to finish
// part of public API
int fiber_join( fiber_t *fiber, uint32_t msec, fiber_t *other)
{
  predicate_t pred;

  // check argument
  if ( other == NULL ) {
    // if bad - yield and return error
    fiberYield( fiber );
    return FIBER_NO_SUCH_FIBER;
  }

  // clean predicate
  memset( &pred, 0, sizeof(pred));

  // fill predicate
  pred.fiber = fiber;
  pred.state = PREDICATE_ACTIVE;
  if ( msec > 0 ) {
    pred.deadline = sched_elapsed() + msec;
  }
  else {
    pred.deadline = 0;
  }
  pred.data = (void*) other;
  pred.pf_check = &fiber_join_check;

  // link fiber to predicate
  fiber->predicate = &pred;

  // change fiber state
  fiber->state = FIBER_SUSPEND;

  // yield
  fiberYield( fiber );

  // execution resume here
  if ( pred.state == PREDICATE_FIRED ) {
    return FIBER_TIMEOUT;
  }
  
  return FIBER_OK;
}


// --------------------------------------------------------------------------
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

// fonction to wait for a given predicate
// part of public API
int fiber_wait_for_var( fiber_t *fiber, uint32_t msec, int *addr, int value)
{
  struct var_check_data vcd;
  predicate_t pred;
  
  // check argument
  if ( addr == NULL ) {
    // if bad - yield and return error
    fiberYield( fiber );
    return FIBER_INVALID_PREDICATE;
  }

  // clean predicate
  memset( &pred, 0, sizeof(pred));

  // fill predicate
  pred.fiber = fiber;
  pred.state = PREDICATE_ACTIVE;
  if ( msec > 0 ) {
    pred.deadline = sched_elapsed() + msec;
  }
  else {
    pred.deadline = 0;
  }
  pred.data = &vcd;
  pred.pf_check = &fiber_var_check;

  // fill values used for checking
  vcd.addr = addr;
  vcd.value = value;
  
  // link fiber to predicate
  fiber->predicate = &pred;

  // change fiber state
  fiber->state = FIBER_SUSPEND;

  // yield
  fiberYield( fiber );

  // execution resume here
  if ( pred.state == PREDICATE_FIRED ) {
    return FIBER_TIMEOUT;
  }
  
  return FIBER_OK;
}


// --------------------------------------------------------------------------
fiber_t *fiber_new(pf_run_t run_func, void *extra)
{
  fiber_t *res;

  // check if there are some fibers available
  
  // allocate
  res = (fiber_t*) malloc(sizeof(*res));
  if ( res == NULL ) {
    return NULL;
  }

  // init data structure
  memset( res, 0, sizeof(*res));
  res->extra = extra;
  res->pf_run = run_func;
  res->state = FIBER_EGG;
  
  return res;
}

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

void *fiber_get_extra(fiber_t *fiber)
{
  if ( fiber == NULL ) {
    return NULL;
  }
  return fiber->extra;
}

int fiber_set_extra(fiber_t *fiber, void *extra)
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  fiber->extra = extra;
  return FIBER_OK;
}

// must be called before starting the fiber
// if not called a defult stack size of 64k is used
int fiber_set_stack_size( fiber_t *fiber, uint32_t stacksz )
{
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( fiber->state != FIBER_EGG ) {
    return FIBER_ILLEGAL_STATE;
  }
  fiber->stacksz = stacksz;
  return FIBER_OK;
}

// starts a newly created fiber
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
  
  // allocate stack for fiber
  if ( fiber->stacksz == 0 ) {
    fiber->stacksz = DEFAULTSTACKSIZE;
  }
  fiber->stack = malloc( fiber->stacksz );
  if ( fiber->stack == NULL ) {
    return FIBER_MEMORY_ALLOCATION_ERROR;
  }

  // link fiber and scheduler
  fiber->scheduler = sched;

  // move fiber to init state
  fiber->state = FIBER_INIT;
  
  // insert in hash table
  h = fiberHash( fiber ) % ARRAYSIZE;
  while( sched->fibers[h] != NULL ) {
    h = (h+1) % ARRAYSIZE;
  }
  fiber->fid = h;
  sched->fibers[h] = fiber;

  // insert in init list
  fiber->next = sched->lists[FIBER_INIT];
  sched->lists[FIBER_INIT] = fiber;

  // increase fiber count
  ++sched->nfibers;
  
  // all good
  return FIBER_OK;
}

// explicit stop of a fiber
int fiber_stop( fiber_t *fiber )
{
  if ( fiberCheckExist(fiber) != FIBER_OK ) {
    return FIBER_NO_SUCH_FIBER;
  }
  // can't stop a fiber that hasn't been started yet
  // can't stop a fiber that is already in TERM or DONE state
  if ( fiber->state < FIBER_TERM && fiber->state > FIBER_INIT ) {
    fiber->state = FIBER_TERM;
    return FIBER_OK;
  }
  else {
    return FIBER_ILLEGAL_STATE;
  }
}

// free a fiber object
// if FIBER_OK is returned the associated memory is freed
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

// create a new scheduler
scheduler_t *scheduler_new()
{
  scheduler_t *res;
  res = (scheduler_t*) malloc(sizeof(scheduler_t));
  if ( !res ) {
    return NULL;
  }
  memset(res, 0, sizeof(*res));
  return res;
}

// free scheduler
int scheduler_free( scheduler_t *sched )
{
  free( sched );
  return FIBER_OK;
}

// start running a scheduler at the given frequency
void scheduler_run( uint32_t period )
{
  // @todo: implement me !
}


// --------------------------------------------------------------------------
//   Returns number of msec elapsed since first call to this function
// --------------------------------------------------------------------------
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


// --------------------------------------------------------------------------
//   Returns number of fibers in scheduler
// --------------------------------------------------------------------------
int sched_get_num_fibers( scheduler_t *sched)
{
  return sched->nfibers;
}

