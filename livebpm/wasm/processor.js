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

        this.port.onmessage = async (event) => {
            const { type } = event.data;

            if (type === 'init') {
                await this.initWasm(event.data.sampleRate, event.data.wasmBinary);

            } else if (type === 'setResolution') {
                if (this.engine) {
                    let resEnum;
                    if      (event.data.value === 1) resEnum = this.module.NoteResolution.HalfNote;
                    else if (event.data.value === 3) resEnum = this.module.NoteResolution.EighthNote;
                    else if (event.data.value === 4) resEnum = this.module.NoteResolution.EighthNoteTriplet;
                    else if (event.data.value === 5) resEnum = this.module.NoteResolution.SixteenthNote;
                    else if (event.data.value === 6) resEnum = this.module.NoteResolution.SmartMixed;
                    else                             resEnum = this.module.NoteResolution.QuarterNote;
                    this.engine.setTargetResolution(resEnum);
                }

            } else if (type === 'setPreset') {
                if (this.engine) {
                    this.engine.setPreset(event.data.id);
                    // Reply with updated config so the UI sliders can sync
                    this.port.postMessage({ type: 'configUpdate', data: this._plainConfig() });
                }

            } else if (type === 'setParam') {
                if (this.engine) {
                    this.engine.setParam(event.data.name, event.data.value);
                }
            }
        };
    }

    async initWasm(sampleRate, wasmBinary) {
        try {
            this.module = await createLiveBpmModule({ wasmBinary });

            // Instantiate C++ Engine wrapper (128-frame block size to match AudioWorklet)
            this.engine = new this.module.LiveBpmEngine(sampleRate, 128);

            // Pre-allocate WASM heap buffer: 128 floats × 4 bytes
            this.heapPtr = this.module._malloc(128 * 4);

            this.ready = true;
            this.port.postMessage({ type: 'ready', deadzone: this.engine.getVisualDeadzone() });

            // Send the initial default config so UI sliders initialise correctly
            this.port.postMessage({ type: 'configUpdate', data: this._plainConfig() });

        } catch (err) {
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    // Unpack Embind val to a plain cloneable JS object before postMessage
    _plainConfig() {
        const raw = this.engine.getConfig();
        return {
            semitoneFluxWeight:   raw.semitoneFluxWeight,
            gatingOffset:         raw.gatingOffset,
            gatingAlpha:          raw.gatingAlpha,
            noveltySmoothing:     raw.noveltySmoothing,
            fluxCompression:      raw.fluxCompression,
            hysteresisBlend:      raw.hysteresisBlend,
            minBpm:               raw.minBpm,
            maxBpm:               raw.maxBpm,
            silenceThreshold:     raw.silenceThreshold,
            halfTimeThreshold:    raw.halfTimeThreshold,
            doubleTimeThreshold:  raw.doubleTimeThreshold,
        };
    }

    process(inputs, outputs, parameters) {
        if (!this.ready || !this.engine) return true;

        const input = inputs[0]?.[0];
        if (!input || input.length === 0) return true;

        // Copy audio data into WASM heap and process
        const heap = new Float32Array(this.module.HEAPF32.buffer, this.heapPtr, input.length);
        heap.set(input);
        this.engine.processBlock(this.heapPtr, input.length);

        // Send result to main thread every 10 frames to avoid saturating the port
        this._frameCount = (this._frameCount || 0) + 1;
        if (this._frameCount % 10 === 0) {
            const rawResult = this.engine.getResult();
            this.port.postMessage({
                type: 'result',
                data: {
                    bpm:              rawResult.bpm,
                    confidence:       rawResult.confidence,
                    hasValidEstimate: rawResult.hasValidEstimate,
                    processingTimeMs: rawResult.processingTimeMs,
                }
            });
        }

        return true;
    }
}

registerProcessor('livebpm-processor', LiveBpmProcessor);
