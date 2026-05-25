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
            } else if (event.data.type === 'loadPreset') {
                if (this.engine) {
                    const configValues = this.engine.loadPreset(event.data.preset);
                    this.port.postMessage({ type: 'presetLoaded', data: configValues });
                }
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
            } else if (event.data.type === 'setParameter') {
                const { param, value } = event.data;
                if (this.engine) {
                    if (param === 'silenceThreshold') this.engine.setSilenceThreshold(value);
                    else if (param === 'onsetSensitivity') this.engine.setOnsetSensitivity(value);
                    else if (param === 'complexDomainWeight') this.engine.setComplexDomainWeight(value);
                    else if (param === 'minBpm') this.engine.setMinBpm(value);
                    else if (param === 'maxBpm') this.engine.setMaxBpm(value);
                    else if (param === 'doubleTimeJumpThreshold') this.engine.setDoubleTimeJumpThreshold(value);
                    else if (param === 'halfTimeJumpThreshold') this.engine.setHalfTimeJumpThreshold(value);
                    else if (param === 'gatingAlpha') this.engine.setGatingAlpha(value);
                    else if (param === 'noveltySmoothing') this.engine.setNoveltySmoothing(value);
                    else if (param === 'fluxCompression') this.engine.setFluxCompression(value);
                    else if (param === 'hysteresisBlend') this.engine.setHysteresisBlend(value);
                    else if (param === 'magneticSnapRange') this.engine.setMagneticSnapRange(value);
                    else if (param === 'fastTempoThreshold') this.engine.setFastTempoThreshold(value);
                    else if (param === 'silenceTimeoutSec') this.engine.setSilenceTimeoutSec(value);
                    else if (param === 'warmupHops') this.engine.setWarmupHops(value);
                    else if (param === 'uiSmoothing') this.engine.setUiSmoothing(value);
                    else if (param === 'visualDeadzone') this.engine.setVisualDeadzone(value);
                    else if (param === 'confThreshold') this.engine.setConfThreshold(value);
                }
            } else if (event.data.type === 'reset') {
                if (this.engine) {
                    this.engine.reset();
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