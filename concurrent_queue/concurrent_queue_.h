#pragma once

//Copyright(c) 2019 Flovin Michaelsen
//
//Permission is hereby granted, free of charge, to any person obtining a copy
//of this software and associated documentation files(the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions :
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#include <atomic>
#include <vector>
#include <limits>

// In the event an exception is thrown during a pop operation, some entries may
// be dequeued out-of-order as some consumers may already be halfway through a 
// pop operation before reintegration efforts are started.
//
// Exception handling may be disabled for a slight performance increase in some
// situations
#define CQ_ENABLE_EXCEPTIONHANDLING 

#ifdef CQ_ENABLE_EXCEPTIONHANDLING 
#define CQ_BUFFER_NOTHROW_POP_MOVE(type) (std::is_nothrow_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_POP_ASSIGN(type) (!CQ_BUFFER_NOTHROW_POP_MOVE(type) && (std::is_nothrow_assignable<type&, type>::value))
#define CQ_BUFFER_NOTHROW_PUSH_MOVE(type) (std::is_nothrow_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_PUSH_ASSIGN(type) (std::is_nothrow_assignable<type&, type>::value)

#undef min
#undef max

namespace cq {

class producer_overflow : public std::runtime_error
{
public:
	producer_overflow(const char* aError) : runtime_error(aError) {}
};

#else
#define CQ_BUFFER_NOTHROW_POP_MOVE(type) (std::is_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_POP_ASSIGN(type) (!CQ_BUFFER_NOTHROW_POP_MOVE(type))
#define CQ_BUFFER_NOTHROW_PUSH_ASSIGN(type) (std::is_same<type, type>::value)
#define CQ_BUFFER_NOTHROW_PUSH_MOVE(type) (std::is_same<type, type>::value)
#endif

#ifndef MAKE_UNIQUE_NAME 
#define CONCAT(a,b)  a##b
#define EXPAND_AND_CONCAT(a, b) CONCAT(a,b)
#define MAKE_UNIQUE_NAME(prefix) EXPAND_AND_CONCAT(prefix, __COUNTER__)
#endif

#define CQ_PADDING(bytes) const uint8_t MAKE_UNIQUE_NAME(trash)[bytes] {}

// For anonymous struct
#pragma warning(push)
#pragma warning(disable : 4201) 

template <class T>
class producer_buffer;

template <class T>
class item_container;

enum class Item_state : int8_t;

// The WizardLoaf concurrent_queue 
// Made for the x86/x64 architecture in Visual Studio 2017, focusing
// on performance. The Queue preserves the FIFO property within the 
// context of single producers. Push operations are wait-free, TryPop & Size 
// are lock-free and producer capacities grows dynamically
template <class T>
class concurrent_queue
{
public:
	typedef std::size_t size_type;

	inline concurrent_queue();
	inline concurrent_queue(const size_type initProducerCapacity);
	inline ~concurrent_queue();

	inline void push(const T& in);
	inline void push(T&& in);

	const bool try_pop(T& out);

	void unsafe_clear();

	// The Size method can be considered an approximation, and may be 
	// innacurate at the time the caller receives the result.
	inline const std::size_t size() const;
private:
	friend class producer_buffer<T>;

	template <class ...Arg>
	void push_internal(Arg&&... in);

	inline void init_producer();

	inline const bool relocate_consumer();

	inline __declspec(restrict)producer_buffer<T>* const create_producer_buffer(const std::size_t withSize) const;
	inline void push_producer_buffer(producer_buffer<T>* const buffer);
	inline void try_alloc_produer_store_slot(const uint8_t storeArraySlot);
	inline void try_swap_producer_array(const uint8_t aromStoreArraySlot);
	inline void try_swap_producer_count(const uint16_t toValue);

	inline const uint16_t claim_store_slot();
	inline producer_buffer<T>* const fetch_from_store(const uint16_t storeSlot) const;
	inline void insert_to_store(producer_buffer<T>* const buffer, const uint16_t storeSlot);
	inline const uint8_t to_store_array_slot(const uint16_t storeSlot) const;
	inline const size_type log2_align(const std::size_t from, const std::size_t clamp) const;

	// Not size_type max because we need some leaway in case  we
	// need to throw consumers out of a buffer whilst repairing it
	static const size_type Buffer_Capacity_Max = ~(std::numeric_limits<size_type>::max() >> 3) / 2;
	static const uint16_t Max_Producers = std::numeric_limits<int16_t>::max() - 1;

