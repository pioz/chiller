/* Minimal stdio.h for the WebAssembly build: snprintf with %s, %c, %d and %.Nf. See libc.c. */
#ifndef CHILLER_WASM_STDIO_H
#define CHILLER_WASM_STDIO_H

#include <stddef.h>

int snprintf(char *buf, size_t n, const char *fmt, ...);

#endif
