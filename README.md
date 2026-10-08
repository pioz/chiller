# chiller

A relaxing sound generator for the terminal, written in C.

chiller synthesizes everything in real time. It uses no audio files, so the sound never loops
and never repeats exactly. It runs on macOS and Linux.

```
   ~  c h i l l e r  ~

   scene [2] Meditation         key E minor          chord Em7
   ────────────────────────────────────────────────────────────────────
   p  harmonic pad   ███████████░░░░░░░░░
   b  binaural       ███████░░░░░░░░░░░░░  theta 6.0 Hz
   n  noise          ████░░░░░░░░░░░░░░░░  pink
   o  ocean          ████░░░░░░░░░░░░░░░░
   r  rain           ░░░░░░░░░░░░░░░░░░░░
   c  chimes         ████████░░░░░░░░░░░░

   volume      ███████░░░     brightness  ████░░░░░░

   breath 6/min     ·····●●●●●●●●●●●●●●●●●●●●●●●·····   inhale…

   1-5 scenes   space new variation   p b n o r c layers
   + - volume   [ ] brightness   q quit

   binaural beats: headphones needed (on speakers the two tones blend together)
```

## Getting started

Requirements:

- **macOS**: just the Xcode command line tools (`xcode-select --install`). Audio goes through
  CoreAudio, which is part of the system.
- **Linux**: a C compiler, `make` and the ALSA development headers. Audio goes through ALSA,
  which PulseAudio and PipeWire also emulate, so it works on modern desktops too.
  - Debian/Ubuntu: `sudo apt install build-essential libasound2-dev`
  - Fedora: `sudo dnf install gcc make alsa-lib-devel`
  - Arch: `sudo pacman -S base-devel alsa-lib`

The Makefile detects the platform and picks the right audio backend.

```sh
make
./chiller        # starts with scene 2 (Meditation)
./chiller 1      # starts with scene 1 (Deep sleep)
```

At startup the sound fades in slowly over a few seconds. Press `q` (or Ctrl-C) to quit: the
sound fades out before the program exits.

## The screen

From top to bottom:

- **Header**: the active scene, the current key and mode (e.g. `E minor`), and the chord
  playing right now (e.g. `Em7`).
- **Layers**: the six sound layers. Each row shows the key that toggles it and a bar with its
  **actual** level. Every change is a slow fade, so you can watch the bar rise and fall. A
  greyed-out row means the layer is off.
