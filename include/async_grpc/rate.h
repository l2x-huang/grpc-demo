#pragma once

#include <chrono>

namespace agrpc {

class Rate {
public:
    Rate();
    Rate(float hz);

    void sleep() const;

private:
    const int duration_;
    mutable std::chrono::steady_clock::time_point start_;
};

} // namespace cc
