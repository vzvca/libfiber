#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>


enum log_level_e {
  LOG_FATAL = 0,
  LOG_ERROR,
  LOG_WARN,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE
};

// --------------------------------------------------------------------------
//  LOG level
// --------------------------------------------------------------------------
static uint8_t log_level = LOG_INFO;

// --------------------------------------------------------------------------
//   Prefix for log messages
// --------------------------------------------------------------------------
void log_prefix( FILE *fout, uint8_t level)
{
  char *s = "?????";
  struct timeval tv;
  time_t nowtime;
  struct tm *nowtm;
  char tmbuf[64], buf[96];
  
  switch(level) {
  case LOG_FATAL: s = " FATAL "; break;
  case LOG_ERROR: s = " ERROR "; break;
  case LOG_WARN:  s = " WARN  "; break;
  case LOG_INFO:  s = " INFO  "; break;
  case LOG_DEBUG: s = " DEBUG "; break;
  case LOG_TRACE: s = " TRACE "; break;
  };

  gettimeofday(&tv, NULL);
  nowtime = tv.tv_sec;
  nowtm = localtime(&nowtime);
  strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", nowtm);
  snprintf(buf, sizeof(buf), "%s.%06ld", tmbuf, tv.tv_usec);

  fprintf( fout, "%s - [%s] - ", buf, s);
}

// --------------------------------------------------------------------------
//   information level
// --------------------------------------------------------------------------
void info( const char *fmt, ...)
{
  va_list va;
  if ( log_level >= LOG_INFO ) {
    log_prefix( stdout, LOG_INFO );
    va_start(va, fmt);
    vfprintf( stdout, fmt, va);
    va_end(va);
  }
}

// --------------------------------------------------------------------------
//   information level
// --------------------------------------------------------------------------
void debug( const char *fmt, ...)
{
  va_list va;
  if ( log_level >= LOG_DEBUG ) {
    log_prefix( stdout, LOG_DEBUG );
    va_start(va, fmt);
    vfprintf( stdout, fmt, va);
    va_end(va);
  }
}

// --------------------------------------------------------------------------
//   information level
// --------------------------------------------------------------------------
void trace( const char *fmt, ...)
{
  va_list va;
  if ( log_level >= LOG_TRACE ) {
    log_prefix( stdout, LOG_TRACE );
    va_start(va, fmt);
    vfprintf( stdout, fmt, va);
    va_end(va);
  }
}

// --------------------------------------------------------------------------
//   information level
// --------------------------------------------------------------------------
void fatal( const char *fmt, ...)
{
  va_list va;
  log_prefix( stderr, LOG_FATAL );
  va_start(va, fmt);
  vfprintf( stderr, fmt, va);
  va_end(va);
  exit(1);
}

// --------------------------------------------------------------------------
//   assert like log function
// --------------------------------------------------------------------------
void fatalif( int expr, const char *fmt, ...)
{
  if ( expr ) {
    va_list va;
    log_prefix( stderr, LOG_FATAL );
    va_start(va, fmt);
    vfprintf( stderr, fmt, va);
    va_end(va);
    exit(1);
  }
}

// --------------------------------------------------------------------------
//   assert like log function
// --------------------------------------------------------------------------
void errorif( int expr, const char *fmt, ...)
{
  if ( expr ) {
    va_list va;
    log_prefix( stderr, LOG_FATAL );
    va_start(va, fmt);
    vfprintf( stderr, fmt, va);
    va_end(va);
  }
}

// --------------------------------------------------------------------------
//   information level
// --------------------------------------------------------------------------
void error( const char *fmt, ...)
{
  va_list va;
  log_prefix( stderr, LOG_ERROR );
  va_start(va, fmt);
  vfprintf( stderr, fmt, va);
  va_end(va);
}

// --------------------------------------------------------------------------
//   information level
// --------------------------------------------------------------------------
void warn( const char *fmt, ...)
{
  va_list va;
  log_prefix( stderr, LOG_WARN );
  va_start(va, fmt);
  vfprintf( stdout, fmt, va);
  va_end(va);
}

// --------------------------------------------------------------------------
//   Sets the log level
// --------------------------------------------------------------------------
void set_log_level( uint8_t level)
{
  if ( level >= LOG_INFO && level <= LOG_TRACE ) {
    log_level = level;
  }
  else {
    error("Invalid log level requested %d. Ignored...\n", level);
  }
}
