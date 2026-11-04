package com.livebpm.android.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.livebpm.android.BpmSample
import com.livebpm.android.BpmViewModel
import com.livebpm.android.Params
import com.livebpm.android.TestVerdict
import com.livebpm.android.ui.theme.AccentCyan
import com.livebpm.android.ui.theme.AccentMagenta
import com.livebpm.android.ui.theme.AccentStops
import com.livebpm.android.ui.theme.AccentViolet
import com.livebpm.android.ui.theme.BgDeep
import com.livebpm.android.ui.theme.BgGlowCenter
import com.livebpm.android.ui.theme.BorderColor
import com.livebpm.android.ui.theme.Danger
import com.livebpm.android.ui.theme.SurfacePanel
import com.livebpm.android.ui.theme.TextFaint
import com.livebpm.android.ui.theme.TextMuted
import com.livebpm.android.ui.theme.TextPrimary
import com.livebpm.android.ui.theme.TrackColor
import kotlin.math.roundToInt

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun BpmScreen(vm: BpmViewModel, onToggleListen: () -> Unit) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .drawBehind {
                // Solid base + a soft glow bloom behind the orb.
                drawRect(BgDeep)
                val c = Offset(size.width / 2f, size.height * 0.30f)
                val rad = size.maxDimension * 0.55f
                drawCircle(
                    brush = Brush.radialGradient(
                        colors = listOf(BgGlowCenter, Color.Transparent),
                        center = c,
                        radius = rad,
                    ),
                    radius = rad,
                    center = c,
                )
            },
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 22.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            // Scrollable main content
            Column(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .verticalScroll(rememberScrollState())
                    .padding(top = 28.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(7.dp)
                            .clip(CircleShape)
                            .background(if (vm.isListening) AccentCyan else TextFaint),
                    )
                    Spacer(Modifier.size(8.dp))
                    Text(
                        text = "LIVEBPM",
                        color = TextMuted,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold,
                        letterSpacing = 6.sp,
                    )
                }

                Spacer(Modifier.height(28.dp))

                PulseOrb(
                    bpmText = vm.bpmText,
                    bpmValue = vm.bpmValue,
                    confidence = vm.confidence,
                    pulsing = vm.isListening && vm.bpmValue >= 30f,
                )

                Spacer(Modifier.height(8.dp))
                Text(
                    text = "${(vm.confidence * 100).roundToInt()}% confidence",
                    color = TextFaint,
                    fontSize = 12.sp,
                    letterSpacing = 1.sp,
                )

                Spacer(Modifier.height(28.dp))

                FlowRow(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Params.resolutions.forEach { opt ->
                        ResolutionChip(
                            label = opt.label,
                            selected = vm.selectedResolution == opt.value,
                            onClick = { vm.selectResolution(opt.value) },
                        )
                    }
                }

                Spacer(Modifier.height(28.dp))

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(16.dp),
                ) {
                    ListenButton(isListening = vm.isListening, onClick = onToggleListen)
                    RecordButton(
                        recording = vm.isRecording,
                        enabled = vm.isListening,
                        onClick = { vm.toggleRecording() },
                    )
                }

                Spacer(Modifier.height(22.dp))

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        text = if (vm.testModeEnabled) "TEST  ▴" else "TEST  ▾",
                        color = if (vm.testModeEnabled) AccentCyan else TextMuted,
                        fontSize = 12.sp,
                        letterSpacing = 3.sp,
                        fontWeight = FontWeight.Medium,
                        modifier = Modifier
                            .clip(RoundedCornerShape(50))
                            .clickable { vm.toggleTestMode() }
                            .padding(horizontal = 16.dp, vertical = 8.dp),
                    )
                    Text(
                        text = if (vm.advancedExpanded) "ADVANCED  ▴" else "ADVANCED  ▾",
                        color = TextMuted,
                        fontSize = 12.sp,
                        letterSpacing = 3.sp,
                        fontWeight = FontWeight.Medium,
                        modifier = Modifier
                            .clip(RoundedCornerShape(50))
                            .clickable { vm.toggleAdvanced() }
                            .padding(horizontal = 16.dp, vertical = 8.dp),
                    )
                }

                AnimatedVisibility(visible = vm.testModeEnabled) {
                    Column {
                        Spacer(Modifier.height(12.dp))
                        TestPanel(vm)
                    }
                }

                AnimatedVisibility(visible = vm.advancedExpanded) {
                    Column {
                        Spacer(Modifier.height(12.dp))
                        AdvancedMenu(vm)
                    }
                }

                Spacer(Modifier.height(16.dp))
            }

            // Live recording graph — shown while recording or when a session exists.
            if (vm.isRecording || vm.recordedBpm.isNotEmpty()) {
                BpmGraph(
                    samples = vm.recordedBpm,
                    recording = vm.isRecording,
                    elapsedSec = vm.recordElapsedSec,
                    targetBpm = if (vm.testModeEnabled) vm.targetBpm else null,
                    onClear = { vm.clearRecording() },
                    onExport = { vm.exportRecording() },
                )
                Spacer(Modifier.height(12.dp))
            }

            // Live input spectrum (frequency histogram of what the mic hears).
            SpectrumView(levels = vm.spectrumLevels)
            Spacer(Modifier.height(8.dp))

            // Docked input meter + status — always visible at the bottom.
            MicLevelMeter(level = vm.micLevel)
            Spacer(Modifier.height(10.dp))
            Text(
                text = vm.status,
                color = TextFaint,
                fontSize = 12.sp,
                textAlign = TextAlign.Center,
            )
            Spacer(Modifier.height(14.dp))
        }
    }
}

