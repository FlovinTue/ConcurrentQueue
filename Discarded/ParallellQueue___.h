// Data shuttle v2
// Flovin Michaelsen

#pragma once


#include "GrowingArray.h"
#include <atomic>
#include <cassert>
#include <thread>
#include "ParallellQueueInternals.h"

#pragma warning(disable : 4324)

template <class T, uint32_t MaxProducers = 32>
class ParallellQueue
{
public:
	ParallellQueue(uint32_t aInitClusters) = 4);
	~ParallellQueue();
	ParallellQueue(const ParallellQueue&) = delete;
	ParallellQueue& operator=(const ParallellQueue&) = delete;

	void Push(const T& aIn);
	bool TryPop(T& aOut);

	uint32_t Size() const;

private:
	void InitConsumer();
	void InitProducer();
	bool RelocateConsumer();

	int32_t FindAvaliableCluster(); 

	PqInternals::Cluster<T>* CreateCluster(uint32_t aSize);
	int32_t PushCluster(PqInternals::Cluster<T>* aCluster);

	static const int32_t InvalidIndex = -1;
	static const uint32_t InitClusterCapacity = 64;

	const uint32_t myObjectId;

	static thread_local GrowingArray<PqInternals::Cluster<T>*> ourProducers;
	static thread_local GrowingArray<PqInternals::Cluster<T>*> ourConsumers;

	PqInternals::Cluster<T>* myClusters[MaxProducers];
	uint8_t myPadding0[64 - (sizeof(myClusters) % 64)];
	std::atomic<uint32_t> mySize;
	uint8_t myPadding1[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myClusterCount;
	uint8_t myPadding2[64 - (sizeof(std::atomic<uint32_t>) % 64)];
	std::atomic<uint32_t> myClusterReservations;
	uint8_t myPadding3[64 - (sizeof(std::atomic<uint32_t>) % 64)];

	static std::atomic<uint32_t> ourIdIterator;
};
// Static init
template <class T, uint32_t MaxProducers>
std::atomic<uint32_t> ParallellQueue<T, MaxProducers>::ourIdIterator(0);
template <class T, uint32_t MaxProducers>
thread_local GrowingArray<PqInternals::Cluster<T>*> ParallellQueue<T, MaxProducers>::ourProducers(4);
template <class T, uint32_t MaxProducers>
thread_local GrowingArray<PqInternals::Cluster<T>*> ParallellQueue<T, MaxProducers>::ourConsumers(4);
// -----------

template<class T, uint32_t MaxProducers>
inline ParallellQueue<T, MaxProducers>::ParallellQueue(uint32_t aInitClusters) :
	myObjectId(ourIdIterator++),
	myClusterCount(0),
	myClusterReservations(0),
	mySize(0),
	myDummyCluster(CreateCluster(0))
{
	for (uint32_t i = 0; i < aInitClusters; ++i)
		PushCluster(CreateCluster(InitClusterCapacity));
}