	// Maximum number of times the producer slot array can grow
	static const uint8_t Producer_Slots_Max_Growth_Count = 15;

	static std::atomic<size_type> ourObjectIterator;

	const size_type myInitBufferCapacity;
	const size_type myObjectId;

	static thread_local std::vector<producer_buffer<T>*> ourProducers;
	static thread_local std::vector<producer_buffer<T>*> ourConsumers;

	static thread_local uint16_t ourRelocationIndex;

	static producer_buffer<T> ourDummyBuffer;

	std::atomic<producer_buffer<T>**> myProducerArrayStore[Producer_Slots_Max_Growth_Count];
	std::atomic<producer_buffer<T>**> myProducerSlots;
	std::atomic<uint16_t> myProducerCount;
	std::atomic<uint16_t> myProducerCapacity;
	std::atomic<uint16_t> myProducerSlotReservation;
	std::atomic<uint16_t> myProducerSlotPostIterator;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	std::atomic<uint16_t> myProducerSlotPreIterator;
#endif
};

template <class T>
std::atomic<typename concurrent_queue<T>::size_type> concurrent_queue<T>::ourObjectIterator(0);
template <class T>
thread_local std::vector<producer_buffer<T>*> concurrent_queue<T>::ourProducers;
template <class T>
thread_local std::vector<producer_buffer<T>*> concurrent_queue<T>::ourConsumers;
template <class T>
thread_local uint16_t concurrent_queue<T>::ourRelocationIndex(static_cast<uint16_t>(rand() % std::numeric_limits<uint16_t>::max()));
template <class T>
producer_buffer<T> concurrent_queue<T>::ourDummyBuffer(0, nullptr);

template<class T>
inline concurrent_queue<T>::concurrent_queue()
	: concurrent_queue<T>(2)
{
}
template<class T>
inline concurrent_queue<T>::concurrent_queue(size_type initProducerCapacity)
	: myObjectId(ourObjectIterator++)
	, myProducerCapacity(0)
	, myProducerCount(0)
	, myProducerSlotPostIterator(0)
	, myProducerSlotReservation(0)
	, myProducerSlots(nullptr)
	, myInitBufferCapacity(log2_align(initProducerCapacity, Buffer_Capacity_Max))
	, myProducerArrayStore{ nullptr }
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	, myProducerSlotPreIterator(0)
#endif
{
}
template<class T>
inline concurrent_queue<T>::~concurrent_queue()
{
	const uint16_t producerCount(myProducerCount.load(std::memory_order_acquire));

	for (uint16_t i = 0; i < producerCount; ++i) {
		if (myProducerSlots[i] == &ourDummyBuffer)
			continue;
		myProducerSlots[i]->destroy_all();
	}
	for (uint16_t i = 0; i < Producer_Slots_Max_Growth_Count; ++i) {
		delete[] myProducerArrayStore[i];
	}
	memset(&myProducerArrayStore[0], 0, sizeof(std::atomic<producer_buffer<T>**>) * Producer_Slots_Max_Growth_Count);
}

template<class T>
void concurrent_queue<T>::push(const T & in)
{
	push_internal<const T&>(in);
}
template<class T>
inline void concurrent_queue<T>::push(T && in)
{
	push_internal<T&&>(std::move(in));
}
template<class T>
template<class ...Arg>
inline void concurrent_queue<T>::push_internal(Arg&& ...in)
{
	const std::size_t producerSlot(myObjectId);

	if (!(producerSlot < ourProducers.size()))
		ourProducers.resize(producerSlot + 1, nullptr);

	producer_buffer<T>* buffer(ourProducers[producerSlot]);

	if (!buffer) {
		init_producer();
		buffer = ourProducers[producerSlot];
	}
	if (!buffer->try_push(std::forward<Arg>(in)...)) {
		producer_buffer<T>* const next(create_producer_buffer(size_t(buffer->capacity()) * 2));
		buffer->push_front(next);
		ourProducers[producerSlot] = next;
		next->try_push(std::forward<Arg>(in)...);
	}
}
template<class T>
const bool concurrent_queue<T>::try_pop(T & out)
{
	const std::size_t consumerSlot(myObjectId);
	if (!(consumerSlot < ourConsumers.size()))
		ourConsumers.resize(consumerSlot + 1, &ourDummyBuffer);

	producer_buffer<T>* buffer = ourConsumers[consumerSlot];

	for (uint16_t attempt(0); !buffer->try_pop(out); ++attempt) {
		if (!(attempt < myProducerCount.load(std::memory_order_acquire)))
			return false;

		if (!relocate_consumer())
			return false;

		buffer = ourConsumers[consumerSlot];
	}
	return true;
}
template<class T>
inline void concurrent_queue<T>::unsafe_clear()
{
	std::atomic_thread_fence(std::memory_order_acquire);

	for (uint16_t i = 0; i < myProducerCount.load(std::memory_order_relaxed); ++i) {
		myProducerSlots[i]->unsafe_clear();
	}

	std::atomic_thread_fence(std::memory_order_release);
}
template<class T>
inline const std::size_t concurrent_queue<T>::size() const
{
	const uint16_t producerCount(myProducerCount.load(std::memory_order_relaxed));

	std::size_t size(0);
	for (uint16_t i = 0; i < producerCount; ++i) {
		size += myProducerSlots[i]->size();
	}
	return size;
}
template<class T>
inline void concurrent_queue<T>::init_producer()
{
	producer_buffer<T>* const newBuffer(create_producer_buffer(myInitBufferCapacity));
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		push_producer_buffer(newBuffer);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		newBuffer->destroy_all();
		throw;
	}
#endif
	ourProducers[myObjectId] = newBuffer;
}
template<class T>
inline const bool concurrent_queue<T>::relocate_consumer()
{
	const uint16_t producers(myProducerCount.load(std::memory_order_acquire));
	const uint16_t relocation(ourRelocationIndex--);

	for (uint16_t i = 0, j = relocation; i < producers; ++i, ++j) {
		const uint16_t entry(j % producers);
		producer_buffer<T>* const buffer(myProducerSlots[entry]->find_back());
		if (buffer) {
			ourConsumers[myObjectId] = buffer;

			if (myProducerSlots[entry] != buffer) {
				if (buffer->verify_as_replacement()) {
					myProducerSlots[entry] = buffer;
				}
			}
			return true;
		}
	}
	return false;
}
template<class T>
inline __declspec(restrict)producer_buffer<T>* const concurrent_queue<T>::create_producer_buffer(const std::size_t withSize) const
{
	const std::size_t size(log2_align(withSize, Buffer_Capacity_Max));

	const std::size_t bufferSize(sizeof(producer_buffer<T>));
	const std::size_t dataBlockSize(sizeof(item_container<T>) * size);
	const std::size_t alignment(alignof(T));

	const std::size_t totalBlockSize(bufferSize + dataBlockSize + alignment);

	uint8_t* totalBlock(nullptr);
	producer_buffer<T>* buffer(nullptr);
	item_container<T>* data(nullptr);

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		totalBlock = new uint8_t[totalBlockSize];

		const std::size_t bufferOffset(0);
		const std::size_t bufferEndAddr(reinterpret_cast<std::size_t>(totalBlock + bufferSize));
		const std::size_t alignmentReminder(bufferEndAddr % alignment);
		const std::size_t alignmentOffset(alignment - (alignmentReminder ? alignmentReminder : alignment));
		const std::size_t dataBlockOffset(bufferOffset + bufferSize + alignmentOffset);

		data = new (totalBlock + dataBlockOffset) item_container<T>[size];
		buffer = new(totalBlock + bufferOffset) producer_buffer<T>(static_cast<size_type>(size), data);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		delete[] totalBlock;
		throw;
	}