@Composable
private fun PulseOrb(
    bpmText: String,
    bpmValue: Float,
    confidence: Float,
    pulsing: Boolean,
) {
    // One animation cycle == one beat (clamped to a sane range); idle breathes slowly.
    val periodMs = if (pulsing) (60_000f / bpmValue).roundToInt().coerceIn(250, 2000) else 1100
    val transition = rememberInfiniteTransition(label = "orb")
    val phase by transition.animateFloat(
        initialValue = 0f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(periodMs, easing = LinearEasing), RepeatMode.Restart),
        label = "phase",
    )

    // Confidence eases toward its target so the arc glides rather than jumps.
    val confAnim by animateFloatAsState(
        targetValue = confidence.coerceIn(0f, 1f),
        animationSpec = tween(250),
        label = "conf",
    )

    val rippleAlpha = (1f - phase) * (if (pulsing) 0.5f else 0.08f)
    val rippleScale = 1f + 0.26f * phase
    val thump = if (pulsing) 1f + 0.05f * (1f - phase) else 1f

    Box(modifier = Modifier.size(250.dp), contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            val strokeW = 7.dp.toPx()
            val center = Offset(size.width / 2f, size.height / 2f)
            val r = size.minDimension / 2f - strokeW
            val sweep = Brush.sweepGradient(
                colors = listOf(AccentMagenta, AccentViolet, AccentCyan, AccentMagenta),
                center = center,
            )

            // Ambient bloom
            drawCircle(
                brush = Brush.radialGradient(
                    colors = listOf(AccentCyan.copy(alpha = 0.10f), Color.Transparent),
                    center = center,
                    radius = r * 1.35f,
                ),
                radius = r * 1.35f,
                center = center,
            )

            // Beat ripple
            if (rippleAlpha > 0.01f) {
                drawCircle(
                    color = AccentCyan.copy(alpha = rippleAlpha),
                    radius = r * rippleScale,
                    center = center,
                    style = Stroke(width = strokeW * 0.55f),
                )
            }

            // Track ring
            drawCircle(color = TrackColor, radius = r, center = center, style = Stroke(width = strokeW))

            // Confidence arc (gradient)
            drawArc(
                brush = sweep,
                startAngle = -90f,
                sweepAngle = confAnim * 360f,
                useCenter = false,
                topLeft = Offset(center.x - r, center.y - r),
                size = Size(r * 2f, r * 2f),
                style = Stroke(width = strokeW, cap = StrokeCap.Round),
            )
        }

        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.graphicsLayer { scaleX = thump; scaleY = thump },
        ) {
            Text(
                text = bpmText,
                color = TextPrimary,
                fontSize = 60.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
            )
            Text(
                text = "BPM",
                color = AccentCyan,
                fontSize = 12.sp,
                letterSpacing = 6.sp,
                fontWeight = FontWeight.SemiBold,
            )
        }
    }
}

