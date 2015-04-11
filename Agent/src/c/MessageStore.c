# include <JVMHeapScan.h>
# include <strsafe.h>

static OutputInterface opInterface;
static SynchronizerInterface syncInterface;
static boolean isToBeSplit;
static int splitSz;

static struct MsgNode
{
	int msgSize;
	char* msg;
	struct MsgNode *nextNode;
};

static struct MsgGroup
{
	int totalMsgSize;
	int totalMsgs;
	struct MsgNode *firstNode;
	struct MsgNode *lastNode;
	struct MsgGroup* nextGrp;
};

static struct MsgGroupHolder
{
	struct MsgGroup* startGrp;
	struct MsgGroup* curMsgGrp;
	struct MsgGroup* nextGrp;
};

static struct MsgGroup curMsgGrp;

static struct MsgGroupHolder leftSpaceLeftEntry, leftSpaceRightEntry, rightSpaceLeftEntry, rightSpaceRightEntry;

void freeMsgNode(struct MsgNode* msgNode)
{
	free(msgNode->msg);
	free(msgNode);
}

struct MsgNode* createMsgNode(char* msg, int msgSize)
{
	struct MsgNode *msgNode = (struct MsgNode*) (allocateMemory(sizeof(struct MsgNode)));
	msgNode->msgSize = msgSize;
	msgNode->msg = msg;
	msgNode->nextNode = NULL;

	return msgNode;
}

void initializeMsgGrp(struct MsgGroup* localMsgGrp)
{
	struct MsgNode *msgNode = createMsgNode("\0", sizeof("\0"));
	localMsgGrp->firstNode = msgNode;
	localMsgGrp->lastNode = msgNode;
	localMsgGrp->totalMsgs = 0;
	localMsgGrp->totalMsgSize = sizeof("\0");
	localMsgGrp->nextGrp = NULL;
}

void onStartMsgGrp()
{
	initializeMsgGrp(&curMsgGrp);
}

void onInitializeMsgGroupHolder(struct MsgGroupHolder* msgHolder)
{
	struct MsgGroup* msgGroup = (struct MsgGroup*)allocateMemory(sizeof(struct MsgGroup));
	initializeMsgGrp(msgGroup);
	msgHolder->startGrp = msgGroup;
	msgHolder->curMsgGrp = msgGroup;
	msgHolder->nextGrp = NULL;
}

struct MsgGroupHolder* getMsgGroupHolder(int id)
{
	if (id & 1)
	{
		if (id & 2)
			return &rightSpaceLeftEntry;
		return &rightSpaceRightEntry;
	}
	else
	{
		if (id & 2)
			return &leftSpaceLeftEntry;
		return &leftSpaceRightEntry;
	}
}

void mergeEntries(struct MsgGroupHolder* leftEntry, struct MsgGroupHolder* rightEntry)
{
	leftEntry->nextGrp = rightEntry->startGrp;
	leftEntry->curMsgGrp = rightEntry->curMsgGrp;
	onInitializeMsgGroupHolder(rightEntry);
}

struct MsgGroupHolder* getMsgGroupHolderInSpace(MSG_LOCATION_ID id)
{
	if (id & 1)
	{
		mergeEntries(&rightSpaceLeftEntry, &rightSpaceRightEntry);
		return &rightSpaceLeftEntry;
	}
	mergeEntries(&leftSpaceLeftEntry, &leftSpaceRightEntry);
	return &leftSpaceLeftEntry;
}

void mergeMsgAsString(struct MsgGroup* msgGrp, char** totalMsg, int* totalMsgSize)
{
	struct MsgNode *msgNode = msgGrp->firstNode->nextNode;
	char* msgSeperator = opInterface.getMsgSeperator();
	int seperatorSize = (strlen(msgSeperator) + 1 /* for \0 */) * sizeof(char);
	int msgSize = msgGrp->totalMsgSize + msgGrp->totalMsgs * seperatorSize;
	*totalMsg = allocateMemory(msgSize);
	int position = 0;
	int counter = 0;

	if (msgNode != NULL)
	{
		position += sprintf_s(*totalMsg + position, msgSize - position, "%s", msgNode->msg);
		struct MsgNode* tmpNextNode = msgNode->nextNode;
		freeMsgNode(msgNode);
		msgNode = tmpNextNode;

		while (msgNode != NULL)
		{
			//printf("%d Size:%d\n", counter++, position);
			position += sprintf_s(*totalMsg + position, msgSize - position, "%s%s", msgSeperator, msgNode->msg);
			tmpNextNode = msgNode->nextNode;
			freeMsgNode(msgNode);
			msgNode = tmpNextNode;
		}
	}

	// RESIZING ...
	*totalMsgSize = (strlen(*totalMsg) + 1) /*for /0*/ * sizeof(char);
}


void onFlushMsg(char* msg, int msgSize)
{
	struct MsgNode *msgNode = createMsgNode(msg, msgSize);
	syncInterface.onProduce(msgNode);
}

