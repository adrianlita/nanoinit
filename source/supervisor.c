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

#include "supervisor.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>

typedef struct supervisor_output_stream_s {
    int pipe_fd;
    int file_fd;
    const char *path;
    int rotate_size;
    int rotate_count;
    long long bytes_written;
    const char *application_name;
    const char *stream_name;
} supervisor_output_stream_t;

typedef struct supervisor_control_block_s {
    const nanoinit_application_config_t *application; //application data from config

    pid_t pid;
    int running;

    supervisor_output_stream_t stdout_stream;
    supervisor_output_stream_t stderr_stream;
} supervisor_control_block_t;

typedef enum supervisor_spawn_result_e {
    SUPERVISOR_SPAWN_ERROR = -1,
    SUPERVISOR_SPAWN_STARTED = 0,
    SUPERVISOR_SPAWN_SKIPPED = 1,
} supervisor_spawn_result_t;

static supervisor_spawn_result_t supervisor_spawn(supervisor_control_block_t *scb);
static void supervisor_free_scb();
static int supervisor_any_running(void);
static int supervisor_any_stream_open(void);
static void supervisor_init_output_stream(supervisor_output_stream_t *stream);
static void supervisor_close_output_stream(supervisor_output_stream_t *stream);
static void supervisor_close_all_output_streams(void);
static void supervisor_drain_output_streams(int timeout_ms);
static void supervisor_drain_scb_output_streams(supervisor_control_block_t *scb);
static int supervisor_output_stream_configured(const char *path, int rotate_size);
static int supervisor_start_output_stream(supervisor_output_stream_t *stream, int pipe_fd, const char *path, int rotate_size, int rotate_count, const char *application_name, const char *stream_name);
static int supervisor_open_output_stream_file(supervisor_output_stream_t *stream);
static int supervisor_rotate_output_stream(supervisor_output_stream_t *stream);
static int supervisor_write_output_stream(supervisor_output_stream_t *stream, const char *buffer, size_t size);
static void supervisor_reap_children(void);
static void supervisor_log_process_status(const char *name, pid_t pid, int status, int app_error);
static void supervisor_sigterm_cb(int signo);
static void supervisor_sigusr1_cb(int signo);

static volatile sig_atomic_t supervisor_got_signal_stop = 0;
static volatile sig_atomic_t supervisor_got_signal_reload = 0;
static bool manual_mode = false;
static supervisor_control_block_t *scb = 0;
static int scb_count = 0;


int supervisor_start(const nanoinit_arguments_t *arguments, const nanoinit_config_t *config) {
supervisor_start_begin:
    //initialize everything
    supervisor_got_signal_stop = 0;
    supervisor_got_signal_reload = 0;

    manual_mode = arguments->manual_mode;

    scb_count = config ? config->application_count : 0;
    if(scb_count > 0) {
        scb = (supervisor_control_block_t *)malloc(sizeof(supervisor_control_block_t) * scb_count);
        if(scb == 0) {
            log_ni_error("supervisor_start() could not allocate memory for scb");
            return -1;
        }
    }
    else {
        scb = 0;
    }

    //initialize scb
    for(int i = 0; i < scb_count; i++) {
        scb[i].application = &config->applications[i];
        scb[i].pid = 0;
        scb[i].running = 0;
        supervisor_init_output_stream(&scb[i].stdout_stream);
        supervisor_init_output_stream(&scb[i].stderr_stream);
    }

    //register signals to nanoinit
    signal(SIGTERM, supervisor_sigterm_cb);
    signal(SIGINT, supervisor_sigterm_cb);
    signal(SIGQUIT, supervisor_sigterm_cb);
    signal(SIGUSR1, supervisor_sigusr1_cb);

    //spawn processes
    for(int i = 0; i < scb_count; i++) {
        supervisor_spawn_result_t spawn_result = supervisor_spawn(&scb[i]);
        if(spawn_result == SUPERVISOR_SPAWN_STARTED) {
            log("supervisor_start() successfully spawned '%s' with pid %d", scb[i].application->name, (int)scb[i].pid);
        }
        else if(spawn_result == SUPERVISOR_SPAWN_ERROR) {
            log_app_error("supervisor_start() failed to spawn '%s'", scb[i].application->name);
        }
    }

    //supervise processes and received signals; this loop is finished when supervisor_got_signal_stop will get set by signal
    int running = 1;
    while(running) {
        supervisor_reap_children();

        if(supervisor_got_signal_stop) {    //if got the terminate
            int signal_to_forward = supervisor_got_signal_stop;
            supervisor_got_signal_stop = 0;

            //forward the signal to all processes
            for(int i = 0; i < scb_count; i++) {
                if(scb[i].running) {
                    log("supervisor_start() sending %d to %s (pid=%d)...", signal_to_forward, scb[i].application->name, (int)scb[i].pid);
                    kill(scb[i].pid, signal_to_forward);
                }
            }
            
            running = 0;
        }

        supervisor_drain_output_streams(1000);
    }

    //wait for processes to terminate after SIGTERM was forwarded
    running = supervisor_any_running() || supervisor_any_stream_open();
    while(running) {
        supervisor_reap_children();
        supervisor_drain_output_streams(1000);
        running = supervisor_any_running() || supervisor_any_stream_open();
    }

    //cleanup
    supervisor_free_scb();
    
    //check whether a nanoinit-reload (SIGUSR1) was received and restart everytthing
    if(supervisor_got_signal_reload) {
        supervisor_got_signal_reload = 0;

        config_free();
        const nanoinit_config_t *result = config_init(arguments->config_file, arguments->config_json_object);
        if(result == 0) {
            log_ni_error("supervisor_start() could not read new config; using zero-config");
        }

        log("supervisor_start() SIGUSR1 received, reloading and restarting everything according to new configuration");
        goto supervisor_start_begin;
    }


    return 0;
}

