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

#pragma once

#include "arguments.h"
#include "config.h"

#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

typedef struct supervisor_output_stream_s {
    int pipe_fd;
    int file_fd;
    const char *path;
    int rotate_size;
    int rotate_count;
    int passthrough_fd;
    long long bytes_written;
    const char *prefix_format;
    char *prefix_device_name;
    int prefix_at_line_start;
    const char *application_name;
    const char *stream_name;
} supervisor_output_stream_t;

typedef struct supervisor_control_block_s {
    const nanoinit_application_config_t *application; //application data from config

    pid_t pid;
    int running;
    int desired_running;
    time_t started_at;

    supervisor_output_stream_t stdout_stream;
    supervisor_output_stream_t stderr_stream;
} supervisor_control_block_t;

typedef enum supervisor_spawn_result_e {
    SUPERVISOR_SPAWN_ERROR = -1,
    SUPERVISOR_SPAWN_STARTED = 0,
} supervisor_spawn_result_t;

extern volatile sig_atomic_t supervisor_got_signal_stop;
extern volatile sig_atomic_t supervisor_got_signal_reload;
extern supervisor_control_block_t *scb;
extern int scb_count;
extern int supervisor_control_fd;
extern const char *supervisor_control_socket_path;

void supervisor_free_scb(void);

supervisor_spawn_result_t supervisor_spawn(supervisor_control_block_t *scb);
int supervisor_any_running(void);
void supervisor_reap_children(void);

int supervisor_start_control_socket(const char *path);
void supervisor_close_control_socket(void);
void supervisor_accept_control_connection(void);

void supervisor_init_output_stream(supervisor_output_stream_t *stream);
void supervisor_close_output_stream(supervisor_output_stream_t *stream);
void supervisor_close_all_output_streams(void);
int supervisor_any_stream_open(void);
int supervisor_file_output_configured(const char *path);
int supervisor_output_stream_configured(const char *path, int rotate_size, int passthrough, const char *prefix_logs);
int supervisor_start_output_stream(
    supervisor_output_stream_t *stream,
    int pipe_fd,
    const char *path,
    int rotate_size,
    int rotate_count,
    int passthrough_fd,
    const char *application_name,
    const char *stream_name,
    const char *prefix_logs
);
void supervisor_drain_output_stream(supervisor_output_stream_t *stream);
void supervisor_drain_output_streams(int timeout_ms);
void supervisor_drain_scb_output_streams(supervisor_control_block_t *scb);
