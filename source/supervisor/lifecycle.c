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
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void supervisor_log_process_status(const char *name, pid_t pid, int status, int app_error);

supervisor_spawn_result_t supervisor_spawn(supervisor_control_block_t *scb) {
    int stdout_pipe[2] = { -1, -1 };
    int stderr_pipe[2] = { -1, -1 };
    int stdout_is_file = supervisor_file_output_configured(scb->application->stdout_path);
    int stderr_is_file = supervisor_file_output_configured(scb->application->stderr_path);
    int pipe_stdout = supervisor_output_stream_configured(
        scb->application->stdout_path,
        scb->application->stdout_rotate_size,
        scb->application->stdout_passthrough,
        scb->application->prefix_logs
    );
    int pipe_stderr = supervisor_output_stream_configured(
        scb->application->stderr_path,
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

    if(scb->application->stdout_passthrough && (scb->application->stdout_path != 0) && !stdout_is_file) {
        log_ni_error("supervisor_spawn() stdout passthrough ignored for app %s because stdout is not a file path", scb->application->name);
    }

    if(scb->application->stderr_passthrough && (scb->application->stderr_path != 0) && !stderr_is_file) {
        log_ni_error("supervisor_spawn() stderr passthrough ignored for app %s because stderr is not a file path", scb->application->name);
    }

    if(pipe_stdout && (pipe(stdout_pipe) != 0)) {
        log_ni_error("supervisor_spawn() could not create stdout pipe for app %s", scb->application->name);
        return SUPERVISOR_SPAWN_ERROR;
    }

    if(pipe_stderr && (pipe(stderr_pipe) != 0)) {
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
        scb->started_at = 0;
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
        signal(SIGPIPE, SIG_DFL);

        //redirect stdout ?
        if(pipe_stdout) {
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
        if(pipe_stderr) {
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

    if(pipe_stdout) {
        int passthrough_fd = -1;
        if(stdout_is_file) {
            passthrough_fd = scb->application->stdout_passthrough ? STDOUT_FILENO : -1;
        }
        else if(scb->application->stdout_path == 0) {
            passthrough_fd = STDOUT_FILENO;
        }
        close(stdout_pipe[1]);
        if(supervisor_start_output_stream(
            &scb->stdout_stream,
            stdout_pipe[0],
            scb->application->stdout_path,
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
        else if(scb->application->stderr_path == 0) {
            passthrough_fd = STDERR_FILENO;
        }
        close(stderr_pipe[1]);
        if(supervisor_start_output_stream(
            &scb->stderr_stream,
            stderr_pipe[0],
            scb->application->stderr_path,
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
