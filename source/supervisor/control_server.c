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

#include "internal.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

int supervisor_control_fd = -1;
const char *supervisor_control_socket_path = 0;

static int supervisor_control_socket_is_active(const char *path);
static void supervisor_handle_control_request(int client_fd, char *request);
static void supervisor_handle_control_status(int client_fd, const char *name);
static void supervisor_handle_control_start(int client_fd, const char *name);
static void supervisor_handle_control_stop(int client_fd, const char *name);
static void supervisor_handle_control_list(int client_fd);
static supervisor_control_block_t *supervisor_find_scb(const char *name);
static const char *supervisor_scb_status(const supervisor_control_block_t *scb);
static long long supervisor_scb_uptime(const supervisor_control_block_t *scb);
static int supervisor_write_control_response(int fd, const char *format, ...);

int supervisor_start_control_socket(const char *path) {
    supervisor_close_control_socket();
    if(path == 0) {
        log_ni_error("supervisor_start_control_socket() control socket path is not configured");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t path_size = strlen(path);
    if(path_size >= sizeof(addr.sun_path)) {
        log_ni_error("supervisor_start_control_socket() control socket path is too long: %s", path);
        return -1;
    }
    memcpy(addr.sun_path, path, path_size + 1);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) {
        log_ni_error("supervisor_start_control_socket() could not create control socket");
        return -1;
    }

    if(fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
        log_ni_error("supervisor_start_control_socket() could not mark control socket close-on-exec");
        close(fd);
        return -1;
    }

    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if((errno == EADDRINUSE) && !supervisor_control_socket_is_active(path)) {
            unlink(path);
            if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                goto control_socket_bound;
            }
        }

        log_ni_error("supervisor_start_control_socket() could not bind control socket %s", path);
        close(fd);
        return -1;
    }

control_socket_bound:
    if(listen(fd, 16) != 0) {
        log_ni_error("supervisor_start_control_socket() could not listen on control socket %s", path);
        close(fd);
        unlink(path);
        return -1;
    }

    supervisor_control_fd = fd;
    supervisor_control_socket_path = path;
    return 0;
}

void supervisor_close_control_socket(void) {
    if(supervisor_control_fd >= 0) {
        close(supervisor_control_fd);
        supervisor_control_fd = -1;
    }

    if(supervisor_control_socket_path != 0) {
        unlink(supervisor_control_socket_path);
        supervisor_control_socket_path = 0;
    }
}

static int supervisor_control_socket_is_active(const char *path) {
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;

    size_t path_size = strlen(path);
    if(path_size >= sizeof(addr.sun_path)) {
        return 0;
    }
    memcpy(addr.sun_path, path, path_size + 1);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < 0) {
        return 0;
    }

    int is_active = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return is_active;
}

void supervisor_accept_control_connection(void) {
    int client_fd = accept(supervisor_control_fd, 0, 0);
    if(client_fd < 0) {
        if(errno != EINTR) {
            log_ni_error("supervisor_accept_control_connection() accept failed");
        }
        return;
    }

    if(fcntl(client_fd, F_SETFD, FD_CLOEXEC) != 0) {
        log_ni_error("supervisor_accept_control_connection() could not mark client socket close-on-exec");
        close(client_fd);
        return;
    }

    struct timeval socket_timeout;
    socket_timeout.tv_sec = 1;
    socket_timeout.tv_usec = 0;
    if(setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &socket_timeout, sizeof(socket_timeout)) != 0) {
        log_ni_error("supervisor_accept_control_connection() could not set client read timeout");
    }
    if(setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout, sizeof(socket_timeout)) != 0) {
        log_ni_error("supervisor_accept_control_connection() could not set client write timeout");
    }

    char request[1024];
    size_t request_size = 0;
    while(request_size < sizeof(request) - 1) {
        ssize_t read_result = read(client_fd, request + request_size, sizeof(request) - 1 - request_size);
        if(read_result > 0) {
            char *newline = memchr(request + request_size, '\n', (size_t)read_result);
            request_size += (size_t)read_result;
            if(newline != 0) {
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
            supervisor_write_control_response(client_fd, "ERROR\ncould not read control request\n");
            close(client_fd);
            return;
        }
    }

    request[request_size] = 0;
    supervisor_handle_control_request(client_fd, request);
    close(client_fd);
}

static void supervisor_handle_control_request(int client_fd, char *request) {
    char *newline = strchr(request, '\n');
    if(newline != 0) {
        *newline = 0;
    }

    char *command = request;
    while((*command == ' ') || (*command == '\t')) {
        command++;
    }

    char *name = command;
    while((*name != 0) && (*name != ' ') && (*name != '\t')) {
        name++;
    }
    if(*name != 0) {
        *name = 0;
        name++;
    }
    while((*name == ' ') || (*name == '\t')) {
        name++;
    }

    char *end = name + strlen(name);
    while((end > name) && ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r'))) {
        end--;
    }
    *end = 0;

    if(command[0] == 0) {
        supervisor_write_control_response(client_fd, "ERROR\nmissing control command\n");
    }
    else if(strcasecmp(command, "LIST") == 0) {
        if(name[0] != 0) {
            supervisor_write_control_response(client_fd, "ERROR\nlist does not take an app name\n");
            return;
        }
        supervisor_handle_control_list(client_fd);
    }
    else if(strcasecmp(command, "STATUS") == 0) {
        supervisor_handle_control_status(client_fd, name);
    }
    else if(strcasecmp(command, "START") == 0) {
        supervisor_handle_control_start(client_fd, name);
    }
    else if(strcasecmp(command, "STOP") == 0) {
        supervisor_handle_control_stop(client_fd, name);
    }
    else {
        supervisor_write_control_response(client_fd, "ERROR\nunknown control command: %s\n", command);
    }
}

