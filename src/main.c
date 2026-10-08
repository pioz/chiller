#include "audio.h"
#include "synth.h"

#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ scenes */

typedef struct {
    float hz;
    const char *name;
} Band;

static const Band BANDS[] = {
    {2.5f, "delta"}, /* deep sleep */
    {6.0f, "theta"}, /* meditation, drowsiness */
    {10.0f, "alpha"}, /* relaxed wakefulness */
};
#define N_BANDS 3

typedef struct {
    const char *name;
    float level[L_COUNT]; /* pad, binaural, noise, ocean, rain, chimes */
    int band;
    int noise;
    float brightness;
} Scene;

static const Scene SCENES[] = {
    {"Deep sleep", {0.70f, 0.50f, 0.60f, 0.60f, 0.00f, 0.00f}, 0, NOISE_BROWN, 0.12f},
    {"Meditation", {0.80f, 0.50f, 0.20f, 0.30f, 0.00f, 0.60f}, 1, NOISE_PINK, 0.35f},
    {"Relax", {0.60f, 0.40f, 0.35f, 0.00f, 0.50f, 0.30f}, 2, NOISE_PINK, 0.50f},
    {"Ocean", {0.35f, 0.30f, 0.15f, 1.00f, 0.00f, 0.25f}, 2, NOISE_BROWN, 0.30f},
    {"Night rain", {0.50f, 0.30f, 0.25f, 0.00f, 1.00f, 0.00f}, 1, NOISE_PINK, 0.25f},
};
#define N_SCENES ((int)(sizeof SCENES / sizeof SCENES[0]))

static const struct {
    char key;
    const char *name;
} LAYERS[L_COUNT] = {
    {'p', "harmonic pad"}, {'b', "binaural"}, {'n', "noise"},
    {'o', "ocean"},        {'r', "rain"},     {'c', "chimes"},
};

/* ------------------------------------------------------------------ app state */

static SynthParams params;
static Variation variation;
static float remembered[L_COUNT];
static int band;
static int scene;
static uint32_t rng;
static volatile sig_atomic_t quit_requested;

static void apply_scene(int i)
{
    const Scene *s = &SCENES[i];
    scene = i;
    for (int l = 0; l < L_COUNT; l++) {
        params.level[l] = s->level[l];
        remembered[l] = s->level[l] > 0.01f ? s->level[l] : 0.5f;
    }
    band = s->band;
    params.beat_hz = BANDS[band].hz;
    params.noise_color = s->noise;
    params.brightness = s->brightness;
}

static void toggle_layer(int l)
{
    if (params.level[l] > 0.01f) {
        remembered[l] = params.level[l];
        params.level[l] = 0.0f;
    } else {
        params.level[l] = remembered[l];
    }
}

/* off -> delta -> theta -> alpha -> off */
static void cycle_binaural(void)
{
    if (params.level[L_BINAURAL] <= 0.01f) {
        band = 0;
        params.level[L_BINAURAL] = remembered[L_BINAURAL];
    } else if (band < N_BANDS - 1) {
        band++;
    } else {
        toggle_layer(L_BINAURAL);
    }
    params.beat_hz = BANDS[band].hz;
}

/* off -> pink -> brown -> white -> off */
static void cycle_noise(void)
{
    if (params.level[L_NOISE] <= 0.01f) {
        params.noise_color = NOISE_PINK;
        params.level[L_NOISE] = remembered[L_NOISE];
    } else if (params.noise_color != NOISE_WHITE) {
        params.noise_color++;
    } else {
        toggle_layer(L_NOISE);
    }
}

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

static void new_variation(void)
{
    variation = synth_random_variation(&rng);
    synth_set_variation(&variation);
}

/* ------------------------------------------------------------------ terminal */

static struct termios orig_termios;
static int raw_mode;

static void term_restore(void)
{
    if (!raw_mode) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    raw_mode = 0;
}

static void term_raw(void)
{
    if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &orig_termios) != 0) return;
    struct termios t = orig_termios;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
    raw_mode = 1;
    atexit(term_restore);
    fputs("\033[?1049h\033[?25l\033[2J", stdout);
}

static void on_signal(int sig)
{
    (void)sig;
    quit_requested = 1;
}

/* ------------------------------------------------------------------ drawing */

#define C_RESET "\033[0m"
#define C_DIM "\033[2m"
#define C_TITLE "\033[38;5;153m"
#define C_ON "\033[38;5;117m"
#define C_ACCENT "\033[38;5;183m"
#define C_BREATH "\033[38;5;152m"

