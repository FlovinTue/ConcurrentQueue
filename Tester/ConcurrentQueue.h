#pragma once

// Flovin Michaelsen 2019


#include <atomic>
#include <vector>

// Define to enable basic exception handling in exchange
// for a slight performance decrease (in some cases)
#define CQ_ENABLE_EXCEPTIONS 
//#undef CQ_ENABLE_EXCEPTIONS

#ifdef CQ_ENABLE_EXCEPTIONS 
#define CQ_BUFFER_NOTHROW_POP_MOVE(type) (std::is_nothrow_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_POP_ASSIGN(type) (!CQ_BUFFER_NOTHROW_POP_MOVE(type) && (std::is_nothrow_assignable<type&, type>::value))
#define CQ_BUFFER_NOTHROW_PUSH_MOVE(type) (std::is_nothrow_move_assignable<type>::value)
#define CQ_BUFFER_NOTHROW_PUSH_ASSIGN(type) (std::is_nothrow_assignable<type&, type>::value)


class ProducerOverflow : public std::exception
{
public:
	virtual const char* what() const throw() {
		return "Maximum number of producers exceeded";
	}
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

// Evaluates the size of aPreviousBlock and creates
// an array of uint8_t that pads to next cache line
#define CACHELINE_PADDING(aPreviousBlock) const uint8_t MAKE_UNIQUE_NAME(myPadding)[64 - (sizeof (aPreviousBlock) % 64)] {}

#undef max

// For anonymous struct
#pragma warning(disable : 4201) 

template <class T>
class CqBuffer;

template <class T>
class CqItemContainer;

// The WizardLoaf ConcurrentQueue 
// Made for the x86/x64 architecture in Visual Studio 2017, focusing
// on performance. The Queue preserves the FIFO property within the 
// context of single producers. Push operations are wait-free, TryPop(?) & Size 
// are lock-free and producer capacities grows dynamically
template <class T>
class ConcurrentQueue
{
public:
	typedef uint32_t size_type;

	inline ConcurrentQueue();
	inline ConcurrentQueue(size_type aInitProducerCapacity);
	inline ~ConcurrentQueue();

	inline void Push(const T& aIn);
	inline void Push(T&& aIn);

	const bool TryPop(T& aOut);

	// The Size method can be considered an approximation, and may be 
	// innacurate at the time the caller receives the result.
	inline const size_t Size() const;
private:
	template <class ...Arg>
	void Push(Arg... aIn);

	inline void InitProducer();

	inline const bool RelocateConsumer();

	inline __declspec(restrict)CqBuffer<T>* const CreateProducerBuffer(const size_t aSize) const;
	inline void PushProducerBuffer(CqBuffer<T>* const aBuffer);
	inline void TrySwapProducerArray(const uint16_t aFromStoreSlot);
	inline void TrySwapProducerCount(const uint16_t aFromValue);

	inline const size_type Log2Align(const size_t aFrom, const size_t aClamp) const;

	// Not size_type max because we need some leaway in case 
	// we need to repair a block
	static const size_type BufferCapacityMax = ~(std::numeric_limits<size_type>::max() >> 1) - (std::numeric_limits<uint16_t>::max() - 1);

	// Maximum number of times the producer slot array
	// can grow
	static const uint16_t ProducerSlotsMaxGrowthCount = 15;

	static std::atomic<size_type> ourObjectIterator;

	const size_type myInitBufferCapacity;
	const size_type myObjectId;

	static thread_local std::vector<CqBuffer<T>*> ourProducers;
	static thread_local std::vector<CqBuffer<T>*> ourConsumers;

	static thread_local uint16_t ourRelocationIndex;

