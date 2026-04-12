#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace livebpm { namespace dsp {

class Biquad {
public:
    struct Coefficients {
        float b0 = 1, b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;
    };

    void setCoefficients(const Coefficients& c) { c_ = c; }

    float process(float input)
    {
        float output = c_.b0 * input + c_.b1 * x1_ + c_.b2 * x2_
                     - c_.a1 * y1_  - c_.a2 * y2_;
        x2_ = x1_; x1_ = input;
        y2_ = y1_; y1_ = output;
        return output;
    }

    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

    static Coefficients makeHighpass(double sr, double freq, double Q = 0.707)
    {
        double w0    = 2.0 * M_PI * freq / sr;
        double alpha = std::sin(w0) / (2.0 * Q);
        double cosw0 = std::cos(w0);
        double a0    = 1.0 + alpha;

        return {
            static_cast<float>((1.0 + cosw0) / 2.0 / a0),
            static_cast<float>(-(1.0 + cosw0) / a0),
            static_cast<float>((1.0 + cosw0) / 2.0 / a0),
            static_cast<float>(-2.0 * cosw0 / a0),
            static_cast<float>((1.0 - alpha) / a0)
        };
    }

private:
    Coefficients c_;
    float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

}} // namespace livebpm::dsp