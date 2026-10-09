/* The browser runs the whole engine on the audio thread, so the synth's mutex is a no-op. */
#ifndef CHILLER_WASM_PTHREAD_H
#define CHILLER_WASM_PTHREAD_H

typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0

static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_trylock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }

#endif
