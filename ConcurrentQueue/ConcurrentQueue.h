#pragma once

//Copyright(c) 2019 Flovin Michaelsen
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
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

class ProducerOverflow : public std::runtime_error
{
public:
	ProducerOverflow(const char* aError) : runtime_error(aError) {}
};

#else
#define CQ_BUFFER_NOTHROW_POP_MOVE(type) (std::is_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_POP_ASSIGN(type) (!CQ_BUFFER_NOTHROW_POP_MOVE(type))
#define CQ_BUFFER_NOTHROW_PUSH_ASSIGN(type) (std::is_same<type, type>::value)
#define CQ_BUFFER_NOTHROW_PUSH_MOVE(type) (std::is_same<type, type>::value)
#endif

#ifndef MAKE_UNIQUE_NAME 
#define _CONCAT_(a,b)  a##b
#define _EXPAND_AND_CONCAT_(a, b) _CONCAT_(a,b)
#define MAKE_UNIQUE_NAME(prefix) _EXPAND_AND_CONCAT_(prefix, __COUNTER__)
#endif

#define CQ_PADDING(bytes) const uint8_t MAKE_UNIQUE_NAME(trash)[bytes] {}

// For anonymous struct
#pragma warning(push)
#pragma warning(disable : 4201) 

#undef max
#undef min

template <class T>
class CqBuffer;

template <class T>
class CqItemContainer;

enum class CqItemState : int8_t;

// The WizardLoaf ConcurrentQueue 
// Made for the x86/x64 architecture in Visual Studio 2017, focusing
// on performance. The Queue preserves the FIFO property within the 
// context of single producers. Push operations are wait-free, TryPop & Size 
// are lock-free and producer capacities grows dynamically
template <class T>
class ConcurrentQueue
{
public:
	typedef std::size_t size_type;

	inline ConcurrentQueue();
	inline ConcurrentQueue(size_type aInitProducerCapacity);
	inline ~ConcurrentQueue();

	inline void Push(const T& aIn);
	inline void Push(T&& aIn);

	const bool TryPop(T& aOut);

	// The Size method can be considered an approximation, and may be 
	// innacurate at the time the caller receives the result.
	inline const std::size_t Size() const;
private:
	friend class CqBuffer<T>;

	template <class ...Arg>
	void PushInternal(Arg&&... aIn);

	inline void InitProducer();

	inline const bool RelocateConsumer();

	inline __declspec(restrict)CqBuffer<T>* const CreateProducerBuffer(const std::size_t aSize) const;
	inline void PushProducerBuffer(CqBuffer<T>* const aBuffer);
	inline void TryAllocProducerStoreSlot(const uint8_t aStoreArraySlot);
	inline void TrySwapProducerArray(const uint8_t aFromStoreArraySlot);
	inline void TrySwapProducerCount(const uint16_t aToValue);

	inline const uint16_t ClaimStoreSlot();
	inline CqBuffer<T>* const FetchFromStore(const uint16_t aStoreSlot) const;
	inline void InsertToStore(CqBuffer<T>* const aBuffer, const uint16_t aStoreSlot);
	inline const uint8_t ToStoreArraySlot(const uint16_t aStoreSlot) const;
	inline const size_type Log2Align(const std::size_t aFrom, const std::size_t aClamp) const;

	// Not size_type max because we need some leaway in case  we
	// need to throw consumers out of a buffer whilst repairing it
	static const size_type BufferCapacityMax = ~(std::numeric_limits<size_type>::max() >> 3) / 2;
	static const uint16_t MaxProducers = std::numeric_limits<int16_t>::max() - 1;

	// Maximum number of times the producer slot array can grow
	static const uint8_t ProducerSlotsMaxGrowthCount = 15;

	static std::atomic<size_type> ourObjectIterator;

	const size_type myInitBufferCapacity;
	const size_type myObjectId;

	static thread_local std::vector<CqBuffer<T>*> ourProducers;
	static thread_local std::vector<CqBuffer<T>*> ourConsumers;

