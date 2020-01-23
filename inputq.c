#include "task.h"

#include <stdint.h>

// default value for constants
#define INPUT_QUEUE_DEF_HWM  64
#define INPUT_QUEUE_DEF_LWM  60
#define INPUT_QUEUE_DEF_LIFE 32

typedef struct input_queue input_queue_t;
typedef struct input_queue_elem input_queue_elem_t;

struct input_queue_elem
{
  input_queue_elem_t* next;  // next in linked list
  
  uint32_t timestamp;   // timestamp of element
  uint8_t  state;       // state of element (see enum input_queue_elem_state_e)

  void*    userdata;       // user data
  void (*pf_free)(void*);  // used to free user data
  void (*pf_copy)(void*, void**);  // used to copy user data
};


// A queue is a list of elements plus some characteritics
// that help in the management of the queue
struct input_queue
{
  input_queue_elem_t *head;  // first element
  input_queue_elem_t *tail;  // last element
  uint16_t             hwm;  // high water mark - when reached oldest elements are dropped until lwn is reached
  uint16_t             lwm;  // low water mark
  uint32_t            life;  // maximum lifetime of elements in the queue
};

enum input_queue_elem_state_e {
  QUEUE_ELEM_DROP,   // the element can be dropped and will no be used
  QUEUE_ELEM_TAKE,   // the element can be dropped but will be used
  QUEUE_ELEM_KEEP,   // the element will not be used but has to be kept in the queue
};



input_queue_elem_t *input_queue_elem_new()
{
  return (input_queue_elem_t *) malloc(sizeof(input_queue_elem_t));
}

void input_queue_elem_free( input_queue_elem_t *e )
{
  free(e);
}

void input_queue_append( input_queue_t *q, input_queue_elem_t *e )
{
  if ( q->tail ) {
    q->tail->next = e;
  }
  else {
    q->head = e;
  }
  q->tail = e;
  e->next = NULL;
}

// Clean message queue keeping messages whose state is "QUEUE_ELEM_KEEP"
void input_queue_clean( input_queue_t *q )
{
  input_queue_elem_t *l_head = NULL;
  input_queue_elem_t *l_tail = NULL;
  input_queue_elem_t *e, *ne;

  for( e = q->head; e; e = ne ) {
    ne = e->next;
    if ( e->state == QUEUE_ELEM_KEEP ) {
      if ( l_tail != NULL ) {
	l_tail->next = e;
	l_tail = e;
      }
      else {
	l_head = l_tail = e;
      }
      e->next = NULL;
    }
    else {
      // The element is ready to be freed
      if ( e->pf_free ) {
	e->pf_free(e);
      }
    }
  }

  q->head = l_head;
  q->tail = l_tail;
}

// Appends a queue to another
// All elements in queue "t" are appended to queue "q"
// Queue "t" is empty when the function returns
void input_queue_move( input_queue_t *q, input_queue_t *t )
{
  if ( q->tail ) {
    q->tail->next = t->head;
  }
  else {
    q->head = q->tail = t->head;
  }
  t->head = t->tail = NULL;
}

// computes the length of a queue
int input_queue_length( input_queue_t *q )
{
  input_queue_elem_t *e;
  int res = 0;
  for( e = q->head; e; e = e->next ) res++;
  return res;
}


// predicates for queue
#define MAXMATCH 8

typedef struct input_queue_predicate_data
{
  input_queue_t *q;
  pf_check_t     pf_check;        // function that will check if an element of q match the predicate
  uint8_t        maxmatch;        // maximum number of elements to extract from the queue in a call
  uint8_t        nmatch;          // number of elements extracted
  void*          match[MAXMATCH]; // where the predicate will copy elements that match
  
} input_queue_predicate_data_t;

// predicate checking function
static int input_queue_check(fiber_t *fiber, void *data)
{
  input_queue_predicate_data_t *iqpd = (input_queue_predicate_data_t*) data;
  input_queue_elem_t *e;

  // reset the extraction result
  iqpd->nmatch = 0;
  memset( iqpd->match, 0, sizeof(iqpd->match));

  // loop over the queue
  for( e = iqpd->q->head; e; e = e->next ) {
    // check if the user data match
    if ( iqpd->pf_check( fiber, e->userdata )) {
      if ( iqpd->nmatch < iqpd->maxmatch ) {
	if ( e->pf_copy != NULL ) {
	  e->pf_copy( e->userdata, iqpd->match + iqpd->nmatch);
	}
	else {
	  iqpd->match[iqpd->nmatch] = e->userdata;
	}
	
	// increment number of match
	iqpd->nmatch++;
	
	// change element state
	if ( e->state == QUEUE_ELEM_DROP ) {
	  e->state = QUEUE_ELEM_TAKE;
	}
      }
      else if ( iqpd->nmatch >= iqpd->maxmatch ) {
	// mark the element as to be kept
	e->state = QUEUE_ELEM_KEEP;
      }
    }
  }

  // return 1 of some match were found
  return ( iqpd->nmatch != 0 );
}

// dumps statistics about the input queue
void input_queue_stats( input_queue_t *q )
{
  
}

// part of public API
int wait_for_input_queue( fiber_t *fiber, uint32_t msec, input_queue_t *q, pf_check_t pf_check, int nmatch, void **matchtab )
{
  input_queue_predicate_data_t iqpd;
  int res;
  
  if ( fiber == NULL ) {
    return FIBER_NO_SUCH_FIBER;
  }
  if ( pf_check == NULL ) {
    error("Fiber %d : wait_for_input_queue - predicate function missing.\n", fiber->fid);
    fiber_yield( fiber );
    return FIBER_ERROR;
  }
  if ( matchtab == NULL ) {
    error("Fiber %d : wait_for_input_queue - match array missing.\n", fiber->fid);
    fiber_yield( fiber );
    return FIBER_ERROR;
  }
  if ( nmatch <= 0 || nmatch > MAXMATCH ) {
    error("Fiber %d : wait_for_input_queue - invalid match.\n", fiber->fid);
    fiber_yield( fiber );
    return FIBER_ERROR;    
  }

  // fill predicate data structure
  iqpd->nmatch = 0;
  iqpd->maxmatch = nmatch;
  iqpd->pf_check = pf_check;
  iqpd->q = q;

  // call library function
  res = fiber_wait_for_cond( fiber, msec, &input_queue_check, (void *) &iqpd);

  // if the result is OK transfer it
  if ( res == FIBER_OK ) {
    memcpy( matchtab, nmatch * sizeof(void*));
  }

  // forward result
  return res;
}


