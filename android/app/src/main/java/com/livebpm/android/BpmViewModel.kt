package com.livebpm.android

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import android.app.Application
import android.content.Intent
import androidx.core.content.FileProvider
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import java.util.Locale
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.round

/** One point on the recording graph: milliseconds since recording started + BPM. */
data class BpmSample(val tMs: Long, val bpm: Float)

/** Live accuracy stats against a known reference tempo (test mode). */
data class TestStats(
    val total: Int = 0,
    val onTarget: Int = 0,
    val doubleTime: Int = 0,   // detected ≈ 2 × target
    val halfTime: Int = 0,     // detected ≈ ½ × target
    val off: Int = 0,
    val sumAbsErrOnTarget: Double = 0.0,
) {
    val onPct: Int get() = if (total > 0) (onTarget * 100) / total else 0
    val doublePct: Int get() = if (total > 0) (doubleTime * 100) / total else 0
    val halfPct: Int get() = if (total > 0) (halfTime * 100) / total else 0
    val offPct: Int get() = if (total > 0) (off * 100) / total else 0
    val meanAbsErr: Double get() = if (onTarget > 0) sumAbsErrOnTarget / onTarget else 0.0
}

/** Classification of the current reading vs the reference tempo. */
enum class TestVerdict { NONE, ON_TARGET, DOUBLE_TIME, HALF_TIME, OFF }

/**
 * Owns the native engine and drives all UI state. The display-smoothing logic
 * (confidence gating, EMA, magnetic snap, visual deadzone, 2s clear timeout)
 * is ported from the web DisplayState in livebpm/wasm/index.html.
 */
class BpmViewModel(app: Application) : AndroidViewModel(app) {

    // ---- Observable UI state -------------------------------------------------
    var bpmText by mutableStateOf("--")
        private set
    var confidence by mutableStateOf(0f) // 0..1
        private set

    /** Smoothed numeric BPM that drives the beat-pulse animation (0 = no estimate). */
    var bpmValue by mutableStateOf(0f)
        private set

    /** Microphone input level for the bottom meter, 0..1 (dB-scaled, peak-held). */
    var micLevel by mutableStateOf(0f)
        private set

    /** Whether a tempo recording is currently in progress. */
    var isRecording by mutableStateOf(false)
        private set

    /** Elapsed seconds of the current (or last) recording, for the graph header. */
    var recordElapsedSec by mutableStateOf(0f)
        private set

    /** Recorded (time, BPM) points backing the history graph. */
    val recordedBpm = mutableStateListOf<BpmSample>()

    /** Per-band spectrum levels (0..1, dB-scaled) for the input monitor. */
    var spectrumLevels by mutableStateOf(FloatArray(SPECTRUM_BANDS))
        private set

    // ---- Test mode (reference-tempo accuracy) ---------------------------------
    var testModeEnabled by mutableStateOf(false)
        private set

    /** Known tempo of what's playing (default: Billie Jean, 117 BPM). */
    var targetBpm by mutableStateOf(117f)
        private set

    /** On-target tolerance in BPM. */
    var testTolerance by mutableStateOf(3f)
        private set

    var testVerdict by mutableStateOf(TestVerdict.NONE)
        private set

    var testStats by mutableStateOf(TestStats())
        private set
    var status by mutableStateOf("Ready.")
        private set
    var isListening by mutableStateOf(false)
        private set
    var advancedExpanded by mutableStateOf(false)
        private set

    var selectedResolution by mutableStateOf(Params.DEFAULT_RESOLUTION)
        private set
    var selectedPreset by mutableStateOf(Params.DEFAULT_PRESET)
        private set
    var heuristicsEnabled by mutableStateOf(true)
        private set

    /** Current value of every Advanced slider, keyed by param id. */
    val sliderValues = mutableStateMapOf<String, Float>()

    // ---- Display-smoothing state (mirrors JS DisplayState) -------------------
    private var currentBpm = 0.0
    private var alpha = 0.15
    private var visualDeadzone = 0.1
    private var magneticSnapRange = 0.15
    private var confThresholdDisp = 0.50

    // ---- Engine / audio ------------------------------------------------------
    private var handle: Long = 0L

    // Peak amplitude held between meter polls; written on the audio thread.
    @Volatile private var rawLevel = 0f

    // Latest per-band magnitudes from the audio thread (fresh array each block).
    @Volatile private var rawBands: FloatArray? = null

    private val spectrumAnalyzer = SpectrumAnalyzer(SAMPLE_RATE, bandCount = SPECTRUM_BANDS)

