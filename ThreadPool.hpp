#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    // the constructor just launches some amount of workers
    ThreadPool(size_t threads_n = std::thread::hardware_concurrency()) : stop(false)
    {
        if(!threads_n)
            throw std::invalid_argument("more than zero threads expected");

        this->workers.reserve(threads_n);
        for(; threads_n; --threads_n)
            this->workers.emplace_back(
                [this]
                {
                    while(true)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                [this]{ return this->stop || !this->tasks.empty(); });
                            if(this->stop && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                }
            );
    }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    // add new work item to the pool
    template<class F, class... Args>
    std::future<typename std::result_of<F(Args...)>::type> enqueue(F&& f, Args&&... args)
    {
        using packaged_task_t = std::packaged_task<typename std::result_of<F(Args...)>::type ()>;

        std::shared_ptr<packaged_task_t> task(new packaged_task_t(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            ));
        auto res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->tasks.emplace([task](){ (*task)(); });
        }
        this->condition.notify_one();
        return res;
    }
    // the destructor joins all threads
    virtual ~ThreadPool()
    {
        this->stop = true;
        this->condition.notify_all();
        for(std::thread& worker : this->workers)
            worker.join();
    }
private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;

    // synchronization
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic_bool stop;
};

#endif
