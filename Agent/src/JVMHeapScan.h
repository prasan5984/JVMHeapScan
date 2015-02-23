#include <jvmti.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <jni.h>
#include <Windows.h>

#define MSGDELIMITER '|'
#define PAUSE_SECONDS 100
#define FILE_PATH_LENGTH 100
#define DEFAULT_OP_FILE "F:\\MemDetails.txt"

enum messageCode
{
	NULL_CODE = 0,
	CLASS_TAG = 1,
	OBJECT_TAG = 2,
	DEALLOCATE_OBJECT = 3,
	HEAP_REF_INFO = 4
};

struct classTagDtl
{
	jlong tag;
	char* classSign;
};

struct objectTagDtl
{
	jlong tag;
	jlong classTag;
};

struct heapRefDtl
{
	jint referenceKind;
	jint index;
	jlong classTag;
	jlong referrerClassTag;
	jlong tag;
	jlong referrerTag;
	jint length;
	jlong size;
};

union msgDtlPtr
{
	struct classTagDtl* ptrClassTagDtl;
	struct objectTagDtl* ptrObjectDtl;
	struct heapRefDtl* ptrHeapRefDtl;
};

struct msgStruct
{
	enum messageCode code;
	union msgDtlPtr detail_struct;
	struct msgStruct* nextMsg;
};