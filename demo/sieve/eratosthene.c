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

typedef struct channel_st channel_t;

/*
 * --------------------------------------------------------------------------
 *  channel_t structure
 * --------------------------------------------------------------------------
 */
struct channel_st {
  size_t     szelem;     /* size of an element */
  size_t     nbelem;     /* size in elements of the input ring */
  size_t     start;      /* index of next element to read */
  size_t     end;        /* index of last element to read */
  size_t     stride;     /* used to walk in 'data' */
  char       data[1];    /* contains nb elem elements of size szelem */
};

channel_t *channel_new( size_t szelem, size_t nbelem )
{
  channel_t *chan;
  
  if ( nbelem < 1 ) {
    nbelem = 1;
  }
  if ( szelem < 1 ) {
    szelem = 1;
  }
  chan = (channel_t*) malloc( sizeof(channel_t) + nbelem*szelem -1 );
  if ( chan == NULL ) {
    return NULL;
  }

  chan->szelem = chan->stride = szelem;
  chan->nbelem = nbelem;
  chan->start = chan->end = 0;
  
  return chan;
}

void channel_free( channel_t *chan )
{
  free( chan );
}

void channel_send( fiber_t *fiber, channel_t *chan, char *data )
{
  int idx;
  if ( (chan->end - chan->start) >= chan->nbelem ) {
    fiber_wait_for_cond( fiber, fiber_check_can_send, chan );
  }
  idx = chan->stride * (chan->end % chan->nbelem);
  memcpy( chan->data + idx, data, chan->szelem );
  chan->end ++;
}

void channel_recv( fiber_t *fiber, channel_t *chan, message_t *msg )
{
  if ( chan->end == chan->start ) {
    fiber_wait_for_cond( fiber, fiber_check_can_recv, chan );
  }
  idx = chan->stride * (chan->start % chan->nbelem);
  if ( msg->addrin != NULL ) {
    memcpy( msg->addrin, chan->data + idx, chan->szelem );
  }
  chan->start ++;  
}


/*
 * --------------------------------------------------------------------------
 *  Global variables
 * --------------------------------------------------------------------------
 */
int done = 0;  /* becomes one when it's time to stop */

/*
 * --------------------------------------------------------------------------
 *  Each fiber has a such a structure attached to it.
 *  'call_chan' is used to post work to the fiber
 *  When doing its work the fiber will post work to the 'source' fiber
 *  which will reply and return its result by posting it to the 'repl_chan'
 * --------------------------------------------------------------------------
 */
typedef struct {
  channel_t *call_chan;   /* channel to use to post work to fiber */
  channel_t *reply_chan;  /* channel used to return result of current call */
  channel_t *return_chan; /* channel used by 'source' fiber to return 
			   * its result. */
  fiber_t   *source;      /* integer stream generator */
  int        n;           /* used to filter output of 'source' */
} extra_t;


/*
 * --------------------------------------------------------------------------
 *  Cleanup
 * --------------------------------------------------------------------------
 */
void generator_done( fiber_t *fiber )
{
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
  channel_free( extra->call_chan );
  channel_free( extra->return_chan );
  free( extra );
}

/*
 * --------------------------------------------------------------------------
 *  Creates a generator
 * --------------------------------------------------------------------------
 */
fiber_t *generator_new( fiber_t *source, pf_run_t task_func, int n)
{
  channel_t *return_chan;
  channel_t *call_chan;
  scheduler_t *sched;
  fiber_t *fiber;
  extra_t *extra;

  if ( source != NULL ) {
    sched = fiber_get_scheduler( source );
    fatalif( sched == NULL, "scheduler null" );
  }
  else {
    sched = scheduler_new();
  }
 
  extra = (extra_t*) malloc( sizeof(extra_t) );
  fatalif ( extra == NULL, "memory allocation error !\n");

  extra->source = source;
  extra->n = n;
  extra->reply_chan = NULL;

  return_chan = channel_new( sizeof(int), 1);
  fatalif( return_chan == NULL, "memory allocation error !\n");
  extra->return_chan = return_chan;
  
  call_chan = channel_new( sizeof(channel_t*), 1);
  fatalif( call_chan == NULL, "memory allocation error !\n");  
  extra->call_chan = call_chan;
  
  fiber = fiber_new( task_func, extra );
  fatalif ( fiber == NULL, "memory allocation error !\n");

  fiber_set_stack_size( fiber, 2048 );
  fiber_start( fiber, sched );
}

