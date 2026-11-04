package com.livebpm.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.livebpm.android.BpmViewModel
import com.livebpm.android.Params
import com.livebpm.android.SliderParam
import com.livebpm.android.ui.theme.AccentCyan
import com.livebpm.android.ui.theme.BgDeep
import com.livebpm.android.ui.theme.BorderColor
import com.livebpm.android.ui.theme.Danger
import com.livebpm.android.ui.theme.SurfaceElevated
import com.livebpm.android.ui.theme.SurfacePanel
import com.livebpm.android.ui.theme.TextFaint
import com.livebpm.android.ui.theme.TextMuted
import com.livebpm.android.ui.theme.TextPrimary
import com.livebpm.android.ui.theme.TrackColor
import kotlin.math.roundToInt

private const val TEMPO_SECTION = "Tempo Tracking & Locking"

@Composable
fun AdvancedMenu(vm: BpmViewModel) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .border(1.dp, BorderColor, RoundedCornerShape(20.dp))
            .background(SurfacePanel)
            .padding(18.dp),
    ) {
        PresetSelector(vm)

        Spacer(Modifier.height(14.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            GhostButton("Flush", onClick = vm::flush, modifier = Modifier.weight(1f))
            GhostButton("Reset", onClick = vm::resetSettings, modifier = Modifier.weight(1f), danger = true)
        }

        Spacer(Modifier.height(14.dp))
        Text("Tap a parameter name to see what it does.", color = TextFaint, fontSize = 12.sp)

        Params.sections.forEach { section ->
            Spacer(Modifier.height(22.dp))
            SectionHeader(section.title)
            Spacer(Modifier.height(6.dp))

            if (section.title == TEMPO_SECTION) {
                HeuristicsRow(checked = vm.heuristicsEnabled, onCheckedChange = vm::toggleHeuristics)
            }

            section.params.forEach { p ->
                ParamSlider(
                    param = p,
                    value = vm.sliderValues[p.id] ?: p.default,
                    onValueChange = { vm.setParam(p.id, it) },
                )
            }
        }
    }
}

@Composable
private fun SectionHeader(title: String) {
    Text(
        text = title.uppercase(),
        color = AccentCyan,
        fontSize = 11.sp,
        letterSpacing = 2.sp,
        fontWeight = FontWeight.SemiBold,
    )
}

@Composable
private fun PresetSelector(vm: BpmViewModel) {
    var expanded by remember { mutableStateOf(false) }
    val currentLabel = Params.presets.firstOrNull { it.value == vm.selectedPreset }?.label
        ?: vm.selectedPreset
    val shape = RoundedCornerShape(12.dp)

    Box {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clip(shape)
                .background(SurfaceElevated)
                .border(1.dp, BorderColor, shape)
                .clickable { expanded = true }
                .padding(horizontal = 14.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(currentLabel, color = TextPrimary, fontSize = 14.sp, modifier = Modifier.weight(1f))
            Text("▾", color = AccentCyan, fontSize = 14.sp)
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            Params.presets.forEach { p ->
                DropdownMenuItem(
                    text = { Text(p.label) },
                    onClick = {
                        vm.loadPreset(p.value)
                        expanded = false
                    },
                )
            }
        }
    }
}

@Composable
private fun GhostButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    danger: Boolean = false,
) {
    val shape = RoundedCornerShape(12.dp)
    val color = if (danger) Danger else TextMuted
    Box(
        modifier = modifier
            .clip(shape)
            .border(1.dp, if (danger) Danger else BorderColor, shape)
            .clickable(onClick = onClick)
            .padding(vertical = 11.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = color, fontSize = 14.sp, fontWeight = FontWeight.Medium)
    }
}

@Composable
private fun HeuristicsRow(checked: Boolean, onCheckedChange: (Boolean) -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onCheckedChange(!checked) }
            .padding(vertical = 6.dp),
    ) {
        Text(
            text = "Heuristics (Octave / Pattern correction)",
            color = TextMuted,
            fontSize = 13.sp,
            modifier = Modifier.weight(1f),
        )
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = BgDeep,
                checkedTrackColor = AccentCyan,
                checkedBorderColor = AccentCyan,
                uncheckedThumbColor = TextMuted,
                uncheckedTrackColor = TrackColor,
                uncheckedBorderColor = BorderColor,
            ),
        )
    }
}

@Composable
private fun ParamSlider(param: SliderParam, value: Float, onValueChange: (Float) -> Unit) {
    var showTip by remember { mutableStateOf(false) }
    val steps = remember(param.id) {
        val intervals = ((param.max - param.min) / param.step).roundToInt()
        (intervals - 1).coerceAtLeast(0)
    }

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = param.label,
                color = TextMuted,
                fontSize = 13.sp,
                modifier = Modifier
                    .weight(1f)
                    .clickable { showTip = !showTip },
            )
            ValueChip(Params.format(value, param.decimals))
        }
        if (showTip) {
            Text(
                text = param.tip,
                color = TextFaint,
                fontSize = 12.sp,
                modifier = Modifier.padding(top = 3.dp, bottom = 2.dp),
            )
        }
        Slider(
            value = value.coerceIn(param.min, param.max),
            onValueChange = onValueChange,
            valueRange = param.min..param.max,
            steps = steps,
            colors = SliderDefaults.colors(
                thumbColor = AccentCyan,
                activeTrackColor = AccentCyan,
                inactiveTrackColor = TrackColor,
                activeTickColor = Color.Transparent,
                inactiveTickColor = Color.Transparent,
            ),
        )
    }
}

@Composable
private fun ValueChip(text: String) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(SurfaceElevated)
            .padding(horizontal = 10.dp, vertical = 3.dp),
    ) {
        Text(text, color = AccentCyan, fontSize = 12.sp, fontWeight = FontWeight.Medium)
    }
}
