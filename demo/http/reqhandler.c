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

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "task.h"
#include "card.h"

/* data associated to a fiber */
typedef struct extra_s {
  int fd;       /* socket attached to fiber */
  int pause;    /* time to pause between each image sent */
  int hasdata;  /* tells if data is ready on socket */
} extra_t;

static char *boundary = "--boundarydonotcross";

/* this set contains the current sockets to watch */
static fd_set cnxset;
static int  cnxmaxfd = 0;

#define BACKLOG 5
#define CNXMAX 64
static fiber_t *cnx2fiber[CNXMAX];

/* all the cards, defined in main.c */
extern card_t *allcards[];

/* --------------------------------------------------------------------------
 *  Get the socket of fiber
 * --------------------------------------------------------------------------*/
static int get_fiber_fd( fiber_t *f )
{
  extra_t *data = (extra_t*) fiber_get_extra(f);
  return data->fd;
}

/* --------------------------------------------------------------------------
 *  Get the pause flag of fiber
 * --------------------------------------------------------------------------*/
static int get_fiber_pause( fiber_t *f )
{
  extra_t *data = (extra_t*) fiber_get_extra(f);
  return data->pause;
}

/* --------------------------------------------------------------------------
 *  Set the hasdata flag of fiber
 * --------------------------------------------------------------------------*/
static void set_fiber_hasdata( int val )
{
  extra_t *data = (extra_t*) fiber_get_extra(f);
  extra->hasdata = val;
}

/* --------------------------------------------------------------------------
 *  Write, checking for errors.
 * --------------------------------------------------------------------------*/
static void safewrite(int fd, const char *b, size_t n)
{
  size_t w = 0;
  while (w < n){
    ssize_t s = write(fd, b + w, n - w);
    if (s < 0 && errno != EINTR)
      return;
    else if (s < 0)
      s = 0;
    w += (size_t)s;
  }
}

/* --------------------------------------------------------------------------
 *  Write a line
 * --------------------------------------------------------------------------*/
void writeln( int fd, char *fmt, ... )
{
  va_list va;
  char buffer[512];
  va_start(va, fmt);
  vsnprintf( buffer, sizeof(buffer), fmt, va);
  buffer[sizeof(buffer)-1] = '\0';
  va_end(va);
  safewrite( fd, buffer, strlen(buffer));
  safewrite( fd, "\n", 1);
}

/* --------------------------------------------------------------------------
*  Send HTTP return code
* Only 2 codes supported
* --------------------------------------------------------------------------*/
static void send_respose( int fd, int code )
{
  switch( code ) {
  case 200:
    writeln( fd, "HTTP/1.1 200 OK");
    break;
  default:
    writeln( fd, "HTTP/1.1 404 NOT FOUND");
    break;
  }
}

/* --------------------------------------------------------------------------
 *  Returns data string
 * --------------------------------------------------------------------------*/
static char *date()
{
  return "Mon, 1 Jan 2030 12:34:56 GMT";
}

/* --------------------------------------------------------------------------
 *  Send headers
 * --------------------------------------------------------------------------*/