	std::atomic<CqBuffer<T>**> myProducerArrayStore[ProducerSlotsMaxGrowthCount];
	std::atomic<CqBuffer<T>**> myProducerSlots;
	std::atomic<uint16_t> myProducerCount;
	std::atomic<uint16_t> myProducerCapacity;
	std::atomic<uint16_t> myProducerSlotReservation;
	std::atomic<uint16_t> myProducerSlotPostIterator;
};

template <class T>
std::atomic<typename ConcurrentQueue<T>::size_type> ConcurrentQueue<T>::ourObjectIterator(0);
template <class T>
thread_local std::vector<CqBuffer<T>*> ConcurrentQueue<T>::ourProducers(1, nullptr);
template <class T>
thread_local std::vector<CqBuffer<T>*> ConcurrentQueue<T>::ourConsumers(1, nullptr);

template <class T>
thread_local uint16_t ConcurrentQueue<T>::ourRelocationIndex(static_cast<uint16_t>(rand() % std::numeric_limits<uint16_t>::max()));

template<class T>
inline ConcurrentQueue<T>::ConcurrentQueue()
	: ConcurrentQueue<T>(2) {
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
{

}

template<class T>
inline ConcurrentQueue<T>::~ConcurrentQueue() {
	const uint16_t producerCount(myProducerCount.load(std::memory_order_acquire));

	for (uint16_t i = 0; i < producerCount; ++i) {
		myProducerSlots[i]->DestroyAll();
	}

	for (uint16_t i = 0; i < ProducerSlotsMaxGrowthCount; ++i) {
		delete[] myProducerArrayStore[i];
	}

	memset(&myProducerArrayStore[0], 0, sizeof(std::atomic<CqBuffer<T>**>) * ProducerSlotsMaxGrowthCount);
}

template<class T>
void ConcurrentQueue<T>::Push(const T & aIn) {
	Push<const T&>(aIn);
}
template<class T>
inline void ConcurrentQueue<T>::Push(T && aIn) {
	Push<T&&>(std::move(aIn));
}
template<class T>
template<class ...Arg>
inline void ConcurrentQueue<T>::Push(Arg ...aIn) {
	const size_t producerSlot(myObjectId);

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
const bool ConcurrentQueue<T>::TryPop(T & aOut) {
	const size_t consumerSlot(myObjectId);

	if (!(consumerSlot < ourConsumers.size()))
		ourConsumers.resize(consumerSlot + 1, nullptr);

	CqBuffer<T>* buffer = ourConsumers[consumerSlot];

	if (!buffer) {
		if (!RelocateConsumer())
			return false;

		buffer = ourConsumers[consumerSlot];
	}

	for (uint16_t attempt(0); !buffer->TryPop(aOut); ++attempt) {
		if (!(attempt < myProducerCount.load(std::memory_order_relaxed)))
			return false;

		if (!RelocateConsumer())
			return false;

		buffer = ourConsumers[consumerSlot];
	}
	return true;
}
template<class T>
inline const size_t ConcurrentQueue<T>::Size() const {
	const uint16_t producerCount(myProducerCount.load(std::memory_order_acquire));

	size_t size(0);
	for (uint16_t i = 0; i < producerCount; ++i) {
		size += myProducerSlots[i]->Size();
	}
	return size;
}
template<class T>
inline void ConcurrentQueue<T>::InitProducer() {
	CqBuffer<T>* const newBuffer(CreateProducerBuffer(myInitBufferCapacity));
#ifdef CQ_ENABLE_EXCEPTIONS
	try {
#endif
		PushProducerBuffer(newBuffer);
#ifdef CQ_ENABLE_EXCEPTIONS
	}
	catch (...) {
		newBuffer->DestroyAll();
		throw;
	}
#endif
	ourProducers[myObjectId] = newBuffer;
}
template<class T>
inline const bool ConcurrentQueue<T>::RelocateConsumer() {
	const uint16_t producers(myProducerCount.load(std::memory_order_acquire));
	const uint16_t relocation(ourRelocationIndex--);

	for (uint16_t i = 0, j = relocation; i < producers; ++i, ++j) {
		const uint16_t entry(j % producers);
		CqBuffer<T>* const buffer(myProducerSlots[entry]->FindBack());
		if (buffer) {
			ourConsumers[myObjectId] = buffer;
			myProducerSlots[entry] = buffer;
			return true;
		}
	}

	return false;
}
template<class T>
inline __declspec(restrict)CqBuffer<T>* const ConcurrentQueue<T>::CreateProducerBuffer(const size_t aSize) const {
	const size_t size(Log2Align(aSize, BufferCapacityMax));

	const size_t bufferSize(sizeof(CqBuffer<T>));
	const size_t dataBlockSize(sizeof(CqItemContainer<T>) * size);

	const size_t totalBlockSize(bufferSize + dataBlockSize);

	const size_t bufferOffset(0);
	const size_t dataBlockOffset(bufferOffset + bufferSize);


	uint8_t* totalBlock(nullptr);
	CqBuffer<T>* buffer(nullptr);
	CqItemContainer<T>* data(nullptr);

	const uint8_t alignmentPadding(8);

#ifdef CQ_ENABLE_EXCEPTIONS
	try {
		totalBlock = new uint8_t[totalBlockSize + alignmentPadding];

		data = new (totalBlock + dataBlockOffset) CqItemContainer<T>[size];
		buffer = new(totalBlock + bufferOffset) CqBuffer<T>(static_cast<size_type>(size), data);
	}
	catch (...) {
		delete[] totalBlock;
		throw;
	}
#else
	totalBlock = new uint8_t[totalBlockSize + alignmentPadding];
	data = new (totalBlock + dataBlockOffset) CqItemContainer<T>[size];
	buffer = new(totalBlock + bufferOffset) CqBuffer<T>(static_cast<size_type>(size), data);
#endif

	return buffer;
}

template<class T>
inline void ConcurrentQueue<T>::PushProducerBuffer(CqBuffer<T>* const aBuffer) {
	const uint16_t reservedSlot(myProducerSlotReservation++);

#ifdef CQ_ENABLE_EXCEPTIONS
	if ((uint16_t(1) << ProducerSlotsMaxGrowthCount < reservedSlot)) {
		--myProducerSlotReservation;
		throw ProducerOverflow();
	}
#endif

	const uint16_t storeSlot(static_cast<uint16_t>(log2f(static_cast<float>(reservedSlot))));
	const uint16_t producerCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(storeSlot + 1))));

