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

typedef struct supervisor_control_block_s {
    const nanoinit_application_config_t *application; //application data from config

    pid_t pid;
    int running;
} supervisor_control_block_t;

typedef enum supervisor_spawn_result_e {
    SUPERVISOR_SPAWN_ERROR = -1,
    SUPERVISOR_SPAWN_STARTED = 0,
    SUPERVISOR_SPAWN_SKIPPED = 1,
} supervisor_spawn_result_t;

static supervisor_spawn_result_t supervisor_spawn(supervisor_control_block_t *scb);
static void supervisor_free_scb();
static int supervisor_any_running(void);
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
        int defunct_status;
        pid_t defunct_pid = waitpid(-1, &defunct_status, WNOHANG);
        if(defunct_pid > 0) {
            for(int i = 0; i < scb_count; i++) {
                if(scb[i].pid == defunct_pid) {
                    if(WIFEXITED(defunct_status) && WEXITSTATUS(defunct_status) == 0) {
                        //clean exit
                        log("supervisor_start() process %s (pid=%d) finished with status %d", scb[i].application->name, (int)defunct_pid, WEXITSTATUS(defunct_status));
                    }
                    else if(WIFEXITED(defunct_status)) {
                        log_app_error("supervisor_start() process %s (pid=%d) exited with status %d", scb[i].application->name, (int)defunct_pid, WEXITSTATUS(defunct_status));
                    }
                    else if(WIFSIGNALED(defunct_status)) {
                        log_app_error("supervisor_start() process %s (pid=%d) terminated by signal %d", scb[i].application->name, (int)defunct_pid, WTERMSIG(defunct_status));
                    }
                    else {
                        log_app_error("supervisor_start() process %s (pid=%d) changed state with status %d", scb[i].application->name, (int)defunct_pid, defunct_status);
                    }

                    scb[i].running = 0;
                    if(scb[i].application->autorestart) {
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

        //checkAL poate exista un mecanism la waitpid sa nu mai facem sleep deloc, fara WNOHANG
        sleep(1);
    }

    //wait for processes to terminate after SIGTERM was forwarded
    running = supervisor_any_running();
    while(running) {
        int defunct_status;
        pid_t defunct_pid = wait(&defunct_status);
        if(defunct_pid > 0) {
            const char *name = 0;
            for(int i = 0; i < scb_count; i++) {
                if(scb[i].pid == defunct_pid) {
                    scb[i].running = 0;
                    name = scb[i].application->name;
                    break;
                }
            }
            if(name == 0) {
                name = "unknown";
            }
            if(WIFEXITED(defunct_status)) {
                log("supervisor_start() process %s (pid=%d) finished with status %d", name, (int)defunct_pid, WEXITSTATUS(defunct_status));
            }
            else if(WIFSIGNALED(defunct_status)) {
                log("supervisor_start() process %s (pid=%d) terminated by signal %d", name, (int)defunct_pid, WTERMSIG(defunct_status));
            }
            else {
                log("supervisor_start() process %s (pid=%d) changed state with status %d", name, (int)defunct_pid, defunct_status);
            }
        }
        else {
            if(errno != ECHILD) {
                log_ni_error("supervisor_start() wait() returned invalid value");
            }
            break;
        }

        running = supervisor_any_running();
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

    scb->running = 1;
    scb->pid = fork();
    if(scb->pid == -1) {
        log_ni_error("supervisor_spawn() fork failed");
        scb->running = 0;
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

        //redirect stderr ?
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

    return SUPERVISOR_SPAWN_STARTED;
}

static void supervisor_free_scb(void) {
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
