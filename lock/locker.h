#ifndef LOCKER_H
#define LOCKER_H

#include<semaphore.h>
#include<pthread.h>
#include<exception>

class Sem
{
public:
    Sem()
    {
        if(0 != sem_init(&m_sem, 0, 0))
        {
            throw std::exception();
        }
    }
    Sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~Sem()
    {
        sem_destroy(&m_sem);
    }

    bool wait()
    {
        return 0 == sem_wait(&m_sem);
    }
    bool post()
    {
        return 0 == sem_post(&m_sem);
    }
private:
    sem_t m_sem;

};

class Locker
{
public:
    Locker()
    {
        if(0 != pthread_mutex_init(&m_mutex, NULL))
            throw std::exception();
    }
    ~Locker(){
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return 0 == pthread_mutex_lock(&m_mutex);
    }
    bool unlock()
    {
        return 0 == pthread_mutex_unlock(&m_mutex);
    }
    pthread_mutex_t *get(){
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};
class Cond
{
public:
    Cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~Cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif 