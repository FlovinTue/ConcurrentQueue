// ThreadSafeConsumptionQueue.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <string>
#include <vector>
#include <iostream>


#define GDUL
//#define MOODYCAMEL
//#define MSC_RUNTIME
//#define MTX_WRAPPER

#include "Tester.h"
#include <string>
#include <random>
#include <functional>

//#include <vld.h>
#include <mutex>


#pragma warning(disable : 4324)

namespace gdul
{
static std::random_device rd;
static std::mt19937 rng(rd());
}

class Thrower
{
public:
#ifdef GDUL
	Thrower() = default;
	Thrower(std::uint32_t aCount) : count(aCount){}
	Thrower& operator=(Thrower&&) = default;
	Thrower& operator=(const Thrower& aOther)
	{
		count = aOther.count;
#ifdef GDUL_CQ_ENABLE_EXCEPTIONHANDLING
		throwTarget = aOther.throwTarget;
		if (!--throwTarget) {
			throwTarget = gdul::rng() % 50000;
			throw std::runtime_error("Test");
		}
#endif
		return *this;
	}
	Thrower& operator+=(std::uint32_t aCount)
	{
		count += aCount;
		return *this;
	}
#endif

	uint32_t count = 0;
#ifdef GDUL_CQ_ENABLE_EXCEPTIONHANDLING
	uint32_t throwTarget = gdul::rng() % 50000;
#endif
};

static std::atomic_flag ourSpin{ ATOMIC_FLAG_INIT };
static std::atomic<int64_t> ourAllocated(0);
template <class T>
class tracking_allocator : public std::allocator<T>
{
public:
	tracking_allocator() = default;

	template <typename U>
	struct rebind
	{
		using other = tracking_allocator<U>;
	};
	tracking_allocator(const tracking_allocator<T>&) = default;
	tracking_allocator& operator=(const tracking_allocator<T>&) = default;

	T* allocate(std::size_t count) {
		ourAllocated += count * sizeof(T);
		T* ret = std::allocator<T>::allocate(count);
		spin();
		std::cout << "allocated " << count * sizeof(T) <<"--------------" << " new value: " << ourAllocated << std::endl;
		release();
		return ret;
	}
	void deallocate(T* obj, std::size_t count) {
		ourAllocated -= count * sizeof(T);
		std::allocator<T>::deallocate(obj, count);
		spin();
		std::cout << "deallocated " << count * sizeof(T) << "--------------" << " new value: " << ourAllocated << std::endl;
		release();
	}
	void spin()
	{
		while (ourSpin.test_and_set()){
			std::this_thread::sleep_for(std::chrono::microseconds(10));
		}
	}
	void release()
	{
		ourSpin.clear();
	}
	template <class Other>
	tracking_allocator(tracking_allocator<Other>&) {};
};
template <class Allocator>
void TestRun(std::uint32_t aRuns, Allocator& alloc)
{
	std::cout << "Pre-run alloc value is: " << ourAllocated << std::endl;

	Tester<Thrower, Allocator> tester(alloc);

	double concurrentRes(0.0);
	double singleRes(0.0);
	double writeRes(0.0);
	double readRes(0.0);
	double singleProdSingleCon(0.0);

	concurrentRes = tester.ExecuteConcurrent(aRuns);
	singleRes = tester.ExecuteSingleThread(aRuns);
	writeRes = tester.ExecuteWrite(aRuns);
	readRes = tester.ExecuteRead(aRuns);
	singleProdSingleCon = tester.ExecuteSingleProducerSingleConsumer(aRuns);

	std::cout << "\n";
#ifdef _DEBUG
	std::string config("Debug mode");
#else
	std::string config("Release mode");
#endif
	std::string str = std::string(
		std::string("Executed a total of ") +
		std::to_string(Writes * aRuns) +
		std::string(" read & write operations") +
		std::string("\n") +
		std::string("Averaging results \n") +
		std::string("Concurrent (") + std::to_string(Writers) + std::string("Writers, ") + std::to_string(Readers) + std::string("Readers): ") +
		std::string(std::to_string(concurrentRes / static_cast<double>(aRuns))) + std::string("			// total: ") + std::to_string(concurrentRes) +
		std::string("s\n") +
		std::string("Concurrent (") + std::to_string(1) + std::string("Writers, ") + std::to_string(1) + std::string("Readers): ") +
		std::string(std::to_string(singleProdSingleCon / static_cast<double>(aRuns))) + std::string("			// total: ") + std::to_string(singleProdSingleCon) +
		std::string("s\n") +
		std::string("Single thread: ") +
		std::string(std::to_string(singleRes / static_cast<double>(aRuns))) + std::string("						// total: ") + std::to_string(singleRes) +
		std::string("s\n") +
		std::string("Pure writes: (") + std::to_string(Writers) + std::string("): ") +
		std::string(std::to_string(writeRes / static_cast<double>(aRuns))) + std::string("					// total: ") + std::to_string(writeRes) +
		std::string("s\n") +
		std::string("Pure reads: (") + std::to_string(Readers) + std::string("): ") +
		std::string(std::to_string(readRes / static_cast<double>(aRuns))) + std::string("					// total: ") + std::to_string(readRes) +
		std::string("s\n") +
		std::string("per " + std::to_string(Writes) + " operations in ") +
		config);

	std::cout << str << "\n" << std::endl;

	std::cout << "Post-run alloc value is: " << ourAllocated << std::endl;

}

int main()
{
	//tracking_allocator<std::uint8_t> alloc;
	std::allocator<uint8_t> alloc;
	
	for (std::uint32_t i = 0; i < 6; ++i)
		{
	#ifdef CQ_ENABLE_EXCEPTIONHANDLING
#ifdef NDEBUG
			uint32_t runs(10000);
#else
			uint32_t runs(100);
#endif
	#else
			#ifdef NDEBUG
			uint32_t runs(10000);
#else
			uint32_t runs(1000);
#endif
	#endif
		
			TestRun(runs, alloc);
		}


		std::cout << "\nAfter runs finished, the allocated value is: " << ourAllocated << '\n' << std::endl;

	return 0;
}