#endif

	return buffer;
}
// Find a slot for the buffer in the producer store. Also, update the active producer 
// array, capacity and producer count as is necessary. In the event a new producer array 
// needs to be allocated, threads will compete to do so.
template<class T>
inline void concurrent_queue<T>::push_producer_buffer(producer_buffer<T>* const buffer)
{
	const uint16_t reservedSlot(claim_store_slot());
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	if (!(reservedSlot < Max_Producers)) {
		throw producer_overflow("Max producers exceeded");
	}
#endif
	insert_to_store(buffer, reservedSlot);

	const uint16_t postIterator(myProducerSlotPostIterator.fetch_add(1, std::memory_order_acq_rel) + 1);
	const uint16_t numReserved(myProducerSlotReservation.load(std::memory_order_acquire));

	if (postIterator == numReserved) {
		for (uint16_t i = 0; i < postIterator; ++i) {
			insert_to_store(fetch_from_store(i), i);
		}
		for (uint8_t i = Producer_Slots_Max_Growth_Count - 1; i < Producer_Slots_Max_Growth_Count; --i) {
			if (myProducerArrayStore[i]) {
				try_swap_producer_array(i);
				break;
			}
		}
		try_swap_producer_count(postIterator);
	}
}
// Allocate a buffer array of capacity appropriate to the slot
// and attempt to swap the current value for the new one
template<class T>
inline void concurrent_queue<T>::try_alloc_produer_store_slot(const uint8_t storeArraySlot)
{
	const uint16_t producerCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(storeArraySlot + 1))));

	producer_buffer<T>** const newProducerSlotBlock(new producer_buffer<T>*[producerCapacity]);
	memset(&newProducerSlotBlock[0], 0, sizeof(producer_buffer<T>**) * producerCapacity);

	producer_buffer<T>** expected(nullptr);
	if (!myProducerArrayStore[storeArraySlot].compare_exchange_strong(expected, newProducerSlotBlock, std::memory_order_acq_rel, std::memory_order_acquire)) {
		delete[] newProducerSlotBlock;
	}
}
// Try swapping the current producer array for one from the store, and follow up
// with an attempt to swap the capacity value for the one corresponding to the slot
template<class T>
inline void concurrent_queue<T>::try_swap_producer_array(const uint8_t fromStoreArraySlot)
{
	const uint16_t targetCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(fromStoreArraySlot + 1))));
	for (producer_buffer<T>** expectedProducerArray(myProducerSlots.load(std::memory_order_acquire));; expectedProducerArray = myProducerSlots.load(std::memory_order_acquire)) {

		bool superceeded(false);
		for (uint8_t i = fromStoreArraySlot + 1; i < Producer_Slots_Max_Growth_Count; ++i) {
			if (!myProducerSlots.load(std::memory_order_acquire)) {
				break;
			}
			if (myProducerArrayStore[i].load(std::memory_order_acquire)) {
				superceeded = true;
			}
		}
		if (superceeded) {
			break;
		}
		producer_buffer<T>** const desiredProducerArray(myProducerArrayStore[fromStoreArraySlot].load(std::memory_order_acquire));
		if (myProducerSlots.compare_exchange_strong(expectedProducerArray, desiredProducerArray, std::memory_order_acq_rel, std::memory_order_acquire)) {

			for (uint16_t expectedCapacity(myProducerCapacity.load(std::memory_order_acquire));; expectedCapacity = myProducerCapacity.load(std::memory_order_acquire)) {
				if (!(expectedCapacity < targetCapacity)) {

					break;
				}
				if (myProducerCapacity.compare_exchange_strong(expectedCapacity, targetCapacity, std::memory_order_acq_rel, std::memory_order_acquire)) {
					break;
				}
			}
			break;
		}
	}
}
// Attempt to swap the producer count value for the arg value if the
// existing one is lower
template<class T>
inline void concurrent_queue<T>::try_swap_producer_count(const uint16_t toValue)
{
	const uint16_t desired(toValue);
	for (uint16_t i = myProducerCount.load(std::memory_order_acquire); i < desired; i = myProducerCount.load(std::memory_order_acquire)) {
		uint16_t expected(i);

		if (myProducerCount.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
			break;
		}
	}
}
template<class T>
inline const uint16_t concurrent_queue<T>::claim_store_slot()
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	const uint16_t preIteration(myProducerSlotPreIterator.fetch_add(1, std::memory_order_acq_rel));
	const uint8_t minimumStoreArraySlot(to_store_array_slot(preIteration));
	const uint8_t minimumStoreArraySlotClamp((Producer_Slots_Max_Growth_Count - 1 < minimumStoreArraySlot ? Producer_Slots_Max_Growth_Count - 1 : minimumStoreArraySlot));

	if (!myProducerArrayStore[minimumStoreArraySlotClamp].load(std::memory_order_acquire)) {
		try {
			try_alloc_produer_store_slot(minimumStoreArraySlotClamp);
		}
		catch (...) {
			myProducerSlotPreIterator.fetch_sub(1, std::memory_order_release);
			throw;
		}
	}
	return myProducerSlotReservation.fetch_add(1, std::memory_order_acq_rel);
