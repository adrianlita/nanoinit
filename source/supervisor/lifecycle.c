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
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SUPERVISOR_RESTART_DELAY_SECONDS 1
#define SUPERVISOR_CHILD_ERROR_WAIT_MS 100

typedef enum supervisor_child_error_stage_e {
    SUPERVISOR_CHILD_ERROR_PREPARE_ARGS = 1,
    SUPERVISOR_CHILD_ERROR_REDIRECT_STDOUT,
    SUPERVISOR_CHILD_ERROR_REDIRECT_STDERR,
    SUPERVISOR_CHILD_ERROR_EXEC,
} supervisor_child_error_stage_t;

typedef struct supervisor_child_error_s {
    int stage;
    int error_code;
} supervisor_child_error_t;

static void supervisor_log_process_status(const char *name, pid_t pid, int status, int app_error);
static int supervisor_create_child_error_pipe(int error_pipe[2]);
static void supervisor_close_pipe(int pipe_fds[2]);
static void supervisor_child_redirect_pipe(int error_pipe_fd, supervisor_child_error_stage_t stage, int pipe_fds[2], int output_fd);
static void supervisor_child_redirect_path(int error_pipe_fd, supervisor_child_error_stage_t stage, const char *path, bool create_log_dirs, int output_fd);
static void supervisor_child_report_error(int error_pipe_fd, supervisor_child_error_stage_t stage, int error_code);
static int supervisor_read_child_error(int error_pipe_fd, supervisor_child_error_t *child_error);
static void supervisor_log_child_error(const supervisor_control_block_t *scb, const supervisor_child_error_t *child_error, const char *stdout_target, const char *stderr_target);

