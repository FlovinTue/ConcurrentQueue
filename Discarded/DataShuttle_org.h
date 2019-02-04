#pragma once

#include "GrowingArray.h"
#include <functional>
#include <atomic>


// Internals
// --------------------------------------------

namespace DataShuttleInternals
{
	class Producer
	{
	public:
		Producer() :
			myCurrentBufferSlot(-1),
			myDoInit(true),
			myExit([](void) {})
		{
		}
		~Producer()
		{
			myExit();
		}
		Producer(Producer& aProducer)
		{
			myCurrentBufferSlot = aProducer.myCurrentBufferSlot;
			myDoInit = aProducer.myDoInit;
			myExit = aProducer.myExit;
			aProducer.myExit = [](void) {};
		}
		Producer& operator=(Producer& aProducer)
		{
			myCurrentBufferSlot = aProducer.myCurrentBufferSlot;
			myDoInit = aProducer.myDoInit;
			myExit = aProducer.myExit;
			aProducer.myExit = [](void) {};
			return *this;
		}

		bool myDoInit;
		int32_t myCurrentBufferSlot;
		std::function<void()> myExit;
	};
	struct Consumer
	{
		bool myDoInit = true;
		static std::atomic<int32_t> ourStartBufferIterator;
		int32_t myCurrentBufferSlot = -1;
	};
	std::atomic<int32_t> Consumer::ourStartBufferIterator(0);

	template <class T, uint32_t Capacity>
	class Buffer
	{
	public:
		Buffer();
		~Buffer() = default;
		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		inline bool RegisterProducer();
		inline void UnregisterProducer();

		inline bool Avaliable() const;

		inline int32_t Size() const;

		inline bool TryPush(const T& aIn);
		inline bool TryPop(T& aOut);
	private:

		uint32_t myWriteSlot;
		uint8_t myPadding0[64 - (sizeof(std::atomic<uint32_t>) % 64)];
		std::atomic<uint32_t> myReadSlot;
		uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
		std::atomic<uint32_t> myReadIterator;
		uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
		std::atomic<bool> myHasOwner;
		uint8_t myPadding3[64 - (sizeof(std::atomic<bool>) % 64)];
		std::atomic<int32_t> myAvaliableEntries;
		uint8_t myPadding4[64 - (sizeof(std::atomic<int32_t>) % 64)];
		T myData[Capacity];

	};
	template<class T, uint32_t Capacity>
	inline Buffer<T, Capacity>::Buffer() :
		myHasOwner(false),
		myWriteSlot(0),
		myReadSlot(0),
		myAvaliableEntries(0),
		myReadIterator(0)
	{
	}
	template<class T, uint32_t Capacity>
	inline bool Buffer<T, Capacity>::RegisterProducer()
	{
		bool expected(false);
		return myHasOwner.compare_exchange_strong(expected, true);
	}
	template<class T, uint32_t Capacity>
	inline bool Buffer<T, Capacity>::Avaliable() const
	{
		return (myWriteSlot < Capacity) & (!myHasOwner);
	}
	template<class T, uint32_t Capacity>
	inline int32_t Buffer<T, Capacity>::Size() const
	{
		return myAvaliableEntries;
	}
	template<class T, uint32_t Capacity>
	inline void Buffer<T, Capacity>::UnregisterProducer()
	{
		myHasOwner = false;
	}
	template<class T, uint32_t Capacity>
	inline bool Buffer<T, Capacity>::TryPush(const T & aIn)
	{
		if (myWriteSlot == Capacity)
			return false;

		myData[myWriteSlot] = aIn;

		++myWriteSlot;
		++myAvaliableEntries;

		return true;
	}
	template<class T, uint32_t Capacity>
	inline bool Buffer<T, Capacity>::TryPop(T & aOut)				// REWRITE ????????
	{
		if (!(0 < myAvaliableEntries--))
		{
			++myAvaliableEntries;
			return false;
		}

		aOut = myData[myReadSlot++];

		if (++myReadIterator == Capacity)
		{
			myWriteSlot = 0;
			myReadSlot = 0;
			myReadIterator = 0;
		}

		return true;
	}
}
// --------------------------------------------

