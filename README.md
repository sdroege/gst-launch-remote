# Remote-control test app for GStreamer on Android and iOS

## About

To learn more about this application, see
https://coaxion.net/blog/2014/10/gstreamer-remote-controlled-testing-application-for-android-ios-and-more/


## Building on Android

* Open the android-launch/ directory in Android Studio
  * If you downloaded the GStreamer for Android binary, set
    `gstAndroidRoot = /path/to/gst-android-binaries` in `~/.gradle/gradle.properties`
  * If you built GStreamer for Android using Cerbero, you're good to go
* Set up the NDK in Android Studio (under Project Structure, or set ANDROID_NDK_HOME in the environment, or ndk.dir in `local.properties`
* Build the project in Android Studio like any other project