    private val audioCapture = AudioCapture(getApplication<Application>(), SAMPLE_RATE) { buf, n ->
        if (handle != 0L) NativeBridge.nativeProcess(handle, buf, n)
        var peak = 0f
        for (i in 0 until n) {
            val a = abs(buf[i])
            if (a > peak) peak = a
        }
        if (peak > rawLevel) rawLevel = peak
        rawBands = spectrumAnalyzer.process(buf, n)
    }
    private var pollJob: Job? = null
    private var clearJob: Job? = null
    private var lastNotifyMs = 0L
    private var recordStartMs = 0L
    private var lastSampleMs = 0L

    init {
        // Seed sliders with their defaults so the UI renders before the engine replies.
        Params.sliderParams.values.forEach { sliderValues[it.id] = it.default }

        handle = NativeBridge.nativeCreate(SAMPLE_RATE.toDouble(), audioCapture.blockSize)
        // Mirror the web 'ready' handler: default preset + default resolution.
        loadPreset(Params.DEFAULT_PRESET)
        if (handle != 0L) NativeBridge.nativeSetResolution(handle, selectedResolution)
    }

    // ---- User actions --------------------------------------------------------

    fun loadPreset(name: String) {
        selectedPreset = name
        if (handle != 0L) {
            val arr = NativeBridge.nativeLoadPreset(handle, name)
            if (arr.size > Params.HEURISTICS_INDEX) applyPresetValues(arr)
        }
        flushDisplay()
    }

    /** "Reset" button — revert tunables to the current preset's defaults. */
    fun resetSettings() = loadPreset(selectedPreset)

    /** "Flush" button — clear DSP state without changing settings. */
    fun flush() {
        if (handle != 0L) NativeBridge.nativeReset(handle)
        flushDisplay()
    }

    fun selectResolution(value: Int) {
        selectedResolution = value
        if (handle != 0L) NativeBridge.nativeSetResolution(handle, value)
        flushDisplay()
    }

    fun toggleHeuristics(enabled: Boolean) {
        heuristicsEnabled = enabled
        if (handle != 0L) NativeBridge.nativeSetHeuristics(handle, enabled)
        flushDisplay()
    }

    fun setParam(id: String, value: Float) {
        sliderValues[id] = value
        when (id) {
            Params.P_UI_SMOOTHING -> alpha = value.toDouble()
            Params.P_VISUAL_DEADZONE -> visualDeadzone = value.toDouble()
            Params.P_MAGNETIC_SNAP -> magneticSnapRange = value.toDouble()
            Params.P_CONF_THRESHOLD -> confThresholdDisp = value.toDouble()
        }
        if (handle != 0L) NativeBridge.nativeSetParam(handle, id, value.toDouble())
    }

    fun toggleAdvanced() { advancedExpanded = !advancedExpanded }

    /** Start/stop logging the detected BPM over time. Requires the mic to be on. */
    fun toggleRecording() {
        if (isRecording) {
            isRecording = false
            val secs = recordElapsedSec.toInt()
            status = "Recording stopped (${secs / 60}:${(secs % 60).toString().padStart(2, '0')})."
        } else {
            if (!isListening) return
            recordedBpm.clear()
            recordStartMs = System.currentTimeMillis()
            lastSampleMs = 0L
            recordElapsedSec = 0f
            isRecording = true
            status = "Recording…"
        }
    }

    /** Discard the recorded history (hides the graph). */
    fun clearRecording() {
        recordedBpm.clear()
        recordElapsedSec = 0f
    }

