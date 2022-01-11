#pragma once

#include <cassert>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <variant>

namespace agrpc {

namespace detail {
struct Blank {};
}  // namespace detail

template <typename T>
class Try {
public:
    Try() = default;

    template <typename U>
    explicit Try(U&& val) : val_(std::forward<U>(val)) {}

    template <typename U>
    Try<T>& operator=(U&& val) {
        val_ = std::forward<U>(val);
        return *this;
    }

    explicit Try(std::exception_ptr&& e) : val_(std::move(e)) {}

    const T& value() const& {
        check();
        return std::get<T>(val_);
    }

    operator const T&() const& { return value(); }
    operator T&() & { return value(); }
    operator T&&() && { return std::move(value()); }

    T& value() & {
        check();
        return std::get<T>(val_);
    }

    T&& value() && {
        check();
        return std::move(std::get<T>(val_));
    }

    std::exception_ptr& exception() {
        if (!has_exception()) {
            throw std::logic_error("not exception");
        }

        return std::get<2>(val_);
    }

    bool has_value() const { return val_.index() == 1; }

    bool has_exception() const { return val_.index() == 2; }

    template <typename R>
    R get() {
        return std::forward<R>(value());
    }

private:
    bool not_init() const { return val_.index() == 0; }

    void check() const {
        if (has_exception()) {
            std::rethrow_exception(std::get<2>(val_));
        } else if (not_init()) {
            throw std::logic_error("not init");
        }
    }

    std::variant<detail::Blank, T, std::exception_ptr> val_;
};

template <>
class Try<void> {
public:
    Try() { val_ = true; }

    explicit Try(std::exception_ptr&& e) : val_(std::move(e)) {}

    bool has_value() const { return val_.index() == 0; }

    bool has_exception() const { return val_.index() == 1; }

    template <typename R>
    R get() {
        return std::forward<R>(*this);
    }

private:
    std::variant<bool, std::exception_ptr> val_;
};

template <typename T>
struct TryWrapper {
    using type = Try<T>;
};

template <typename T>
struct TryWrapper<Try<T>> {
    using type = Try<T>;
};

template <typename T>
using try_type_t = typename TryWrapper<T>::type;
}  // namespace agrpc
