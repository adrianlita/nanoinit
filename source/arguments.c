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

#include "arguments.h"
#include <argp.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef NANOINIT_DEFAULT_CONFIG_FILE
#define NANOINIT_DEFAULT_CONFIG_FILE "/etc/nanoinit/config.json"
#endif

#ifndef NANOINIT_DEFAULT_CONTROL_SOCKET
#define NANOINIT_DEFAULT_CONTROL_SOCKET "/tmp/nanoinit.sock"
#endif

static nanoinit_arguments_t arguments = {0};

const char *argp_program_version = "nanoinit v0.0.1 build 123451234";
const char *argp_program_bug_address = "<adrian@axiplus.com>";
static char doc[] = "nanoinit - for documentation and usage check https://github.com/adrianlita/nanoinit";
static char args_doc[] = "[status APP | start APP | stop APP | list | ls]";

static struct argp_option options[] = {
    { "config-file", 'c', "/path/to/config.json", 0, "Specifies the configuration JSON file. If omitted, nanoinit uses /etc/nanoinit/config.json when it exists; otherwise no apps will be run, but nanoinit will sleep for infinity and wait for a kill signal.", 0 },
    { "config-json-object", 'j', "nanoinit-settings", 0, "Specifies the parent JSON object. Default value is null, which means that it will look directly into the root of the JSON file.", 0},
    { "log-path", 'l', "/path/to/log.txt", 0, "Specified the path for writing log-files. Default only uses stderr and stdout for logging.", 0 },
    { "reload", 'r', 0, 0, "Looks for top nanoinit process and sends a SIGUSR1 signal to it, forcing it to terminate all apps, reload config and restart apps.", 0 },
    { "verbose", 'v', "0-2", 0, "Specified application print verbosity level. Values are 0(nanoinit ERR)-default, 1(application ERR), 2(LOG).", 0 },
    { 0 } 
};

static error_t argp_parse_cb(int key, char *arg, struct argp_state *state);

const nanoinit_arguments_t *arguments_init(int argc, char **argv) {
    //parse provided command line arguments
    struct argp argp = { options, argp_parse_cb, args_doc, doc, 0, 0, 0 };
    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    //check config file and config json object in environment vars
    char *config_file_env = getenv("NANOINIT_CONFIG_FILE");
    if(config_file_env != 0) {
        if(arguments.config_file) {
            free(arguments.config_file);
        }
        arguments.config_file = strdup(config_file_env);
    }

    if((arguments.config_file == 0) && (access(NANOINIT_DEFAULT_CONFIG_FILE, F_OK) == 0)) {
        arguments.config_file = strdup(NANOINIT_DEFAULT_CONFIG_FILE);
    }

    char *config_json_object_env = getenv("NANOINIT_CONFIG_JSON_OBJECT");
    if(config_json_object_env != 0) {
        if(arguments.config_json_object) {
            free(arguments.config_json_object);
        }
        arguments.config_json_object = strdup(config_json_object_env);
    }

    char *control_socket_env = getenv("NANOINIT_CONTROL_SOCKET");
    if(control_socket_env != 0) {
        arguments.control_socket_path = strdup(control_socket_env);
    }
    else {
        arguments.control_socket_path = strdup(NANOINIT_DEFAULT_CONTROL_SOCKET);
    }

    return &arguments;
}

static error_t argp_parse_cb(int key, char *arg, struct argp_state *state) {
    nanoinit_arguments_t *iter_arguments = state->input;
    switch (key) {
        case 'c':
            if(arg == 0) {
                return ARGP_ERR_UNKNOWN;
            }

            if(iter_arguments->config_file) {
                free(iter_arguments->config_file);
                iter_arguments->config_file = 0;
            }
            iter_arguments->config_file = strdup(arg);
            if(iter_arguments->config_file == 0) {
                return ARGP_ERR_UNKNOWN;
            }
            break;

        case 'j':
            if(arg == 0) {
                return ARGP_ERR_UNKNOWN;
            }

            if(iter_arguments->config_json_object) {
                free(iter_arguments->config_json_object);
                iter_arguments->config_json_object = 0;
            }
            iter_arguments->config_json_object = strdup(arg);
            if(iter_arguments->config_json_object == 0) {
                return ARGP_ERR_UNKNOWN;
            }
            break;

        case 'l':
            if(arg == 0) {
                return ARGP_ERR_UNKNOWN;
            }

            if(iter_arguments->log_path) {
                free(iter_arguments->log_path);
                iter_arguments->log_path = 0;
            }
            iter_arguments->log_path = strdup(arg);
            if(iter_arguments->log_path == 0) {
                return ARGP_ERR_UNKNOWN;
            }
            break;

        case 'r':
            iter_arguments->special_mode = NI_COMMAND_RELOAD;
            break;

        case 'v':
            if(arg == 0) {
                return ARGP_ERR_UNKNOWN;
            }

            if(arg[1] != 0) {
                argp_usage(state);
            }

            iter_arguments->verbosity_level  = arg[0] - '0';
            if((iter_arguments->verbosity_level < 0) || (iter_arguments->verbosity_level > 2)) {
                //invalid verbosity level
                argp_usage(state);
            }
            break;

        case ARGP_KEY_ARG:
            if(state->arg_num == 0) {
                if(iter_arguments->special_mode != NI_NO_SPECIAL_MODE) {
                    argp_usage(state);
                }

                if(strcmp(arg, "status") == 0) {
                    iter_arguments->special_mode = NI_COMMAND_CONTROL_STATUS;
                }
                else if(strcmp(arg, "start") == 0) {
                    iter_arguments->special_mode = NI_COMMAND_CONTROL_START;
                }
                else if(strcmp(arg, "stop") == 0) {
                    iter_arguments->special_mode = NI_COMMAND_CONTROL_STOP;
                }
                else if((strcmp(arg, "list") == 0) || (strcmp(arg, "ls") == 0)) {
                    iter_arguments->special_mode = NI_COMMAND_CONTROL_LIST;
                }
                else {
                    argp_usage(state);
                }
            }
            else if(state->arg_num == 1) {
                if((iter_arguments->special_mode != NI_COMMAND_CONTROL_STATUS) &&
                   (iter_arguments->special_mode != NI_COMMAND_CONTROL_START) &&
                   (iter_arguments->special_mode != NI_COMMAND_CONTROL_STOP)) {
                    argp_usage(state);
                }

                iter_arguments->control_app_name = strdup(arg);
                if(iter_arguments->control_app_name == 0) {
                    return ARGP_ERR_UNKNOWN;
                }
            }
            else {
                argp_usage(state);
            }
            break;

        case ARGP_KEY_END:
            if(((iter_arguments->special_mode == NI_COMMAND_CONTROL_STATUS) ||
                (iter_arguments->special_mode == NI_COMMAND_CONTROL_START) ||
                (iter_arguments->special_mode == NI_COMMAND_CONTROL_STOP)) &&
               (iter_arguments->control_app_name == 0)) {
                argp_usage(state);
            }

            if((iter_arguments->special_mode == NI_COMMAND_CONTROL_LIST) &&
               (iter_arguments->control_app_name != 0)) {
                argp_usage(state);
            }

            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

void arguments_free() {
    free(arguments.config_file);
    free(arguments.config_json_object);
    free(arguments.log_path);
    free(arguments.control_socket_path);
    free(arguments.control_app_name);
}
