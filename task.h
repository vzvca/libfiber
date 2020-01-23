#ifndef __FIBER_TASK_H__
#define __FIBER_TASK_H__

#include <stdint.h>
#include <setjmp.h>

#define MAXFIBERS 503
#define ARRAYSIZE 512
#define DEFAULTSTACKSIZE (65536)

enum log_level_e {
  LOG_FATAL = 0,
  LOG_ERROR,
  LOG_WARN,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE
};

void set_log_level( uint8_t level);
void fatal( const char *fmt, ...);
void info( const char *fmt, ...);
void error( const char *fmt, ...);
void warn( const char *fmt, ...);
void debug( const char *fmt, ...);
void trace( const char *fmt, ...);


// forward declarations
typedef struct scheduler scheduler_t;
typedef struct predicate predicate_t;
typedef struct fiber fiber_t;

// function pointers types
typedef void (*pf_init_t)(fiber_t *fiber);
typedef void (*pf_run_t)(fiber_t *fiber); 
typedef void (*pf_term_t)(fiber_t *fiber);
typedef void (*pf_done_t)(fiber_t *fiber);

typedef int  (*pf_check_t)(fiber_t *, void *);

typedef void (*pf_pre_hook_t)(scheduler_t *sched, void *extra);
typedef void (*pf_post_hook_t)(scheduler_t *sched, void *extra);


// fiber data structure
struct fiber
{
  scheduler_t *scheduler;   // scheduler
  fiber_t *next;            // used by scheduler
  
  jmp_buf context;          // context

  // methods of object
  pf_init_t pf_init;
  pf_run_t  pf_run;
  pf_term_t pf_term;
  pf_done_t pf_done;

  uint8_t state;       // state of fiber 

  uint32_t fid;        // fiber identifier generated on creation

  // @todo : use these fields
  uint32_t lastexec;   // last execution timestamp
  uint32_t quantum;    // CPU share
  
  uint32_t stacksz;    // stack definition
  uint8_t*   stack;

  void*    extra;      // extra info attached to task during construction

  // for a fiber there can be only one predicate active at a given time
  // the predicate is stored here
  predicate_t *predicate; 
};

enum fiber_state_e {
  FIBER_EGG,                // Not born yet ! Still in its egg
  FIBER_INIT,               // Initial state of fiber after call to fiber_create
  FIBER_SUSPEND,            // suspended : waiting for a timer/predicate realization
  FIBER_RUNNING,            // ready to run, not waiting for anything
  FIBER_TERM,               // Fiber state when asked to terminate
  FIBER_DONE,               // Last state of a fiber just before disappearing
  FIBER_NUM_STATES
};

enum fiber_error_e {
  FIBER_OK = 0,             // no error
  FIBER_ERROR,              // unspecified error
  FIBER_TIMEOUT,            // a wait function ended with a timeout
  FIBER_TOO_MANY_FIBERS,    // cannot link fiber to scheduler because scheduler is full
  FIBER_INVALID_TIMEOUT,    // ivalid timeout specified
  FIBER_NO_SUCH_FIBER,      // returned when looking for a non existent fiber (using its ID or pointer)
  FIBER_ILLEGAL_STATE,      // trying to do an illegal transition
  FIBER_MEMORY_ALLOCATION_ERROR, // malloc failed
  FIBER_SIGNALERROR,        // failure during sigaltstack/sigaction
  FIBER_INVALID_PREDICATE,  // predicat de test non valide
};

// scheduler
struct scheduler
{
  fiber_t *fibers[ARRAYSIZE];       // all fibers are kept in an array
  fiber_t *lists[FIBER_NUM_STATES]; // head of lists
  fiber_t *running;                 // currently running fiber
  int nfibers;                      // number of fibers
  
  // definition of 2 functions that will be invoked before each scheduler
  // cycle and just after.
  // Both functions are invoked with the same "extra" argument.
  pf_pre_hook_t  pf_pre_hook;
  pf_post_hook_t pf_post_hook;
  void *extra;

  jmp_buf context;                  // main context: used by fibers to give back control to scheduler while yielding
};

// predicate to test
struct predicate {
  predicate_t *next;        // next in linked list
  uint32_t deadline;        // when the timer expires
  fiber_t     *fiber;       // associated fiber
  void        *data;        // data to pass to predicate function
  pf_check_t   pf_check;    // predicate function called for fiber
  uint8_t      state;       // state of predicate (ACTIVE, EXPIRED, DEAD)
};

enum predicate_state_e {
  PREDICATE_ACTIVE = 0,     // predicate is active and must be checked
  PREDICATE_REALIZED,       // condition is now true and associated timer (if any) is not expired
  PREDICATE_FIRED,          // associated timer has expired before the predicate becomes true
  PREDICATE_DEAD,           // ready to remove
};


// fonction to give back processor
// part of public API
int fiber_yield(fiber_t *fiber);
// fonction to wait for a given amount of time
// part of public API
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


// --------------------------------------------------------------------------
fiber_t *fiber_new(pf_run_t run_func, void *extra);
int fiber_set_init_func( fiber_t *fiber, pf_init_t init_func);
int fiber_set_term_func( fiber_t *fiber, pf_term_t term_func);
int fiber_set_done_func( fiber_t *fiber, pf_done_t done_func);
void *fiber_get_extra(fiber_t *fiber);
int fiber_set_extra( fiber_t *fiber, void *extra );

// must be called before starting the fiber
// if not called a defult stack size of 64k is used
int fiber_set_stack_size( fiber_t *fiber, uint32_t stacksz );
int fiber_get_stack_size( fiber_t *fiber );

// starts a newly created fiber
int fiber_start( scheduler_t *sched, fiber_t *fiber );

// explicit stop of a fiber: fiber will switch to FIBER_TERM state
int fiber_stop( fiber_t *fiber );

// free a fiber object
// if FIBER_OK is returned the associated memory is freed
int fiber_free( fiber_t *fiber );




// create a new scheduler
scheduler_t *scheduler_new();

// start running a scheduler at the given frequency
void scheduler_run( uint32_t period );

// returns elapsed msec since first call to this function
uint32_t sched_elapsed();

// scheduler cycle
// used to run the scheduler from an external event loop
void sched_cycle(scheduler_t *sched, uint32_t elapsed);

// free a scheduler : will do nothing if the scheduler is currently
// running a fiber.
int scheduler_free( scheduler_t *sched );

// set the hooks function
int scheduler_set_hooks( scheduler_t *sched, pf_pre_hook_t pf_pre_hook, pf_post_hook_t pf_post_hook, void *extra);

// get number of fibers (all states considered)
int sched_get_num_fibers( scheduler_t *sched);

#endif
