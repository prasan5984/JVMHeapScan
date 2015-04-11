#include <ThreadSync_PriorityProducer.h>

static struct ProducerSpace leftProducerSpace, rightProducerSpace, *currentSpace, consumingSpace;
static HANDLE consumerGate;
static boolean consumerIn = FALSE;

static int entrySz, messageQueueSz, producerSpaceSz, threadSz;
static HANDLE mainProducerGate;

static struct MsgStoreIntf messageStore;

static void* allocateMemory(int size)
{
	return malloc(size);
}

void setSizes()
{
	entrySz = sizeof(struct Entry);
	messageQueueSz = sizeof(struct MessageQueue);
	producerSpaceSz = sizeof(struct ProducerSpace);
	threadSz = sizeof(LONG);
}

HANDLE createMutex(boolean initialState)
{
	HANDLE mutex = CreateMutex(NULL, initialState, NULL);
	return mutex;
}

HANDLE createEvent(boolean manualReset, boolean initialState)
{
	HANDLE lock = CreateEvent(NULL, manualReset, initialState, NULL);
	return lock;
}

struct Entry createEntry(MSG_LOCATION_ID id)
{
	struct Entry* entry = (struct Entry*) allocateMemory(entrySz);
	struct Entry* otherEntry = (struct Entry*) allocateMemory(entrySz);

	HANDLE lock = createMutex(FALSE);
	entry->id = id;
	entry->lock = lock;
	entry->otherEntry = otherEntry;
	return *entry;
}

struct ProducerSpace createNewSpace(MSG_LOCATION_ID id)
{
	struct Entry leftEntry = createEntry(LEFT_ENTRY);
	struct Entry rightEntry = createEntry(RIGHT_ENTRY);

	*(leftEntry.otherEntry) = rightEntry;
	*(rightEntry.otherEntry) = leftEntry;

	struct MessageQueue* msgQueue = (struct MessageQueue*) allocateMemory(messageQueueSz);
	msgQueue->leftEntry = leftEntry;
	msgQueue->rightEntry = rightEntry;

	struct ProducerSpace* space = (struct ProducerSpace*) allocateMemory(producerSpaceSz);
	space->gate = createEvent(TRUE, TRUE);
	space->accumlateGate = createEvent(FALSE, FALSE);
	space->isReady = FALSE;
	space->messageQueue = *msgQueue;

	volatile LONG* threads = (LONG*)allocateMemory(threadSz);
	*threads = 0;

	space->threads = threads;
	space->id = id;

	return *space;
}

static void onInitialize(struct MsgStoreIntf msgStore)
{
	setSizes();
	leftProducerSpace = createNewSpace(LEFT_SPACE);
	rightProducerSpace = createNewSpace(RIGHT_SPACE);

	leftProducerSpace.otherSpace = &rightProducerSpace;
	rightProducerSpace.otherSpace = &leftProducerSpace;

	currentSpace = &leftProducerSpace;

	consumerGate = createEvent(TRUE, FALSE);
	mainProducerGate = createMutex(FALSE);

	messageStore = msgStore;
}

struct Entry getMessageEntry(struct MessageQueue queue)
{
	int status;
	struct Entry entry = queue.leftEntry;

	status = WaitForSingleObject(entry.lock, 0);

	while (status == WAIT_TIMEOUT)
	{
		entry = *(entry.otherEntry);
		status = WaitForSingleObject(entry.lock, QUEUE_WAIT_TIME);
	}

	return entry;
}

struct ProducerSpace changeSpace()
{
	int status;
	struct ProducerSpace otherSpace = *(currentSpace->otherSpace);

	status = WaitForSingleObject(mainProducerGate, INFINITE);

	if (!otherSpace.isReady)
	{
		SetEvent(consumerGate);
		otherSpace.isReady = TRUE;
		currentSpace = currentSpace->otherSpace;
	}

	ReleaseMutex(mainProducerGate);

	return otherSpace;
}

void onProduce(void* msg)
{
	int entryStatus;
	struct ProducerSpace localSpace = *currentSpace;
	entryStatus = WaitForSingleObject(localSpace.gate, 0);

	if (entryStatus != WAIT_OBJECT_0)
	{
		localSpace = changeSpace();
	}
	else
	{
		if (!consumerIn)
			SetEvent(consumerGate);
	}

	SetEvent(localSpace.accumlateGate);
	InterlockedIncrement(localSpace.threads);
	struct Entry entry = getMessageEntry(localSpace.messageQueue);

	messageStore.STORE_MSG(localSpace.id + entry.id, msg);

	ReleaseMutex(entry.lock);
	InterlockedDecrement(localSpace.threads);
}

int onTryConsumerEntry(int waitSeconds)
{
	int status;
	status = WaitForSingleObjectEx(consumerGate, waitSeconds, TRUE);

	// TODO: CHANGE IT TO A THREAD SYNC STATUS
	if (status == WAIT_OBJECT_0)
	{
		consumerIn = TRUE;
		ResetEvent(consumerGate);
	}
	return status;
}

void accumulateMessage(struct ProducerSpace space)
{
	int status;
	while (1)
	{
		status = WaitForSingleObjectEx(space.accumlateGate, messageStore.accumulateThresholdMillis, TRUE);
		if (status != WAIT_OBJECT_0)
			break;
		SleepEx(messageStore.accumulateSleepMillis, TRUE);
	}
}

MSG_LOCATION_ID onStartConsume()
{
	struct ProducerSpace localSpace = *currentSpace;

	if (messageStore.accumulate)
		accumulateMessage(localSpace);

	struct ProducerSpace otherSpace = *(localSpace.otherSpace);
	otherSpace.isReady = FALSE;
	SetEvent(otherSpace.gate);

	ResetEvent(localSpace.gate);
	consumerIn = FALSE;

	while (*(localSpace.threads) != 0)
	{
		Sleep(CONSUMER_WAIT_TIME);
	}

	struct MessageQueue messageQueue = localSpace.messageQueue;
	WaitForSingleObjectEx(messageQueue.leftEntry.lock, INFINITE, TRUE);
	WaitForSingleObjectEx(messageQueue.rightEntry.lock, INFINITE, TRUE);
	consumingSpace = localSpace;

	return localSpace.id;
}

void onEndConsume()
{
	struct MessageQueue messageQueue = consumingSpace.messageQueue;
	ReleaseMutex(messageQueue.leftEntry.lock);
	ReleaseMutex(messageQueue.rightEntry.lock);

}

SynchronizerInterface getSynchronizerInterface()
{
	SynchronizerInterface* syncIntf = (SynchronizerInterface*)allocateMemory(sizeof(SynchronizerInterface));
	syncIntf->onEndConsume = *onEndConsume;
	syncIntf->onInitialize = *onInitialize;
	syncIntf->onProduce = *onProduce;
	syncIntf->onStartConsume = *onStartConsume;
	syncIntf->onTryConsumerEntry = *onTryConsumerEntry;

	return *syncIntf;
}