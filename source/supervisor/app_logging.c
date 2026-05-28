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

#define _GNU_SOURCE

#include "internal.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int supervisor_open_output_stream_file(supervisor_output_stream_t *stream);
static int supervisor_rotate_output_stream(supervisor_output_stream_t *stream);
static char *supervisor_rotated_path(const char *path, int index);
static int supervisor_write_fd(
    supervisor_output_stream_t *stream,
    int fd,
    const char *buffer,
    size_t size,
    long long *bytes_written,
    const char *destination
);
static int supervisor_write_output_stream_chunk(supervisor_output_stream_t *stream, const char *buffer, size_t size);
static int supervisor_write_output_stream(supervisor_output_stream_t *stream, const char *buffer, size_t size);

void supervisor_init_output_stream(supervisor_output_stream_t *stream) {
    stream->pipe_fd = -1;
    stream->file_fd = -1;
    stream->path = 0;
    stream->rotate_size = 0;
    stream->rotate_count = 0;
    stream->passthrough_fd = -1;
    stream->bytes_written = 0;
    stream->application_name = 0;
    stream->stream_name = 0;
}

void supervisor_close_output_stream(supervisor_output_stream_t *stream) {
    if(stream->pipe_fd >= 0) {
        close(stream->pipe_fd);
    }
    if(stream->file_fd >= 0) {
        close(stream->file_fd);
    }

    supervisor_init_output_stream(stream);
}

void supervisor_close_all_output_streams(void) {
    for(int i = 0; i < scb_count; i++) {
        supervisor_close_output_stream(&scb[i].stdout_stream);
        supervisor_close_output_stream(&scb[i].stderr_stream);
    }
}

int supervisor_any_stream_open(void) {
    for(int i = 0; i < scb_count; i++) {
        if((scb[i].stdout_stream.pipe_fd >= 0) || (scb[i].stderr_stream.pipe_fd >= 0)) {
            return 1;
        }
    }

    return 0;
}

int supervisor_file_output_configured(const char *path) {
    return (path != 0) && (path[0] != 0);
}

int supervisor_output_stream_configured(const char *path, int rotate_size, int passthrough) {
    return supervisor_file_output_configured(path) && ((rotate_size > 0) || passthrough);
}

