#ifndef THREAD_SAFE_QUEUE_H
#define THREAD_SAFE_QUEUE_H

#include <algorithm>
#include <mutex>
#include <queue>
#include <vector>

template <class T>
struct Thread_safe_queue {
	void push(T &&t) {
		std::lock_guard<std::mutex> lock(qm);
		q.push(std::move(t));
	}
	void push(const T &t) {
		std::lock_guard<std::mutex> lock(qm);
		q.push(t);
	}
	bool pop(T &t) {
		std::lock_guard<std::mutex> lock(qm);
		if (q.empty()) {
			return false;
		}
		t = std::move(q.front());
		q.pop();
		return true;
	}
	std::vector<T> pop_n(int n) {
		std::vector<T> retval;
		retval.reserve(n);
		std::lock_guard<std::mutex> lock(qm);
		for (n = std::min(n, static_cast<int>(q.size())); n; n--) {
			retval.emplace_back(std::move(q.front()));
			q.pop();
		}
		return retval;
	}
	bool empty() const {
		std::lock_guard<std::mutex> lock(qm);
		return q.empty();
	}
	int size() const {
		std::lock_guard<std::mutex> lock(qm);
		return q.size();
	}
	std::queue<T> &not_thread_safe_get() {
		return q;
	}

	private:
	mutable std::mutex qm;
	std::queue<T> q;
};

#endif