template <class T, uint32_t BufferSizes = 64, uint32_t MaxBuffers = 128>
class DataShuttle
{
public:
	DataShuttle(uint32_t aInitBuffers = 4);
	~DataShuttle();
	DataShuttle(const DataShuttle&) = delete;
	DataShuttle& operator=(const DataShuttle&) = delete;

	bool TryPush(const T& aIn);
	bool TryPop(T& aOut);

	uint32_t Size() const;

private:
	void InitConsumer();
	void InitProducer();
	void ExitProducer();
	bool RelocateProducer();
	bool RelocateConsumer();

	int32_t FindAvaliable();
	int32_t AllocateNew();

	static const int32_t InvalidIndex = -1;

	static thread_local GrowingArray<DataShuttleInternals::Producer> ourProducers;
	static thread_local GrowingArray<DataShuttleInternals::Consumer> ourConsumers;

	static std::atomic<uint32_t> ourIdIterator;

	const uint32_t myObjectId;
	const uint32_t myBufferSizes;

	const std::thread::id myOwnerId;

	__declspec(align(64)) DataShuttleInternals::Buffer<T, BufferSizes>* myBuffers[MaxBuffers];

	std::atomic<uint32_t> mySize;
	uint8_t myPadding0[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myBufferCount;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myBufferReservations;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
};
// Static init
template <class T, uint32_t BufferSizes, uint32_t MaxBuffers>
std::atomic<uint32_t> DataShuttle<T, BufferSizes, MaxBuffers>::ourIdIterator = 0;
template <class T, uint32_t BufferSizes, uint32_t MaxBuffers>
thread_local GrowingArray<DataShuttleInternals::Producer> DataShuttle<T, BufferSizes, MaxBuffers>::ourProducers(4);
template <class T, uint32_t BufferSizes, uint32_t MaxBuffers>
thread_local GrowingArray<DataShuttleInternals::Consumer> DataShuttle<T, BufferSizes, MaxBuffers>::ourConsumers(4);
// -----------

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline DataShuttle<T, BufferSizes, MaxBuffers>::DataShuttle(uint32_t aInitBuffers) :
	myObjectId(ourIdIterator++),
	myBufferSizes(BufferSizes),
	myBufferCount(0),
	myBufferReservations(0),
	mySize(0),
	myOwnerId(std::this_thread::get_id())
{
	for (uint32_t i = 0; i < aInitBuffers; ++i)
		AllocateNew();
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline DataShuttle<T, BufferSizes, MaxBuffers>::~DataShuttle()
{
	for (uint32_t i = 0; i < myBufferCount && i < MaxBuffers; ++i)
		delete[] myBuffers[i];
	memset(myBuffers, 0, sizeof(myBuffers));
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline bool DataShuttle<T, BufferSizes, MaxBuffers>::TryPush(const T & aIn)
{
	if (!(myObjectId < ourProducers.Size()))
		ourProducers.Resize(myObjectId + 1);

	DataShuttleInternals::Producer& producer = ourProducers[myObjectId];

	if (producer.myDoInit)
		InitProducer();

	while (!myBuffers[producer.myCurrentBufferSlot]->TryPush(aIn))
		if (!RelocateProducer())
			return false;

	++mySize;
	return true;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline bool DataShuttle<T, BufferSizes, MaxBuffers>::TryPop(T & aOut)
{
	if (!(myObjectId < ourConsumers.Size()))
		ourConsumers.Resize(myObjectId + 1);

	DataShuttleInternals::Consumer& consumer = ourConsumers[myObjectId];

	if (consumer.myDoInit)
		InitConsumer();

	while (!myBuffers[consumer.myCurrentBufferSlot]->TryPop(aOut))
		if (!RelocateConsumer())
			return false;
	
	--mySize;
	return true;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline uint32_t DataShuttle<T, BufferSizes, MaxBuffers>::Size() const
{
	return mySize;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline void DataShuttle<T, BufferSizes, MaxBuffers>::InitConsumer()
{
	ourConsumers[myObjectId].myDoInit = false;
	ourConsumers[myObjectId].myCurrentBufferSlot = DataShuttleInternals::Consumer::ourStartBufferIterator++ % myBufferCount;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline void DataShuttle<T, BufferSizes, MaxBuffers>::InitProducer()
{
	ourProducers[myObjectId].myCurrentBufferSlot = InvalidIndex;

	if (!RelocateProducer())
		return;

	ourProducers[myObjectId].myDoInit = false;
	if (std::this_thread::get_id() != myOwnerId)
		ourProducers[myObjectId].myExit = std::bind(&DataShuttle::ExitProducer, this);
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline void DataShuttle<T, BufferSizes, MaxBuffers>::ExitProducer()
{
	myBuffers[ourProducers[myObjectId].myCurrentBufferSlot]->UnregisterProducer();
	ourProducers[myObjectId].myCurrentBufferSlot = InvalidIndex;
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline bool DataShuttle<T, BufferSizes, MaxBuffers>::RelocateProducer()
{
	int32_t nextBufferSlot(InvalidIndex);
	for (;;)
	{
		nextBufferSlot = FindAvaliable();

		if (nextBufferSlot != InvalidIndex)
			if (myBuffers[nextBufferSlot]->RegisterProducer())
				break;
			else
				continue;

		nextBufferSlot = AllocateNew();

		if (nextBufferSlot == InvalidIndex)
			return false;

		if (myBuffers[nextBufferSlot]->RegisterProducer())
			break;
	}

	if (ourProducers[myObjectId].myCurrentBufferSlot != InvalidIndex)
		myBuffers[ourProducers[myObjectId].myCurrentBufferSlot]->UnregisterProducer();

	ourProducers[myObjectId].myCurrentBufferSlot = nextBufferSlot;
	return true;
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline bool DataShuttle<T, BufferSizes, MaxBuffers>::RelocateConsumer()
{
	int32_t startBufferSlot = ourConsumers[myObjectId].myCurrentBufferSlot;
	int32_t buffers(myBufferCount);

	for (int32_t i = 1; i < buffers; ++i)
	{
		int32_t bufferSlot = (startBufferSlot + i) % buffers;
		if (0 < myBuffers[bufferSlot]->Size())
		{
			ourConsumers[myObjectId].myCurrentBufferSlot = bufferSlot;
			return true;
		}
	}
	return false;
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline int32_t DataShuttle<T, BufferSizes, MaxBuffers>::FindAvaliable()
{
	int32_t bufferCount(static_cast<int32_t>(myBufferCount));

	int32_t startBufferSlot(ourProducers[myObjectId].myCurrentBufferSlot);

	for (int32_t i = 1; i < bufferCount + 1; ++i)
	{
		int32_t bufferSlot((startBufferSlot + i) % bufferCount);

		if (myBuffers[bufferSlot] && myBuffers[bufferSlot]->Avaliable())
			return bufferSlot;
	}
	return InvalidIndex;
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline int32_t DataShuttle<T, BufferSizes, MaxBuffers>::AllocateNew() 
{
	int32_t newBufferSlot = static_cast<int32_t>(myBufferReservations++);

	assert(newBufferSlot < MaxBuffers && "Tried to allocate beyond max buffers. Increase max buffers?");
	if (!(newBufferSlot < MaxBuffers))
		return InvalidIndex;

	DataShuttleInternals::Buffer<T, BufferSizes>* buffer = new DataShuttleInternals::Buffer<T, BufferSizes>;

	myBuffers[newBufferSlot] = buffer;

	++myBufferCount;

	return newBufferSlot;
}
