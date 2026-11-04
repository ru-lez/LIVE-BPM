# Keep JNI entry points — the native library calls back into these by name.
-keepclasseswithmembernames class * {
    native <methods>;
}
-keep class com.livebpm.android.NativeBridge { *; }
