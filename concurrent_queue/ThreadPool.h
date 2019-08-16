#pragma once

#include <functional>
#include <thread>
#include <atomic>
#include <concurrent_queue.h>
#include <vector>

class ThreadPool
{
public:
	ThreadPool(uint32_t aThreads = std::thread::hardware_concurrency());
	~ThreadPool();

	void AddTask(std::function<void()> aWorkFunction);
	void Decommission();

	bool HasUnfinishedTasks() const;
private:
	void Idle();

	std::vector<std::thread> myThreads;

	concurrency::concurrent_queue<std::function<void()>> myTaskQueue;
	//ConcurrentQueue<std::function<void()>> myTaskQueue;

	std::atomic<bool> myIsInCommission;

	std::atomic<uint32_t> myTaskCounter;
};