	// If a producerarray needs to be allocated, threads 
	// will compete to do so
	if (!myProducerArrayStore[storeSlot].load(std::memory_order_acquire)) {
		CqBuffer<T>** const newProducerSlotBlock(new CqBuffer<T>*[producerCapacity]);
		memset(&newProducerSlotBlock[0], 0, sizeof(CqBuffer<T>*) * producerCapacity);

		CqBuffer<T>** expected(nullptr);
		if (!myProducerArrayStore[storeSlot].compare_exchange_strong(expected, newProducerSlotBlock)) {
			delete[] newProducerSlotBlock;
		}
	}

	// Store buffer in store first, because this is guaranteed 
	// to be currently allocated
	myProducerArrayStore[storeSlot][reservedSlot] = aBuffer;

	const uint16_t reservationCount(myProducerSlotReservation.load(std::memory_order_acquire));
	const uint16_t postIterator(++myProducerSlotPostIterator);

	// Now, if post iterator matches the current reservation count
	// that means all the slots that came before this value is inserted
	// and we can copy all of them into the current top array
	if (postIterator == reservationCount) {
		const uint16_t targetStoreSlot(static_cast<uint16_t>(log2f(static_cast<float>(postIterator - 1))));

		for (uint16_t i = 0; i < postIterator; ++i) {
			const uint16_t sourceStoreSlot(static_cast<uint16_t>(log2f(static_cast<float>(i))));
			myProducerArrayStore[targetStoreSlot][i] = myProducerArrayStore[sourceStoreSlot][i];
		}
		// At this point myProducerSlotstore[storeSlot] 
		// should contain postIterator producers

		// Now we'll try swapping the current producerSlotArray for
		// the one we are referreing to here. 
		TrySwapProducerArray(targetStoreSlot);
		// And update the producer count
		TrySwapProducerCount(postIterator);
	}

}