    /**
     * Export the recorded session as CSV and open the system share sheet
     * (Drive, email, etc.). With test mode on, each row also carries the
     * reference tempo, delta, and verdict for offline analysis.
     */
    fun exportRecording() {
        if (recordedBpm.isEmpty()) return
        val app = getApplication<Application>()
        try {
            val dir = File(app.cacheDir, "exports").apply { mkdirs() }
            val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val file = File(dir, "livebpm_$stamp.csv")

            val sb = StringBuilder()
            if (testModeEnabled) {
                sb.append("time_sec,bpm,target_bpm,delta_bpm,verdict\n")
                for (s in recordedBpm) {
                    sb.append(String.format(
                        Locale.US, "%.2f,%.2f,%.1f,%.2f,%s\n",
                        s.tMs / 1000.0, s.bpm, targetBpm, s.bpm - targetBpm, classify(s.bpm).name,
                    ))
                }
            } else {
                sb.append("time_sec,bpm\n")
                for (s in recordedBpm) {
                    sb.append(String.format(Locale.US, "%.2f,%.2f\n", s.tMs / 1000.0, s.bpm))
                }
            }
            file.writeText(sb.toString())

            val uri = FileProvider.getUriForFile(app, "${app.packageName}.fileprovider", file)
            val send = Intent(Intent.ACTION_SEND).apply {
                type = "text/csv"
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            val chooser = Intent.createChooser(send, "Export BPM recording").apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            app.startActivity(chooser)
            status = "Exported ${recordedBpm.size} samples to CSV."
        } catch (e: Exception) {
            status = "Export failed: ${e.message}"
        }
    }

    // ---- Test mode ------------------------------------------------------------

    fun toggleTestMode() {
        testModeEnabled = !testModeEnabled
        if (testModeEnabled) resetTestStats()
    }

    // Named update* (not set*) to avoid a JVM signature clash with the
    // compiler-generated property setters of targetBpm / testTolerance.
    fun updateTargetBpm(value: Float) {
        targetBpm = value
        resetTestStats()
    }

    fun updateTestTolerance(value: Float) {
        testTolerance = value
        resetTestStats()
    }

    fun resetTestStats() {
        testStats = TestStats()
        testVerdict = TestVerdict.NONE
    }

    /** Classify a reading against the reference tempo. */
    private fun classify(b: Float): TestVerdict {
        if (b <= 0f) return TestVerdict.NONE
        val t = targetBpm
        val tol = testTolerance
        return when {
            abs(b - t) <= tol -> TestVerdict.ON_TARGET
            abs(b - 2f * t) <= 2f * tol -> TestVerdict.DOUBLE_TIME
            abs(b - t / 2f) <= tol -> TestVerdict.HALF_TIME
            else -> TestVerdict.OFF
        }
    }

    /** Classify the current reading against the reference and update session stats. */
    private fun updateTestStats() {
        if (!testModeEnabled || !isListening) return
        val b = bpmValue
        val verdict = classify(b)
        testVerdict = verdict
        if (verdict == TestVerdict.NONE) return
        val t = targetBpm
        val s = testStats
        testStats = TestStats(
            total = s.total + 1,
            onTarget = s.onTarget + if (verdict == TestVerdict.ON_TARGET) 1 else 0,
            doubleTime = s.doubleTime + if (verdict == TestVerdict.DOUBLE_TIME) 1 else 0,
            halfTime = s.halfTime + if (verdict == TestVerdict.HALF_TIME) 1 else 0,
            off = s.off + if (verdict == TestVerdict.OFF) 1 else 0,
            sumAbsErrOnTarget = s.sumAbsErrOnTarget +
                if (verdict == TestVerdict.ON_TARGET) abs(b - t).toDouble() else 0.0,
        )
    }

    private fun recordTick() {
        val now = System.currentTimeMillis()
        if (now - lastSampleMs < RECORD_SAMPLE_MS) return
        lastSampleMs = now
        val elapsed = now - recordStartMs
        recordElapsedSec = elapsed / 1000f
        if (bpmValue > 0f) {
            recordedBpm.add(BpmSample(elapsed, bpmValue))
            if (recordedBpm.size > MAX_RECORD_POINTS) {
                // Halve resolution to keep memory/draw bounded on long sessions.
                val kept = recordedBpm.filterIndexed { i, _ -> i % 2 == 0 }
                recordedBpm.clear()
                recordedBpm.addAll(kept)
            }
        }
    }

    fun onMicPermissionDenied() {
        status = "Microphone permission denied."
    }

    fun startListening() {
        if (isListening) return
        try {
            audioCapture.start()
        } catch (e: Exception) {
            status = "Error: ${e.message}"
            return
        }
        isListening = true
        status = "Listening... play a rhythm!"
        rawLevel = 0f
        micLevel = 0f
        rawBands = null
        spectrumLevels = FloatArray(SPECTRUM_BANDS)
        resetTestStats()
        lastNotifyMs = 0L
        // Foreground "now" service: keeps detecting while backgrounded + shows the
        // live BPM on the lock screen / Samsung Now Bar.
        LiveBpmService.start(getApplication<Application>())
        flushDisplay()
        startPolling()
    }

    fun stopListening() {
        if (!isListening) return
        audioCapture.stop()
        isListening = false
        isRecording = false // freeze any active recording (history is kept for review)
        pollJob?.cancel(); pollJob = null
        clearJob?.cancel(); clearJob = null
        rawLevel = 0f
        micLevel = 0f
        rawBands = null
        spectrumLevels = FloatArray(SPECTRUM_BANDS)
        testVerdict = TestVerdict.NONE
        LiveBpmService.stop(getApplication<Application>())
        flushDisplay()
        status = "Ready."
    }

    // ---- Internals -----------------------------------------------------------

    private fun applyPresetValues(arr: DoubleArray) {
        Params.PRESET_PARAM_ORDER.forEachIndexed { i, id ->
            sliderValues[id] = arr[i].toFloat()
        }
        heuristicsEnabled = arr[Params.HEURISTICS_INDEX] != 0.0
        alpha = sliderValues[Params.P_UI_SMOOTHING]?.toDouble() ?: alpha
        visualDeadzone = sliderValues[Params.P_VISUAL_DEADZONE]?.toDouble() ?: visualDeadzone
        magneticSnapRange = sliderValues[Params.P_MAGNETIC_SNAP]?.toDouble() ?: magneticSnapRange
        confThresholdDisp = sliderValues[Params.P_CONF_THRESHOLD]?.toDouble() ?: confThresholdDisp
    }

    private fun startPolling() {
        pollJob?.cancel()
        pollJob = viewModelScope.launch {
            while (isActive) {
                if (handle != 0L) updateDisplay(NativeBridge.nativeGetResult(handle))
                updateMicLevel()
                updateSpectrum()
                updateTestStats()
                if (isRecording) recordTick()
                maybeUpdateNotification()
                delay(POLL_INTERVAL_MS)
            }
        }
    }

    private fun maybeUpdateNotification() {
        val now = System.currentTimeMillis()
        if (now - lastNotifyMs >= NOTIFY_INTERVAL_MS) {
            lastNotifyMs = now
            LiveBpmService.instance?.update(bpmText, confidence)
        }
    }

    private fun updateMicLevel() {
        val peak = rawLevel
        rawLevel = 0f
        val target = amplitudeToLevel(peak)
        // Instant attack, smooth release — reads like a real input meter.
        micLevel = if (target >= micLevel) target else micLevel + (target - micLevel) * METER_RELEASE
    }

    private fun updateSpectrum() {
        val bands = rawBands ?: return
        val prev = spectrumLevels
        val next = FloatArray(bands.size)
        for (i in bands.indices) {
            // SPECTRUM_GAIN is display gain only — lifts band averages (much smaller
            // than full-scale) into the same -60 dB..0 window as the level meter.
            val target = amplitudeToLevel(bands[i] * SPECTRUM_GAIN)
            val p = if (i < prev.size) prev[i] else 0f
            next[i] = if (target >= p) target else p + (target - p) * METER_RELEASE
        }
        spectrumLevels = next
    }

    /** Map a linear peak amplitude (0..1) to a perceptual 0..1 meter level over a dB floor. */
    private fun amplitudeToLevel(peak: Float): Float {
        if (peak <= 1e-5f) return 0f
        val db = 20.0 * log10(peak.toDouble())          // <= 0 dBFS
        return ((db + METER_DB_RANGE) / METER_DB_RANGE).coerceIn(0.0, 1.0).toFloat()
    }

    private fun flushDisplay() {
        currentBpm = 0.0
        bpmText = "--"
        bpmValue = 0f
        confidence = 0f
        clearJob?.cancel(); clearJob = null
    }

    private fun updateDisplay(r: DoubleArray) {
        if (r.size < 3) return
        val bpm = r[0]
        val conf = r[1]
        val hasValid = r[2] != 0.0

        if (hasValid) {
            clearJob?.cancel(); clearJob = null

            // Confidence-gated EMA — only nudge the tempo when the engine is sure.
            if (conf >= confThresholdDisp) {
                if (currentBpm == 0.0) currentBpm = bpm
                currentBpm = alpha * bpm + (1.0 - alpha) * currentBpm
            }

            var target = currentBpm
            val nearest = round(target)
            if (abs(target - nearest) < magneticSnapRange) target = nearest

            val displayed = bpmText.toDoubleOrNull()
            if (displayed != null && abs(target - displayed) < visualDeadzone) {
                target = displayed
            }

            bpmText = String.format(Locale.US, "%.1f", target)
            bpmValue = target.toFloat()
            confidence = conf.toFloat()
        } else if (clearJob == null) {
            // No valid estimate — clear the display after 2 seconds.
            clearJob = viewModelScope.launch {
                delay(2000)
                currentBpm = 0.0
                bpmText = "--"
                bpmValue = 0f
                confidence = 0f
                clearJob = null
            }
        }
    }

    override fun onCleared() {
        stopListening()
        if (handle != 0L) {
            NativeBridge.nativeDestroy(handle)
            handle = 0L
        }
        super.onCleared()
    }

    companion object {
        private const val SAMPLE_RATE = 48000
        // Match the web build, which applies its display smoothing roughly every
        // 10 audio render quanta (~27 ms) rather than 10x/second.
        private const val POLL_INTERVAL_MS = 27L
        // Input meter: per-poll release factor and the dB span mapped to 0..1.
        private const val METER_RELEASE = 0.30f
        private const val METER_DB_RANGE = 60.0
        // How often to refresh the live notification (ms).
        private const val NOTIFY_INTERVAL_MS = 500L
        // Recording: how often to log a BPM point, and the max points kept.
        private const val RECORD_SAMPLE_MS = 250L
        private const val MAX_RECORD_POINTS = 2400
        // Spectrum monitor: band count and display gain (see updateSpectrum).
        const val SPECTRUM_BANDS = 24
        private const val SPECTRUM_GAIN = 6f
    }
}
