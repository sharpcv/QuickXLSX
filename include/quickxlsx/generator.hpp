#pragma once
#include <coroutine>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>
namespace quickxlsx {
/** Move-only, single-pass coroutine sequence. Destroying it cancels and destroys suspended work. */
template<typename T>
class Generator {
public:
    /** Coroutine promise implementation used by producer functions. */ struct promise_type;
    /** Owning coroutine handle type. */ using handle_type = std::coroutine_handle<promise_type>;
    /** Constructs an empty sequence. */ Generator() noexcept = default;
    /** Takes ownership of coroutine. */ explicit Generator(handle_type coroutine) noexcept : coroutine_(coroutine) {}
    /** Generators cannot be copied. */ Generator(const Generator&) = delete;
    /** Generators cannot be copied. */ Generator& operator=(const Generator&) = delete;
    /** Transfers coroutine ownership; the source becomes empty. */ Generator(Generator&& other) noexcept : coroutine_(std::exchange(other.coroutine_, {})) {}
    /** Destroys current coroutine and transfers ownership; the source becomes empty. */
    Generator& operator=(Generator&& other) noexcept { if (this != &other) { if (coroutine_) coroutine_.destroy(); coroutine_ = std::exchange(other.coroutine_, {}); } return *this; }
    /** Destroys the coroutine and its current yielded value. */ ~Generator() { if (coroutine_) coroutine_.destroy(); }

    /** Single-pass iterator; references remain valid only until increment or generator destruction. */
    class iterator {
    public:
        /** Iterator category. */ using iterator_category = std::input_iterator_tag;
        /** Produced value type. */ using value_type = T;
        /** Iterator distance type. */ using difference_type = std::ptrdiff_t;
        /** Ephemeral yielded reference type. */ using reference = const T&;
        /** Ephemeral yielded pointer type. */ using pointer = const T*;
        /** Constructs an end iterator. */ iterator() noexcept = default;
        /** Binds to a generator coroutine without taking ownership. */ explicit iterator(handle_type coroutine) noexcept : coroutine_(coroutine) {}
        /** Returns current value; invalid before begin successfully resumes. */ reference operator*() const noexcept { return *coroutine_.promise().current_; }
        /** Returns current value pointer with the same lifetime as operator*. */ pointer operator->() const noexcept { return std::addressof(**this); }
        /** Resumes to the next value and rethrows producer exceptions at their observation point. */
        iterator& operator++() { coroutine_.resume(); if (coroutine_.done()) coroutine_.promise().rethrow_if_failed(); return *this; }
        /** Advances; this input iterator has no prior-value result. */ void operator++(int) { ++*this; }
        /** Tests whether iteration is exhausted. */ friend bool operator==(const iterator& value, std::default_sentinel_t) noexcept { return !value.coroutine_ || value.coroutine_.done(); }
    private: handle_type coroutine_{};
    };

    /** Starts or resumes production and returns the first iterator; producer exceptions propagate. */
    iterator begin() { if (!coroutine_) return {}; coroutine_.resume(); if (coroutine_.done()) coroutine_.promise().rethrow_if_failed(); return iterator(coroutine_); }
    /** Returns the stateless end sentinel. */ std::default_sentinel_t end() const noexcept { return {}; }

    /** Standard coroutine promise storing one owning yielded value and any deferred exception. */
    struct promise_type {
        /** Returns a Generator owning this promise's coroutine. */ Generator get_return_object() noexcept { return Generator(handle_type::from_promise(*this)); }
        /** Defers execution until iteration begins. */ std::suspend_always initial_suspend() const noexcept { return {}; }
        /** Keeps completed state inspectable until Generator destruction. */ std::suspend_always final_suspend() const noexcept { return {}; }
        /** Moves a yielded value into promise-owned storage. */ std::suspend_always yield_value(T value) noexcept { current_.emplace(std::move(value)); return {}; }
        /** Completes a producer returning no explicit value. */ void return_void() const noexcept {}
        /** Captures a producer exception for rethrow by iteration. */ void unhandled_exception() noexcept { exception_ = std::current_exception(); }
        /** Rethrows the captured producer exception, if any. */ void rethrow_if_failed() const { if (exception_) std::rethrow_exception(exception_); }
        /** Current yielded value. */ std::optional<T> current_;
        /** Deferred producer exception. */ std::exception_ptr exception_;
    };
private: handle_type coroutine_{};
};
} // namespace quickxlsx
