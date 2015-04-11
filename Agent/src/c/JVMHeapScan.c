#include <JVMHeapScan.h>
#include <java_crw_demo.h>

static int lastTag = 1;
static jvmtiEnv *local_jvmti_env;
static jrawMonitorID rawMonitorId;

static MessageStore msgStore;
static OutputInterface opInterface;
static jvmtiHeapCallbacks heapCallBacks;

static int heapRefDtlSize, objectTagSize, freeTagSize;
static char* jarPath;

void* allocateMemory(int size)
{
	return malloc(size);
}

int getChars(jlong value)
{
	int chars = 1;

	if (value < 0)
	{
		chars++;
		value = value*-1;
	}

	for (; value > 9; value = value / 10)
		chars++;

	return chars;
}

char* createString(int size)
{
	char* str = allocateMemory(size);
	ZeroMemory(str, size);
	return str;
}

void calculateSize()
{
	int heapMsgCodeLength = getChars(HEAP_REF_INFO);
	int objectTagMsgCodeLength = getChars(OBJECT_TAG);
	int freeTagMsgCodeLength = getChars(DEALLOCATE_OBJECT);

	heapRefDtlSize = heapMsgCodeLength + sizeof(char) * 8 + 1;
	objectTagSize = objectTagMsgCodeLength + 2 * sizeof(char) + 1;
	freeTagSize = freeTagMsgCodeLength + 1 * sizeof(char) + 1;
}

void onFatalError(char* errorMsg)
{
	printf("Fatal error encountered in JVM Heap Scan Agent: %s", errorMsg);
	exit(3);
}

void onCheckJVMTIError(jvmtiError errorCode, char* functionName)
{
	if (errorCode != JVMTI_ERROR_NONE)
	{
		jvmtiError curErr;
		char* agentErrMsg;
		curErr = (*local_jvmti_env)->GetErrorName(local_jvmti_env, errorCode, &agentErrMsg);
		char* newErrorMsg = createString(strlen(agentErrMsg) + strlen(functionName) + 2 + 50);

		if (curErr != JVMTI_ERROR_NONE)
			sprintf(newErrorMsg, "Error %s. Unable to fetch error details", functionName);
		else
			sprintf(newErrorMsg, "Error encountered %s. Error Reference: %s", functionName, agentErrMsg);

		onFatalError(newErrorMsg);
	}
}

void StoreObjectTag(jlong objectTag, jlong classTag, boolean flushFl)
{
	int totalSz = objectTagSize + getChars(objectTag) + getChars(classTag);
	char* objectTagStr = createString(totalSz);
	sprintf(objectTagStr, "%d%c%ld%c%ld", (int)OBJECT_TAG, MSGDELIMITER, (long)objectTag, MSGDELIMITER, (long)classTag);
	msgStore.includeMessage(objectTagStr, totalSz, flushFl);
}

void StoreClassTagMsg(jlong tag, char* classSign, boolean flushFl)
{
	int classDtlSize = getChars(CLASS_TAG) + (strlen(classSign) + getChars(tag) + 1 + 2 /*for '\0'*/) * sizeof(char);
	char* classDtlStr = createString(classDtlSize);
	sprintf(classDtlStr, "%d%c%s%c%ld", CLASS_TAG, MSGDELIMITER, classSign, MSGDELIMITER, tag);
	msgStore.includeMessage(classDtlStr, classDtlSize, flushFl);
}

void StoreClassTag(jclass class, jlong tag, boolean flushFl)
{
	static char* fName = "";
	jvmtiError errorCode;
	char* classSign;
	errorCode = (*local_jvmti_env)->GetClassSignature(local_jvmti_env, class, &classSign, NULL);
	onCheckJVMTIError(errorCode, fName);
	StoreClassTagMsg(tag, classSign, flushFl);
}

