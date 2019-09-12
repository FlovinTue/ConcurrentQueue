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
#include <atomic_shared_ptr.h>

#include <vld.h>

#pragma warning(disable : 4324)
//
//class Thrower
//{
//public:
//	Thrower() = default;
//	Thrower(uint32_t aCount) : count(aCount){}
//	Thrower& operator=(Thrower&&) = delete;
//	Thrower& operator=(const Thrower& aOther)
//	{
//		count = aOther.count;
//#ifdef CQ_ENABLE_EXCEPTIONHANDLING
//		if (!--throwTarget) {
//			throwTarget = rand() % 5000;
//			throw std::runtime_error("Test");
//		}
//#endif
//		return *this;
//	}
//	Thrower& operator+=(uint32_t aCount)
//	{
//		count += aCount;
//		return *this;
//	}
//
//	uint32_t count = 0;
//#ifdef CQ_ENABLE_EXCEPTIONHANDLING
//	uint32_t throwTarget = rand() % 5000;
//#endif
//};
//void TestRun(uint32_t aRuns)
//{
//	aRuns;
//	Tester<Thrower>* tester = new Tester<Thrower>();
//
//	double result = tester->ExecuteConcurrent(aRuns);
//
//#ifdef _DEBUG
//	std::string config("Debug mode");
//#else
//	std::string config("Release mode");
//#endif
//	std::string str = std::string(
//		std::string("Executed a total of ") +
//		std::to_string(Writes * aRuns) +
//		std::string(" read & write operations") +
//		std::string(" averaging ") +
//		std::to_string(result / static_cast<double>(aRuns)) +
//		std::string("s per " + std::to_string(Writes) + " read & write operations with a total time of ") +
//		std::to_string(result) +
//		std::string("s in ") +
//		config);
//
//	std::cout << str << std::endl;
//
//	delete tester;
//}

template <class T, class Allocator>
class shared_ptr_allocator_adaptor : public Allocator
{
public:
	shared_ptr_allocator_adaptor(T* retAddr, std::size_t size)
		: myAddress(retAddr)
		, mySize(size)
	{
	};

	T* allocate(std::size_t /*count*/)
	{
		return myAddress;
	};
	void deallocate(T* /*addr*/, std::size_t /*count*/)
	{
		Allocator::deallocate(myAddress, mySize);
	}

private:
	T* const myAddress;
	std::size_t mySize;
};

typedef std::size_t size_type;
static const std::size_t Buffer_Capacity_Max = ~(std::numeric_limits<size_type>::max() >> 3) / 2;

typedef shared_ptr_allocator_adaptor<uint8_t, std::allocator<uint8_t>> allocator_adaptor_type;
typedef gdul::shared_ptr<gdul::cqdetail::producer_buffer<uint8_t>, shared_ptr_allocator_adaptor<uint8_t, std::allocator<uint8_t>>> shared_ptr_type;

template<class T, class Allocator>
inline shared_ptr_type create_producer_buffer(std::size_t withSize)
{
	const std::size_t log2size(gdul::cqdetail::log2_align(withSize, Buffer_Capacity_Max));

	const std::size_t alignOfControlBlock(alignof(gdul::aspdetail::control_block<void*, allocator_adaptor_type>));
	const std::size_t alignOfData(alignof(T));
	const std::size_t alignOfBuffer(alignof(gdul::cqdetail::producer_buffer<T>));

	const std::size_t maxAlignBuffData(alignOfBuffer < alignOfData ? alignOfData : alignOfBuffer);
	const std::size_t maxAlign(maxAlignBuffData < alignOfControlBlock ? alignOfControlBlock : maxAlignBuffData);

	const std::size_t bufferByteSize(sizeof(gdul::cqdetail::producer_buffer<T>));
	const std::size_t dataBlockByteSize(sizeof(gdul::cqdetail::item_container<T>) * log2size);
	const std::size_t controlBlockByteSize(shared_ptr_type::Alloc_Size_Claim);

	const std::size_t controlBlockSize(gdul::cqdetail::aligned_size(controlBlockByteSize, maxAlign));
	const std::size_t bufferSize(gdul::cqdetail::aligned_size(bufferByteSize, maxAlign));
	const std::size_t dataBlockSize(gdul::cqdetail::aligned_size(dataBlockByteSize, maxAlign));

	const std::size_t totalBlockSize(controlBlockSize + bufferSize + dataBlockSize + maxAlign);

	uint8_t* totalBlock(nullptr);

	gdul::cqdetail::producer_buffer<T>* buffer(nullptr);
	gdul::cqdetail::item_container<T>* data(nullptr);

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try
	{
#endif
		Allocator allocator;
		totalBlock = allocator.allocate(totalBlockSize);

		const std::size_t totalBlockBegin(reinterpret_cast<std::size_t>(totalBlock));
		const std::size_t controlBlockBegin(gdul::cqdetail::next_aligned_to(totalBlockBegin, maxAlign));
		const std::size_t bufferBegin(controlBlockBegin + controlBlockSize);
		const std::size_t dataBegin(bufferBegin + bufferSize);

		const std::size_t bufferOffset(bufferBegin - totalBlockBegin);
		const std::size_t dataOffset(dataBegin - totalBlockBegin);

		data = new (totalBlock + dataOffset) gdul::cqdetail::item_container<T>[log2size];
		buffer = new(totalBlock + bufferOffset) gdul::cqdetail::producer_buffer<T>(static_cast<size_type>(log2size), data);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...)
	{
		delete[] totalBlock;
		throw;
	}
#endif

	allocator_adaptor_type allocAdaptor(totalBlock, totalBlockSize);
	auto deleter = [](gdul::cqdetail::producer_buffer<T>* obj)
	{
		(*obj).~producer_buffer<T>();
	};

	shared_ptr_type returnValue(buffer, deleter, allocAdaptor);

	return returnValue;
}

int main()
{
	//	for (uint32_t i = 0; i < 800; ++i)
	//	{
	//#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	//		uint32_t runs(1000);
	//#else
	//		uint32_t runs(100000);
	//#endif
	//	
	//		TestRun(runs);
	//	}

	create_producer_buffer<uint8_t, std::allocator<uint8_t>>(245);

	return 0;
}