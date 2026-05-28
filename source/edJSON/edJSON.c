/**
 * MIT License
 * 
 * Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Luiza Rusnac - www.axiplus.com
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

#include "edJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

typedef enum {
    edJSON_STATE_PARSE_BEGIN = 0,
    edJSON_STATE_PARSE_FINISHED,

    edJSON_STATE_IN_COMMENT,

    edJSON_STATE_FIND_VALUE,

    edJSON_STATE_IN_OBJECT,
    edJSON_STATE_IN_OBJECT_NAME,
    edJSON_STATE_IN_VALUE,
    edJSON_STATE_IN_ARRAY,
    
    edJSON_STATE_FIND_COLON,
    edJSON_STATE_FIND_COMMA,
} edJSON_state_t;

int edJSON_parse(const char *json, edJSON_path_t *path_mem, size_t path_max_depth, edJSON_cb_t jsonEvent, void *private) {
    if(json == 0) {
        return EDJSON_ERR_NO_INPUT;
    }

    if((path_max_depth == 0) || (path_mem == 0)) {
        return EDJSON_ERR_NO_MEMORY;
    }

    edJSON_state_t state = edJSON_STATE_PARSE_BEGIN;
    edJSON_value_t val;
    size_t top = 0;
    size_t i = 0;

    while(json[i] != 0) {
        switch(state) {
            case edJSON_STATE_PARSE_BEGIN: {
                path_mem[top]._prev = state;
                path_mem[top].index = -1;
                if(!strchr("\r\n\t/{[ ", json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if((json[i] != 0) && (json[i] == '/')) {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                    }
                    else {
                        return i + 1;
                    }
                }
                else if(json[i] == '{') {
                    state = edJSON_STATE_IN_OBJECT;

                    if(top < path_max_depth) {
                        path_mem[top]._prev = state;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    }
                }
                else if(json[i] == '[') {
                    state = edJSON_STATE_IN_ARRAY;

                    if(top < path_max_depth) {
                        path_mem[top]._prev = state;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    }
                    break;
                }
            } break;
                
            case edJSON_STATE_IN_COMMENT:
                if((json[i] == '\r') || (json[i] == '\n')) {
                    state = path_mem[top]._prev;
                    top--;
                }
                break;

            case edJSON_STATE_IN_OBJECT:
                if(!strchr("\n\b\r\t\"/} ", json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if((json[i] != 0) && (json[i] == '/')) {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                    }
                    else {
                        return i + 1;
                    }
                }
                else if(json[i] == '"') {
                    if(top < path_max_depth) {
                        path_mem[top].index = -1;
                        path_mem[top].value = json + i + 1;
                        path_mem[top].value_size = 0;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    }
                    state = edJSON_STATE_IN_OBJECT_NAME;  
                }
                else if(json[i] == '}') {
                    if(path_mem[top]._prev != edJSON_STATE_IN_OBJECT) {
                        return i + 1;
                    }
                    path_mem[top].value = 0;
                    top--;
                    state = edJSON_STATE_FIND_COMMA;
                }
                break;

            case edJSON_STATE_IN_OBJECT_NAME:
                if(json[i] == '\\') {
                    i++;
                    if(json[i] != 0) {
                        if(json[i] == 'u') {
                            i++;
                            size_t val = 0;
                            while((val < 4) && (json[i] != 0)) {
                                if(!strchr("0123456789ABCDEFabcdef", json[i])) {
                                    return i + 1;
                                }
                                i++;
                                val++;
                            }
                            i--;
                        }
                        else if(!strchr("\"\\/ubfnrt", json[i])) {
                            return i + 1;
                        }
                    }
                }
                else if(json[i] == '"') {
                    path_mem[top].value_size = json + i - path_mem[top].value;
                    state = edJSON_STATE_FIND_COLON;
                }

                break;

            case edJSON_STATE_FIND_COLON:
                if(!strchr("\n\r\t /:",json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if((json[i] == '/') && (json[i] != 0)) {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                    }
                    else {
                        return i + 1;
                    }
                }
                else if(json[i] == ':') {
                    state = edJSON_STATE_FIND_VALUE;
                }
                break;

            case edJSON_STATE_FIND_VALUE:
                if(!strchr("\n\r\t /{[\"-0123456789tfn", json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if((json[i] == '/') && (json[i] != 0)) {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                        break;
                    }
                    else {
                        return i + 1;
                    }
                }
                else if(json[i] == '{') {
                    state = edJSON_STATE_IN_OBJECT;

                    top++;
                    if(top < path_max_depth) {
                        path_mem[top]._prev = state;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    } 
                    break;
                }
                else if(json[i] == '[') {
                    state = edJSON_STATE_IN_ARRAY;

                    top++;
                    if(top < path_max_depth) {
                        path_mem[top]._prev = state;
                        path_mem[top].index = -1;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    }
                    break;
                }
                else if(strchr("\"-ntf0123456789", json[i])) {
                    state = edJSON_STATE_IN_VALUE;
                    continue;
                }
                break;

            case edJSON_STATE_FIND_COMMA:
                if(!strchr("\n\r\t /,]}", json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if((json[i] == '/') && (json[i] != 0)) {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                    }
                    else {
                        return i + 1;
                    }
                }
                else if(json[i] == ',') {
                    state = path_mem[top]._prev;
                }
                else if(json[i] == ']') {
                     if(path_mem[top]._prev != edJSON_STATE_IN_ARRAY) {
                        return i + 1;
                    }
                    state = edJSON_STATE_FIND_COMMA;
                    path_mem[top].index = -1;
                    path_mem[top].value = 0;
                    top--;
                }
                else if(json[i] == '}') {
                    if(path_mem[top]._prev != edJSON_STATE_IN_OBJECT) {
                        return i + 1;
                    }
                    path_mem[top].value = 0;

                    if(top == 0) {
                        state = edJSON_STATE_PARSE_FINISHED;
                    }
                    else {
                        top--;
                    }
                }
                break;

            case edJSON_STATE_IN_ARRAY: {      
                if(!strchr("\n\r\t\\ /{[\"]-+ntf0123456789", json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if((json[i] == '/') && (json[i] != 0)) {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                    }
                    else {
                        return i + 1;
                    }
                }
                else if(json[i] == '{') {
                    state = edJSON_STATE_IN_OBJECT;
                    path_mem[top].index++;

                    top++;
                    if(top < path_max_depth) {
                        path_mem[top]._prev = state;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    }
                }
                else if(json[i] == '[') {
                    state = edJSON_STATE_IN_ARRAY;
                    path_mem[top].index++;

                    top++;
                    if(top < path_max_depth) {
                        path_mem[top]._prev = state;
                        path_mem[top].index = -1;
                    }
                    else {
                        return EDJSON_ERR_NO_MEMORY;
                    }                    
                }
                else if(json[i] == ']') {
                    if(path_mem[top]._prev != edJSON_STATE_IN_ARRAY) {
                        return i + 1;
                    }
                    state = edJSON_STATE_FIND_COMMA;
                    path_mem[top].index = -1;
                    path_mem[top].value = 0;
                    top--;
                }
                else if(strchr("\"-+ntf0123456789", json[i])) {
                    state = edJSON_STATE_IN_VALUE;
                    path_mem[top].index++;
                    continue;
                }
            } break;

            case edJSON_STATE_IN_VALUE:
                if(json[i] == '"') {
                    val.value_type = EDJSON_VT_STRING;
                    i++;
                    val.value.string.value = json + i;
                    val.value.string.value_size = i;

                    while((json[i] != 0) && (json[i] != '"')) {
                        if(json[i] == '\n') {
                            return i + 1;
                        }
                        if(json[i] == '\\') {
                            i++;
                            if(!strchr("\"\\/ubfnrt", json[i])) {
                                return i + 1;
                            }
                            if(json[i] == 'u') {
                                i++;
                                size_t val = 0;
                                while((val < 4) && (json[i] != 0)) {
                                    if(!strchr("0123456789ABCDEFabcdef", json[i])) {
                                        return i + 1;
                                    }
                                    i++;
                                    val++;
                                }
                                i--;
                            }
                        }
                        i++;
                    }
                    val.value.string.value_size = i - val.value.string.value_size;
                }
                else if(json[i] == 't') {
                    val.value_type = EDJSON_VT_BOOL;
                    char t[] = "true";
                    int j = 0;
                    while(t[j]) {
                        if(json[i] != t[j]) {
                            return i + 1;
                        }
                        j++;
                        i++;
                    }
                    i--;
                    val.value.boolean = true;
                }
                else if(json[i] == 'f') {
                    val.value_type = EDJSON_VT_BOOL;
                    char f[] = "false";
                    int j = 0;
                    while(f[j]) {
                        if(json[i] != f[j]) {
                            return i + 1;
                        }
                        j++;
                        i++;
                    }
                    i--;
                    val.value.boolean = false;
                }
                else if(json[i] == 'n') {
                    val.value_type = EDJSON_VT_NULL;
                    char n[] = "null";
                    int j = 0;
                    while(n[j]) {
                        if(json[i] != n[j]) {
                            return i + 1;
                        }
                        j++;
                        i++;
                    }
                    i--;
                }
                else {
                    val.value_type = EDJSON_VT_INTEGER;
                    int nrc = 0;
                    char nr[32];
                    bool b[3] = {0, 0, 0};
                    while((json[i] != 0) && strchr(".Ee+-0123456789", json[i])) {
                        if(nrc >= 32) {
                            return i + 1;
                        }    
                        if((json[i] == '+') || (json[i] == '-')) {
                            nr[nrc++] = json[i];
                            i++;
                            if(!strchr("0123456789", json[i])) {
                                return i + 1;
                            }
                        }
                        nr[nrc++] = json[i];
                        i++;
                        if(strchr(".eE+-", json[i])) {
                            val.value_type = EDJSON_VT_DOUBLE;
                            if(json[i] == '.') {
                                if((b[0] == 1) || (b[1] == 1)){
                                    return i + 1;
                                }
                                else {
                                    b[0] = 1;
                                }
                                nr[nrc++] = json[i];
                                i++;
                                if(!strchr("0123456789",json[i])) {
                                    return i + 1;
                                }
                            }
                            if((json[i] == 'e') || (json[i] == 'E')) {
                                if(b[1] == 1){
                                    return i + 1;
                                }
                                else {
                                    b[1] = 1;
                                    nr[nrc++] = json[i];
                                    i++;
                                    if((json[i] == '+') || (json[i] == '-')) {
                                        if(b[2] == 1) {
                                            return i + 1;
                                        }
                                        else {
                                            b[2] = 1;
                                            continue;
                                        }
                                    }
                                    if(!strchr("0123456789",json[i])) {
                                        return i + 1;
                                    }
                                }
                            }
                            if((json[i] == '+') || (json[i] == '-')) {
                                return i + 1;
                            }
                        }
                    }
                    nr[nrc] = 0;

                    if(val.value_type == EDJSON_VT_DOUBLE) {
                        val.value.floating = atof(nr);
                    }
                    else {
                        val.value.integer = atoll(nr);
                    }
                    i--;
                }

                if(jsonEvent) {
                    int rc = jsonEvent(path_mem, top + 1, val, private);
                    if(rc != 0) {
                        return EDJSON_SUCCESS;
                    }
                }
                state = edJSON_STATE_FIND_COMMA;
                break;
            
            case edJSON_STATE_PARSE_FINISHED:
                if(!strchr("\n\r\t /", json[i])) {
                    return i + 1;
                }
                else if(json[i] == '/') {
                    i++;
                    if(json[i] == '/') {
                        top++;
                        if(top < path_max_depth) {
                            path_mem[top]._prev = state;
                        }
                        else {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        
                        state = edJSON_STATE_IN_COMMENT;
                    }
                    else {
                        return i + 1;
                    }
                }
                break;
        }
        i++;
    }

    return EDJSON_SUCCESS;
}

int edJSON_string_unescape(char *dest, size_t dest_size, const char *source, size_t source_size) {
    if((dest == 0) || (source == 0) || (dest_size == 0)) {
        return EDJSON_ERR_NO_INPUT;
    }

    if(dest == source) {
        dest_size = source_size;
    }

    size_t di = 0;
    size_t si = 0;
    while(si < source_size) {
        if(di >= dest_size) {
            return EDJSON_ERR_NO_MEMORY;
        }

        if(source[si] == '\\') {
            si++;
            switch(source[si++]) {
                case '\\':
                case '"':
                case '/':
                    dest[di++] = source[si - 1];
                    break;

                case 'r':
                    dest[di++] = '\r';
                    break;

                case 'n':
                    dest[di++] = '\n';
                    break;

                case 't':
                    dest[di++] = '\t';
                    break;

                case 'b':
                    dest[di++] = '\b';
                    break;

                case 'f':
                    dest[di++] = '\f';
                    break;

                case 'u': {
                    uint32_t x = 0;
                    uint32_t unicode = 0;
                    uint8_t i = 0;
                    while((si < source_size) && (i < 4)) {
                        x *= 16;
                        if((source[si] >= '0') && (source[si] <= '9')) {
                            x += source[si] - '0';
                        }
                        else if((source[si] >= 'a') && (source[si] <= 'f')) {
                            x += source[si] - 'a' + 10;
                        }
                        else if((source[si] >= 'A') && (source[si] <= 'F')) {
                            x += source[si] - 'A' + 10;
                        }
                        else {
                            return EDJSON_ERR_BAD_STRING;
                        }
                        si++;
                        i++;
                    }

                    unicode = x;
                    if(unicode >= 0xD800) {
                        unicode -= 0xD800;
                        unicode *= 0x400;

                        //do another round
                        if(source_size - si < 2) {
                            return EDJSON_ERR_BAD_STRING;
                        }

                        if(source[si++] != '\\') {
                            return EDJSON_ERR_BAD_STRING;
                        }
                        if(source[si++] != 'u') {
                            return EDJSON_ERR_BAD_STRING;
                        }

                        x = 0;
                        i = 0;
                        while((si < source_size) && (i < 4)) {
                            x *= 16;
                            if((source[si] >= '0') && (source[si] <= '9')) {
                                x += source[si] - '0';
                            }
                            else if((source[si] >= 'a') && (source[si] <= 'f')) {
                                x += source[si] - 'a' + 10;
                            }
                            else if((source[si] >= 'A') && (source[si] <= 'F')) {
                                x += source[si] - 'A' + 10;
                            }
                            else {
                                return EDJSON_ERR_BAD_STRING;
                            }
                            si++;
                            i++;
                        }
                        if(x < 0xDC00) {
                            return EDJSON_ERR_BAD_STRING;
                        }

                        x -= 0xDC00;
                        unicode += x;
                        unicode += 0x10000;
                    }

                    //now convert unicode to UTF-8
                    if(unicode < 0x80) {
                        if(di >= dest_size) {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        dest[di++] = unicode & 0xFF;
                    }
                    else if(unicode < 0x800) {
                        if((di + 2) >= dest_size) {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        dest[di++] = 0xC0 + ((unicode & 0x3C0) >> 6);
                        if((uint8_t)dest[di - 1] < 0xC3) {
                            return EDJSON_ERR_BAD_STRING;
                        }

                        dest[di++] = 0x80 + (unicode & 0x3F);
                    }
                    else if(unicode < 0x10000) {
                        if((di + 3) >= dest_size) {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        dest[di++] = 0xE0 + ((unicode & 0xF000) >> 12);
                        dest[di++] = 0x80 + ((unicode & 0xFC0) >> 6);
                        dest[di++] = 0x80 + (unicode & 0x3F);
                    }
                    else if(unicode < 0x110000) {
                        if((di + 4) >= dest_size) {
                            return EDJSON_ERR_NO_MEMORY;
                        }
                        dest[di++] = 0xF0 + ((unicode & 0x1C0000) >> 18);
                        dest[di++] = 0x80 + ((unicode & 0x3F000) >> 12);
                        dest[di++] = 0x80 + ((unicode & 0xFC0) >> 6);
                        dest[di++] = 0x80 + (unicode & 0x3F);
                    }
                    else {
                        return EDJSON_ERR_BAD_STRING;
                    }
                    
                    
                } break;                

                default:
                    return EDJSON_ERR_BAD_STRING;
                    break;
            }
        }
        else {
            dest[di++] = source[si++];
        }
    }

    dest[di] = 0;
    return di;
}

int edJSON_build_path_string(char *dest, size_t dest_size, const edJSON_path_t *path, size_t path_size) {
    if((dest == 0) || (path == 0) || (dest_size == 0)) {
        return EDJSON_ERR_NO_INPUT;
    }

    size_t len = 0;
    for(size_t i = 0; i < path_size; i++) {
        if(path[i].index < 0) {
            dest[len++] = '/';

            int rc = edJSON_string_unescape(dest + len, dest_size - len, path[i].value, path[i].value_size);
            if(rc < 0) {
                return rc;
            }
            else {
                len += rc;
            }
        }
        else {
            int rc = snprintf(dest + len, dest_size - len, "[%d]", path[i].index);
            if(rc < 0) {
                return EDJSON_ERR_NO_MEMORY;
            }
            else {
                len += rc;
            }
        }

        if(len >= dest_size) {
            return EDJSON_ERR_NO_MEMORY;
        }
    }

    return len;
}