void checkAndTagClasses(jvmtiEnv* jvmti_env, jint classCount, jclass* classes, boolean flushFl)
{
	static char* fName = "on saving loaded class details";
	int  i;
	jvmtiError errorCode;
	for (i = 0; i < classCount; i++)
	{
		jlong tag_ptr = 0;
		errorCode = (*jvmti_env)->GetTag(jvmti_env, *classes, &tag_ptr);
		onCheckJVMTIError(errorCode, fName);

		if (tag_ptr == 0)
		{
			errorCode = (*jvmti_env)->SetTag(jvmti_env, *classes, lastTag);
			onCheckJVMTIError(errorCode, fName);
			lastTag++;
		}

		StoreClassTag(*classes, tag_ptr, flushFl);
		classes++;
	}
}

jint JNICALL
followReferenceCallback(jvmtiHeapReferenceKind reference_kind, const jvmtiHeapReferenceInfo* reference_info, jlong class_tag, jlong referrer_class_tag, jlong size, jlong* tag_ptr, jlong* referrer_tag_ptr, jint length, void* user_data)
{
	int index = -1;
	if (reference_info != NULL)
	{
		switch (reference_kind)
		{
		case JVMTI_HEAP_REFERENCE_ARRAY_ELEMENT:
			index = ((jvmtiHeapReferenceInfoArray*)reference_info)->index;
			break;
		case JVMTI_HEAP_REFERENCE_FIELD | JVMTI_HEAP_REFERENCE_STATIC_FIELD:
			index = ((jvmtiHeapReferenceInfoField*)reference_info)->index;
			break;
		case JVMTI_HEAP_REFERENCE_CONSTANT_POOL:
			index = ((jvmtiHeapReferenceInfoConstantPool*)reference_info)->index;
			break;
		default:
			break;
		}
	}

	if (*tag_ptr == 0 && class_tag != 0)
	{
		*tag_ptr = lastTag++;
		StoreObjectTag(*tag_ptr, class_tag, FALSE);
	}

	if (referrer_tag_ptr != NULL && *referrer_tag_ptr == 0 && referrer_class_tag != 0)
	{
		*referrer_tag_ptr = lastTag++;
		StoreObjectTag(*referrer_tag_ptr, referrer_class_tag, FALSE);
	}

	jlong referrerTag = 0;
	if (referrer_tag_ptr != NULL)
		referrerTag = *referrer_tag_ptr;

	int totalSz = heapRefDtlSize + getChars(reference_kind) + getChars(index) + getChars(class_tag) + getChars(referrer_class_tag)
		+ getChars(*tag_ptr) + getChars(referrerTag) + getChars(length) + getChars(size);

	char* heapRefDtlStr = createString(totalSz);

	sprintf(heapRefDtlStr, "%d%c%d%c%d%c%ld%c%ld%c%ld%c%ld%c%d%c%ld", HEAP_REF_INFO, MSGDELIMITER, (int)reference_kind, MSGDELIMITER,
		index, MSGDELIMITER, (long)class_tag, MSGDELIMITER, (long)referrer_class_tag, MSGDELIMITER, (long)*tag_ptr, MSGDELIMITER,
		(long)referrerTag, MSGDELIMITER, (int)length, MSGDELIMITER, (long)size);

	msgStore.includeMessage(heapRefDtlStr, totalSz, FALSE);

	return JVMTI_VISIT_OBJECTS;

}

void onGetClassMetadata()
{
	jvmtiError errCode;
	static char* fName = "on getting class information";

	jint classCount;
	jclass* classes;

	errCode = (*local_jvmti_env)->RawMonitorEnter(local_jvmti_env, rawMonitorId);
	onCheckJVMTIError(errCode, fName);

	errCode = (*local_jvmti_env)->GetLoadedClasses(local_jvmti_env, &classCount, &classes);
	onCheckJVMTIError(errCode, fName);

	msgStore.startMsgGrp();
	checkAndTagClasses(local_jvmti_env, classCount, classes, FALSE);
	msgStore.flushMsgGrp();

	errCode = (*local_jvmti_env)->RawMonitorExit(local_jvmti_env, rawMonitorId);
	onCheckJVMTIError(errCode, fName);
}

