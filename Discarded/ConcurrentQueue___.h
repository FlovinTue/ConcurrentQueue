#pragma once

#include <cstdint>
#include <memory>
#include <cassert>
#include <atomic>

#pragma warning(disable : 4324)

template <class T>
class CqStaticQueue;
template <class T>
class CqBuffer;

enum SQ_RETVAL : uint32_t
{
	SQ_RETVAL_FAILIURE = 0,
	SQ_RETVAL_RELOCATE = 1,
	SQ_RETVAL_SUCCESS = 2
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

	bool TryPushStaticQueue();

	void DeallocateSlot(uint32_t aSlot);
	CqStaticQueue<T>* AllocateNew(uint32_t aSize);


	CqStaticQueue<T>* myStaticQueues[NumBuffers()];

	uint32_t myWriteSlot;
	uint32_t myReadSlot;

};
template<class T, uint32_t InitCapacity>
inline ConcurrentQueue<T, InitCapacity>::ConcurrentQueue() :
	myWriteSlot(0),
	myReadSlot(0)
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
	SQ_RETVAL result(SQ_RETVAL_FAILIURE);

	while (!(result = myStaticQueues[myWriteSlot]->TryPush(aIn)));

	if (result & SQ_RETVAL_RELOCATE)
		TryPushStaticQueue();
}
template<class T, uint32_t InitCapacity>
inline bool ConcurrentQueue<T, InitCapacity>::TryPop(T & aOut)
{
	return myStaticQueues[myReadSlot]->TryPop(aOut);
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
inline bool ConcurrentQueue<T, InitCapacity>::TryPushStaticQueue()
{
	myStaticQueues[++myWriteSlot] = AllocateNew(SlotCapacity(myWriteSlot));

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
		new(block + bufferOffsetA) CqBuffer<T>(dataA, bufferCapacities),
		new(block + bufferOffsetB) CqBuffer<T>(dataB, bufferCapacities)
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

	SQ_RETVAL TryPop(T& aOut);
	SQ_RETVAL TryPush(const T& aIn);

	uint32_t Size() const;
	uint32_t Capacity() const;

private:
	SQ_RETVAL EvaluateQueueReplacement(uint32_t aCurrentSize);

	const uint32_t myCapacity;
	const uint32_t myReplacementSize;

	uint8_t myPadding0[64 - (sizeof(CqBuffer<T>) * 2 % 64)];
	std::atomic<uint32_t> mySize;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myReadSlot;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myReadIterator;
	uint8_t myPadding3[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	uint8_t myWriteBuffer;
	CqBuffer<T>* myBuffers[2];
};

template <class T>
inline CqStaticQueue<T>::CqStaticQueue(CqBuffer<T>* aBuffers[2]) :
	mySize(0),
	myReadIterator(0),
	myReadSlot(0),
	myWriteBuffer(0),
	myCapacity(aBuffers[0]->Capacity() * 2),
	myReplacementSize(myCapacity - myCapacity / 4),
	myBuffers{aBuffers[0], aBuffers[1]}
{
	
}

template <class T>
inline SQ_RETVAL CqStaticQueue<T>::TryPop(T & aOut)
{
	if (myCapacity < mySize--)
	{
		++mySize;
		return SQ_RETVAL_FAILIURE;
	}

	uint32_t slot = (myReadSlot++) % myCapacity;
	uint32_t buffer = slot / (myCapacity / 2);

	aOut = (*myBuffers[buffer])[slot % (myCapacity / 2)];

	uint32_t readIterator = ++myReadIterator;
	uint32_t resetCheck = readIterator % (myCapacity / 2);
	if (!resetCheck)
	{
		myBuffers[buffer]->Reset();
	}

	return SQ_RETVAL_SUCCESS;
}

template <class T>
inline SQ_RETVAL CqStaticQueue<T>::TryPush(const T & aIn)
{
	uint32_t writeBuffer = myWriteBuffer;

	if (myBuffers[writeBuffer]->TryPush(aIn))
	{
		return EvaluateQueueReplacement(++mySize);
	}
	++writeBuffer %= 2;
	if (myBuffers[writeBuffer]->TryPush(aIn))
	{
		myWriteBuffer = writeBuffer;

		return EvaluateQueueReplacement(++mySize);
	}
	return SQ_RETVAL_FAILIURE;
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

template<class T>
inline SQ_RETVAL CqStaticQueue<T>::EvaluateQueueReplacement(uint32_t aCurrentSize)
{
	uint32_t match = static_cast<uint32_t>(aCurrentSize == myReplacementSize);
	uint32_t returnValue(SQ_RETVAL_SUCCESS);
	returnValue |= match;
	return static_cast<SQ_RETVAL>(returnValue);
}

template <class T>
class CqBuffer
{
public:
	CqBuffer(T* aBlock, uint32_t aCapacity);

	inline bool TryPush(const T& aIn);

	inline T& operator[](uint32_t aSlot);

	inline void Reset();

	inline uint32_t Capacity() const;
private:
	const uint32_t myCapacity;

	std::atomic<uint32_t> myWriteSlot;
	uint8_t myPadding0[64 - (sizeof(uint32_t) % 64)];

	T* myData;
};
template <class T>
inline CqBuffer<T>::CqBuffer(T* aBlock, uint32_t aCapacity) :
	myCapacity(aCapacity),
	myData(aBlock),
	myWriteSlot(0)

{
}
template <class T>
inline bool CqBuffer<T>::TryPush(const T & aIn)
{
	uint32_t writeSlot = myWriteSlot++;

	if (!(writeSlot < myCapacity))
		return false;

	myData[writeSlot] = aIn;

	return true;
}
template <class T>
inline T & CqBuffer<T>::operator[](uint32_t aSlot)
{
	assert(aSlot < myCapacity && "Invalid index");

	return myData[aSlot];
}
template <class T>
inline void CqBuffer<T>::Reset()
{
	myWriteSlot = 0;
}

template<class T>
inline uint32_t CqBuffer<T>::Capacity() const
{
	return myCapacity;
}

