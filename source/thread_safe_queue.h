#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <utility>

template<class T>
class ThreadSafeQueue
{
public:
    explicit ThreadSafeQueue(size_t capacity = 1000) : mCapacity{ capacity } {}
    // Push an element. Block if queue is full.
    void push(T elem);
    // Pop an element and return a copy. Block if queue is empty.
    T pop();

    size_t size();

private:
    std::mutex mMutex;
    std::condition_variable mNotEmpty;
    std::condition_variable mNotFull;
    std::queue<T> mQueue;
    const size_t mCapacity;
};

template<class T>
void ThreadSafeQueue<T>::push(T elem)
{
    std::unique_lock<std::mutex> lck(mMutex);
    mNotFull.wait(lck, [this] { return mQueue.size() < mCapacity; });

    mQueue.push(std::forward<T>(elem));
    lck.unlock();

    mNotEmpty.notify_one();
}

template<class T>
T ThreadSafeQueue<T>::pop()
{
    std::unique_lock<std::mutex> lck(mMutex);
    mNotEmpty.wait(lck, [this] { return !mQueue.empty(); });

    T elem = mQueue.front();
    mQueue.pop();
    lck.unlock();

    mNotFull.notify_one();
    return elem;
}