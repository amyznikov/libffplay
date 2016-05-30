# libffplay

Use (old) Android Camera API and ffmpeg libs to send audio/video streams from Android device to media server.


### Build Order:

1. Get sources

  $ git clone https://github.com/amyznikov/libffplay.git
 
2. Enter root directoty

  $ cd libffplay/

3. Get external submodules
  
  $ git submodule init && git submodule update
  
4. Build submodules 
   
  It is mandatory to run the script 'build-modules' from inside of libffplay/external directory, 
  because of it uses relative path names:
   
  $ cd external && ./build-modules
   
  By default this script will build libraries for android-15 platform (Android 4.0.3 devices).
  To change target platfom, open the script in a text editor and change target platform as desired.
   
  After successfull build, prebuilt module binaries and headers will installed into libffplay/external/sysroot directory.
   
5. Now build libffplay main dynamic library
  
  $ cd libffplay/src
  
  $ make all
  
  By default, target platform is android-15 platform (Android 4.0.3 devices).
  To change target platform, open Makefile in text editor and set desired platform in APP_PLATFORM= variable.
  
  Warning: Make sure the submodules and libffplay.so are set to build for the same android platform!
    
  
  
6. Install libffplay into some location where application projects can find it.
  
  By default, `make install` will install prebuilt shared library into NDK folder  $(ndk_root)/special-is/lib/ffplay.
  To change target folder, pass prefix=/your/path/here to `make install` command:
  
  $ make install prefix=/my/preferable/path
  
  Prebuilt library directory structure is as follows:
  
```
$ tree /opt/android-ndk/special-is/lib/ffplay
/opt/android-ndk/special-is/lib/ffplay
├── Android.mk
├── armeabi-v7a
│   └── libffplay.so
└── ffplay
    └── com
        └── sis
            └── ffplay
                ├── CameraPreview.java
                └── log.java

```

To use the library in application, add to application project source path to $(ndk_root)/special-is/lib/ffplay/ffplay, 
add NDK support, and import ffplay module in Android.mk, like follows:
```
$ cat /home/projects/CameraDemo/jni/Android.mk 
  LOCAL_PATH := $(call my-dir)
  include $(CLEAR_VARS)
  SPECIALISDIR  := $(ANDROID_NDK)/special-is
  $(call import-add-path,$(SPECIALISDIR)/lib)
  $(call import-module,ffplay)
  
$ cat /home/projects/CameraDemo/jni/Application.mk 
  APP_MODULES           := ffplay
  APP_ABI               := armeabi-v7a
  APP_PLATFORM          := android-15
  APP_OPTIM             := release
  APP_DEBUGGABLE        := false
```


To use libffplay with ant, use custom_rules.xml to import java source directory 
and precompiled binaries into project:

```
$ cat custom_rules.xml
<?xml version="1.0" encoding="UTF-8"?>
<project>

  <property environment="env" />
  <property name="specialis.dir" value="${env.ANDROID_NDK}/special-is"/>
  <property name="source.dir" value="src;${specialis.dir}/lib/ffplay/ffplay"/>

  <target name="-pre-build">
    <copy todir="${jar.libs.dir}/armeabi-v7a">
     <fileset dir="${specialis.dir}/lib/ffplay/armeabi-v7a" includes="*.so" />
    </copy>
  </target>
</project>
```


See https://github.com/amyznikov/CameraDemo as some simple example.