static void supervisor_handle_control_status(int client_fd, const char *name) {
    if((name == 0) || (name[0] == 0)) {
        supervisor_write_control_response(client_fd, "ERROR\nmissing app name\n");
        return;
    }

    supervisor_control_block_t *target = supervisor_find_scb(name);
    if(target == 0) {
        supervisor_write_control_response(client_fd, "ERROR\napp not found: %s\n", name);
        return;
    }

    long long uptime = supervisor_scb_uptime(target);
    supervisor_write_control_response(client_fd, "OK\n");
    supervisor_write_control_response(client_fd, "name: %s\n", target->application->name);
    supervisor_write_control_response(client_fd, "status: %s\n", supervisor_scb_status(target));
    if(target->running) {
        supervisor_write_control_response(client_fd, "pid: %d\n", (int)target->pid);
    }
    else {
        supervisor_write_control_response(client_fd, "pid: -\n");
    }
    supervisor_write_control_response(client_fd, "uptime: ");
    if(uptime >= 0) {
        supervisor_write_control_response(client_fd, "%llds\n", uptime);
    }
    else {
        supervisor_write_control_response(client_fd, "-\n");
    }
    supervisor_write_control_response(client_fd, "autostart: %s\n", target->application->autostart ? "true" : "false");
    supervisor_write_control_response(client_fd, "autorestart: %s\n", target->application->autorestart ? "true" : "false");
}

static void supervisor_handle_control_start(int client_fd, const char *name) {
    if((name == 0) || (name[0] == 0)) {
        supervisor_write_control_response(client_fd, "ERROR\nmissing app name\n");
        return;
    }

    supervisor_control_block_t *target = supervisor_find_scb(name);
    if(target == 0) {
        supervisor_write_control_response(client_fd, "ERROR\napp not found: %s\n", name);
        return;
    }

    target->desired_running = 1;
    if(target->running) {
        supervisor_write_control_response(client_fd, "OK\napp '%s' is already running\n", target->application->name);
        return;
    }

    supervisor_spawn_result_t spawn_result = supervisor_spawn(target);
    if(spawn_result == SUPERVISOR_SPAWN_STARTED) {
        supervisor_write_control_response(client_fd, "OK\nstarted app '%s' with pid %d\n", target->application->name, (int)target->pid);
    }
    else {
        target->desired_running = 0;
        supervisor_write_control_response(client_fd, "ERROR\nfailed to start app '%s'\n", target->application->name);
    }
}

static void supervisor_handle_control_stop(int client_fd, const char *name) {
    if((name == 0) || (name[0] == 0)) {
        supervisor_write_control_response(client_fd, "ERROR\nmissing app name\n");
        return;
    }

    supervisor_control_block_t *target = supervisor_find_scb(name);
    if(target == 0) {
        supervisor_write_control_response(client_fd, "ERROR\napp not found: %s\n", name);
        return;
    }

    target->desired_running = 0;
    if(!target->running) {
        supervisor_write_control_response(client_fd, "OK\napp '%s' is already stopped\n", target->application->name);
        return;
    }

    if(kill(target->pid, SIGTERM) != 0) {
        supervisor_write_control_response(client_fd, "ERROR\nfailed to stop app '%s'\n", target->application->name);
        return;
    }

    supervisor_write_control_response(client_fd, "OK\nstopping app '%s'\n", target->application->name);
}

static void supervisor_handle_control_list(int client_fd) {
    supervisor_write_control_response(client_fd, "OK\n");
    supervisor_write_control_response(client_fd, "name\tstatus\tpid\tuptime\tautostart\tautorestart\n");

    for(int i = 0; i < scb_count; i++) {
        long long uptime = supervisor_scb_uptime(&scb[i]);
        supervisor_write_control_response(
            client_fd,
            "%s\t%s\t",
            scb[i].application->name,
            supervisor_scb_status(&scb[i])
        );

        if(scb[i].running) {
            supervisor_write_control_response(client_fd, "%d\t", (int)scb[i].pid);
        }
        else {
            supervisor_write_control_response(client_fd, "-\t");
        }

        if(uptime >= 0) {
            supervisor_write_control_response(client_fd, "%llds", uptime);
        }
        else {
            supervisor_write_control_response(client_fd, "-");
        }

        supervisor_write_control_response(
            client_fd,
            "\t%s\t%s\n",
            scb[i].application->autostart ? "true" : "false",
            scb[i].application->autorestart ? "true" : "false"
        );
    }
}

static supervisor_control_block_t *supervisor_find_scb(const char *name) {
    for(int i = 0; i < scb_count; i++) {
        if(strcmp(scb[i].application->name, name) == 0) {
            return &scb[i];
        }
    }

    return 0;
}

static const char *supervisor_scb_status(const supervisor_control_block_t *scb) {
    if(scb->running && scb->desired_running) {
        return "running";
    }

    if(scb->running) {
        return "stopping";
    }

    return "stopped";
}

static long long supervisor_scb_uptime(const supervisor_control_block_t *scb) {
    if(!scb->running || (scb->started_at == 0)) {
        return -1;
    }

    return (long long)difftime(time(0), scb->started_at);
}

static int supervisor_write_control_response(int fd, const char *format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    int size = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if(size < 0) {
        return -1;
    }

    size_t write_size = (size_t)size;
    if(write_size >= sizeof(buffer)) {
        write_size = sizeof(buffer) - 1;
    }

    size_t written = 0;
    while(written < write_size) {
        ssize_t write_result = write(fd, buffer + written, write_size - written);
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