@Composable
private fun MicLevelMeter(level: Float) {
    val count = 22
    val lit = (level.coerceIn(0f, 1f) * count).roundToInt()
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = "MIC",
            color = TextFaint,
            fontSize = 10.sp,
            letterSpacing = 2.sp,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.padding(end = 12.dp),
        )
        Row(
            modifier = Modifier
                .weight(1f)
                .height(13.dp),
            horizontalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            for (i in 0 until count) {
                val frac = i.toFloat() / (count - 1)
                val onColor = when {
                    frac < 0.55f -> AccentCyan
                    frac < 0.80f -> AccentViolet
                    frac < 0.92f -> AccentMagenta
                    else -> Danger
                }
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clip(RoundedCornerShape(2.dp))
                        .background(if (i < lit) onColor else TrackColor),
                )
            }
        }
    }
}

@Composable
private fun ResolutionChip(label: String, selected: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(50)
    val base = Modifier
        .clip(shape)
        .clickable(onClick = onClick)
    val styled = if (selected) {
        base.background(Brush.horizontalGradient(AccentStops))
    } else {
        base.border(1.dp, BorderColor, shape)
    }
    Box(
        modifier = styled.padding(horizontal = 14.dp, vertical = 9.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label,
            color = if (selected) BgDeep else TextMuted,
            fontSize = 13.sp,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
        )
    }
}

@Composable
private fun ListenButton(isListening: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(50)
    val base = Modifier
        .clip(shape)
        .clickable(onClick = onClick)
    val styled = if (isListening) {
        base.border(1.5.dp, Danger, shape)
    } else {
        base.background(Brush.horizontalGradient(AccentStops))
    }
    Box(
        modifier = styled.padding(horizontal = 52.dp, vertical = 16.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = if (isListening) "Stop" else "Listen",
            color = if (isListening) Danger else BgDeep,
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp,
        )
    }
}

@Composable
private fun RecordButton(recording: Boolean, enabled: Boolean, onClick: () -> Unit) {
    val ring = if (enabled) Danger else BorderColor
    Box(
        modifier = Modifier
            .size(56.dp)
            .alpha(if (enabled) 1f else 0.4f)
            .clip(CircleShape)
            .border(2.dp, ring, CircleShape)
            .then(if (enabled) Modifier.clickable(onClick = onClick) else Modifier),
        contentAlignment = Alignment.Center,
    ) {
        if (recording) {
            Box(
                modifier = Modifier
                    .size(20.dp)
                    .clip(RoundedCornerShape(5.dp))
                    .background(Danger),
            )
        } else {
            Box(
                modifier = Modifier
                    .size(26.dp)
                    .clip(CircleShape)
                    .background(if (enabled) Danger else TextFaint),
            )
        }
    }
}

