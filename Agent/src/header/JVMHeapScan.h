#include <jvmti.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <jni.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <IPHlpApi.h>
#pragma comment(lib, "Ws2_32.lib")
#include <ThreadSync_PriorityProducer.h>

#define MSGDELIMITER '|'
#define PROPERTIES_FILE_NAME "JVMHeapScan.properties" 

enum messageCode
{
	NULL_CODE = 0,
	CLASS_TAG = 1,
	OBJECT_TAG = 2,
	DEALLOCATE_OBJECT = 3,
	HEAP_REF_INFO = 4,
	AGENT_EXIT
};

//INTERFACES

typedef int(*initializeMsgStore)(boolean accFlag);
typedef void(*_flushMsg)(char* msg, int msgLength);
typedef void(*_includeMessage)(char*, int, boolean);
typedef void(*_startMsgGrp)();
typedef void(*_flushMsgGrp)();
typedef boolean(*_retrieveMsg)(MSG_LOCATION_ID id, int* totalMsg, int* totalMsgSize, char** msg);

typedef struct _JvmHeapScanInterfaces JvmHeapScanInterfaces;

typedef void(*_initialize)(JvmHeapScanInterfaces);
typedef char* (*_getMessageSeperator)();
typedef int(*_sendMsg)();
typedef void(*_startProcess)();
typedef void(*_onKillThread)();

typedef void(*_followReference)();
typedef void(*_startGC)();
typedef void(*_onFatalError)(char *errMsg);
typedef void(*_onGetClassMetadata)();

struct _MessageStore
{
	_initialize initialize;
	_flushMsg flushMessage;
	_startMsgGrp startMsgGrp;
	_includeMessage includeMessage;
	_flushMsgGrp flushMsgGrp;
	_retrieveMsg retrieveMsg;
};

struct _OutputInterface
{
	_startProcess start;
	_initialize initialize;
	_getMessageSeperator getMsgSeperator;
	_sendMsg sendMsg;
	_onKillThread onKillThread;
};

struct _JVMFunctions
{
	char* port;
	_initialize initialize;
	_followReference followReference;
	_startGC startGC;
	_onFatalError onFatalError;
	_onGetClassMetadata onGetClassMetadata;
};

struct _AgentOptions
{
	int socketMsgLimit;
	char* port;
	char* trackerPath;
};

typedef struct _OutputInterface OutputInterface;
typedef struct _MessageStore MessageStore;
typedef struct _JVMFunctions JVMFunctions;
typedef struct _AgentOptions AgentOptions;

struct _JvmHeapScanInterfaces
{
	AgentOptions agentOptions;
	MessageStore msgStore;
	OutputInterface opInterface;
	JVMFunctions jvmFunctions;
	SynchronizerInterface syncInterface;
};

extern MessageStore getMsgStoreInterface();
extern OutputInterface getOutputInterface();
extern SynchronizerInterface getSynchronizerInterface();
extern void* allocateMemory(int size);
extern int getChars(jlong value);