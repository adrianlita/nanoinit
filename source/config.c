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

#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "log.h"
#include "edJSON/edJSON.h"

static nanoinit_config_t config = {0};


#define EDJSON_PATH_MAX             32      //this practically depends on the tree depth of the JSON object; nanoinit needs only 3 levels when used without a JSON object
#define JSON_PARSE_BUFFER_SIZE      1024    //this should fit max build path length

typedef struct config_message_s {
    //input
    const char *json_object;
    uint32_t json_object_length;
    uint8_t json_object_components;

    //output
    int return_code;

    //state variables
    enum {
        CONFIG_STATE_SEARCHING = 0,
        CONFIG_STATE_FOUND,
        CONFIG_STATE_FINISHED,
    } state;

    char *current_app;
} config_message_t;

static int edJSON_callback(const edJSON_path_t *path, size_t path_size, edJSON_value_t value, void *private);
static bool config_path_is_inside_json_object(const config_message_t *config_message, const char *path);
static int config_set_non_negative_integer(config_message_t *config_message, edJSON_value_t value, int *destination, const char *field_name, const char *app_name);

const nanoinit_config_t *config_init(const char *filename, const char *json_object) {
    config_free();

    if(filename == 0) {
        return &config;
    }

    char *json_content = 0;
    long length = 0;
    FILE *f = fopen(filename, "rb");
    if(f) {
        if((fseek(f, 0, SEEK_END) != 0) || ((length = ftell(f)) < 0) || (fseek(f, 0, SEEK_SET) != 0)) {
            log_ni_error("config_init() could not read JSON file size for %s", filename);
        }
        else {
            json_content = (char*)malloc(sizeof(char) * ((size_t)length + 1));
            if(json_content) {
                size_t s = fread(json_content, 1, (size_t)length, f);
                if(ferror(f)) {
                    log_ni_error("config_init() could not read JSON file %s", filename);
                    free(json_content);
                    json_content = 0;
                }
                else {
                    json_content[s] = 0;
                }
            }
            else {
                log_ni_error("config_init() bad memory allocation");
            }
        }
        fclose(f);
    }
    else {
        log_ni_error("config_init() JSON file %s could not be opened", filename);
    }

    if(json_content) {
        edJSON_path_t edJSON_path[EDJSON_PATH_MAX];
        config_message_t config_message;

        if(json_object == 0) {
            config_message.state = CONFIG_STATE_FOUND;
            config_message.json_object = "";
            config_message.json_object_components = 0;
            config_message.return_code = 0;     //by default, object is found
        }
        else {
            config_message.state = CONFIG_STATE_SEARCHING;
            config_message.json_object = json_object;
            config_message.json_object_components = 0;
            config_message.return_code = 1;     //by default, object is not found
        }
        
        config_message.json_object_length = strlen(config_message.json_object);
        
        //calculate json_object components
        size_t i = 0;
        while(i < config_message.json_object_length) {
            if(config_message.json_object[i] == '/') {
                config_message.json_object_components++;
            }
            i++;
        }

        config_message.current_app = strdup("");
        if(config_message.current_app == 0) {
            log_ni_error("config_init() bad memory allocation");
            free(json_content);
            return &config;
        }

        int rc = edJSON_parse(json_content, edJSON_path, EDJSON_PATH_MAX, edJSON_callback, (void*)&config_message);
        free(json_content);
        free(config_message.current_app);

        bool has_config = false;
        if(rc != EDJSON_SUCCESS) {
            log_ni_error("config_init() JSON parsing error");
        }
        else {
            switch(config_message.return_code) {
                case 0:
                    has_config = true;
                    break;

                case 1:
                    log_ni_error("config_init() JSON parsing: object '%s' not found in config", json_object);
                    break;

                case 2:
                    log_ni_error("config_init() JSON parsing: bad config parameters");
                    break;

                case 3:
                    log_ni_error("config_init() JSON parsing: no memory left");
                    break;

                case 4:
                    log_ni_error("config_init() JSON parsing: invalid string object found");
                    break;

                default:
                    log_ni_error("config_init() JSON parsing ok; unknown error");
                    break;
            }
        }

        //validate config data
        if(has_config) {
            for(int i = 0; i < config.application_count; i++) {
                if(config.applications[i].name == 0) {
                    has_config = false;
                    break;
                }

                if(config.applications[i].path == 0) {
                    has_config = false;
                    break;
                }
            }
        }
        
        //zero out config
        if(!has_config) {
            config_free();
            memset(&config, 0, sizeof(nanoinit_config_t));
        }
    }

    return &config;
}