static void request_headers( int fd )
{
  writeln( fd, "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
  writeln( fd, "Connection: close");
  writeln( fd, "Content-Type: multipart/x-mixed-replace;boundary=%s", boundary);
  writeln( fd, "Expires: %s", date());
  writeln( fd, "Pragma': no-cache");
  writeln( fd, "" );
}

/* --------------------------------------------------------------------------
 *  Send image headers
 * --------------------------------------------------------------------------*/
static void image_headers( int fd, card_t *card )
{
  writeln( fd, "X-Timestamp: time.time()");
  writeln( fd, "Content-Length: %d", card->sz );
  /* mime-type must be set according file content*/
  writeln( fd, "Content-Type: image/gif");
  writeln( fd, "" );
} 

/* --------------------------------------------------------------------------
 *  Send multipart image
 * --------------------------------------------------------------------------*/
void image( fiber_t *fiber )
{
  card_t *card;
  int pause, fd = get_fiber_fd(fiber);

  /* reply OK */
  send_response( fd, 200);
     
  /* Response headers (multipart) */
  request_headers( fd );

  while(1) {
    /* boundary */
    writeln( fd, boundary );
    writeln( fd, "" );
    
    /* choose a random card */
    card = allcards(rand() % 52);
    
    /* dump headers */
    image_header( fd, card );

    /* dump image */
    safewrite( fd, card->bin, card->sz );

    /* wait a while */
    fiber_wait( get_fiber_pause(fiber) );
  }
}

/* --------------------------------------------------------------------------
 *  Send error 404 reply
 * --------------------------------------------------------------------------*/
void error404( fiber_t *fiber )
{
  int fd = get_fiber_fd(fiber);
  send_response( fd, 404);
}

/* --------------------------------------------------------------------------
 *  Send home page
 * --------------------------------------------------------------------------*/
void home( fiber_t *fiber )
{
  int fd = get_fiber_fd(fiber);
  char *page = "<html><head></head><body><img src='1.gif'></body></html>";
  
  send_response( fd, 200);
  
  writeln( fd, "Connection: close");
  writeln( fd, "Content-Type: text/html");
  writeln( fd, "Content-Length: %d", strlen(page)+1);
  writeln( fd, "" );

  writeln( fd, page);
}

/* --------------------------------------------------------------------------
 *  On accept a fiber is created which uses this function
 *  Depending on the web page requested, another function will be called
 *  to handle the request.
 * --------------------------------------------------------------------------*/
void generic_task( fiber_t *fiber )
{
  extra_t *extra = (extra_t*) fiber_get_extra( fiber );
  int n, fd = get_fiber_fd( fiber );
  char buffer[4096];
  char *location;

  /* wait for incoming data */
  extra->hasdata = 0;
  ret = fiber_wait_for_var( fiber, MSEC, &extra->hasdata, 1);
  if ( ret == FIBER_TIMEOUT ) {
    continue;
  }

  /* read request */
  n = read( fd, buffer, sizeof(buffer));
  puts(buffer);
  if ( n > 0 ) {
    if ( strncmp( buffer, "GET ", 4 ) == 0 ) {
      if ( strcmp(location, "/") == 0 ) {
	home( fiber );
      }
      else if ( isdigit(location[1]) ) {
	image( fiber );
      }
      else {
	error404( fiber );
      }
    }
    else {
      /* only GET supported */
      error404( fiber );
    }
  }
  else {
    /* read returned 0, which means that the connection was closed
     * just leave the infinite read loop. */
    break;
  }
}

/* --------------------------------------------------------------------------
 *  Called once fiber has finished running
 * --------------------------------------------------------------------------*/
void done( fiber_t *fiber )
{
  int i, fd = get_fiber_fd(fiber);
  for ( i = 0; i < CNXMAX; ++i ) {
    if ( cnx2fiber[i] == fiber ) {
      cnx2fiber[i] = NULL;
    }
  }
  close(fd);
  FD_CLEAR( fd, &cnxfd );
  /* recompute cnxmaxfd if needed */
  if ( cnxmaxfd == fd ) {
    for( i = cnxmaxfd = 0; i < CNXMAX; ++i ) {
      if ( cnx2fiber[i] != NULL ) {
	fd = fiber_get_fd( cnx2fiber[i] );
	if ( fd > cnxmaxfd ) {
	  cnxmaxfd = fd;
	}
      }
    }
  }
  /* safe to free fiber. Its stack has already been deallocated */
  free( fiber->extra );
  free( fiber );
}


/* --------------------------------------------------------------------------
 *  Call select to find which connection has pending data to read
 *  Arrange for the fiber handling th connection to wake up.
 *
 *  This function is called at the beginnig of each scheduler cycle.
 * --------------------------------------------------------------------------*/
int selectloop(void) {
  fd_set rfds;
  struct timeval tv;
  int i, retval;

  /* Watch sockets to see when they have input. */
  FD_ZERO(&rfds);
  memcpy( &rdfs, &cnxfs, sizeof(rdfs) );

  /* Wait up to 50 msecs. */
  tv.tv_sec = 0;
  tv.tv_usec = 5000;

  retval = select( cnxmaxfd+1, &rfds, NULL, NULL, &tv);
  /* Don't rely on the value of tv now! */

  if (retval == -1) {
    perror("select()");
    
  } else if (retval) {
    printf("Data is available now.\n");

    /* loop over connections */
    for( i = 0; i < CNXMAX; ++i ) {
      fiber_t *fiber = cnx2fiber[i];
      if ( fiber != NULL ) {
	extra_t *extra = fiber_get_extra( fiber );
	int fd = get_fiber_fd( fiber );
	if ( FD_ISSET( fd, &rdfs) ) {
	  extra->hasdata = 1;  /* it will wake up the fiber */
	}
      }
    }
    
  } else {
    printf("No data within five seconds.\n");
  }
}

/* --------------------------------------------------------------------------
 *  Create a task to handle connection
 * --------------------------------------------------------------------------*/
void mktask( scheduler_t *sched, int fd )
{
  extra_t *pextra;
  fiber_t *fiber;

  extra = (extra_t*) malloc(sizeof(extra_t));
  extra->fd = fd;
  extra->pause = 200 + (rand() % 800); /* pause between 200ms to 1s */
  extra->hasdata = 0;

  FD_SET( fd, &cnxset );
  if ( fd > cnxmaxfd ) {
    cnxmaxfd = fd;
  }
  
  fiber = fiber_new( generic_task, extra);
  fiber_set_stack_size( fiber, 8192);
  fiber_set_done_func( fiber, done);
  fiber_start( sched, fiber);
}

/* --------------------------------------------------------------------------
 *  accept_task
 *  Called when select find some data
 * --------------------------------------------------------------------------*/
void accept_task( fiber_t *fiber )
{
  int ret, newfd, fd = get_fiber_fd( fiber );
  extra_t *extra = (extra_t*) fiber_get_extra( fiber );

  while(1) {
    /* wait for incoming data */
    extra->hasdata = 0;
    ret = fiber_wait_for_var( fiber, MSEC, &extra->hasdata, 1);
    if ( ret == FIBER_TIMEOUT ) {
      continue;
    }

    /* create new connection and new fiber to serve it */
    newfd = accept(fd, NULL, NULL);
    if ( newfd >= 0 ) {
      mktask( fiber_get_scheduler( fiber ), newfd );
    }
    else {
      perror("accept()");
    }
  }
}

/* --------------------------------------------------------------------------
 *  server
 *  Opens the server socket, create the scheduler and the initial task
 *  that will handle
 * --------------------------------------------------------------------------*/
void server( short portno )
{
  struct sockaddr_in server_addr;
  int serverfd;
  scheduler_t *sched;
  fiber_t *fiber;

  /* create listening socket */
  serverfd = socket(AF_INET, SOCK_STREAM, 0);
  if ( serverfd < 0 ) {
    perror("ERROR opening server socket");
    exit(1);
  }

  /* bind it */
  bzero( (char*) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(portno);
 
  if ( bind(serverfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0 ) {
    perror("ERROR on binding");
    exit(1);
  }
   
  /* start listening */
  if ( listen(serverfd, BACKLOG) < 0 ) {
    perror("ERROR on listen");
    exit(1);
  }
 
  /* add server socket to watched fd set */
  cnxmaxfd = serverfd;
  FD_SET( serverfd, &cnxset );

  /* create scheduler */
  sched = scheduler_new();

  /* create the fiber that will accept
   * incoming connections */
  extra = (extra_t*) malloc(sizeof(extra_t));
  if ( extra == NULL ) {
    exit(1);
  }
  extra->fd = serverfd;
  extra->pause = 0;
  extra->hasdata = 0;

  fiber = fiber_new( accept_task, extra );
  fiber_set_stack_size( fiber, 8192);
  fiber_start( sched, fiber );

  while(1) {
    selectloop();
    scheduler_cycle( sched );
  }
}
