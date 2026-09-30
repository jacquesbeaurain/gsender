#pragma once

/* A small freestanding runtime for C plugins built without a libc
 * (clang --target=wasm32 -nostdlib): memory and string functions, a heap,
 * and helpers to build and read JSON. Compile src/runtime.c with the plugin. */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t n);

void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* ptr, size_t size);
void free(void* ptr);

/* A growable string on the heap. */
typedef struct {
    char* data;
    int32_t len;
    int32_t cap;
} gs_str_t;

void gs_str_init(gs_str_t* s);
void gs_str_free(gs_str_t* s);
void gs_str_append(gs_str_t* s, const char* text);
void gs_str_append_int(gs_str_t* s, int64_t value);
/** Appends a number with a fixed number of decimals (0..9), as G-code wants it. */
void gs_str_append_fixed(gs_str_t* s, double value, int decimals);
/** Appends a JSON string literal, quotes and escapes included. */
void gs_str_append_json_string(gs_str_t* s, const char* text);

/** Copies a response into the host's buffer: its length, or -1 if it does not fit. */
int32_t gs_respond(char* response_buf, int32_t buf_len, const char* json);

/* Reading flat JSON objects (as the host sends them): the value of "key" at the top level. */
bool gs_json_number(const char* json, const char* key, double* out);
bool gs_json_bool(const char* json, const char* key, bool* out);
/** Copies the string value into out (always terminated); false when missing or not a string. */
bool gs_json_string(const char* json, const char* key, char* out, int32_t out_len);

#ifdef __cplusplus
}
#endif
