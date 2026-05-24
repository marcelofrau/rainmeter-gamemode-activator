#include "logger.h"
#include <stdarg.h>
#include <time.h>
#include <string.h>

bool logger_init(Logger *log, const char *filepath) {
    if (!log) return false;
    log->console = false;
    log->file = fopen(filepath, "a");
    return log->file != NULL;
}

void logger_log(Logger *log, const char *fmt, ...) {
    if (!log) return;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);

    va_list args;
    va_start(args, fmt);

    if (log->console) {
        printf("[%s] ", timestamp);
        vprintf(fmt, args);
        printf("\n");
    }

    if (log->file) {
        fprintf(log->file, "[%s] ", timestamp);
        vfprintf(log->file, fmt, args);
        fprintf(log->file, "\n");
        fflush(log->file);
    }

    va_end(args);
}

void logger_close(Logger *log) {
    if (log && log->file) {
        fclose(log->file);
        log->file = NULL;
    }
}
