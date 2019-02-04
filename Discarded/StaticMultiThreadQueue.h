#pragma once

#include <cstdint>
#include <memory>
#include <cassert>
#include <atomic>

template <class T, uint32_t BufferSize>
class StaticMultiThreadQueue
{
public:
	StaticMultiThreadQueue();
	~StaticMultiThreadQueue() = default;

	bool TryPop(T& aOut);
	bool TryPush(const T& aIn);

	uint32_t Size() const;

private:
	enum class EntryState : uint32_t
	{
		Unoccupied = 0,
		Occupied = 1,
		InUse = 2
	};
	struct Entry
	{
		std::atomic<uint32_t> myState;
		T myData;
	};

	__declspec(align(64)) Entry myBuffer[BufferSize];
	__declspec(align(64)) std::atomic<uint32_t> myWriteIterator;
	__declspec(align(64)) std::atomic<uint32_t> myReadIterator;
};

template<class T, uint32_t BufferSize>
inline StaticMultiThreadQueue<T, BufferSize>::StaticMultiThreadQueue() :
	myWriteIterator(0),
	myReadIterator(0)
{
	memset(myBuffer, 0, sizeof(myBuffer));
}

template<class T, uint32_t BufferSize>
inline bool StaticMultiThreadQueue<T, BufferSize>::TryPop(T & aOut)
{
	uint32_t readIterator = myReadIterator;
	uint32_t readSlot = readIterator % BufferSize;

	if (static_cast<EntryState>(myBuffer[readSlot].myState >> 24) == EntryState::InUse)
		return false;

	uint32_t iteration = (readIterator / BufferSize) + 1;
	uint32_t desiredState = (static_cast<uint32_t>(EntryState::InUse)<< 24) | iteration;
	uint32_t expectedState = (static_cast<uint32_t>(EntryState::Occupied) << 24) | iteration;

	if (!myBuffer[readSlot].myState.compare_exchange_strong(expectedState, desiredState))
		return false;

	++myReadIterator;

	aOut = myBuffer[readSlot].myData;

	uint32_t unoccupiedState = (static_cast<uint32_t>(EntryState::Unoccupied) << 24) | iteration;

	myBuffer[readSlot].myState.store(unoccupiedState);

	return true;
}

template<class T, uint32_t BufferSize>
inline bool StaticMultiThreadQueue<T, BufferSize>::TryPush(const T & aIn)
{
	uint32_t writeIterator = myWriteIterator;
	uint32_t writeSlot = writeIterator % BufferSize;

	if (static_cast<EntryState>(myBuffer[writeSlot].myState >> 24) == EntryState::InUse)
	{
		//assert(Size() < BufferSize && "Buffer full. Increase size?");
		return false;
	}

	uint32_t iteration = (writeIterator / BufferSize) + 1;
	uint32_t desiredState = (static_cast<uint32_t>(EntryState::InUse) << 24) | (iteration);
	uint32_t expectedState = (static_cast<uint32_t>(EntryState::Unoccupied) << 24) | (iteration - 1);

	if (!myBuffer[writeSlot].myState.compare_exchange_strong(expectedState, desiredState))
		return false;

	++myWriteIterator;

	myBuffer[writeSlot].myData = aIn;

	uint32_t occupiedState = (static_cast<uint32_t>(EntryState::Occupied) << 24) | iteration;

	myBuffer[writeSlot].myState.store(occupiedState);

	return true;
}

template<class T, uint32_t BufferSize>
inline uint32_t StaticMultiThreadQueue<T, BufferSize>::Size() const
{
	return myWriteIterator - myReadIterator;
}
