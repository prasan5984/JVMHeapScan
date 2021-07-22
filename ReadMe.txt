****************
JVM HEAP SCANNER 
****************
A JVM agent written in 'C' to track the instance count of Java Types in Heap. It interacts with the JVM using JVM Tool Instrumentation (JVM TI) interface.

On JVM Start, `java.lang.Object` is instrumented to call a tracker method in ObjectAllocationTracker. For count of newly intialized instances, the tracker method updates the cout maintained in backend agent through a JNI call. During GC, JVM TI uses call back function to reduce the instance count.

The instance count of the types are logged and also can be accessed through a configured TCP socket.

Additionally, through the socket commands can be passed to trigger a GC or follow the reference of currently active objects. This will be required when the agent connects to an existing JVM.


1. Generate a dll file using the source. Currently the agent only works in windows.

2. Command to run the agent is 
java -agentpath:<Complete Path of the JVM HeapScan.dll>=<JVMHeapScan.properties directory>  <other java command line options>

3. In case if the properties directory is not provided as option in the command line, then the properties file must be present in the current working directory

4. Properties:

	1. Java Tracker Path: For live tracking of objects, Java file ObjectAllocationTracker is required. This path must denote the directory where the class file is present.
	
	2. Default Port: Server socket port
	
	3. Socket Message Limit: The maximum number of messages that can be transmitted at an instance.

5. The java_crw_demo files is used for BCI in this project

***NOTE***: Heavy load and performance testing of the agent is pending.