void config_free() {
    for(int i = 0; i < config.application_count; i++) {
        free(config.applications[i].name);
        free(config.applications[i].path);

        for(int j = 0; j < config.applications[i].arg_count; j++) {
            free(config.applications[i].args[j]);
        }
        free(config.applications[i].args);

        free(config.applications[i].stdout_path);
        free(config.applications[i].stderr_path);
        free(config.applications[i].prefix_logs);
    }

    free(config.applications);
    config.applications = 0;
    config.application_count = 0;
    free(config.ni_log_format);
    config.ni_log_format = 0;
}

static int edJSON_callback(const edJSON_path_t *path, size_t path_size, edJSON_value_t value, void *private) {
    config_message_t *config_message = (config_message_t *)private;
    

    char pv[JSON_PARSE_BUFFER_SIZE];
    int rc = edJSON_build_path_string(pv, JSON_PARSE_BUFFER_SIZE, path, path_size);
    if(rc < 0) {
        log_ni_error("edJSON_callback() edJSON_build_path_string failed");
        config_message->return_code = 3;    //no memory left
        return rc;  //stop parsing
    }

    
switch_config_message_state:
    switch(config_message->state) {
        case CONFIG_STATE_SEARCHING: {
            if(config_path_is_inside_json_object(config_message, pv)) {
                config_message->state = CONFIG_STATE_FOUND;
                config_message->return_code = 0;
                goto switch_config_message_state;
            }
        } break;

        case CONFIG_STATE_FOUND: {
            if(!config_path_is_inside_json_object(config_message, pv)) {
                config_message->state = CONFIG_STATE_FINISHED;
                goto switch_config_message_state;
            }
            
            //parse
            if(path_size <= config_message->json_object_components) {
                config_message->return_code = 2;
                return 1;
            }

            char current_value[JSON_PARSE_BUFFER_SIZE];
            size_t component = config_message->json_object_components;

            if(path[component].index >= 0) {
                config_message->return_code = 2;
                return 1;
            }

            int rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, path[component].value, path[component].value_size);
            if(rc < EDJSON_SUCCESS) {
                config_message->return_code = 4;
                return 1;
            }

            if(strcmp(current_value, "ni_log_format") == 0) {
                if(path_size != (component + 1)) {
                    log_ni_error("edJSON_callback() ni_log_format should not have child objects");
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_STRING) {
                    log_ni_error("edJSON_callback() ni_log_format value type should be string");
                    config_message->return_code = 2;
                    return 1;
                }

                rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, value.value.string.value, value.value.string.value_size);
                if(rc < EDJSON_SUCCESS) {
                    config_message->return_code = 4;
                    return 1;
                }

                char *ni_log_format = strdup(current_value);
                if(ni_log_format == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
                free(config.ni_log_format);
                config.ni_log_format = ni_log_format;
                return 0;
            }

            if(strcmp(config_message->current_app, current_value) != 0) {
                free(config_message->current_app);
                config_message->current_app = strdup(current_value);
                if(config_message->current_app == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }

                config.application_count++;
                nanoinit_application_config_t *applications = (nanoinit_application_config_t *)realloc(config.applications, sizeof(nanoinit_application_config_t) * config.application_count);
                if(applications == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config.application_count--;
                    config_message->return_code = 3;
                    return 1;
                }
                config.applications = applications;

                //init memory
                memset(&config.applications[config.application_count - 1], 0, sizeof(nanoinit_application_config_t));
                config.applications[config.application_count - 1].autostart = true;
                config.applications[config.application_count - 1].stdout_rotate_count = 1;
                config.applications[config.application_count - 1].stderr_rotate_count = 1;

                //set name
                config.applications[config.application_count - 1].name = strdup(current_value);
                if(config.applications[config.application_count - 1].name == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
            }

            //move on to first property of the application
            component++;
            if(component >= path_size) {
                config_message->return_code = 2;
                return 1;
            }

            if(path[component].index >= 0) {
                config_message->return_code = 2;
                return 1;
            }

            rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, path[component].value, path[component].value_size);
            if(rc < EDJSON_SUCCESS) {
                config_message->return_code = 4;
                return 1;
            }

            component++;

            //if component is path
            if(strcmp(current_value, "path") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() path should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_STRING) {
                    log_ni_error("edJSON_callback() path value type should be string for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, value.value.string.value, value.value.string.value_size);
                if(rc < EDJSON_SUCCESS) {
                    config_message->return_code = 4;
                    return 1;
                }

                //set path
                char *path_copy = strdup(current_value);
                if(path_copy == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
                free(config.applications[config.application_count - 1].path);
                config.applications[config.application_count - 1].path = path_copy;
            }

            //if component is args
            else if(strcmp(current_value, "args") == 0) {
                if((component < path_size) && (path[component].index >= 0)) {
                    component++;
                }

                if(path_size != component) {
                    log_ni_error("edJSON_callback() argument itself should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_STRING) {
                    log_ni_error("edJSON_callback() argument value type should be string for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, value.value.string.value, value.value.string.value_size);
                if(rc < EDJSON_SUCCESS) {
                    config_message->return_code = 4;
                    return 1;
                }

                //add argument
                config.applications[config.application_count - 1].arg_count++;
                char **args = (char **)realloc(config.applications[config.application_count - 1].args, sizeof(char *) * config.applications[config.application_count - 1].arg_count);
                if(args == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config.applications[config.application_count - 1].arg_count--;
                    config_message->return_code = 3;
                    return 1;
                }
                config.applications[config.application_count - 1].args = args;

                config.applications[config.application_count - 1].args[config.applications[config.application_count - 1].arg_count - 1] = strdup(current_value);
                if(config.applications[config.application_count - 1].args[config.applications[config.application_count - 1].arg_count - 1] == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
            }

            //if component is autorestart
            else if(strcmp(current_value, "autorestart") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() autorestart should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_BOOL) {
                    log_ni_error("edJSON_callback() autorestart value type should be boolean for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                //set autorestart
                config.applications[config.application_count - 1].autorestart = value.value.boolean;
            }

            //if component is autostart
            else if(strcmp(current_value, "autostart") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() autostart should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_BOOL) {
                    log_ni_error("edJSON_callback() autostart value type should be boolean for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                //set autostart
                config.applications[config.application_count - 1].autostart = value.value.boolean;
            }

            //if component is prefix_logs
            else if(strcmp(current_value, "prefix_logs") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() prefix_logs should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_STRING) {
                    log_ni_error("edJSON_callback() prefix_logs value type should be string for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, value.value.string.value, value.value.string.value_size);
                if(rc < EDJSON_SUCCESS) {
                    config_message->return_code = 4;
                    return 1;
                }

                char *prefix_logs = strdup(current_value);
                if(prefix_logs == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
                free(config.applications[config.application_count - 1].prefix_logs);
                config.applications[config.application_count - 1].prefix_logs = prefix_logs;
            }

            //if component is stdout
            else if(strcmp(current_value, "stdout") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stdout should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_STRING) {
                    log_ni_error("edJSON_callback() stdout value type should be string for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, value.value.string.value, value.value.string.value_size);
                if(rc < EDJSON_SUCCESS) {
                    config_message->return_code = 4;
                    return 1;
                }

                //set stdout_path
                char *stdout_path = strdup(current_value);
                if(stdout_path == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
                free(config.applications[config.application_count - 1].stdout_path);
                config.applications[config.application_count - 1].stdout_path = stdout_path;
            }

            //if component is stdout_passthrough
            else if(strcmp(current_value, "stdout_passthrough") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stdout_passthrough should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_BOOL) {
                    log_ni_error("edJSON_callback() stdout_passthrough value type should be boolean for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                //set stdout_passthrough
                config.applications[config.application_count - 1].stdout_passthrough = value.value.boolean;
            }

            //if component is stderr
            else if(strcmp(current_value, "stderr") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stderr should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_STRING) {
                    log_ni_error("edJSON_callback() stderr value type should be string for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                rc = edJSON_string_unescape(current_value, JSON_PARSE_BUFFER_SIZE, value.value.string.value, value.value.string.value_size);
                if(rc < EDJSON_SUCCESS) {
                    config_message->return_code = 4;
                    return 1;
                }

                //set stderr_path
                char *stderr_path = strdup(current_value);
                if(stderr_path == 0) {
                    log_ni_error("edJSON_callback() bad memory allocation");
                    config_message->return_code = 3;
                    return 1;
                }
                free(config.applications[config.application_count - 1].stderr_path);
                config.applications[config.application_count - 1].stderr_path = stderr_path;
            }

            //if component is stderr_passthrough
            else if(strcmp(current_value, "stderr_passthrough") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stderr_passthrough should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(value.value_type != EDJSON_VT_BOOL) {
                    log_ni_error("edJSON_callback() stderr_passthrough value type should be boolean for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                //set stderr_passthrough
                config.applications[config.application_count - 1].stderr_passthrough = value.value.boolean;
            }

            //if component is stdout_rotate_size
            else if(strcmp(current_value, "stdout_rotate_size") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stdout_rotate_size should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(config_set_non_negative_integer(config_message, value, &config.applications[config.application_count - 1].stdout_rotate_size, "stdout_rotate_size", config.applications[config.application_count - 1].name) != 0) {
                    return 1;
                }
            }

            //if component is stdout_rotate_count
            else if(strcmp(current_value, "stdout_rotate_count") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stdout_rotate_count should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(config_set_non_negative_integer(config_message, value, &config.applications[config.application_count - 1].stdout_rotate_count, "stdout_rotate_count", config.applications[config.application_count - 1].name) != 0) {
                    return 1;
                }
            }

            //if component is stderr_rotate_size
            else if(strcmp(current_value, "stderr_rotate_size") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stderr_rotate_size should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(config_set_non_negative_integer(config_message, value, &config.applications[config.application_count - 1].stderr_rotate_size, "stderr_rotate_size", config.applications[config.application_count - 1].name) != 0) {
                    return 1;
                }
            }

            //if component is stderr_rotate_count
            else if(strcmp(current_value, "stderr_rotate_count") == 0) {
                if(path_size != component) {
                    log_ni_error("edJSON_callback() stderr_rotate_count should not have child objects for app %s", config.applications[config.application_count - 1].name);
                    config_message->return_code = 2;
                    return 1;
                }

                if(config_set_non_negative_integer(config_message, value, &config.applications[config.application_count - 1].stderr_rotate_count, "stderr_rotate_count", config.applications[config.application_count - 1].name) != 0) {
                    return 1;
                }
            }

            //if component is anything lese
            else {
                config_message->return_code = 2;    //invalid parameter
                return 1;
            }
        } break;

        case CONFIG_STATE_FINISHED: {
            config_message->return_code = 0;
            return 1;   //stop parsing
        }

        default:
            break;
    }

    return 0;
}

static bool config_path_is_inside_json_object(const config_message_t *config_message, const char *path) {
    if(config_message->json_object_length == 0) {
        return true;
    }

    return (strlen(path) > config_message->json_object_length)
        && (strncmp(config_message->json_object, path, config_message->json_object_length) == 0)
        && (path[config_message->json_object_length] == '/');
}

static int config_set_non_negative_integer(config_message_t *config_message, edJSON_value_t value, int *destination, const char *field_name, const char *app_name) {
    if(value.value_type != EDJSON_VT_INTEGER) {
        log_ni_error("edJSON_callback() %s value type should be integer for app %s", field_name, app_name);
        config_message->return_code = 2;
        return -1;
    }

    if(value.value.integer < 0) {
        log_ni_error("edJSON_callback() %s value should not be negative for app %s", field_name, app_name);
        config_message->return_code = 2;
        return -1;
    }

    *destination = value.value.integer;
    return 0;
}
