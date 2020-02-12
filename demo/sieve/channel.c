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

#include <stdlib.h>
#include <string.h>

typedef struct channel channel_t;

/*
 * --------------------------------------------------------------------------
 *  Channel structure
 *  A channel is a another name for a fifo
 * --------------------------------------------------------------------------
 */
struct channel {
  size_t     szelem;     /* size of an element */
  size_t     nbelem;     /* size in elements of the input ring */
  size_t     start;      /* index of next element to read */
  size_t     end;        /* index of last element to read */
  size_t     stride;     /* used to walk in 'data' */
  char       data[1];    /* contains nb elem elements of size szelem */
};

/*
 * --------------------------------------------------------------------------
 *  Allocates a new channel
 * --------------------------------------------------------------------------
 */
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

/*
 * --------------------------------------------------------------------------
 *  Frees a channel object
 * --------------------------------------------------------------------------
 */
void channel_free( channel_t *chan )
{
  free( chan );
}

/*
 * --------------------------------------------------------------------------
 *  Predicate used to check if channel is ready for send operation
 *  which is possible is its input queue is not full
 * --------------------------------------------------------------------------
 */
static int fiber_check_can_send( fiber_t *fiber, void *data )
{
  channel_t *chan = (channel_t*) data;
  return ((chan->end - chan->start) < chan->nbelem);
}

/*
 * --------------------------------------------------------------------------
 *  Predicate used to check if a channel is ready for recv operation
 *  which is possible is its input queue is not empty
 * --------------------------------------------------------------------------
 */
static int fiber_check_can_recv( fiber_t *fiber, void *data )
{
  channel_t *chan = (channel_t*) data;
  return (chan->end > chan->start);
}

/*
 * --------------------------------------------------------------------------
 *  send operation
 *
 *  Yields and waits for the channel to be ready for sending.
 *  Then copy data to channel internal buffer.
 * --------------------------------------------------------------------------
 */
void channel_send( fiber_t *fiber, channel_t *chan, char *data )
{
  int idx;
  fiber_wait_for_cond( fiber, 0, fiber_check_can_send, chan );
  idx = chan->stride * (chan->end % chan->nbelem);
  memcpy( chan->data + idx, data, chan->szelem );
  chan->end ++;
}

/*
 * --------------------------------------------------------------------------
 *  recv operation
 *
 *  Yields and waits for the channel to be ready for receiving.
 *  Then copy data from channel internal buffer.
 * --------------------------------------------------------------------------
 */
void channel_recv( fiber_t *fiber, channel_t *chan, char *data )
{
  int idx;
  fiber_wait_for_cond( fiber, 0, fiber_check_can_recv, chan );
  idx = chan->stride * (chan->start % chan->nbelem);
  memcpy( data, chan->data + idx, chan->szelem );
  chan->start ++;
  fiber_yield( fiber );
}
