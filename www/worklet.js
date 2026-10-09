import { Engine } from "./engine.js";

// Renders on the browser's audio thread and reports the state to the page ~20 times a second.
class ChillerProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const { bytes, seed } = options.processorOptions;
    this.engine = new Engine(new WebAssembly.Module(bytes), seed);
    this.blocks = 0;
    this.port.onmessage = (e) => {
      this.engine.command(e.data);
      this.port.postMessage(this.engine.state());
    };
    this.port.postMessage(this.engine.state());
  }

  process(_inputs, outputs) {
    const [left, right] = outputs[0];
    this.engine.render(left, right ?? left);
    if (++this.blocks % 16 === 0) this.port.postMessage(this.engine.state());
    return true;
  }
}

registerProcessor("chiller", ChillerProcessor);
