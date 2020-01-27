#ifndef __LIBFIBER_LOGGER_H__
#define __LIBFIBER_LOGGER_H__

enum log_level_e {
  LOG_FATAL = 0,
  LOG_ERROR,
  LOG_WARN,
  LOG_INFO,
  LOG_DEBUG,
  LOG_TRACE
};

void set_log_level( uint8_t level);
void fatal( const char *fmt, ...);
void info( const char *fmt, ...);
void error( const char *fmt, ...);
void warn( const char *fmt, ...);
void debug( const char *fmt, ...);
void trace( const char *fmt, ...);

#endif
