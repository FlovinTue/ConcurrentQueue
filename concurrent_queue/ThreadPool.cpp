#include "stdafx.h"
#include "ThreadPool.h"

ThreadPool::ThreadPool(uint32_t aThreads) :
	myIsInCommission(true),
	myTaskCounter(0)
{
	const std::size_t numThreads(std::thread::hardware_concurrency());
	for (uint32_t i = 0; i < aThreads; ++i) {
		myThreads.push_back(std::thread(&ThreadPool::Idle, this));
	}
}


ThreadPool::~ThreadPool()
{
	Decommission();
}

void ThreadPool::AddTask(std::function<void()> aWorkFunction)
{
	++myTaskCounter;
	myTaskQueue.push(aWorkFunction);
}

void ThreadPool::Decommission()
{
	myIsInCommission = false;

	for (size_t i = 0; i < myThreads.size(); ++i)
		myThreads[i].join();

	myThreads.clear();
}

bool ThreadPool::HasUnfinishedTasks() const
{
	return 0 < myTaskCounter._My_val;
}
void ThreadPool::Idle()
{

	while (myIsInCommission._My_val | (0 < myTaskCounter._My_val))
	{
		std::function<void()> task;
		if (myTaskQueue.try_pop(task))
		{
			task();
			--myTaskCounter;
		}
		else
		{
			std::this_thread::yield();
		}
	}
}
