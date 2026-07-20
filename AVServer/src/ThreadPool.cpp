#include "ThreadPool.h"

#include <cstdio>
#include <exception>
#include <utility>

ThreadPool::Worker::Worker(std::size_t workerId, bool isCore)
    : id(workerId),
      core(isCore),
      finished(false)
{
}

ThreadPool::ThreadPool(std::size_t coreThreadCount,
                       std::size_t maxThreadCount,
                       std::size_t maxQueueSize,
                       std::chrono::seconds idleTimeout)
    : m_coreThreadCount(coreThreadCount),
      m_maxThreadCount(maxThreadCount),
      m_maxQueueSize(maxQueueSize),
      m_idleTimeout(idleTimeout),
      m_currentThreadCount(0),
      m_idleThreadCount(0),
      m_nextWorkerId(0),
      m_started(false),
      m_stopping(false)
{
}

ThreadPool::~ThreadPool()
{
    stop();
}

bool ThreadPool::start()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_started)
        return true;
    if (m_coreThreadCount == 0 ||
            m_maxThreadCount < m_coreThreadCount ||
            m_maxQueueSize == 0 ||
            m_idleTimeout.count() <= 0) {
        return false;
    }

    m_stopping = false;
    m_started = true;
    for (std::size_t index = 0; index < m_coreThreadCount; ++index) {
        if (!addWorkerLocked(true)) {
            m_stopping = true;
            m_condition.notify_all();
            lock.unlock();
            stop();
            return false;
        }
    }
    return true;
}

bool ThreadPool::submit(const std::function<void()> &task)
{
    if (!task)
        return false;

    reapFinishedWorkers();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || m_stopping || m_tasks.size() >= m_maxQueueSize)
        return false;

    m_tasks.push_back(task);
    if (m_tasks.size() > m_idleThreadCount &&
            m_currentThreadCount < m_maxThreadCount) {
        if (addWorkerLocked(false)) {
            std::printf("thread pool expanded current=%zu\n",
                        m_currentThreadCount);
        }
    }
    m_condition.notify_one();
    return true;
}

void ThreadPool::stop()
{
    std::vector<std::unique_ptr<Worker> > workers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started && m_workers.empty())
            return;
        m_stopping = true;
        m_condition.notify_all();
        workers.swap(m_workers);
    }

    for (std::size_t index = 0; index < workers.size(); ++index) {
        if (workers[index]->thread.joinable())
            workers[index]->thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_tasks.clear();
    m_currentThreadCount = 0;
    m_idleThreadCount = 0;
    m_started = false;
}

std::size_t ThreadPool::workerCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentThreadCount;
}

std::size_t ThreadPool::idleWorkerCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_idleThreadCount;
}

std::size_t ThreadPool::pendingTaskCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_tasks.size();
}

bool ThreadPool::addWorkerLocked(bool coreWorker)
{
    if (m_currentThreadCount >= m_maxThreadCount)
        return false;

    std::unique_ptr<Worker> worker(new Worker(++m_nextWorkerId, coreWorker));
    Worker *workerPointer = worker.get();
    ++m_currentThreadCount;
    ++m_idleThreadCount;
    try {
        worker->thread = std::thread(&ThreadPool::workerLoop,
                                     this,
                                     workerPointer);
    } catch (const std::exception &error) {
        --m_currentThreadCount;
        --m_idleThreadCount;
        std::printf("worker creation failed error=%s\n", error.what());
        return false;
    }

    std::printf("worker created id=%zu core=%s\n",
                worker->id,
                worker->core ? "true" : "false");
    m_workers.push_back(std::move(worker));
    return true;
}

void ThreadPool::workerLoop(Worker *worker)
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (worker->core) {
                m_condition.wait(lock, [this]() {
                    return m_stopping || !m_tasks.empty();
                });
            } else {
                const bool ready = m_condition.wait_for(
                            lock,
                            m_idleTimeout,
                            [this]() {
                    return m_stopping || !m_tasks.empty();
                });
                if (!ready && m_tasks.empty() &&
                        m_currentThreadCount > m_coreThreadCount) {
                    markWorkerFinishedLocked(worker, "idle_timeout");
                    return;
                }
            }

            if (m_stopping && m_tasks.empty()) {
                markWorkerFinishedLocked(worker, "shutdown");
                return;
            }
            if (m_tasks.empty())
                continue;

            task = m_tasks.front();
            m_tasks.pop_front();
            if (m_idleThreadCount > 0)
                --m_idleThreadCount;
        }

        try {
            task();
        } catch (const std::exception &error) {
            std::printf("worker task exception id=%zu error=%s\n",
                        worker->id,
                        error.what());
        } catch (...) {
            std::printf("worker task exception id=%zu error=unknown\n",
                        worker->id);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_idleThreadCount;
        }
    }
}

void ThreadPool::markWorkerFinishedLocked(Worker *worker,
                                          const char *reason)
{
    if (m_idleThreadCount > 0)
        --m_idleThreadCount;
    if (m_currentThreadCount > 0)
        --m_currentThreadCount;
    worker->finished = true;
    std::printf("worker exiting id=%zu reason=%s\n",
                worker->id,
                reason);
    if (!worker->core && !m_stopping) {
        std::printf("thread pool shrunk current=%zu\n",
                    m_currentThreadCount);
    }
}

void ThreadPool::reapFinishedWorkers()
{
    std::vector<std::unique_ptr<Worker> > finished;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<std::unique_ptr<Worker> >::iterator it = m_workers.begin();
        while (it != m_workers.end()) {
            if ((*it)->finished) {
                finished.push_back(std::move(*it));
                it = m_workers.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (std::size_t index = 0; index < finished.size(); ++index) {
        if (finished[index]->thread.joinable())
            finished[index]->thread.join();
    }
}