void onFlushMsgGrp()
{
	char* totalMsg;
	int msgSize;
	mergeMsgAsString(&curMsgGrp, &totalMsg, &msgSize);
	onFlushMsg(totalMsg, msgSize);
}

void onIncludeMessage(char* msg, int msgSize, boolean immediateFl)
{
	if (!immediateFl)
	{
		struct MsgNode *msgNode = createMsgNode(msg, msgSize);
		curMsgGrp.lastNode->nextNode = msgNode;
		curMsgGrp.lastNode = msgNode;
		curMsgGrp.totalMsgSize = msgNode->msgSize + curMsgGrp.totalMsgSize;
		curMsgGrp.totalMsgs++;
	}
	else
		onFlushMsg(msg, msgSize);
}

void onQueueMessage(MSG_LOCATION_ID id, void* msg)
{
	struct MsgNode *msgNode = (struct MsgNode*) msg;
	struct MsgGroupHolder* msgGrpHolder = getMsgGroupHolder(id);
	struct MsgGroup* curMsgGrp = msgGrpHolder->curMsgGrp;

	if (isToBeSplit)
	{
		if (splitSz <= curMsgGrp->totalMsgs)
		{
			struct MsgGroup* newGrp = (struct MsgGroup*) allocateMemory(sizeof(struct MsgGroup));
			initializeMsgGrp(newGrp);
			curMsgGrp->nextGrp = newGrp;
			msgGrpHolder->curMsgGrp = newGrp;
			curMsgGrp = newGrp;
		}
	}
	curMsgGrp->lastNode->nextNode = msgNode;
	curMsgGrp->lastNode = msgNode;
	curMsgGrp->totalMsgSize += msgNode->msgSize;
	curMsgGrp->totalMsgs++;
}

void mergeGrp(struct MsgGroup* group1, struct MsgGroup* group2)
{
	group1->totalMsgs += group2->totalMsgs;
	group1->totalMsgSize += group2->totalMsgSize;
	group1->lastNode->nextNode = group2->firstNode->nextNode;
	free(group2);
}

boolean onGetMsgSplit(struct MsgGroupHolder* holder, struct MsgGroup** curGrp)
{
	*curGrp = holder->startGrp;
	struct MsgGroup* nextGrp = holder->nextGrp;

	int totMsgs = (*curGrp)->totalMsgs;

	while (nextGrp != NULL)
	{
		if ((totMsgs += nextGrp->totalMsgs) <= splitSz)
		{
			mergeGrp(*curGrp, nextGrp);
			nextGrp = (*curGrp)->nextGrp;
		}
		else
		{
			holder->startGrp = nextGrp;
			holder->nextGrp = nextGrp->nextGrp;
			return TRUE;
		}
	}
	return FALSE;
}

boolean onRetrieveMessage(MSG_LOCATION_ID id, int* totalMsg, int* totalMsgSize, char** msg)
{
	struct MsgGroupHolder* msgHolder = getMsgGroupHolderInSpace(id);
	struct MsgGroup *msgGroup;

	boolean pendingFl = onGetMsgSplit(msgHolder, &msgGroup);
	
	if (!pendingFl)
		onInitializeMsgGroupHolder(msgHolder);

	*totalMsg = msgGroup->totalMsgs;

	if (*totalMsg < 1)
		return pendingFl;

	mergeMsgAsString(msgGroup, msg, totalMsgSize);
	free(msgGroup);
	return pendingFl;
}

static void onInitialize(JvmHeapScanInterfaces intfs)
{
	opInterface = intfs.opInterface;
	syncInterface = intfs.syncInterface;
	onInitializeMsgGroupHolder(&leftSpaceLeftEntry);
	onInitializeMsgGroupHolder(&leftSpaceRightEntry);
	onInitializeMsgGroupHolder(&rightSpaceLeftEntry);
	onInitializeMsgGroupHolder(&rightSpaceRightEntry);

	struct MsgStoreIntf dataStore;
	dataStore.accumulate = FALSE;
	dataStore.accumulateSleepMillis = 0;
	dataStore.accumulateThresholdMillis = 0;
	dataStore.STORE_MSG = onQueueMessage;
	syncInterface.onInitialize(dataStore);

	if (intfs.agentOptions.socketMsgLimit > 0)
	{
		isToBeSplit = TRUE;
		splitSz = intfs.agentOptions.socketMsgLimit;
	}
}

MessageStore getMsgStoreInterface()
{
	MessageStore* thisStore = (MessageStore*)allocateMemory(sizeof(MessageStore));
	thisStore->initialize = onInitialize;
	thisStore->startMsgGrp = onStartMsgGrp;
	thisStore->flushMessage = onFlushMsg;
	thisStore->flushMsgGrp = onFlushMsgGrp;
	thisStore->includeMessage = onIncludeMessage;
	thisStore->retrieveMsg = onRetrieveMessage;
	return *thisStore;
}