template<class T, uint32_t MaxProducers>
inline ParallellQueue<T, MaxProducers>::~ParallellQueue()
{
	
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::Push(const T & aIn)
{
	if (!(myObjectId < ourProducers.Size()))
		ourProducers.Resize(myObjectId + 1);

	PqInternals::Cluster<T>* cluster = ourProducers[myObjectId];

	if (!cluster)
		InitProducer();

	while (!cluster->TryPush(aIn))
	{
		cluster = CreateCluster(location->Capacity() * 2);
		ourProducers[myObjectId] = cluster;
	}

	++mySize;
	return true;
}
template<class T, uint32_t MaxProducers>
inline bool ParallellQueue<T, MaxProducers>::TryPop(T & aOut)
{
	if (!(myObjectId < ourConsumers.Size()))
		ourConsumers.Resize(myObjectId + 1);

	PqInternals::Cluster<T>* cluster = ourConsumers[myObjectId];

	if (!cluster)
		InitConsumer();

	while (!location->TryPop(aOut))
		if (!RelocateConsumer())
			return false;
	
	--mySize;
	return true;
}
template<class T, uint32_t MaxProducers>
inline uint32_t ParallellQueue<T, MaxProducers>::Size() const
{
	return mySize;
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::InitConsumer()
{
	if (myClusterCount)
		RelocateConsumer();
}
template<class T, uint32_t MaxProducers>
inline void ParallellQueue<T, MaxProducers>::InitProducer()
{
	int32_t nextClusterSlot(InvalidIndex);
	for (;;)
	{
		nextClusterSlot = FindAvaliableCluster();

		if (nextClusterSlot != InvalidIndex)
			if (myBuffers[nextClusterSlot]->RegisterProducer())
				break;
			else
				continue;

		nextClusterSlot = PushCluster();

		if (nextClusterSlot == InvalidIndex)
			return false;

		if (myBuffers[nextClusterSlot]->RegisterProducer())
			break;
	}

	ourProducers[myObjectId].myCurrentBufferSlot = nextClusterSlot;
	return true;
}
template<class T, uint32_t MaxProducers>
inline bool ParallellQueue<T, MaxProducers>::RelocateConsumer()
{
	const uint32_t startClusterSlot = 0;
	const uint32_t buffers(myClusterCount);

	for (int32_t i = 1; i < buffers; ++i)
	{
		const int32_t clusterSlot = (startClusterSlot + i) % buffers;
		if (0 < myClusters[clusterSlot]->Size())
		{
			ourConsumers[myObjectId] = myClusters[clusterSlot];
			return true;
		}
	}
	return false;
}
template<class T, uint32_t MaxProducers>
inline PqInternals::Cluster<T>* ParallellQueue<T, MaxProducers>::CreateCluster(uint32_t aSize)
{
	const uint32_t size(aSize + aSize % 2);

	const uint32_t clusterSize = sizeof(PqInternals::Cluster<T>);
	const uint32_t bufferSize = sizeof(PqInternals::Buffer<T>);
	const uint32_t dataSize = sizeof(T) * size;
	const uint32_t blockSize = clusterSize + dataSize * 2;

	const uint32_t clusterAddress(0);

	const uint32_t bufferAddressA(clusterAddress + clusterSize);
	const uint32_t dataAddressA(bufferAddressA + bufferSize);

	const uint32_t bufferAddressB(dataAddressA + dataSize);
	const uint32_t dataAddressB(bufferAddressB + bufferSize);

	void* block = new uint8_t(blockSize);

	T* dataA = new (block + dataAddressA) T[size];
	T* dataB = new (block + dataAddressB) T[size];
	PqInternals::Buffer<T>* bufferA = new(block + bufferAddressA) PqInternals::Buffer<T>(size, dataA);
	PqInternals::Buffer<T>* bufferB = new(block + bufferAddressB) PqInternals::Buffer<T>(size, dataB);
	PqInternals::Cluster<T>* cluster = new(block + clusterAddress) PqInternals::Cluster<T>(size);

	return cluster;
}

template<class T, uint32_t MaxProducers>
inline int32_t ParallellQueue<T, MaxProducers>::PushCluster(PqInternals::Cluster<T>* aCluster)
{
	const int32_t nextClusterSlot = static_cast<int32_t>(myClusterReservations++);

	assert(nextClusterSlot < MaxProducers && "Tried to allocate beyond max buffers. Increase max buffers?");
	if (!(nextClusterSlot < MaxProducers))
		return InvalidIndex;

	myClusters[nextClusterSlot] = aCluster;

	++myClusterCount;

	return nextClusterSlot;
}
template<class T, uint32_t MaxProducers>
inline int32_t ParallellQueue<T, MaxProducers>::FindAvaliableCluster()
{
	const int32_t bufferCount(static_cast<int32_t>(myClusterCount));

	const int32_t startClusterSlot(ourProducers[myObjectId].myCluster);

	for (int32_t i = 1; i < bufferCount + 1; ++i)
	{
		const int32_t clusterSlot((startClusterSlot + i) % bufferCount);

		if (myClusters[clusterSlot] && myClusters[clusterSlot]->Avaliable())
			return clusterSlot;
	}
	return InvalidIndex;
}