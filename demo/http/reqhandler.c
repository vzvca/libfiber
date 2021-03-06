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
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "task.h"
#include "card.h"

/* data associated to a fiber */
typedef struct extra_s {
  int fd;       /* socket attached to fiber */
  int hasdata;  /* tells if data is ready on socket */
  FILE *fin;    /* some fibers open a file, that where they store it */
} extra_t;

/* boundary used for multipart data */
/* this boundary is used in the video sample */
static char *boundary = "--myboundary";

/* this set contains the current sockets to watch */
static fd_set cnxset;
static int  cnxmaxfd = 0;

#define BACKLOG 5
#define CNXMAX 64
static fiber_t *cnx2fiber[CNXMAX];

/* all the cards, defined in main.c */
extern card_t *allcards[];

#define MSEC 100

/* --------------------------------------------------------------------------
 *  Get the socket of fiber
 * --------------------------------------------------------------------------*/
static int get_fiber_fd( fiber_t *f )
{
  extra_t *data = (extra_t*) fiber_get_extra(f);
  return data->fd;
}

/* --------------------------------------------------------------------------
 *  Set the hasdata flag of fiber
 * --------------------------------------------------------------------------*/
static void set_fiber_hasdata( fiber_t *fiber, int val )
{
  extra_t *extra = (extra_t*) fiber_get_extra(fiber);
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
    if (s < 0 ) return;
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
  safewrite( fd, "\r\n", 2);
}

/* --------------------------------------------------------------------------
*  Send HTTP return code
* Only 2 codes supported
* --------------------------------------------------------------------------*/
static void send_response( int fd, int code )
{
  switch( code ) {
  case 200:
    writeln( fd, "HTTP/1.1 200 OK");
    break;
  default:
    writeln( fd, "HTTP/1.1 404 Not Found");
    break;
  }
}

/* --------------------------------------------------------------------------
 *  Send headers
 * --------------------------------------------------------------------------*/
static void request_headers( int fd )
{
  writeln( fd, "Content-Type: multipart/x-mixed-replace; boundary=\"%s\"", boundary+2);
  writeln( fd, "" );
}

/* --------------------------------------------------------------------------
 *  Send image headers
 * --------------------------------------------------------------------------*/
static void image_headers( int fd, card_t *card )
{
  /* mime-type must be set according file content*/
  writeln( fd, "Content-Type: image/gif");
  writeln( fd, "Content-Length: %d", card->sz );
  writeln( fd, "" );
} 

/* --------------------------------------------------------------------------
 *  Pause fiber and discard input
 * --------------------------------------------------------------------------*/
static void pausef( fiber_t *fiber )
{
  int n, nothing = 1, fd = get_fiber_fd(fiber);
  extra_t *extra = fiber_get_extra( fiber );
  char buffer[4096];

 redo:
  fiber_yield( fiber );

  /* data pending ? */
  if ( extra->hasdata ) {
    extra->hasdata = 0;
    do {
      n = read( fd, buffer, sizeof(buffer));
      if ( n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK) ) {
	error("not ready !\n");
	goto redo;
      }
      if ( n > 0 ) {
	nothing = 0;
      }
    } while ( n == sizeof(buffer) );
    if ( nothing ) {
      error("remote end close connection !\n");
      fiber_stop( fiber );
      fiber_yield( fiber );
    }
  }
}

/* --------------------------------------------------------------------------
 *  Send mjpeg video
 * --myboundary
 * Content-Type: image/jpeg
 * Content-Length: 42149
 * --------------------------------------------------------------------------*/
void video( fiber_t *fiber, char *fname )
{
  extra_t *extra = fiber_get_extra( fiber );
  int   n, fd = get_fiber_fd(fiber);
  char  buffer[4096];

  /* reply OK */
  send_response( fd, 200);
     
  /* Response headers (multipart) */
  request_headers( fd );

  while(1) {
    extra->fin = fopen( fname, "rb" );
    do {
      n = fread( buffer, 1, sizeof(buffer), extra->fin);
      safewrite( fd, buffer, n );
      pausef( fiber );
    } while(n == sizeof(buffer));
    fclose(extra->fin);
    extra->fin = NULL;
  }
}

/* --------------------------------------------------------------------------
 *  Sends video of traffic in New York
 * --------------------------------------------------------------------------*/
void traffic( fiber_t *fiber )
{
  video( fiber, "www/traffic.mjpeg" );
}

/* --------------------------------------------------------------------------
 *  Sends video of a bar in Barcelona
 * --------------------------------------------------------------------------*/
void ovisobar( fiber_t *fiber )
{
  video( fiber, "www/oviso.mjpeg" );
}

/* --------------------------------------------------------------------------
 *  Sends music
 * --------------------------------------------------------------------------*/
