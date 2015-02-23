#include <JVMHeapScan.h>

static FILE *fp;
static int lastTag = 1;
static struct msgStruct *lastMsg, startMsg;


void* allocateMemory(int size)
{
	return malloc(size);
}

void initializeMsgStructure()
{
	startMsg.code = NULL_CODE;
	startMsg.nextMsg = NULL;
	
	lastMsg = &startMsg;
}

void includeMessage(enum MessageCode code, union msgDtlPtr msgDtl)
{
	if (lastMsg->code == NULL_CODE)
	{
		lastMsg->code = code;
		lastMsg->detail_struct = msgDtl;
	}
	else
	{
		struct msgStruct *curMsg = allocateMemory(sizeof(struct msgStruct));
		
		curMsg->code = code;
		curMsg->detail_struct = msgDtl;
		curMsg->nextMsg = NULL;

		lastMsg->nextMsg = curMsg;
		lastMsg = curMsg;
	}
}



void initialize()
{
	FILE *propertiesFile;
	int error = fopen_s(&propertiesFile, "JVMHeapScan.properties", "r");
	
	if (error != 0)
		error = fopen_s(&fp, DEFAULT_OP_FILE, "w");
	else
	{
		char outputFileName[FILE_PATH_LENGTH];

		for (int i = 0; i < FILE_PATH_LENGTH; i++)
		{
			 char fileNameCh = fgetc(propertiesFile);
			 if (fileNameCh != ';')
				 outputFileName[i] = fileNameCh;
			 else
			 {
				 outputFileName[i] = '\0';
				 break;
			 }
		}
		
		fopen_s(&fp, outputFileName, "w");
	}
}

void JNICALL
onVMDeath(jvmtiEnv *jvmti_env,
JNIEnv* jni_env)
{
	fclose(fp);
}

void setObjectTag(jlong* tagPtr, jlong classTag)
{
	*tagPtr = ++lastTag;

	struct objectTagDtl* _objectTagDtl = allocateMemory(sizeof(struct objectTagDtl));
	_objectTagDtl->tag = *tagPtr;
	_objectTagDtl->classTag = classTag;

	union msgDtlPtr* _msgDtlPtr = allocateMemory(sizeof(union msgDtlPtr));
	_msgDtlPtr->ptrObjectDtl = (_objectTagDtl);

	includeMessage(OBJECT_TAG, *_msgDtlPtr);

}

jint JNICALL
followReferenceCallback(jvmtiHeapReferenceKind reference_kind,const jvmtiHeapReferenceInfo* reference_info,jlong class_tag,jlong referrer_class_tag,jlong size,jlong* tag_ptr,jlong* referrer_tag_ptr,jint length,void* user_data)

{
	struct heapRefDtl* _heapRefDtl = allocateMemory(sizeof(struct heapRefDtl));

	_heapRefDtl->referenceKind = reference_kind;
	
	_heapRefDtl->index = -1;
	if (reference_info != NULL)
	{
		switch (reference_kind)
		{
		case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT:
			_heapRefDtl->index = ((jvmtiHeapReferenceInfoArray*)reference_info)->index;
			break;
		case JVMTI_HEAP_REFERENCE_FIELD | JVMTI_HEAP_REFERENCE_STATIC_FIELD:
			_heapRefDtl->index = ((jvmtiHeapReferenceInfoField*)reference_info)->index;
			break;
		case JVMTI_HEAP_REFERENCE_CONSTANT_POOL:
			_heapRefDtl->index = ((jvmtiHeapReferenceInfoConstantPool*)reference_info)->index;
			break;
		default:
			break;
		}
	}
	
	_heapRefDtl->classTag = class_tag;
		
	_heapRefDtl->referrerClassTag = referrer_class_tag;

	if (*tag_ptr == 0 && class_tag != 0)
	{
		setObjectTag(tag_ptr, class_tag);
	}
	_heapRefDtl->tag = *tag_ptr;
	
	if (referrer_tag_ptr != NULL && *referrer_tag_ptr == 0 && referrer_class_tag != 0)
	{
		setObjectTag(referrer_tag_ptr, referrer_class_tag);
	}
	_heapRefDtl->referrerTag = 0;
	if (referrer_tag_ptr != NULL)
		_heapRefDtl->referrerTag = *referrer_tag_ptr;

	_heapRefDtl->length = length;
			
	_heapRefDtl->size = size;
	
	union msgDtlPtr* _msgDtlPtr = allocateMemory(sizeof(union msgDtlPtr));
	_msgDtlPtr->ptrHeapRefDtl = (_heapRefDtl);

	includeMessage(HEAP_REF_INFO, *_msgDtlPtr);

	return JVMTI_VISIT_OBJECTS;

}

void checkAndTagClasses(jvmtiEnv* jvmti_env, jint classCount, jclass* classes)
{
	jint status, i;

	for (i = 0; i < classCount; i++)
	{
		jlong tag_ptr = 0;
		status = (*jvmti_env)->GetTag(jvmti_env, *classes, &tag_ptr);

		if (tag_ptr == 0)
		{
			status = (*jvmti_env)->SetTag(jvmti_env, *classes, lastTag);
			if (status != JNI_OK)
			{
				printf("Failed with error code %d", status);
			}
			
			char* classSign;
			status = (*jvmti_env)->GetClassSignature(jvmti_env, *classes, &classSign, NULL);
			if (status != JNI_OK)
			{
				printf("Failed with error code %d", status);
			}
			
			struct classTagDtl *_tagDtlStruct = allocateMemory(sizeof(struct classTagDtl));
			_tagDtlStruct->classSign = classSign;
			_tagDtlStruct->tag = lastTag;

			union msgDtlPtr *msgDtl = allocateMemory(sizeof(union msgDtlPtr));
			msgDtl->ptrClassTagDtl = _tagDtlStruct;

			includeMessage(CLASS_TAG, *msgDtl);
			
			lastTag++;
		}
		classes++;
	}
}

