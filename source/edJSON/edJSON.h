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

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief edJSON Version 
 * 
 * Current version of edJSON library
 * */
#define EDJSON_VERSION      "1.1.1"

/**
 * @brief Path array element.
 * 
 * Structure for representing an element in the JSON path.
 * */
typedef struct edJSON_path_s {
    char _prev;             /**< internal data, do not use and do not modify. */

    int index;              /**< -1 if value is non-array, otherwise current array index. */
    const char *value;      /**< Path value. Exists only when index < 0. NOT null-terminated. */
    size_t value_size;      /**< Value size. Exists only when index < 0. */
} edJSON_path_t;

/**
 * @brief JSON value structure.
 * 
 * Structure for representing a JSON-stored value.
 * */
typedef struct edJOSN_value_s {
    enum {
        EDJSON_VT_INTEGER,      /**< Returned value is an integer. */
        EDJSON_VT_STRING,       /**< Returned value is a string. */
        EDJSON_VT_DOUBLE,       /**< Returned value is a double. */
        EDJSON_VT_BOOL,         /**< Returned value is a boolean. */
        EDJSON_VT_NULL,         /**< Returned value is null. */
    } value_type;               /**< Returned value type. */

    union {
        int integer;            /**< Union member to read if value_type is integer. */
        struct {
            const char *value;  /**< String value. NOT null-terminated */
            size_t value_size;  /**< String value size. */
        } string;               /**< Union member to read if value_type is string. */
        double floating;        /**< Union member to read if value_type is double. */
        bool boolean;           /**< Union member to read if value_type is boolean. */
    } value;                    /**< Returned value. To see which type it is, check value_type. */
} edJSON_value_t;

/**
 * @brief JSON Event Callback.
 * 
 * A callback with this prototype is called whenever a value is found by the JSON parser.
 * Function MUST NOT call edJSON_parse() function for the same content.
 * Callback can return a non-zero value to stop the parser with success, for example when a specific value was found and there's no need to further look into the content.
 * 
 * @param path Found path array. See edJSON_path_t for detailed description.
 * @param path_size Found path array size.
 * @param value Found value. See edJSON_value_t for detailed description.
 * @param private Private data passed to _parse() function.
 * @return User must return either 0, for the parser to continue parsing, or a non-null value to stop parsing immediately and return with EDJSON_SUCCESS.
 * */
typedef int(*edJSON_cb_t)(const edJSON_path_t *path, size_t path_size, edJSON_value_t value, void *private);

/**
 * @brief edJSON_* return codes.
 * 
 * Common return codes for all edJSON functions. Check individual functions for details.
 * */
#define EDJSON_SUCCESS              (0)     /**< Function returned success. */
#define EDJSON_ERR_NO_MEMORY        (-1)    /**< Insufficient memory. */
#define EDJSON_ERR_NO_INPUT         (-2)    /**< Bad input variable. */
#define EDJSON_ERR_BAD_STRING       (-3)    /**< Bad string. Only returned by edJSON_string_unescape() */

/**
 * @brief Prase JSON content
 * 
 * Function parses JSON content and calls jsonEvent callback when value is found.
 * Function is thread-safe as long as the content isn't volatile.
 * 
 * @param json Pointer to the JSON content. Content is const, function does not change it.
 * @param path_mem Pointer to a memory location where current path is build. This should be allocated by the caller.
 * @param path_max_depth Maximum path depth. Represents the maximum number of elements that path_mem can store.
 * @param jsonEvent Pointer to the callback function which is called when a value is found. Can be NULL when user just wants to validate JSON content.
 * @param private Private user data; Opaque to edJSON, everything on this falls into user's responsibility
 * @return Return code of the function (see defines above); 0 for success, negative for internal error, or positive for content error (return code represents index of where error occured).
 * */
int edJSON_parse(const char *json, edJSON_path_t *path_mem, size_t path_max_depth, edJSON_cb_t jsonEvent, void *private);

/**
 * @brief Utility functions.
 * */

/**
 * @brief Unescape a string returned through the JSON event callback. Can be either a path element or a string value.
 * 
 * Function unescapes a JSON path or value returned string.
 * All unescaped unicodes are converted to UTF-8.
 * It replaces all escaped characters and adds a null-termination on the string.
 * If dest is the same as source, unescaping is done in-place. This is an optimization for doing on the same memory as JSON (json will be trashed in the end).
 * If function fails dest is changed anyhow.
 * 
 * @param dest Pointer to the destination string. In the end, a null-terminated string. If this is the same as source, dest_size is ignored and unescaping will be done in-place.
 * @param dest_size Size of the memory pointed by the dest pointer. If conversion will exceed this, error is returned.
 * @param source Pointer to the source string, as returned via the edJSON callback.
 * @param source_size Source string size, as returned via the edJSON callback.
 * @return When negative, error code of the function (see defines above) and when positive the number bytes destination has.
 * */
int edJSON_string_unescape(char *dest, size_t dest_size, const char *source, size_t source_size);

/**
 * @brief Builds a path string from the path vector returned through the JSON event callback.
 * 
 * Function builds a path string from the path vector.
 * Function uses edJSON_string_unescape to build the string.
 * If function fails dest is changed anyhow.
 * 
 * @param dest Pointer to the destination string. In the end, a null-terminated string. If this is the same as source, dest_size is ignored and unescaping will be done in-place.
 * @param dest_size Size of the memory pointed by the dest pointer. If conversion will exceed this, error is returned.
 * @param path Pointer to the path array, as returned via the edJSON callback.
 * @param path_size path array size, as returned via the edJSON callback.
 * @return When negative, error code of the function (see defines above) and when positive the number bytes destination has.
 * */
int edJSON_build_path_string(char *dest, size_t dest_size, const edJSON_path_t *path, size_t path_size);
