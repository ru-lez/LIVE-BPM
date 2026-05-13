#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "livebpm/dsp/Biquad.h"

#include "livebpm/Engine.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cmath>

// Globals

static std::atomic<bool> running{true};
static livebpm::Engine*  g_engine = nullptr;


// Callbacks

void signalHandler(int)
{
    running.store(false);
}

// Highpass filter instance (removes DC offset and rumble below 80Hz)
static livebpm::dsp::Biquad highpass;
static bool filterInitialized = false;

void audioCallback(ma_device* /*device*/, void* /*output*/,
                   const void* input, ma_uint32 frameCount)
{
    if (g_engine && input) {
        // Initialize filter once on first callback
        if (!filterInitialized) {
            highpass.setCoefficients(
                livebpm::dsp::Biquad::makeHighpass(48000.0, 80.0));
            filterInitialized = true;
        }

        const float* in = static_cast<const float*>(input);

        // Filter into a temporary buffer, don't modify miniaudio's buffer
        std::vector<float> filtered(frameCount);
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            filtered[i] = highpass.process(in[i]);
        }

        g_engine->processBlock(filtered.data(), static_cast<int>(frameCount));
    }
}


// Helper functions

void listDevices()
{
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::cerr << "Error: Could not initialize audio context.\n";
        return;
    }

    ma_device_info* captureDevices;
    ma_uint32        captureCount;
    ma_device_info* playbackDevices;
    ma_uint32        playbackCount;

    if (ma_context_get_devices(&context,
                                &playbackDevices, &playbackCount,
                                &captureDevices,  &captureCount) != MA_SUCCESS) {
        std::cerr << "Error: Could not enumerate audio devices.\n";
        ma_context_uninit(&context);
        return;
    }

    std::cout << "Available capture devices:\n";
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        std::cout << "  [" << i << "] " << captureDevices[i].name;
        if (captureDevices[i].isDefault)
            std::cout << "  (default)";
        std::cout << "\n";
    }

    ma_context_uninit(&context);
}

void printUsage()
{
    std::cout << "LiveBPM - Real-time BPM detector\n"
              << "Usage: livebpm_cli [options]\n"
              << "  -r <samplerate>   Sample rate   (default: 48000)\n"
              << "  -b <blocksize>    Block size    (default: 512)\n"
              << "  -d <index>        Device index  (default: system default)\n"
              << "  -l                List available capture devices\n"
              << "  -h                Show help\n"
              << "\nPress Ctrl+C to exit.\n";
}


