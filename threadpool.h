#ifndef __THREADPOOL_H_
#define __THREADPOOL_H_

#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "safequeue.h"

class threadpool {
private:
    bool m_shutdown;
    safequeue<std::function<void()>> m_queue;
    std::vector<std::thread> m_threads;
    std::mutex m_condition_mutex;
    std::condition_variable m_condition;

    class threadworker {
    private:
        int m_id;
        threadpool* m_pool;

    public:
        threadworker(threadpool* pool, const int id) : m_pool(pool), m_id(id) {  }

        void operator()() {
            std::function<void()> func;
            bool result;

            while (!m_pool->m_shutdown) {
                {
                    std::unique_lock<std::mutex> lock(m_pool->m_condition_mutex);
                    if (m_pool->m_queue.empty()) {
                        m_pool->m_condition.wait(lock);
                    }
                    result = m_pool->m_queue.dequeue(func);
                }

                if (result) {
                    func();
                }
            }
        }
    };

public:
    threadpool(const int n_threads) : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false) {  }

    threadpool(const threadpool&) = delete;

    threadpool(threadpool &&) = delete;

    threadpool& operator=(const threadpool&) = delete;

    threadpool& operator=(threadpool &&) = delete;

    void init() {
        for (int i = 0; i < m_threads.size(); ++i) {
            m_threads[i] = std::thread(threadworker(this, i));
        }
    }

    void shutdown() {
        m_shutdown = true;
        m_condition.notify_all();

        for (int i = 0; i < m_threads.size(); ++i) {
            if (m_threads[i].joinable()) {
                m_threads[i].join();
            }
        }
    }

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        std::function<void()> wrapper_func = [task_ptr]() {
            (*task_ptr)();
        };

        m_queue.enqueue(wrapper_func);
        m_condition.notify_one();

        return task_ptr->get_future();
    }
};


#endif
