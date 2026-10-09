/* The few libc functions the engine needs, for a freestanding WebAssembly build.
 * Accuracy is around 1e-7, far below anything audible. */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define TAU 6.28318530717958647692f
#define LN2 0.69314718055994530942f
#define LOG2E 1.44269504088896340736f

/* ------------------------------------------------------------------ memory */

void *memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* ------------------------------------------------------------------ math */

static float scale_by_pow2(float x, int k)
{
    union { uint32_t u; float f; } p;
    while (k > 127) { x *= 1.7014118e38f; k -= 127; }   /* 2^127 */
    while (k < -126) { x *= 1.1754944e-38f; k += 126; } /* 2^-126 */
    p.u = (uint32_t)(k + 127) << 23;
    return x * p.f;
}

float sinf(float x)
{
    x -= __builtin_rintf(x * (1.0f / TAU)) * TAU; /* [-pi, pi] */
    if (x > PI / 2) x = PI - x;                   /* [-pi/2, pi/2] */
    else if (x < -PI / 2) x = -PI - x;
    float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 +
               x2 * (1.0f / 362880 - x2 * (1.0f / 39916800))))));
}

float cosf(float x) { return sinf(x + PI / 2); }

float expf(float x)
{
    if (x < -103.0f) return 0.0f;
    if (x > 88.7f) return __builtin_inff();
    float k = __builtin_rintf(x * LOG2E);
    float r = x - k * 0.693145752f - k * 1.42860677e-6f; /* x - k*ln2, split for accuracy */
    float p = 1.0f + r * (1.0f + r * (0.5f + r * (1.0f / 6 + r * (1.0f / 24 + r * (1.0f / 120 +
              r * (1.0f / 720 + r * (1.0f / 5040)))))));
    return scale_by_pow2(p, (int)k);
}

float logf(float x)
{
    if (x <= 0.0f) return -__builtin_inff();
    union { float f; uint32_t u; } v = {x};
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x007fffff) | 0x3f800000; /* mantissa in [1, 2) */
    float m = v.f;
    if (m > 1.41421356f) { m *= 0.5f; e++; } /* [sqrt(1/2), sqrt(2)) */
    float s = (m - 1.0f) / (m + 1.0f), s2 = s * s;
    float lm = 2.0f * s * (1.0f + s2 * (1.0f / 3 + s2 * (1.0f / 5 + s2 * (1.0f / 7 + s2 * (1.0f / 9)))));
    return lm + (float)e * LN2;
}

float powf(float x, float y) { return expf(y * logf(x)); } /* x > 0 only: enough for mtof */

float tanhf(float x)
{
    if (x > 9.0f) return 1.0f;
    if (x < -9.0f) return -1.0f;
    float e = expf(2.0f * x);
    return (e - 1.0f) / (e + 1.0f);
}

double fmod(double x, double y) { return x - __builtin_trunc(x / y) * y; }

/* ------------------------------------------------------------------ snprintf */

typedef struct {
    char *buf;
    size_t n, len;
} Out;

static void put(Out *o, char c)
{
    if (o->len + 1 < o->n) o->buf[o->len] = c;
    o->len++;
}

static void put_uint(Out *o, unsigned long v)
{
    char tmp[24];
    int i = 0;
    do tmp[i++] = (char)('0' + v % 10); while ((v /= 10) > 0);
    while (i > 0) put(o, tmp[--i]);
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    Out o = {buf, n, 0};
    va_list ap;
    va_start(ap, fmt);
    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { put(&o, *f); continue; }
        int prec = 6;
        if (f[1] == '.') {
            prec = 0;
            for (f += 2; *f >= '0' && *f <= '9'; f++) prec = prec * 10 + (*f - '0');
            f--;
        }
        switch (*++f) {
        case 's':
            for (const char *s = va_arg(ap, const char *); *s; s++) put(&o, *s);
            break;
        case 'c':
            put(&o, (char)va_arg(ap, int));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) put(&o, '-');
            put_uint(&o, v < 0 ? -(unsigned long)v : (unsigned long)v);
            break;
        }
        case 'f': {
            double v = va_arg(ap, double), scale = 1;
            if (v < 0) { put(&o, '-'); v = -v; }
            for (int i = 0; i < prec; i++) scale *= 10;
            unsigned long all = (unsigned long)(v * scale + 0.5);
            put_uint(&o, (unsigned long)(all / scale));
            if (prec > 0) {
                put(&o, '.');
                unsigned long frac = all % (unsigned long)scale;
                for (unsigned long d = (unsigned long)scale / 10; d > 0; d /= 10) put(&o, (char)('0' + frac / d % 10));
            }
            break;
        }
        case '%':
            put(&o, '%');
            break;
        case '\0':
            f--;
            break;
        }
    }
    va_end(ap);
    if (n > 0) buf[o.len < n ? o.len : n - 1] = '\0';
    return (int)o.len;
}
