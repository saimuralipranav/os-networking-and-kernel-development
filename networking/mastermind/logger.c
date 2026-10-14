#include "logger.h"
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

static FILE *log_file = NULL;
static int logging_enabled = 0;

void logger_init(int enabled) {
    logging_enabled = enabled;
    if (logging_enabled) {
        log_file = fopen("log.txt", "a");
    }
}

void logger_log(const char *fmt, ...) {
    if (!logging_enabled || !log_file) {
        return;
    }

    char time_buffer[30];
    struct timeval tv;
    time_t curtime;

    gettimeofday(&tv, NULL);
    curtime = tv.tv_sec;

    /* Format the time part per specification */
    strftime(time_buffer, 30, "%Y-%m-%d %H:%M:%S", localtime(&curtime));

    /* Format message */
    char msg_buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Print to log file and flush */
    fprintf(log_file, "[%s.%06ld] [LOG] %s\n", time_buffer, (long)tv.tv_usec, msg_buf);
    fflush(log_file);
}

void logger_close(void) {
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    logging_enabled = 0;
}
