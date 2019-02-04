 //ParallellQueue_v2

#pragma once

#ifndef MAKE_UNIQUE_NAME

#define _CONCAT_(a,b)  a##b
#define _LABEL_(a, b) _CONCAT_(a, b)
#define MAKE_UNIQUE_NAME(prefix) _LABEL_(prefix, __COUNTER__)

#endif

//#define CACHELINE_PADDING(aPreviousBlock) const uint8_t MAKE_UNIQUE_NAME(myPadding)[64 - ((sizeof(aPreviousBlock)) % 64)] {}

#include "GrowingArray.h"
#include <atomic>
#include <cassert>

template <class T>
class PqBuffer;

template <class T, uint32_t MaxProducers = 32>
class ParallellQueue
{
public:
	inline ParallellQueue(uint32_t aInitProducerSlots = 4, uint32_t aInitBufferCapacity = 64);
	inline ~ParallellQueue();

	void Push(const T& aIn);
	const bool TryPop(T& aOut);

	inline const uint32_t Size() const;

private:
	inline void InitConsumer();
	inline void InitProducer();

	inline const bool RelocateConsumer();

	inline const uint32_t FindAvaliableBuffer() const;
	
	inline __declspec(restrict) PqBuffer<T>* const CreateBuffer(const uint32_t aSize) const;
	inline __declspec(noalias) const uint32_t PushBuffer(PqBuffer<T>* const aBuffer);

	static const uint32_t InvalidIndex = uint32_t(UINT32_MAX);

	static std::atomic<uint32_t> ourObjectIterator;

	const uint32_t myInitBufferCapacity;
	const uint32_t myObjectId;

	static thread_local GrowingArray<PqBuffer<T>*> ourProducers;
	static thread_local GrowingArray<PqBuffer<T>*> ourConsumers;

	static thread_local uint32_t ourRelocationIterator;

	PqBuffer<T>* myBuffers[MaxProducers];
	CACHELINE_PADDING(myBuffers);
	std::atomic<uint32_t> myBufferCount;
	CACHELINE_PADDING(myBufferCount);
	std::atomic<uint32_t> myBufferReservations;
};

template <class T, uint32_t MaxProducers>
std::atomic<uint32_t> ParallellQueue<T, MaxProducers>::ourObjectIterator(0);
template <class T, uint32_t MaxProducers>
thread_local GrowingArray<PqBuffer<T>*> ParallellQueue<T, MaxProducers>::ourProducers(4, GAFLAG_NULLDATA);
template <class T, uint32_t MaxProducers>
thread_local GrowingArray<PqBuffer<T>*> ParallellQueue<T, MaxProducers>::ourConsumers(4, GAFLAG_NULLDATA);
template <class T, uint32_t MaxProducers>
thread_local uint32_t ParallellQueue<T, MaxProducers>::ourRelocationIterator(0);

template<class T, uint32_t MaxProducers>
inline ParallellQueue<T, MaxProducers>::ParallellQueue(uint32_t aInitProducerSlots, uint32_t aInitBufferCapacity) :
	myObjectId(ourObjectIterator++),
	myBufferCount(0),
	myBufferReservations(0),
	myInitBufferCapacity(aInitBufferCapacity)
{
	const uint32_t initProducerSlots(0 < aInitProducerSlots ? aInitProducerSlots : 1);

	for (uint32_t i = 0; i < initProducerSlots; ++i)
		PushBuffer(CreateBuffer(myInitBufferCapacity));
}