void music( fiber_t *fiber, char *fname )
{
  extra_t *extra = fiber_get_extra( fiber );
  int   n, fd = get_fiber_fd(fiber);
  char  buffer[4096];

  /* reply OK */
  send_response( fd, 200);
     
  /* Response headers */
  writeln( fd, "Content-Type: audio/mpeg");
  writeln( fd, "" );

  extra->fin = fopen( fname, "rb" );
  do {
    n = fread( buffer, 1, sizeof(buffer), extra->fin);
    safewrite( fd, buffer, n );
    pausef( fiber );
  } while(n == sizeof(buffer));
  fclose(extra->fin);
  extra->fin = NULL;
}

/* --------------------------------------------------------------------------
 *  Sends music
 * --------------------------------------------------------------------------*/
void meydan( fiber_t *fiber )
{
  music( fiber, "www/Meydn-SynthwaveVibe.mp3");
}

/* --------------------------------------------------------------------------
 *  Checks if it is a card
 * --------------------------------------------------------------------------*/
int iscard( char *name )
{
  int i;
  if (strcmp( name + 2, ".gif" )) {
    return 0;
  }
  for( i = 0; i < 52 ; ++i ) {
    if ( strncmp( name, allcards[i]->name, 2) == 0 ) return 1;
  }
  return 0;
}

/* --------------------------------------------------------------------------
 *  Send error 404 reply
 * --------------------------------------------------------------------------*/
void error404( fiber_t *fiber )
{
  int fd = get_fiber_fd(fiber);
  send_response( fd, 404);

  writeln( fd, "Server: libfiber (linux)");
  writeln( fd, "Content-Type: text/html; charset=iso-8859-1");
  writeln( fd, "Content-Length: 0");
  writeln( fd, "Connection: Closed");
  writeln( fd, "");
  writeln( fd, "<h1>Error 404 : Not Found</h1>");
}

/* --------------------------------------------------------------------------
 *  Sends an image
 * --------------------------------------------------------------------------*/
void card( fiber_t *fiber, char *name )
{
  int fd = get_fiber_fd(fiber);
  int i;
  for ( i = 0; i < 52; ++i ) {
    if ( strncmp( name, allcards[i]->name, 2 ) == 0 ) break;
  }
  if ( i < 52 ) {
    send_response( fd, 200);
    image_headers( fd, allcards[i] );
    safewrite( fd, allcards[i]->bin, allcards[i]->sz );
  }
  else {
    error404( fiber );
  }
}

/* --------------------------------------------------------------------------
 *  Send home page
 * --------------------------------------------------------------------------*/
void html( fiber_t *fiber, char *fname )
{
  extra_t *extra = fiber_get_extra( fiber );
  int n, fd = get_fiber_fd(fiber);
  char  buffer[4096];
  
  send_response( fd, 200);
  
  writeln( fd, "Server: libfiber (linux)");
  writeln( fd, "Connection: Closed");
  writeln( fd, "Content-Type: text/html; charset=iso-8859-1");
  writeln( fd, "" );
  
  extra->fin = fopen( fname, "rb" );
  do {
    n = fread( buffer, 1, sizeof(buffer), extra->fin);
    safewrite( fd, buffer, n );
    fiber_yield( fiber);
  } while(n == sizeof(buffer));
  fclose(extra->fin);
  extra->fin = NULL;
}

/* --------------------------------------------------------------------------
 *  Send home page
 * --------------------------------------------------------------------------*/
void home( fiber_t *fiber )
{
  html( fiber, "www/index.html" );
}

/* --------------------------------------------------------------------------
 *  Send cards
 * --------------------------------------------------------------------------*/
void deck52( fiber_t *fiber )
{
  html( fiber, "www/cards.html" );
}

/* --------------------------------------------------------------------------
 *  On accept a fiber is created which uses this function
 *  Depending on the web page requested, another function will be called
 *  to handle the request.
 * --------------------------------------------------------------------------*/
void generic_task( fiber_t *fiber )
{
  extra_t *extra = (extra_t*) fiber_get_extra( fiber );
  int n, ret, fd = get_fiber_fd( fiber );
  char buffer[4096];
  char *location;

  /* wait for incoming data */
  do {
    extra->hasdata = 0;
    ret = fiber_wait_for_var( fiber, MSEC, &extra->hasdata, 1);
  } while( FIBER_TIMEOUT == ret );

  
  /* read request */
  n = read( fd, buffer, sizeof(buffer));
  puts(buffer);
  if ( n > 0 ) {
    if ( strncmp( buffer, "GET ", 4 ) == 0 ) {
      location = strtok( buffer + 4, " " );
      
      if ( strcmp(location, "/") == 0 ) {
	debug("home page");
	home( fiber );
      }
      else if ( strcmp(location+1, "index.html") == 0 ) {
	debug("home page");
	home( fiber );
      }
      else if ( strcmp(location+1, "cards.html") == 0 ) {
	debug("52 cards html page");
	deck52( fiber );
      }
      else if ( strncmp(location+1, "traffic.mjpeg", 11) == 0 ) {
	debug("traffic video");
	traffic( fiber );
      }
      else if ( strncmp(location+1, "bar.mjpeg", 11) == 0 ) {
	debug("bar in Spain video");
	ovisobar( fiber );
      }
      else if ( strncmp(location+1, "meydan.mp3", 11) == 0 ) {
	debug("music");
	meydan( fiber );
      }
      else if ( iscard(location+1) ) {
	debug("card");
	card( fiber, location + 1 );
      }
      else {
	puts("404");
	error404( fiber );
      }
    }
    else {
      /* only GET supported */
      error404( fiber );
    }
  }
}

