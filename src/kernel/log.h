
// log.h
//
// Logging functions.
//
// Adapted from <http://github.com/littleosbook/aenix>.

#ifndef LOG_H
#define LOG_H

void log_debug(char *fname, char *fmt, ...);
void log_info(char *fname, char *fmt, ...);
void log_error(char *fname, char *fmt, ...);

#endif /* LOG_H */