static void supervisor_sigterm_cb(int signo) {
    supervisor_got_signal_stop = signo;
}

static void supervisor_sigusr1_cb(int signo) {
    (void)signo;    //always SIGUSR1

    supervisor_got_signal_stop = SIGTERM;
    supervisor_got_signal_reload = 1;
}


static supervisor_spawn_result_t supervisor_spawn(supervisor_control_block_t *scb) {
    if(manual_mode && scb->application->manual) {
        scb->pid = 0;
        scb->running = 0;
        log("supervisor_spawn() process %s not spawned because is marked as manual", scb->application->name);
        return SUPERVISOR_SPAWN_SKIPPED;
    }

    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };
    int rotate_stdout = supervisor_output_stream_configured(scb->application->stdout_path, scb->application->stdout_rotate_size);
    int rotate_stderr = supervisor_output_stream_configured(scb->application->stderr_path, scb->application->stderr_rotate_size);

    supervisor_close_output_stream(&scb->stdout_stream);
    supervisor_close_output_stream(&scb->stderr_stream);

    if((scb->application->stdout_rotate_size > 0) && !rotate_stdout) {
        log_ni_error("supervisor_spawn() stdout rotation ignored for app %s because stdout is not a file path", scb->application->name);
    }

    if((scb->application->stderr_rotate_size > 0) && !rotate_stderr) {
        log_ni_error("supervisor_spawn() stderr rotation ignored for app %s because stderr is not a file path", scb->application->name);
    }

    if(rotate_stdout && (pipe(stdout_pipe) != 0)) {
        log_ni_error("supervisor_spawn() could not create stdout pipe for app %s", scb->application->name);
        return SUPERVISOR_SPAWN_ERROR;
    }

    if(rotate_stderr && (pipe(stderr_pipe) != 0)) {
        log_ni_error("supervisor_spawn() could not create stderr pipe for app %s", scb->application->name);
        if(stdout_pipe[0] >= 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        return SUPERVISOR_SPAWN_ERROR;
    }

    scb->running = 1;
    scb->pid = fork();
    if(scb->pid == -1) {
        log_ni_error("supervisor_spawn() fork failed");
        scb->running = 0;
        if(stdout_pipe[0] >= 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        if(stderr_pipe[0] >= 0) {
            close(stderr_pipe[0]);
            close(stderr_pipe[1]);
        }
        return SUPERVISOR_SPAWN_ERROR;
    }

    if(scb->pid == 0) {
        //child process
        
        //unregister signals from parent
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGUSR1, SIG_DFL);

        //redirect stdout ?
        if(rotate_stdout) {
            close(stdout_pipe[0]);
            if(dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
                log_ni_error("supervisor_spawn() could not redirect stdout pipe for app %s", scb->application->name);
            }
            close(stdout_pipe[1]);
        }
        else {
            char *stdout_path = scb->application->stdout_path ? strdup(scb->application->stdout_path) : 0;
            if(stdout_path && stdout_path[0] == 0) {
                free(stdout_path);
                stdout_path = strdup("/dev/null");
            }

            if(stdout_path) {
                int stdout_fd = open(stdout_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if(stdout_fd < 0) {
                    log_ni_error("supervisor_spawn() could not open %s for redirecting stdout for app %s", stdout_path, scb->application->name);
                }
                else {
                    dup2(stdout_fd, STDOUT_FILENO);
                    close(stdout_fd);
                }
                free(stdout_path);
            }
        }

        //redirect stderr ?
        if(rotate_stderr) {
            close(stderr_pipe[0]);
            if(dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
                log_ni_error("supervisor_spawn() could not redirect stderr pipe for app %s", scb->application->name);
            }
            close(stderr_pipe[1]);
        }
        else {
            char *stderr_path = scb->application->stderr_path ? strdup(scb->application->stderr_path) : 0;
            if(stderr_path && stderr_path[0] == 0) {
                free(stderr_path);
                stderr_path = strdup("/dev/null");
            }

            if(stderr_path) {
                int stderr_fd = open(stderr_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if(stderr_fd < 0) {
                    log_ni_error("supervisor_spawn() could not open %s for redirecting stderr for app %s", stderr_path, scb->application->name);
                }
                else {
                    dup2(stderr_fd, STDERR_FILENO);
                    close(stderr_fd);
                }
                free(stderr_path);
            }
        }

        //copy path and arguments as they will be freed
        char *app_path = strdup(scb->application->path);
        if(app_path == 0) {
            log_ni_error("supervisor_spawn() could not allocate memory for application path");
            exit(1);
        }

        int arg_count = scb->application->arg_count + 2;
        char **app_args = (char **)malloc(sizeof(char*) * arg_count);
        if(app_args == 0) {
            log_ni_error("supervisor_spawn() could not allocate memory for application arguments vector");
            exit(1);
        }
        app_args[0] = strdup(app_path);
        if(app_args[0] == 0) {
            log_ni_error("supervisor_spawn() could not allocate memory for application first argument (path)");
            exit(1);
        }
        app_args[arg_count - 1] = 0;

        for(int i = 0; i < scb->application->arg_count; i++) {
            app_args[i + 1] = strdup(scb->application->args[i]);
            if(app_args[i + 1] == 0) {
                log_ni_error("supervisor_spawn() could not allocate memory for application argument %d", i);
                exit(1);
            }
        }

        //free parent inherited memory
        supervisor_close_all_output_streams();
        supervisor_free_scb();
        config_free();
        arguments_free();
        log_free();

        //create new session
        setsid();

        //execute
        int result = execv(app_path, app_args);
        if(result != 0) {
            log_ni_error("supervisor_spawn() failed to spawn process %s", app_path);
        }

        //free all used memory
        free(app_path);
        for(int i = 0; i < arg_count; i++) {
            free(app_args[i]);
        }
        free(app_args);

        //gracefully stop fork
        _exit(result);
    }

    if(rotate_stdout) {
        close(stdout_pipe[1]);
        if(supervisor_start_output_stream(&scb->stdout_stream, stdout_pipe[0], scb->application->stdout_path, scb->application->stdout_rotate_size, scb->application->stdout_rotate_count, scb->application->name, "stdout") != 0) {
            close(stdout_pipe[0]);
        }
    }

    if(rotate_stderr) {
        close(stderr_pipe[1]);
        if(supervisor_start_output_stream(&scb->stderr_stream, stderr_pipe[0], scb->application->stderr_path, scb->application->stderr_rotate_size, scb->application->stderr_rotate_count, scb->application->name, "stderr") != 0) {
            close(stderr_pipe[0]);
        }
    }

    return SUPERVISOR_SPAWN_STARTED;
}

static void supervisor_free_scb(void) {
    supervisor_close_all_output_streams();
    free(scb);
    scb = 0;
    scb_count = 0;
}

static int supervisor_any_running(void) {
    for(int i = 0; i < scb_count; i++) {
        if(scb[i].running == 1) {
            return 1;
        }
    }

    return 0;
}

static int supervisor_any_stream_open(void) {
    for(int i = 0; i < scb_count; i++) {
        if((scb[i].stdout_stream.pipe_fd >= 0) || (scb[i].stderr_stream.pipe_fd >= 0)) {
            return 1;
        }
    }

    return 0;
}

static void supervisor_init_output_stream(supervisor_output_stream_t *stream) {
    stream->pipe_fd = -1;
    stream->file_fd = -1;
    stream->path = 0;
    stream->rotate_size = 0;
    stream->rotate_count = 0;
    stream->bytes_written = 0;
    stream->application_name = 0;
    stream->stream_name = 0;
}

static void supervisor_close_output_stream(supervisor_output_stream_t *stream) {
    if(stream->pipe_fd >= 0) {
        close(stream->pipe_fd);
    }

    if(stream->file_fd >= 0) {
        close(stream->file_fd);
    }

    supervisor_init_output_stream(stream);
}

static void supervisor_close_all_output_streams(void) {
    for(int i = 0; i < scb_count; i++) {
        supervisor_close_output_stream(&scb[i].stdout_stream);
        supervisor_close_output_stream(&scb[i].stderr_stream);
    }
}

static int supervisor_output_stream_configured(const char *path, int rotate_size) {
    return (path != 0) && (path[0] != 0) && (rotate_size > 0);
}

static int supervisor_start_output_stream(supervisor_output_stream_t *stream, int pipe_fd, const char *path, int rotate_size, int rotate_count, const char *application_name, const char *stream_name) {
    int flags = fcntl(pipe_fd, F_GETFL, 0);
    if(flags < 0) {
        log_ni_error("supervisor_start_output_stream() could not read pipe flags for %s %s", application_name, stream_name);
        return -1;
    }

    if(fcntl(pipe_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        log_ni_error("supervisor_start_output_stream() could not set pipe non-blocking for %s %s", application_name, stream_name);
        return -1;
    }

    stream->pipe_fd = pipe_fd;
    stream->path = path;
    stream->rotate_size = rotate_size;
    stream->rotate_count = rotate_count;
    stream->bytes_written = 0;
    stream->application_name = application_name;
    stream->stream_name = stream_name;

    if(supervisor_open_output_stream_file(stream) != 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if(null_fd < 0) {
            log_ni_error("supervisor_start_output_stream() could not open /dev/null fallback for %s %s", application_name, stream_name);
            return 0;
        }
        stream->file_fd = null_fd;
    }

    return 0;
}

static int supervisor_open_output_stream_file(supervisor_output_stream_t *stream) {
    stream->file_fd = open(stream->path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    stream->bytes_written = 0;
    if(stream->file_fd < 0) {
        log_ni_error("supervisor_open_output_stream_file() could not open %s for app %s %s rotation", stream->path, stream->application_name, stream->stream_name);
        return -1;
    }

    return 0;
}

static char *supervisor_rotated_path(const char *path, int index) {
    int path_size = snprintf(0, 0, "%s.%d", path, index);
    if(path_size < 0) {
        return 0;
    }

    char *rotated_path = (char *)malloc((size_t)path_size + 1);
    if(rotated_path == 0) {
        return 0;
    }

    snprintf(rotated_path, (size_t)path_size + 1, "%s.%d", path, index);
    return rotated_path;
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
        if((stream->rotate_size > 0) && (stream->bytes_written >= stream->rotate_size)) {
            if(supervisor_rotate_output_stream(stream) != 0) {
                return -1;
            }
        }

        size_t chunk_size = size - written_from_buffer;
        if(stream->rotate_size > 0) {
            long long remaining_size = (long long)stream->rotate_size - stream->bytes_written;
            if(remaining_size <= 0) {
                continue;
            }

            if((long long)chunk_size > remaining_size) {
                chunk_size = (size_t)remaining_size;
            }
        }

        size_t written_from_chunk = 0;
        while(written_from_chunk < chunk_size) {
            ssize_t write_result = write(stream->file_fd, buffer + written_from_buffer + written_from_chunk, chunk_size - written_from_chunk);
            if(write_result < 0) {
                if(errno == EINTR) {
                    continue;
                }

                log_ni_error("supervisor_write_output_stream() could not write %s output for app %s", stream->stream_name, stream->application_name);
                return -1;
            }

            if(write_result == 0) {
                return -1;
            }

            written_from_chunk += (size_t)write_result;
            stream->bytes_written += write_result;
        }

        written_from_buffer += chunk_size;
    }

    return 0;
}

static void supervisor_drain_output_stream(supervisor_output_stream_t *stream) {
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

static void supervisor_drain_scb_output_streams(supervisor_control_block_t *scb) {
    supervisor_drain_output_stream(&scb->stdout_stream);
    supervisor_drain_output_stream(&scb->stderr_stream);
}

static void supervisor_drain_output_streams(int timeout_ms) {
    fd_set read_fds;
    FD_ZERO(&read_fds);

    int max_fd = -1;
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

    for(int i = 0; i < scb_count; i++) {
        if((scb[i].stdout_stream.pipe_fd >= 0) && FD_ISSET(scb[i].stdout_stream.pipe_fd, &read_fds)) {
            supervisor_drain_output_stream(&scb[i].stdout_stream);
        }

        if((scb[i].stderr_stream.pipe_fd >= 0) && FD_ISSET(scb[i].stderr_stream.pipe_fd, &read_fds)) {
            supervisor_drain_output_stream(&scb[i].stderr_stream);
        }
    }
}

static void supervisor_reap_children(void) {
    while(1) {
        int defunct_status;
        pid_t defunct_pid = waitpid(-1, &defunct_status, WNOHANG);
        if(defunct_pid > 0) {
            for(int i = 0; i < scb_count; i++) {
                if(scb[i].pid == defunct_pid) {
                    supervisor_log_process_status(scb[i].application->name, defunct_pid, defunct_status, 1);
                    scb[i].running = 0;
                    supervisor_drain_scb_output_streams(&scb[i]);
                    supervisor_close_output_stream(&scb[i].stdout_stream);
                    supervisor_close_output_stream(&scb[i].stderr_stream);

                    if(scb[i].application->autorestart && !supervisor_got_signal_stop) {
                        supervisor_spawn_result_t spawn_result = supervisor_spawn(&scb[i]);
                        if(spawn_result == SUPERVISOR_SPAWN_STARTED) {
                            log("supervisor_start() respawned %s (pid=%d)", scb[i].application->name, (int)scb[i].pid);
                        }
                        else if(spawn_result == SUPERVISOR_SPAWN_ERROR) {
                            log_app_error("supervisor_start() failed to spawn '%s'", scb[i].application->name);
                        }
                    }

                    break;
                }
            }
        }
        else if(defunct_pid == 0) {
            break;
        }
        else {
            if(errno != ECHILD) {
                log_ni_error("supervisor_reap_children() waitpid() returned invalid value");
            }
            break;
        }
    }
}

static void supervisor_log_process_status(const char *name, pid_t pid, int status, int app_error) {
    if(name == 0) {
        name = "unknown";
    }

    if(WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        log("supervisor_start() process %s (pid=%d) finished with status %d", name, (int)pid, WEXITSTATUS(status));
    }
    else if(WIFEXITED(status)) {
        if(app_error) {
            log_app_error("supervisor_start() process %s (pid=%d) exited with status %d", name, (int)pid, WEXITSTATUS(status));
        }
        else {
            log("supervisor_start() process %s (pid=%d) exited with status %d", name, (int)pid, WEXITSTATUS(status));
        }
    }
    else if(WIFSIGNALED(status)) {
        if(app_error) {
            log_app_error("supervisor_start() process %s (pid=%d) terminated by signal %d", name, (int)pid, WTERMSIG(status));
        }
        else {
            log("supervisor_start() process %s (pid=%d) terminated by signal %d", name, (int)pid, WTERMSIG(status));
        }
    }
    else {
        if(app_error) {
            log_app_error("supervisor_start() process %s (pid=%d) changed state with status %d", name, (int)pid, status);
        }
        else {
            log("supervisor_start() process %s (pid=%d) changed state with status %d", name, (int)pid, status);
        }
    }
}
