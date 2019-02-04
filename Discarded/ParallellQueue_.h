// ParallellQueue_v2

#pragma once


#include "GrowingArray.h"
#include <atomic>
#include <cassert>
#include <thread>

#pragma warning(disable : 4324)

template <class T>
class PqBuffer;

template <class T, uint32_t MaxProducers = 32>
class ParallellQueue
{
public:
	ParallellQueue(uint32_t aInitBuffers = 4);
	~ParallellQueue();
	ParallellQueue(const ParallellQueue&) = delete;
	ParallellQueue& operator=(const ParallellQueue&) = delete;

	void Push(const T& aIn);
	bool TryPop(T& aOut);

	uint32_t Size() const;

private:
	void InitConsumer();
	void InitProducer();
	bool RelocateConsumer();

	uint32_t FindAvaliableBuffer(); 

	PqBuffer<T>* CreateBuffer(uint32_t aSize);
	uint32_t PushBuffer(PqBuffer<T>* aBuffer);

	static std::atomic<uint32_t> ourObjectIterator;

	static const uint32_t InvalidIndex = uint32_t(-1);
	static const uint32_t InitBufferCapacity = 64;

	const uint32_t myObjectId;

	static thread_local GrowingArray<PqBuffer<T>*> ourProducers;
	static thread_local GrowingArray<PqBuffer<T>*> ourConsumers;

