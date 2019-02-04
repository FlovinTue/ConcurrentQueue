#pragma once

#include <cstdint>
#include <memory>
#include <cassert>
#include <atomic>
#include <vector>

#pragma warning(disable : 4324)

template <class T>
class CqStaticQueue;
template <class T>
class CqBuffer;

enum class Sq_ReturnValue : uint32_t
{
	Relocate = 1,
	Success = 2,
	Failiure = 4
};

template <class T, uint32_t InitCapacity = 256>
class ConcurrentQueue
{
public:
	ConcurrentQueue();
	~ConcurrentQueue();

	void Push(const T& aIn);
	bool TryPop(T& aOut);

private:
	static uint32_t constexpr NumBuffers();
	static uint32_t constexpr SlotCapacity(uint32_t aSlot);

	bool TryPushNewQueue();

	void DeallocateSlot(uint32_t aSlot);
	CqStaticQueue<T>* AllocateNew(uint32_t aSize);


	CqStaticQueue<T>* myStaticQueues[NumBuffers()];

	std::atomic<uint32_t> myLastBufferSlot;
	uint32_t myWriteSlot;
	uint32_t myReadSlot;
};
template<class T, uint32_t InitCapacity>
inline ConcurrentQueue<T, InitCapacity>::ConcurrentQueue() :
	myWriteSlot(0),
	myReadSlot(0),
	myLastBufferSlot(0)
{
	memset(&myStaticQueues[0], 0, sizeof(void*) * NumBuffers());
	myStaticQueues[0] = AllocateNew(InitCapacity);
}
template<class T, uint32_t InitCapacity>
inline ConcurrentQueue<T, InitCapacity>::~ConcurrentQueue()
{
	for (uint32_t i = 0; i < myWriteSlot + 1; ++i)
		DeallocateSlot(i);

	memset(&myStaticQueues[0], 0, sizeof(void*) * myWriteSlot + 1);
}
template<class T, uint32_t InitCapacity>
inline void ConcurrentQueue<T, InitCapacity>::Push(const T & aIn)
{
	uint32_t result(static_cast<uint32_t>(Sq_ReturnValue::Failiure));

	while (!(result = myStaticQueues[myWriteSlot]->TryPush(aIn)));

	//if (result & SQ_RETVAL_RELOCATE)
	//	TryPushNewQueue();

}
template<class T, uint32_t InitCapacity>
inline bool ConcurrentQueue<T, InitCapacity>::TryPop(T & aOut)
{
	const uint32_t readSlot = myReadSlot;
	const uint32_t result = myStaticQueues[readSlot]->TryPop(aOut);

	//if (result & SQ_RETVAL_RELOCATE & (readSlot < myWriteSlot))
	//{
	//	myReadSlot = readSlot + 1;

	//	return TryPop(aOut);
	//}
	const bool success = static_cast<bool>(result & static_cast<uint32_t>(Sq_ReturnValue::Success));
	return success;
}
template<class T, uint32_t InitCapacity>
inline constexpr uint32_t ConcurrentQueue<T, InitCapacity>::NumBuffers()
{
	uint32_t count(0);

	for (uint32_t i = 0; i < 32; ++i)
	{
		count += static_cast<bool>(InitCapacity << i);
	}
	return count;
}
template<class T, uint32_t InitCapacity>
inline constexpr uint32_t ConcurrentQueue<T, InitCapacity>::SlotCapacity(uint32_t aSlot)
{
	uint32_t initPow = 32 - NumBuffers();
	uint32_t capacity = static_cast<uint32_t>(pow(2, initPow + aSlot));

	return capacity;
}

template<class T, uint32_t InitCapacity>
inline bool ConcurrentQueue<T, InitCapacity>::TryPushNewQueue()
{
	if (myWriteSlot != myLastBufferSlot++)
		return false;

	std::cout << "Grew" << std::endl;
	myStaticQueues[myWriteSlot + 1] = AllocateNew(SlotCapacity(myWriteSlot));

	++myWriteSlot;
	myLastBufferSlot = myWriteSlot;
	return true;
}

template<class T, uint32_t InitCapacity>
inline void ConcurrentQueue<T, InitCapacity>::DeallocateSlot(uint32_t aSlot)
{
	uint8_t* block = reinterpret_cast<uint8_t*>(myStaticQueues[aSlot]);

	delete[] block;
	myStaticQueues[aSlot] = nullptr;
}