@Composable
private fun BpmGraph(
    samples: List<BpmSample>,
    recording: Boolean,
    elapsedSec: Float,
    targetBpm: Float?,
    onClear: () -> Unit,
    onExport: () -> Unit,
) {
    // Y range from the data, with a 20-BPM minimum span + padding.
    var lo = Float.MAX_VALUE
    var hi = -Float.MAX_VALUE
    for (s in samples) {
        if (s.bpm < lo) lo = s.bpm
        if (s.bpm > hi) hi = s.bpm
    }
    if (samples.isEmpty()) { lo = 60f; hi = 140f }
    if (targetBpm != null) {
        // Keep the reference line in view.
        if (targetBpm < lo) lo = targetBpm
        if (targetBpm > hi) hi = targetBpm
    }
    if (hi - lo < 20f) {
        val mid = (hi + lo) / 2f
        lo = mid - 10f
        hi = mid + 10f
    }
    val padY = (hi - lo) * 0.12f
    val yMin = lo - padY
    val yMax = hi + padY

    val totalMs = if (recording) {
        (elapsedSec * 1000f).toLong().coerceAtLeast(1L)
    } else {
        (samples.lastOrNull()?.tMs ?: 1L).coerceAtLeast(1L)
    }
    val durSecs = (totalMs / 1000L).toInt()
    val curBpm = samples.lastOrNull()?.bpm

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .border(1.dp, BorderColor, RoundedCornerShape(16.dp))
            .background(SurfacePanel)
            .padding(14.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (recording) {
                BlinkingDot()
                Spacer(Modifier.size(7.dp))
                Text("REC", color = Danger, fontSize = 11.sp, letterSpacing = 2.sp, fontWeight = FontWeight.SemiBold)
            } else {
                Text("TEMPO", color = AccentCyan, fontSize = 11.sp, letterSpacing = 2.sp, fontWeight = FontWeight.SemiBold)
            }
            Spacer(Modifier.size(10.dp))
            Text(fmtTime(durSecs), color = TextMuted, fontSize = 12.sp)
            Spacer(Modifier.weight(1f))
            if (curBpm != null) {
                Text(
                    "${Params.format(curBpm, 1)} BPM",
                    color = TextPrimary,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium,
                )
            }
            if (!recording && samples.isNotEmpty()) {
                Spacer(Modifier.size(14.dp))
                Text(
                    "Export",
                    color = AccentCyan,
                    fontSize = 12.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .clickable(onClick = onExport)
                        .padding(horizontal = 4.dp, vertical = 2.dp),
                )
                Spacer(Modifier.size(6.dp))
                Text(
                    "Clear",
                    color = TextFaint,
                    fontSize = 12.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .clickable(onClick = onClear)
                        .padding(horizontal = 4.dp, vertical = 2.dp),
                )
            }
        }

        Spacer(Modifier.height(10.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(120.dp),
        ) {
            Canvas(modifier = Modifier.fillMaxSize()) {
                val w = size.width
                val h = size.height

                for (g in 0..3) {
                    val gy = h * g / 3f
                    drawLine(TrackColor, Offset(0f, gy), Offset(w, gy), strokeWidth = 1f)
                }

                // Reference tempo line (test mode).
                if (targetBpm != null) {
                    val ty = (h - ((targetBpm - yMin) / (yMax - yMin)) * h).coerceIn(0f, h)
                    drawLine(
                        color = TextMuted,
                        start = Offset(0f, ty),
                        end = Offset(w, ty),
                        strokeWidth = 1.5f,
                        pathEffect = PathEffect.dashPathEffect(floatArrayOf(12f, 8f)),
                    )
                }

                if (samples.isNotEmpty()) {
                    fun xFor(t: Long) = (t.toFloat() / totalMs.toFloat()) * w
                    fun yFor(b: Float) = (h - ((b - yMin) / (yMax - yMin)) * h).coerceIn(0f, h)

                    if (samples.size >= 2) {
                        val line = Path()
                        val fill = Path()
                        samples.forEachIndexed { i, s ->
                            val x = xFor(s.tMs)
                            val y = yFor(s.bpm)
                            if (i == 0) {
                                line.moveTo(x, y)
                                fill.moveTo(x, h)
                                fill.lineTo(x, y)
                            } else {
                                line.lineTo(x, y)
                                fill.lineTo(x, y)
                            }
                        }
                        fill.lineTo(xFor(samples.last().tMs), h)
                        fill.close()

                        drawPath(
                            fill,
                            brush = Brush.verticalGradient(
                                listOf(AccentCyan.copy(alpha = 0.18f), Color.Transparent),
                            ),
                        )
                        drawPath(
                            line,
                            brush = Brush.horizontalGradient(AccentStops),
                            style = Stroke(width = 2.5.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round),
                        )
                    }

                    val last = samples.last()
                    drawCircle(
                        color = AccentCyan,
                        radius = 3.5.dp.toPx(),
                        center = Offset(xFor(last.tMs), yFor(last.bpm)),
                    )
                }
            }

            Text(
                "${yMax.roundToInt()}",
                color = TextFaint,
                fontSize = 10.sp,
                modifier = Modifier.align(Alignment.TopStart),
            )
            Text(
                "${yMin.roundToInt()}",
                color = TextFaint,
                fontSize = 10.sp,
                modifier = Modifier.align(Alignment.BottomStart),
            )
        }
    }
}

@Composable
private fun SpectrumView(levels: FloatArray) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.Bottom,
    ) {
        Text(
            text = "45Hz",
            color = TextFaint,
            fontSize = 9.sp,
            modifier = Modifier.padding(end = 6.dp),
        )
        Row(
            modifier = Modifier
                .weight(1f)
                .height(52.dp),
            horizontalArrangement = Arrangement.spacedBy(2.dp),
            verticalAlignment = Alignment.Bottom,
        ) {
            levels.forEachIndexed { i, level ->
                val frac = if (levels.size > 1) i.toFloat() / (levels.size - 1) else 0f
                val color = when {
                    frac < 0.33f -> AccentMagenta
                    frac < 0.66f -> AccentViolet
                    else -> AccentCyan
                }
                val hFrac = level.coerceIn(0.02f, 1f) // keep a visible baseline stub
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight(hFrac)
                        .clip(RoundedCornerShape(topStart = 2.dp, topEnd = 2.dp))
                        .background(if (level > 0.02f) color else TrackColor),
                )
            }
        }
        Text(
            text = "8kHz",
            color = TextFaint,
            fontSize = 9.sp,
            modifier = Modifier.padding(start = 6.dp),
        )
    }
}