supervisor_spawn_result_t supervisor_spawn(supervisor_control_block_t *scb) {
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };
    int child_error_pipe[2] = { -1, -1 };
    supervisor_output_paths_t output_paths = { 0 };
    if(supervisor_output_paths_render(scb->application, &output_paths) != 0) {
        return SUPERVISOR_SPAWN_ERROR;
    }

    int stdout_is_file = supervisor_file_output_configured(output_paths.stdout_path);
    int stderr_is_file = supervisor_file_output_configured(output_paths.stderr_path);
    int pipe_stdout = supervisor_output_stream_configured(
        output_paths.stdout_path,
        scb->application->stdout_rotate_size,
        scb->application->stdout_passthrough,
        scb->application->prefix_logs
    );
    int pipe_stderr = supervisor_output_stream_configured(
        output_paths.stderr_path,
        scb->application->stderr_rotate_size,
        scb->application->stderr_passthrough,
        scb->application->prefix_logs
    );

    supervisor_close_output_stream(&scb->stdout_stream);
    supervisor_close_output_stream(&scb->stderr_stream);

    if((scb->application->stdout_rotate_size > 0) && !stdout_is_file) {
        log_ni_error("supervisor_spawn() stdout rotation ignored for app %s because stdout is not a file path", scb->application->name);
    }

    if((scb->application->stderr_rotate_size > 0) && !stderr_is_file) {
        log_ni_error("supervisor_spawn() stderr rotation ignored for app %s because stderr is not a file path", scb->application->name);
    }

    if(scb->application->stdout_passthrough && (output_paths.stdout_path != 0) && !stdout_is_file) {
        log_ni_error("supervisor_spawn() stdout passthrough ignored for app %s because stdout is not a file path", scb->application->name);
    }

    if(scb->application->stderr_passthrough && (output_paths.stderr_path != 0) && !stderr_is_file) {
        log_ni_error("supervisor_spawn() stderr passthrough ignored for app %s because stderr is not a file path", scb->application->name);
    }

    if(pipe_stdout && (pipe(stdout_pipe) != 0)) {
        log_ni_error("supervisor_spawn() could not create stdout pipe for app %s", scb->application->name);
        supervisor_output_paths_free(&output_paths);
        return SUPERVISOR_SPAWN_ERROR;
    }

    if(pipe_stderr && (pipe(stderr_pipe) != 0)) {
        log_ni_error("supervisor_spawn() could not create stderr pipe for app %s", scb->application->name);
        if(stdout_pipe[0] >= 0) {
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
        }
        supervisor_output_paths_free(&output_paths);
        return SUPERVISOR_SPAWN_ERROR;
    }

    if(supervisor_create_child_error_pipe(child_error_pipe) != 0) {
        log_ni_error("supervisor_spawn() could not create child error pipe for app %s", scb->application->name);
        supervisor_close_pipe(stdout_pipe);
        supervisor_close_pipe(stderr_pipe);
        supervisor_output_paths_free(&output_paths);
        return SUPERVISOR_SPAWN_ERROR;
    }

    scb->running = 1;
    scb->pid = fork();
    if(scb->pid == -1) {
        log_ni_error("supervisor_spawn() fork failed");
        scb->running = 0;
        scb->started_at = 0;
        supervisor_close_pipe(stdout_pipe);
        supervisor_close_pipe(stderr_pipe);
        supervisor_close_pipe(child_error_pipe);
        supervisor_output_paths_free(&output_paths);
        return SUPERVISOR_SPAWN_ERROR;
    }

    if(scb->pid == 0) {
        //child process
        close(child_error_pipe[0]);

        //unregister signals from parent
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGUSR1, SIG_DFL);
        signal(SIGPIPE, SIG_DFL);

        if(pipe_stdout) {
            supervisor_child_redirect_pipe(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_REDIRECT_STDOUT, stdout_pipe, STDOUT_FILENO);
        }
        else {
            supervisor_child_redirect_path(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_REDIRECT_STDOUT, output_paths.stdout_path, scb->create_log_dirs, STDOUT_FILENO);
        }

        if(pipe_stderr) {
            supervisor_child_redirect_pipe(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_REDIRECT_STDERR, stderr_pipe, STDERR_FILENO);
        }
        else {
            supervisor_child_redirect_path(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_REDIRECT_STDERR, output_paths.stderr_path, scb->create_log_dirs, STDERR_FILENO);
        }

        //copy path and arguments as they will be freed
        char *app_path = strdup(scb->application->path);
        if(app_path == 0) {
            supervisor_child_report_error(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_PREPARE_ARGS, ENOMEM);
            _exit(255);
        }

        int arg_count = scb->application->arg_count + 2;
        char **app_args = (char **)malloc(sizeof(char*) * arg_count);
        if(app_args == 0) {
            supervisor_child_report_error(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_PREPARE_ARGS, ENOMEM);
            _exit(255);
        }
        app_args[0] = strdup(app_path);
        if(app_args[0] == 0) {
            supervisor_child_report_error(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_PREPARE_ARGS, ENOMEM);
            _exit(255);
        }
        app_args[arg_count - 1] = 0;

        for(int i = 0; i < scb->application->arg_count; i++) {
            app_args[i + 1] = strdup(scb->application->args[i]);
            if(app_args[i + 1] == 0) {
                supervisor_child_report_error(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_PREPARE_ARGS, ENOMEM);
                _exit(255);
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
            supervisor_child_report_error(child_error_pipe[1], SUPERVISOR_CHILD_ERROR_EXEC, errno);
        }

        //free all used memory
        free(app_path);
        for(int i = 0; i < arg_count; i++) {
            free(app_args[i]);
        }
        free(app_args);

        //gracefully stop fork
        _exit(255);
    }

    close(child_error_pipe[1]);
    child_error_pipe[1] = -1;

    if(pipe_stdout) {
        int passthrough_fd = -1;
        if(stdout_is_file) {
            passthrough_fd = scb->application->stdout_passthrough ? STDOUT_FILENO : -1;
        }
        else if(output_paths.stdout_path == 0) {
            passthrough_fd = STDOUT_FILENO;
        }
        close(stdout_pipe[1]);
        if(supervisor_start_output_stream(
            &scb->stdout_stream,
            stdout_pipe[0],
            output_paths.stdout_path,
            scb->create_log_dirs,
            scb->application->stdout_rotate_size,
            scb->application->stdout_rotate_count,
            passthrough_fd,
            scb->application->name,
            "stdout",
            scb->application->prefix_logs
        ) != 0) {
            close(stdout_pipe[0]);
        }
    }

    if(pipe_stderr) {
        int passthrough_fd = -1;
        if(stderr_is_file) {
            passthrough_fd = scb->application->stderr_passthrough ? STDERR_FILENO : -1;
        }
        else if(output_paths.stderr_path == 0) {
            passthrough_fd = STDERR_FILENO;
        }
        close(stderr_pipe[1]);
        if(supervisor_start_output_stream(
            &scb->stderr_stream,
            stderr_pipe[0],
            output_paths.stderr_path,
            scb->create_log_dirs,
            scb->application->stderr_rotate_size,
            scb->application->stderr_rotate_count,
            passthrough_fd,
            scb->application->name,
            "stderr",
            scb->application->prefix_logs
        ) != 0) {
            close(stderr_pipe[0]);
        }
    }

    scb->started_at = time(0);

    supervisor_child_error_t child_error;
    if(supervisor_read_child_error(child_error_pipe[0], &child_error)) {
        const char *stdout_target = pipe_stdout ? "stdout pipe" : supervisor_output_path_redirect_target(output_paths.stdout_path, "stdout pipe");
        const char *stderr_target = pipe_stderr ? "stderr pipe" : supervisor_output_path_redirect_target(output_paths.stderr_path, "stderr pipe");
        supervisor_log_child_error(scb, &child_error, stdout_target, stderr_target);
        close(child_error_pipe[0]);
        child_error_pipe[0] = -1;
        supervisor_output_paths_free(&output_paths);
        return SUPERVISOR_SPAWN_CHILD_ERROR;
    }

    close(child_error_pipe[0]);
    child_error_pipe[0] = -1;
    supervisor_output_paths_free(&output_paths);
    return SUPERVISOR_SPAWN_STARTED;
}

