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

// In the event an exception is thrown during a pop operation, some entries may
// be dequeued out-of-order as some consumers may already be halfway through a 
// pop operation before reintegration efforts are started.
//
// Exception handling may be enabled for basic exception safety at the cost of 
// a slight performance decrease

/*#define CQ_ENABLE_EXCEPTIONHANDLING*/

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

// For anonymous struct and alignas(64) 
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

enum class item_state : uint8_t;

std::size_t log2_align(std::size_t from, std::size_t clamp);

template <class T, class Allocator>
std::size_t calc_block_size(std::size_t fromCapacity);

inline uint8_t to_store_array_slot(uint16_t storeSlot);

constexpr std::size_t next_aligned_to(std::size_t addr, std::size_t align);
constexpr std::size_t aligned_size(std::size_t byteSize, std::size_t align);

template <class T, class Allocator>
class shared_ptr_allocator_adaptor;

template <class T, class Allocator>
struct shared_ptr_type_wrapper
{
	typedef shared_ptr_allocator_adaptor<uint8_t, Allocator> allocator_adapter_type;
	typedef shared_ptr<producer_buffer<T, Allocator>, allocator_adapter_type> shared_ptr_type;
};

// The maximum allowed consumption from a producer per visit
static constexpr uint16_t Consumer_Force_Relocation_Pop_Count = 16;
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

	typedef typename cqdetail::shared_ptr_type_wrapper<T, Allocator>::shared_ptr_type shared_ptr_type;

	template <class ...Arg>
	void push_internal(Arg&&... in);

	inline void init_producer(size_type withCapacity);

	inline bool relocate_consumer();

	inline shared_ptr_type create_producer_buffer(std::size_t withSize);
	inline void push_producer_buffer(shared_ptr_type buffer);
	inline void try_alloc_produer_store_slot(uint8_t storeArraySlot);
	inline void try_swap_producer_array(uint8_t aromStoreArraySlot);
	inline void try_swap_producer_count(uint16_t toValue);

	inline uint16_t claim_store_slot();
	inline shared_ptr_type fetch_from_store(uint16_t storeSlot) const;
	inline void insert_to_store(shared_ptr_type buffer, uint16_t storeSlot);

	static std::atomic<size_type> ourObjectIterator;

	const size_type myInitBufferCapacity;
	const size_type myObjectId;

	static thread_local std::vector<shared_ptr_type, allocator_type> ourProducers;
	static thread_local std::vector<cqdetail::consumer_wrapper<shared_ptr_type>, allocator_type> ourConsumers;

	static shared_ptr_type ourDummyBuffer;

	std::atomic<shared_ptr_type*> myProducerArrayStore[cqdetail::Producer_Slots_Max_Growth_Count];

	union
	{
		std::atomic<shared_ptr_type*> myProducerSlots;
		shared_ptr_type* myDebugView;
	};
	allocator_type myAllocator;

	std::atomic<uint16_t> myProducerCount;
	std::atomic<uint16_t> myProducerCapacity;
	std::atomic<uint16_t> myProducerSlotReservation;
	std::atomic<uint16_t> myProducerSlotPostIterator;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	std::atomic<uint16_t> myProducerSlotPreIterator;
#endif
};

