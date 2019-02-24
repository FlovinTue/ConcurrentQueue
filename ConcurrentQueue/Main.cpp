// ThreadSafeConsumptionQueue.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <string>
#include <vector>
#include <iostream>
#include "Tester.h"
#include <string>
//#include <vld.h>

class Thrower
{
public:
	Thrower() = default;
	Thrower(uint32_t aCount) : count(aCount){}
	Thrower& operator=(Thrower&&) = delete;
	Thrower& operator=(const Thrower& aOther)
	{
		count = aOther.count;
		if (rng() % 1000 == 1) {
			throw std::runtime_error("Test");
		}
		return *this;
	}
	Thrower& operator+=(uint32_t aCount)
	{
		count += aCount;
		return *this;
	}

	static std::random_device rnd;
	static std::mt19937 rng;
	uint32_t count = 0;
};
std::random_device Thrower::rnd;
std::mt19937 Thrower::rng(Thrower::rng());
void TestRun(uint32_t aRuns)
{
	Tester<int>* tester = new Tester<int>();

	double result = tester->ExecuteConcurrent(aRuns);

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

	std::cout << str << std::endl;

	delete tester;
}
int main()
{
	for (uint32_t i = 0; i < 800; ++i)
	{
		uint32_t runs(100000);

		TestRun(runs);
	}

	return 0;
}

