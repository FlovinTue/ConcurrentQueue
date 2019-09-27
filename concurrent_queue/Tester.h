#pragma once

#include <thread>
#include "ThreadPool.h"
#include "concurrent_queue.h"
#include "Timer.h"
#include <concurrent_queue.h>
#include <queue>
#include <mutex>
#include <concurrentqueue.h>


// Test queue

#define GDUL
//#define MOODYCAMEL
//#define MSC_RUNTIME
//#define MTX_WRAPPER

template <class T>
class queue_mutex_wrapper
{
public:

	bool try_pop(T& out){
		mtx.lock();
		if (myQueue.empty()) {
			mtx.unlock();
			return false;
		}
		out = myQueue.front();
		myQueue.pop();
		mtx.unlock();
		return true;
	}
	void push(T& in) { mtx.lock(); myQueue.push(in); mtx.unlock(); }

	std::mutex mtx;
	std::queue<T> myQueue;
};

const uint32_t Writes = 2048;
const uint32_t Writers = std::thread::hardware_concurrency() / 2;
const uint32_t Readers = std::thread::hardware_concurrency() / 2;
const uint32_t WritesPerThread(Writes / Writers);
const uint32_t ReadsPerThread(Writes / Readers);

template <class T, class Allocator>
class Tester
{
public:
	Tester(Allocator& alloc);
	~Tester();

	double ExecuteConcurrent(uint32_t aRuns);

private:
	bool CheckResults() const;

	void Write();
	void Read();

#ifdef GDUL
	gdul::concurrent_queue<T, Allocator> myQueue;
#elif defined(MSC_RUNTIME)
	concurrency::concurrent_queue<T> myQueue;
#elif defined(MOODYCAMEL)
	moodycamel::ConcurrentQueue<T> myQueue;
#elif defined(MTX_WRAPPER)
	queue_mutex_wrapper<T> myQueue;
#endif

	ThreadPool myWriter;
	ThreadPool myReader;

	std::atomic<bool> myIsRunning;
	std::atomic<uint32_t> myWrittenSum;
	std::atomic<uint32_t> myReadSum;
	std::atomic<uint32_t> myThrown;
};

template<class T, class Allocator>
inline Tester<T, Allocator>::Tester(Allocator& alloc) :
	myIsRunning(false),
	myWriter(Writers, 0),
	myReader(Readers, Writers),
	myWrittenSum(0),
	myReadSum(0),
	myThrown(0)
#ifdef GDUL
	, myQueue(alloc)
#endif
{
	srand(static_cast<uint32_t>(time(0)));
}

template<class T, class Allocator>
inline Tester<T, Allocator>::~Tester()
{
	myWriter.Decommission();
	myReader.Decommission();
}
template<class T, class Allocator>
inline double Tester<T, Allocator>::ExecuteConcurrent(uint32_t aRuns)
{
	double result(0.0);

	for (uint32_t i = 0; i < aRuns; ++i) {

		for (uint32_t j = 0; j < Writers; ++j)
			myWriter.AddTask(std::bind(&Tester::Write, this));
		for (uint32_t j = 0; j < Readers; ++j)
			myReader.AddTask(std::bind(&Tester::Read, this));

		myWrittenSum = 0;
		myReadSum = 0;
		myThrown = 0;

		Timer timer;
		myIsRunning = true;

		while (myWriter.HasUnfinishedTasks() | myReader.HasUnfinishedTasks())
			std::this_thread::yield();

#ifdef GDUL
		myQueue.unsafe_clear();
#elif defined(MSC_RUNTIME)
		myQueue.clear();
#endif

		result += timer.GetTotalTime();

		myIsRunning = false;
	}

	std::cout << "Threw " << myThrown;
	if (!CheckResults()) {
		std::cout << " and failed check";
	}
	std::cout << std::endl;

	return result;
}
template<class T, class Allocator>
inline bool Tester<T, Allocator>::CheckResults() const
{
	if (myWrittenSum != myReadSum)
		return false;

	return true;
}
template<class T, class Allocator>
inline void Tester<T, Allocator>::Write()
{
#ifdef GDUL
	myQueue.reserve(WritesPerThread);
#endif

	while (!myIsRunning);

	uint32_t sum(0);

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	for (int j = 0; j < WritesPerThread; ) {
		const T in(rand());
		try {
			myQueue.push(in);
			++j;
			sum += in.count;
		}
		catch (...) {
			++myThrown;
		}
	}
#else
	for (uint32_t j = 0; j < WritesPerThread; ++j) {
		T in;
		in.count = 1;
#endif
#ifndef MOODYCAMEL
		myQueue.push(in);
#else
		myQueue.enqueue(in);
#endif
		sum += in.count;
	}
	myWrittenSum += sum;
}

template<class T, class Allocator>
inline void Tester<T, Allocator>::Read()
{
	while (!myIsRunning);

	uint32_t sum(0);

	T out;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	for (int j = 0; j < ReadsPerThread;) {
		try {
			if (myQueue.try_pop(out)) {
				++j;
				sum += out.count;
			}
		}
		catch (...) {
			++myThrown;
		}
	}

#else
	for (uint32_t j = 0; j < ReadsPerThread; ++j) {
		while (true) {
#ifndef MOODYCAMEL
			if (myQueue.try_pop(out)) {
#else
				if (myQueue.try_dequeue(out)) {
#endif
				sum += out.count;
				break;
			}
			else {
				std::this_thread::yield();
			}
		}
	}
#endif
	myReadSum += sum;
}