#else
	const uint16_t reservedSlot(myProducerSlotReservation.fetch_add(1, std::memory_order_acq_rel));
	const uint8_t storeArraySlot(ToStoreArraySlot(reservedSlot));
	if (!myProducerArrayStore[storeArraySlot]) {
		TryAllocProducerStoreSlot(storeArraySlot);
	}
	return reservedSlot;
#endif
}
template<class T>
inline producer_buffer<T>* const concurrent_queue<T>::fetch_from_store(const uint16_t storeSlot) const
{
	for (uint8_t i = Producer_Slots_Max_Growth_Count - 1; i < Producer_Slots_Max_Growth_Count; --i) {
		producer_buffer<T>** const producerArray(myProducerArrayStore[i]);
		if (!producerArray) {
			continue;
		}
		producer_buffer<T>* const producerBuffer(producerArray[storeSlot]);
		if (!producerBuffer) {
			continue;
		}
		return producerBuffer;
	}
	return nullptr;
}
template<class T>
inline void concurrent_queue<T>::insert_to_store(producer_buffer<T>* const buffer, const uint16_t storeSlot)
{
	for (uint8_t i = Producer_Slots_Max_Growth_Count - 1; i < Producer_Slots_Max_Growth_Count; --i) {
		producer_buffer<T>** const producerArray(myProducerArrayStore[i].load(std::memory_order_acquire));
		if (!producerArray) {
			continue;
		}
		producerArray[storeSlot] = buffer;
		break;
	}
}
template<class T>
inline const uint8_t concurrent_queue<T>::to_store_array_slot(const uint16_t storeSlot) const
{
	const float fSourceStoreSlot(log2f(static_cast<float>(storeSlot)));
	const uint8_t sourceStoreSlot(static_cast<uint8_t>(fSourceStoreSlot));
	return sourceStoreSlot;
}
template<class T>
inline const typename concurrent_queue<T>::size_type concurrent_queue<T>::log2_align(const std::size_t from, const std::size_t clamp) const
{
	const std::size_t from_(from < 2 ? 2 : from);

	const float flog2(std::log2f(static_cast<float>(from_)));
	const float nextLog2(std::ceil(flog2));
	const float fNextVal(std::powf(2.f, nextLog2));

	const std::size_t nextVal(static_cast<size_t>(fNextVal));
	const std::size_t clampedNextVal((clamp < nextVal) ? clamp : nextVal);

	return static_cast<size_type>(clampedNextVal);
}

