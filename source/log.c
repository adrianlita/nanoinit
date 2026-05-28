/**
 * MIT License
 * 
 * Copyright (c) 2022 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
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
 * */

#define _GNU_SOURCE         //for vasprintf

#include "log.h"
#include "log_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int instances = 0;
static int app_verbosity_level = 0;
static FILE *log_file = 0;
static char *log_format = 0;
static char *log_device_name = 0;

#define LOG_DEFAULT_FORMAT "{message}"
#define LOG_APP_NAME "nanoinit"

static const char *log_get_format(void);
static const char *log_get_device_name(void);
static void log_write_line(FILE *output, const char *line);

int log_init(int verbosity_level, const char *log_path) {
    instances++;
    if(instances > 1) {
        log_ni_error("log_init() called too many times");
        return -1;
    }

    char *format_env = getenv("NI_LOG_FORMAT");
    log_format = strdup(format_env ? format_env : LOG_DEFAULT_FORMAT);
    log_device_name = log_format_resolve_device_name();

    if((verbosity_level < 0) || (verbosity_level > 2)) {
        log_ni_error("log_init() invalid verbosity level: %d", verbosity_level);
        return -2;
    }

    app_verbosity_level = verbosity_level;
    if(log_path) {
        log_file = fopen(log_path, "w");
        if(log_file == 0) {
            log_ni_error("log_init() could not open logfile '%s' for writing; logging to file is disabled", log_path);
            return -3;
        }
    }

    return 0;
}

void log_free(void) {
    if(log_file) {
        fclose(log_file);
        log_file = 0;
    }

    free(log_format);
    log_format = 0;
    free(log_device_name);
    log_device_name = 0;

    instances--;
    app_verbosity_level = 0;
}

void _log_add(int verbosity_level, const char *format, ...) {
    if((verbosity_level < 0) || (verbosity_level > 2)) {
        log_ni_error("_log_add() invalid verbosity level: %d; assuming NI-ERROR", verbosity_level);
        verbosity_level = 0;
    }

    if(format == 0) {
        log_ni_error("_log_add() invalid format parameter");
        return;
    }

    char *message = 0;
    va_list arg;
    va_start(arg, format);
    int result = vasprintf(&message, format, arg);
    va_end(arg);
    (void)result;

    if(message == 0) {
        message = strdup(format);
        if(message == 0) {
            return;
        }
    }

    char *timestamp = log_format_current_timestamp();
    log_format_values_t values = {
        .timestamp = timestamp ? timestamp : "",
        .app_name = LOG_APP_NAME,
        .device_name = log_get_device_name(),
        .message = message,
    };

    char *rendered_message = log_format_render(log_get_format(), &values);
    if(rendered_message == 0) {
        rendered_message = strdup(message);
        if(rendered_message == 0) {
            free(timestamp);
            free(message);
            return;
        }
    }

    //log to file
    if(log_file) {
        log_write_line(log_file, rendered_message);
        fflush(log_file);
    }

    //print
    if(verbosity_level <= app_verbosity_level) {
        FILE *output = stderr;
        if(verbosity_level > 1) {
            output = stdout;
        }

        log_write_line(output, rendered_message);
    }

    free(rendered_message);
    free(timestamp);
    free(message);
}

static const char *log_get_format(void) {
    return log_format ? log_format : LOG_DEFAULT_FORMAT;
}

static const char *log_get_device_name(void) {
    return log_device_name ? log_device_name : "";
}

static void log_write_line(FILE *output, const char *line) {
    fputs(line, output);
    fputc('\n', output);
}
