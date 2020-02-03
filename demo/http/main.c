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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

#include "card.h"


/* defined in b64.c
 * decode b64 encoded input string 'sin' into 'sout'
 * returns the total number of bytes writtent to sout
 * which must be large enough
 */
extern int b64decode( char *sin, char *sout );

/* defined in reqhandler.c
 * create a server listening on port portno
 * and handles incoming requests */
extern void server( short portno );


card_t *allcards[] =
  {
   /* spades */
   &card_2s, &card_3s, &card_4s, &card_5s, &card_6s, &card_7s, &card_8s, &card_9s,
   &card_ts, &card_js, &card_qs, &card_ks, &card_as,
   /* hearts */
   &card_2h, &card_3h, &card_4h, &card_5h, &card_6h, &card_7h, &card_8h, &card_9h,
   &card_th, &card_jh, &card_qh, &card_kh, &card_ah,
   /* diamonds */
   &card_2d, &card_3d, &card_4d, &card_5d, &card_6d, &card_7d, &card_8d, &card_9d,
   &card_td, &card_jd, &card_qd, &card_kd, &card_ad,
   /* clubs */
   &card_2c, &card_3c, &card_4c, &card_5c, &card_6c, &card_7c, &card_8c, &card_9c,
   &card_tc, &card_jc, &card_qc, &card_kc, &card_ac,
  };

static int init()
{
  char buffer[64];
  int i;
  for ( i = 0; i < 52; ++i ) {
    card_t *pcard = allcards[i];
    pcard->bin = malloc(strlen(pcard->b64));
    pcard->sz = b64decode( pcard->b64, pcard->bin );
  }
}

int main( int argc, char **argv )
{
  signal( SIGPIPE, SIG_IGN );
  init();
  server(5001);
  return 0;
}