template<class T>
inline void ConcurrentQueue<T>::TrySwapProducerArray(const uint16_t aFromStoreSlot) {
	const uint16_t targetCapacity(static_cast<uint16_t>(powf(2.f, static_cast<float>(aFromStoreSlot + 1))));
	// This loop should only
	// be able to happen ProducerSlotsMaxGrowthCount * 2 times
	// at most
	for (CqBuffer<T>** expectedProducerArray(myProducerSlots.load(std::memory_order_acquire));; expectedProducerArray = myProducerSlots.load(std::memory_order_acquire)) {
		// Make sure we're not trying to change a lower array
		// with a higher one
		bool superceeded(false);
		for (uint16_t i = aFromStoreSlot + 1; i < ProducerSlotsMaxGrowthCount; ++i) {
			if (!myProducerSlots.load(std::memory_order_acquire)) {
				break;
			}
			if (expectedProducerArray == myProducerArrayStore[i]) {
				superceeded = true;
			}
		}
		if (superceeded) {
			// Some other thread beat us to the punch
			break;
		}
		CqBuffer<T>** const desiredProducerArray(myProducerArrayStore[aFromStoreSlot].load(std::memory_order_acquire));
		if (myProducerSlots.compare_exchange_strong(expectedProducerArray, desiredProducerArray, std::memory_order_release)) {
			// This loop should only be able to happen 
			// ProducerSlotsMaxGrowthCount * 2 times at most
			for (uint16_t expectedCapacity(myProducerCapacity.load(std::memory_order_acquire));; expectedCapacity = myProducerCapacity.load(std::memory_order_acquire)) {
				// Make sure we're not trying to downchange capacity
				if (!(expectedCapacity < targetCapacity)) {
					break;
				}
				if (myProducerCapacity.compare_exchange_strong(expectedCapacity, targetCapacity, std::memory_order_release)) {
					break;
				}
			}
			break;
		}
	}
}

template<class T>
inline void ConcurrentQueue<T>::TrySwapProducerCount(const uint16_t aFromValue) {
	const uint16_t desired(aFromValue);
	// This loop should only be able to happen 'desired' amount of times
	for (uint16_t i = 0; i < desired; ++i) {

		if (!(myProducerCount.load(std::memory_order_acquire) < desired)) {
			// Someone else beat us to the punch
			break;
		}
		uint16_t expected(myProducerCount.load(std::memory_order_acquire));
		if (myProducerCount.compare_exchange_strong(expected, desired, std::memory_order_release)) {
			break;
		}
	}
}

template<class T>
inline const typename ConcurrentQueue<T>::size_type ConcurrentQueue<T>::Log2Align(const size_t aFrom, const size_t aClamp) const {
	const size_t from(aFrom < 2 ? 2 : aFrom);

	const float flog2(std::log2f(static_cast<float>(from)));
	const float nextLog2(std::ceil(flog2));
	const float fNextVal(std::powf(2.f, nextLog2));

	const size_t nextVal(static_cast<size_t>(fNextVal));
	const size_t clampedNextVal((aClamp < nextVal) ? aClamp : nextVal);

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

	inline const size_t Size() const;

	__declspec(noalias) inline size_type Capacity() const;

	// Searches the buffer list towards the front for
	// the first buffer containing entries
	inline CqBuffer<T>* const FindBack();
	// Pushes a buffer to the front of the buffer
	// list. Used for newly allocated buffers.
	inline void PushFront(CqBuffer<T>* const aNewCluster);
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

	inline void TryReintegrateEntries();

	inline CqBuffer<T>* const FindTail();

	CqBuffer<T>* myNext;
	CqBuffer<T>* myTail;

	const size_type myCapacity;

	CACHELINE_PADDING(myCapacity);
	CqItemContainer<T>* const myDataBlock;
	CACHELINE_PADDING(CqItemContainer<T>*);
	std::atomic<size_type> myReadSlot;
	CACHELINE_PADDING(size_type);
	std::atomic<size_type> myPostReadIterator[2];
	CACHELINE_PADDING(size_type[2]);
	std::atomic<size_type> myPreReadIterator;
	CACHELINE_PADDING(size_type);
	size_type myWriteSlot[2];
	CACHELINE_PADDING(size_type[2]);
	size_type myPostWriteIterator;


#ifdef CQ_ENABLE_EXCEPTIONS
	std::atomic<size_type> myFailedPops;
	std::atomic_flag myRepairSlot;
#endif
};

