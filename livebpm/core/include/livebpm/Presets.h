#pragma once
#include "Config.h"
#include <string>

namespace livebpm {

    inline Config GetPreset(const std::string& name) {
        Config c; // Base defaults from Config.h

        if (name == "flute") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.015; 
            c.onsetSensitivity = 0.01;  
            c.complexDomainWeight = 0.75; 
            c.gatingAlpha = 0.15;
            c.noveltySmoothing = 0.35;
            c.fluxCompression = 60.0;
            c.hysteresisBlend = 0.04;
            c.uiSmoothing = 0.2;
            c.VISUAL_DEADZONE = 0.25;
            c.magneticSnapRange = 0.20;
            c.minBpm = 50.0;
            c.maxBpm = 200.0;
            c.doubleTimeJumpThreshold = 25.0;
            c.halfTimeJumpThreshold = 15.0;
        } 
        else if (name == "guitar") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.03;
            c.onsetSensitivity = 0.018;  
            c.complexDomainWeight = 0.35; 
            c.gatingAlpha = 0.10;
            c.noveltySmoothing = 0.45;
            c.fluxCompression = 50.0;
            c.hysteresisBlend = 0.07;
            c.uiSmoothing = 0.15;
            c.VISUAL_DEADZONE = 0.15;
            c.magneticSnapRange = 0.15;
            c.minBpm = 40.0;
            c.maxBpm = 240.0;
            c.doubleTimeJumpThreshold = 25.0;
            c.halfTimeJumpThreshold = 10.0;
        } else if (name == "electricguitar") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.02;
            c.onsetSensitivity = 0.015;  
            c.complexDomainWeight = 0.20;
            c.gatingAlpha = 0.15;
            c.noveltySmoothing = 0.40;
            c.fluxCompression = 40.0;
            c.hysteresisBlend = 0.06;
            c.uiSmoothing = 0.15;
            c.VISUAL_DEADZONE = 0.15;
            c.magneticSnapRange = 0.15;
            c.minBpm = 40.0;
            c.maxBpm = 240.0;
            c.doubleTimeJumpThreshold = 25.0; 
            c.halfTimeJumpThreshold = 10.0;
        } else if (name == "saxophone") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.020; 
            c.onsetSensitivity = 0.012;  
            c.complexDomainWeight = 0.55; 
            c.gatingAlpha = 0.12;
            c.noveltySmoothing = 0.40;
            c.fluxCompression = 55.0;
            c.hysteresisBlend = 0.05;
            c.uiSmoothing = 0.2;
            c.VISUAL_DEADZONE = 0.20;
            c.magneticSnapRange = 0.15;
            c.minBpm = 40.0;
            c.maxBpm = 240.0;
            c.doubleTimeJumpThreshold = 20.0;
            c.halfTimeJumpThreshold = 10.0;
        } 
        else if (name == "acousticbass" || name == "acoustic bass") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.025; 
            c.onsetSensitivity = 0.020;
            c.complexDomainWeight = 0.50; 
            c.gatingAlpha = 0.08;
            c.noveltySmoothing = 0.30;
            c.fluxCompression = 45.0;
            c.hysteresisBlend = 0.04;
            c.uiSmoothing = 0.15;
            c.VISUAL_DEADZONE = 0.25;
            c.magneticSnapRange = 0.25;
            c.minBpm = 30.0;
            c.maxBpm = 200.0;
            c.doubleTimeJumpThreshold = 20.0;
            c.halfTimeJumpThreshold = 15.0;
        } else if (name == "electricbass") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.015;
            c.onsetSensitivity = 0.025;
            c.complexDomainWeight = 0.30;
            c.gatingAlpha = 0.10;
            c.noveltySmoothing = 0.35;
            c.fluxCompression = 45.0;
            c.hysteresisBlend = 0.05;
            c.uiSmoothing = 0.15;
            c.VISUAL_DEADZONE = 0.20;
            c.magneticSnapRange = 0.10;
            c.minBpm = 40.0;
            c.maxBpm = 200.0;
            c.doubleTimeJumpThreshold = 20.0;
            c.halfTimeJumpThreshold = 10.0;
        } else if (name == "cajon") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.03;
            c.onsetSensitivity = 0.025;  
            c.complexDomainWeight = 0.20; 
            c.gatingAlpha = 0.08;
            c.noveltySmoothing = 0.50;
            c.fluxCompression = 45.0;
            c.hysteresisBlend = 0.06;
            c.uiSmoothing = 0.3;
            c.VISUAL_DEADZONE = 0.10;
            c.magneticSnapRange = 0.25;
            c.minBpm = 40.0;
            c.maxBpm = 280.0;
            c.doubleTimeJumpThreshold = 10.0;
            c.halfTimeJumpThreshold = 15.0;
        } 
        else if (name == "liveband") {
            c.useHeuristics = true;
            c.SILENCE_THRESHOLD = 0.04;
            c.onsetSensitivity = 0.03;
            c.complexDomainWeight = 0.5;
            c.gatingAlpha = 0.1;
            c.noveltySmoothing = 0.4;
            c.fluxCompression = 50.0;
            c.hysteresisBlend = 0.05;
            c.uiSmoothing = 0.2;
            c.VISUAL_DEADZONE = 0.20;
            c.magneticSnapRange = 0.15;
            c.minBpm = 50.0;
            c.maxBpm = 220.0;
            c.doubleTimeJumpThreshold = 20.0;
            c.halfTimeJumpThreshold = 10.0;
        } 
        else if (name == "slowacoustic") {
            c.useHeuristics = false;
            c.SILENCE_THRESHOLD = 0.01;
            c.onsetSensitivity = 0.005;
            c.complexDomainWeight = 0.75;
            c.gatingAlpha = 0.15;
            c.noveltySmoothing = 0.3;
            c.fluxCompression = 60.0;
            c.hysteresisBlend = 0.04;
            c.uiSmoothing = 0.05;
            c.VISUAL_DEADZONE = 0.30;
            c.magneticSnapRange = 0.20;
            c.minBpm = 30.0;
            c.maxBpm = 120.0;
            c.doubleTimeJumpThreshold = 40.0;
            c.halfTimeJumpThreshold = 10.0;
        }

        return c;
    }

} // namespace livebpm