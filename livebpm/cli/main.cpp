// Ignoring third-party libraries for -Wall -Wextra -Wpedantic
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "livebpm/Engine.h"
#include "livebpm/Presets.h"
#include "livebpm/dsp/Biquad.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cmath>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static std::atomic<bool> running{true};
static livebpm::Engine* g_engine = nullptr;
static std::atomic<int>  g_resolutionChoice{2}; // 2 = QuarterNote default
static std::atomic<int>  g_presetId{0};         // 0 = Default

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void signalHandler(int) { running.store(false); }

// Highpass filter instance (removes DC offset and rumble below 80Hz)
static livebpm::dsp::Biquad highpass;
static bool filterInitialized = false;

void audioCallback(ma_device* device, void* /*output*/,
                   const void* input, ma_uint32 frameCount)
{
    if (g_engine && input) {
        // Initialise filter once on first callback using the actual negotiated rate
        if (!filterInitialized) {
            highpass.setCoefficients(
                livebpm::dsp::Biquad::makeHighpass(
                    static_cast<double>(device->sampleRate), 80.0));
            filterInitialized = true;
        }

        const float* in = static_cast<const float*>(input);

        // Filter into a temporary buffer to avoid modifying miniaudio's buffer
        std::vector<float> filtered(frameCount);
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            filtered[i] = highpass.process(in[i]);
        }

        g_engine->processBlock(filtered.data(), static_cast<int>(frameCount));
    }
}

// ---------------------------------------------------------------------------
// Resolution helpers
// ---------------------------------------------------------------------------

const char* resolutionName(livebpm::NoteResolution r)
{
    switch (r) {
        case livebpm::NoteResolution::HalfNote:          return "Half note (0.5x)";
        case livebpm::NoteResolution::QuarterNote:       return "Quarter note";
        case livebpm::NoteResolution::EighthNote:        return "Eighth note (2x)";
        case livebpm::NoteResolution::EighthNoteTriplet: return "Eighth triplet (3x)";
        case livebpm::NoteResolution::SixteenthNote:     return "Sixteenth note (4x)";
        case livebpm::NoteResolution::SmartMixed:        return "Smart / Auto";
        default:                                         return "Unknown";
    }
}

livebpm::NoteResolution choiceToResolution(int choice)
{
    switch (choice) {
        case 1:  return livebpm::NoteResolution::HalfNote;
        case 2:  return livebpm::NoteResolution::QuarterNote;
        case 3:  return livebpm::NoteResolution::EighthNote;
        case 4:  return livebpm::NoteResolution::EighthNoteTriplet;
        case 5:  return livebpm::NoteResolution::SixteenthNote;
        case 6:  return livebpm::NoteResolution::SmartMixed;
        default: return livebpm::NoteResolution::QuarterNote;
    }
}

livebpm::NoteResolution pickResolution()
{
    std::cout << "\nSelect note resolution:\n"
              << "  [1] Half note\n"
              << "  [2] Quarter note  (default)\n"
              << "  [3] Eighth note\n"
              << "  [4] Eighth triplet\n"
              << "  [5] Sixteenth note\n"
              << "  [6] Smart / Auto\n"
              << "\nEnter choice (or press Enter for Quarter note): ";

    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return livebpm::NoteResolution::QuarterNote;

    try {
        int choice = std::stoi(input);
        g_resolutionChoice.store(choice);
        return choiceToResolution(choice);
    } catch (...) {
        return livebpm::NoteResolution::QuarterNote;
    }
}

// ---------------------------------------------------------------------------
// Instrument preset helpers
// ---------------------------------------------------------------------------

const char* presetName(int id)
{
    switch (id) {
        case 1:  return "Cajon Flamenco (Percussive)";
        case 2:  return "Flute (Legato)";
        case 3:  return "Guitar, Acoustic";
        case 4:  return "Guitar, Electric";
        case 5:  return "Bass, Acoustic";
        case 6:  return "Bass, Electric";
        case 7:  return "Saxophone (Alto / Tenor)";
        case 8:  return "Full Band / Live Mix";
        case 9:  return "Ambient / Slow Acoustic";
        default: return "Default / General Purpose";
    }
}

livebpm::Config presetConfig(int id)
{
    switch (id) {
        case 1:  return livebpm::GetPreset("cajon");
        case 2:  return livebpm::GetPreset("flute");
        case 3:  return livebpm::GetPreset("guitar");
        case 4:  return livebpm::GetPreset("electricguitar");
        case 5:  return livebpm::GetPreset("acousticbass");
        case 6:  return livebpm::GetPreset("electricbass");
        case 7:  return livebpm::GetPreset("saxophone");
        case 8:  return livebpm::GetPreset("liveband");
        case 9:  return livebpm::GetPreset("slowacoustic");
        default: return livebpm::GetPreset("default");
    }
}