template<class T>
inline CqBuffer<T>::CqBuffer(const size_type aCapacity, CqItemContainer<T>* const aDataBlock)
	: myNext(nullptr)
	, myTail(nullptr)
	, myDataBlock(aDataBlock)
	, myCapacity(aCapacity)
	, myReadSlot(0)
	, myPostReadIterator{ 0,0 }
	, myPreReadIterator(0)
	, myWriteSlot{ 0,0 }
	, myPostWriteIterator(0)
#ifdef CQ_ENABLE_EXCEPTIONS
	, myFailedPops(0)
#endif
{
#ifdef CQ_ENABLE_EXCEPTIONS
	myRepairSlot.clear();
#endif
}

template<class T>
inline void CqBuffer<T>::DestroyAll() {
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
inline CqBuffer<T>* const CqBuffer<T>::FindBack() {
	CqBuffer<T>* back(this);

	while (back) {
		if (back->myReadSlot.load(std::memory_order_relaxed) != back->myPostWriteIterator)
			break;

		back = back->myNext;
	}
	return back;
}

template<class T>
inline const size_t CqBuffer<T>::Size() const {
	size_t size(myPostWriteIterator);
	size -= myPreReadIterator.load(std::memory_order_relaxed);

	if (myNext)
		size += myNext->Size();

	return size;
}

template<class T>
__declspec(noalias) inline typename CqBuffer<T>::size_type CqBuffer<T>::Capacity() const {
	return myCapacity;
}

template<class T>
inline void CqBuffer<T>::PushFront(CqBuffer<T>* const aNewBuffer) {
	CqBuffer<T>* last(this);
	while (last->myNext) {
		last = last->myNext;
	}
	last->myNext = aNewBuffer;
	aNewBuffer->myTail = last;
}
template<class T>
template<class ...Arg>
inline const bool CqBuffer<T>::TryPush(Arg && ...aIn) {
	const size_type blockCapacity(myCapacity / 2);
	const uint8_t block(static_cast<uint8_t>((myPostWriteIterator % myCapacity) / blockCapacity));

	if (myWriteSlot[block] == blockCapacity)
		return false;

	const size_type blockSlot(myWriteSlot[block]++);
	const size_type blockOffset(blockCapacity * block);
	const size_type slot(blockOffset + blockSlot);

	WriteIn(slot, std::forward<Arg>(aIn)...);

	std::_Atomic_signal_fence(std::memory_order_seq_cst);

	++myPostWriteIterator;

	return true;
}
template<class T>
inline const bool CqBuffer<T>::TryPop(T & aOut) {
	const size_type slotReservation(++myPreReadIterator);
	const size_type slotAvaliability(myPostWriteIterator);
	const size_type difference(slotAvaliability - slotReservation);

	if (myCapacity < difference) {
		--myPreReadIterator;
		return false;
	}

	const size_type readSlotTotal(myReadSlot++);
	const size_type readSlotBlockTotal(readSlotTotal % myCapacity);

	WriteOut(readSlotBlockTotal, aOut);

	const uint8_t origin(myDataBlock[readSlotBlockTotal].Origin());

	const size_type readEntries(++myPostReadIterator[origin]);
	const size_type blockCapacity(myCapacity / 2);

	if (readEntries == blockCapacity) {
		myPostReadIterator[origin].store(0, std::memory_order_release);
		myWriteSlot[origin] = 0;
	}

	return true;
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, U&& aIn) {
	const size_type blockCapacity(myCapacity / 2);
	const uint8_t block(static_cast<uint8_t>(aSlot / blockCapacity));

	myDataBlock[aSlot].Store(std::move(aIn), block);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_MOVE(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, U&& aIn) {
	const size_type blockCapacity(myCapacity / 2);
	const uint8_t block(static_cast<uint8_t>(aSlot / blockCapacity));

	try {
		myDataBlock[aSlot].Store(std::move(aIn), block);
	}
	catch (...) {
		--myWriteSlot[block];
		throw;
	}
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, const U& aIn) {
	const size_type blockCapacity(myCapacity / 2);
	const uint8_t block(static_cast<uint8_t>(aSlot / blockCapacity));

	myDataBlock[aSlot].Store(aIn, block);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_PUSH_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteIn(const size_type aSlot, const U& aIn) {
	const size_type blockCapacity(myCapacity / 2);
	const uint8_t block(static_cast<uint8_t>(aSlot / blockCapacity));

	try {
		myDataBlock[aSlot].Store(aIn, block);
	}
	catch (...) {
		--myWriteSlot[block];
		throw;
	}
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_MOVE(U)>*>
inline void CqBuffer<T>::WriteOut(const size_type aSlot, U& aOut) {
	myDataBlock[aSlot].Move(aOut);
}
template <class T>
template <class U, std::enable_if_t<CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteOut(const size_type aSlot, U& aOut) {

	myDataBlock[aSlot].Assign(aOut);
}
template <class T>
template <class U, std::enable_if_t<!CQ_BUFFER_NOTHROW_POP_MOVE(U) && !CQ_BUFFER_NOTHROW_POP_ASSIGN(U)>*>
inline void CqBuffer<T>::WriteOut(const size_type aSlot, U& aOut) {
	try {
		myDataBlock[aSlot].TryMove(aOut);
	}
	catch (...) {
		myDataBlock[aSlot].SetReintegrationTag();
		TryReintegrateEntries();
		throw;
	}
}
template<class T>
inline void CqBuffer<T>::TryReintegrateEntries() {
#ifdef CQ_ENABLE_EXCEPTIONS
	myFailedPops.fetch_add(1, std::memory_order_release);
	// Designate first thread to enter the repairman, 
	// and throw everyone else out
	const bool isClaimed(myRepairSlot.test_and_set());

	if (!isClaimed) {
		const size_type blockCapacity(myCapacity / 2);
		const size_type nextWriteCycle((myPostWriteIterator / blockCapacity) + 1);
		const size_type writeCap(nextWriteCycle * blockCapacity);
		const size_type offsetFromWriteCap(std::numeric_limits<int16_t>::max());
		const size_type preReadReplacement(writeCap + offsetFromWriteCap);
		const size_type preReplacementReadIterator(myPreReadIterator.exchange(preReadReplacement));
		const size_type readReplacementOffset(preReadReplacement - preReplacementReadIterator);

		// Now we need to wait for all variables to add up before continuing
		for (;;) {
			const size_type preReadIterator(myPreReadIterator.load(std::memory_order_acquire) - readReplacementOffset);
			if (preReadIterator == myReadSlot.load(std::memory_order_acquire)) {
				break;
			}
		}
		for (;;) {
			const size_type readSlot(myReadSlot.load(std::memory_order_acquire));
			const size_type readSlotBlockTotal(readSlot % myCapacity);
			const size_type activeBlock(readSlotBlockTotal / blockCapacity);
			const size_type readSlotBlock(readSlot % blockCapacity);
			const size_type postReadIterator(myPostReadIterator[activeBlock].load(std::memory_order_acquire));
			const size_type toMatch(postReadIterator  + myFailedPops.load(std::memory_order_acquire));

			if (readSlotBlock == toMatch) {
				size_type movedEntries(0);
				// And finally, swap the bad entries up front and decrement
				// all the iterators
				for (size_type i = readSlotBlockTotal - 1; !(readSlotBlockTotal < i) & (myFailedPops.load(std::memory_order_acquire)); --i) {
					CqItemContainer<T>& current(myDataBlock[i]);
					if (current.IsTaggedForReintegration()) {
						current.RemoveReintegrationTag();
						++movedEntries;
						myFailedPops.fetch_sub(1, std::memory_order_relaxed);
						CqItemContainer<T>& target(myDataBlock[readSlotBlockTotal - movedEntries]);
						myDataBlock[i].Swap(target);
					}
				}
				myRepairSlot.clear();

				// Restore the iterator variables to valid states
				myReadSlot.fetch_sub(movedEntries, std::memory_order_release);
				myPreReadIterator.fetch_sub(movedEntries + readReplacementOffset, std::memory_order_release);
				break;
			}
		}
	}
#endif
}
template<class T>
inline CqBuffer<T>* const CqBuffer<T>::FindTail() {
	CqBuffer<T>* tail(this);
	while (tail->myTail) {
		tail = tail->myTail;
	}
	return tail;
}
template <class T>
class CqItemContainer
{
public:
	CqItemContainer<T>(const CqItemContainer<T>&) = delete;
	CqItemContainer<T>& operator=(const CqItemContainer&) = delete;

	inline CqItemContainer();

	inline void Store(const T& aIn, const uint8_t aOrigin);
	inline void Store(T&& aIn, const uint8_t aOrigin);

	inline void Swap(CqItemContainer<T>& aOther);

	template<class U = T, std::enable_if_t<std::is_move_assignable<U>::value>* = nullptr>
	inline void TryMove(U& aOut);
	template<class U = T, std::enable_if_t<!std::is_move_assignable<U>::value>* = nullptr>
	inline void TryMove(U& aOut);

	inline void Assign(T& aOut);
	inline void Move(T& aOut);

	inline void SetReintegrationTag();
	inline void RemoveReintegrationTag();
	inline const bool IsTaggedForReintegration() const;

	inline const uint8_t Origin() const;
private:
	inline T& Reference() const;
	static const uint64_t ourPtrMask = (uint64_t(UINT32_MAX) << 16 | uint64_t(UINT16_MAX));

	T myData;
	union
	{
		T* myReference;
		uint64_t myPtrBlock;
		struct
		{
			uint16_t trash[3];
			uint8_t myOrigin;
			uint8_t myReintegrationTag;
		};
	};
};
template<class T>
inline CqItemContainer<T>::CqItemContainer() :
	myReference(nullptr),
	myData() {
}
template<class T>
inline void CqItemContainer<T>::Store(const T & aIn, const uint8_t aOrigin) {
	myData = aIn;
	myReference = &myData;
	myOrigin = aOrigin;
}
template<class T>
inline void CqItemContainer<T>::Store(T && aIn, const uint8_t aOrigin) {
	myData = std::move(aIn);
	myReference = &myData;
	myOrigin = aOrigin;
}
template<class T>
inline void CqItemContainer<T>::Swap(CqItemContainer<T>& aOther) {
	T* const ref(aOther.myReference);
	aOther.myReference = myReference;
	myReference = ref;
}
template<class T>
inline void CqItemContainer<T>::Assign(T & aOut) {
	aOut = Reference();
}
template<class T>
inline void CqItemContainer<T>::Move(T & aOut) {
	aOut = std::move(Reference());
}
template<class T>
inline void CqItemContainer<T>::SetReintegrationTag() {
	myReintegrationTag = 1;
}
template<class T>
inline void CqItemContainer<T>::RemoveReintegrationTag() {
	myReintegrationTag = 0;
}
template<class T>
inline const bool CqItemContainer<T>::IsTaggedForReintegration() const {
	return myReintegrationTag;
}
template<class T>
inline const uint8_t CqItemContainer<T>::Origin() const {
	return myOrigin;
}
template<class T>
inline T& CqItemContainer<T>::Reference() const {
	return *reinterpret_cast<T*>(myPtrBlock & ourPtrMask);
}
template<class T>
template<class U, std::enable_if_t<std::is_move_assignable<U>::value>*>
inline void CqItemContainer<T>::TryMove(U& aOut) {
	aOut = std::move(Reference());
}
template<class T>
template<class U, std::enable_if_t<!std::is_move_assignable<U>::value>*>
inline void CqItemContainer<T>::TryMove(U& aOut) {
	aOut = Reference();
}