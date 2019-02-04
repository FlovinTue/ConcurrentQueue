#pragma once

#include <atomic>
#include <stdint.h>
#include "GrowingArray.h"

namespace PqInternals
{
	// new stuff
	enum RETURNCODE : uint32_t
	{
		RETURNCODE_FAILIURE = 1 << 1,
		RETURNCODE_SUCCESS = 1 << 2,
		RETURNCODE_RELOCATE = 1 << 3
	};
	template <class T>
	class Buffer;

	template <class T>
	class Cluster
	{
	public:
		Cluster(uint32_t aCapacity, Buffer<T>** aBuffers);
		~Cluster() = default;

		bool Avaliable() const;
		bool RegisterProducer();

		bool TryPop(T& aOut);
		bool TryPush(const T& aIn);

		Cluster<T>* FindBack() const;

		uint32_t Size() const;
		uint32_t Capacity() const;

		void PushFront(Cluster<T>* aNewCluster);
		void PushTail(Cluster<T>* aOldCluster);
	private:
		std::atomic<bool> myIsAvaliable;

		Cluster<T>* myNext;
		Cluster<T>* myTail;

		const uint32_t myCapacity;

		std::atomic<uint32_t> myReadSlot;
		uint8_t myPadding0[64 - (sizeof(std::atomic<uint32_t>) % 64)];
		std::atomic<uint32_t> myReadEntries;
		uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
		std::atomic<uint32_t> mySize;
		uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
		uint32_t myWriteSlot[2];
		uint32_t myWrittenSlots;
		uint8_t myPadding3[64 - (sizeof(uint32_t) % 64)];

		const T* myBuffers[2];
	};

	template<class T>
	inline Cluster<T>::Cluster(uint32_t aCapacity, Buffer<T>** aBuffers) :
		myNext(nullptr),
		myTail(nullptr),
		myBuffers{aBuffers[0], aBuffers[1]},
		myCapacity(aCapacity),
		myReadSlot(0),
		myReadEntries(0),
		mySize(0),
		myWriteSlot{ 0,0 },
		myWrittenSlots(0),
		myIsAvaliable(true)
	{

	}
	template<class T>
	inline bool Cluster<T>::Avaliable() const
	{
		return myIsAvaliable;
	}
	template<class T>
	inline bool Cluster<T>::RegisterProducer()
	{
		bool expected(true);
		return myIsAvaliable.compare_exchange_strong(expected, false);
	}
	// ------------------
	template <class T>
	inline bool Cluster<T>::TryPop(T& aOut)
	{
		if (!(--mySize < myCapacity))
		{
			++mySize;
			return false;
		}

		uint32_t bufferCapacity = myCapacity / 2;
		uint32_t readSlotTotal = myReadSlot++;
		uint32_t readSlot = readSlotTotal % bufferCapacity;
		uint32_t buffer = readSlotTotal / bufferCapacity;

		aOut = myBuffers[buffer][readSlot];

		if ((++myReadEntries % bufferCapacity) == 0)
		{
			myBufferStates &= (1 << buffer);
		}
	}
	template <class T>
	inline bool Cluster<T>::TryPush(const T& aIn)
	{
		uint32_t bufferCapacity = myCapacity / 2;
		uint32_t buffer = (myWrittenSlots % myCapacity) / bufferCapacity;

		if (myWriteSlot[buffer] == myCapacity)
			return false;

		uint32_t writeSlotTotal = myWriteSlot++;
		uint32_t writeSlot = writeSlotTotal % bufferCapacity;

		myBuffers[buffer][writeSlot] = aIn;

		++mySize;
	}
	template<class T>
	inline Cluster<T>* Cluster<T>::FindBack() const
	{
		Cluster<T>* back(this);

		while (back)
		{
			if (back->Size())
				break;
			back = back->myNext;
		}
	}

	template<class T>
	inline uint32_t Cluster<T>::Size() const
	{
		return uint32_t();
	}

	template<class T>
	inline uint32_t Cluster<T>::Capacity() const
	{
		return myCapacity;
	}

	template<class T>
	inline void Cluster<T>::PushFront(Cluster<T>* aNewCluster)
	{
		Cluster<T>* next(myNext);
		while (next)
		{
			if (!next->myNext)
				break;

			next = next->myNext;
		}
		next->myNext = aOldCluster;
	}
	template<class T>
	inline void Cluster<T>::PushTail(Cluster<T>* aOldCluster)
	{
		Cluster<T>* tail(myTail);
		while (tail)
		{
			if (!tail->myTail)
				break;

			tail = tail->myTail;
		}
		tail->myTail = aOldCluster;
	}
}