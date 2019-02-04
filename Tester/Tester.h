#pragma once

#include <thread>
#include "ThreadPool.h"
#include "ConcurrentQueue.h"
#include "Timer.h"


const uint32_t Buffers = 4;
const uint32_t BufferSizes = 512;
const uint32_t TotalBuffer = Buffers * BufferSizes;
const uint32_t Writes = TotalBuffer;
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

	ConcurrentQueue<T> myQueue;

	ThreadPool myWorker;

	std::atomic<bool> myIsRunning;
	//std::atomic<T> myWrittenSum;
	//std::atomic<T> myReadSum;
};

template<class T>
inline Tester<T>::Tester() :
	myIsRunning(false),
	myWorker(Writers + Readers)/*,
	myWrittenSum(0),
	myReadSum(0)*/
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

		Timer timer;
		myIsRunning = true;

		while (myWorker.HasUnfinishedTasks())
			std::this_thread::yield();

		result += timer.GetTotalTime();

		myIsRunning = false;
	}

	if (!CheckResults()) {
		std::cout << "failed check" << std::endl;
	}
	return result;
}
template<class T>
inline bool Tester<T>::CheckResults() const
{
	//if (myWrittenSum != myReadSum)
	//	return false;

	return true;
}
template<class T>
inline void Tester<T>::Write()
{
	while (!myIsRunning);

	//const T in(static_cast<T>(rand()));


	for (int j = 0; j < WritesPerThread; ++j) {
		const T in = 1244;
		myQueue.Push(in);


	}
	//myWrittenSum += in * WritesPerThread;
}

template<class T>
inline void Tester<T>::Read()
{
	while (!myIsRunning);

	//T sum(static_cast<T>(0));

	T out;
	for (int j = 0; j < ReadsPerThread; ++j) {
		while (true) {

			if (myQueue.TryPop(out))
				break;


			//if (myQueue.TryPop(out)) {
			//	//sum += out;
			//	break;
			//}
			//else
			//	std::this_thread::yield();
		}
	}
	//myReadSum += sum;
}

