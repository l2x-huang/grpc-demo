// Use a monotonic clock for measuring intervals
// refer: https://stackoverflow.com/a/55234186
#include "async_grpc/rate.h"
#include <chrono>
#include <limits.h>
#include <thread>

namespace {

template <class T>
auto clamp(T a, T min, T max) {
    return a < min ? min : (a > max ? max : a);
}

}  // namespace

namespace agrpc {

Rate::Rate() : Rate(10) {
}

Rate::Rate(float hz)
  : duration_(1000 / hz)
  , start_(std::chrono::steady_clock::now()) {
}

void Rate::sleep() const {
    int duration = clamp(duration_, 1, INT_MAX);
    int cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start_)
                   .count();
    int left = duration - cost;
    left = clamp(left, 0, duration);
    if (left > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(left));
    }
    //     start_ += std::chrono::milliseconds(duration);
    start_ = std::chrono::steady_clock::now();
}

}  // namespace agrpc