template <class T>
class producer_buffer
{
public:
	typedef typename concurrent_queue<T>::size_type size_type;

	producer_buffer(const size_type aCapacity, item_container<T>* const aDataBlock);
	~producer_buffer() = default;

	template<class ...Arg>
	inline const bool try_push(Arg&&... in);
	inline const bool try_pop(T& out);

	// Deallocates all buffers in the list
	inline void destroy_all();

	inline const std::size_t size() const;

	inline const size_type capacity() const;

	// Makes sure that predecessors are wholly unused
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline const bool verify_as_replacement();
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline const bool verify_as_replacement();

	// Searches the buffer list towards the front for
	// the first buffer contining entries
	inline producer_buffer<T>* const find_back();
	// Pushes a newly allocated buffer buffer to the front of the 
	// buffer list
	inline void push_front(producer_buffer<T>* const aNewBuffer);

	inline void unsafe_clear();

private:
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>* = nullptr>
	inline void write_in(const size_type aSlot, U&& in);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>* = nullptr>
	inline void write_in(const size_type aSlot, U&& in);
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>* = nullptr>
	inline void write_in(const size_type aSlot, const U& in);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>* = nullptr>
	inline void write_in(const size_type aSlot, const U& in);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>* = nullptr>
	inline void write_out(const size_type aSlot, U& out);
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void write_out(const size_type aSlot, U& out);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void write_out(const size_type aSlot, U& out);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void post_pop_cleanup(const size_type aReadSlot);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void post_pop_cleanup(const size_type aReadSlot);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void check_for_damage();
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void check_for_damage();

	inline void reintegrate_failed_entries(const size_type aFailCount);

	// Searches the buffer list towards the back for the last node
	inline producer_buffer<T>* const find_tail();

	static const size_type Buffer_Lock_Offset = concurrent_queue<T>::Buffer_Capacity_Max + concurrent_queue<T>::Max_Producers;

	size_type myWriteSlot;
	std::atomic<size_type> myPostWriteIterator;

	// The tail becomes the de-facto storage place for unused buffers,
	// until they are destroyed with the entire structure
	producer_buffer<T>* myPrevious;
	producer_buffer<T>* myNext;

