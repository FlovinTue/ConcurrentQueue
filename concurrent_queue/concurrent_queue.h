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

#pragma once

#include <atomic>
#include <vector>
#include <limits>
#include <atomic_shared_ptr.h>


// Exception handling may be enabled for basic exception safety at the cost of 
// a slight performance decrease


/* #define CQ_ENABLE_EXCEPTIONHANDLING */

// In the event an exception is thrown during a pop operation, some entries may
// be dequeued out-of-order as some consumers may already be halfway through a 
// pop operation before reintegration efforts are started.

#ifdef CQ_ENABLE_EXCEPTIONHANDLING 
#define CQ_BUFFER_NOTHROW_POP_MOVE(type) (std::is_nothrow_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_POP_ASSIGN(type) (!CQ_BUFFER_NOTHROW_POP_MOVE(type) && (std::is_nothrow_assignable<type&, type>::value))
#define CQ_BUFFER_NOTHROW_PUSH_MOVE(type) (std::is_nothrow_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_PUSH_ASSIGN(type) (std::is_nothrow_assignable<type&, type>::value)
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
#define CQ_CACHELINE_SIZE 64u

// For anonymous struct and alignas 
#pragma warning(push, 2)
#pragma warning(disable : 4201) 
#pragma warning(disable : 4324) 

#undef min
#undef max

namespace gdul {

namespace cqdetail {

class producer_overflow : public std::runtime_error
{
public:
	producer_overflow(const char* errorMessage) : runtime_error(errorMessage) {}
};

template <class PtrType>
struct consumer_wrapper;

template <class T, class Allocator>
class producer_buffer;

template <class T>
class item_container;

template <class T, class Allocator>
class dummy_container;

template <class IndexType, class Allocator>
class index_pool;

enum class item_state : uint8_t;

std::size_t log2_align(std::size_t from, std::size_t clamp);

template <class T, class Allocator>
std::size_t calc_block_size(std::size_t fromCapacity);

inline uint8_t to_store_array_slot(uint16_t producerIndex);
inline uint16_t to_store_array_capacity(uint16_t storeSlot);

constexpr std::size_t next_aligned_to(std::size_t addr, std::size_t align);
constexpr std::size_t aligned_size(std::size_t byteSize, std::size_t align);

template <class T, class Allocator>
class shared_ptr_allocator_adaptor;

// The maximum allowed consumption from a producer per visit
static const uint16_t Consumer_Force_Relocation_Pop_Count = 24;
// Maximum number of times the producer slot array can grow
static constexpr uint8_t Producer_Slots_Max_Growth_Count = 15;

static constexpr uint16_t Max_Producers = std::numeric_limits<int16_t>::max() - 1;
}
// The WizardLoaf MPMC unbounded concurrent_queue.
// FIFO is respected within the context of single producers. push operations are wait-free
// (assuming pre-allocated memory using reserve() alt. a wait-free allocator), try_pop & 
// size are lock-free. Basic exception safety may be enabled via define CQ_ENABLE_EXCEPTIONHANDLING 
// at the price of a slight performance decrease.
template <class T, class Allocator = std::allocator<uint8_t>>
class concurrent_queue
{
public:
	typedef std::size_t size_type;
	typedef Allocator allocator_type;

	inline concurrent_queue();
	inline concurrent_queue(allocator_type allocator);
	inline concurrent_queue(size_type initProducerCapacity);
	inline concurrent_queue(size_type initProducerCapacity, allocator_type allocator);
	inline ~concurrent_queue();

	inline void push(const T& in);
	inline void push(T&& in);

	bool try_pop(T& out);

	// Reserves a minimum capacity for the calling producer
	inline void reserve(size_type capacity);

	void unsafe_clear();

	// size hint
	inline size_type size() const;

	// Not quite size_type max because we need some leaway in case we
	// need to throw consumers out of a buffer whilst repairing it
	static constexpr size_type Buffer_Capacity_Max = ~(std::numeric_limits<size_type>::max() >> 3) / 2;

private:
	friend class cqdetail::producer_buffer<T, Allocator>;

	typedef cqdetail::producer_buffer<T, Allocator> buffer_type;
	typedef cqdetail::shared_ptr_allocator_adaptor<uint8_t, Allocator> allocator_adapter_type;
	typedef shared_ptr<buffer_type, allocator_adapter_type> shared_ptr_type;
	typedef atomic_shared_ptr<buffer_type, allocator_adapter_type> atomic_shared_ptr_type;

	typedef int nonsense;

	template <class ...Arg>
	void push_internal(Arg&&... in);

	inline void init_producer(size_type withCapacity);

	inline bool relocate_consumer();

	inline shared_ptr_type create_producer_buffer(std::size_t withSize);
	inline void push_producer_buffer(shared_ptr_type buffer);
	inline void try_alloc_produer_store_slot(uint8_t storeArraySlot);
	inline void try_swap_producer_array(uint8_t aromStoreArraySlot);
	inline void try_swap_producer_count(uint16_t toValue);
	inline void try_swap_producer_array_capacity(uint16_t toCapacity);
	inline bool has_producer_array_been_superceeded(uint16_t arraySlot);

	inline uint16_t claim_store_slot();
	inline shared_ptr_type fetch_from_store(uint16_t bufferSlot) const;
	inline void insert_to_store(shared_ptr_type buffer, uint16_t bufferSlot, uint16_t storeSlot);

	const size_type myInitBufferCapacity;
	const size_type myInstanceIndex;

	static thread_local std::vector<shared_ptr_type, allocator_type> ourProducers;
	static thread_local std::vector<cqdetail::consumer_wrapper<shared_ptr_type>, allocator_type> ourConsumers;

	static cqdetail::index_pool<size_type, allocator_type> ourIndexPool;
	static cqdetail::dummy_container<T, Allocator> ourDummyContainer;

	static std::atomic<uint16_t> ourRelocationIndex;