int supervisor_any_running(void) {
    for(int i = 0; i < scb_count; i++) {
        if(scb[i].running == 1) {
            return 1;
        }
    }

    return 0;
}

void supervisor_reap_children(void) {
    while(1) {
        int defunct_status;
        pid_t defunct_pid = waitpid(-1, &defunct_status, WNOHANG);
        if(defunct_pid > 0) {
            for(int i = 0; i < scb_count; i++) {
                if(scb[i].pid == defunct_pid) {
                    supervisor_log_process_status(scb[i].application->name, defunct_pid, defunct_status, 1);
                    scb[i].running = 0;
                    scb[i].pid = 0;
                    scb[i].started_at = 0;
                    supervisor_drain_scb_output_streams(&scb[i]);
                    supervisor_close_output_stream(&scb[i].stdout_stream);
                    supervisor_close_output_stream(&scb[i].stderr_stream);

                    if(scb[i].application->autorestart && scb[i].desired_running && !supervisor_got_signal_stop) {
                        supervisor_schedule_restart(&scb[i]);
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

void supervisor_start_pending_restarts(void) {
    time_t now = time(0);

    for(int i = 0; i < scb_count; i++) {
        if(!scb[i].restart_pending || scb[i].running || !scb[i].desired_running || !scb[i].application->autorestart || supervisor_got_signal_stop) {
            continue;
        }

        if(now < scb[i].restart_at) {
            continue;
        }

        scb[i].restart_pending = 0;
        scb[i].restart_at = 0;

        supervisor_spawn_result_t spawn_result = supervisor_spawn(&scb[i]);
        if(spawn_result == SUPERVISOR_SPAWN_STARTED) {
            log("supervisor_start() respawned %s (pid=%d)", scb[i].application->name, (int)scb[i].pid);
        }
        else if(spawn_result == SUPERVISOR_SPAWN_ERROR) {
            log_app_error("supervisor_start() failed to spawn '%s'", scb[i].application->name);
            supervisor_schedule_restart(&scb[i]);
        }
    }
}

static void supervisor_log_process_status(const char *name, pid_t pid, int status, int app_error) {
    if(WIFEXITED(status)) {
        if(WEXITSTATUS(status) == 0) {
            log("supervisor_start() process %s (pid=%d) finished with status %d", name, (int)pid, WEXITSTATUS(status));
        }
        else {
            if(app_error) {
                log_app_error("supervisor_start() process %s (pid=%d) exited with status %d", name, (int)pid, WEXITSTATUS(status));
            }
            else {
                log("supervisor_start() process %s (pid=%d) exited with status %d", name, (int)pid, WEXITSTATUS(status));
            }
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

void supervisor_schedule_restart(supervisor_control_block_t *scb) {
    scb->restart_pending = 1;
    scb->restart_at = time(0) + SUPERVISOR_RESTART_DELAY_SECONDS;
    log("supervisor_start() scheduled restart for %s in %ds", scb->application->name, SUPERVISOR_RESTART_DELAY_SECONDS);
}

static int supervisor_create_child_error_pipe(int error_pipe[2]) {
    if(pipe(error_pipe) != 0) {
        return -1;
    }

    if(fcntl(error_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        supervisor_close_pipe(error_pipe);
        return -1;
    }

    return 0;
}

static void supervisor_close_pipe(int pipe_fds[2]) {
    if(pipe_fds[0] >= 0) {
        close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }

    if(pipe_fds[1] >= 0) {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }
}

static void supervisor_child_redirect_pipe(int error_pipe_fd, supervisor_child_error_stage_t stage, int pipe_fds[2], int output_fd) {
    close(pipe_fds[0]);
    if(dup2(pipe_fds[1], output_fd) < 0) {
        supervisor_child_report_error(error_pipe_fd, stage, errno);
        _exit(255);
    }
    close(pipe_fds[1]);
}

static void supervisor_child_redirect_path(int error_pipe_fd, supervisor_child_error_stage_t stage, const char *path, bool create_log_dirs, int output_fd) {
    const char *redirect_path = supervisor_output_path_redirect_target(path, 0);
    if(redirect_path == 0) {
        return;
    }

    if(create_log_dirs && (supervisor_output_path_create_parent_dirs(redirect_path) != 0)) {
        supervisor_child_report_error(error_pipe_fd, stage, errno);
        _exit(255);
    }

    int fd = open(redirect_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd < 0) {
        supervisor_child_report_error(error_pipe_fd, stage, errno);
        _exit(255);
    }

    if(dup2(fd, output_fd) < 0) {
        supervisor_child_report_error(error_pipe_fd, stage, errno);
        close(fd);
        _exit(255);
    }

    close(fd);
}

static void supervisor_child_report_error(int error_pipe_fd, supervisor_child_error_stage_t stage, int error_code) {
    supervisor_child_error_t child_error = {
        .stage = stage,
        .error_code = error_code,
    };

    signal(SIGPIPE, SIG_IGN);

    const char *buffer = (const char *)&child_error;
    size_t written = 0;
    while(written < sizeof(child_error)) {
        ssize_t write_result = write(error_pipe_fd, buffer + written, sizeof(child_error) - written);
        if(write_result < 0) {
            if(errno == EINTR) {
                continue;
            }
            break;
        }
        if(write_result == 0) {
            break;
        }
        written += (size_t)write_result;
    }

    close(error_pipe_fd);
}

static int supervisor_read_child_error(int error_pipe_fd, supervisor_child_error_t *child_error) {
    if((error_pipe_fd < 0) || (child_error == 0)) {
        return 0;
    }

    fd_set read_fds;
    struct timeval timeout;

    while(1) {
        FD_ZERO(&read_fds);
        FD_SET(error_pipe_fd, &read_fds);
        timeout.tv_sec = SUPERVISOR_CHILD_ERROR_WAIT_MS / 1000;
        timeout.tv_usec = (SUPERVISOR_CHILD_ERROR_WAIT_MS % 1000) * 1000;

        int select_result = select(error_pipe_fd + 1, &read_fds, 0, 0, &timeout);
        if(select_result < 0) {
            if(errno == EINTR) {
                continue;
            }
            return 0;
        }

        if(select_result == 0) {
            return 0;
        }

        break;
    }

    ssize_t read_result = read(error_pipe_fd, child_error, sizeof(*child_error));
    return read_result == (ssize_t)sizeof(*child_error);
}

static void supervisor_log_child_error(const supervisor_control_block_t *scb, const supervisor_child_error_t *child_error, const char *stdout_target, const char *stderr_target) {
    const char *name = scb->application->name ? scb->application->name : "(unknown)";
    const char *path = scb->application->path ? scb->application->path : "(null)";
    const char *error = child_error->error_code > 0 ? strerror(child_error->error_code) : "unknown error";

    switch(child_error->stage) {
        case SUPERVISOR_CHILD_ERROR_PREPARE_ARGS:
            log_ni_error("supervisor_spawn() failed to prepare arguments for app %s (%s): %s", name, path, error);
            break;

        case SUPERVISOR_CHILD_ERROR_REDIRECT_STDOUT: {
            const char *target = stdout_target ? stdout_target : "stdout pipe";
            log_ni_error("supervisor_spawn() failed to redirect stdout for app %s to %s: %s", name, target, error);
        } break;

        case SUPERVISOR_CHILD_ERROR_REDIRECT_STDERR: {
            const char *target = stderr_target ? stderr_target : "stderr pipe";
            log_ni_error("supervisor_spawn() failed to redirect stderr for app %s to %s: %s", name, target, error);
        } break;

        case SUPERVISOR_CHILD_ERROR_EXEC:
            log_ni_error("supervisor_spawn() failed to spawn process %s for app %s: %s", path, name, error);
            break;

        default:
            log_ni_error("supervisor_spawn() failed to prepare child process %s for app %s: %s", path, name, error);
            break;
    }
}