	static thread_local uint16_t ourRelocationIndex;

	static CqBuffer<T> ourDummyBuffer;

	std::atomic<CqBuffer<T>**> myProducerArrayStore[ProducerSlotsMaxGrowthCount];
	std::atomic<CqBuffer<T>**> myProducerSlots;
	std::atomic<uint16_t> myProducerCount;
	std::atomic<uint16_t> myProducerCapacity;
	std::atomic<uint16_t> myProducerSlotReservation;
	std::atomic<uint16_t> myProducerSlotPostIterator;
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	std::atomic<uint16_t> myProducerSlotPreIterator;
#endif
};

template <class T>
std::atomic<typename ConcurrentQueue<T>::size_type> ConcurrentQueue<T>::ourObjectIterator(0);
template <class T>
thread_local std::vector<CqBuffer<T>*> ConcurrentQueue<T>::ourProducers;
template <class T>
thread_local std::vector<CqBuffer<T>*> ConcurrentQueue<T>::ourConsumers;
template <class T>
thread_local uint16_t ConcurrentQueue<T>::ourRelocationIndex(static_cast<uint16_t>(rand() % std::numeric_limits<uint16_t>::max()));
template <class T>
CqBuffer<T> ConcurrentQueue<T>::ourDummyBuffer(0, nullptr);

template<class T>
inline ConcurrentQueue<T>::ConcurrentQueue()
	: ConcurrentQueue<T>(2)
{
}
template<class T>
inline ConcurrentQueue<T>::ConcurrentQueue(size_type aInitProducerCapacity)
	: myObjectId(ourObjectIterator++)
	, myProducerCapacity(0)
	, myProducerCount(0)
	, myProducerSlotPostIterator(0)
	, myProducerSlotReservation(0)
	, myProducerSlots(nullptr)
	, myInitBufferCapacity(Log2Align(aInitProducerCapacity, BufferCapacityMax))
	, myProducerArrayStore{ nullptr }
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	, myProducerSlotPreIterator(0)
#endif
{
}
template<class T>
inline ConcurrentQueue<T>::~ConcurrentQueue()
{
	const uint16_t producerCount(myProducerCount.load(std::memory_order_acquire));

	for (uint16_t i = 0; i < producerCount; ++i) {
		if (myProducerSlots[i] == &ourDummyBuffer)
			continue;
		myProducerSlots[i]->DestroyAll();
	}
	for (uint16_t i = 0; i < ProducerSlotsMaxGrowthCount; ++i) {
		delete[] myProducerArrayStore[i];
	}
	memset(&myProducerArrayStore[0], 0, sizeof(std::atomic<CqBuffer<T>**>) * ProducerSlotsMaxGrowthCount);
}

template<class T>
void ConcurrentQueue<T>::Push(const T & aIn)
{
	PushInternal<const T&>(aIn);
}
template<class T>
inline void ConcurrentQueue<T>::Push(T && aIn)
{
	PushInternal<T&&>(std::move(aIn));
}
template<class T>
template<class ...Arg>
inline void ConcurrentQueue<T>::PushInternal(Arg&& ...aIn)
{
	const std::size_t producerSlot(myObjectId);

	if (!(producerSlot < ourProducers.size()))
		ourProducers.resize(producerSlot + 1, nullptr);

	CqBuffer<T>* buffer(ourProducers[producerSlot]);

	if (!buffer) {
		InitProducer();
		buffer = ourProducers[producerSlot];
	}
	if (!buffer->TryPush(std::forward<Arg>(aIn)...)) {
		CqBuffer<T>* const next(CreateProducerBuffer(size_t(buffer->Capacity()) * 2));
		buffer->PushFront(next);
		ourProducers[producerSlot] = next;
		next->TryPush(std::forward<Arg>(aIn)...);
	}
}
template<class T>
const bool ConcurrentQueue<T>::TryPop(T & aOut)
{
	const std::size_t consumerSlot(myObjectId);
	if (!(consumerSlot < ourConsumers.size()))
		ourConsumers.resize(consumerSlot + 1, &ourDummyBuffer);

	CqBuffer<T>* buffer = ourConsumers[consumerSlot];

	for (uint16_t attempt(0); !buffer->TryPop(aOut); ++attempt) {
		if (!(attempt < myProducerCount.load(std::memory_order_acquire)))
			return false;

		if (!RelocateConsumer())
			return false;

		buffer = ourConsumers[consumerSlot];
	}
	return true;
}
template<class T>
inline const std::size_t ConcurrentQueue<T>::Size() const
{
	const uint16_t producerCount(myProducerCount.load(std::memory_order_relaxed));

	std::size_t size(0);
	for (uint16_t i = 0; i < producerCount; ++i) {
		size += myProducerSlots[i]->Size();
	}
	return size;
}
template<class T>
inline void ConcurrentQueue<T>::InitProducer()
{
	CqBuffer<T>* const newBuffer(CreateProducerBuffer(myInitBufferCapacity));
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		PushProducerBuffer(newBuffer);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		newBuffer->DestroyAll();
		throw;
	}