	std::atomic<atomic_shared_ptr_type*> myProducerArrayStore[cqdetail::Producer_Slots_Max_Growth_Count];

	union
	{
		std::atomic<atomic_shared_ptr_type*> myProducerSlots;
		const shared_ptr_type* myDebugView;
	};
	allocator_type myAllocator;

	std::atomic<uint16_t> myProducerCount;
	std::atomic<uint16_t> myProducerCapacity;
	std::atomic<uint16_t> myProducerSlotReservation;
	std::atomic<uint16_t> myProducerSlotPostIterator;
};

template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::concurrent_queue()
	: concurrent_queue<T, Allocator>(std::allocator<uint8_t>())
{
}
template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::concurrent_queue(allocator_type allocator)
	: concurrent_queue<T, Allocator>(2, allocator)
{
}
template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::concurrent_queue(typename concurrent_queue<T, Allocator>::size_type initProducerCapacity)
	: concurrent_queue<T, Allocator>(initProducerCapacity, std::allocator<uint8_t>())
{
}
template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::concurrent_queue(size_type initProducerCapacity, allocator_type allocator)
	: myInstanceIndex(ourIndexPool.get())
	, myProducerCapacity(0)
	, myProducerCount(0)
	, myProducerSlotPostIterator(0)
	, myProducerSlotReservation(0)
	, myProducerSlots(nullptr)
	, myInitBufferCapacity(cqdetail::log2_align(initProducerCapacity, Buffer_Capacity_Max))
	, myProducerArrayStore{ nullptr }
	, myAllocator(allocator)
{
	static_assert(std::is_same<uint8_t, Allocator::value_type>(), "Value type for allocator must be uint8_t");
}
template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::~concurrent_queue()
{
	std::atomic_thread_fence(std::memory_order_acquire);

	const uint16_t producerCount(myProducerCount.load(std::memory_order_relaxed));
	const uint16_t slots(cqdetail::to_store_array_slot(producerCount - static_cast<bool>(producerCount)) + static_cast<bool>(producerCount));

	for (uint16_t i = 0; i < slots; ++i) {
		const uint16_t slotSize(static_cast<uint16_t>(std::pow(2, i + 1)));
		atomic_shared_ptr_type* const storeSlot(myProducerArrayStore[i].load(std::memory_order_relaxed));

		for (uint16_t slotIndex = 0; slotIndex < slotSize; ++slotIndex) {
			if (storeSlot[slotIndex]){
				storeSlot[slotIndex]->unsafe_clear();
				storeSlot[slotIndex]->invalidate();
			}
			storeSlot[slotIndex].unsafe_store(nullptr);
		}

		const std::size_t slotByteSize(slotSize * sizeof(atomic_shared_ptr_type));
		uint8_t* const storeBlock(reinterpret_cast<uint8_t*>(storeSlot));
		myAllocator.deallocate(storeBlock, slotByteSize);
	}

	ourIndexPool.add(myInstanceIndex);
}

template<class T, class Allocator>
void concurrent_queue<T, Allocator>::push(const T & in)
{
	push_internal<const T&>(in);
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::push(T && in)
{
	push_internal<T&&>(std::move(in));
}
template<class T, class Allocator>
template<class ...Arg>
inline void concurrent_queue<T, Allocator>::push_internal(Arg&& ...in)
{
	const size_type producerSlot(myInstanceIndex);

	if (!(producerSlot < ourProducers.size()))
		ourProducers.resize(producerSlot + 1, ourDummyContainer.myDummyBuffer);

	buffer_type* buffer(ourProducers[producerSlot].get_owned());

	if (!buffer->try_push(std::forward<Arg>(in)...)) {
		if (buffer->is_valid()) {
			shared_ptr_type next(create_producer_buffer(std::size_t(buffer->get_capacity()) * 2));
			buffer->push_front(next);
			ourProducers[producerSlot] = std::move(next);
		}
		else {
			init_producer(myInitBufferCapacity);
		}
		ourProducers[producerSlot]->try_push(std::forward<Arg>(in)...);
	}
}
template<class T, class Allocator>
bool concurrent_queue<T, Allocator>::try_pop(T & out)
{
	const size_type consumerSlot(myInstanceIndex);
	if (!(consumerSlot < ourConsumers.size()))
		ourConsumers.resize(consumerSlot + 1, cqdetail::consumer_wrapper<shared_ptr_type>(ourDummyContainer.myDummyBuffer));

	buffer_type* buffer = ourConsumers[consumerSlot].myPtr.get_owned();

	for (uint16_t attempt(0); !buffer->try_pop(out); ++attempt) {
		if (!(attempt < myProducerCount.load(std::memory_order_relaxed)))
			return false;

		if (!relocate_consumer())
			return false;

		buffer = ourConsumers[consumerSlot].myPtr.get_owned();
	}

	if (!(++ourConsumers[consumerSlot].myPopCounter < cqdetail::Consumer_Force_Relocation_Pop_Count)) {
		relocate_consumer();
	}

	return true;
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::reserve(typename concurrent_queue<T, Allocator>::size_type capacity)
{
	const size_type producerSlot(myInstanceIndex);

	if (!(producerSlot < ourProducers.size()))
		ourProducers.resize(producerSlot + 1, ourDummyContainer.myDummyBuffer);

	if (!ourProducers[producerSlot]->is_valid()) {
		init_producer(capacity);
		return;
	}
	if (ourProducers[producerSlot]->get_capacity() < capacity) {
		const size_type alignedCapacity(cqdetail::log2_align(capacity, Buffer_Capacity_Max));
		shared_ptr_type buffer(create_producer_buffer(alignedCapacity));
		ourProducers[producerSlot]->push_front(buffer);
		ourProducers[producerSlot] = std::move(buffer);
	}
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::unsafe_clear()
{
	std::atomic_thread_fence(std::memory_order_acquire);

	for (uint16_t i = 0; i < myProducerCount.load(std::memory_order_relaxed); ++i) {
		myProducerSlots[i]->unsafe_clear();
	}

	std::atomic_thread_fence(std::memory_order_release);
}
template<class T, class Allocator>
inline typename concurrent_queue<T, Allocator>::size_type concurrent_queue<T, Allocator>::size() const
{
	const uint16_t producerCount(myProducerCount.load(std::memory_order_acquire));

	size_type accumulatedSize(0);
	for (uint16_t i = 0; i < producerCount; ++i) {
		accumulatedSize += myProducerSlots[i]->size();
	}
	return accumulatedSize;
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::init_producer(typename concurrent_queue<T, Allocator>::size_type withCapacity)
{
	shared_ptr_type newBuffer(create_producer_buffer(withCapacity));
	push_producer_buffer(newBuffer);
	ourProducers[myInstanceIndex] = std::move(newBuffer);
}
template<class T, class Allocator>
inline bool concurrent_queue<T, Allocator>::relocate_consumer()
{
	const uint16_t relocation(ourRelocationIndex.fetch_add(1, std::memory_order_acq_rel));
	const uint16_t producers(myProducerCount.load(std::memory_order_acquire));

	for (uint16_t i = 0, j = relocation; i < producers; ++i, ++j) {
		const uint16_t entry(j % producers);
		atomic_shared_ptr_type* const arr(myProducerSlots.load(std::memory_order_relaxed));

		shared_ptr_type producerBuffer(arr[entry].load());
		if (!producerBuffer->is_active()) {
			shared_ptr_type successor = producerBuffer->find_back();
			if (successor) {
				if (myProducerSlots[entry].get_owned() != successor.get_owned()) {
					if (producerBuffer->verify_successor(successor)) {
						producerBuffer = std::move(successor);
						myProducerSlots[entry].store(producerBuffer);
					}
				}
			}
		}
		if (producerBuffer) {
			ourConsumers[myInstanceIndex].myPtr = std::move(producerBuffer);
			ourConsumers[myInstanceIndex].myPopCounter = 0;
			return true;
		}
	}
	return false;
}
template<class T, class Allocator>
inline typename concurrent_queue<T, Allocator>::shared_ptr_type concurrent_queue<T, Allocator>::create_producer_buffer(std::size_t withSize)
{
	const std::size_t log2size(cqdetail::log2_align(withSize, Buffer_Capacity_Max));

	const std::size_t alignOfControlBlock(alignof(aspdetail::control_block<void*, allocator_adapter_type>));
	const std::size_t alignOfData(alignof(T));
	const std::size_t alignOfBuffer(alignof(buffer_type));

	const std::size_t maxAlignBuffData(alignOfBuffer < alignOfData ? alignOfData : alignOfBuffer);
	const std::size_t maxAlign(maxAlignBuffData < alignOfControlBlock ? alignOfControlBlock : maxAlignBuffData);

	const std::size_t bufferByteSize(sizeof(buffer_type));
	const std::size_t dataBlockByteSize(sizeof(cqdetail::item_container<T>) * log2size);
	const std::size_t controlBlockByteSize(shared_ptr_type::alloc_size_claim());

	const std::size_t controlBlockSize(cqdetail::aligned_size(controlBlockByteSize, maxAlign));
	const std::size_t bufferSize(cqdetail::aligned_size(bufferByteSize, maxAlign));
	const std::size_t dataBlockSize(cqdetail::aligned_size(dataBlockByteSize, maxAlign));

	const std::size_t totalBlockSize(controlBlockSize + bufferSize + dataBlockSize + maxAlign);

	uint8_t* totalBlock(nullptr);

	buffer_type* buffer(nullptr);
	cqdetail::item_container<T>* data(nullptr);

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try
	{
#endif
		totalBlock = myAllocator.allocate(totalBlockSize);

		const std::size_t totalBlockBegin(reinterpret_cast<std::size_t>(totalBlock));
		const std::size_t controlBlockBegin(cqdetail::next_aligned_to(totalBlockBegin, maxAlign));
		const std::size_t bufferBegin(controlBlockBegin + controlBlockSize);
		const std::size_t dataBegin(bufferBegin + bufferSize);

		const std::size_t bufferOffset(bufferBegin - totalBlockBegin);
		const std::size_t dataOffset(dataBegin - totalBlockBegin);

		data = new (totalBlock + dataOffset) cqdetail::item_container<T>[log2size];
		buffer = new(totalBlock + bufferOffset) buffer_type(static_cast<size_type>(log2size), data);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...)
	{
		myAllocator.deallocate(totalBlock, totalBlockSize);
		throw;
	}
#endif

	allocator_adapter_type allocAdaptor(totalBlock, totalBlockSize);

	auto deleter = [](buffer_type* obj)
	{
		(*obj).~producer_buffer<T, Allocator>();
	};

	shared_ptr_type returnValue(buffer, deleter, allocAdaptor);

	return returnValue;
}
// Find a slot for the buffer in the producer store. Also, update the active producer 
// array, capacity and producer count as is necessary. In the event a new producer array 
// needs to be allocated, threads will compete to do so.
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::push_producer_buffer(shared_ptr_type buffer)
{
	const uint16_t bufferSlot(claim_store_slot());
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	if (!(bufferSlot < cqdetail::Max_Producers)) {
		throw cqdetail::producer_overflow("Max producers exceeded");
	}
#endif
	insert_to_store(std::move(buffer), bufferSlot, cqdetail::to_store_array_slot(bufferSlot));

	const uint16_t postIterator(myProducerSlotPostIterator.fetch_add(1, std::memory_order_acq_rel) + 1);
	const uint16_t numReserved(myProducerSlotReservation.load(std::memory_order_acquire));

	// If postIterator and numReserved match, that means all indices below postIterator have been properly inserted
	if (postIterator == numReserved) {

		for (uint16_t i = 0; i < postIterator; ++i) {
			insert_to_store(fetch_from_store(i), i, cqdetail::to_store_array_slot(postIterator - 1));
		}

		try_swap_producer_array(cqdetail::to_store_array_slot(postIterator - 1));
		try_swap_producer_count(postIterator);
	}
}
// Allocate a buffer array of capacity appropriate to the slot
// and attempt to swap the current value for the new one
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_alloc_produer_store_slot(uint8_t storeArraySlot)
{
	const uint16_t producerCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(storeArraySlot + 1))));

	const std::size_t blockSize(sizeof(atomic_shared_ptr_type) * producerCapacity);

	uint8_t* const block(myAllocator.allocate(blockSize));

	atomic_shared_ptr_type* const newProducerSlotBlock(reinterpret_cast<atomic_shared_ptr_type*>(block));

	for (std::size_t i = 0; i < producerCapacity; ++i) {
		atomic_shared_ptr_type* const item(&newProducerSlotBlock[i]);
		new (item) (atomic_shared_ptr_type);
	}
	atomic_shared_ptr_type* expected(nullptr);
	if (!myProducerArrayStore[storeArraySlot].compare_exchange_strong(expected, newProducerSlotBlock, std::memory_order_acq_rel, std::memory_order_acquire)) {

		for (std::size_t i = 0; i < producerCapacity; ++i) {
			newProducerSlotBlock[i].~atomic_shared_ptr_type();
		}
		myAllocator.deallocate(block, blockSize);
	}
}
// Try swapping the current producer array for one from the store, and follow up
// with an attempt to swap the capacity value for the one corresponding to the slot
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_swap_producer_array(uint8_t fromStoreArraySlot)
{
	atomic_shared_ptr_type* expectedProducerArray(myProducerSlots.load(std::memory_order_acquire));
	for (;;) {

		if (has_producer_array_been_superceeded(fromStoreArraySlot)) {
			break;
		}

		atomic_shared_ptr_type* const desiredProducerArray(myProducerArrayStore[fromStoreArraySlot].load(std::memory_order_acquire));
		if (myProducerSlots.compare_exchange_strong(expectedProducerArray, desiredProducerArray, std::memory_order_release)) {

			try_swap_producer_array_capacity(cqdetail::to_store_array_capacity(fromStoreArraySlot));
			break;
		}
	}
}
// Attempt to swap the producer count value for the arg value if the
// existing one is lower
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_swap_producer_count(uint16_t toValue)
{
	const uint16_t desired(toValue);
	for (uint16_t i = myProducerCount.load(std::memory_order_acquire); i < desired;) {

		uint16_t& expected(i);
		if (myProducerCount.compare_exchange_strong(expected, desired, std::memory_order_release)) {
			break;
		}
	}
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_swap_producer_array_capacity(uint16_t toCapacity)
{
	const uint16_t desiredCapacity(toCapacity);
	uint16_t expectedCapacity(myProducerCapacity.load(std::memory_order_acquire));

	for (; expectedCapacity < desiredCapacity;) {
		if (myProducerCapacity.compare_exchange_strong(expectedCapacity, desiredCapacity, std::memory_order_acq_rel, std::memory_order_acquire)) {
			break;
		}
	}
}
template<class T, class Allocator>
inline bool concurrent_queue<T, Allocator>::has_producer_array_been_superceeded(uint16_t arraySlot)
{
	const atomic_shared_ptr_type* const activeArray(myProducerSlots.load(std::memory_order_acquire));

	if (!activeArray) {
		return false;
	}

	for (uint8_t higherStoreSlot = arraySlot + 1; higherStoreSlot < cqdetail::Producer_Slots_Max_Growth_Count; ++higherStoreSlot) {
		if (myProducerArrayStore[higherStoreSlot].load(std::memory_order_acquire) == activeArray) {
			return true;
		}
	}

	return false;
}
template<class T, class Allocator>
inline uint16_t concurrent_queue<T, Allocator>::claim_store_slot()
{
	uint16_t reservedSlot(std::numeric_limits<uint16_t>::max());
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	for (bool foundSlot(false);!foundSlot;)
	{
		uint16_t expectedSlot(myProducerSlotReservation.load(std::memory_order_acquire));
		const uint8_t storeArraySlot(cqdetail::to_store_array_slot(expectedSlot));
		try_alloc_produer_store_slot(storeArraySlot);
		do {
			if (myProducerSlotReservation.compare_exchange_strong(expectedSlot, expectedSlot + 1, std::memory_order_release)) {
				reservedSlot = expectedSlot;
				foundSlot = true;
				break;
			}
		} while (cqdetail::to_store_array_slot(expectedSlot) == storeArraySlot);
	}
#else
	reservedSlot = myProducerSlotReservation.fetch_add(1, std::memory_order_acq_rel);
	const uint8_t storeArraySlot(cqdetail::to_store_array_slot(reservedSlot));
	if (!myProducerArrayStore[storeArraySlot].load(std::memory_order_acquire)) {
		try_alloc_produer_store_slot(storeArraySlot);
	}
#endif
	return reservedSlot;
}
template<class T, class Allocator>
inline typename concurrent_queue<T, Allocator>::shared_ptr_type concurrent_queue<T, Allocator>::fetch_from_store(uint16_t bufferSlot) const
{
	// Go backwards through all store arrays to find most up-to-date versions of the buffers

	for (uint8_t i = cqdetail::Producer_Slots_Max_Growth_Count - 1; i < cqdetail::Producer_Slots_Max_Growth_Count; --i) {
		atomic_shared_ptr_type* const producerArray(myProducerArrayStore[i].load(std::memory_order_acquire));
		if (!producerArray) {
			continue;
		}

		shared_ptr_type producerBuffer(producerArray[bufferSlot].load());
		if (!producerBuffer) {
			continue;
		}
		return producerBuffer;
	}

	throw std::runtime_error("fetch_from_store found no entry when an existing entry should be guaranteed");
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::insert_to_store(shared_ptr_type buffer, uint16_t bufferSlot, uint16_t storeSlot)
{
	atomic_shared_ptr_type* const producerArray(myProducerArrayStore[storeSlot].load(std::memory_order_acquire));
	producerArray[bufferSlot].store(std::move(buffer));
}

namespace cqdetail {

template <class T, class Allocator>
class alignas(CQ_CACHELINE_SIZE) producer_buffer
{
private:
	typedef typename concurrent_queue<T, Allocator>::shared_ptr_type shared_ptr_type;

public:
	typedef typename concurrent_queue<T>::size_type size_type;

	producer_buffer(size_type capacity, item_container<T>* dataBlock);
	~producer_buffer();

	template<class ...Arg>
	inline bool try_push(Arg&&... in);
	inline bool try_pop(T& out);

	inline size_type size() const;

	inline size_type get_capacity() const;

	// Makes sure that predecessors are wholly unused
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline bool verify_successor(const shared_ptr_type&);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline bool verify_successor(const shared_ptr_type& successor);

	// Contains entries and / or has no next buffer
	inline bool is_active() const;

	// Essentially, is not dummy-buffer
	inline bool is_valid() const;

	inline void invalidate();

	// Searches the buffer list towards the front for
	// the first buffer contining entries
	inline shared_ptr_type find_back();
	// Pushes a newly allocated buffer buffer to the front of the 
	// buffer list
	inline void push_front(shared_ptr_type newBuffer);

	inline void unsafe_clear();

private:

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>* = nullptr>
	inline void write_in(size_type slot, U&& in);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>* = nullptr>
	inline void write_in(size_type slot, U&& in);
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>* = nullptr>
	inline void write_in(size_type slot, const U& in);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>* = nullptr>
	inline void write_in(size_type slot, const U& in);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>* = nullptr>
	inline void write_out(size_type slot, U& out);
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void write_out(size_type slot, U& out);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void write_out(size_type slot, U& out);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void post_pop_cleanup(size_type readSlot);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void post_pop_cleanup(size_type readSlot);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void check_for_damage();
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void check_for_damage();

	inline void reintegrate_failed_entries(size_type failCount);

	static constexpr size_type Buffer_Lock_Offset = concurrent_queue<T>::Buffer_Capacity_Max + Max_Producers;

	std::atomic<size_type> myPreReadIterator;
	std::atomic<size_type> myReadSlot;

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	std::atomic<size_type> myPostReadIterator;
	std::atomic<uint16_t> myFailiureCount;
	std::atomic<uint16_t> myFailiureIndex;
	CQ_PADDING((CQ_CACHELINE_SIZE * 2) - ((sizeof(size_type) * 3) + (sizeof(uint16_t) * 2)));

#else
	CQ_PADDING((CQ_CACHELINE_SIZE * 2) - sizeof(size_type) * 2);
#endif
	size_type myWriteSlot;
	std::atomic<size_type> myPostWriteIterator;
	CQ_PADDING(CQ_CACHELINE_SIZE);
	std::atomic<bool> myNextState;
	shared_ptr_type myNext;

	const size_type myCapacity;

	item_container<T>* const myDataBlock;
};
template<class T, class Allocator>
inline producer_buffer<T, Allocator>::producer_buffer(typename producer_buffer<T, Allocator>::size_type capacity, item_container<T>* dataBlock)
	: myNext(nullptr)
	, myDataBlock(dataBlock)
	, myCapacity(capacity)
	, myReadSlot(0)
	, myPreReadIterator(0)
	, myWriteSlot(0)
	, myPostWriteIterator(0)
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	, myFailiureIndex(0)
	, myFailiureCount(0)
	, myPostReadIterator(0)
	, myNextState(false)
#endif
{
}

template<class T, class Allocator>
inline producer_buffer<T, Allocator>::~producer_buffer()
{
	for (size_type i = 0; i < myCapacity; ++i) {
		myDataBlock[i].~item_container<T>();
	}
}
template<class T, class Allocator>
inline bool producer_buffer<T, Allocator>::is_active() const
{
	return (!myNext || (myReadSlot.load(std::memory_order_relaxed) != myPostWriteIterator.load(std::memory_order_relaxed)));
}
template<class T, class Allocator>
inline bool producer_buffer<T, Allocator>::is_valid() const
{
	return myDataBlock[myWriteSlot % myCapacity].get_state_local() != item_state::Dummy;
}
template<class T, class Allocator>
inline void producer_buffer<T, Allocator>::invalidate()
{
	myDataBlock[myWriteSlot % myCapacity].set_state(item_state::Dummy);
}
template<class T, class Allocator>
inline typename producer_buffer<T, Allocator>::shared_ptr_type producer_buffer<T, Allocator>::find_back()
{
	producer_buffer<T, Allocator>* back(this);
	shared_ptr_type sharedBack(nullptr);

	while (back) {
		const size_type readSlot(back->myReadSlot.load(std::memory_order_acquire));
		const size_type postWrite(back->myPostWriteIterator.load(std::memory_order_acquire));

		const bool thisBufferEmpty(readSlot == postWrite);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		const bool veto(back->myFailiureCount.load(std::memory_order_acquire) != back->myFailiureIndex.load(std::memory_order_acquire));
		const bool valid(!thisBufferEmpty | veto);
#else
		const bool valid(!thisBufferEmpty);
#endif
		if (valid) {
			break;
	}
		sharedBack = back->myNext;
		back = sharedBack.get_owned();
}
	return sharedBack;
}

template<class T, class Allocator>
inline typename producer_buffer<T, Allocator>::size_type producer_buffer<T, Allocator>::size() const
{
	const size_type readSlot(myReadSlot.load(std::memory_order_relaxed));
	size_type accumulatedSize(myPostWriteIterator.load(std::memory_order_relaxed));
	accumulatedSize -= readSlot;

	if (myNext)
		accumulatedSize += myNext->size();

	return accumulatedSize;
}

template<class T, class Allocator>
inline typename producer_buffer<T, Allocator>::size_type producer_buffer<T, Allocator>::get_capacity() const
{
	return myCapacity;
}
template<class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline bool producer_buffer<T, Allocator>::verify_successor(const shared_ptr_type&)
{
	return true;
}
template<class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline bool producer_buffer<T, Allocator>::verify_successor(const shared_ptr_type& successor)
{
	if (!myNextState.load(std::memory_order_acquire)) {
		return false;
	}

	shared_ptr_type next(nullptr);
	producer_buffer<T, Allocator>* inspect(this);

	do {
		const size_type preRead(inspect->myPreReadIterator.load(std::memory_order_acquire));
		for (size_type i = 0; i < inspect->myCapacity; ++i)
		{
			const size_type index((preRead - i) % inspect->myCapacity);

			if (inspect->myDataBlock[index].get_state_local() != item_state::Empty)
			{
				return false;
			}
		}
		next = inspect->myNext;
		inspect = next.get_owned();

		if (inspect == successor.get_owned()) {
			break;
		}
	} while (next->myNextState.load(std::memory_order_acquire));

	return true;
}
template<class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::check_for_damage()
{
}
template<class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::check_for_damage()
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
	const bool failCheckB(!(difference < cqdetail::Max_Producers));
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
template<class T, class Allocator>
inline void producer_buffer<T, Allocator>::push_front(shared_ptr_type newBuffer)
{
	producer_buffer<T, Allocator>* last(this);
	shared_ptr_type sharedLast(nullptr);

	while (last->myNext) {
		sharedLast = last->myNext;
		last = sharedLast.get_owned();
	}
	last->myNext = std::move(newBuffer);
	last->myNextState.store(true, std::memory_order_release);
}
template<class T, class Allocator>
inline void producer_buffer<T, Allocator>::unsafe_clear()
{
	myPreReadIterator.store(myPostWriteIterator.load(std::memory_order_relaxed), std::memory_order_relaxed);
	myReadSlot.store(myPostWriteIterator.load(std::memory_order_relaxed), std::memory_order_relaxed);

#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	myFailiureCount.store(0, std::memory_order_relaxed);
	myFailiureIndex.store(0, std::memory_order_relaxed);

	myPostReadIterator.store(myPostWriteIterator.load(std::memory_order_relaxed), std::memory_order_relaxed);
#endif
	if (myNext) {
		myNext->unsafe_clear();
	}
}
template<class T, class Allocator>
template<class ...Arg>
inline bool producer_buffer<T, Allocator>::try_push(Arg && ...in)
{
	const size_type slotTotal(myWriteSlot++);
	const size_type slot(slotTotal % myCapacity);

	std::atomic_thread_fence(std::memory_order_acquire);

	if (myDataBlock[slot].get_state_local() != item_state::Empty) {
		--myWriteSlot;
		return false;
	}

	write_in(slot, std::forward<Arg>(in)...);

	myDataBlock[slot].set_state_local(item_state::Valid);

	myPostWriteIterator.fetch_add(1, std::memory_order_release);

	return true;
}
template<class T, class Allocator>
inline bool producer_buffer<T, Allocator>::try_pop(T & out)
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
template <class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void producer_buffer<T, Allocator>::write_in(typename producer_buffer<T, Allocator>::size_type slot, U&& in)
{
	myDataBlock[slot].store(std::move(in));
}
template <class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void producer_buffer<T, Allocator>::write_in(typename producer_buffer<T, Allocator>::size_type slot, U&& in)
{
	try {
		myDataBlock[slot].store(std::move(in));
	}
	catch (...) {
		--myWriteSlot;
		throw;
	}
}
template <class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::write_in(typename producer_buffer<T, Allocator>::size_type slot, const U& in)
{
	myDataBlock[slot].store(in);
}
template <class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::write_in(typename producer_buffer<T, Allocator>::size_type slot, const U& in)
{
	try {
		myDataBlock[slot].store(in);
	}
	catch (...) {
		--myWriteSlot;
		throw;
	}
}
template <class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>*>
inline void producer_buffer<T, Allocator>::write_out(typename producer_buffer<T, Allocator>::size_type slot, U& out)
{
	myDataBlock[slot].move(out);
}
template<class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::post_pop_cleanup(typename producer_buffer<T, Allocator>::size_type readSlot)
{
	myDataBlock[readSlot].set_state(item_state::Empty);
	std::atomic_thread_fence(std::memory_order_release);
}
template<class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::post_pop_cleanup(typename producer_buffer<T, Allocator>::size_type readSlot)
{
	myDataBlock[readSlot].set_state(item_state::Empty);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	myDataBlock[readSlot].reset_ref();
	myPostReadIterator.fetch_add(1, std::memory_order_release);
#else
	std::atomic_thread_fence(std::memory_order_release);
#endif
}
template <class T, class Allocator>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::write_out(typename producer_buffer<T, Allocator>::size_type slot, U& out)
{
	myDataBlock[slot].assign(out);
}
template <class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void producer_buffer<T, Allocator>::write_out(typename producer_buffer<T, Allocator>::size_type slot, U& out)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		myDataBlock[slot].try_move(out);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		if (myFailiureCount.fetch_add(1, std::memory_order_acq_rel) == myFailiureIndex.load(std::memory_order_acquire)) {
			myPreReadIterator.fetch_add(Buffer_Lock_Offset, std::memory_order_release);
		}
		myDataBlock[slot].set_state(item_state::Failed);
		myPostReadIterator.fetch_add(1, std::memory_order_release);
		throw;
	}
#endif
}
template<class T, class Allocator>
inline void producer_buffer<T, Allocator>::reintegrate_failed_entries(typename producer_buffer<T, Allocator>::size_type failCount)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	const size_type readSlotTotal(myReadSlot.load(std::memory_order_acquire));
	const size_type readSlotTotalOffset(readSlotTotal + myCapacity);

	const size_type startIndex(readSlotTotalOffset - 1);

	size_type numRedirected(0);
	for (size_type i = 0, j = startIndex; numRedirected != failCount; ++i, --j) {
		const size_type currentIndex((startIndex - i) % myCapacity);
		item_container<T>& currentItem(myDataBlock[currentIndex]);
		const item_state currentState(currentItem.get_state_local());

		if (currentState == item_state::Failed) {
			const size_type toRedirectIndex((startIndex - numRedirected) % myCapacity);
			item_container<T>& toRedirect(myDataBlock[toRedirectIndex]);

			toRedirect.redirect(currentItem);
			currentItem.set_state_local(item_state::Valid);
			++numRedirected;
		}
	}
#else
	failCount;
#endif
}

// used to be able to redirect access to data in the event
// of an exception being thrown
template <class T>
class item_container
{
public:
	item_container<T>(const item_container<T>&) = delete;
	item_container<T>& operator=(const item_container&) = delete;

	inline item_container();
	inline item_container(item_state state);

	inline void store(const T& in);
	inline void store(T&& in);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	inline void redirect(item_container<T>& to);
#endif
	template<class U = T, std::enable_if_t<std::is_move_assignable<U>::value>* = nullptr>
	inline void try_move(U& out);
	template<class U = T, std::enable_if_t<!std::is_move_assignable<U>::value>* = nullptr>
	inline void try_move(U& out);

	inline void assign(T& out);
	inline void move(T& out);

	inline item_state get_state_local() const;
	inline void set_state(item_state state);
	inline void set_state_local(item_state state);

	inline void reset_ref();

private:
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	// May or may not reference this continer
	inline item_container<T>& reference() const;

	// Simple bitmask that represents the pointer portion of a 64 bit integer
	static const uint64_t ourPtrMask = (uint64_t(std::numeric_limits<uint32_t>::max()) << 16 | uint64_t(std::numeric_limits<uint16_t>::max()));
#endif
	T myData;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	union
	{
		uint64_t myStateBlock;
		item_container<T>* myReference;
		struct
		{
			uint16_t trash[3];
			item_state myState;
		};
	};
#else
	item_state myState;
#endif
};
template<class T>
inline item_container<T>::item_container()
	: myData()
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	, myReference(this)
#else
	, myState(item_state::Empty)
#endif
{
}
template<class T>
inline item_container<T>::item_container(item_state state)
	: myState(state)
{
}
template<class T>
inline void item_container<T>::store(const T & in)
{
	myData = in;
	reset_ref();
}
template<class T>
inline void item_container<T>::store(T && in)
{
	myData = std::move(in);
	reset_ref();
}
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
template<class T>
inline void item_container<T>::redirect(item_container<T>& to)
{
	const uint64_t otherPtrBlock(to.myStateBlock & ourPtrMask);
	myStateBlock &= ~ourPtrMask;
	myStateBlock |= otherPtrBlock;
}
#endif
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
inline void item_container<T>::set_state(item_state state)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	reference().myState = state;
#else
	myState = state;
#endif
}
template<class T>
inline void item_container<T>::set_state_local(item_state state)
{
	myState = state;
}
template<class T>
inline void item_container<T>::reset_ref()
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	myReference = this;
#endif
}
template<class T>
inline item_state item_container<T>::get_state_local() const
{
	return myState;
}
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
template<class T>
inline item_container<T>& item_container<T>::reference() const
{
	return *reinterpret_cast<item_container<T>*>(myStateBlock & ourPtrMask);
}
#endif
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

