package com.livebpm.android

import android.annotation.SuppressLint
import android.content.Context
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import kotlin.concurrent.thread

/**
 * Captures mono 32-bit float PCM from the microphone and pushes fixed-size
 * blocks to [onAudio] on a dedicated high-priority thread. This is the Android
 * counterpart of the browser AudioWorklet in livebpm/wasm/processor.js.
 *
 * To match the web build — which requests the mic with echo-cancellation,
 * noise-suppression and auto-gain all disabled — we capture from the least
 * processed source the device offers (UNPROCESSED, else VOICE_RECOGNITION),
 * so automatic gain control doesn't skew onset detection.
 *
 * RECORD_AUDIO permission must be granted before [start] is called.
 */
class AudioCapture(
    context: Context,
    private val sampleRate: Int = 48000,
    private val onAudio: (FloatArray, Int) -> Unit,
) {
    @Volatile private var running = false
    private var recordThread: Thread? = null
    private var recorder: AudioRecord? = null

    /** Frames per block handed to the engine. */
    val blockSize: Int

    private val minBufferBytes: Int
    private val audioSource: Int

    init {
        val minBuf = AudioRecord.getMinBufferSize(
            sampleRate,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_FLOAT
        )
        minBufferBytes = if (minBuf > 0) minBuf else 4096
        // ~1024-frame blocks, but never smaller than the device minimum (4 bytes/float).
        blockSize = maxOf(1024, minBufferBytes / 4)

        val am = context.getSystemService(Context.AUDIO_SERVICE) as? AudioManager
        val supportsUnprocessed =
            am?.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true"
        audioSource = if (supportsUnprocessed) {
            MediaRecorder.AudioSource.UNPROCESSED
        } else {
            // Broadly available fallback that disables AGC / noise suppression.
            MediaRecorder.AudioSource.VOICE_RECOGNITION
        }
    }

    @SuppressLint("MissingPermission")
    fun start() {
        if (running) return

        // Buffer a few blocks so we never underrun while the engine processes.
        val bufferBytes = maxOf(blockSize * 4 * 4, minBufferBytes)
        val rec = AudioRecord(
            audioSource,
            sampleRate,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_FLOAT,
            bufferBytes
        )
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            throw IllegalStateException("AudioRecord failed to initialise")
        }

        recorder = rec
        running = true
        rec.startRecording()

        recordThread = thread(name = "livebpm-audio", priority = Thread.MAX_PRIORITY) {
            val buf = FloatArray(blockSize)
            while (running) {
                val n = rec.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING)
                if (n > 0) {
                    onAudio(buf, n)
                } else if (n < 0) {
                    break // read error
                }
            }
        }
    }

    fun stop() {
        running = false
        recordThread?.join(500)
        recordThread = null
        recorder?.let {
            try { it.stop() } catch (_: Exception) {}
            it.release()
        }
        recorder = null
    }
}
