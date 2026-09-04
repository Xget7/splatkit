# JNI calls NativeEngine.onNativeEvent by name; R8 must keep it and the native methods.
-keepclassmembers class com.splatkit.NativeEngine {
    private void onNativeEvent(int, java.lang.String, int);
    native <methods>;
}