/* --------------------------------------------------------------------------
 *  Called once fiber has finished running
 * --------------------------------------------------------------------------*/
void done( fiber_t *fiber )
{
  extra_t *extra = fiber_get_extra( fiber );
  int i, fd = get_fiber_fd(fiber);

  if ( extra->fin != NULL ) {
    fclose( extra->fin );
  }
  
  for ( i = 0; i < CNXMAX; ++i ) {
    if ( cnx2fiber[i] == fiber ) {
      cnx2fiber[i] = NULL;
    }
  }
  close(fd);
  FD_CLR( fd, &cnxset );
  /* recompute cnxmaxfd if needed */
  if ( cnxmaxfd == fd ) {
    for( i = cnxmaxfd = 0; i < CNXMAX; ++i ) {
      if ( cnx2fiber[i] != NULL ) {
	fd = get_fiber_fd( cnx2fiber[i] );
	if ( fd > cnxmaxfd ) {
	  cnxmaxfd = fd;
	}
      }
    }
  }
  /* safe to free fiber. Its stack has already been deallocated */
  free( fiber_get_extra( fiber ) );
  free( fiber );
}


/* --------------------------------------------------------------------------
 *  Call select to find which connection has pending data to read
 *  Arrange for the fiber handling th connection to wake up.
 *
 *  This function is called at the beginnig of each scheduler cycle.
 * --------------------------------------------------------------------------*/
int selectloop(void) {
  fd_set rdfs;
  struct timeval tv;
  int i, retval;

  /* Watch sockets to see when they have input. */
  FD_ZERO(&rdfs);
  memcpy( &rdfs, &cnxset, sizeof(rdfs) );

  /* Wait up to 50 msecs. */
  tv.tv_sec = 0;
  tv.tv_usec = 5000;

  retval = select( cnxmaxfd+1, &rdfs, NULL, NULL, &tv);

  if (retval == -1) {
    perror("select()");
    
  } else if (retval) {
    /* loop over connections */
    for( i = 0; i < CNXMAX; ++i ) {
      fiber_t *fiber = cnx2fiber[i];
      if ( fiber != NULL ) {
	extra_t *extra = fiber_get_extra( fiber );
	int fd = get_fiber_fd( fiber );
	if ( FD_ISSET( fd, &rdfs) ) {
	  /* it will wake up the fiber */
	  extra->hasdata = 1; 
	}
      }
    }
  }
}

/* --------------------------------------------------------------------------
 *  Create a task to handle connection
 * --------------------------------------------------------------------------*/
void mktask( scheduler_t *sched, int fd )
{
  extra_t *extra;
  fiber_t *fiber;
  int i;

  for( i = 0 ; i < CNXMAX; ++i ) {
    if ( cnx2fiber[i] == NULL ) {
      break;
    }
  }
  if ( i == CNXMAX ) {
    error("Maximum number of connection reached.\n");
  }
  else { 
    int flags;

    extra = (extra_t*) malloc(sizeof(extra_t));
    extra->fd = fd;
    extra->hasdata = 0;
    extra->fin = NULL;

    flags = fcntl(fd ,F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    FD_SET( fd, &cnxset );
    if ( fd > cnxmaxfd ) {
      cnxmaxfd = fd;
    }
  
    fiber = fiber_new( generic_task, extra);
    fiber_set_done_func( fiber, done);
    fiber_start( sched, fiber);

    cnx2fiber[i] = fiber;
  }
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
  extra_t *extra;

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
  sched = sched_new();

  /* create the fiber that will accept
   * incoming connections */
  extra = (extra_t*) malloc(sizeof(extra_t));
  if ( extra == NULL ) {
    exit(1);
  }
  extra->fd = serverfd;
  extra->hasdata = 0;

  fiber = fiber_new( accept_task, extra );
  fiber_start( sched, fiber );

  cnx2fiber[0] = fiber;
  
  while(1) {
    selectloop();
    sched_cycle( sched, sched_elapsed() );
  }
}