/*
 * --------------------------------------------------------------------------
 *  This function will return an integer by 'calling' the 'source'
 *  integer generator associated with fiber.
 * --------------------------------------------------------------------------
 */
int next_integer( fiber_t *fiber )
{
  extra_t *xfiber  = (extra_t*) fiber_get_extra( fiber );
  extra_t *xsource = (extra_t*) fiber_get_extra( xfiber->source );
  int n = 0;
  
  /* send a pointer to the channel to use to send back result */
  channel_send( fiber, xsource->call_chan, xfiber->return_chan );

  /* wait for answer and return it */
  channel_recv( fiber, xfiber->return_chan, (char*) &n );
  return n;
}

/*
 * --------------------------------------------------------------------------
 *  Block until another fiber asks 'fiber' to do some work
 * --------------------------------------------------------------------------
 */
int receive( fiber_t *fiber )
{
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
  extra->reply_chan = NULL;
  channel_recv( fiber, extra->call_chan, &extra->reply_chan );
  return (extra->reply_chan != NULL);
}

/*
 * --------------------------------------------------------------------------
 *  Sends an integer to the channel that was received
 * --------------------------------------------------------------------------
 */
void send( fiber_t *fiber, int x )
{
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
  channel_send( fiber, extra->reply_chan, (char*) &i);
}


/*
 * --------------------------------------------------------------------------
 *  Each time it gets 'called' this generator returns an integer
 *  generated by its 'source' generator and that is not a multiple
 *  of its 'n' field. It filters 'source' outputs by factor 'n'.
 *
 *  Its 'source' generator will return 'filtered' integers too.
 *  Many filtering generators are chained so that each time an integer
 *  is returned it is a prime number.
 * --------------------------------------------------------------------------
 */
void filter_by_factor_task( fiber_t *fiber )
{
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
  int x, n = extra->n;
  /* wait for being called */
  while( receive( fiber ) ) {
    /* loop until we get a number not multiple of n */
    do {
      x = next_integer( fiber );
      if ( x % n ) {
	send( fiber, x );
      }
    } while( (x % n) == 0 );
  }
}

/*
 * --------------------------------------------------------------------------
 *  Simple generator that returns integers one by one
 * --------------------------------------------------------------------------
 */
void all_numbers_task( fiber_t * fiber )
{
  int x = 1;
  /* wait for being called */
  while( receive( fiber ) ) {
    send( fiber, x++ );
  }
}

/*
 * --------------------------------------------------------------------------
 *  This generator returns only prime numbers
 * --------------------------------------------------------------------------
 */
void eratosthene_task( fiber_t *fiber )
{
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
  int n;
  /* wait for being called */
  while( receive( fiber ) ) {
    n = next_integer( fiber );
    send( fiber, n );
    /* chain generator */
    extra->source = generator_new( extra->source, filter_by_factor_task, n );
  }
}

/*
 * --------------------------------------------------------------------------
 *  This task calls 20 times the eratosthene prime generator
 *  It will give the 20 first prime numbers
 * --------------------------------------------------------------------------
 */
void main_task( fiber_t *fiber )
{
  int i;
  for( i = 0; i <= 20; ++i ) {
    printf("prime#%d = %d\n", i, next_integer( i ));
  }
  /* tell it is the end */
  done = 1;
}

/*
 * --------------------------------------------------------------------------
 *  Main program
 * --------------------------------------------------------------------------
 */
int main()
{
  fiber_t *fmain, *fsieve, *fallnumbers;
  scheduler_t *sched;

  /* create main tasks */
  fallnumbers = generator_new( NULL, all_numbers_task, 0 );
  fsieve      = generator_new( fallnumbers, eratosthene_task, 0 );
  fmain       = generator_new( fsieve, main_task, 0 );

  /* retrieve scheuler */
  sched = fiber_get_scheduler( fallnumbers );

  /* loop while main isn't finished */
  while( !done ) {
    sched_cycle( sched, 0 );
  }

  /* force all fibers to stop */
  sched_stop( sched );

  /* another cycle is needed to stop all tasks */
  sched_cycle( sched, 0 );

  /* release scheduler */
  sched_free( sched );
  
  return 0;
}