	const size_type myCapacity;
	item_container<T>* const myDataBlock;
	CQ_PADDING(128 - sizeof(void*));
	std::atomic<size_type> myReadSlot;
	CQ_PADDING(64 - sizeof(size_type));
	std::atomic<size_type> myPreReadIterator;

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	std::atomic<uint16_t> myFailiureCount;
	std::atomic<uint16_t> myFailiureIndex;
	bool myValidFlag;
	CQ_PADDING(64 - 5);
	std::atomic<size_type> myPostReadIterator;
#endif
};
template<class T>
inline producer_buffer<T>::producer_buffer(const size_type aCapacity, item_container<T>* const aDataBlock)
	: myNext(nullptr)
	, myPrevious(nullptr)
	, myDataBlock(aDataBlock)
	, myCapacity(aCapacity)
	, myReadSlot(0)
	, myPreReadIterator(0)
	, myWriteSlot(0)
	, myPostWriteIterator(0)
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	, myFailiureIndex(0)
	, myFailiureCount(0)
	, myPostReadIterator(0)
	, myValidFlag(true)
#endif
{
}

template<class T>
inline void producer_buffer<T>::destroy_all()
{
	producer_buffer<T>* current = find_tail();

	while (current) {
		const size_type lastCapacity(current->capacity());
		uint8_t* const lastBlock(reinterpret_cast<uint8_t*>(current));
		item_container<T>* const lastDataBlock(current->myDataBlock);

		current = current->myNext;
		if (!std::is_trivially_destructible<T>::value) {
			for (size_type i = 0; i < lastCapacity; ++i) {
				lastDataBlock[i].~item_container<T>();
			}
		}
		delete[] lastBlock;
	}
}

// Searches buffer list towards the front for
// a buffer with contents. Returns null upon 
// failiure
template<class T>
inline producer_buffer<T>* const producer_buffer<T>::find_back()
{
	producer_buffer<T>* back(this);

	while (back) {
		const size_type readSlot(back->myReadSlot.load(std::memory_order_acquire));
		const size_type postWrite(back->myPostWriteIterator.load(std::memory_order_acquire));

		const bool match(readSlot == postWrite);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		const bool veto(back->myFailiureCount.load(std::memory_order_acquire) != back->myFailiureIndex.load(std::memory_order_acquire));
		const bool valid(!match | veto);
#else
		const bool valid(!match);
#endif
		if (valid) {
			break;
		}

		back = back->myNext;
	}
	return back;
}

template<class T>
inline const std::size_t producer_buffer<T>::size() const
{
	std::size_t size(myPostWriteIterator.load(std::memory_order_relaxed));
	const std::size_t readSlot(myReadSlot.load(std::memory_order_acquire));
	size -= readSlot;

	if (myNext)
		size += myNext->size();

	return size;
}

template<class T>
inline  const typename producer_buffer<T>::size_type producer_buffer<T>::capacity() const
{
	return myCapacity;
}
template<class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline const bool producer_buffer<T>::verify_as_replacement()
{
	return true;
}
template<class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline const bool producer_buffer<T>::verify_as_replacement()
{
	producer_buffer<T>* previous(myPrevious);
	while (previous) {
		if (previous->myValidFlag) {
			const size_type preRead(previous->myPreReadIterator.load(std::memory_order_acquire));
			for (size_type i = 0; i < previous->myCapacity; ++i) {
				const size_type index((preRead - i) % previous->myCapacity);

				if (previous->myDataBlock[index].get_state_local() != Item_state::Empty) {
					return false;
				}
			}
		}
		previous->myValidFlag = false;
		previous = previous->myPrevious;
	}
	return true;
}
template<class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T>::check_for_damage()
{
}
template<class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T>::check_for_damage()
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	const size_type preRead(myPreReadIterator.load(std::memory_order_acquire));
	const size_type preReadLockOffset(preRead - Buffer_Lock_Offset);
	if (preReadLockOffset != myPostReadIterator.load(std::memory_order_acquire)) {
		return;
	}

	const uint16_t failiureIndex(myFailiureIndex.load(std::memory_order_acquire));
	const uint16_t failiureCount(myFailiureCount.load(std::memory_order_acquire));
	const uint16_t difference(failiureCount - failiureIndex);

	const bool failCheckA(0 == difference);
	const bool failCheckB(!(difference < concurrent_queue<T>::Max_Producers));
	if (failCheckA | failCheckB) {
		return;
	}

	const size_type toReintegrate(failiureCount - failiureIndex);

	uint16_t expected(failiureIndex);
	const uint16_t desired(failiureCount);
	if (myFailiureIndex.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
		reintegrate_failed_entries(toReintegrate);

		myPostReadIterator.fetch_sub(toReintegrate);
		myReadSlot.fetch_sub(toReintegrate);
		myPreReadIterator.fetch_sub(Buffer_Lock_Offset + toReintegrate);
	}