@Composable
private fun TestPanel(vm: BpmViewModel) {
    val verdict = vm.testVerdict
    val stats = vm.testStats
    val delta = vm.bpmValue - vm.targetBpm

    val (verdictLabel, verdictColor) = when (verdict) {
        TestVerdict.ON_TARGET -> "ON TARGET" to AccentCyan
        TestVerdict.DOUBLE_TIME -> "DOUBLE-TIME (2×)" to AccentViolet
        TestVerdict.HALF_TIME -> "HALF-TIME (½×)" to AccentViolet
        TestVerdict.OFF -> "OFF TARGET" to Danger
        TestVerdict.NONE -> "—" to TextFaint
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .border(1.dp, BorderColor, RoundedCornerShape(16.dp))
            .background(SurfacePanel)
            .padding(14.dp),
    ) {
        Text(
            text = "REFERENCE TEMPO TEST",
            color = AccentCyan,
            fontSize = 11.sp,
            letterSpacing = 2.sp,
            fontWeight = FontWeight.SemiBold,
        )
        Spacer(Modifier.height(4.dp))
        Text(
            text = "Play a track whose BPM you know and check the detector against it.",
            color = TextFaint,
            fontSize = 12.sp,
        )

        Spacer(Modifier.height(10.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text("Target BPM", color = TextMuted, fontSize = 13.sp)
            Text("${vm.targetBpm.roundToInt()}", color = TextPrimary, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        }
        androidx.compose.material3.Slider(
            value = vm.targetBpm,
            onValueChange = { vm.updateTargetBpm(it.roundToInt().toFloat()) },
            valueRange = 40f..240f,
            colors = androidx.compose.material3.SliderDefaults.colors(
                thumbColor = AccentCyan,
                activeTrackColor = AccentCyan,
                inactiveTrackColor = TrackColor,
            ),
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text("Tolerance ±", color = TextMuted, fontSize = 13.sp)
            Text(Params.format(vm.testTolerance, 1), color = TextPrimary, fontSize = 13.sp)
        }
        androidx.compose.material3.Slider(
            value = vm.testTolerance,
            onValueChange = { vm.updateTestTolerance((it * 2).roundToInt() / 2f) },
            valueRange = 1f..10f,
            colors = androidx.compose.material3.SliderDefaults.colors(
                thumbColor = AccentCyan,
                activeTrackColor = AccentCyan,
                inactiveTrackColor = TrackColor,
            ),
        )

        Spacer(Modifier.height(6.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = verdictLabel,
                color = verdictColor,
                fontSize = 13.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.sp,
            )
            Spacer(Modifier.weight(1f))
            if (verdict != TestVerdict.NONE && vm.bpmValue > 0f) {
                Text(
                    text = (if (delta >= 0) "+" else "") + Params.format(delta, 1) + " BPM",
                    color = verdictColor,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Medium,
                )
            }
        }

        if (stats.total > 0) {
            Spacer(Modifier.height(8.dp))
            Text(
                text = "On ${stats.onPct}%  ·  2× ${stats.doublePct}%  ·  ½× ${stats.halfPct}%  ·  Off ${stats.offPct}%",
                color = TextMuted,
                fontSize = 12.sp,
            )
            Spacer(Modifier.height(2.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = "Avg error on-target: ${Params.format(stats.meanAbsErr.toFloat(), 2)} BPM  ·  n=${stats.total}",
                    color = TextFaint,
                    fontSize = 11.sp,
                    modifier = Modifier.weight(1f),
                )
                Text(
                    text = "Reset",
                    color = TextFaint,
                    fontSize = 12.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .clickable { vm.resetTestStats() }
                        .padding(horizontal = 6.dp, vertical = 2.dp),
                )
            }
        }
    }
}

@Composable
private fun BlinkingDot() {
    val transition = rememberInfiniteTransition(label = "rec")
    val a by transition.animateFloat(
        initialValue = 0.25f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(700, easing = LinearEasing), RepeatMode.Reverse),
        label = "blink",
    )
    Box(
        modifier = Modifier
            .size(9.dp)
            .alpha(a)
            .clip(CircleShape)
            .background(Danger),
    )
}

private fun fmtTime(totalSec: Int): String {
    val m = totalSec / 60
    val s = totalSec % 60
    return "$m:${s.toString().padStart(2, '0')}"
}
