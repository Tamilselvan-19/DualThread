#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T>
class ThreadSafeQueue {
public:
    void Push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stop) return;
            m_queue.push(value);
        }
        m_cv.notify_one();
    }

    bool Pop(T& value) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return !m_queue.empty() || m_stop; });
        if (m_queue.empty()) return false;
        value = m_queue.front();
        m_queue.pop();
        return true;
    }

    // Waits for an item for at most timeoutMs. Returns std::nullopt on timeout
    // or when the queue has been stopped and drained.
    std::optional<T> WaitPop(int timeoutMs) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [this] { return !m_queue.empty() || m_stop; })) {
            return std::nullopt;
        }
        if (m_queue.empty()) return std::nullopt;
        T value = m_queue.front();
        m_queue.pop();
        return value;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::queue<T> empty;
        m_queue.swap(empty);
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
};
