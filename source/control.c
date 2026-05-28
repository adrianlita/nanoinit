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

#include "control.h"

#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/un.h>

static const char *control_command_name(nanoinit_special_mode_t special_mode);
static int control_write_all(int fd, const char *buffer, size_t size);

int control_send_command(const nanoinit_arguments_t *arguments) {
    signal(SIGPIPE, SIG_IGN);

    if((arguments == 0) || (arguments->control_socket_path == 0)) {
        fprintf(stderr, "control socket path is not configured\n");
        return 1;
    }

    const char *command = control_command_name(arguments->special_mode);
    if(command == 0) {
        fprintf(stderr, "invalid control command\n");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t socket_path_size = strlen(arguments->control_socket_path);
    if(socket_path_size >= sizeof(addr.sun_path)) {
        fprintf(stderr, "control socket path is too long: %s\n", arguments->control_socket_path);
        return 1;
    }
    memcpy(addr.sun_path, arguments->control_socket_path, socket_path_size + 1);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) {
        perror("could not create control socket");
        return 1;
    }

    if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "could not connect to nanoinit control socket %s: %s\n", arguments->control_socket_path, strerror(errno));
        close(fd);
        return 1;
    }

    char request[1024];
    int request_size;
    if(arguments->control_app_name != 0) {
        request_size = snprintf(request, sizeof(request), "%s %s\n", command, arguments->control_app_name);
    }
    else {
        request_size = snprintf(request, sizeof(request), "%s\n", command);
    }

    if((request_size < 0) || ((size_t)request_size >= sizeof(request))) {
        fprintf(stderr, "control request is too long\n");
        close(fd);
        return 1;
    }

    if(control_write_all(fd, request, (size_t)request_size) != 0) {
        fprintf(stderr, "could not write control request: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    shutdown(fd, SHUT_WR);

    char response[4096];
    size_t response_size = 0;
    int response_truncated = 0;
    while(1) {
        ssize_t read_result = read(fd, response + response_size, sizeof(response) - 1 - response_size);
        if(read_result > 0) {
            response_size += (size_t)read_result;
            if(response_size >= sizeof(response) - 1) {
                response_truncated = 1;
                break;
            }
        }
        else if(read_result == 0) {
            break;
        }
        else {
            if(errno == EINTR) {
                continue;
            }
            fprintf(stderr, "could not read control response: %s\n", strerror(errno));
            close(fd);
            return 1;
        }
    }

    close(fd);
    response[response_size] = 0;

    int is_error = 0;
    char *body = response;
    if(strncmp(response, "OK\n", 3) == 0) {
        body = response + 3;
    }
    else if(strncmp(response, "ERROR\n", 6) == 0) {
        body = response + 6;
        is_error = 1;
    }
    else {
        is_error = 1;
    }

    FILE *output = is_error ? stderr : stdout;
    fputs(body, output);
    if(response_truncated) {
        fprintf(output, "\nresponse truncated\n");
    }

    return is_error || response_truncated;
}

static const char *control_command_name(nanoinit_special_mode_t special_mode) {
    switch(special_mode) {
        case NI_COMMAND_CONTROL_STATUS:
            return "STATUS";

        case NI_COMMAND_CONTROL_START:
            return "START";

        case NI_COMMAND_CONTROL_STOP:
            return "STOP";

        case NI_COMMAND_CONTROL_LIST:
            return "LIST";

        default:
            return 0;
    }
}

static int control_write_all(int fd, const char *buffer, size_t size) {
    size_t written = 0;
    while(written < size) {
        ssize_t write_result = write(fd, buffer + written, size - written);
        if(write_result < 0) {
            if(errno == EINTR) {
                continue;
            }
            return -1;
        }
        if(write_result == 0) {
            return -1;
        }
        written += (size_t)write_result;
    }

    return 0;
}
