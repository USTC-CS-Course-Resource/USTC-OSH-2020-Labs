#ifndef __UTILITY_H
#define __UTILITY_H

#include <stdio.h>  
#include <unistd.h>
#include <stdarg.h>         // For args
#include <stdlib.h>         // For mkdtemp(3)
/*
 * This part is for utilities
 */ 

void error_exit(int code, const char *message) {
    perror(message);
    _exit(code);
}

void logger(const char *type, const char *format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[container][%s] ", type);
    vfprintf(stderr, format, args);
    va_end(args);
} 

void errexit(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    perror("");
    va_end(args);
    exit(1);
} 

#endif