#pragma once

#include <cstdint>
#include <memory>
#include <cassert>
#include <atomic>

#pragma warning(disable : 4324)

template <class T, uint32_t Capacity>
class Buffer
{
public:
	Buffer();
	~Buffer() = default;
	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;

	inline bool TryPush(const T& aIn);

	inline T& operator[](uint32_t aSlot);

	inline void Reset();

private:
	std::atomic<uint32_t> myWriteSlot;
	uint8_t myPadding0[64 - (sizeof(uint32_t) % 64)];

	T myData[Capacity];
};
template<class T, uint32_t Capacity>
inline Buffer<T, Capacity>::Buffer() :
	myWriteSlot(0)
{
}
template<class T, uint32_t Capacity>
inline bool Buffer<T, Capacity>::TryPush(const T & aIn)
{
	if (myWriteSlot == Capacity)
		return false;

	myData[myWriteSlot++] = aIn;

	return true;
}

template<class T, uint32_t Capacity>
inline T & Buffer<T, Capacity>::operator[](uint32_t aSlot)
{
	assert(aSlot < Capacity && "Invalid index");

	return myData[aSlot];
}

template<class T, uint32_t Capacity>
inline void Buffer<T, Capacity>::Reset()
{
	myWriteSlot = 0;
}

template <class T, uint32_t Capacity = 256>
class CqStaticQueue
{
	static_assert(Capacity % 2 == 0, "Please initialize StaticConcurrentQueue with capacity divisible by two");

public:
	CqStaticQueue();
	~CqStaticQueue() = default;

	bool TryPop(T& aOut);
	bool TryPush(const T& aIn);

	uint32_t Size() const;

	uint32_t GetCapacity() const;

private:

	Buffer<T, Capacity / 2> myBuffers[2];
	uint8_t myPadding0[64 - (sizeof(Buffer<T, Capacity>) * 2 % 64)];
	std::atomic<uint32_t> mySize;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myReadSlot;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myReadIterator;
	uint8_t myPadding3[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	uint8_t myWriteBuffer;
};

template<class T, uint32_t Capacity>
inline CqStaticQueue<T, Capacity>::CqStaticQueue() :
	mySize(0),
	myReadIterator(0),
	myReadSlot(0),
	myWriteBuffer(0)
{
	
}

template<class T, uint32_t Capacity>
inline bool CqStaticQueue<T, Capacity>::TryPop(T & aOut)
{
	if (Capacity < mySize--)
	{
		++mySize;
		return false;
	}

	uint32_t slot = (myReadSlot++) % Capacity;
	uint32_t buffer = slot / (Capacity / 2);

	aOut = myBuffers[buffer][slot % (Capacity / 2)];

	uint32_t readIterator = ++myReadIterator;
	uint32_t resetCheck = readIterator % (Capacity / 2);
	if (!resetCheck)
	{
		myBuffers[buffer].Reset();
	}

	return true;
}

template<class T, uint32_t Capacity>
inline bool CqStaticQueue<T, Capacity>::TryPush(const T & aIn)
{
	if (!myBuffers[myWriteBuffer].TryPush(aIn))
	{
		++myWriteBuffer %= 2;
		if (myBuffers[myWriteBuffer].TryPush(aIn))
		{
			++mySize;
			return true;
		}
		return false;
	}

	++mySize;

	return true;
}

template<class T, uint32_t Capacity>
inline uint32_t CqStaticQueue<T, Capacity>::Size() const
{
	return mySize;
}

template<class T, uint32_t Capacity>
inline uint32_t CqStaticQueue<T, Capacity>::GetCapacity() const
{
	return Capacity;
}