template<class T, uint32_t InitCapacity>
inline CqStaticQueue<T>* ConcurrentQueue<T, InitCapacity>::AllocateNew(uint32_t aSize)
{
	const uint32_t bufferCapacities((aSize + aSize % 2) / 2);

	const uint32_t sQueueSize = sizeof(CqStaticQueue<T>);
	const uint32_t bufferSize = sizeof(CqBuffer<T>);
	const uint32_t dataBlockSize = sizeof(T) * bufferCapacities;

	const uint32_t totalBlockSize = sQueueSize + bufferSize * 2 + dataBlockSize * 2;

	const uint64_t sQueueOffset(0);

	const uint64_t bufferOffsetA(sQueueOffset + sQueueSize);
	const uint64_t dataOffsetA(bufferOffsetA + bufferSize);

	const uint64_t bufferOffsetB(dataOffsetA + dataBlockSize);
	const uint64_t dataOffsetB(bufferOffsetB + bufferSize);

	uint8_t* block = new uint8_t[totalBlockSize];

	T* dataA = new (block + dataOffsetA) T[dataBlockSize];
	T* dataB = new (block + dataOffsetB) T[dataBlockSize];

	CqBuffer<T>* buffers[2] =
	{
		new(block + bufferOffsetA) CqBuffer<T>(dataA),
		new(block + bufferOffsetB) CqBuffer<T>(dataB)
	};

	CqStaticQueue<T>* sQueue = new(block + sQueueOffset) CqStaticQueue<T>(buffers);

	return sQueue;
}
template <class T>
class CqStaticQueue
{
public:
	CqStaticQueue(CqBuffer<T>* aBuffers[2]);
	CqStaticQueue() = delete;
	~CqStaticQueue() = default;

	uint32_t TryPop(T& aOut);
	uint32_t TryPush(const T& aIn);

	uint32_t Size() const;
	uint32_t Capacity() const;

private:

	const uint32_t myCapacity;
	const uint32_t myReplacementSize;

	std::atomic<uint32_t> mySize;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myAvaliableSlots;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myReadSlot;
	uint8_t myPadding3[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myWriteSlot;
	uint8_t myPadding4[64 - (sizeof(std::atomic<uint32_t>) % 64)];


	CqBuffer<T>* myBuffers[2];
};

template <class T>
inline CqStaticQueue<T>::CqStaticQueue(CqBuffer<T>* aBuffers[2]) :
	mySize(0),
	myCapacity(aBuffers[0]->Capacity() * 2),
	myReplacementSize(myCapacity - myCapacity / 4),
	myBuffers{ aBuffers[0], aBuffers[1] },
	myAvaliableSlots(myCapacity),
	myReadSlot(0),
	myWriteSlot(0)
{

}

template <class T>
inline uint32_t CqStaticQueue<T>::TryPop(T & aOut)
{
	if (!(--mySize < myCapacity))
	{
		++mySize;
		return static_cast<uint32_t>(Sq_ReturnValue::Failiure);
	}

	uint32_t bufferCapacity = myCapacity / 2;
	uint32_t readSlotTotal = myReadSlot++;
	uint32_t readSlot = readSlotTotal % bufferCapacity;
	uint32_t buffer = readSlotTotal / bufferCapacity;

	myBuffers[buffer]->Get(readSlot, aOut);

	if (readSlot == (bufferCapacity - 1))
		myAvaliableSlots += bufferCapacity;

	return static_cast<uint32_t>(Sq_ReturnValue::Success);
}

template <class T>
inline uint32_t CqStaticQueue<T>::TryPush(const T & aIn)
{
	if (myCapacity < --myAvaliableSlots)
	{
		++myAvaliableSlots;
		return static_cast<uint32_t>(Sq_ReturnValue::Failiure);
	}

	uint32_t bufferCapacity = myCapacity / 2;
	uint32_t writeSlotTotal = myWriteSlot++;
	uint32_t writeSlot = writeSlotTotal % bufferCapacity;
	uint32_t buffer = writeSlotTotal / bufferCapacity;

	myBuffers[buffer]->Set(writeSlot, aIn);

	++mySize;
	
	return static_cast<uint32_t>(Sq_ReturnValue::Success);
}

template <class T>
inline uint32_t CqStaticQueue<T>::Size() const
{
	return mySize._My_val;
}

template <class T>
inline uint32_t CqStaticQueue<T>::Capacity() const
{
	return myCapacity;
}
template <class T>
class CqBuffer
{
public:
	CqBuffer(T* aBlock);

	inline void Set(uint32_t aSlot, const T& aIn);
	inline void Get(uint32_t aSlot, T& aOut);

private:
	T* myData;
};
template<class T>
inline CqBuffer<T>::CqBuffer(T * aBlock) :
	myData(aBlock)
{
}
template <class T>
inline void CqBuffer<T>::Set(uint32_t aSlot, const T & aIn)
{
	myData[aSlot] = aIn;
}
template<class T>
inline void CqBuffer<T>::Get(uint32_t aSlot, T& aOut)
{
	aOut = myData[aSlot];
}
