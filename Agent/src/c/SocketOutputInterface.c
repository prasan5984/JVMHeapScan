# include <SocketOutputInterface.h>

//static int status;
static SOCKET clientSocket, listenSocket = INVALID_SOCKET;
static MessageStore msgStore;
static boolean statusFl = TRUE;
//static HANDLE socketMutex;
static SynchronizerInterface syncInterface;
static JVMFunctions jvmFunctions;

static void onStart();
static int onSendMessage(char** msgBuffer, int* length);

static DWORD threadId = 0;
int threadStatus = IDLE;
static HANDLE statusLock;
static boolean die = FALSE;

void onHandleSocketError(int status)
{
	closesocket(clientSocket);
	onStart();
}

void onCheckSocketError(int status, char* action)
{
	static const int constStrLen = 42;
	if (status != 0)
	{
		int err = WSAGetLastError();
		char* errMsg = (char *)allocateMemory(strlen(action) + getChars(err) + 42);
		sprintf(errMsg, "Error during %s. Winsock error Reference: %d", action, err);
		jvmFunctions.onFatalError(errMsg);
	}
}

int onCleanSocket(int status)
{
	if (status > LISTENING)
	{
		int size = getChars(AGENT_EXIT) + 1;
		char* msgStr = (void*)allocateMemory(size);
		memset(msgStr, 0x0, size);
		sprintf(msgStr, "%d", AGENT_EXIT);
		onSendMessage(&msgStr, &size); // Introduce helper methods and it is to be moved there
		int err = closesocket(clientSocket);
		if (err == SOCKET_ERROR)
			return CLIENT_SOCKET_CLEANUP;
	}
	if (status > IDLE)
	{
		int err = closesocket(listenSocket);
		if (err == SOCKET_ERROR)
			return LISTEN_SOCKET_CLEANUP;
	}
	return NONE;
}

void CALLBACK onThreadExit(ULONG_PTR dwParam)
{
	int err = onCleanSocket(threadStatus);
	ExitThread(err);
}

void onChangeStatus(int status)
{
	DWORD waitStatus = WaitForSingleObjectEx(statusLock, 0, TRUE);

	if (waitStatus != WAIT_OBJECT_0)
	{
		if (die)
			onThreadExit((ULONG_PTR)0);
	}
	else
		threadStatus = status;
}

void onKillThread()
{
	ResetEvent(statusLock);
	die = TRUE;

	if (threadStatus > IDLE)
	{
		HANDLE currentThread = OpenThread(THREAD_ALL_ACCESS, FALSE, threadId);
		if (threadStatus & 1)
		{
			DWORD returnStatus = QueueUserAPC(onThreadExit, currentThread, (ULONG_PTR)0);
			DWORD status1 = GetLastError();
		}
		WaitForSingleObject(currentThread, INFINITE);

	}
}

void initializeSocket(char* port)
{
	WSADATA wsaData;
	int status;

	status = WSAStartup(MAKEWORD(2, 2), &wsaData);
	onCheckSocketError(status, "'Agent Socket Initialization'");

	struct addrinfo *result = NULL, *ptr = NULL, hints;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL, port, &hints, &result);
	onCheckSocketError(status, "'Agent Socket Initialization'");

	if (status != 0)
		return;

	listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	status = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
	onCheckSocketError(status, "'Agent Socket Initialization'");

	if (status != 0)
		return;

	freeaddrinfo(result);
}

int checkIncomingMsg(char** opBuffer, u_long* bufLength)
{
	int status;
	status = ioctlsocket(clientSocket, FIONREAD, bufLength);

	if (status == SOCKET_ERROR)
		return status;

	if (*bufLength <= 0)
		return status;


	*opBuffer = (void*)allocateMemory(*bufLength + 1);
	memset(*opBuffer, 0X0, *bufLength + 1);
	int bytesread = recv(clientSocket, *opBuffer, *bufLength, 0);
	return status;
}

int onSendMessage(char** msgBuffer, int* length)
{
	int status = -1;
	status = send(clientSocket, *msgBuffer, *length, 0);
	return status;
}

static void onInitialize(JvmHeapScanInterfaces intfs)
{
	msgStore = intfs.msgStore;
	syncInterface = intfs.syncInterface;
	jvmFunctions = intfs.jvmFunctions;
	initializeSocket(intfs.agentOptions.port);
	statusLock = CreateEvent(NULL, TRUE, FALSE, NULL);
}

int processIncomingMessage()
{
	char* msgBuffer;
	int length;
	int status;

	status = checkIncomingMsg(&msgBuffer, &length);

	if (status != 0)
		return status;

	if (length > 0)
	{
		char msgCode = *msgBuffer;

		if (msgCode == FOLLOW_REFERENCE)
		{
			jvmFunctions.followReference();
		}
		else if (msgCode == START_GC)
		{
			jvmFunctions.startGC();
		}
		else if (msgCode == GET_BASIC_DATA)
		{
			jvmFunctions.onGetClassMetadata();
		}
	}

	return status;
}

void startFlushProcess()
{
	int status, sendStatus = 0, recvStatus = 0;
	boolean breakFl = FALSE;
	while (!breakFl)
	{
		sendStatus = 0;
		recvStatus = 0;

		recvStatus = processIncomingMessage();
		if (recvStatus == SOCKET_ERROR)
		{
			onHandleSocketError(recvStatus);
			break;
		}

		onChangeStatus(WAITING);
		status = syncInterface.onTryConsumerEntry(CONSUMER_WAIT_SECONDS * 1000);

		if (status == WAIT_OBJECT_0)
		{
			onChangeStatus(ACCUMULATING);
			MSG_LOCATION_ID id = syncInterface.onStartConsume();

			int totalMsg, totalMsgSize;
			char* msg;

			boolean pending = TRUE;
			
			onChangeStatus(NORMAL);
			while (pending)
			{
				pending = msgStore.retrieveMsg(id, &totalMsg, &totalMsgSize, &msg);

				if (totalMsg > 0)
				{
					sendStatus = onSendMessage(&msg, &totalMsgSize);
					free(msg);
					if (sendStatus == SOCKET_ERROR)
					{
						onHandleSocketError(sendStatus);
						breakFl = TRUE;
						break;
					}
				}
			}
		}
	}
}

void onStart()
{
	threadId = GetThreadId(GetCurrentThread());
		
	int status;
	status = listen(listenSocket, SOMAXCONN);
	onCheckSocketError(status, "'Listening to Client Socket'");
	
	SetEvent(statusLock);
	onChangeStatus(LISTENING);
	clientSocket = accept(listenSocket, NULL, NULL);
	onChangeStatus(NORMAL);

	int imode = 1;
	status = ioctlsocket(clientSocket, FIONBIO, &imode);
	onCheckSocketError(status, "'Listening to Client Socket'");
	startFlushProcess();
}

char* onGetMsgSeperator()
{
	return MESSAGE_SEPERATOR;
}

OutputInterface getOutputInterface()
{
	OutputInterface* thisInterface = (OutputInterface *)allocateMemory(sizeof(OutputInterface));

	thisInterface->initialize = onInitialize;
	thisInterface->sendMsg = onSendMessage;
	thisInterface->start = onStart;
	thisInterface->getMsgSeperator = onGetMsgSeperator;
	thisInterface->onKillThread = onKillThread;
	return *thisInterface;
}