#endif
}
template<class T>
inline void producer_buffer<T>::push_front(producer_buffer<T>* const aNewBuffer)
{
	producer_buffer<T>* last(this);
	while (last->myNext) {
		last = last->myNext;
	}
	last->myNext = aNewBuffer;
	aNewBuffer->myPrevious = last;
}
template<class T>
inline void producer_buffer<T>::unsafe_clear()
{
	myPreReadIterator.store(myPostWriteIterator.load(std::memory_order_relaxed));
	myReadSlot.store(myPostWriteIterator.load(std::memory_order_relaxed));

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	myFailiureCount.store(0, std::memory_order_relaxed);
	myFailiureIndex.store(0, std::memory_order_relaxed);
	myValidFlag = true;

	myPostReadIterator.store(myPostWriteIterator.load(std::memory_order_relaxed));
#endif
	if (myNext) {
		myNext->unsafe_clear();
	}
}
template<class T>
template<class ...Arg>
inline const bool producer_buffer<T>::try_push(Arg && ...in)
{
	const size_type slotTotal(myWriteSlot++);
	const size_type slot(slotTotal % myCapacity);

	std::atomic_thread_fence(std::memory_order_acquire);

	if (myDataBlock[slot].get_state_local() != Item_state::Empty) {
		--myWriteSlot;
		return false;
	}

	write_in(slot, std::forward<Arg>(in)...);

	myDataBlock[slot].set_state_local(Item_state::Valid);

	myPostWriteIterator.fetch_add(1, std::memory_order_release);

	return true;
}
template<class T>
inline const bool producer_buffer<T>::try_pop(T & out)
{
	const size_type lastWritten(myPostWriteIterator.load(std::memory_order_acquire));
	const size_type slotReserved(myPreReadIterator.fetch_add(1, std::memory_order_acq_rel) + 1);
	const size_type avaliable(lastWritten - slotReserved);

	if (myCapacity < avaliable) {
		myPreReadIterator.fetch_sub(1, std::memory_order_release);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		check_for_damage();
#endif
		return false;
	}
	const size_type readSlotTotal(myReadSlot.fetch_add(1, std::memory_order_acq_rel));
	const size_type readSlot(readSlotTotal % myCapacity);

	write_out(readSlot, out);

	post_pop_cleanup(readSlot);

	return true;
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void producer_buffer<T>::write_in(const size_type aSlot, U&& in)
{
	myDataBlock[aSlot].store(std::move(in));
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void producer_buffer<T>::write_in(const size_type aSlot, U&& in)
{
	try {
		myDataBlock[aSlot].store(std::move(in));
	}
	catch (...) {
		--myWriteSlot;
		throw;
	}
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void producer_buffer<T>::write_in(const size_type aSlot, const U& in)
{
	myDataBlock[aSlot].store(in);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void producer_buffer<T>::write_in(const size_type aSlot, const U& in)
{
	try {
		myDataBlock[aSlot].store(in);
	}
	catch (...) {
		--myWriteSlot;
		throw;
	}
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>*>
inline void producer_buffer<T>::write_out(const size_type aSlot, U& out)
{
	myDataBlock[aSlot].move(out);
}
template<class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T>::post_pop_cleanup(const size_type aReadSlot)
{
	myDataBlock[aReadSlot].set_state(Item_state::Empty);
	std::atomic_thread_fence(std::memory_order_release);
}
template<class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T>::post_pop_cleanup(const size_type aReadSlot)
{
	myDataBlock[aReadSlot].set_state(Item_state::Empty);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	myDataBlock[aReadSlot].reset_ref();
	myPostReadIterator.fetch_add(1, std::memory_order_release);
#else
	std::atomic_thread_fence(std::memory_order_release);
#endif
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T>::write_out(const size_type aSlot, U& out)
{
	myDataBlock[aSlot].assign(out);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T>::write_out(const size_type aSlot, U& out)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		myDataBlock[aSlot].try_move(out);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		if (myFailiureCount.fetch_add(1, std::memory_order_acq_rel) == myFailiureIndex.load(std::memory_order_acquire)) {
			myPreReadIterator.fetch_add(Buffer_Lock_Offset, std::memory_order_release);
		}
		myDataBlock[aSlot].set_state(Item_state::Failed);
		myPostReadIterator.fetch_add(1, std::memory_order_release);
		throw;
	}
#endif
}
template<class T>
inline void producer_buffer<T>::reintegrate_failed_entries(const size_type aFailCount)
{
	const size_type readSlotTotal(myReadSlot.load(std::memory_order_acquire));
	const size_type readSlotTotalOffset(readSlotTotal + myCapacity);

	const size_type startIndex(readSlotTotalOffset - 1);

	size_type numRedirected(0);
	for (size_type i = 0, j = startIndex; numRedirected != aFailCount; ++i, --j) {
		const size_type currentIndex((startIndex - i) % myCapacity);
		item_container<T>& currentItem(myDataBlock[currentIndex]);
		const Item_state currentState(currentItem.get_state_local());

		if (currentState == Item_state::Failed) {
			const size_type toRedirectIndex((startIndex - numRedirected) % myCapacity);
			item_container<T>& toRedirect(myDataBlock[toRedirectIndex]);

			toRedirect.redirect(currentItem);
			currentItem.set_state_local(Item_state::Valid);
			++numRedirected;
		}
	}
}
template<class T>
inline producer_buffer<T>* const producer_buffer<T>::find_tail()
{
	producer_buffer<T>* tail(this);
	while (tail->myPrevious) {
		tail = tail->myPrevious;
	}
	return tail;
}

// Class used to be able to redirect access to data in the event
// of an exception being thrown
template <class T>
class item_container
{
public:
	item_container<T>(const item_container<T>&) = delete;
	item_container<T>& operator=(const item_container&) = delete;

	inline item_container();

	inline void store(const T& in);
	inline void store(T&& in);

	inline void redirect(item_container<T>& to);

	template<class U = T, std::enable_if_t<std::is_move_assignable<U>::value>* = nullptr>
	inline void try_move(U& out);
	template<class U = T, std::enable_if_t<!std::is_move_assignable<U>::value>* = nullptr>
	inline void try_move(U& out);

	inline void assign(T& out);
	inline void move(T& out);

	inline const Item_state get_state_local() const;
	inline void set_state(const Item_state state);
	inline void set_state_local(const Item_state state);

	inline void reset_ref();

private:
	// May or may not reference this continer
	inline item_container<T>& reference() const;

	// Simple bitmask that represents the pointer portion of a 64 bit integer
	static const uint64_t ourPtrMask = (uint64_t(std::numeric_limits<uint32_t>::max()) << 16 | uint64_t(std::numeric_limits<uint16_t>::max()));

	T myData;
	union
	{
		uint64_t myStateBlock;
		item_container<T>* myReference;
		struct
		{
			uint16_t trash[3];
			Item_state myState;
		};
	};
};
template<class T>
inline item_container<T>::item_container()
	: myData()
	, myReference(this)
{
}
template<class T>
inline void item_container<T>::store(const T & in)
{
	myData = in;
	myReference = this;
}
template<class T>
inline void item_container<T>::store(T && in)
{
	myData = std::move(in);
	myReference = this;
}
template<class T>
inline void item_container<T>::redirect(item_container<T>& to)
{
	const uint64_t otherPtrBlock(to.myStateBlock & ourPtrMask);
	myStateBlock &= ~ourPtrMask;
	myStateBlock |= otherPtrBlock;
}
template<class T>
inline void item_container<T>::assign(T & out)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	out = reference().myData;
#else
	out = myData;
#endif
}
template<class T>
inline void item_container<T>::move(T & out)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	out = std::move(reference().myData);
#else
	out = std::move(myData);
#endif
}
template<class T>
inline void item_container<T>::set_state(const Item_state state)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	reference().myState = state;
#else
	myState = state;
#endif
}
template<class T>
inline void item_container<T>::set_state_local(const Item_state state)
{
	myState = state;
}
template<class T>
inline void item_container<T>::reset_ref()
{
	myReference = this;
}
template<class T>
inline const Item_state item_container<T>::get_state_local() const
{
	return myState;
}
template<class T>
inline item_container<T>& item_container<T>::reference() const
{
	return *reinterpret_cast<item_container<T>*>(myStateBlock & ourPtrMask);
}
template<class T>
template<class U, std::enable_if_t<std::is_move_assignable<U>::value>*>
inline void item_container<T>::try_move(U& out)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	out = std::move(reference().myData);
#else
	out = std::move(myData);
#endif
}
template<class T>
template<class U, std::enable_if_t<!std::is_move_assignable<U>::value>*>
inline void item_container<T>::try_move(U& out)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	out = reference().myData;
#else
	out = myData;
#endif
}

enum class Item_state : int8_t
{
	Empty,
	Valid,
	Failed
};
}
#pragma warning(pop)