enum class item_state : uint8_t
{
	Empty,
	Valid,
	Failed,
	Dummy
};
std::size_t log2_align(std::size_t from, std::size_t clamp)
{
	const std::size_t from_(from < 2 ? 2 : from);

	const float flog2(std::log2f(static_cast<float>(from_)));
	const float nextLog2(std::ceil(flog2));
	const float fNextVal(std::powf(2.f, nextLog2));

	const std::size_t nextVal(static_cast<size_t>(fNextVal));
	const std::size_t clampedNextVal((clamp < nextVal) ? clamp : nextVal);

	return clampedNextVal;
}
template<class T, class Allocator>
std::size_t calc_block_size(std::size_t fromCapacity)
{
	const std::size_t log2size(cqdetail::log2_align(fromCapacity, cqdetail::Buffer_Capacity_Max));

	const std::size_t alignOfControlBlock(alignof(aspdetail::control_block<void*, typename concurrent_queue<T, Allocator>::shared_ptr_allocator_adaptor_type>));
	const std::size_t alignOfData(alignof(T));
	const std::size_t alignOfBuffer(alignof(cqdetail::producer_buffer<T, Allocator>));

	const std::size_t maxAlignBuffData(alignOfBuffer < alignOfData ? alignOfData : alignOfBuffer);
	const std::size_t maxAlign(maxAlignBuffData < alignOfControlBlock ? alignOfControlBlock : maxAlignBuffData);

	const std::size_t bufferByteSize(sizeof(cqdetail::producer_buffer<T, Allocator>));
	const std::size_t dataBlockByteSize(sizeof(cqdetail::item_container<T>) * log2size);
	const std::size_t controlBlockByteSize(shared_ptr_type::Alloc_Size_Claim);

	const std::size_t controlBlockSize(cqdetail::aligned_size(controlBlockByteSize, maxAlign));
	const std::size_t bufferSize(cqdetail::aligned_size(bufferByteSize, maxAlign));
	const std::size_t dataBlockSize(cqdetail::aligned_size(dataBlockByteSize, maxAlign));

	const std::size_t totalBlockSize(controlBlockSize + bufferSize + dataBlockSize + maxAlign);

	return totalBlockSize;
}
inline uint8_t to_store_array_slot(uint16_t producerIndex)
{
	const float fSourceStoreSlot(log2f(static_cast<float>(producerIndex)));
	const uint8_t sourceStoreSlot(static_cast<uint8_t>(fSourceStoreSlot));
	return sourceStoreSlot;
}
uint16_t to_store_array_capacity(uint16_t storeSlot)
{
	return uint16_t(static_cast<uint16_t>(powf(2.f, static_cast<float>(storeSlot + 1))));
}
constexpr std::size_t next_aligned_to(std::size_t addr, std::size_t align)
{
	const std::size_t mod(addr % align);
	const std::size_t remainder(align - mod);
	const std::size_t offset(remainder == align ? 0 : remainder);

	return addr + offset;
}
constexpr std::size_t aligned_size(std::size_t byteSize, std::size_t align)
{
	const std::size_t div(byteSize / align);
	const std::size_t mod(byteSize % align);
	const std::size_t total(div + static_cast<std::size_t>(static_cast<bool>(mod)));

	return align * total;
}
template <class PtrType>
struct consumer_wrapper
{
	consumer_wrapper(PtrType ptr)
		: myPtr(std::move(ptr))
		, myPopCounter(0)
	{}
	PtrType myPtr;
	uint16_t myPopCounter;
};
template <class T, class Allocator>
class shared_ptr_allocator_adaptor : public Allocator
{
public:
	shared_ptr_allocator_adaptor()
		: myAddress(nullptr)
		, mySize(0)
	{};
	shared_ptr_allocator_adaptor(T* retAddr, std::size_t size)
		: myAddress(retAddr)
		, mySize(size)
	{};

