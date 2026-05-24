#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdbool.h>

typedef struct {
    FILE *file;
    bool  console;
} Logger;

bool logger_init(Logger *log, const char *filepath);
void logger_log(Logger *log, const char *fmt, ...);
void logger_close(Logger *log);

#endif
