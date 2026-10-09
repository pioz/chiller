#include "app.h"
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

static volatile sig_atomic_t quit_requested;

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
    int full = (int)lroundf(fminf(fmaxf(v, 0.0f), 1.0f) * (float)width);
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
    app_poll();

    char out[8192], b[256];
    size_t o = 0;
#define P(...) (o += (size_t)snprintf(out + o, sizeof out - o, __VA_ARGS__))

    P("\033[H\n");
    P("   " C_TITLE "~  c h i l l e r  ~" C_RESET "\033[K\n\n");
    P("   scene " C_ACCENT "[%d] %-18s" C_RESET " key " C_ACCENT "%-16s" C_RESET " chord " C_ACCENT "%s" C_RESET "\033[K\n",
      app_scene() + 1, app_scene_name(app_scene()), app_key_name(), app_chord());
    P("   " C_DIM "────────────────────────────────────────────────────────────────────" C_RESET "\033[K\n");

    for (int l = 0; l < L_COUNT; l++) {
        bar(b, sizeof b, app_level(l), 20);
        P("   %s%c" C_RESET "  %s%-14s %s  %s" C_RESET "\033[K\n", C_ACCENT, app_layer_key(l),
          app_layer_on(l) ? C_ON : C_DIM, app_layer_name(l), b, app_layer_detail(l));
    }

    P("\033[K\n");
    bar(b, sizeof b, app_volume(), 10);
    P("   volume      " C_ON "%s" C_RESET, b);
    bar(b, sizeof b, app_brightness(), 10);
    P("     brightness  " C_ON "%s" C_RESET "\033[K\n\n", b);

    /* Breathing guide: 5 s inhale, 5 s exhale, in phase with the waves */
    double ph = app_breath_phase();
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
            if (a == 'A') app_change_volume(0.05f);
            if (a == 'B') app_change_volume(-0.05f);
            if (a == 'C') app_change_brightness(0.1f);
            if (a == 'D') app_change_brightness(-0.1f);
            continue;
        }
        if (c >= '1' && c < '1' + APP_SCENES) {
            app_apply_scene(c - '1');
            continue;
        }
        for (int l = 0; l < L_COUNT; l++)
            if (c == app_layer_key(l)) app_press(l);
        switch (c) {
        case 'q': case 'Q': quit_requested = 1; break;
        case ' ': app_new_variation(); break;
        case '+': case '=': app_change_volume(0.05f); break;
        case '-': case '_': app_change_volume(-0.05f); break;
        case ']': app_change_brightness(0.1f); break;
        case '[': app_change_brightness(-0.1f); break;
        }
    }
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
            argv0, APP_SCENES, argv0, APP_SCENES);
}

int main(int argc, char **argv)
{
    uint32_t seed = (uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16);

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
    if (start_scene < 0 || start_scene >= APP_SCENES) return usage(argv[0]), 2;

    app_init(seed, start_scene);

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
    app_set_muted(1);
    for (int i = 0; i < 30; i++) {
        draw("see you soon…");
        usleep(50000);
    }
    audio_stop();
    term_restore();
    return 0;
}
