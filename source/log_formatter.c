/**
 * MIT License
 * 
 * Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
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

#include "log_formatter.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int log_format_append(char **buffer, size_t *length, size_t *capacity, const char *value, size_t value_size);
static const char *log_format_placeholder_value(const char *name, size_t name_size, const log_format_values_t *values);

char *log_format_render(const char *format, const log_format_values_t *values) {
    if(format == 0) {
        format = "{message}";
    }

    char *result = 0;
    size_t result_length = 0;
    size_t result_capacity = 0;

    for(size_t i = 0; format[i] != 0; ) {
        if(format[i] != '{') {
            if(log_format_append(&result, &result_length, &result_capacity, format + i, 1) != 0) {
                free(result);
                return 0;
            }
            i++;
            continue;
        }

        const char *placeholder_begin = format + i + 1;
        const char *placeholder_end = strchr(placeholder_begin, '}');
        if(placeholder_end == 0) {
            if(log_format_append(&result, &result_length, &result_capacity, format + i, strlen(format + i)) != 0) {
                free(result);
                return 0;
            }
            break;
        }

        const char *value = log_format_placeholder_value(placeholder_begin, (size_t)(placeholder_end - placeholder_begin), values);
        if(value != 0) {
            if(log_format_append(&result, &result_length, &result_capacity, value, strlen(value)) != 0) {
                free(result);
                return 0;
            }
        }
        else {
            size_t placeholder_size = (size_t)(placeholder_end - (format + i)) + 1;
            if(log_format_append(&result, &result_length, &result_capacity, format + i, placeholder_size) != 0) {
                free(result);
                return 0;
            }
        }

        i = (size_t)(placeholder_end - format) + 1;
    }

    if(result == 0) {
        result = (char *)malloc(1);
        if(result == 0) {
            return 0;
        }
        result[0] = 0;
    }

    return result;
}

char *log_format_current_timestamp(void) {
    struct timeval tv;
    if(gettimeofday(&tv, 0) != 0) {
        memset(&tv, 0, sizeof(tv));
    }

    unsigned long long ts_sec = (unsigned long long)(tv.tv_sec);
    unsigned int ts_msec = (unsigned int)(tv.tv_usec) / 1000;

    int size = snprintf(0, 0, "%llu.%03u", ts_sec, ts_msec);
    if(size < 0) {
        return 0;
    }

    char *timestamp = (char *)malloc((size_t)size + 1);
    if(timestamp == 0) {
        return 0;
    }

    snprintf(timestamp, (size_t)size + 1, "%llu.%03u", ts_sec, ts_msec);
    return timestamp;
}

char *log_format_resolve_device_name(void) {
    char *env_device_name = getenv("DEVICE_NAME");
    if((env_device_name != 0) && (env_device_name[0] != 0)) {
        return strdup(env_device_name);
    }

    char hostname[256];
    if(gethostname(hostname, sizeof(hostname)) == 0) {
        hostname[sizeof(hostname) - 1] = 0;
        if(hostname[0] != 0) {
            return strdup(hostname);
        }
    }

    return strdup("");
}

static int log_format_append(char **buffer, size_t *length, size_t *capacity, const char *value, size_t value_size) {
    if(value_size == 0) {
        return 0;
    }

    size_t required_capacity = *length + value_size + 1;
    if(required_capacity > *capacity) {
        size_t new_capacity = *capacity ? *capacity : 64;
        while(new_capacity < required_capacity) {
            new_capacity *= 2;
        }

        char *new_buffer = (char *)realloc(*buffer, new_capacity);
        if(new_buffer == 0) {
            return -1;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, value, value_size);
    *length += value_size;
    (*buffer)[*length] = 0;
    return 0;
}

static const char *log_format_placeholder_value(const char *name, size_t name_size, const log_format_values_t *values) {
    if(values == 0) {
        return 0;
    }

    if((name_size == strlen("timestamp")) && (strncmp(name, "timestamp", name_size) == 0)) {
        return values->timestamp ? values->timestamp : "";
    }

    if((name_size == strlen("app-name")) && (strncmp(name, "app-name", name_size) == 0)) {
        return values->app_name ? values->app_name : "";
    }

    if((name_size == strlen("device-name")) && (strncmp(name, "device-name", name_size) == 0)) {
        return values->device_name ? values->device_name : "";
    }

    if((name_size == strlen("message")) && (strncmp(name, "message", name_size) == 0)) {
        return values->message ? values->message : "";
    }

    return 0;
}
