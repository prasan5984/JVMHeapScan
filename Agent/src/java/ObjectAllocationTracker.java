public class ObjectAllocationTracker {

	private static int flag = 0;

	public static void trackObject(Object o) {
		if (flag == 1)
			_trackObject(o);
	}

	private static native void _trackObject(Object o);

}