template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::concurrent_queue()
	: concurrent_queue<T, Allocator>(std::allocator<uint8_t>())
{
	cqdetail::producer_buffer<int, std::allocator<uint8_t>> nonsense(0, nullptr);
	nonsense;
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
	: myObjectId(ourObjectIterator++)
	, myProducerCapacity(0)
	, myProducerCount(0)
	, myProducerSlotPostIterator(0)
	, myProducerSlotReservation(0)
	, myProducerSlots(nullptr)
	, myInitBufferCapacity(cqdetail::log2_align(initProducerCapacity, Buffer_Capacity_Max))
	, myProducerArrayStore{ nullptr }
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	, myProducerSlotPreIterator(0)
#endif
	, myAllocator(allocator)
{
	static_assert(std::is_same<uint8_t, Allocator::value_type>(), "Value type for allocator must be uint8_t");
}
template<class T, class Allocator>
inline concurrent_queue<T, Allocator>::~concurrent_queue()
{
	//const uint16_t producerCount(myProducerCount.load(std::memory_order_acquire));

	//for (uint16_t i = 0; i < producerCount; ++i) {
	//	if (myProducerSlots[i] == &ourDummyBuffer)
	//		continue;
	//	myProducerSlots[i]->destroy_all(myAllocator);
	//}
	//for (uint16_t i = 0; i < cqdetail::Producer_Slots_Max_Growth_Count; ++i) {
	//	delete[] myProducerArrayStore[i];
	//}
	//memset(&myProducerArrayStore[0], 0, sizeof(std::atomic<cqdetail::producer_buffer<T, Allocator>**>) * cqdetail::Producer_Slots_Max_Growth_Count);
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
	const size_type producerSlot(myObjectId);

	if (!(producerSlot < ourProducers.size()))
		ourProducers.resize(producerSlot + 1, nullptr);

	cqdetail::producer_buffer<T, Allocator>* buffer(ourProducers[producerSlot].get_owned());

	if (!buffer) {
		init_producer(myInitBufferCapacity);
		buffer = ourProducers[producerSlot].get_owned();
	}
	if (!buffer->try_push(std::forward<Arg>(in)...)) {
		shared_ptr_type next(create_producer_buffer(std::size_t(buffer->get_capacity()) * 2));
		buffer->push_front(next);
		ourProducers[producerSlot] = std::move(next);
		ourProducers[producerSlot]->try_push(std::forward<Arg>(in)...);
	}
}
template<class T, class Allocator>
bool concurrent_queue<T, Allocator>::try_pop(T & out)
{
	const size_type consumerSlot(myObjectId);
	if (!(consumerSlot < ourConsumers.size()))
		ourConsumers.resize(consumerSlot + 1, cqdetail::consumer_wrapper<shared_ptr_type>{ &ourDummyBuffer, 0 , (static_cast<uint16_t>(rand() % std::numeric_limits<uint16_t>::max()))});

	cqdetail::producer_buffer<T, Allocator>* buffer = ourConsumers[consumerSlot].myPtr.get_owned();

	for (uint16_t attempt(0); !buffer->try_pop(out); ++attempt) {
		if (!(attempt < myProducerCount.load(std::memory_order_acquire)))
			return false;

		if (!relocate_consumer())
			return false;

		buffer = ourConsumers[consumerSlot].myPtr.get_owned();
	}

	if (cqdetail::Consumer_Force_Relocation_Pop_Count < ++ourConsumers[consumerSlot].myPopCounter){
		relocate_consumer();
	}

	return true;
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::reserve(typename concurrent_queue<T, Allocator>::size_type capacity)
{
	const size_type producerSlot(myObjectId);

	if (!(producerSlot < ourProducers.size()))
		ourProducers.resize(producerSlot + 1, nullptr);

	if (!ourProducers[producerSlot]) {
		init_producer(capacity);
		return;
	}
	if (ourProducers[producerSlot]->get_capacity() < capacity) {
		const size_type alignedCapacity(cqdetail::log2_align(capacity, Buffer_Capacity_Max));
		shared_ptr_type buffer(create_producer_buffer(alignedCapacity));
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		try {

#endif
			ourProducers[producerSlot]->push_front(buffer);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		}
		catch (...) {
			buffer->destroy_all(myAllocator);
			throw;
	}
#endif
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
	const uint16_t producerCount(myProducerCount.load(std::memory_order_relaxed));

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
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		push_producer_buffer(newBuffer);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		newBuffer->destroy_all(myAllocator);
		throw;
	}
#endif
	ourProducers[myObjectId] = std::move(newBuffer);
}
template<class T, class Allocator>
inline bool concurrent_queue<T, Allocator>::relocate_consumer()
{
	const uint16_t producers(myProducerCount.load(std::memory_order_relaxed));
	const uint16_t relocation(ourConsumers[myObjectId].myRelocationIndex++);

	for (uint16_t i = 0, j = relocation; i < producers; ++i, ++j) {
		const uint16_t entry(j % producers);
		shared_ptr_type buffer(myProducerSlots[entry]->find_back());
		if (buffer) {
			ourConsumers[myObjectId].myPtr = buffer;

			if (myProducerSlots[entry].get_owned() != buffer.get_owned()) {
				if (buffer->verify_as_replacement()) {
					myProducerSlots[entry].store(std::move(buffer));
				}
			}

			ourConsumers[myObjectId].myPopCounter = 0;

			return true;
		}
	}
	return false;
}
template<class T, class Allocator>
inline typename concurrent_queue<T, Allocator>::shared_ptr_type concurrent_queue<T, Allocator>::create_producer_buffer(std::size_t withSize)
{
	const std::size_t log2size(cqdetail::log2_align(withSize, cqdetail::Buffer_Capacity_Max));

	const std::size_t alignOfControlBlock(alignof(aspdetail::control_block<void*, cqdetail::shared_ptr_type_wrapper<T, Allocator>::allocator_adaptor_type>));
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

	uint8_t* totalBlock(nullptr);

	cqdetail::producer_buffer<T, Allocator>* buffer(nullptr);
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
		buffer = new(totalBlock + bufferOffset) cqdetail::producer_buffer<T, Allocator>(static_cast<size_type>(log2size), data);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
}
	catch (...)
	{
		myAllocator.deallocate(totalBlock);
		throw;
	}
#endif

	typename cqdetail::shared_ptr_type_wrapper<T, Allocator>::allocator_adaptor_type allocAdaptor(totalBlock, totalBlockSize);

	auto deleter = [](cqdetail::producer_buffer<T, Allocator>* obj)
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
	const uint16_t reservedSlot(claim_store_slot());
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	if (!(reservedSlot < cqdetail::Max_Producers)) {
		throw cqdetail::producer_overflow("Max producers exceeded");
	}
#endif
	insert_to_store(std::move(buffer), reservedSlot);

	const uint16_t postIterator(myProducerSlotPostIterator.fetch_add(1, std::memory_order_acq_rel) + 1);
	const uint16_t numReserved(myProducerSlotReservation.load(std::memory_order_acquire));

	if (postIterator == numReserved) {
		for (uint16_t i = 0; i < postIterator; ++i) {
			insert_to_store(fetch_from_store(i), i);
		}
		for (uint8_t i = cqdetail::Producer_Slots_Max_Growth_Count - 1; i < cqdetail::Producer_Slots_Max_Growth_Count; --i) {
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
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_alloc_produer_store_slot(uint8_t storeArraySlot)
{
	const uint16_t producerCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(storeArraySlot + 1))));

	const std::size_t blockSize(sizeof(shared_ptr_type) * producerCapacity);
	uint8_t* const block(myAllocator.allocate(blockSize));

	shared_ptr_type* const newProducerSlotBlock = new (block) shared_ptr_type[producerCapacity];

	shared_ptr_type* expected(nullptr);
	if (!myProducerArrayStore[storeArraySlot].compare_exchange_strong(expected, newProducerSlotBlock, std::memory_order_acq_rel, std::memory_order_acquire)) {

		for (std::size_t i = 0; i < producerCapacity; ++i){
			newProducerSlotBlock[i].~shared_ptr_type();
		}
		myAllocator.deallocate(block);
	}
}
// Try swapping the current producer array for one from the store, and follow up
// with an attempt to swap the capacity value for the one corresponding to the slot
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_swap_producer_array(uint8_t fromStoreArraySlot)
{
	const uint16_t targetCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(fromStoreArraySlot + 1))));
	for (shared_ptr_type* expectedProducerArray(myProducerSlots.load(std::memory_order_acquire));; expectedProducerArray = myProducerSlots.load(std::memory_order_acquire)) {

		bool superceeded(false);
		for (uint8_t i = fromStoreArraySlot + 1; i < cqdetail::Producer_Slots_Max_Growth_Count; ++i) {
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
		shared_ptr_type* const desiredProducerArray(myProducerArrayStore[fromStoreArraySlot].load(std::memory_order_acquire));
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
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::try_swap_producer_count(uint16_t toValue)
{
	const uint16_t desired(toValue);
	for (uint16_t i = myProducerCount.load(std::memory_order_acquire); i < desired; i = myProducerCount.load(std::memory_order_acquire)) {
		uint16_t expected(i);

		if (myProducerCount.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
			break;
		}
	}
}
template<class T, class Allocator>
inline uint16_t concurrent_queue<T, Allocator>::claim_store_slot()
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	const uint16_t preIteration(myProducerSlotPreIterator.fetch_add(1, std::memory_order_acq_rel));
	const uint8_t minimumStoreArraySlot(cqdetail::to_store_array_slot(preIteration));
	const uint8_t minimumStoreArraySlotClamp((cqdetail::Producer_Slots_Max_Growth_Count - 1 < minimumStoreArraySlot ? cqdetail::Producer_Slots_Max_Growth_Count - 1 : minimumStoreArraySlot));

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
	const uint8_t storeArraySlot(cqdetail::to_store_array_slot(reservedSlot));
	if (!myProducerArrayStore[storeArraySlot]) {
		try_alloc_produer_store_slot(storeArraySlot);
	}
	return reservedSlot;
#endif
}
template<class T, class Allocator>
inline typename concurrent_queue<T, Allocator>::shared_ptr_type concurrent_queue<T, Allocator>::fetch_from_store(uint16_t storeSlot) const
{
	for (uint8_t i = cqdetail::Producer_Slots_Max_Growth_Count - 1; i < cqdetail::Producer_Slots_Max_Growth_Count; --i) {
		shared_ptr_type* const producerArray(myProducerArrayStore[i].load(std::memory_order_acquire));
		if (!producerArray) {
			continue;
		}
		shared_ptr_type const producerBuffer(producerArray[storeSlot]);
		if (!producerBuffer) {
			continue;
		}
		return producerBuffer;
	}
	return nullptr;
}
template<class T, class Allocator>
inline void concurrent_queue<T, Allocator>::insert_to_store(shared_ptr_type buffer, uint16_t storeSlot)
{
	for (uint8_t i = cqdetail::Producer_Slots_Max_Growth_Count - 1; i < cqdetail::Producer_Slots_Max_Growth_Count; --i) {
		shared_ptr_type* const producerArray(myProducerArrayStore[i].load(std::memory_order_acquire));
		if (!producerArray) {
			continue;
		}
		producerArray[storeSlot] = std::move(buffer);
		break;
	}
}

namespace cqdetail {

template <class T, class Allocator>
class alignas(CQ_CACHELINE_SIZE) producer_buffer
{
private:
	//typedef typename concurrent_queue<T, Allocator>::shared_ptr_type shared_ptr_type;
	typedef typename shared_ptr_type_wrapper<T, Allocator>::shared_ptr_type shared_ptr_type;

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
	inline bool verify_as_replacement();
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline bool verify_as_replacement();

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
	bool myValidFlag;
	CQ_PADDING((CQ_CACHELINE_SIZE * 2) - ((sizeof(size_type) * 3) + (sizeof(uint16_t) * 2) + sizeof(bool)));

#else
	CQ_PADDING((CQ_CACHELINE_SIZE * 2) - sizeof(size_type) * 2);
#endif

	size_type myWriteSlot;
	std::atomic<size_type> myPostWriteIterator;

	CQ_PADDING(CQ_CACHELINE_SIZE);

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
	, myValidFlag(true)
#endif
{
}

template<class T, class Allocator>
inline producer_buffer<T, Allocator>::~producer_buffer()
{
	for (size_type i = 0; i < myCapacity; ++i){
		 myDataBlock[i].~item_container<T>();
	}
}
template<class T, class Allocator>
inline typename producer_buffer<T, Allocator>::shared_ptr_type producer_buffer<T, Allocator>::find_back()
{
	producer_buffer<T, Allocator>* back(this);
	shared_ptr_type sharedBack(nullptr);

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
		sharedBack = back->myNext;
		back = sharedBack.get_owned();
	}
	return sharedBack;
}

template<class T, class Allocator>
inline typename producer_buffer<T, Allocator>::size_type producer_buffer<T, Allocator>::size() const
{
	const size_type readSlot(myReadSlot.load(std::memory_order_acquire));
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
inline bool producer_buffer<T, Allocator>::verify_as_replacement()
{
	return true;
}
template<class T, class Allocator>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline bool producer_buffer<T, Allocator>::verify_as_replacement()
{
	//producer_buffer<T, Allocator>* previous(myPrevious);
	//while (previous) {
	//	if (previous->myValidFlag) {
	//		const size_type preRead(previous->myPreReadIterator.load(std::memory_order_acquire));
	//		for (size_type i = 0; i < previous->myCapacity; ++i) {
	//			const size_type index((preRead - i) % previous->myCapacity);

	//			if (previous->myDataBlock[index].get_state_local() != item_state::Empty) {
	//				return false;
	//			}
	//		}
	//	}
	//	previous->myValidFlag = false;
	//	previous = previous->myPrevious;
	//}
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
}
template<class T, class Allocator>
inline void producer_buffer<T, Allocator>::unsafe_clear()
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
	// May or may not reference this continer
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
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
	Failed
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
inline uint8_t to_store_array_slot(uint16_t storeSlot)
{
	const float fSourceStoreSlot(log2f(static_cast<float>(storeSlot)));
	const uint8_t sourceStoreSlot(static_cast<uint8_t>(fSourceStoreSlot));
	return sourceStoreSlot;
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
	PtrType myPtr;
	uint16_t myPopCounter;
	uint16_t myRelocationIndex;
};
template <class T, class Allocator>
class shared_ptr_allocator_adaptor : public Allocator
{
public:
	shared_ptr_allocator_adaptor(T* retAddr, std::size_t size)
		: myAddress(retAddr)
		, mySize(size)
	{
	};

	T* allocate(std::size_t /*count*/)
	{
		return myAddress;
	};
	void deallocate(T* /*addr*/, std::size_t /*count*/)
	{
		Allocator::deallocate(myAddress, mySize);
	}

private:
	T* const myAddress;
	std::size_t mySize;
};
}
template <class T, class Allocator>
std::atomic<typename concurrent_queue<T, Allocator>::size_type> concurrent_queue<T, Allocator>::ourObjectIterator(0);
template <class T, class Allocator>
thread_local std::vector<typename concurrent_queue<T, Allocator>::shared_ptr_type, Allocator> concurrent_queue<T, Allocator>::ourProducers(4, nullptr);
template <class T, class Allocator>
thread_local std::vector<cqdetail::consumer_wrapper<typename concurrent_queue<T, Allocator>::shared_ptr_type>, Allocator> concurrent_queue<T, Allocator>::ourConsumers(4, {&concurrent_queue<T, Allocator>::ourDummyBuffer, 0, (static_cast<uint16_t>(rand() % std::numeric_limits<uint16_t>::max()))});
template <class T, class Allocator>
typename concurrent_queue<T, Allocator>::shared_ptr_type concurrent_queue<T, Allocator>::ourDummyBuffer(make_shared<cqdetail::producer_buffer<T, Allocator>, Allocator>(0, nullptr));
}
#pragma warning(pop)
