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

#include "internal.h"
#include "log.h"
#include "log_formatter.h"

#include <stdlib.h>

typedef struct supervisor_output_path_format_s {
    char *timestamp;
    char *timestamp_iso;
    char *device_name;
} supervisor_output_path_format_t;

static void output_path_format_init(supervisor_output_path_format_t *format);
static void output_path_format_free(supervisor_output_path_format_t *format);
static char *output_path_render(const char *path, const char *application_name, const supervisor_output_path_format_t *format);

int supervisor_output_paths_render(const nanoinit_application_config_t *application, supervisor_output_paths_t *paths) {
    if((application == 0) || (paths == 0)) {
        return -1;
    }

    const char *application_name = application->name ? application->name : "";
    const char *log_application_name = application->name ? application->name : "(unknown)";

    paths->stdout_path = 0;
    paths->stderr_path = 0;

    supervisor_output_path_format_t format;
    output_path_format_init(&format);

    paths->stdout_path = output_path_render(application->stdout_path, application_name, &format);
    if((application->stdout_path != 0) && (paths->stdout_path == 0)) {
        output_path_format_free(&format);
        log_ni_error("supervisor_output_paths_render() could not render stdout path for app %s", log_application_name);
        return -1;
    }

    paths->stderr_path = output_path_render(application->stderr_path, application_name, &format);
    output_path_format_free(&format);
    if((application->stderr_path != 0) && (paths->stderr_path == 0)) {
        supervisor_output_paths_free(paths);
        log_ni_error("supervisor_output_paths_render() could not render stderr path for app %s", log_application_name);
        return -1;
    }

    return 0;
}

void supervisor_output_paths_free(supervisor_output_paths_t *paths) {
    if(paths == 0) {
        return;
    }

    free(paths->stdout_path);
    free(paths->stderr_path);
    paths->stdout_path = 0;
    paths->stderr_path = 0;
}

const char *supervisor_output_path_redirect_target(const char *path, const char *pipe_target) {
    if(path == 0) {
        return pipe_target;
    }

    if(path[0] == 0) {
        return "/dev/null";
    }

    return path;
}

static void output_path_format_init(supervisor_output_path_format_t *format) {
    format->timestamp = log_format_current_timestamp();
    format->timestamp_iso = log_format_current_timestamp_iso();
    format->device_name = log_format_resolve_device_name();
}

static void output_path_format_free(supervisor_output_path_format_t *format) {
    free(format->timestamp);
    free(format->timestamp_iso);
    free(format->device_name);

    format->timestamp = 0;
    format->timestamp_iso = 0;
    format->device_name = 0;
}

static char *output_path_render(const char *path, const char *application_name, const supervisor_output_path_format_t *format) {
    if(path == 0) {
        return 0;
    }

    log_format_values_t values = {
        .timestamp = format->timestamp ? format->timestamp : "",
        .timestamp_iso = format->timestamp_iso ? format->timestamp_iso : "",
        .app_name = application_name ? application_name : "",
        .device_name = format->device_name ? format->device_name : "",
        .message = "",
    };

    return log_format_render(path, &values);
}