int supervisor_start_output_stream(
    supervisor_output_stream_t *stream,
    int pipe_fd,
    const char *path,
    int rotate_size,
    int rotate_count,
    int passthrough_fd,
    const char *application_name,
    const char *stream_name
) {
    int flags = fcntl(pipe_fd, F_GETFL, 0);
    if(flags < 0) {
        log_ni_error("supervisor_start_output_stream() could not read pipe flags for %s %s", application_name, stream_name);
        return -1;
    }

    if(fcntl(pipe_fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        log_ni_error("supervisor_start_output_stream() could not set pipe non-blocking for %s %s", application_name, stream_name);
        return -1;
    }

    stream->pipe_fd = pipe_fd;
    stream->file_fd = -1;
    stream->path = path;
    stream->rotate_size = rotate_size;
    stream->rotate_count = rotate_count;
    stream->passthrough_fd = passthrough_fd;
    stream->bytes_written = 0;
    stream->application_name = application_name;
    stream->stream_name = stream_name;

    if(supervisor_open_output_stream_file(stream) != 0) {
        stream->file_fd = open("/dev/null", O_WRONLY);
        if(stream->file_fd < 0) {
            log_ni_error("supervisor_start_output_stream() could not open /dev/null fallback for %s %s", application_name, stream_name);
            supervisor_close_output_stream(stream);
            return -1;
        }
    }

    return 0;
}

static int supervisor_open_output_stream_file(supervisor_output_stream_t *stream) {
    stream->file_fd = open(stream->path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if(stream->file_fd < 0) {
        log_ni_error("supervisor_open_output_stream_file() could not open %s for app %s %s output", stream->path, stream->application_name, stream->stream_name);
        return -1;
    }

    stream->bytes_written = lseek(stream->file_fd, 0, SEEK_END);
    if(stream->bytes_written < 0) {
        stream->bytes_written = 0;
    }

    return 0;
}

static char *supervisor_rotated_path(const char *path, int index) {
    char *result = 0;
    if(asprintf(&result, "%s.%d", path, index) < 0) {
        return 0;
    }

    return result;
}

static int supervisor_rotate_output_stream(supervisor_output_stream_t *stream) {
    if(stream->file_fd >= 0) {
        close(stream->file_fd);
        stream->file_fd = -1;
    }

    if(stream->rotate_count <= 0) {
        unlink(stream->path);
        return supervisor_open_output_stream_file(stream);
    }

    char *oldest_path = supervisor_rotated_path(stream->path, stream->rotate_count);
    if(oldest_path == 0) {
        log_ni_error("supervisor_rotate_output_stream() could not allocate rotated path for app %s %s", stream->application_name, stream->stream_name);
        return -1;
    }
    unlink(oldest_path);
    free(oldest_path);

    for(int i = stream->rotate_count - 1; i >= 1; i--) {
        char *from_path = supervisor_rotated_path(stream->path, i);
        char *to_path = supervisor_rotated_path(stream->path, i + 1);
        if((from_path == 0) || (to_path == 0)) {
            free(from_path);
            free(to_path);
            log_ni_error("supervisor_rotate_output_stream() could not allocate rotated path for app %s %s", stream->application_name, stream->stream_name);
            return -1;
        }
        rename(from_path, to_path);
        free(from_path);
        free(to_path);
    }

    char *first_path = supervisor_rotated_path(stream->path, 1);
    if(first_path == 0) {
        log_ni_error("supervisor_rotate_output_stream() could not allocate rotated path for app %s %s", stream->application_name, stream->stream_name);
        return -1;
    }
    rename(stream->path, first_path);
    free(first_path);

    return supervisor_open_output_stream_file(stream);
}

static int supervisor_write_output_stream(supervisor_output_stream_t *stream, const char *buffer, size_t size) {
    size_t written_from_buffer = 0;
    while(written_from_buffer < size) {
        size_t chunk_size = size - written_from_buffer;
        if((stream->rotate_size > 0) && (stream->bytes_written + (long long)chunk_size > stream->rotate_size)) {
            size_t available = 0;
            if(stream->bytes_written < stream->rotate_size) {
                available = (size_t)(stream->rotate_size - stream->bytes_written);
            }

            size_t newline_chunk = 0;
            for(size_t i = 0; (i < chunk_size) && (i <= available); i++) {
                if(buffer[written_from_buffer + i] == '\n') {
                    newline_chunk = i + 1;
                }
            }

            if(newline_chunk > 0) {
                chunk_size = newline_chunk;
            }
        }

        if(supervisor_write_output_stream_chunk(stream, buffer + written_from_buffer, chunk_size) != 0) {
            return -1;
        }
        written_from_buffer += chunk_size;

        if((stream->rotate_size > 0) &&
           (stream->bytes_written >= stream->rotate_size) &&
           (chunk_size > 0) &&
           (buffer[written_from_buffer - 1] == '\n')) {
            if(supervisor_rotate_output_stream(stream) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int supervisor_write_output_stream_chunk(supervisor_output_stream_t *stream, const char *buffer, size_t size) {
    if(supervisor_write_fd(stream, stream->file_fd, buffer, size, &stream->bytes_written, "file") != 0) {
        return -1;
    }

    if(stream->passthrough_fd >= 0) {
        if(supervisor_write_fd(stream, stream->passthrough_fd, buffer, size, 0, "passthrough") != 0) {
            return -1;
        }
    }

    return 0;
}

static int supervisor_write_fd(
    supervisor_output_stream_t *stream,
    int fd,
    const char *buffer,
    size_t size,
    long long *bytes_written,
    const char *destination
) {
    size_t written_from_chunk = 0;

    while(written_from_chunk < size) {
        ssize_t write_result = write(fd, buffer + written_from_chunk, size - written_from_chunk);
        if(write_result < 0) {
            if(errno == EINTR) {
                continue;
            }

            log_ni_error("supervisor_write_output_stream() could not write %s output for app %s to %s", stream->stream_name, stream->application_name, destination);
            return -1;
        }

        if(write_result == 0) {
            return -1;
        }

        written_from_chunk += (size_t)write_result;
        if(bytes_written != 0) {
            *bytes_written += write_result;
        }
    }

    return 0;
}

void supervisor_drain_output_stream(supervisor_output_stream_t *stream) {
    char buffer[4096];

    while(stream->pipe_fd >= 0) {
        ssize_t read_result = read(stream->pipe_fd, buffer, sizeof(buffer));
        if(read_result > 0) {
            if(stream->file_fd >= 0) {
                supervisor_write_output_stream(stream, buffer, (size_t)read_result);
            }
        }
        else if(read_result == 0) {
            supervisor_close_output_stream(stream);
            break;
        }
        else {
            if(errno == EINTR) {
                continue;
            }

            if((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                break;
            }

            log_ni_error("supervisor_drain_output_stream() could not read %s output for app %s", stream->stream_name, stream->application_name);
            supervisor_close_output_stream(stream);
            break;
        }
    }
}

void supervisor_drain_scb_output_streams(supervisor_control_block_t *scb) {
    supervisor_drain_output_stream(&scb->stdout_stream);
    supervisor_drain_output_stream(&scb->stderr_stream);
}