template<class T, uint32_t MaxProducers>
inline ParallellQueue<T, MaxProducers>::~ParallellQueue()
{
	for (uint32_t i = 0; i < myBufferCount; ++i)
	{
		myBuffers[i]->DestroyAll();
	}
}
template<class T, uint32_t MaxProducers>
void ParallellQueue<T, MaxProducers>::Push(const T & aIn)
{
	const uint32_t producerSlot(myObjectId);

	if (!(producerSlot < ourProducers.Size()))
		ourProducers.Resize(producerSlot + 1);

	PqBuffer<T>* buffer(ourProducers[producerSlot]);

	if (!buffer)
	{
		InitProducer();
		buffer = ourProducers[producerSlot];
	}

	while (!buffer->TryPush(aIn))
	{
		PqBuffer<T>* const next(CreateBuffer(buffer->Capacity() * 2));
		next->RegisterProducer();
		buffer->PushFront(next);
		ourProducers[producerSlot] = next;
		buffer = ourProducers[producerSlot];
	}
}
template<class T, uint32_t MaxProducers>
const bool ParallellQueue<T, MaxProducers>::TryPop(T & aOut)
{
	const uint32_t consumerSlot(myObjectId);

	if (!(consumerSlot < ourConsumers.Size()))
		ourConsumers.Resize(consumerSlot + 1);

	PqBuffer<T>* buffer = ourConsumers[consumerSlot];

	if (!buffer)
	{
		InitConsumer();
		buffer = ourConsumers[consumerSlot];
	}

	while (!buffer->TryPop(aOut))
	{
		if (!RelocateConsumer())
			return false;

		buffer = ourConsumers[consumerSlot];
	}	
	return true;
}
template<class T, uint32_t MaxProducers>
inline const uint32_t ParallellQueue<T, MaxProducers>::Size() const
{
	uint32_t size(0);
	for (uint32_t i = 0; i < myBufferCount._My_val; ++i)
		size += myBuffers[i]->Size();
	return size;
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::InitConsumer()
{
	RelocateConsumer();

	if (!ourConsumers[myObjectId])
		ourConsumers[myObjectId] = myBuffers[0];
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::InitProducer()
{
	uint32_t nextBufferSlot(InvalidIndex);
	for (;;)
	{
		nextBufferSlot = FindAvaliableBuffer();

		if (nextBufferSlot != InvalidIndex)
		{
			if (myBuffers[nextBufferSlot]->RegisterProducer())
				break;
			else
				continue;
		}

		PqBuffer<T>* const nextBuffer(CreateBuffer(myInitBufferCapacity));

		nextBufferSlot = PushBuffer(nextBuffer);

		if (myBuffers[nextBufferSlot]->RegisterProducer())
			break;
	}
	ourProducers[myObjectId] = myBuffers[nextBufferSlot];
}
template<class T, uint32_t MaxProducers>
inline const bool ParallellQueue<T, MaxProducers>::RelocateConsumer()
{
	const uint32_t buffers(myBufferCount._My_val);
	const uint32_t startBufferSlot = ourRelocationIterator++ % buffers;

	for (uint32_t i = 1; i < buffers + 1; ++i)
	{
		const uint32_t bufferSlot = (startBufferSlot + i) % buffers;

		if (0 < myBuffers[bufferSlot]->Size())
		{
			PqBuffer<T>* const buffer = myBuffers[bufferSlot]->FindBack();
			ourConsumers[myObjectId] = buffer;
			myBuffers[bufferSlot] = buffer;
			return true;
		}
	}
	return false;
}
template<class T, uint32_t MaxProducers>
inline __declspec(restrict) PqBuffer<T>* const ParallellQueue<T, MaxProducers>::CreateBuffer(const uint32_t aSize) const
{
	const uint32_t size(aSize + aSize % 2);


	const uint32_t bufferSize(sizeof(PqBuffer<T>));
	const uint32_t dataBlockSize(sizeof(T) * size);

	const uint32_t totalBlockSize(bufferSize + dataBlockSize);

	const uint64_t bufferOffset(0);
	const uint64_t dataOffset(bufferOffset + bufferSize);

	uint8_t* block;

	T* data;
	
	
#if (201703L <= __cplusplus)
	block = reinterpret_cast<uint8_t*>(operator new[](totalBlockSize, std::align_val_t(8)));
#else
	const uint32_t alignmentPadding(8);

	block = new uint8_t[totalBlockSize + alignmentPadding];
#endif

	data = new (block + dataOffset) T[size];

	PqBuffer<T>* const buffer = new(block + bufferOffset) PqBuffer<T>(size, data);

	return buffer;
}

template<class T, uint32_t MaxProducers>
__declspec(noalias) inline const uint32_t ParallellQueue<T, MaxProducers>::PushBuffer(PqBuffer<T>* const aBuffer)
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
inline const uint32_t ParallellQueue<T, MaxProducers>::FindAvaliableBuffer() const
{
	const uint32_t bufferCount(myBufferCount._My_val);

	const uint32_t startBufferSlot(0);

	for (uint32_t i = 0; i < bufferCount; ++i)
	{
		const uint32_t bufferSlot((startBufferSlot + i) % bufferCount);

		if (myBuffers[bufferSlot] && myBuffers[bufferSlot]->Avaliable())
		{
			return bufferSlot;
		}
	}
	return InvalidIndex;
}
template <class T>
class PqBuffer
{
public:
	PqBuffer(const uint32_t aCapacity, T* const aDataBlock);
	~PqBuffer() = default;

	inline void DestroyAll();

	__declspec(noalias) inline bool Avaliable() const;
	__declspec(noalias) inline bool RegisterProducer();

	inline const bool TryPop(T& aOut);
	inline const bool TryPush(const T& aIn);

	inline PqBuffer<T>* const FindBack();

	inline const uint32_t Size() const;

	__declspec(noalias) inline uint32_t Capacity() const;

	inline void PushFront(PqBuffer<T>* const aNewCluster);
private:
	inline PqBuffer<T>* const FindTail();

	std::atomic<bool> myIsAvaliable;

	PqBuffer<T>* myNext;
	PqBuffer<T>* myTail;

	const uint32_t myCapacity;


	CACHELINE_PADDING(myCapacity);
	T* const myDataBlocks[2];
	CACHELINE_PADDING(myDataBlocks);
	std::atomic<uint32_t> myReadSlot;
	CACHELINE_PADDING(myReadSlot);
	std::atomic<uint32_t> myPostReadIterator[2];
	CACHELINE_PADDING(myPostReadIterator);
	std::atomic<uint32_t> myPreReadIterator;
	CACHELINE_PADDING(myPreReadIterator);
	uint32_t myWriteSlot[2];
	CACHELINE_PADDING(myWriteSlot);
	uint32_t myPostWriteIterator;
};

template<class T>
inline PqBuffer<T>::PqBuffer(const uint32_t aCapacity, T* const aDataBlock) :
	myNext(nullptr),
	myTail(nullptr),
	myDataBlocks{ &aDataBlock[0], &aDataBlock[aCapacity / 2] },
	myCapacity(aCapacity),
	myReadSlot(0),
	myPostReadIterator{ 0,0 },
	myPreReadIterator(0),
	myWriteSlot{ 0,0 },
	myPostWriteIterator(0),
	myIsAvaliable(true)
{

}
template<class T>
inline void PqBuffer<T>::DestroyAll()
{
	PqBuffer<T>* current = FindTail();

	while (current)
	{
		const uint32_t lastCapacity(current->Capacity());
		uint8_t* const lastBlock(reinterpret_cast<uint8_t*>(current));
		T* const lastDataBlock(current->myDataBlocks[0]);

		current = current->myNext;
		if (!std::is_trivially_destructible<T>::value)
		{
			for (uint32_t i = 0; i < lastCapacity; ++i)
			{
				lastDataBlock[i].~T();
			}
		}
#if (201703L <= __cplusplus)
		operator delete[](lastBlock, sizeof(PqBuffer<T>) + sizeof(T) * myCapacity, std::align_val_t(8));
#else
		delete[] lastBlock;
#endif
	}
}
template<class T>
__declspec(noalias) inline bool PqBuffer<T>::Avaliable() const
{
	return myIsAvaliable._My_val;
}
template<class T>
__declspec(noalias) inline bool PqBuffer<T>::RegisterProducer()
{
	bool expected(true);
	return myIsAvaliable.compare_exchange_strong(expected, false);
}
// ------------------
template <class T>
inline const bool PqBuffer<T>::TryPop(T& aOut)
{
	if (myPostWriteIterator < ++myPreReadIterator)
	{
		--myPreReadIterator;
		return false;
	}

	const uint32_t bufferCapacity(myCapacity / 2);
	const uint32_t readSlotTotal(myReadSlot++);
	const uint32_t readSlot(readSlotTotal % bufferCapacity);
	const uint32_t buffer((readSlotTotal % myCapacity) / bufferCapacity);

	aOut = myDataBlocks[buffer][readSlot];

	const uint32_t readEntries(++myPostReadIterator[buffer]);

	if (readEntries == bufferCapacity)
	{
		myPostReadIterator[buffer].store(0, std::memory_order_relaxed);
		myWriteSlot[buffer] = 0;
	}
	return true;
}
template <class T>
inline const bool PqBuffer<T>::TryPush(const T& aIn)
{
	const uint32_t bufferCapacity(myCapacity / 2);
	const uint32_t buffer((myPostWriteIterator % myCapacity) / bufferCapacity);

	if (myWriteSlot[buffer] == bufferCapacity)
		return false;

	myDataBlocks[buffer][myWriteSlot[buffer]++] = aIn;

	++myPostWriteIterator;

	return true;
}
template<class T>
inline PqBuffer<T>* const PqBuffer<T>::FindBack()
{
	PqBuffer<T>* back(this);

	while (back->myNext)
	{
		uint32_t localSize(back->myPostWriteIterator);
		localSize -= back->myPreReadIterator._My_val;

		if (localSize)
			break;

		back = back->myNext;
	}
	return back;
}

template<class T>
inline const uint32_t PqBuffer<T>::Size() const
{
	uint32_t size(myPostWriteIterator); 
	size -=myPreReadIterator._My_val;

	if (myNext)
		size += myNext->Size();

	return size;
}

template<class T>
__declspec(noalias) inline uint32_t PqBuffer<T>::Capacity() const
{
	return myCapacity;
}

template<class T>
inline void PqBuffer<T>::PushFront(PqBuffer<T>* const aNewCluster)
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
inline PqBuffer<T>* const PqBuffer<T>::FindTail()
{
	PqBuffer<T>* tail(this);
	while (tail->myTail)
	{
		tail = tail->myTail;
	}
	return tail;
}