- **Volume and brightness**: the master volume and the brightness of the pad.
- **Breathing guide**: see [The breathing guide](#the-breathing-guide).
- **Command reminder** at the bottom.

## Controls

| key                 | action                                                             |
|---------------------|--------------------------------------------------------------------|
| `1`–`5`             | choose a scene                                                     |
| `space`             | new musical variation                                              |
| `p`                 | harmonic pad on/off                                                |
| `b`                 | binaural beats: off → delta → theta → alpha → off                  |
| `n`                 | noise: off → pink → brown → white → off                            |
| `o`                 | ocean waves on/off                                                 |
| `r`                 | rain on/off                                                        |
| `c`                 | chimes on/off                                                      |
| `+` `-` / `↑` `↓`   | volume                                                             |
| `[` `]` / `←` `→`   | brightness                                                         |
| `q`                 | quit, with a fade-out                                              |

Every change happens gradually, over one to a few seconds, so there are never abrupt jumps.

## Scenes

A scene is a ready-made, balanced mix of layers. Switching scene crossfades smoothly from one
to the other.

| # | scene      | what you hear                                                       |
|---|------------|---------------------------------------------------------------------|
| 1 | Deep sleep | dark pad, brown noise, slow waves, delta binaural beats (2.5 Hz)    |
| 2 | Meditation | pad up front, sparse chimes, a little ocean, theta beats (6 Hz)     |
| 3 | Relax      | light rain, chimes, pink noise, alpha beats (10 Hz), brighter pad   |
| 4 | Ocean      | waves up front, soft pad, alpha beats                               |
| 5 | Night rain | rain up front, soft pad, pink noise, theta beats                    |

After choosing a scene you can still toggle any layer on or off. When a layer comes back on,
it returns to the level it had before you switched it off.

## Layers

- **Harmonic pad (`p`)**: slow, warm chords. Each chord lasts 10–16 seconds and blends into
  the next. Every note is three slightly detuned oscillators, which gives a gentle chorus.
  The chords are always major 7th, minor 7th or dominant 7th. Each mode's diminished
  (tense) chord is never used.
- **Binaural beats (`b`)**: two pure tones at slightly different frequencies, one per ear,
  for example 180 Hz on the left and 186 Hz on the right. No real sound pulses at 6 Hz. The
  brain perceives the difference as a slow pulsation. Each press moves to the next band:
  - **delta**, 2.5 Hz: deep sleep
  - **theta**, 6 Hz: meditation, drowsiness
  - **alpha**, 10 Hz: relaxed wakefulness
  - then off.
- **Noise (`n`)**: each press changes the color.
  - **Pink** is soft and balanced.
  - **Brown** is deeper, like a distant river.
  - **White** is brighter and better at covering external sounds. It is harsher, so it is
    played a little quieter.
  - Then the noise switches off.
  The two stereo channels are independent, which makes the noise sound wide rather than
  coming from the center.
- **Ocean (`o`)**: a wave every 10 seconds, in sync with the breathing guide. Each wave has a
  slightly different strength, and it reaches one ear a moment before the other.
- **Rain (`r`)**: a soft, filtered hiss that comes and goes in slow gusts, plus drops
  falling here and there.
- **Chimes (`c`)**: occasional bell-like notes (about one every 3–4 seconds), always taken
  from the pentatonic scale of the current key, so they never clash with the pad.

Pad, chimes and rain drops share a reverb, which gives the sense of an open space.

### Binaural beats need headphones

Binaural beats only work when each ear hears **only** its own tone. Speakers are stereo too,
but both ears hear both speakers, and on a laptop the speakers are only a few centimeters
apart. The two tones mix in the air before reaching you. The result is an acoustic
("monaural") beat, a sound that really pulses, the same in both ears. It is still a gentle
pulsation, but it is no longer a binaural beat. Small speakers also reproduce the low carrier
(140–220 Hz) poorly. Everything else works the same on speakers. If you prefer, switch the
beats off with `b`.

## New variation (`space`)

Space picks a new random variation of the music, without touching the atmosphere. The scene,
the active layers, volume, brightness, binaural band and noise color stay as they are.

What changes:

- **Key**: a tonic between C and G, in a low octave.
- **Mode**, which sets the character:
  - major: serene
  - lydian: floating, dreamy
  - dorian: melancholic but soft
  - minor: more introspective
  - mixolydian: warm, laid-back
- **Chord progression**: one of four per mode, all built to avoid tense chords.
- **Chord length**: between 10 and 16 seconds.
- **Binaural carrier**: the base tone (140–220 Hz). The beat frequency itself does not change,
  so theta stays theta.

How the change sounds:

- The pad restarts from the first chord of the new progression. The old chord fades out while
  the new one fades in, with no break.
- The chimes switch to the notes of the new key immediately.
- The binaural tone glides slowly to its new pitch.

The choice is random, so now and then a key similar to the previous one comes up. Keep pressing
space until you find the atmosphere you like.

## Brightness (`[` `]`)

Brightness acts **only on the harmonic pad**. It controls a low-pass filter, which lets low
frequencies through and cuts those above a threshold.

- **Low**: most harmonics are cut. The pad becomes dark and muffled, as if heard from another
  room. This is the best setting for sleep: the Deep sleep scene starts at 12%.
- **High**: more harmonics come through. The sound is more present and open, and the slight
  shimmer from the detuned voices becomes audible. The Relax scene starts at 50%.

The cutoff is `250 + 3500 × brightness²` Hz, from about 250 to 3750 Hz. The value is squared,
so the steps are finer in the dark range, where differences are most audible. A very slow
oscillator (about one cycle every 33 seconds) moves the cutoff by ±15%, so the pad "breathes"
even when you don't touch anything. A brightness change takes about a second and a half.

Noise, ocean, rain, chimes and binaural beats are not affected.

## The breathing guide

The breathing row follows a 10-second cycle: 5 seconds to inhale (the dots expand) and 5 to
exhale (they contract). That is 6 breaths per minute. In heart rate variability (HRV) research,
this is the pace that most calms the nervous system. The ocean waves follow the same cycle: the
wave rises as you inhale and withdraws as you exhale. Try breathing along with the bar.

## Why it should be relaxing

The design choices are based on research. The evidence ranges from moderate to weak and varies
a lot from person to person: treat it as inspiration, not therapy.

- **0.1 Hz rhythm (6 breaths per minute).** Waves and the breathing guide follow a 10-second
  cycle. Breathing at about 6 cycles per minute maximizes heart rate variability and vagal tone
  ("resonance breathing", Lehrer & Gevirtz 2014).
- **Slow, tension-free harmony.** Chords last 10–16 s and are only major or minor 7ths.
  Diminished chords are excluded and there is no beat. Slow tempos and little harmonic surprise
  are associated with lower heart rate and blood pressure (Bernardi et al. 2006).
- **Pink, brown and white noise.** They mask external sounds, and some studies find that they
  stabilize sleep (Zhou et al. 2012; Papalambros et al. 2017). Pink and brown have spectra
  that roll off toward the highs and sound softer. White covers high-pitched sounds better
  but is harsher, which is why it is played a little quieter.
- **Natural sounds.** Listening to waves and rain shifts activity toward the parasympathetic
  nervous system compared to artificial sounds (Gould van Praag et al. 2017).
- **Binaural beats.** A meta-analysis (Garcia-Argibay et al. 2019) finds a small effect on
  anxiety, while other studies are negative. For this reason they are kept at a low volume.

## Rendering to a WAV file

```sh
./chiller --render out.wav 60 3    # 60 seconds of scene 3, without playing it
```

The sound is written to a 16-bit stereo WAV file at 44.1 kHz. The command also prints peak and
RMS levels, which is useful to check the mix.

## How it works

- `src/synth.c`: the synthesizer. It contains the pad, binaural beats, noise, ocean, rain,
  FM chimes and a Freeverb reverb, and ends with a soft limiter, so the output never clips.
  - The UI thread only sets *target* values: levels, brightness, beat frequency. The audio
    thread moves toward them smoothly, sample by sample, so every change is a fade.
  - The audio thread reads the targets with `pthread_mutex_trylock`, so it never blocks. If the
    lock is busy, it reuses the previous values for one buffer.
- `src/audio_mac.c` (macOS) and `src/audio_alsa.c` (Linux): the audio output. Both implement
  the two functions in `src/audio.h` and simply keep calling `synth_render()`. On macOS
  AudioQueue calls it from its own thread. On Linux a dedicated thread renders and writes
  blocks to ALSA, recovering automatically from underruns. To support another system, only a
  new file of this kind is needed.
- `src/main.c`: the terminal UI (raw mode, ANSI colors), scenes, key handling and the
  `--render` mode.

The program uses about 7% of one CPU core.