void onFollowReference()
{
	jvmtiError errCode;
	msgStore.startMsgGrp();
	errCode = (*local_jvmti_env)->FollowReferences(local_jvmti_env, 0, NULL, NULL, &heapCallBacks, NULL);
	onCheckJVMTIError(errCode, "on iterating over heap");
	msgStore.flushMsgGrp();
}

void onStartGC()
{
	jvmtiError errCode;
	errCode = (*local_jvmti_env)->ForceGarbageCollection(local_jvmti_env);
	onCheckJVMTIError(errCode, "on initiating GC");
}

void JNICALL onObjectFree(jvmtiEnv *jvmtiEnv, jlong tag)
{
	int tagSz = getChars(tag) + freeTagSize;
	char *tagStr = (char *)createString(tagSz);
	sprintf(tagStr, "%d%c%ld", (int)DEALLOCATE_OBJECT, MSGDELIMITER, tag);
	msgStore.includeMessage(tagStr, tagSz, TRUE);
}

void JNICALL
referenceFollowerInit(jvmtiEnv* jvmti_env, JNIEnv* jni_env, void* arg)
{
	jint size = sizeof(heapCallBacks);
	memset(&heapCallBacks, 0, size);
	heapCallBacks.heap_reference_callback = &followReferenceCallback;
	
	onGetClassMetadata();
	onFollowReference();
	opInterface.start();
}

void JNICALL
onClassFileLoad(jvmtiEnv *jvmti_env,
JNIEnv* jni_env,
jclass class_being_redefined,
jobject loader,
const char* name,
jobject protection_domain,
jint class_data_len,
const unsigned char* class_data,
jint* new_class_data_len,
unsigned char** new_class_data)
{
	static char* fName = "while instrumenting class";
	jvmtiError errCode;
	errCode = (*jvmti_env)->RawMonitorEnter(jvmti_env, rawMonitorId);
	onCheckJVMTIError(errCode, fName);

	if (strcmp(name, "java/lang/Object") == 0)
	{
		unsigned class_number = 0;
		unsigned system_class = 0;

		unsigned char* temp_class_data = NULL;
		jint temp_class_length = 0;

		java_crw_demo
			(class_number,
			name,
			class_data,
			class_data_len,
			system_class,
			"ObjectAllocationTracker",
			"LObjectAllocationTracker;",
			NULL,
			NULL,
			NULL,
			NULL,
			"trackObject",
			"(Ljava/lang/Object;)V",
			NULL,
			NULL,
			&temp_class_data,
			&temp_class_length,
			NULL,
			NULL);

		void* localClassData;

		errCode = (*jvmti_env)->Allocate(jvmti_env, temp_class_length, (unsigned char **)&localClassData);
		onCheckJVMTIError(errCode, fName);
		(void)memcpy((void *)localClassData, (void *)temp_class_data, (int)temp_class_length);
		*new_class_data = (unsigned char *)localClassData;
		*new_class_data_len = temp_class_length;
		(void)free((void*)temp_class_data);
	}
	errCode = (*jvmti_env)->RawMonitorExit(jvmti_env, rawMonitorId);
	onCheckJVMTIError(errCode, fName);
}

void JNICALL
vmInitialization
(jvmtiEnv *jvmti_env,
JNIEnv* jni_env,
jthread thread)
{
	static char* fName = "during initialization";
	jvmtiError errCode;

	jclass threadClass = (*jni_env)->FindClass(jni_env, "java/lang/Thread");
	jmethodID threadConstructor = (*jni_env)->GetMethodID(jni_env, threadClass, "<init>", "()V");
	jobject threadObj = (*jni_env)->NewObject(jni_env, threadClass, threadConstructor);

	errCode = (*jvmti_env)->RunAgentThread(jvmti_env, threadObj, &referenceFollowerInit, NULL, JVMTI_THREAD_MIN_PRIORITY);
	onCheckJVMTIError(errCode, fName);
}

