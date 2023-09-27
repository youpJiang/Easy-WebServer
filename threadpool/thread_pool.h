#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include<exception>
#include "../lock/locker.h"

template<typename T>
class ThreadPool final{
public:
    ThreadPool(int threadnum = 8,int maxtask = 1000);
    ~ThreadPool();
    bool append(T* task);

private:
    static void *worker(void *arg);
    void run();
private:

    //end the pool or not.
    bool m_stop;
    //num of threads
    int m_thread_number;
    // max request
    int m_max_requests;
    //task queue
    //TODO: use shared_ptr to optimise it.
    std::list<T *>m_workqueue;
    //Availability of tasks to be addressed
    Sem m_queuestat;
    //threads 
    pthread_t *m_threads;
    //locker which protect the task queue.
    Locker m_queuelocker;
};

template<typename T>
ThreadPool<T>::ThreadPool(int threadnum, int maxtask):m_thread_number(threadnum),m_max_requests(maxtask),m_stop(false), m_threads(NULL)
{
    if(0 >= threadnum || 0 >= maxtask) 
        throw std::exception();
    m_threads = new pthread_t[m_thread_number];
    if(!m_threads) 
        throw std::exception();
    for(int i = 0; i < threadnum; ++i){
        if(0 != pthread_create(m_threads + i, NULL, worker, this)){
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
ThreadPool<T>::~ThreadPool(){
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool ThreadPool<T>::append(T *task){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(task);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void *ThreadPool<T>::worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

template<typename T>
void ThreadPool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *task = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!task)
            continue;
        task->process();
    }
}
#endif