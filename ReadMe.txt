****************
JVM HEAP SCANNER -- (Currently this agent is a Work in Progress)
****************

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