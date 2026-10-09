import { Engine } from "./engine.js";

const $ = (id) => document.getElementById(id);
const SAMPLE_RATE = 44100;
const BREATH_DOTS = 8; // per side, as in the terminal's ·····●●●●●·····

let ctx = null;      // AudioContext
let send = null;     // sends a command to the engine
let playing = false;
let pauseTimer = 0;
let wakeLock = null;
let built = false;
let layerKeys = ""; // "pbnorc", from the engine

// ------------------------------------------------------------------ audio

async function start() {
  ctx = new AudioContext({ sampleRate: SAMPLE_RATE, latencyHint: "playback" });
  // Safari: play even with the silent switch on, like a music app.
  if (navigator.audioSession) navigator.audioSession.type = "playback";

  const bytes = await (await fetch("chiller.wasm")).arrayBuffer();
  const seed = crypto.getRandomValues(new Uint32Array(1))[0] || 1;
  let node;

  if (ctx.audioWorklet) {
    await ctx.audioWorklet.addModule("worklet.js");
    node = new AudioWorkletNode(ctx, "chiller", {
      numberOfInputs: 0,
      outputChannelCount: [2],
      processorOptions: { bytes, seed },
    });
    node.port.onmessage = (e) => update(e.data);
    send = (msg) => node.port.postMessage(msg);
  } else {
    // AudioWorklet needs HTTPS (or localhost). Over plain http, e.g. a phone on the
    // local network, fall back to the older ScriptProcessorNode on the page's thread.
    const engine = new Engine(await WebAssembly.compile(bytes), seed);
    node = ctx.createScriptProcessor(2048, 1, 2);
    node.onaudioprocess = (e) =>
      engine.render(e.outputBuffer.getChannelData(0), e.outputBuffer.getChannelData(1));
    send = (msg) => { engine.command(msg); update(engine.state()); };
    setInterval(() => playing && update(engine.state()), 50);
    update(engine.state());
  }

  node.connect(ctx.destination);
  await ctx.resume();
  playing = true;
  keepAwake(true);
  $("intro").hidden = true;
  showPlaying();
}

function togglePlay() {
  if (!ctx) return;
  clearTimeout(pauseTimer);
  if (playing) {
    // Fade out like `q` in the terminal, then suspend the audio to save battery.
    send({ cmd: "mute", value: 1 });
    pauseTimer = setTimeout(() => ctx.suspend(), 1200);
  } else {
    ctx.resume();
    send({ cmd: "mute", value: 0 });
  }
  playing = !playing;
  keepAwake(playing);
  showPlaying();
}

// iOS stops web audio when the screen locks, so keep the screen on while playing.
async function keepAwake(on) {
  try {
    if (on && !wakeLock && navigator.wakeLock) {
      wakeLock = await navigator.wakeLock.request("screen");
      wakeLock.addEventListener("release", () => { wakeLock = null; });
    } else if (!on && wakeLock) {
      await wakeLock.release();
    }
  } catch {
    // not supported or refused: the app works anyway
  }
}

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible" && playing) {
    ctx.resume();
    keepAwake(true);
  }
});

// ------------------------------------------------------------------ interface

function bar(el, segments) {
  el.replaceChildren(...Array.from({ length: segments }, () => document.createElement("i")));
}

function setBar(el, value) {
  const lit = Math.round(Math.max(0, Math.min(1, value)) * el.children.length);
  for (let i = 0; i < el.children.length; i++) el.children[i].classList.toggle("lit", i < lit);
  el.setAttribute("aria-valuenow", Math.round(value * 100));
}

function build(state) {
  $("scenes").replaceChildren(...state.scenes.map((name, i) => {
    const b = document.createElement("button");
    b.textContent = i + 1;
    b.title = name;
    b.setAttribute("aria-label", `Scene ${i + 1}: ${name}`);
    b.onclick = () => command("scene", i);
    return b;
  }));

  $("layers").replaceChildren(...state.layers.map((layer, l) => {
    const b = document.createElement("button");
    b.className = "layer";
    b.innerHTML = `<span class="key"></span><span class="name"></span><span class="bar"></span><span class="detail"></span>`;
    b.querySelector(".key").textContent = layer.key;
    b.querySelector(".name").textContent = layer.name;
    bar(b.querySelector(".bar"), 10);
    b.onclick = () => command("press", l);
    return b;
  }));

  bar($("volume"), 10);
  bar($("brightness"), 10);
  layerKeys = state.layers.map((layer) => layer.key).join("");
  built = true;
}

function update(state) {
  if (!built) build(state);
  $("key").textContent = state.key;
  $("chord").textContent = state.chord;
  $("scene-name").textContent = state.scenes[state.scene];

  [...$("scenes").children].forEach((b, i) => b.setAttribute("aria-pressed", i === state.scene));

  [...$("layers").children].forEach((b, l) => {
    const layer = state.layers[l];
    b.classList.toggle("on", layer.on);
    b.setAttribute("aria-pressed", layer.on);
    b.querySelector(".detail").textContent = layer.detail;
    setBar(b.querySelector(".bar"), layer.level);
  });

  setBar($("volume"), state.volume);
  setBar($("brightness"), state.brightness);

  // 5 s inhale, 5 s exhale, in phase with the ocean waves
  const env = 0.5 - 0.5 * Math.cos(2 * Math.PI * state.breath);
  const fill = Math.round(env * BREATH_DOTS);
  let dots = "";
  for (let i = -BREATH_DOTS; i <= BREATH_DOTS; i++) dots += Math.abs(i) <= fill ? "●" : "·";
  $("breath").textContent = dots;
  $("breath-label").textContent = state.breath < 0.5 ? "inhale…" : "exhale…";
}

function showPlaying() {
  $("play").textContent = playing ? "❚❚" : "▶";
  $("play").setAttribute("aria-label", playing ? "Pause" : "Play");
}

function command(cmd, value) {
  if (send) send({ cmd, value });
}

// ------------------------------------------------------------------ input

$("start").onclick = () => {
  $("start").disabled = true;
  start().catch((err) => {
    $("start").disabled = false;
    $("intro-note").textContent = `cannot start the audio: ${err.message}`;
  });
};
$("play").onclick = togglePlay;
$("variation").onclick = () => command("variation");
document.querySelectorAll("[data-cmd]").forEach((b) => {
  b.onclick = () => command(b.dataset.cmd, Number(b.dataset.value));
});

// The terminal's shortcuts, for desktop browsers.
const KEYS = {
  " ": ["variation"],
  "+": ["volume", 0.05], "=": ["volume", 0.05], "-": ["volume", -0.05], "_": ["volume", -0.05],
  ArrowUp: ["volume", 0.05], ArrowDown: ["volume", -0.05],
  "]": ["brightness", 0.1], "[": ["brightness", -0.1],
  ArrowRight: ["brightness", 0.1], ArrowLeft: ["brightness", -0.1],
};
document.addEventListener("keydown", (e) => {
  if (e.metaKey || e.ctrlKey || e.altKey || !send) return;
  const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
  if (k >= "1" && k <= "5") command("scene", Number(k) - 1);
  else if (k.length === 1 && layerKeys.includes(k)) command("press", layerKeys.indexOf(k));
  else if (k === "q") togglePlay();
  else if (KEYS[k]) command(...KEYS[k]);
  else return;
  e.preventDefault();
});

if ("serviceWorker" in navigator && window.isSecureContext) {
  navigator.serviceWorker.register("sw.js").catch(() => {});
}