void printInt(int intValue)
{
	fprintf(fp, "%d%c", intValue, MSGDELIMITER);
}

void printLong(jlong longValue)
{
	fprintf(fp, "%ld%c", longValue, MSGDELIMITER);
}

void printHeapRefInfo(union msgDtlPtr _msgDtlPtr)
{
	struct heapRefDtl _heapRefDtl = *(_msgDtlPtr.ptrHeapRefDtl);

	printInt(_heapRefDtl.referenceKind);
	
	if (_heapRefDtl.index < 0)
		fprintf(fp, "%c", MSGDELIMITER);
	else
		printInt(_heapRefDtl.index);

	printLong(_heapRefDtl.classTag);
	printLong(_heapRefDtl.referrerClassTag);
	printLong(_heapRefDtl.tag);
	printLong(_heapRefDtl.referrerTag);
	
	if (_heapRefDtl.length < 0)
		fprintf(fp, "%c", MSGDELIMITER);
	else
		printInt(_heapRefDtl.length);
	
	printLong(_heapRefDtl.size);
}

void printClassTag(union msgDtlPtr _msgDtlPtr)
{
	struct classTagDtl _classTagDtl = *(_msgDtlPtr.ptrClassTagDtl);

	printLong(_classTagDtl.tag);
	fprintf(fp, "%s%c", _classTagDtl.classSign, MSGDELIMITER);
}

void printObjectTag(union msgDtlPtr _msgDtlPtr)
{
	struct objectTagDtl _objectTagDtl = *(_msgDtlPtr.ptrObjectDtl);

	printLong(_objectTagDtl.tag);
	printLong(_objectTagDtl.classTag);
}

void flushMessages()
{
	struct msgStruct* msg = &startMsg;

	while (msg != NULL)
	{
		if (msg->code == NULL_CODE)
			return;

		printInt(msg->code);
		
		switch ((*msg).code)
		{
		case CLASS_TAG:
			printClassTag(msg->detail_struct);
			break;
		
		case OBJECT_TAG:
			printObjectTag(msg->detail_struct);
			break;

		case DEALLOCATE_OBJECT:
			break;

		case HEAP_REF_INFO:
			printHeapRefInfo(msg->detail_struct);
			break;
		}

		fprintf(fp, "\n");

		msg = (*msg).nextMsg;
	}
	
}

void referenceFollowerInit(jvmtiEnv* jvmti_env, JNIEnv* jni_env, void* arg)
{
	jint status;
	jvmtiHeapCallbacks heapCallBacks;
	jint size = sizeof(heapCallBacks);
	memset(&heapCallBacks, 0, size);
	
	heapCallBacks.heap_reference_callback = &followReferenceCallback;

	while (1)
	{
		fprintf(fp, "%s\n", "*** Start of Iteration ***");

		jint classCount;
		jclass* classes;

		status = (*jvmti_env)->GetLoadedClasses(jvmti_env, &classCount, &classes);

		if (status != JNI_OK) {
			printf("Failed with error code %d", status);
		}
		
		initializeMsgStructure();

		checkAndTagClasses(jvmti_env, classCount, classes);

		flushMessages();

		initializeMsgStructure();

		status = (*jvmti_env)->FollowReferences(jvmti_env, 0, NULL, NULL, &heapCallBacks, NULL);

		if (status != JNI_OK) {
			printf("Failed with error code %d", status);
		}

		flushMessages();

		fprintf(fp, "%s\n", "*** Completion of Iteration ***");

		Sleep(PAUSE_SECONDS * 1000);
	}
		
}

void JNICALL
vmInitialization
(jvmtiEnv *jvmti_env,
JNIEnv* jni_env,
jthread thread)
{
	jint status;
		
	jclass threadClass = (*jni_env)->FindClass(jni_env, "java/lang/Thread");
	jmethodID threadConstructor = (*jni_env)->GetMethodID(jni_env, threadClass, "<init>", "()V");
	jobject threadObj = (*jni_env)->NewObject(jni_env, threadClass, threadConstructor);

	status = (*jvmti_env)->RunAgentThread(jvmti_env, threadObj, &referenceFollowerInit, NULL, JVMTI_THREAD_MIN_PRIORITY);

	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
	}
}

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
	jvmtiEnv *jvmti;
	jint status;

	initialize();

	status = (*vm)->GetEnv(vm, (void**)&jvmti, JVMTI_VERSION);

	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
		return status;
	}

	jvmtiCapabilities capabilities;
	memset(&capabilities, 0, sizeof(capabilities));
	capabilities.can_tag_objects = 1;
	
	status = (*jvmti)->AddCapabilities(jvmti, &capabilities);
	
	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
		return status;
	}
	
	jvmtiEventCallbacks eventCallbacks;
	jint size = sizeof(eventCallbacks);

	memset(&eventCallbacks, 0, size);
	eventCallbacks.VMInit = &vmInitialization;
	eventCallbacks.VMDeath = &onVMDeath;

	status = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
	
	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
		return status;
	}

	status = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);

	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
		return status;
	}

	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
		return status;
	}

	status = (*jvmti)->SetEventCallbacks(jvmti, &eventCallbacks, size);

	if (status != JNI_OK) {
		printf("Failed with error code %d", status);
		return status;
	}

	return JNI_OK;
}
