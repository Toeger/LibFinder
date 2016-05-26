#pragma once

#include <mutex>
#include <queue>

template <class T>
struct Thread_safe_queue {
    void push(T &&t) {
        std::unique_lock<std::mutex> lock(qm);
        q.push(std::move(t));
    }
    void push(const T &t) {
        std::unique_lock<std::mutex> lock(qm);
        q.push(t);
    }
    bool pop(T &t) {
        std::unique_lock<std::mutex> lock(qm);
        if (q.empty()) {
            return false;
        }
        t = std::move(q.front());
        q.pop();
        return true;
    }
    bool empty() const {
        std::unique_lock<std::mutex> lock(qm);
        return q.empty();
    }

    private:
    mutable std::mutex qm;
    std::queue<T> q;
};
