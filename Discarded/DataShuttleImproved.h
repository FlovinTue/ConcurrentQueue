#pragma once

#include <GrowingArray.h>
#include <functional>
#include <atomic>
#include <cassert>
#include <memory>

// Internals
// --------------------------------------------

namespace DataShuttleImprovedInternals
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
	class InternalQueue
	{
	public:
		InternalQueue();
		~InternalQueue() = default;
		InternalQueue(const InternalQueue&) = delete;
		InternalQueue& operator=(const InternalQueue&) = delete;

		inline bool RegisterProducer();
		inline void UnregisterProducer();

		inline bool Avaliable() const;

		inline int32_t Size() const;

		inline bool TryPush(const T& aIn);
		inline bool TryPop(T& aOut);
	private:
		__declspec(align(64)) uint32_t myWriteSlot;
		__declspec(align(64)) std::atomic<uint32_t> myReadSlot;
		__declspec(align(64)) std::atomic<uint32_t> myReadIterator;
		__declspec(align(64)) std::atomic<bool> myHasOwner;
		__declspec(align(64)) std::atomic<int32_t> myAvaliableEntries;
		__declspec(align(64)) T myData[Capacity];
	};
	template<class T, uint32_t Capacity>
	inline InternalQueue<T, Capacity>::InternalQueue() :
		myHasOwner(false),
		myWriteSlot(0),
		myReadSlot(0),
		myAvaliableEntries(0),
		myReadIterator(0)
	{
	}
	template<class T, uint32_t Capacity>
	inline bool InternalQueue<T, Capacity>::RegisterProducer()
	{
		bool expected(false);
		return myHasOwner.compare_exchange_strong(expected, true);
	}
	template<class T, uint32_t Capacity>
	inline bool InternalQueue<T, Capacity>::Avaliable() const
	{
		return (myWriteSlot < Capacity) & (!myHasOwner);
	}
	template<class T, uint32_t Capacity>
	inline int32_t InternalQueue<T, Capacity>::Size() const
	{
		return myAvaliableEntries;
	}
	template<class T, uint32_t Capacity>
	inline void InternalQueue<T, Capacity>::UnregisterProducer()
	{
		myHasOwner = false;
	}
	template<class T, uint32_t Capacity>
	inline bool InternalQueue<T, Capacity>::TryPush(const T & aIn)
	{
		if (myWriteSlot == Capacity)
			return false;

		myData[myWriteSlot] = aIn;

		++myWriteSlot;
		++myAvaliableEntries;

		return true;
	}
	template<class T, uint32_t Capacity>
	inline bool InternalQueue<T, Capacity>::TryPop(T & aOut)
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

template <class T, uint32_t BufferSizes = 64, uint32_t MaxBuffers = 16, uint32_t MaxProducers = 16>
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

	static thread_local GrowingArray<DataShuttleImprovedInternals::Producer> ourProducers;
	static thread_local GrowingArray<DataShuttleImprovedInternals::Consumer> ourConsumers;

	static std::atomic<uint32_t> ourIdIterator;

	const uint32_t myObjectId;
	const uint32_t myBufferSizes;

	const std::thread::id myOwnerId;

	__declspec(align(64)) DataShuttleImprovedInternals::Buffer<T, BufferSizes>* myBuffers[MaxBuffers];
	__declspec(align(64)) std::atomic<uint32_t> mySize;
	__declspec(align(64)) std::atomic<uint32_t> myBufferCount;
	__declspec(align(64)) std::atomic<uint32_t> myBufferReservations;
	uint8_t myPadding[64 - (sizeof(std::atomic<uint32_t>) % 64)];
};
// Static init
template <class T, uint32_t BufferSizes = 64, uint32_t MaxBuffers = 128, uint32_t MaxProducers = 16>
std::atomic<uint32_t> DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::ourIdIterator = 0;
template <class T, uint32_t BufferSizes = 64, uint32_t MaxBuffers = 128, uint32_t MaxProducers = 16>
thread_local GrowingArray<DataShuttleImprovedInternals::Producer> DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::ourProducers(4);
template <class T, uint32_t BufferSizes = 64, uint32_t MaxBuffers = 128, uint32_t MaxProducers = 16>
thread_local GrowingArray<DataShuttleImprovedInternals::Consumer> DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::ourConsumers(4);
// -----------

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers >
inline DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers, MaxProducers >::DataShuttle(uint32_t aInitBuffers) :
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

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers >
inline DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::~DataShuttle()
{
	for (uint32_t i = 0; i < myBufferCount && i < MaxBuffers; ++i)
		delete[] myBuffers[i];
	memset(myBuffers, 0, sizeof(myBuffers));
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers >
inline bool DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::TryPush(const T & aIn)
{
	if (!(myObjectId < ourProducers.Size()))
		ourProducers.Resize(myObjectId + 1);

	DataShuttleImprovedInternals::Producer& producer = ourProducers[myObjectId];

	if (producer.myDoInit)
		InitProducer();

	while (!myBuffers[producer.myCurrentBufferSlot]->TryPush(aIn))
		if (!RelocateProducer())
			return false;

	++mySize;
	return true;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers >
inline bool DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::TryPop(T & aOut)
{
	if (!(myObjectId < ourConsumers.Size()))
		ourConsumers.Resize(myObjectId + 1);

	DataShuttleImprovedInternals::Consumer& consumer = ourConsumers[myObjectId];

	if (consumer.myDoInit)
		InitConsumer();

	while (!myBuffers[consumer.myCurrentBufferSlot]->TryPop(aOut))
		if (!RelocateConsumer())
			return false;

	--mySize;
	return true;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers >
inline uint32_t DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::Size() const
{
	return mySize;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers >
inline void DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::InitConsumer()
{
	ourConsumers[myObjectId].myDoInit = false;
	ourConsumers[myObjectId].myCurrentBufferSlot = DataShuttleImprovedInternals::Consumer::ourStartBufferIterator++ % myBufferCount;
}
template<class T, uint32_t BufferSizes, uint32_t MaxBuffers>
inline void DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::InitProducer()
{
	ourProducers[myObjectId].myCurrentBufferSlot = InvalidIndex;

	if (!RelocateProducer())
		return;

	ourProducers[myObjectId].myDoInit = false;
	if (std::this_thread::get_id() != myOwnerId)
		ourProducers[myObjectId].myExit = std::bind(&DataShuttle::ExitProducer, this);
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers>
inline void DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::ExitProducer()
{
	myBuffers[ourProducers[myObjectId].myCurrentBufferSlot]->UnregisterProducer();
	ourProducers[myObjectId].myCurrentBufferSlot = InvalidIndex;
}

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers>
inline bool DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::RelocateProducer()
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

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers>
inline bool DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::RelocateConsumer()
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

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers>
inline int32_t DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::FindAvaliable()
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

template<class T, uint32_t BufferSizes, uint32_t MaxBuffers, uint32_t MaxProducers>
inline int32_t DataShuttle<T, BufferSizes, MaxBuffers, MaxProducers>::AllocateNew()
{
	int32_t newBufferSlot = static_cast<int32_t>(myBufferReservations++);

	assert(newBufferSlot < MaxBuffers && "Tried to allocate beyond max buffers. Increase max buffers?");
	if (!(newBufferSlot < MaxBuffers))
		return InvalidIndex;

	DataShuttleImprovedInternals::InternalQueue<T, BufferSizes>* buffer = new DataShuttleImprovedInternals::InternalQueue<T, BufferSizes>;

	myBuffers[newBufferSlot] = buffer;

	++myBufferCount;

	return newBufferSlot;
}
