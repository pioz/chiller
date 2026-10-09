// The C engine (src/synth.c + src/app.c) compiled to WebAssembly by `make web`.
// Runs inside the AudioWorklet, or on the page itself when worklets are unavailable.

export const LAYERS = 6;
export const SCENES = 5;

export class Engine {
  constructor(module, seed) {
    this.wasm = new WebAssembly.Instance(module, {}).exports;
    this.wasm.app_init(seed >>> 0, 1);
    // Memory never grows (the engine allocates nothing), so the views stay valid.
    this.bytes = new Uint8Array(this.wasm.memory.buffer);
    this.out = new Float32Array(this.wasm.memory.buffer, this.wasm.web_buffer(), 4096 * 2);
  }

  // TextDecoder is missing in AudioWorklet scopes; the engine's strings are plain ASCII.
  str(ptr) {
    let s = "";
    while (this.bytes[ptr]) s += String.fromCharCode(this.bytes[ptr++]);
    return s;
  }

  render(left, right) {
    const n = this.wasm.web_render(left.length);
    for (let i = 0; i < n; i++) {
      left[i] = this.out[2 * i];
      right[i] = this.out[2 * i + 1];
    }
  }

  command({ cmd, value }) {
    const w = this.wasm;
    switch (cmd) {
      case "scene": w.app_apply_scene(value); break;
      case "press": w.app_press(value); break;
      case "variation": w.app_new_variation(); break;
      case "volume": w.app_change_volume(value); break;
      case "brightness": w.app_change_brightness(value); break;
      case "mute": w.app_set_muted(value ? 1 : 0); break;
    }
  }

  state() {
    const w = this.wasm;
    w.app_poll();
    const layers = [];
    for (let l = 0; l < LAYERS; l++) {
      layers.push({
        key: String.fromCharCode(w.app_layer_key(l)),
        name: this.str(w.app_layer_name(l)),
        on: w.app_layer_on(l) !== 0,
        detail: this.str(w.app_layer_detail(l)),
        level: w.app_level(l),
      });
    }
    const scenes = [];
    for (let i = 0; i < SCENES; i++) scenes.push(this.str(w.app_scene_name(i)));
    return {
      scene: w.app_scene(),
      scenes,
      key: this.str(w.app_key_name()),
      chord: this.str(w.app_chord()),
      layers,
      volume: w.app_volume(),
      brightness: w.app_brightness(),
      breath: w.app_breath_phase(),
    };
  }
}