	T* allocate(std::size_t count)
	{
		if (!myAddress) {
			myAddress = this->Allocator::allocate(count);
			mySize = count;
		}
		return myAddress;
	};
	void deallocate(T* /*addr*/, std::size_t /*count*/)
	{
		T* const addr(myAddress);
		std::size_t size(mySize);

		myAddress = nullptr;
		mySize = 0;

		Allocator::deallocate(addr, size);
	}

private:
	T* myAddress;
	std::size_t mySize;
};
template <class T, class Allocator>
class dummy_container
{
public:
	dummy_container();
	typedef shared_ptr<producer_buffer<T, Allocator>, shared_ptr_allocator_adaptor<uint8_t, Allocator>> dummy_type;

private:
	item_container<T> myDummyItem;
	producer_buffer<T, Allocator> myDummyRawBuffer;
public:
	dummy_type myDummyBuffer;
};
template <class IndexType, class Allocator>
class index_pool
{
public:
	index_pool();
	~index_pool();

	IndexType get();
	void add(IndexType index);

	struct node
	{
		node(IndexType index, shared_ptr<node, Allocator> next)
			: myIndex(index)
			, myNext(std::move(next))
		{}
		IndexType myIndex;
		atomic_shared_ptr<node, Allocator> myNext;
	};
	atomic_shared_ptr<node, Allocator> myTop;

private:
	std::atomic<IndexType> myIterator;
};
template<class IndexType, class Allocator>
inline index_pool<IndexType, Allocator>::index_pool()
	: myTop(nullptr)
	, myIterator(0)
{
	static_assert(std::is_same<Allocator::value_type, uint8_t>::value, "value_type for allocator must be uint8_t");
}
template<class IndexType, class Allocator>
inline index_pool<IndexType, Allocator>::~index_pool()
{
	shared_ptr<node, Allocator> top(myTop.unsafe_load());
	while (top) {
		shared_ptr<node, Allocator> next(top->myNext.unsafe_load());
		myTop.unsafe_store(next);
		top = std::move(next);
	}
}
template<class IndexType, class Allocator>
inline IndexType index_pool<IndexType, Allocator>::get()
{
	shared_ptr<node, Allocator> top(myTop.load());
	while (top) {
		versioned_raw_ptr<node, Allocator> expected(top);
		if (myTop.compare_exchange_strong(expected, top->myNext.load())) {
			return top->myIndex;
		}
	}

	return myIterator++;
}
template<class IndexType, class Allocator>
inline void index_pool<IndexType, Allocator>::add(IndexType index)
{
	shared_ptr<node, Allocator> entry(make_shared<node, Allocator>(index, nullptr));

	versioned_raw_ptr<node, Allocator> expected;
	do {
		shared_ptr<node, Allocator> top(myTop.load());
		expected = top.get_versioned_raw_ptr();
		entry->myNext = std::move(top);
	} while (!myTop.compare_exchange_strong(expected, std::move(entry)));
}
template<class T, class Allocator>
inline dummy_container<T, Allocator>::dummy_container()
	: myDummyItem(item_state::Dummy)
	, myDummyRawBuffer(1, &myDummyItem)
	, myDummyBuffer(&myDummyRawBuffer, [](producer_buffer<T, Allocator>*) {})
{
}
}
template <class T, class Allocator>
cqdetail::index_pool<typename concurrent_queue<T, Allocator>::size_type, Allocator> concurrent_queue<T, Allocator>::ourIndexPool;
template <class T, class Allocator>
thread_local std::vector<typename concurrent_queue<T, Allocator>::shared_ptr_type, Allocator> concurrent_queue<T, Allocator>::ourProducers(0);
template <class T, class Allocator>
thread_local std::vector<cqdetail::consumer_wrapper<typename concurrent_queue<T, Allocator>::shared_ptr_type>, Allocator> concurrent_queue<T, Allocator>::ourConsumers;
template <class T, class Allocator>
cqdetail::dummy_container<T, Allocator> concurrent_queue<T, Allocator>::ourDummyContainer;
template <class T, class Allocator>
std::atomic<uint16_t> concurrent_queue<T, Allocator>::ourRelocationIndex(0);
}
#pragma warning(pop)
