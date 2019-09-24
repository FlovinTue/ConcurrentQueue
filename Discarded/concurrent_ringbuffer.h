#pragma once

#include <cstdint>
#include <memory>
#include <cassert>
#include <atomic>

template <class T, uint32_t BufferSize>
class concurrent_ringbuffer
{
public:
	concurrent_ringbuffer();
	~concurrent_ringbuffer() = default;

	bool try_pop(T& aOut);
	bool try_push(const T& aIn);

	uint32_t size() const;

private:
	typedef uint16_t state_type;

	static constexpr state_type Entry_Flag_Occupied = (state_type(1) << ((sizeof(state_type) * 8) - 1));
	static constexpr state_type Entry_Flag_In_Use = (Entry_Flag_Occupied >> 1);
	static constexpr state_type Iteration_Mask = (~(Entry_Flag_In_Use | Entry_Flag_Occupied));

	alignas(128) std::atomic<uint32_t> myWriteIterator;
	alignas(128) std::atomic<uint32_t> myReadIterator;
	alignas(64) std::atomic<state_type> myState[BufferSize];
	T myData[BufferSize];
};

template<class T, uint32_t BufferSize>
inline concurrent_ringbuffer<T, BufferSize>::concurrent_ringbuffer() :
	myWriteIterator(0),
	myReadIterator(0),
	myState{0}
{
}

template<class T, uint32_t BufferSize>
inline bool concurrent_ringbuffer<T, BufferSize>::try_pop(T & aOut)
{
	const uint32_t readIterator = myReadIterator.load(std::memory_order_acquire);
	const uint32_t readSlot = readIterator % BufferSize;

	if (myState[readSlot].load(std::memory_order_acquire) & Entry_Flag_In_Use)
		return false;

	const uint32_t iteration((readIterator / BufferSize) + 1);
	const state_type iterationCompressed(static_cast<state_type>(iteration & Iteration_Mask));
	const state_type desiredState = (Entry_Flag_In_Use | iterationCompressed);
	state_type expectedState = Entry_Flag_Occupied | iterationCompressed;

	if (!myState[readSlot].compare_exchange_strong(expectedState, desiredState, std::memory_order_seq_cst))
		return false;

	myReadIterator.fetch_add(1, std::memory_order_acq_rel);

	aOut = myData[readSlot];

	const state_type unoccupiedState = iterationCompressed;

	myState[readSlot].store(unoccupiedState, std::memory_order_release);

	return true;
}

template<class T, uint32_t BufferSize>
inline bool concurrent_ringbuffer<T, BufferSize>::try_push(const T & aIn)
{
	const uint32_t writeIterator = myWriteIterator.load(std::memory_order_acquire);
	const uint32_t writeSlot = writeIterator % BufferSize;

	if (myState[writeSlot].load(std::memory_order_acquire) & Entry_Flag_In_Use)
		return false;

	const uint32_t iteration = (writeIterator / BufferSize) + 1;
	const state_type iterationCompressed(static_cast<state_type>(iteration & Iteration_Mask));
	const state_type desiredState = Entry_Flag_In_Use | (iterationCompressed);
	state_type expectedState = ((iterationCompressed - 1) & Iteration_Mask);

	if (!myState[writeSlot].compare_exchange_strong(expectedState, desiredState, std::memory_order_seq_cst))
		return false;

	myWriteIterator.fetch_add(1, std::memory_order_acq_rel);

	myData[writeSlot] = aIn;

	const state_type occupiedState = Entry_Flag_Occupied | iterationCompressed;

	myState[writeSlot].store(occupiedState, std::memory_order_release);

	return true;
}

template<class T, uint32_t BufferSize>
inline uint32_t concurrent_ringbuffer<T, BufferSize>::size() const
{
	return myWriteIterator.load(std::memory_order_acquire) - myReadIterator.load(std::memory_order_acquire);
}
