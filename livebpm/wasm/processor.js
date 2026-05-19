// This file runs in the browser's AudioWorklet thread.
// It receives audio from the microphone and passes it to the WASM engine.
import createLiveBpmModule from './livebpm_wasm.js';

// Polyfill performance.now() to define in AudioWorkletGlobalScope
if (typeof globalThis.performance === 'undefined') {
    globalThis.performance = {
        now: function() {
            return Date.now(); 
        }
    };
}

class LiveBpmProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.engine = null;
        this.heapPtr = null;
        this.module = null;
        this.ready = false;

        // Listen for when the WASM module arrives
        this.port.onmessage = async (event) => {
            if (event.data.type === 'init') {
                // Receive the binary from the main thread
                await this.initWasm(event.data.sampleRate, event.data.wasmBinary);
            } else if (event.data.type === 'setResolution') {
                if (this.engine) {
                    // Map the integer from JS to the Embind Enum
                    let resEnum;
                    if (event.data.value === 1) resEnum = this.module.NoteResolution.HalfNote;
                    else if (event.data.value === 3) resEnum = this.module.NoteResolution.EighthNote;
                    else if (event.data.value === 4) resEnum = this.module.NoteResolution.EighthNoteTriplet;
                    else if (event.data.value === 5) resEnum = this.module.NoteResolution.SixteenthNote;
                    else if (event.data.value === 6) resEnum = this.module.NoteResolution.SmartMixed;
                    else resEnum = this.module.NoteResolution.QuarterNote;
                    
                    this.engine.setTargetResolution(resEnum);
                }
            } else if (event.data.type === 'setHeuristics') {
                if (this.engine) {
                    this.engine.setUseHeuristics(event.data.enabled);
                }
            }
        };
    }

    async initWasm(sampleRate, wasmBinary) {
        try {
            // Pass the binary into the Emscripten module factory
            this.module = await createLiveBpmModule({
                wasmBinary: wasmBinary
            });

            // Instantiate C++ Engine wrapper
            this.engine = new this.module.LiveBpmEngine(sampleRate, 128);
            
            // Pre-allocate memory in the WASM heap for the audio buffer
            // 128 floats * 4 bytes per float
            this.heapPtr = this.module._malloc(128 * 4);
            
            this.ready = true;
            this.port.postMessage({ type: 'ready', deadzone: this.engine.getVisualDeadzone() });
        } catch (err) {
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    process(inputs, outputs, parameters) {
        if (!this.ready || !this.engine) return true;
        
        const input = inputs[0]?.[0]; // Channel 0 of input 0
        if (!input || input.length === 0) return true;

        // Create a JS view into WASM heap and copy audio data over
        const heap = new Float32Array(this.module.HEAPF32.buffer, this.heapPtr, input.length);
        heap.set(input);

        // Process the block using C++ code
        this.engine.processBlock(this.heapPtr, input.length);

        // Send the result back to the main thread (every 10 frames to avoid saturating the message port)
        this._frameCount = (this._frameCount || 0) + 1;
        if (this._frameCount % 10 === 0) {
            const rawResult = this.engine.getResult();

            // Unpacking to a plain JS object to prevent cloning errors
            const plainResult = {
                bpm: rawResult.bpm,
                confidence: rawResult.confidence,
                hasValidEstimate: rawResult.hasValidEstimate,
                processingTimeMs: rawResult.processingTimeMs
            };

            this.port.postMessage({ type: 'result', data: plainResult });
        }

        return true; // Keep the processor alive
    }
}

registerProcessor('livebpm-processor', LiveBpmProcessor);