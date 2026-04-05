#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace log_committer {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads);
    ~ThreadPool();

    void enqueue(std::function<void()> task);
    size_t size() const { return workers_.size(); }

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex                        queue_mutex_;
    std::condition_variable           cv_;
    std::atomic<bool>                 stop_ { false };
};

} // namespace log_committer
