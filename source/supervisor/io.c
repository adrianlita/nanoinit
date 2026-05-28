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

#include <errno.h>
#include <sys/select.h>
#include <unistd.h>

void supervisor_drain_output_streams(int timeout_ms) {
    fd_set read_fds;
    FD_ZERO(&read_fds);

    int max_fd = -1;
    if(supervisor_control_fd >= 0) {
        FD_SET(supervisor_control_fd, &read_fds);
        max_fd = supervisor_control_fd;
    }

    for(int i = 0; i < scb_count; i++) {
        if(scb[i].stdout_stream.pipe_fd >= 0) {
            FD_SET(scb[i].stdout_stream.pipe_fd, &read_fds);
            if(scb[i].stdout_stream.pipe_fd > max_fd) {
                max_fd = scb[i].stdout_stream.pipe_fd;
            }
        }

        if(scb[i].stderr_stream.pipe_fd >= 0) {
            FD_SET(scb[i].stderr_stream.pipe_fd, &read_fds);
            if(scb[i].stderr_stream.pipe_fd > max_fd) {
                max_fd = scb[i].stderr_stream.pipe_fd;
            }
        }
    }

    if(max_fd < 0) {
        sleep((unsigned int)((timeout_ms + 999) / 1000));
        return;
    }

    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int select_result = select(max_fd + 1, &read_fds, 0, 0, &timeout);
    if(select_result < 0) {
        if(errno != EINTR) {
            log_ni_error("supervisor_drain_output_streams() select failed");
        }
        return;
    }

    if(select_result == 0) {
        return;
    }

    if((supervisor_control_fd >= 0) && FD_ISSET(supervisor_control_fd, &read_fds)) {
        supervisor_accept_control_connection();
    }

    for(int i = 0; i < scb_count; i++) {
        if((scb[i].stdout_stream.pipe_fd >= 0) && FD_ISSET(scb[i].stdout_stream.pipe_fd, &read_fds)) {
            supervisor_drain_output_stream(&scb[i].stdout_stream);
        }

        if((scb[i].stderr_stream.pipe_fd >= 0) && FD_ISSET(scb[i].stderr_stream.pipe_fd, &read_fds)) {
            supervisor_drain_output_stream(&scb[i].stderr_stream);
        }
    }
}
