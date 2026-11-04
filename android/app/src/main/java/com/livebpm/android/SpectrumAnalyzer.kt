package com.livebpm.android

import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Lightweight spectrum analyser behind the monitor view: keeps a rolling window
 * of the most recent mic samples, runs a Hann-windowed radix-2 FFT, and folds the
 * magnitudes into log-spaced frequency bands (spanning roughly the engine's
 * 55 Hz–8 kHz semitone filter bank, so the bars correlate with what the detector
 * actually analyses).
 *
 * Not thread-safe: call [process] from the audio thread only. The returned array
 * is freshly allocated and safe to hand across threads.
 */
class SpectrumAnalyzer(
    sampleRate: Int,
    private val fftSize: Int = 2048,
    val bandCount: Int = 24,
    minHz: Double = 45.0,
    maxHz: Double = 8000.0,
) {
    private val history = FloatArray(fftSize)
    private val re = FloatArray(fftSize)
    private val im = FloatArray(fftSize)
    private val window = FloatArray(fftSize) { i ->
        (0.5 - 0.5 * cos(2.0 * PI * i / (fftSize - 1))).toFloat() // Hann
    }

    // Bin index of each band edge (log-spaced), size bandCount + 1.
    private val bandEdges: IntArray

    init {
        val binHz = sampleRate.toDouble() / fftSize
        val logMin = ln(minHz)
        val logMax = ln(min(maxHz, sampleRate / 2.0))
        bandEdges = IntArray(bandCount + 1) { b ->
            val hz = exp(logMin + (logMax - logMin) * b / bandCount)
            (hz / binHz).toInt().coerceIn(1, fftSize / 2)
        }
    }

    /** Feed a block of samples; returns the average magnitude per band. */
    fun process(samples: FloatArray, n: Int): FloatArray {
        // Append to the rolling window (keep the most recent fftSize samples).
        val take = min(n, fftSize)
        val keep = fftSize - take
        if (keep > 0) System.arraycopy(history, take, history, 0, keep)
        System.arraycopy(samples, n - take, history, keep, take)

        for (i in 0 until fftSize) {
            re[i] = history[i] * window[i]
            im[i] = 0f
        }
        fft(re, im)

        val bands = FloatArray(bandCount)
        for (b in 0 until bandCount) {
            val from = bandEdges[b]
            val to = maxOf(bandEdges[b + 1], from + 1)
            var sum = 0f
            for (k in from until to) {
                sum += sqrt(re[k] * re[k] + im[k] * im[k])
            }
            // Normalise by band width and rough FFT+Hann gain (N/4) so a
            // full-scale sine lands near 1.0.
            bands[b] = sum / (to - from) / (fftSize / 4f)
        }
        return bands
    }

    /** In-place iterative radix-2 Cooley–Tukey FFT (validated against numpy). */
    private fun fft(re: FloatArray, im: FloatArray) {
        val n = re.size
        // Bit-reversal permutation
        var j = 0
        for (i in 0 until n - 1) {
            if (i < j) {
                var t = re[i]; re[i] = re[j]; re[j] = t
                t = im[i]; im[i] = im[j]; im[j] = t
            }
            var m = n shr 1
            while (m in 1..j) {
                j -= m
                m = m shr 1
            }
            j += m
        }
        // Butterflies
        var len = 2
        while (len <= n) {
            val ang = -2.0 * PI / len
            val wRe = cos(ang).toFloat()
            val wIm = sin(ang).toFloat()
            var base = 0
            while (base < n) {
                var curRe = 1f
                var curIm = 0f
                val half = len / 2
                for (k in 0 until half) {
                    val aRe = re[base + k]
                    val aIm = im[base + k]
                    val bRe = re[base + k + half] * curRe - im[base + k + half] * curIm
                    val bIm = re[base + k + half] * curIm + im[base + k + half] * curRe
                    re[base + k] = aRe + bRe
                    im[base + k] = aIm + bIm
                    re[base + k + half] = aRe - bRe
                    im[base + k + half] = aIm - bIm
                    val nRe = curRe * wRe - curIm * wIm
                    curIm = curRe * wIm + curIm * wRe
                    curRe = nRe
                }
                base += len
            }
            len = len shl 1
        }
    }
}