// Main
int main(int argc, char* argv[])
{
    // --- Argument parsing ---
    double sampleRate  = 48000.0;
    int    blockSize   = 512;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-r" && i + 1 < argc) {
            sampleRate = std::stod(argv[++i]);
        } else if (arg == "-b" && i + 1 < argc) {
            blockSize = std::stoi(argv[++i]);
        } else if (arg == "-h") {
            printUsage();
            return 0;
        }
    }

    // --- Configure DSP engine ---
    livebpm::Config config;
    config.sampleRate = sampleRate;
    config.blockSize  = blockSize;

    livebpm::Engine engine(config);
    g_engine = &engine;

    // --- List devices and ask user to pick one ---
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        std::cerr << "Error: Could not initialize audio context.\n";
        return 1;
    }

    ma_device_info* captureDevices;
    ma_uint32        captureCount;
    ma_device_info* playbackDevices;
    ma_uint32        playbackCount;

    if (ma_context_get_devices(&context,
                                &playbackDevices, &playbackCount,
                                &captureDevices,  &captureCount) != MA_SUCCESS) {
        std::cerr << "Error: Could not enumerate audio devices.\n";
        ma_context_uninit(&context);
        return 1;
    }

    // Print available devices
    std::cout << "=== LiveBPM CLI ===\n\n"
              << "Available capture devices:\n";
    for (ma_uint32 i = 0; i < captureCount; ++i) {
        std::cout << "  [" << i << "] " << captureDevices[i].name;
        if (captureDevices[i].isDefault)
            std::cout << "  (default)";
        std::cout << "\n";
    }

    // Ask user to pick a device
    int deviceIndex = -1;
    while (deviceIndex < 0 || deviceIndex >= static_cast<int>(captureCount)) {
        std::cout << "\nEnter device number (or press Enter for default): ";
        std::string input;
        std::getline(std::cin, input);

        // If user just pressed Enter use the default
        if (input.empty()) {
            deviceIndex = -1;
            break;
        }

        try {
            deviceIndex = std::stoi(input);
            if (deviceIndex < 0 || deviceIndex >= static_cast<int>(captureCount)) {
                std::cout << "Invalid choice. Please enter a number between 0 and "
                          << captureCount - 1 << ".\n";
                deviceIndex = -1;
            }
        } catch (...) {
            std::cout << "Invalid input. Please enter a number.\n";
            deviceIndex = -1;
        }
    }

    // Store selected device ID
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

    // --- Configure audio capture device ---
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format     = ma_format_f32;
    deviceConfig.capture.channels   = 1;
    deviceConfig.capture.pDeviceID  = selectedDeviceId;
    deviceConfig.sampleRate         = static_cast<ma_uint32>(sampleRate);
    deviceConfig.periodSizeInFrames = static_cast<ma_uint32>(blockSize);
    deviceConfig.dataCallback       = audioCallback;

    ma_device device;
    ma_result  result = ma_device_init(nullptr, &deviceConfig, &device);

    if (result != MA_SUCCESS) {
        std::cerr << "Error: Could not initialize audio device (" << result << ")\n"
                  << "Check that you have a microphone connected and permissions granted.\n";
        return 1;
    }

    // --- Print startup info ---
    std::cout << "\n=== Starting BPM Detection ===\n"
              << "Active device: " << device.capture.name << "\n"
              << "Sample rate:   " << device.sampleRate   << " Hz\n"
              << "Block size:    " << blockSize            << "\n"
              << "Press Ctrl+C to exit.\n\n";

    // --- Start capture ---
    result = ma_device_start(&device);
    if (result != MA_SUCCESS) {
        std::cerr << "Error: Could not start audio capture.\n";
        ma_device_uninit(&device);
        return 1;
    }

    std::signal(SIGINT, signalHandler);

    double currentDisplayBpm = 0.0;
    double lastPrintedBpm = 0.0;

    const double ALPHA = 0.15;
    const double CONF_THRESHOLD = 0.45;
    const double VISUAL_DEADZONE = 0.2;

    // --- Main display loop (10 fps) ---
    while (running.load()) {
        auto bpmResult = engine.getLatestResult();

        std::cout << "\r";

        if (bpmResult.hasValidEstimate) {
            if (bpmResult.confidence >= CONF_THRESHOLD) {
                if (currentDisplayBpm == 0.0) {
                    currentDisplayBpm = bpmResult.bpm;
                }
                currentDisplayBpm = (ALPHA * bpmResult.bpm) + ((1.0 - ALPHA) * currentDisplayBpm);
            }

            double targetDisplayValue = currentDisplayBpm;
            // Magnetic Snapping
            double nearestWhole = std::round(targetDisplayValue);
            if (std::abs(targetDisplayValue - nearestWhole) < 0.25) {
                targetDisplayValue = nearestWhole;
            }

            if (lastPrintedBpm != 0.0) {
                if (std::abs(targetDisplayValue - lastPrintedBpm) < VISUAL_DEADZONE) {
                    targetDisplayValue = lastPrintedBpm; // Lock to the current display to stop jitter
                } else {
                    lastPrintedBpm = targetDisplayValue; // Value exceeded deadzone, update display
                }
            } else {
                lastPrintedBpm = targetDisplayValue;
            }

            std::cout << "  BPM: "
                      << std::fixed << std::setprecision(1) << std::setw(6)
                      << targetDisplayValue
                      << "  |  Confidence: "
                      << std::setprecision(0) << std::setw(3)
                      << (bpmResult.confidence * 100) << "%"
                      << "  |  "
                      << std::setprecision(2)
                      << bpmResult.processingTimeMs << " ms";
        } else {
            std::cout << "  Listening... (play a steady rhythm)      ";
        }

        std::cout << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // --- Cleanup ---
    std::cout << "\n\nStopping...\n";
    ma_device_uninit(&device);
    g_engine = nullptr;

    return 0;
}