static void bar(char *dst, size_t n, float v, int width)
{
    int full = (int)lroundf(clampf(v, 0.0f, 1.0f) * (float)width);
    size_t o = 0;
    for (int i = 0; i < width && o + 4 < n; i++) {
        const char *g = i < full ? "█" : "░";
        size_t l = strlen(g);
        memcpy(dst + o, g, l);
        o += l;
    }
    dst[o] = '\0';
}

static void draw(const char *farewell)
{
    SynthStatus st;
    synth_get_status(&st);

    char out[8192], b[256], key[64];
    size_t o = 0;
#define P(...) (o += (size_t)snprintf(out + o, sizeof out - o, __VA_ARGS__))

    synth_variation_name(&variation, key, sizeof key);

    P("\033[H\n");
    P("   " C_TITLE "~  c h i l l e r  ~" C_RESET "\033[K\n\n");
    P("   scene " C_ACCENT "[%d] %-18s" C_RESET " key " C_ACCENT "%-16s" C_RESET " chord " C_ACCENT "%s" C_RESET "\033[K\n",
      scene + 1, SCENES[scene].name, key, st.chord);
    P("   " C_DIM "────────────────────────────────────────────────────────────────────" C_RESET "\033[K\n");

    for (int l = 0; l < L_COUNT; l++) {
        int on = params.level[l] > 0.01f;
        bar(b, sizeof b, st.level[l], 20);
        const char *extra = "";
        char tmp[64];
        if (l == L_BINAURAL && on) {
            snprintf(tmp, sizeof tmp, "%s %.1f Hz", BANDS[band].name, BANDS[band].hz);
            extra = tmp;
        } else if (l == L_NOISE && on) {
            static const char *COLORS[NOISE_COLORS] = {"pink", "brown", "white"};
            extra = COLORS[params.noise_color];
        }
        P("   %s%c" C_RESET "  %s%-14s %s  %s" C_RESET "\033[K\n", C_ACCENT, LAYERS[l].key, on ? C_ON : C_DIM,
          LAYERS[l].name, b, extra);
    }

    P("\033[K\n");
    bar(b, sizeof b, params.master, 10);
    P("   volume      " C_ON "%s" C_RESET, b);
    bar(b, sizeof b, params.brightness, 10);
    P("     brightness  " C_ON "%s" C_RESET "\033[K\n\n", b);

    /* Breathing guide: 5 s inhale, 5 s exhale, in phase with the waves */
    double cycle = synth_breath_seconds();
    double ph = fmod(st.seconds, cycle) / cycle;
    float env = 0.5f - 0.5f * cosf(6.2831853f * (float)ph);
    int half = 16, fill = (int)lroundf(env * (float)half);
    P("   " C_DIM "breath 6/min " C_RESET "    " C_BREATH);
    for (int i = -half; i <= half; i++) P("%s", abs(i) <= fill ? "●" : "·");
    P(C_RESET "   %s\033[K\n\n", ph < 0.5 ? "inhale…" : "exhale…");

    if (farewell) {
        P("   " C_TITLE "%s" C_RESET "\033[K\n", farewell);
    } else {
        P("   " C_DIM "1-5" C_RESET " scenes   " C_DIM "space" C_RESET " new variation   " C_DIM "p b n o r c" C_RESET
          " layers\033[K\n");
        P("   " C_DIM "+ -" C_RESET " volume   " C_DIM "[ ]" C_RESET " brightness   " C_DIM "q" C_RESET " quit\033[K\n\n");
        P("   " C_DIM "binaural beats: headphones needed (on speakers the two tones blend together)" C_RESET "\033[K\n");
    }
    P("\033[J");
#undef P
    fwrite(out, 1, o, stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ input */

static void handle_keys(const char *buf, ssize_t n)
{
    for (ssize_t i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\033' && i + 2 < n && buf[i + 1] == '[') { /* arrow keys */
            char a = buf[i + 2];
            i += 2;
            if (a == 'A') params.master = clampf(params.master + 0.05f, 0, 1);
            if (a == 'B') params.master = clampf(params.master - 0.05f, 0, 1);
            if (a == 'C') params.brightness = clampf(params.brightness + 0.1f, 0, 1);
            if (a == 'D') params.brightness = clampf(params.brightness - 0.1f, 0, 1);
            continue;
        }
        if (c >= '1' && c < '1' + N_SCENES) {
            apply_scene(c - '1');
            continue;
        }
        switch (c) {
        case 'q': case 'Q': quit_requested = 1; break;
        case ' ': new_variation(); break;
        case 'p': toggle_layer(L_PAD); break;
        case 'b': cycle_binaural(); break;
        case 'n': cycle_noise(); break;
        case 'o': toggle_layer(L_OCEAN); break;
        case 'r': toggle_layer(L_RAIN); break;
        case 'c': toggle_layer(L_CHIMES); break;
        case '+': case '=': params.master = clampf(params.master + 0.05f, 0, 1); break;
        case '-': case '_': params.master = clampf(params.master - 0.05f, 0, 1); break;
        case ']': params.brightness = clampf(params.brightness + 0.1f, 0, 1); break;
        case '[': params.brightness = clampf(params.brightness - 0.1f, 0, 1); break;
        }
    }
    synth_set_params(&params);
}

/* ------------------------------------------------------------------ render to WAV */

static void put_u32(FILE *f, uint32_t v) { fwrite((uint8_t[]){v, v >> 8, v >> 16, v >> 24}, 1, 4, f); }
static void put_u16(FILE *f, uint16_t v) { fwrite((uint8_t[]){v, v >> 8}, 1, 2, f); }

static int render_wav(const char *path, double seconds)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return 1;
    }
    uint32_t frames = (uint32_t)(seconds * SAMPLE_RATE), bytes = frames * 4;
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, 2);
    put_u32(f, SAMPLE_RATE);
    put_u32(f, SAMPLE_RATE * 4);
    put_u16(f, 4);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, bytes);

    float buf[2048];
    double peak = 0, sum = 0;
    int bad = 0;
    for (uint32_t done = 0; done < frames;) {
        int n = frames - done < 1024 ? (int)(frames - done) : 1024;
        synth_render(buf, n);
        for (int i = 0; i < 2 * n; i++) {
            float x = buf[i];
            if (!isfinite(x)) bad++, x = 0;
            peak = fmax(peak, fabs(x));
            sum += (double)x * x;
            put_u16(f, (uint16_t)(int16_t)lrintf(x * 32767.0f));
        }
        done += (uint32_t)n;
    }
    fclose(f);
    fprintf(stderr, "%s: %.0f s, peak %.3f, RMS %.3f (%.1f dBFS)%s\n", path, seconds, peak,
            sqrt(sum / (2.0 * frames)), 20 * log10(sqrt(sum / (2.0 * frames)) + 1e-12),
            bad ? ", INVALID SAMPLES!" : "");
    return bad ? 1 : 0;
}

