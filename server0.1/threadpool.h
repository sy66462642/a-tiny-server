#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include"locker.h"
#include<cstdio>


//a thread templete T:task class
template<typename T>
class ThreadPool{
    public:
    ThreadPool(int num=8,int max_req=1000);
    ~ThreadPool();
    bool appendTask(T* request);

    private:
    static void * worker(void *args);
    void run();

    private:
    int threadNum; //threadnum
    pthread_t *threads;//threadslist
    int maxRequests;//max waiting requests
    std::list<T*> taskQueue; //taskqueue
    Locker queueLock; //unique lock
    Semaphore queueStatus;  //juging the task
    bool stop; //when to stop

};

template<typename T>
ThreadPool<T>::ThreadPool(int num,int max_req):
threadNum(num),maxRequests(max_req),stop(false),threads(NULL)
{
    if(threadNum<=0||maxRequests<=0)
    throw std::exception();
    threads=new pthread_t[threadNum];
    if(! threads)
    {
        throw std::exception();
    }

    for(int i=0;i<threadNum;i++)
    {
        printf("%d thread\n",i);
        if(pthread_create(threads+i ,NULL,worker,this)!=0)
        {
            delete [] threads;
            throw std::exception();
        }
        if(pthread_detach(threads[i]))
        {
            delete [] threads;
            throw std::exception();
        }

    }
}


template<typename T>
ThreadPool<T>::~ThreadPool()
{
    delete [] threads;
    stop=true;
}


template<typename T>
bool ThreadPool<T>::appendTask(T *req)
{
    queueLock.lock();
    if(taskQueue.size()>maxRequests)
    {
        queueLock.unLock();
        return false;
    }
    taskQueue.push_back(req);
    queueLock.unLock();
    queueStatus.post();
    return true;

}

template<typename T>
void * ThreadPool<T>::worker(void * arg)
{
    ThreadPool *pool=(ThreadPool *) arg;
    pool->run();
    return pool;
}

template<typename T>
void ThreadPool<T>:: run()
{
    while(!stop)
    {
        queueStatus.wait();
        queueLock.lock();
        if(taskQueue.empty())
        {
            queueLock.unLock();
            continue;
        }
        T* request=taskQueue.front();
        taskQueue.pop_front();
        queueLock.unLock();
        if(!request)
        {
            continue;
        }
        request->process();
    }
}


#endif