jlong setAndGetTag(jobject object, boolean* tagSetFl)
{
	static char* fName = "while tagging objects";
	jvmtiError errCode;

	jlong tag;
	*tagSetFl = 0;
	errCode = (*local_jvmti_env)->GetTag(local_jvmti_env, object, &tag);
	onCheckJVMTIError(errCode, fName);

	if (tag != 0)
		return tag;

	lastTag++;
	errCode = (*local_jvmti_env)->SetTag(local_jvmti_env, object, lastTag);
	onCheckJVMTIError(errCode, fName);
	*tagSetFl = 1;

	return lastTag;
}

void GetObjectTag(jclass class, jobject object, boolean flushFl)
{
	boolean tagSetFl;
	jlong classTag;

	classTag = setAndGetTag(class, &tagSetFl);

	if (tagSetFl)
	{
		StoreClassTag(class, classTag, flushFl);
		tagSetFl = 0;
	}
	jlong objectTag = setAndGetTag(object, &tagSetFl);

	if (tagSetFl)
		StoreObjectTag(objectTag, classTag, flushFl);
}

JNIEXPORT void JNICALL Java_ObjectAllocationTracker__1trackObject
(JNIEnv* jni_env, jclass class, jobject allocatedObj)
{
	jclass allocatedClass = (*jni_env)->GetObjectClass(jni_env, allocatedObj);
	GetObjectTag(allocatedClass, allocatedObj, TRUE);
}

void JNICALL onVMStart
(jvmtiEnv *jvmti_env,
JNIEnv* jni_env)
{
	static char* fName = "while initializing tracker class";
	jvmtiError errCode;

	errCode = (*jvmti_env)->RawMonitorEnter(jvmti_env, rawMonitorId);
	onCheckJVMTIError(errCode, fName);
	jclass trackerClass = (*jni_env)->FindClass(jni_env, "ObjectAllocationTracker");

	jint status;

	JNINativeMethod nativeMethod;
	nativeMethod.name = "_trackObject";
	nativeMethod.signature = "(Ljava/lang/Object;)V";
	nativeMethod.fnPtr = (void *)&Java_ObjectAllocationTracker__1trackObject;
	status = (*jni_env)->RegisterNatives(jni_env, trackerClass, &nativeMethod, 1);
	jfieldID fieldId = (*jni_env)->GetStaticFieldID(jni_env, trackerClass, "flag", "I");
	int value = 1;
	(*jni_env)->SetStaticIntField(jni_env, trackerClass, fieldId, value);
	errCode = (*jvmti_env)->RawMonitorExit(jvmti_env, rawMonitorId);
	onCheckJVMTIError(errCode, fName);
}

void JNICALL
onVMDeath(jvmtiEnv *jvmti_env,
JNIEnv* jni_env)
{
	//opInterface.onKillThread();
}

static void onInitialize(JvmHeapScanInterfaces intfs)
{
	calculateSize();
	jarPath = intfs.agentOptions.trackerPath;
	msgStore = intfs.msgStore;
	opInterface = intfs.opInterface;
}

JVMFunctions getJVMInterface()
{
	JVMFunctions* functions = (JVMFunctions*)allocateMemory(sizeof(JVMFunctions));
	functions->initialize = onInitialize;
	functions->followReference = onFollowReference;
	functions->startGC = onStartGC;
	functions->onFatalError = onFatalError;
	functions->onGetClassMetadata = onGetClassMetadata;
	return *functions;
}