/* ------------------------------------------------------------------ main */

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [scene 1-%d]\n"
            "       %s --render file.wav seconds [scene 1-%d]\n",
            argv0, N_SCENES, argv0, N_SCENES);
}

int main(int argc, char **argv)
{
    rng = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16);
    if (!rng) rng = 1;

    int start_scene = 1, render = 0;
    const char *wav = NULL;
    double seconds = 0;

    if (argc >= 2 && !strcmp(argv[1], "--render")) {
        if (argc < 4) return usage(argv[0]), 2;
        render = 1;
        wav = argv[2];
        seconds = atof(argv[3]);
        if (argc >= 5) start_scene = atoi(argv[4]) - 1;
    } else if (argc >= 2) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) return usage(argv[0]), 0;
        start_scene = atoi(argv[1]) - 1;
    }
    if (start_scene < 0 || start_scene >= N_SCENES) return usage(argv[0]), 2;

    synth_init(rng);
    new_variation();
    params.master = 0.7f;
    apply_scene(start_scene);
    synth_set_params(&params);

    if (render) return render_wav(wav, seconds);

    if (audio_start() != 0) {
        fprintf(stderr, "cannot open the audio output\n");
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    term_raw();

    int stdin_open = 1;
    while (!quit_requested) {
        draw(NULL);
        struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
        if (stdin_open && poll(&pfd, 1, 50) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
            char buf[64];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n > 0) handle_keys(buf, n);
            else stdin_open = 0; /* stdin closed: keep playing until Ctrl-C */
        } else if (!stdin_open) {
            usleep(50000);
        }
    }

    /* fade out on exit */
    params.master = 0.0f;
    synth_set_params(&params);
    for (int i = 0; i < 30; i++) {
        draw("see you soon…");
        usleep(50000);
    }
    audio_stop();
    term_restore();
    return 0;
}