int pickPreset()
{
    std::cout << "\nSelect instrument preset:\n"
              << "  [0] Default / General Purpose\n"
              << "  [1] Cajón Flamenco (Percussive)\n"
              << "  [2] Flute (Legato)\n"
              << "  [3] Guitar, Acoustic\n"
              << "  [4] Guitar, Electric\n"
              << "  [5] Bass, Acoustic\n"
              << "  [6] Bass, Electric\n"
              << "  [7] Saxophone (Alto / Tenor)\n"
              << "  [8] Full Band / Live Mix\n"
              << "  [9] Ambient / Slow Acoustic\n"
              << "\nEnter choice (or press Enter for Default): ";

    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) return 0;

    try {
        int choice = std::stoi(input);
        if (choice >= 0 && choice <= 9) return choice;
    } catch (...) {}

    std::cout << "Invalid input — using Default.\n";
    return 0;
}

// ---------------------------------------------------------------------------
// Input thread — resolution changes and preset switches while running
// ---------------------------------------------------------------------------

void inputThread(livebpm::Engine* engine)
{
    std::cout << "\nType 1-6 to change resolution, or 'preset N' (0-9) to switch instrument.\n\n";

    while (running.load()) {
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (!running.load()) break;
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;

        // Resolution change
        if (cmd.size() == 1 && cmd[0] >= '1' && cmd[0] <= '6') {
            int choice = cmd[0] - '0';
            g_resolutionChoice.store(choice);
            engine->setTargetResolution(choiceToResolution(choice));
            std::cout << "\n  Resolution: " << resolutionName(choiceToResolution(choice)) << "\n";
            std::cout.flush();
            continue;
        }

        // Preset switch
        if (cmd == "preset") {
            int id = 0;
            if (!(ss >> id) || id < 0 || id > 9) {
                std::cout << "\n  Usage: preset 0-9\n";
                std::cout.flush();
                continue;
            }
            engine->updateTunableConfig(presetConfig(id));
            g_presetId.store(id);
            std::cout << "\n  Preset: " << presetName(id) << "\n";
            std::cout.flush();
            continue;
        }

        std::cout << "\n  Commands: 1-6 (resolution)  |  preset 0-9 (instrument)\n";
        std::cout.flush();
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    double sampleRate = 48000.0;
    int    blockSize  = 512;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "-r" && i + 1 < argc) sampleRate = std::stod(argv[++i]);
        else if (arg == "-b" && i + 1 < argc) blockSize  = std::stoi(argv[++i]);
        else if (arg == "-h") {
            std::cout << "LiveBPM CLI\nUsage: livebpm_cli [-r samplerate] [-b blocksize]\n";
            return 0;
        }
    }

    // --- Configure engine with base settings ---
    livebpm::Config base = livebpm::GetPreset("default");
    base.sampleRate = sampleRate;
    base.blockSize  = blockSize;
    livebpm::Engine engine(base);
    g_engine = &engine;

    // --- Device enumeration ---
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::cerr << "Error: Could not initialise audio context.\n";
        return 1;
    }

    ma_device_info* captureDevices; ma_uint32 captureCount;
    ma_device_info* playbackDevices; ma_uint32 playbackCount;
    if (ma_context_get_devices(&context, &playbackDevices, &playbackCount,
                               &captureDevices, &captureCount) != MA_SUCCESS) {
        std::cerr << "Error: Could not enumerate audio devices.\n";
        ma_context_uninit(&context);
        return 1;
    }

    // Suppress unused variable warnings (only need capture devices)
    (void)playbackDevices;
    (void)playbackCount;

    std::cout << "=== LiveBPM CLI ===\n\nAvailable capture devices:\n";
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        std::cout << "  [" << i << "] " << captureDevices[i].name;
        if (captureDevices[i].isDefault) std::cout << "  (default)";
        std::cout << "\n";
    }

    int deviceIndex = -1;
    while (true) {
        std::cout << "\nEnter device number (or press Enter for default): ";
        std::string input;
        std::getline(std::cin, input);
        if (input.empty()) { deviceIndex = -1; break; }
        try {
            deviceIndex = std::stoi(input);
            if (deviceIndex >= 0 && deviceIndex < static_cast<int>(captureCount)) break;
        } catch (...) {}
        std::cout << "Invalid choice.\n";
        deviceIndex = -1;
    }

    ma_device_id* selectedDeviceId = nullptr;
    ma_device_id  chosenDeviceId;
    if (deviceIndex >= 0) {
        chosenDeviceId   = captureDevices[deviceIndex].id;
        selectedDeviceId = &chosenDeviceId;
        std::cout << "\nSelected: " << captureDevices[deviceIndex].name << "\n";
    } else {
        std::cout << "\nUsing default device.\n";
    }
    ma_context_uninit(&context);

    // --- Preset and resolution selection ---
    int presetId = pickPreset();
    g_presetId.store(presetId);
    engine.updateTunableConfig(presetConfig(presetId));
    std::cout << "Preset:     " << presetName(presetId) << "\n";

    auto resolution = pickResolution();
    engine.setTargetResolution(resolution);
    std::cout << "Resolution: " << resolutionName(resolution) << "\n";

    // --- Audio device setup ---
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format     = ma_format_f32;
    deviceConfig.capture.channels   = 1;
    deviceConfig.capture.pDeviceID  = selectedDeviceId;
    deviceConfig.sampleRate         = static_cast<ma_uint32>(sampleRate);
    deviceConfig.periodSizeInFrames = static_cast<ma_uint32>(blockSize);
    deviceConfig.dataCallback       = audioCallback;

    ma_device device;
    if (ma_device_init(nullptr, &deviceConfig, &device) != MA_SUCCESS) {
        std::cerr << "Error: Could not initialise audio device.\n";
        return 1;
    }

    std::cout << "\n=== Starting BPM Detection ===\n"
              << "Device:    " << device.capture.name << "\n"
              << "Rate:      " << device.sampleRate << " Hz\n"
              << "Preset:    " << presetName(presetId) << "\n"
              << "Resolution:" << resolutionName(resolution) << "\n\n";

    if (ma_device_start(&device) != MA_SUCCESS) {
        std::cerr << "Error: Could not start audio capture.\n";
        ma_device_uninit(&device);
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::thread inputListener(inputThread, &engine);

    double currentDisplayBpm = 0.0;
    double lastPrintedBpm = 0.0;
    const double CONF_THRESHOLD = 0.45;

    // --- Display loop (10 fps) ---
    while (running.load()) {
        auto r   = engine.getLatestResult();
        auto res = choiceToResolution(g_resolutionChoice.load());
        
        // Fetch current active parameters for UI smoothing dynamically
        livebpm::Config activeConf = presetConfig(g_presetId.load());
        double ALPHA = activeConf.uiSmoothing;
        double VISUAL_DEADZONE = activeConf.VISUAL_DEADZONE;
        double SNAP_RANGE = activeConf.magneticSnapRange;

        std::cout << "\r";
        if (r.hasValidEstimate) {
            
            // --- Smoothing Logic ---
            if (r.confidence >= CONF_THRESHOLD) {
                if (currentDisplayBpm == 0.0) currentDisplayBpm = r.bpm;
                currentDisplayBpm = (ALPHA * r.bpm) + ((1.0 - ALPHA) * currentDisplayBpm);
            }

            double targetDisplay = currentDisplayBpm;
            double nearestWhole = std::round(targetDisplay);
            if (std::abs(targetDisplay - nearestWhole) < SNAP_RANGE) {
                targetDisplay = nearestWhole; // Magnetic Snap
            }

            if (lastPrintedBpm != 0.0) {
                if (std::abs(targetDisplay - lastPrintedBpm) < VISUAL_DEADZONE) {
                    targetDisplay = lastPrintedBpm; // Prevent jitter
                } else {
                    lastPrintedBpm = targetDisplay;
                }
            } else {
                lastPrintedBpm = targetDisplay;
            }

            std::cout << "  BPM: "
                      << std::fixed << std::setprecision(1) << std::setw(6) << targetDisplay
                      << "  |  Conf: "
                      << std::setprecision(0) << std::setw(3) << (r.confidence * 100) << "%"
                      << "  |  " << std::setprecision(2) << std::setw(5) << r.processingTimeMs << " ms"
                      << "  |  [" << g_resolutionChoice.load() << "] " << resolutionName(res)
                      << "  |  " << presetName(g_presetId.load())
                      << "          ";
        } else {
            std::cout << "  Listening...  "
                      << "  [" << g_resolutionChoice.load() << "] " << resolutionName(res)
                      << "  |  " << presetName(g_presetId.load())
                      << "                    ";
        }
        std::cout << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "\n\nStopping...\n";
    ma_device_uninit(&device);
    g_engine = nullptr;
    
    // Attempt to close the input thread cleanly
    std::cout << "Press Enter once more to close completely...\n";
    if (inputListener.joinable()) inputListener.join();
    
    return 0;
}