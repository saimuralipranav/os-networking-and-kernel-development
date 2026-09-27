#ifndef MASTERMIND_LOGGER_H
#define MASTERMIND_LOGGER_H

#include <stdarg.h>

/**
 * Initializes the logger. If enabled is true, opens log.txt in append mode.
 */
void logger_init(int enabled);

/**
 * Logs an event with microsecond timestamp format specified by the assignment.
 */
void logger_log(const char *fmt, ...);

/**
 * Closes the log file if open.
 */
void logger_close(void);

#endif /* MASTERMIND_LOGGER_H */
