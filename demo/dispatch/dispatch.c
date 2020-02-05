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

/* ----------------------------------------------------------------------------
 *   CAN messaging with fibers
 *   In this example: 
 *      - many fibers will be in charge of one kind of message.
 *      - a fiber will read frames from the CAN bus (up to 64 messages)
 *        and dispatch the messages to the previous fibers.
 *      - the last fiber doesnèt process incoming messages
 *        but sends an heartbeat message on the bus each 500 msec.
 * ----------------------------------------------------------------------------*/
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/can/raw.h>

/* ----------------------------------------------------------------------------
 *  Data structures
 * ----------------------------------------------------------------------------*/
typedef struct extra_s {
  int socket;         /* socket attached to fiber */
  int hasdata;        /* tells if data is ready on socket */
  int canid;          /* associated can message identifier */
  can_frame_t frame;  /* can frame passed to the fiber */
} extra_t;

/* hash table mapping messages to fiber */
fiber_t *hash[32];
#define PRIME 29

/* list of CAN messages that are knowns */
/* these messages are the ones foud in pcap capture */
static uint32_t canids[] =
  {
   0x00,  /* NMT */
   0x5c2,
   0x642,
   0x733, /* CAN open heartbeat message */
   0x742,
   0x741,
   0x721,
   0x7e5,
   0x7e4,
  };

/* message ring - this ring will hold the ncoming CAN messages */
/* at most 64 messages will be handled at the same time */
typedef struct can_frame can_frame_t;
can_frame_t ring[64];
int ring_start = 0, ring_end = 0;

/* ----------------------------------------------------------------------------
 *  Hash function
 *  Used to quickly find the handler of a CAN message
 * ----------------------------------------------------------------------------*/
uint8_t canid_hash(uint32_t id)
{
  return id % PRIME;
}

/* --------------------------------------------------------------------------
 *   Used to get the fiber in charge of a can message
 * --------------------------------------------------------------------------*/
fiber_t *canid_get_fiber(uint32_t id)
{
  uint8_t h = canid_hash(id);
  fiber_t *fiber = hash[h];
  while (fiber) {
    extra_t *extra = fiber_get_extra(fiber);
    if (extra->id == id) return fiber;
    h = (h+1)%64;
    fiber = hash[h];
  }
  return fiber;
}

/* --------------------------------------------------------------------------
 *   Register fiber as in charge of can message with identifier 'id'
 *   Returns 1 is fiber was added and 0 otherwise
 * --------------------------------------------------------------------------*/
int canid_add_fiber(uint32_t id, fiber_t *fiber)
{
  uint8_t h0 = canid_hash(id), h = h0;
  extra_t *extra = fiber_get_extra(fiber);
  fiber_t *f = hash[h];
  while (f) {
    extra_t *ef = fiber_get_extra(f);
    if (extra->id == id) break;
    h = (h+1)%64;
    f = hash[h];
    /* is there is no space left return */
    if ( h == h0 ) return 0;
  }
  hash[h] = fiber;
  extra->canid = canid;
  return 1;
}

/* ----------------------------------------------------------------------------
 *  Opens CAN socket on device "ifname"
 *  Returns the socket
 * ----------------------------------------------------------------------------*/