#endif
	ourProducers[myObjectId] = newBuffer;
}
template<class T>
inline const bool ConcurrentQueue<T>::RelocateConsumer()
{
	const uint16_t producers(myProducerCount.load(std::memory_order_acquire));
	const uint16_t relocation(ourRelocationIndex--);

	for (uint16_t i = 0, j = relocation; i < producers; ++i, ++j) {
		const uint16_t entry(j % producers);
		CqBuffer<T>* const buffer(myProducerSlots[entry]->FindBack());
		if (buffer) {
			ourConsumers[myObjectId] = buffer;

			if (myProducerSlots[entry] != buffer) {
				if (buffer->VerifyAsReplacement()) {			
					myProducerSlots[entry] = buffer;
				}
			}
			return true;
		}
	}
	return false;
}
template<class T>
inline __declspec(restrict)CqBuffer<T>* const ConcurrentQueue<T>::CreateProducerBuffer(const std::size_t aSize) const
{
	const std::size_t size(Log2Align(aSize, BufferCapacityMax));

	const std::size_t bufferSize(sizeof(CqBuffer<T>));
	const std::size_t dataBlockSize(sizeof(CqItemContainer<T>) * size);

	const std::size_t totalBlockSize(bufferSize + dataBlockSize);

	const std::size_t alignmentPadding(bufferSize % 8);

	const std::size_t bufferOffset(0);
	const std::size_t dataBlockOffset(bufferOffset + bufferSize + alignmentPadding);

	uint8_t* totalBlock(nullptr);
	CqBuffer<T>* buffer(nullptr);
	CqItemContainer<T>* data(nullptr);


#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		totalBlock = new uint8_t[totalBlockSize + alignmentPadding];

		data = new (totalBlock + dataBlockOffset) CqItemContainer<T>[size];
		buffer = new(totalBlock + bufferOffset) CqBuffer<T>(static_cast<size_type>(size), data);
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
inline void ConcurrentQueue<T>::PushProducerBuffer(CqBuffer<T>* const aBuffer)
{
	const uint16_t reservedSlot(ClaimStoreSlot());
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	if (!(reservedSlot < MaxProducers)) {
		throw ProducerOverflow("Max producers exceeded");
	}
#endif
	InsertToStore(aBuffer, reservedSlot);

	const uint16_t postIterator(myProducerSlotPostIterator.fetch_add(1, std::memory_order_acq_rel) + 1);
	const uint16_t numReserved(myProducerSlotReservation.load(std::memory_order_acquire));

	if (postIterator == numReserved) {
		for (uint16_t i = 0; i < postIterator; ++i) {
			InsertToStore(FetchFromStore(i), i);
		}
		for (uint8_t i = ProducerSlotsMaxGrowthCount - 1; i < ProducerSlotsMaxGrowthCount; --i) {
			if (myProducerArrayStore[i]) {
				TrySwapProducerArray(i);
				break;
			}
		}
		TrySwapProducerCount(postIterator);
	}
}
// Allocate a buffer array of capacity appropriate to the slot
// and attempt to swap the current value for the new one
template<class T>
inline void ConcurrentQueue<T>::TryAllocProducerStoreSlot(const uint8_t aStoreArraySlot)
{
	const uint16_t producerCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(aStoreArraySlot + 1))));

	CqBuffer<T>** const newProducerSlotBlock(new CqBuffer<T>*[producerCapacity]);
	memset(&newProducerSlotBlock[0], 0, sizeof(CqBuffer<T>**) * producerCapacity);

	CqBuffer<T>** expected(nullptr);
	if (!myProducerArrayStore[aStoreArraySlot].compare_exchange_strong(expected, newProducerSlotBlock, std::memory_order_acq_rel, std::memory_order_acquire)) {
		delete[] newProducerSlotBlock;
	}
}
// Try swapping the current producer array for one from the store, and follow up
// with an attempt to swap the capacity value for the one corresponding to the slot
template<class T>
inline void ConcurrentQueue<T>::TrySwapProducerArray(const uint8_t aFromStoreArraySlot)
{
	const uint16_t targetCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(aFromStoreArraySlot + 1))));
	for (CqBuffer<T>** expectedProducerArray(myProducerSlots.load(std::memory_order_acquire));; expectedProducerArray = myProducerSlots.load(std::memory_order_acquire)) {

		bool superceeded(false);
		for (uint8_t i = aFromStoreArraySlot + 1; i < ProducerSlotsMaxGrowthCount; ++i) {
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
		CqBuffer<T>** const desiredProducerArray(myProducerArrayStore[aFromStoreArraySlot].load(std::memory_order_acquire));
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
inline void ConcurrentQueue<T>::TrySwapProducerCount(const uint16_t aToValue)
{
	const uint16_t desired(aToValue);
	for (uint16_t i = myProducerCount.load(std::memory_order_acquire); i < desired; i = myProducerCount.load(std::memory_order_acquire)) {
		uint16_t expected(i);

		if (myProducerCount.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
			break;
		}
	}
}
template<class T>
inline const uint16_t ConcurrentQueue<T>::ClaimStoreSlot()
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	const uint16_t preIteration(myProducerSlotPreIterator.fetch_add(1, std::memory_order_acq_rel));
	const uint8_t minimumStoreArraySlot(ToStoreArraySlot(preIteration));
	const uint8_t minimumStoreArraySlotClamp(std::min<uint8_t>(minimumStoreArraySlot, ProducerSlotsMaxGrowthCount - 1));

	if (!myProducerArrayStore[minimumStoreArraySlotClamp].load(std::memory_order_acquire)) {
		try {
			TryAllocProducerStoreSlot(minimumStoreArraySlotClamp);
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
inline CqBuffer<T>* const ConcurrentQueue<T>::FetchFromStore(const uint16_t aStoreSlot) const
{
	for (uint8_t i = ProducerSlotsMaxGrowthCount - 1; i < ProducerSlotsMaxGrowthCount; --i) {
		CqBuffer<T>** const producerArray(myProducerArrayStore[i]);
		if (!producerArray) {
			continue;
		}
		CqBuffer<T>* const producerBuffer(producerArray[aStoreSlot]);
		if (!producerBuffer) {
			continue;
		}
		return producerBuffer;
	}
	return nullptr;
}
template<class T>
inline void ConcurrentQueue<T>::InsertToStore(CqBuffer<T>* const aBuffer, const uint16_t aStoreSlot)
{
	for (uint8_t i = ProducerSlotsMaxGrowthCount - 1; i < ProducerSlotsMaxGrowthCount; --i) {
		CqBuffer<T>** const producerArray(myProducerArrayStore[i].load(std::memory_order_acquire));
		if (!producerArray) {
			continue;
		}
		producerArray[aStoreSlot] = aBuffer;
		break;
	}
}
template<class T>
inline const uint8_t ConcurrentQueue<T>::ToStoreArraySlot(const uint16_t aStoreSlot) const
{
	const float fSourceStoreSlot(log2f(static_cast<float>(aStoreSlot)));
	const uint8_t sourceStoreSlot(static_cast<uint8_t>(fSourceStoreSlot));
	return sourceStoreSlot;
}
template<class T>
inline const typename ConcurrentQueue<T>::size_type ConcurrentQueue<T>::Log2Align(const std::size_t aFrom, const std::size_t aClamp) const
{
	const std::size_t from(aFrom < 2 ? 2 : aFrom);

	const float flog2(std::log2f(static_cast<float>(from)));
	const float nextLog2(std::ceil(flog2));
	const float fNextVal(std::powf(2.f, nextLog2));

	const std::size_t nextVal(static_cast<size_t>(fNextVal));
	const std::size_t clampedNextVal((aClamp < nextVal) ? aClamp : nextVal);

	return static_cast<size_type>(clampedNextVal);
}

template <class T>
class CqBuffer
{
public:
	typedef typename ConcurrentQueue<T>::size_type size_type;

	CqBuffer(const size_type aCapacity, CqItemContainer<T>* const aDataBlock);
	~CqBuffer() = default;

	template<class ...Arg>
	inline const bool TryPush(Arg&&... aIn);
	inline const bool TryPop(T& aOut);

	// Deallocates all buffers in the list
	inline void DestroyAll();

	inline const std::size_t Size() const;

	__declspec(noalias) inline const size_type Capacity() const;

	// Makes sure that predecessors are wholly unused
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline const bool VerifyAsReplacement();
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline const bool VerifyAsReplacement();

	// Searches the buffer list towards the front for
	// the first buffer containing entries
	inline CqBuffer<T>* const FindBack();
	// Pushes a newly allocated buffer buffer to the front of the 
	// buffer list
	inline void PushFront(CqBuffer<T>* const aNewBuffer);
private:
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>* = nullptr>
	inline void WriteIn(const size_type aSlot, U&& aIn);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>* = nullptr>
	inline void WriteIn(const size_type aSlot, U&& aIn);
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>* = nullptr>
	inline void WriteIn(const size_type aSlot, const U& aIn);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>* = nullptr>
	inline void WriteIn(const size_type aSlot, const U& aIn);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>* = nullptr>
	inline void WriteOut(const size_type aSlot, U& aOut);
	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void WriteOut(const size_type aSlot, U& aOut);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void WriteOut(const size_type aSlot, U& aOut);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void PostPopCleanup(const size_type aReadSlot);
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void PostPopCleanup(const size_type aReadSlot);

	template <class U = T, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void CheckRepairs();
	template <class U = T, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>* = nullptr>
	inline void CheckRepairs();

	inline void ReintegrateFailedEntries(const size_type aFailCount);

	// Searches the buffer list towards the back for the last node
	inline CqBuffer<T>* const FindTail();

	static const size_type BufferLockOffset = ConcurrentQueue<T>::BufferCapacityMax + ConcurrentQueue<T>::MaxProducers;

	size_type myWriteSlot;
	std::atomic<size_type> myPostWriteIterator;

	// The tail becomes the de-facto storage place for unused buffers,
	// until they are destroyed with the entire structure
	CqBuffer<T>* myPrevious;
	CqBuffer<T>* myNext;

	const size_type myCapacity;
	CqItemContainer<T>* const myDataBlock;
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
inline CqBuffer<T>::CqBuffer(const size_type aCapacity, CqItemContainer<T>* const aDataBlock)
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
inline void CqBuffer<T>::DestroyAll()
{
	CqBuffer<T>* current = FindTail();

	while (current) {
		const size_type lastCapacity(current->Capacity());
		uint8_t* const lastBlock(reinterpret_cast<uint8_t*>(current));
		CqItemContainer<T>* const lastDataBlock(current->myDataBlock);

		current = current->myNext;
		if (!std::is_trivially_destructible<T>::value) {
			for (size_type i = 0; i < lastCapacity; ++i) {
				lastDataBlock[i].~CqItemContainer<T>();
			}
		}
		delete[] lastBlock;
	}
}

// Searches buffer list towards the front for
// a buffer with contents. Returns null upon 
// failiure
template<class T>
inline CqBuffer<T>* const CqBuffer<T>::FindBack()
{
	CqBuffer<T>* back(this);

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
inline const std::size_t CqBuffer<T>::Size() const
{
	std::size_t size(myPostWriteIterator.load(std::memory_order_relaxed));
	const std::size_t readSlot(myReadSlot.load(std::memory_order_acquire));
	size -= readSlot;

	if (myNext)
		size += myNext->Size();

	return size;
}

template<class T>
__declspec(noalias) inline  const typename CqBuffer<T>::size_type CqBuffer<T>::Capacity() const
{
	return myCapacity;
}
template<class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline const bool CqBuffer<T>::VerifyAsReplacement()
{
	return true;
}
template<class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline const bool CqBuffer<T>::VerifyAsReplacement()
{
	CqBuffer<T>* previous(myPrevious);
	while (previous) {
		if (previous->myValidFlag) {
			const size_type preRead(previous->myPreReadIterator.load(std::memory_order_acquire));
			for (size_type i = 0; i < previous->myCapacity; ++i) {
				const size_type index((preRead - i) % previous->myCapacity);

				if (previous->myDataBlock[index].GetStateLocal() != CqItemState::Empty) {
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
inline void CqBuffer<T>::CheckRepairs()
{
}
template<class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::CheckRepairs()
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	const size_type preRead(myPreReadIterator.load(std::memory_order_acquire));
	const size_type preReadLockOffset(preRead - BufferLockOffset);
	if (preReadLockOffset != myPostReadIterator.load(std::memory_order_acquire)) {
		return;
	}

	const uint16_t failiureIndex(myFailiureIndex.load(std::memory_order_acquire));
	const uint16_t failiureCount(myFailiureCount.load(std::memory_order_acquire));
	const uint16_t difference(failiureCount - failiureIndex);

	const bool failCheckA(0 == difference);
	const bool failCheckB(!(difference < ConcurrentQueue<T>::MaxProducers));
	if (failCheckA | failCheckB) {
		return;
	}

	const size_type toReintegrate(failiureCount - failiureIndex);

	uint16_t expected(failiureIndex);
	const uint16_t desired(failiureCount);
	if (myFailiureIndex.compare_exchange_strong(expected, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
		ReintegrateFailedEntries(toReintegrate);

		myPostReadIterator.fetch_sub(toReintegrate);
		myReadSlot.fetch_sub(toReintegrate);
		myPreReadIterator.fetch_sub(BufferLockOffset + toReintegrate);
	}
#endif
}
template<class T>
inline void CqBuffer<T>::PushFront(CqBuffer<T>* const aNewBuffer)
{
	CqBuffer<T>* last(this);
	while (last->myNext) {
		last = last->myNext;
	}
	last->myNext = aNewBuffer;
	aNewBuffer->myPrevious = last;
}
template<class T>
template<class ...Arg>
inline const bool CqBuffer<T>::TryPush(Arg && ...aIn)
{
	const size_type slotTotal(myWriteSlot++);
	const size_type slot(slotTotal % myCapacity);

	std::atomic_thread_fence(std::memory_order_acquire);

	if (myDataBlock[slot].GetStateLocal() != CqItemState::Empty) {
		--myWriteSlot;
		return false;
	}

	WriteIn(slot, std::forward<Arg>(aIn)...);

	myDataBlock[slot].SetStateLocal(CqItemState::Valid);

	myPostWriteIterator.fetch_add(1, std::memory_order_release);

	return true;
}
template<class T>
inline const bool CqBuffer<T>::TryPop(T & aOut)
{
	const size_type lastWritten(myPostWriteIterator.load(std::memory_order_acquire));
	const size_type slotReserved(myPreReadIterator.fetch_add(1, std::memory_order_acq_rel) + 1);
	const size_type avaliable(lastWritten - slotReserved);

	if (myCapacity < avaliable) {
		myPreReadIterator.fetch_sub(1, std::memory_order_release);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
		CheckRepairs();
#endif
		return false;
	}
	const size_type readSlotTotal(myReadSlot.fetch_add(1, std::memory_order_acq_rel));
	const size_type readSlot(readSlotTotal % myCapacity);

	WriteOut(readSlot, aOut);

	PostPopCleanup(readSlot);

	return true;
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, U&& aIn)
{
	myDataBlock[aSlot].Store(std::move(aIn));
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, U&& aIn)
{
	try {
		myDataBlock[aSlot].Store(std::move(aIn));
	}
	catch (...) {
		--myWriteSlot;
		throw;
	}
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, const U& aIn)
{
	myDataBlock[aSlot].Store(aIn);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, const U& aIn)
{
	try {
		myDataBlock[aSlot].Store(aIn);
	}
	catch (...) {
		--myWriteSlot;
		throw;
	}
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>*>
inline void CqBuffer<T>::WriteOut(const size_type aSlot, U& aOut)
{
	myDataBlock[aSlot].Move(aOut);
}
template<class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U) || CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::PostPopCleanup(const size_type aReadSlot)
{
	myDataBlock[aReadSlot].SetState(CqItemState::Empty);
	std::atomic_thread_fence(std::memory_order_release);
}
template<class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::PostPopCleanup(const size_type aReadSlot)
{
	myDataBlock[aReadSlot].SetState(CqItemState::Empty);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	myDataBlock[aReadSlot].ResetRef();
	myPostReadIterator.fetch_add(1, std::memory_order_release);
#else
	std::atomic_thread_fence(std::memory_order_release);
#endif
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteOut(const size_type aSlot, U& aOut)
{
	myDataBlock[aSlot].Assign(aOut);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteOut(const size_type aSlot, U& aOut)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	try {
#endif
		myDataBlock[aSlot].TryMove(aOut);
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	}
	catch (...) {
		if (myFailiureCount.fetch_add(1, std::memory_order_acq_rel) == myFailiureIndex.load(std::memory_order_acquire)) {
			myPreReadIterator.fetch_add(BufferLockOffset, std::memory_order_release);
		}
		myDataBlock[aSlot].SetState(CqItemState::Failed);
		myPostReadIterator.fetch_add(1, std::memory_order_release);
		throw;
	}
#endif
}
template<class T>
inline void CqBuffer<T>::ReintegrateFailedEntries(const size_type aFailCount)
{
	const size_type readSlotTotal(myReadSlot.load(std::memory_order_acquire));
	const size_type readSlotTotalOffset(readSlotTotal + myCapacity);

	const size_type startIndex(readSlotTotalOffset - 1);

	size_type numRedirected(0);
	for (size_type i = 0, j = startIndex; numRedirected != aFailCount; ++i, --j) {
		const size_type currentIndex((startIndex - i) % myCapacity);
		CqItemContainer<T>& currentItem(myDataBlock[currentIndex]);
		const CqItemState currentState(currentItem.GetStateLocal());

		if (currentState == CqItemState::Failed) {
			const size_type toRedirectIndex((startIndex - numRedirected) % myCapacity);
			CqItemContainer<T>& toRedirect(myDataBlock[toRedirectIndex]);

			toRedirect.Redirect(currentItem);
			currentItem.SetStateLocal(CqItemState::Valid);
			++numRedirected;
		}
	}
}
template<class T>
inline CqBuffer<T>* const CqBuffer<T>::FindTail()
{
	CqBuffer<T>* tail(this);
	while (tail->myPrevious) {
		tail = tail->myPrevious;
	}
	return tail;
}

// Class used to be able to redirect access to data in the event
// of an exception being thrown
template <class T>
class CqItemContainer
{
public:
	CqItemContainer<T>(const CqItemContainer<T>&) = delete;
	CqItemContainer<T>& operator=(const CqItemContainer&) = delete;

	inline CqItemContainer();

	inline void Store(const T& aIn);
	inline void Store(T&& aIn);

	inline void Redirect(CqItemContainer<T>& aTo);

	template<class U = T, std::enable_if_t<std::is_move_assignable<U>::value>* = nullptr>
	inline void TryMove(U& aOut);
	template<class U = T, std::enable_if_t<!std::is_move_assignable<U>::value>* = nullptr>
	inline void TryMove(U& aOut);

	inline void Assign(T& aOut);
	inline void Move(T& aOut);

	inline const CqItemState GetStateLocal() const;
	inline void SetState(const CqItemState aState);
	inline void SetStateLocal(const CqItemState aState);

	inline void ResetRef();

private:
	// May or may not reference this container
	inline CqItemContainer<T>& Reference() const;

	// Simple bitmask that represents the pointer portion of a 64 bit integer
	static const uint64_t ourPtrMask = (uint64_t(std::numeric_limits<uint32_t>::max()) << 16 | uint64_t(std::numeric_limits<uint16_t>::max()));

	T myData;
	union
	{
		uint64_t myStateBlock;
		CqItemContainer<T>* myReference;
		struct
		{
			uint16_t trash[3];
			CqItemState myState;
		};
	};
};
template<class T>
inline CqItemContainer<T>::CqItemContainer()
	: myData()
	, myReference(this)
{
}
template<class T>
inline void CqItemContainer<T>::Store(const T & aIn)
{
	myData = aIn;
	myReference = this;
}
template<class T>
inline void CqItemContainer<T>::Store(T && aIn)
{
	myData = std::move(aIn);
	myReference = this;
}
template<class T>
inline void CqItemContainer<T>::Redirect(CqItemContainer<T>& aTo)
{
	const uint64_t otherPtrBlock(aTo.myStateBlock & ourPtrMask);
	myStateBlock &= ~ourPtrMask;
	myStateBlock |= otherPtrBlock;
}
template<class T>
inline void CqItemContainer<T>::Assign(T & aOut)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	aOut = Reference().myData;
#else
	aOut = myData;
#endif
}
template<class T>
inline void CqItemContainer<T>::Move(T & aOut)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	aOut = std::move(Reference().myData);
#else
	aOut = std::move(myData);
#endif
}
template<class T>
inline void CqItemContainer<T>::SetState(const CqItemState aState)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	Reference().myState = aState;
#else
	myState = aState;
#endif
}
template<class T>
inline void CqItemContainer<T>::SetStateLocal(const CqItemState aState)
{
	myState = aState;
}
template<class T>
inline void CqItemContainer<T>::ResetRef()
{
	myReference = this;
}
template<class T>
inline const CqItemState CqItemContainer<T>::GetStateLocal() const
{
	return myState;
}
template<class T>
inline CqItemContainer<T>& CqItemContainer<T>::Reference() const
{
	return *reinterpret_cast<CqItemContainer<T>*>(myStateBlock & ourPtrMask);
}
template<class T>
template<class U, std::enable_if_t<std::is_move_assignable<U>::value>*>
inline void CqItemContainer<T>::TryMove(U& aOut)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	aOut = std::move(Reference().myData);
#else
	aOut = std::move(myData);
#endif
}
template<class T>
template<class U, std::enable_if_t<!std::is_move_assignable<U>::value>*>
inline void CqItemContainer<T>::TryMove(U& aOut)
{
#ifdef CQ_ENABLE_EXCEPTIONHANDLING
	aOut = Reference().myData;
#else
	aOut = myData;
#endif
}

enum class CqItemState : int8_t
{
	Empty,
	Valid,
	Failed
};

#pragma warning(pop)
