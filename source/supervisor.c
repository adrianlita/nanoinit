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

#include "supervisor.h"
#include "supervisor/internal.h"
#include "log.h"

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

volatile sig_atomic_t supervisor_got_signal_stop = 0;
volatile sig_atomic_t supervisor_got_signal_reload = 0;
supervisor_control_block_t *scb = 0;
int scb_count = 0;

static void supervisor_sigterm_cb(int signo);
static void supervisor_sigusr1_cb(int signo);

int supervisor_start(const nanoinit_arguments_t *arguments, const nanoinit_config_t *config) {
supervisor_start_begin:
    //initialize everything
    supervisor_got_signal_stop = 0;
    supervisor_got_signal_reload = 0;

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
        scb[i].desired_running = config->applications[i].autostart;
        scb[i].started_at = 0;
        supervisor_init_output_stream(&scb[i].stdout_stream);
        supervisor_init_output_stream(&scb[i].stderr_stream);
    }

    //register signals to nanoinit
    signal(SIGTERM, supervisor_sigterm_cb);
    signal(SIGINT, supervisor_sigterm_cb);
    signal(SIGQUIT, supervisor_sigterm_cb);
    signal(SIGUSR1, supervisor_sigusr1_cb);
    signal(SIGPIPE, SIG_IGN);

    supervisor_start_control_socket(arguments->control_socket_path);

    //spawn processes
    for(int i = 0; i < scb_count; i++) {
        if(scb[i].desired_running) {
            supervisor_spawn_result_t spawn_result = supervisor_spawn(&scb[i]);
            if(spawn_result == SUPERVISOR_SPAWN_STARTED) {
                log("supervisor_start() successfully spawned '%s' with pid %d", scb[i].application->name, (int)scb[i].pid);
            }
            else if(spawn_result == SUPERVISOR_SPAWN_ERROR) {
                log_app_error("supervisor_start() failed to spawn '%s'", scb[i].application->name);
            }
        }
        else {
            log("supervisor_start() process %s not spawned because autostart is disabled", scb[i].application->name);
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

    supervisor_close_control_socket();

    //wait for processes to terminate after SIGTERM was forwarded
    running = supervisor_any_running() || supervisor_any_stream_open();
    while(running) {
        supervisor_reap_children();
        supervisor_drain_output_streams(1000);
        running = supervisor_any_running() || supervisor_any_stream_open();
    }

    //cleanup
    supervisor_close_control_socket();
    supervisor_free_scb();
    
    //check whether a nanoinit-reload (SIGUSR1) was received and restart everytthing
    if(supervisor_got_signal_reload) {
        supervisor_got_signal_reload = 0;

        config_free();
        config = config_init(arguments->config_file, arguments->config_json_object);
        if(config == 0) {
            log_ni_error("supervisor_start() could not read new config; using zero-config");
        }

        log("supervisor_start() SIGUSR1 received, reloading and restarting everything according to new configuration");
        goto supervisor_start_begin;
    }

    return 0;
}

void supervisor_free_scb(void) {
    supervisor_close_all_output_streams();
    free(scb);
    scb = 0;
    scb_count = 0;
}

static void supervisor_sigterm_cb(int signo) {
    supervisor_got_signal_stop = signo;
}

static void supervisor_sigusr1_cb(int signo) {
    (void)signo;

    supervisor_got_signal_stop = SIGTERM;
    supervisor_got_signal_reload = 1;
}
