#ifndef LOCKER_H
#define LOCKER_H
#include<pthread.h>
#include<exception>
#include<semaphore.h>

//threads syn class

//unique Lock
class Locker{
    public:
        Locker()
        {
            if(pthread_mutex_init(&mutex,NULL)!=0)
            {
                throw std::exception();
            }
        }
        ~Locker()
        {
            pthread_mutex_destroy(&mutex);
        }
        bool lock()
        {
            return pthread_mutex_lock(&mutex)==0;
        }
        bool unLock()
        {
            return pthread_mutex_unlock(&mutex)==0;
        }
        pthread_mutex_t *get()
        {
            return &mutex;
        }

    private:
        pthread_mutex_t mutex;
};

//conditional vars
class Condition
{
    public:
    Condition()
    {
        if(pthread_cond_init(&cond,NULL)!=0)
        throw std::exception();
    }
    ~Condition()
    {
        pthread_cond_destroy(&cond);
    }
    bool wait(pthread_mutex_t *mutex)
    {
        return pthread_cond_wait(&cond,mutex)==0;
    }
    bool wait(pthread_mutex_t *mutex,struct timespec spec)
    {
        return pthread_cond_timedwait(&cond,mutex,&spec)==0;
    }
    bool signal()
    {
        return pthread_cond_signal(&cond)==0;
    }
    bool broadcast()
    {
        return pthread_cond_broadcast(&cond)==0;
    }
    private:
    pthread_cond_t cond;
};

//semaphore
class Semaphore
{
    public:
    Semaphore()
    {
        if(sem_init(&sem,0,0)!=0)
        throw std::exception();
    }
    Semaphore(int num)
    {
        if(sem_init(&sem,0,num)!=0)
        throw std::exception();
    }
    ~Semaphore()
    {
        sem_destroy(&sem);
    }
    bool wait()
    {
        return sem_wait(&sem)==0;
    }
    bool post()
    {
        return sem_post(&sem)==0;
    }
    private:
    sem_t sem;
};
#endif