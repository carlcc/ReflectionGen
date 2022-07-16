#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

struct ParseTask {
    std::string inputFile;
    std::string outputFile;
};

class ParseTaskQueue {
public:
    explicit ParseTaskQueue(size_t capacity)
        : capacity_ { capacity }
    {
    }

    void Push(ParseTask* item)
    {
        {
            std::unique_lock<std::mutex> lck(mutex_);
            produceCv_.wait(lck, [this]() { return queue_.size() < capacity_; });
            queue_.push(item);
        }
        consumeCv_.notify_one();
    }

    ParseTask* Pop()
    {
        ParseTask* ret;
        {
            std::unique_lock<std::mutex> lck(mutex_);
            consumeCv_.wait(lck, [this]() { return !queue_.empty(); });
            ret = queue_.front();
            queue_.pop();
        }
        produceCv_.notify_one();
        return ret;
    }

private:
    std::queue<ParseTask*> queue_;
    std::mutex mutex_;
    std::condition_variable produceCv_;
    std::condition_variable consumeCv_;
    size_t capacity_;
};