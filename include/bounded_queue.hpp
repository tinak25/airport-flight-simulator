// A thread-safe bounded FIFO queue (the "bounded buffer" of the
// producer-consumer pattern).
//
//  * push() blocks while the queue is full.
//  * pop() blocks while the queue is empty, and returns std::nullopt once the
//    queue has been closed and drained.
//  * close() is called once, after ALL producers have finished. This is what
//    makes it safe with several producers: a single shared "finished" flag set
//    by the first producer to finish would make consumers quit while other
//    producers are still sending.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity == 0) throw std::invalid_argument("capacity must be at least 1");
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // Returns false if the queue was closed (the item is not added).
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_.wait(lock, [&] { return items_.size() < capacity_ || closed_; });
        if (closed_) return false;
        items_.push_back(std::move(item));
        lock.unlock();               // notify after unlocking so the woken
        not_empty_.notify_one();     // thread does not immediately block on the mutex
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [&] { return !items_.empty() || closed_; });
        if (items_.empty()) return std::nullopt;  // closed and drained
        T item = std::move(items_.front());
        items_.pop_front();
        lock.unlock();
        not_full_.notify_one();
        return item;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    std::size_t capacity() const { return capacity_; }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::deque<T> items_;
    bool closed_ = false;
};
