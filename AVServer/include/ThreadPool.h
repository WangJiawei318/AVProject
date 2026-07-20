#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class ThreadPool
{
public:
    ThreadPool(std::size_t coreThreadCount,
               std::size_t maxThreadCount,
               std::size_t maxQueueSize,
               std::chrono::seconds idleTimeout);
    ~ThreadPool();

    bool start();
    bool submit(const std::function<void()> &task);
    void stop();

    std::size_t workerCount() const;
    std::size_t idleWorkerCount() const;
    std::size_t pendingTaskCount() const;

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    struct Worker
    {
        Worker(std::size_t workerId, bool isCore);

        std::size_t id;
        bool core;
        bool finished;
        std::thread thread;
    };

    bool addWorkerLocked(bool coreWorker);
    void workerLoop(Worker *worker);
    void markWorkerFinishedLocked(Worker *worker,
                                  const char *reason);
    void reapFinishedWorkers();

private:
    const std::size_t m_coreThreadCount;
    const std::size_t m_maxThreadCount;
    const std::size_t m_maxQueueSize;
    const std::chrono::seconds m_idleTimeout;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<std::function<void()> > m_tasks;
    std::vector<std::unique_ptr<Worker> > m_workers;
    std::size_t m_currentThreadCount;
    std::size_t m_idleThreadCount;
    std::size_t m_nextWorkerId;
    bool m_started;
    bool m_stopping;
};

#endif // THREADPOOL_H
