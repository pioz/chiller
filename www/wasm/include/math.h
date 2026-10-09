/* Minimal math.h for the WebAssembly build: only what src/ uses. See libc.c. */
#ifndef CHILLER_WASM_MATH_H
#define CHILLER_WASM_MATH_H

float sinf(float x);
float cosf(float x);
float expf(float x);
float logf(float x);
float powf(float x, float y);
float tanhf(float x);
double fmod(double x, double y);

static inline float sqrtf(float x) { return __builtin_sqrtf(x); } /* native f32.sqrt */

#endif
