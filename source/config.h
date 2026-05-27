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

#pragma once

#include <stdbool.h>

typedef struct nanoinit_application_config_s {
    char *name;
    char *path;

    int arg_count;
    char **args;

    bool autorestart;
    bool autostart;

    char *stdout_path;
    char *stderr_path;
    bool stdout_passthrough;
    bool stderr_passthrough;

    int stdout_rotate_size;
    int stdout_rotate_count;
    int stderr_rotate_size;
    int stderr_rotate_count;
} nanoinit_application_config_t;

typedef struct nanoinit_config_s {
    nanoinit_application_config_t *applications;
    int application_count;
} nanoinit_config_t;

const nanoinit_config_t *config_init(const char *filename, const char *json_object);
void config_free();
