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

/* THIS CODE HAS BEEN BORROWED FROM INTERNET - SEE BELOW */

/*
  cdecoder.c - c source to a base64 decoding algorithm implementation

  This is part of the libb64 project, and has been placed in the public domain.
  For details, see http://sourceforge.net/projects/libb64
*/

#include <string.h>

typedef enum
  {
   step_a, step_b, step_c, step_d
  } base64_decodestep;

typedef struct
{
  base64_decodestep step;
  char plainchar;
} base64_decodestate;

static int base64_decode_value(char value_in)
{
  static const char decoding[] =
    { 62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,
      -1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,
      21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,32,33,34,
      35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};
  static const char decoding_size = sizeof(decoding);
  value_in -= 43;
  if (value_in < 0 || value_in > decoding_size) return -1;
  return decoding[(int)value_in];
}

static void base64_init_decodestate(base64_decodestate* state_in)
{
  state_in->step = step_a;
  state_in->plainchar = 0;
}

static int base64_decode_block(const char* code_in, const int length_in, char* plaintext_out, base64_decodestate* state_in)
{
  const char* codechar = code_in;
  char* plainchar = plaintext_out;
  char fragment;
	
  *plainchar = state_in->plainchar;
	
  switch (state_in->step)
    {
      while (1)
	{
	case step_a:
	  do {
	    if (codechar == code_in+length_in)
	      {
		state_in->step = step_a;
		state_in->plainchar = *plainchar;
		return plainchar - plaintext_out;
	      }
	    fragment = (char)base64_decode_value(*codechar++);
	  } while (fragment < 0);
	  *plainchar    = (fragment & 0x03f) << 2;
	case step_b:
	  do {
	    if (codechar == code_in+length_in)
	      {
		state_in->step = step_b;
		state_in->plainchar = *plainchar;
		return plainchar - plaintext_out;
	      }
	    fragment = (char)base64_decode_value(*codechar++);
	  } while (fragment < 0);
	  *plainchar++ |= (fragment & 0x030) >> 4;
	  *plainchar    = (fragment & 0x00f) << 4;
	case step_c:
	  do {
	    if (codechar == code_in+length_in)
	      {
		state_in->step = step_c;
		state_in->plainchar = *plainchar;
		return plainchar - plaintext_out;
	      }
	    fragment = (char)base64_decode_value(*codechar++);
	  } while (fragment < 0);
	  *plainchar++ |= (fragment & 0x03c) >> 2;
	  *plainchar    = (fragment & 0x003) << 6;
	case step_d:
	  do {
	    if (codechar == code_in+length_in)
	      {
		state_in->step = step_d;
		state_in->plainchar = *plainchar;
		return plainchar - plaintext_out;
	      }
	    fragment = (char)base64_decode_value(*codechar++);
	  } while (fragment < 0);
	  *plainchar++   |= (fragment & 0x03f);
	}
    }
  /* control should not reach here */
  return plainchar - plaintext_out;
}

int b64decode( char *sin, char *sout )
{
  base64_decodestate state;
  base64_init_decodestate( &state);
  return base64_decode_block( sin, strlen(sin), sout, &state);
}
