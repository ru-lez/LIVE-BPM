#pragma once

#include <vector>
#include <algorithm>
#include <cstddef>

namespace livebpm {
namespace dsp {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buffer_(capacity, T{}),
          capacity_(capacity),
          writePos_(0),
          available_(0) {}

    // Write samples into the buffer
    void write(const T* data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            buffer_[writePos_] = data[i];
            writePos_ = (writePos_ + 1) % capacity_;

            if (available_ < capacity_) {
                ++available_;
            }
        }
    }

    // Read the most recent 'count' samples in chronological order
    void readLatest(T* output, size_t count) const {
        if (count > available_) {
            count = available_;
        }

        size_t start = (writePos_ + capacity_ - count) % capacity_;

        for (size_t i = 0; i < count; ++i) {
            output[i] = buffer_[(start + i) % capacity_];
        }
    }

    size_t available() const {
        return available_;
    }

    size_t capacity() const {
        return capacity_;
    }

    void clear() {
        writePos_ = 0;
        available_ = 0;
        std::fill(buffer_.begin(), buffer_.end(), T{});
    }

private:
    std::vector<T> buffer_;
    size_t capacity_;
    size_t writePos_;
    size_t available_;
};

} // namespace dsp
} // namespace livebpm