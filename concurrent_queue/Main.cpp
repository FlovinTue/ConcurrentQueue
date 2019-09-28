// ThreadSafeConsumptionQueue.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <string>
#include <vector>
#include <iostream>
#include "Tester.h"
#include <string>
#include <random>
#include <functional>

#include <vld.h>
#include <mutex>


#pragma warning(disable : 4324)

class Thrower
{
public:
#ifdef GDUL
	Thrower() = default;
	Thrower(uint32_t aCount) : count(aCount){}
	Thrower& operator=(Thrower&&) = default;
	Thrower& operator=(const Thrower& aOther)
	{
		count = aOther.count;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		throwTarget = aOther.throwTarget;
		if (!--throwTarget) {
			throwTarget = rand() % 5000;
			throw std::runtime_error("Test");
		}
#endif
		return *this;
	}
	Thrower& operator+=(uint32_t aCount)
	{
		count += aCount;
		return *this;
	}
#endif

	uint32_t count = 0;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	uint32_t throwTarget = rand() % 5000;
#endif
};
template <class T>
class tracking_allocator : public std::allocator<T>
{
public:
	tracking_allocator() = default;

	T* allocate(std::size_t count) {
		tracking_allocator<uint8_t>::ourAllocated += count * sizeof(T);
		T* ret = std::allocator<T>::allocate(count);
		spin();
		std::cout << "allocated " << count * sizeof(T) <<"--------------" << " new value: " << ourAllocated << std::endl;
		release();
		return ret;
	}
	void deallocate(T* obj, std::size_t count) {
		tracking_allocator<uint8_t>::ourAllocated -= count * sizeof(T);
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
	static std::atomic_flag ourSpin;
	static std::atomic<int64_t> ourAllocated;
};
template <class Allocator>
void TestRun(uint32_t aRuns, Allocator& alloc)
{
	std::cout << "Pre-run alloc value is: " << tracking_allocator<uint8_t>::ourAllocated << std::endl;

	Tester<Thrower, Allocator> tester(alloc);

	double result = tester.ExecuteConcurrent(aRuns);

	std::cout << "Post-run alloc value is: " << tracking_allocator<uint8_t>::ourAllocated << std::endl;

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
		std::string(" averaging ") +
		std::to_string(result / static_cast<double>(aRuns)) +
		std::string("s per " + std::to_string(Writes) + " read & write operations with a total time of ") +
		std::to_string(result) +
		std::string("s in ") +
		config);

	std::cout << str << "\n" << std::endl;

}
template <class T>
std::atomic_flag tracking_allocator<T>::ourSpin{ ATOMIC_FLAG_INIT };
template <class T>
std::atomic<int64_t> tracking_allocator<T>::ourAllocated(0LL);

int main()
{
	tracking_allocator<uint8_t> alloc;

		for (uint32_t i = 0; i < 400; ++i)
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


		std::cout << "\nAfter runs finished, the allocated value is: " << tracking_allocator<uint8_t>::ourAllocated << '\n' << std::endl;


	return 0;
}


