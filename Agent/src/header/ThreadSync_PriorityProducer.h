#include <WinSock2.h>
#include <WS2tcpip.h>
#include <IPHlpApi.h>
#pragma comment(lib, "Ws2_32.lib")
# include <Windows.h>
# define QUEUE_WAIT_TIME 100
# define CONSUMER_WAIT_TIME 100

typedef enum
{
	LEFT_SPACE = 0,
	RIGHT_SPACE = 1,
	LEFT_ENTRY = 2,
	RIGHT_ENTRY = 4,
} MSG_LOCATION_ID;

typedef void(*_STORE_MSG)(MSG_LOCATION_ID id, void* msg);

struct MsgStoreIntf
{
	boolean accumulate;
	int accumulateThresholdMillis;
	int accumulateSleepMillis;
	_STORE_MSG STORE_MSG;
};

struct Entry
{
	MSG_LOCATION_ID id;
	HANDLE lock;
	struct Entry *otherEntry;
};

struct MessageQueue
{
	struct Entry leftEntry;
	struct Entry rightEntry;
};

struct ProducerSpace
{
	volatile LONG* threads;
	MSG_LOCATION_ID id;
	HANDLE gate, accumlateGate;
	boolean isReady;
	struct MessageQueue messageQueue;
	struct ProducerSpace* otherSpace;
};

typedef void(*_onInitialize)(struct MsgStoreIntf msgStore);
typedef void(*_onProduce)(void* msg);
typedef int(*_onTryConsumerEntry)(int waitMillis);
typedef MSG_LOCATION_ID(*_onStartConsume)();
typedef void(*_onEndConsume)();


struct _SynchronizerInterface
{
	_onInitialize onInitialize;
	_onProduce onProduce;
	_onTryConsumerEntry onTryConsumerEntry;
	_onStartConsume onStartConsume;
	_onEndConsume onEndConsume;
};

typedef struct _SynchronizerInterface SynchronizerInterface;

SynchronizerInterface getSynchronizerInterface();