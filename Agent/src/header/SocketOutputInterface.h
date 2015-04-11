#include <WinSock2.h>
#include <WS2tcpip.h>
#include <IPHlpApi.h>
#pragma comment(lib, "Ws2_32.lib")
#include <JVMHeapScan.h>

# define MESSAGE_SEPERATOR "\n"
# define CONSUMER_WAIT_SECONDS 100

char FOLLOW_REFERENCE = '1';
char START_GC = '2';
char GET_BASIC_DATA = '3';
char CLOSE_CONNECTION = '4';

typedef enum
{
	IDLE = 0,
	LISTENING = 1,
	WAITING = 3,
	NORMAL = 4,
	ACCUMULATING = 5
};

typedef enum errorCodes
{
	NONE = 0,
	LISTEN_SOCKET_CLEANUP = 1,
	CLIENT_SOCKET_CLEANUP = 2
};