#pragma once

#include <thread>
#include "ThreadPool.h"
#include "concurrent_queue_.h"
#include "Timer.h"

const uint32_t Writes = 2048;
const uint32_t Writers = 8;
const uint32_t Readers = 16;
const uint32_t WritesPerThread(Writes / Writers);
const uint32_t ReadsPerThread(Writes / Readers);

template <class T>
class Tester
{
public:
	Tester();
	~Tester();

	double ExecuteConcurrent(uint32_t aRuns);

private:
	bool CheckResults() const;

	void Write();
	void Read();

	cq::concurrent_queue<T> myQueue;

	ThreadPool myWorker;

	std::atomic<bool> myIsRunning;
	std::atomic<uint32_t> myWrittenSum;
	std::atomic<uint32_t> myReadSum;
	std::atomic<uint32_t> myThrown;
};

template<class T>
inline Tester<T>::Tester() :
	myIsRunning(false),
	myWorker(Writers + Readers),
	myWrittenSum(0),
	myReadSum(0),
	myThrown(0)
{
	srand(static_cast<uint32_t>(time(0)));
}

template<class T>
inline Tester<T>::~Tester()
{
	myWorker.Decommission();
}
template<class T>
inline double Tester<T>::ExecuteConcurrent(uint32_t aRuns)
{
	double result(0.0);

	for (uint32_t i = 0; i < aRuns; ++i) {

		for (uint32_t j = 0; j < Writers; ++j)
			myWorker.AddTask(std::bind(&Tester::Write, this));
		for (uint32_t j = 0; j < Readers; ++j)
			myWorker.AddTask(std::bind(&Tester::Read, this));

		myWrittenSum = 0;
		myReadSum = 0;
		myThrown = 0;

		Timer timer;
		myIsRunning = true;

		while (myWorker.HasUnfinishedTasks())
			std::this_thread::yield();

		myQueue.unsafe_clear();

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
template<class T>
inline bool Tester<T>::CheckResults() const
{
	if (myWrittenSum != myReadSum)
		return false;

	return true;
}
template<class T>
inline void Tester<T>::Write()
{
	while (!myIsRunning);

	uint32_t sum(0);

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

	//for (int j = 0; j < WritesPerThread; ++j) {
	//	T in(1);
	//	myQueue.push(in);
	//	sum += in;
	//}
	myWrittenSum += sum;
}

template<class T>
inline void Tester<T>::Read()
{
	while (!myIsRunning);

	uint32_t sum(0);

	T out;
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

	//for (int j = 0; j < ReadsPerThread; ++j) {
	//	while (true) {
	//		if (myQueue.try_pop(out)) {
	//			sum += out;
	//			break;
	//		}
	//	}
	//}
	myReadSum += sum;
}

