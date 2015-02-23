****************
JVM HEAP SCANNER
****************

1. Generate a dll file using the source. Currently the agent only works in windows.

2. Command to run the agent is 
java -agentpath:<Complete Path of the JVM HeapScan.dll>  <other java command line options>

3. Ensure the directory of JVMHeapScan.properties file is included in the PATH variable.