	PqBuffer<T>* myBuffers[MaxProducers];
	uint8_t myPadding0[64 - ((sizeof(myBuffers)) % 64)];
	std::atomic<uint32_t> mySize;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myBufferCount;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myBufferReservations;
	uint8_t myPadding3[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	uint32_t myRelocationIterator;

};

template <class T, uint32_t MaxProducers>
std::atomic<uint32_t> ParallellQueue<T, MaxProducers>::ourObjectIterator(0);
template <class T, uint32_t MaxProducers>
thread_local GrowingArray<PqBuffer<T>*> ParallellQueue<T, MaxProducers>::ourProducers(4, static_cast<uint8_t>(GrowingArrayFlags::NullData));
template <class T, uint32_t MaxProducers>
thread_local GrowingArray<PqBuffer<T>*> ParallellQueue<T, MaxProducers>::ourConsumers(4, static_cast<uint8_t>(GrowingArrayFlags::NullData));

template<class T, uint32_t MaxProducers>
inline ParallellQueue<T, MaxProducers>::ParallellQueue(uint32_t aInitBuffers) :
	myObjectId(ourObjectIterator++),
	myBufferCount(0),
	myBufferReservations(0),
	mySize(0)
{
	for (uint32_t i = 0; i < aInitBuffers; ++i)
		PushBuffer(CreateBuffer(InitBufferCapacity));
}

template<class T, uint32_t MaxProducers>
inline ParallellQueue<T, MaxProducers>::~ParallellQueue()
{
	for (uint32_t i = 0; i < myBufferCount; ++i)
		myBuffers[i]->DestroyAll();
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::Push(const T & aIn)
{
	if (!(myObjectId < ourProducers.Size()))
		ourProducers.Resize(myObjectId + 1);

	PqBuffer<T>** const buffer = &ourProducers[myObjectId];

	if (!*buffer)
		InitProducer();

	while (!(*buffer)->TryPush(aIn))
	{
		PqBuffer<T>* const next = CreateBuffer((*buffer)->Capacity() * 2);
		(*buffer)->PushFront(next);
		ourProducers[myObjectId] = next;
	}

	++mySize;
}
template<class T, uint32_t MaxProducers>
inline bool ParallellQueue<T, MaxProducers>::TryPop(T & aOut)
{
	if (!(myObjectId < ourConsumers.Size()))
		ourConsumers.Resize(myObjectId + 1);

	PqBuffer<T>** const buffer = &ourConsumers[myObjectId];

	if (!(*buffer))
		InitConsumer();

	while (!(*buffer)->TryPop(aOut))
		if (!RelocateConsumer())
			return false;
	
	--mySize;
	return true;
}
template<class T, uint32_t MaxProducers>
inline uint32_t ParallellQueue<T, MaxProducers>::Size() const
{
	return mySize._My_val;
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::InitConsumer()
{
	RelocateConsumer();
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::InitProducer()
{
	uint32_t nextBufferSlot(InvalidIndex);
	for (;;)
	{
		nextBufferSlot = FindAvaliableBuffer();

		if (nextBufferSlot != InvalidIndex)
			if (myBuffers[nextBufferSlot]->RegisterProducer())
				break;
			else
				continue;

		nextBufferSlot = PushBuffer(CreateBuffer(InitBufferCapacity));

		assert(nextBufferSlot != InvalidIndex && "Max producers exceeded");

		if (myBuffers[nextBufferSlot]->RegisterProducer())
			break;
	}

	ourProducers[myObjectId] = myBuffers[nextBufferSlot];
}
template<class T, uint32_t MaxProducers>
inline bool ParallellQueue<T, MaxProducers>::RelocateConsumer()
{
	const uint32_t buffers(myBufferCount._My_val);
	const uint32_t startBufferSlot = myRelocationIterator++ % buffers;

	for (uint32_t i = 1; i < buffers + 1; ++i)
	{
		const uint32_t bufferSlot = (startBufferSlot + i) % buffers;

		if (0 < myBuffers[bufferSlot]->Size())
		{
			ourConsumers[myObjectId] = myBuffers[bufferSlot]->FindBack();
			myBuffers[bufferSlot] = ourConsumers[myObjectId];
			return true;
		}
	}
	return false;
}
template<class T, uint32_t MaxProducers>
inline PqBuffer<T>* ParallellQueue<T, MaxProducers>::CreateBuffer(uint32_t aSize)
{
	const uint32_t size(aSize + aSize % 2);

	const uint32_t bufferSize = sizeof(PqBuffer<T>);
	const uint32_t dataBlockSize = sizeof(T) * size;

	const uint32_t totalBlockSize = bufferSize + dataBlockSize;

	const uint64_t bufferOffset(0);
	const uint64_t dataOffset(bufferOffset + bufferSize);

	uint8_t* block = new uint8_t[totalBlockSize];

	T* data = new (block + dataOffset) T[dataBlockSize];

	PqBuffer<T>* buffer = new(block + bufferOffset) PqBuffer<T>(size, data);

	return buffer;
}

template<class T, uint32_t MaxProducers>
inline uint32_t ParallellQueue<T, MaxProducers>::PushBuffer(PqBuffer<T>* aBuffer)
{
	const uint32_t nextBufferSlot = myBufferReservations++;

	assert(nextBufferSlot < MaxProducers && "Tried to allocate beyond max buffers. Increase max buffers?");
	if (!(nextBufferSlot < MaxProducers))
		return InvalidIndex;

	myBuffers[nextBufferSlot] = aBuffer;

	++myBufferCount;

	return nextBufferSlot;
}
template<class T, uint32_t MaxProducers>
inline uint32_t ParallellQueue<T, MaxProducers>::FindAvaliableBuffer()
{
	const uint32_t bufferCount(myBufferCount._My_val);

	const uint32_t startBufferSlot(0);

	for (uint32_t i = 0; i < bufferCount; ++i)
	{
		const uint32_t bufferSlot((startBufferSlot + i) % bufferCount);

		if (myBuffers[bufferSlot] && myBuffers[bufferSlot]->Avaliable())
			return bufferSlot;
	}
	return InvalidIndex;
}

enum class PqReturnValue : uint32_t
{
	Failiure = 1 << 1,
	Success = 1 << 2,
	Relocate = 1 << 3
};
template <class T>
class PqBuffer
{
public:
	PqBuffer(uint32_t aCapacity, T* aDataBlock);
	~PqBuffer() = default;

	void DestroyAll();

	bool Avaliable() const;
	bool RegisterProducer();

	bool TryPop(T& aOut);
	bool TryPush(const T& aIn);

	PqBuffer<T>* FindBack();
	uint32_t Size() const;
	uint32_t Capacity() const;

	void PushFront(PqBuffer<T>* aNewCluster);
private:
	PqBuffer<T>* FindTail();

	std::atomic<bool> myIsAvaliable;

	PqBuffer<T>* myNext;
	PqBuffer<T>* myTail;

	const uint32_t myCapacity;

	std::atomic<uint32_t> myReadSlot;
	uint8_t myPadding0[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myReadEntries;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> mySize;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	uint32_t myWriteSlot[2];
	uint32_t myWrittenSlots;
	uint8_t myPadding3[64 - (sizeof(uint32_t) % 64)];

	T* const myDataBlocks[2];
};

template<class T>
inline PqBuffer<T>::PqBuffer(uint32_t aCapacity, T* aDataBlock) :
	myNext(nullptr),
	myTail(nullptr),
	myDataBlocks{ &aDataBlock[0], &aDataBlock[aCapacity / 2] },
	myCapacity(aCapacity),
	myReadSlot(0),
	myReadEntries(0),
	mySize(0),
	myWriteSlot{ 0,0 },
	myWrittenSlots(0),
	myIsAvaliable(true)
{

}
template<class T>
inline void PqBuffer<T>::DestroyAll()
{
	PqBuffer<T>* current = FindTail();

	while (current)
	{
		uint8_t* lastBlock = reinterpret_cast<uint8_t*>(current);
		current = current->myNext;
		delete[] lastBlock;
	}
}
template<class T>
inline bool PqBuffer<T>::Avaliable() const
{
	return myIsAvaliable;
}
template<class T>
inline bool PqBuffer<T>::RegisterProducer()
{
	bool expected(true);
	return myIsAvaliable.compare_exchange_strong(expected, false);
}
// ------------------
template <class T>
inline bool PqBuffer<T>::TryPop(T& aOut)
{
	if (!(--mySize < myCapacity))
	{
		++mySize;
		return false;
	}

	uint32_t bufferCapacity = myCapacity / 2;
	uint32_t readSlotTotal = myReadSlot++;
	uint32_t readSlot = readSlotTotal % bufferCapacity;
	uint32_t buffer = (readSlotTotal % myCapacity) / bufferCapacity;

	aOut = myDataBlocks[buffer][readSlot];

	if ((++myReadEntries % bufferCapacity) == 0)
	{
		myWriteSlot[buffer] = 0;
	}
	return true;
}
template <class T>
inline bool PqBuffer<T>::TryPush(const T& aIn)
{
	uint32_t bufferCapacity = myCapacity / 2;
	uint32_t buffer = (myWrittenSlots % myCapacity) / bufferCapacity;

	if (myWriteSlot[buffer] == bufferCapacity)
		return false;

	uint32_t writeSlotTotal = myWriteSlot[buffer]++;
	uint32_t writeSlot = writeSlotTotal % bufferCapacity;

	myDataBlocks[buffer][writeSlot] = aIn;

	++mySize;

	++myWrittenSlots;

	return true;
}
template<class T>
inline PqBuffer<T>* PqBuffer<T>::FindBack()
{
	PqBuffer<T>* back(this);

	while (!back->mySize._My_val)
	{
		back = back->myNext;
	}
	return back;
}

template<class T>
inline uint32_t PqBuffer<T>::Size() const
{
	uint32_t ret = mySize._My_val;

	if (myNext)
		ret += myNext->Size();

	return ret;
}

template<class T>
inline uint32_t PqBuffer<T>::Capacity() const
{
	return myCapacity;
}

template<class T>
inline void PqBuffer<T>::PushFront(PqBuffer<T>* aNewCluster)
{
	PqBuffer<T>* last(this);
	while (last->myNext)
	{
		last = last->myNext;
	}
	last->myNext = aNewCluster;
	aNewCluster->myTail = last;
}

template<class T>
inline PqBuffer<T>* PqBuffer<T>::FindTail()
{
	PqBuffer<T>* tail(this);
	while (tail->myTail)
	{
		tail = tail->myTail;
	}
	return tail;
}