AgentOptions onSetOptions(char* options)
{
	static char outputPath[260], socketPort[6];
	static int socketMsgLimit;
	static char errMessage[] = "Could not find properties file";

	char *propertiesFilePath, propertiesFileFN[260];

	if (options != NULL)
	{
		propertiesFilePath = options;
		sprintf(propertiesFileFN, "%s\\%s", propertiesFilePath, PROPERTIES_FILE_NAME);
	}
	else
		sprintf(propertiesFileFN, ".\\%s", PROPERTIES_FILE_NAME);

	FILE *propertiesFile = fopen(propertiesFileFN, "r");

	if (propertiesFile == NULL)
	{
		char* totErrMsg = (char *)allocateMemory(strlen(errMessage) + strlen(propertiesFileFN) + 5);
		sprintf(totErrMsg, "%s. '%s'", errMessage, propertiesFileFN);
		onFatalError(totErrMsg);
	}

	fscanf(propertiesFile, "Java Tracker Path:%s\n", outputPath);
	fscanf(propertiesFile, "Default Port:%s\n", socketPort);
	fscanf(propertiesFile, "Socket Message Limit:%d\n", &socketMsgLimit);
	fclose(propertiesFile);

	AgentOptions* agentOptions = (AgentOptions*)allocateMemory(sizeof(AgentOptions));
	agentOptions->port = socketPort;
	agentOptions->trackerPath = outputPath;
	agentOptions->socketMsgLimit = socketMsgLimit;

	return *agentOptions;
}

void onInitializeAgent(char* options)
{
	AgentOptions agentOptions = onSetOptions(options);
	JvmHeapScanInterfaces* interfaces = (JvmHeapScanInterfaces *)allocateMemory(sizeof(JvmHeapScanInterfaces));

	MessageStore msgStore = getMsgStoreInterface();
	OutputInterface opInterface = getOutputInterface();
	JVMFunctions function = getJVMInterface();
	SynchronizerInterface syncInterface = getSynchronizerInterface();

	interfaces->agentOptions = agentOptions;
	interfaces->msgStore = msgStore;
	interfaces->opInterface = opInterface;
	interfaces->jvmFunctions = function;
	interfaces->syncInterface = syncInterface;

	msgStore.initialize(*interfaces);
	opInterface.initialize(*interfaces);
	function.initialize(*interfaces);
}

JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM *vm, char *options, void *reserved)
{
	static char* fName = "on loading agent";
	jvmtiError errCode;
	jvmtiEnv *jvmti;

	errCode = (*vm)->GetEnv(vm, (void**)&jvmti, JVMTI_VERSION);
	onCheckJVMTIError(errCode, fName);

	local_jvmti_env = jvmti;
	onInitializeAgent(options);

	jvmtiCapabilities capabilities;
	memset(&capabilities, 0, sizeof(capabilities));
	capabilities.can_tag_objects = 1;
	capabilities.can_generate_all_class_hook_events = 1;
	capabilities.can_generate_object_free_events = 1;

	errCode = (*jvmti)->AddCapabilities(jvmti, &capabilities);
	onCheckJVMTIError(errCode, fName);

	errCode = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
	onCheckJVMTIError(errCode, fName);

	errCode = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);
	onCheckJVMTIError(errCode, fName);

	errCode = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_VM_START, NULL);
	onCheckJVMTIError(errCode, fName);

	errCode = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
	onCheckJVMTIError(errCode, fName);

	errCode = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_OBJECT_FREE, NULL);
	onCheckJVMTIError(errCode, fName);

	jvmtiEventCallbacks eventCallbacks;
	jint size = sizeof(eventCallbacks);

	memset(&eventCallbacks, 0, size);
	eventCallbacks.VMInit = &vmInitialization;
	eventCallbacks.VMDeath = &onVMDeath;
	eventCallbacks.VMStart = &onVMStart;
	eventCallbacks.ObjectFree = &onObjectFree;
	eventCallbacks.ClassFileLoadHook = &onClassFileLoad;

	errCode = (*jvmti)->SetEventCallbacks(jvmti, &eventCallbacks, size);
	onCheckJVMTIError(errCode, fName);

	errCode = (*jvmti)->AddToBootstrapClassLoaderSearch(jvmti, jarPath);
	onCheckJVMTIError(errCode, fName);

	static const char monitorName[] = "monitor1";

	errCode = (*jvmti)->CreateRawMonitor(jvmti, monitorName, &rawMonitorId);
	onCheckJVMTIError(errCode, fName);
	return JNI_OK;
}

JNIEXPORT void JNICALL Agent_OnUnload(JavaVM *vm)
{
	opInterface.onKillThread();
}




