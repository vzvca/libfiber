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
#include "card.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* data associated to a fiber */
typedef struct extra_s {
  int fd;
  int pause;
} extra_t;

static char *boundary = "--boundarydonotcross";

/* defined in main.c */
extern card_t *allcards[];

static int get_fiber_fd( fiber_t *f )
{
  extra_t *data = (extra_t*) fiber_get_extra(f);
  return data->fd;
}

static int get_fiber_pause( fiber_t *f )
{
  extra_t *data = (extra_t*) fiber_get_extra(f);
  return data->pause;
}

/* Write, checking for errors. */
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

static char *date()
{
  return "Mon, 1 Jan 2030 12:34:56 GMT";
}

static void request_headers( int fd )
{
  writeln( fd, "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0");
  writeln( fd, "Connection: close");
  writeln( fd, "Content-Type: multipart/x-mixed-replace;boundary=%s", boundary);
  writeln( fd, "Expires: %s", date());
  writeln( fd, "Pragma': no-cache");
  writeln( fd, "" );
}

static void image_headers( int fd, card_t *card )
{
  writeln( fd, "X-Timestamp: time.time()");
  writeln( fd, "Content-Length: %d", card->sz );
  /* mime-type must be set according file content*/
  writeln( fd, "Content-Type: image/gif");
  writeln( fd, "" );
} 

void image_run( fiber_t *fiber )
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

void error404_run( fiber_t *fiber )
{
  int fd = get_fiber_fd(fiber);
  
  /* reply 404 and close connection */
  send_response( fd, 404);
}

void home_run( fiber_t *fiber )
{
  int fd = get_fiber_fd(fiber);
  
  /* reply 200, send home page and close connection */
  send_response( fd, 404);
}

/* --------------------------------------------------------------------------
 *  On accept a fiber is created which uses this function
 *  Depending on the web page requested, another function will be called
 *  to handle the request.
 * --------------------------------------------------------------------------*/
void generic_run( fiber_t *fiber )
{
  char *location;

  if ( strcmp(location, "/") == 0 ) {
    home_run( fiber );
  }
  else if ( isdigit(location[0]) ) {
  }
  else {
    error404_run( fiber );
  }
}

void done( fiber_t *fiber )
{
  int fd = get_fiber_fd(fiber);
  close(fd);
}


int seloop(void) {
  fd_set rfds;
  struct timeval tv;
  int retval;

  /* Watch stdin (fd 0) to see when it has input. */
  FD_ZERO(&rfds);
  FD_SET(0, &rfds);

  /* Wait up to five seconds. */
  tv.tv_sec = 5;
  tv.tv_usec = 0;

  retval = select(1, &rfds, NULL, NULL, &tv);
  /* Don't rely on the value of tv now! */

  if (retval == -1)
    perror("select()");
  else if (retval)
    printf("Data is available now.\n");
  /* FD_ISSET(0, &rfds) will be true. */
  else
    printf("No data within five seconds.\n");
}

void mktask( schedulter_t *sched, int fd )
{
  extra_t *pextra;
  fiber_t *fiber;

  extra = (extra_t*) malloc(sizeof(extra_t));
  extra->fd = fd;
  extra->pause = 200 + (rand() % 800); /* pause between 200ms to 1s */
  fiber = fiber_new( generic_run, extra);
  fiber_set_stack_size( fiber, 1024);
  fiber_set_done_func( fiber, done);
  fiber_start( sched, fiber);
}




  
