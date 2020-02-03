#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int parse_header( FILE *fin )
{
  char line[64] = "";
  int crnl = 1, sz = 0;

  fprintf( stderr, "parse header\n");
  
  while( fgets( line, sizeof(line), fin) != NULL ) {
    fprintf( stderr, "*** %s", line);
    if ( line[0] == '\r' ) {
      if ( crnl == 0 ) break;
    }
    crnl = 0;
    if ( strncmp( line, "Content-Length: ", 16) == 0 ) {
      sscanf( line, "Content-Length: %d\r", &sz );
    }
  }

  if (sz == 0) {
    fprintf( stderr, "Unable to parse size!\n");
    exit(1);
  }
  fprintf( stderr, "*** SIZE %d\n", sz);

  return sz;
}



int main( int argc, char **argv )
{
  char *s;
  FILE *fin;
  int sz = 0;
  
  fin = fopen( argv[1], "rb");
  
  do {
    /* add blank line */
    if ( sz > 0 ) {
      printf("\r\n");
    }
    sz = parse_header(fin);
    
    s = malloc(sz);
    if ( fread( s, 1, sz, fin) == sz ) {
      printf("--myboundary\r\n");
      printf("Content-Type: image/jpeg\r\n");
      printf("Content-Length: %d\r\n", sz);
      printf("\r\n");
      fflush( stdout);
      fwrite( s, 1, sz, stdout ); 
      fflush( stdout);
    }
    else {
      sz = 0;
    }
    free(s);
    
  } while(sz);
  
  fclose(fin);
}
  
