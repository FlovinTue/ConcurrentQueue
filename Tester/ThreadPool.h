#pragma once

#include <functional>
#include <thread>
#include "ConcurrentQueue.h"
#include <atomic>

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

	ConcurrentQueue<std::function<void()>> myTaskQueue;

	std::atomic<bool> myIsInCommission;

	std::atomic<uint32_t> myTaskCounter;
};