int opencan( char *ifname )
{
  int s, flags;
  struct sockaddr_can addr;
  struct can_frame frame;
  struct ifreq ifr;
  
  if((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
    perror("Error while opening socket");
    return -1;
  }

  strcpy(ifr.ifr_name, ifname);
  ioctl(s, SIOCGIFINDEX, &ifr);
	
  addr.can_family  = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  
  printf("%s at index %d\n", ifname, ifr.ifr_ifindex);
  
  if(bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Error in socket bind");
    return -2;
  }

  /* set non-blocking */
  flags = fcntl(s ,F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);

  return s;
}


/* ----------------------------------------------------------------------------
 *  Reads from the CAN socket and dispatch message to right fiber
 *  The function reads up to 64 CAN frame at once
 *  Then it waits until all the read CAN frames have been processed.
 * ----------------------------------------------------------------------------*/
void read_task( fiber_t *fiber )
{
  extra_t *extra = fiber_get_extra( fiber );
  int fd = extra->socket;
  
  while(1) {
  redo:
    fiber_wait_for_var( fiber, extra->hasdata, 1);
    if ( extra->hasdata ) {
      extra->hasdata = 0;

      /* read frames (up to 64) */
      do {
	n = read( fd, ring + (ring_end % 64), sizeof(can_frame_t));
	if ( n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
	  if ( nothing ) {
	    warning("socket not ready !\n");
	    goto redo;
	  }
	  else break;
	}
	if ( n > 0 ) {
	  nothing = 0;
	  ring_end ++;
	}
	if ( n == 0 ) {
	  break;
	}
      } while ( (ring_end - ring_start) % 64 );

      /* Nothing read but select told us there was data to read !
       * it means socket was closed. Maybe the CAN device went down.
       * This reader can be shutdown as it is using a dead socket.  */
      if ( nothing ) {
	error("Remote end close connection !\n");
	fiber_stop( fiber );
	fiber_yield( fiber );
      }

      /* dispatch all frames read */
      info("Number of CAN frames to process %d\n", ring_end - ring_start);
      while( ring_start < ring_end ) {
	uint32_t canid = ring[ring_start % 64].can_id;
	fiber_t *f = canid_get_fiber( canid );
	if ( f != NULL ) {
	  extra_t *e = fiber_get_extra(f);
	  /* wait until f has processed its data */
	  if ( e->hasdata ) {
	    while(fiber_fait_for_var( fiber, MSEC, f->hasdata, 0) == FIBER_TIMEOUT);
	  }
	  /* push data to f */
	  memcpy( e->frame, &ring[ring_start % 64], sizeof(can_frame_t));
	  f->hasdata = 1;
	}
	ring_start++;
      }
    }
  }
}

/* ----------------------------------------------------------------------------
 *  Sends a heartbit message each 500 msec
 * ----------------------------------------------------------------------------*/
void heartbeat_task( fiber_t *fiber )
{
  extra_t *extra = fiber_get_extra( fiber );
  int scan = extra->socket;
  can_frame_t cfr;
  
  while(1) {
    fiber_wait( fiber, HEARTBIT_PERIOD );
    
    /* send heartbeat */
    cfr.can_id = HEARTBEAT_CANID;
    cfr.dlc = 1;
    cfr.data[0] = 1;

    safewrite( scan, &cfr, sizeof(cfr));
  }
}

/* ----------------------------------------------------------------------------
 *  Message handler task
 * ----------------------------------------------------------------------------*/
void msg_task( fiber_t *fiber )
{
  extra_t *extra = fiber_get_extra( fiber );
  int scan = extra->socket;

  while(1) {
    fiber_wait_for_var( fiber, extra->hasdata, 1);
    if ( extra->hasdata ) {
      extra->hasdata = 0;
      info("Message 0x%x received\n");
    }
  }
}

/* --------------------------------------------------------------------------
 *  Call select to find which connection has pending data to read
 *  Arrange for the fiber handling th connection to wake up.
 *
 *  This function is called at the beginnig of each scheduler cycle.
 * --------------------------------------------------------------------------*/
int selectloop( fiber_t *fiber ) {
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
  struct timeval tv;
  fd_set rdfs;
  int retval, selectfd = extra->socket;

  /* Watch sockets to see when they have input. */
  FD_ZERO(&rdfs);
  FD_SET(&rdfs, selectfd );

  /* Wait up to 50 msecs. */
  tv.tv_sec = 0;
  tv.tv_usec = 5000;

  retval = select( selectfd+1, &rdfs, NULL, NULL, &tv);

  if (retval == -1) {
    perror("select()");
    
  } else if (retval && FD_ISSET(&rdfs, selectfd)) {
    data->hasdata = 1;
  }
}

/* --------------------------------------------------------------------------
 *  Creates a fiber
 * --------------------------------------------------------------------------*/
fiber_t *mktask( scheduler_t *sched, pf_run_t pfrun )
{
  extra_t *extra;
  fiber_t *fiber;
  extra = (extra_t*) malloc(sizeof(extra_t));
  fatalif( extra == NULL, "memory allocation error !\n");
  extra->fd = serverfd;
  extra->hasdata = 0;
  fiber = fiber_new( pfrun, extra );
  fatalif( fiber == NULL, "memory allocation error !\n");
  fiber_start( sched, fiber );
  return fiber;
}

/* --------------------------------------------------------------------------
 *  Server
 *  Opens the CAN socket, create the scheduler and the tasks
 *  starts the processing loop.
 * --------------------------------------------------------------------------*/
void server( char *device )
{
  int serverfd;
  scheduler_t *sched;
  fiber_t *freader, *fiber;
  extra_t *extra;

  /* create listening socket */
  serverfd = opencan( device );
  if ( serverfd < 0 ) {
    perror("ERROR opening server socket");
    exit(1);
  }

  /* create scheduler */
  sched = sched_new();

  /* create the fiber that will accept
   * incoming connections */
  freader = mktask( sched, read_task );
  extra = fiber_get_extra( freader );
  extra->socket = fd;

  /* create the heartbeat fiber */
  mktask( sched, heartbeat_task );
  
  /* create fibers that will handle tasks */
  for( i = 0; i < sizeof(canids)/sizeof(canids[0]); ++i ) {
    fiber = mktask( sched, msg_task );
    canid_add_fiber( canids[i], fiber );
  }
  
  while(1) {
    selectloop( freader );
    sched_cycle( sched, sched_elapsed() );
  }
}
