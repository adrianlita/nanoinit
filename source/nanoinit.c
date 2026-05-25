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

#define _GNU_SOURCE

#include "nanoinit.h"
#include "log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <dirent.h>
#include <unistd.h>

static pid_t get_nanoinit_pid();

void nanoinit_send_reload() {
    pid_t pid = get_nanoinit_pid();
    if(pid < 0) {
        log_ni_error("nanoinit_send_reload() could not get nanoinit PID");
        exit(1);
    }

    int result = kill(pid, SIGUSR1);
    if(result != 0) {
        log_ni_error("nanoinit_send_reload() could not send signal to nanoinit");
        exit(1);
    }

    exit(0);
}

static pid_t get_nanoinit_pid() {
    
    pid_t res = (pid_t)-1;
    pid_t mypid = getpid();

    struct dirent *files;
    DIR *dir = opendir("/proc");
    if(dir == 0) {
        log_ni_error("get_nanoinit_pid() could not open /proc");
        return -1;
    }

    while((files = readdir(dir)) != 0) {
        if((files->d_type == DT_DIR) || (files->d_type == DT_UNKNOWN)) {
            int cpid = 0;
            int i = 0;
            while(files->d_name[i]) {
                if((files->d_name[i] >= '0') && (files->d_name[i] <= '9')) {
                    cpid *= 10;
                    cpid += files->d_name[i] - '0';
                }
                else {
                    cpid = -1;
                    break;
                }
                i++;
            }

            if(cpid > 0) {
                if((pid_t)cpid != mypid) {
                    char *procfname;
                    int a = asprintf(&procfname, "/proc/%d/cmdline", cpid);
                    if((a < 0) || (procfname == 0)) {
                        log_ni_error("get_nanoinit_pid() failed memory allocation");
                        closedir(dir);
                        return -1;
                    }

                    FILE *f = fopen(procfname, "r");
                    free(procfname);
                    if(f) {
                        size_t size;
                        char buffer[4096];
                        size = fread(buffer, sizeof(char), sizeof(buffer) - 1, f);
                        fclose(f);

                        buffer[size] = 0;
                        log("%s", buffer);

                        if(size) {
                            if(strstr(buffer, "nanoinit") != 0) {
                                //found!
                                res = (pid_t)cpid;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    closedir(dir);
    return res;
}
