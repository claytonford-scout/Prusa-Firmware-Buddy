/// \file
#pragma once

#include <inplace_function.hpp>

#include <utils/uncopyable.hpp>

template <typename... Args>
class Subscriber;

/// Point for registering callbacks to
/// Represented as a linked list
/// !!! Not thread-safe
template <typename... Args>
class Publisher : Uncopyable {

public:
    using Subscriber = ::Subscriber<Args...>;
    friend Subscriber;

public:
    /// Calls callbacks of all registered Subscribers
    /// The execution order depends on the insertion order - newer hooks execute first.
    /// Warning - if a hook removes itself during the call, it can cause UB or crash.
    void call_all(Args &&...args) {
        for (auto it = first_; it; it = it->next_) {
            it->callback_(std::forward<Args>(args)...);
        }
    }

private:
    void insert(Subscriber *item) {
        item->next_ = first_;
        first_ = item;
    }

    void remove(Subscriber *item) {
        Subscriber **current = &first_;
        while (*current != item) {
            assert(*current);
            current = &((*current)->next_);
        }
        *current = (*current)->next_;
    }

private:
    Subscriber *first_ = nullptr;
};

/// Guard that registers the provided callback to the specified point
/// The hook gets removed when the function is destroyed
/// !!! Not thread safe
template <typename... Args>
class Subscriber : Uncopyable {

public:
    using Callback = stdext::inplace_function<void(Args...)>;
    using Publisher = ::Publisher<Args...>;
    friend Publisher;

public:
    // Note: Template deducation problems without the "auto"
    Subscriber(Publisher &publisher, const auto &cb)
        : publisher_(publisher)
        , callback_(cb) {
        publisher.insert(this);
    }

    ~Subscriber() {
        publisher_.remove(this);
    }

private:
    Publisher &publisher_;
    Subscriber *next_ = nullptr;
    Callback callback_;
};
