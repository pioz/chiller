/* Minimal string.h for the WebAssembly build. See libc.c. */
#ifndef CHILLER_WASM_STRING_H
#define CHILLER_WASM_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

#endif
