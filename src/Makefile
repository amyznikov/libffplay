#######################################################################################################################
#
# libffplay Makefile
#   play videos with ffmpeg libs on android surface
#
# A. Myznikov, Sunday 19 October 2014
#
#######################################################################################################################
SHELL = /bin/bash

LIBNAME        = ffplay
VERSION        = 0.0.0

APP_ABI        = armeabi-v7a
APP_PLATFORM   = android-15
NDK_TOOLCHAIN_VERSION = 4.9


DEFINES         += -DFFPLAY_VERSION=\"$(VERSION)\"
HEADERS         += sendvideo.h ffplay-java-api.h pthread_wait.h debug.h ffmpeg.h cclist.h
SOURCES         += sendvideo.c ffplay-java-api.c debug.c ffmpeg.c 
JNIHEADERS      += com_sis_ffplay_CameraPreview.h
JNISOURCES      += com_sis_ffplay_CameraPreview.c

# 
# -mfpu=vfpv3-d16
TARGET_CFLAGS   := -Wextra -O3 -g0 -mthumb -marm -mfpu=neon -march=armv7-a -mfloat-abi=softfp \
                   -mvectorize-with-neon-quad -fsee \
                   -ffast-math -fassociative-math -fno-signed-zeros -fexpensive-optimizations \
                   -fno-strict-aliasing -T script -ffunction-sections -funwind-tables -fstack-protector \
                   -no-canonical-prefixes 
TARGET_arm_release_CFLAGS  := -DDEBUG -fomit-frame-pointer -fstrict-aliasing -funswitch-loops -finline-limit=300
TARGET_thumb_release_CFLAGS  := -mthumb -DDEBUG -fomit-frame-pointer -fno-strict-aliasing -finline-limit=64


ndk_build := $(shell which ndk-build)
ifeq ($(strip $(ndk_build)),)
$(error Can not locate ndk-build in your PATH. Is NDK installed correctly?)  
endif

android:=$(shell which android)
ifeq ($(strip $(android)),)
$(error Can not locate android in your PATH. Is SDK installed correctly?)  
endif


ndk_root := $(shell dirname $(ndk_build))
sdk_root := $(shell dirname $(shell dirname $(android)))

ifndef APP_PLATFORM
maxplatform=$(shell ls -1 $(sdk_root)/platforms | fgrep android | sed 's/android-//g' | sort -rg  | head -n 1)
APP_PLATFORM = android-$(maxplatform)
endif


classpath:=$(sdk_root)/platforms/$(APP_PLATFORM)/android.jar
ifeq ("$(wildcard $(classpath))","")
$(error ERROR: File $(classpath) not exists. Is APP_PLATFORM set correctly?)  
endif


ifndef prefix
prefix = $(ndk_root)/special-is
endif



includedir = $(prefix)/include
prebuiltdir = $(prefix)/lib/$(LIBNAME)
libinstalldir = $(prebuiltdir)/$(APP_ABI)
javainstalldir = $(prebuiltdir)/$(LIBNAME)
docdir = $(prebuiltdir)/doc

$(prefix)       	: ; mkdir -p $@
$(includedir)   	: ; mkdir -p $@
$(prebuiltdir)  	: ; mkdir -p $@
$(libinstalldir)	: ; mkdir -p $@
$(javainstalldir)	: ; mkdir -p $@
$(docdir)           : ; mkdir -p $@



all: lib


lib: Makefile jni/Android.mk jni/Application.mk \
  $(JNIHEADERS) $(JNISOURCES) .check_jni_signatures   
	ndk-build -j 1 V=1 APP_ABI=$(APP_ABI) NDK_TOOLCHAIN_VERSION=$(NDK_TOOLCHAIN_VERSION) APP_PLATFORM=$(APP_PLATFORM)

.PHONY: jni/Android.mk
jni/Android.mk : Makefile
	mkdir -p jni
	echo 'LOCAL_PATH := $$(call my-dir)/..' > $@
	echo 'include $$(CLEAR_VARS)' >> $@
	echo 'LOCAL_MODULE    := $(LIBNAME)' >> $@
	echo 'SPECIALISDIR    := $(ndk_root)/special-is' >> $@
	echo 'INCLUDES        += -I. -I$$(SPECIALISDIR)/include' >> $@
	echo 'LOCAL_SRC_FILES := $(SOURCES) $(JNISOURCES)' >> $@
	echo "TARGET_CFLAGS   := $(TARGET_CFLAGS)" >> $@
	echo "TARGET_arm_release_CFLAGS := $(TARGET_arm_release_CFLAGS)" >> $@
	echo "TARGET_thumb_release_CFLAGS := $(TARGET_thumb_release_CFLAGS)" >> $@
	echo 'LOCAL_CFLAGS    := -std=gnu99 $(DEFINES) $$(INCLUDES) -D__LINUX__ -DEXPORT=""' >> $@
	echo 'LOCAL_CXXFLAGS  := $$(LOCAL_CFLAGS)' >> $@
	echo 'LOCAL_LDLIBS    := $$(SPECIALISDIR)/lib/libavutil.a \
                             $$(SPECIALISDIR)/lib/libavformat.a \
                             $$(SPECIALISDIR)/lib/libswscale.a \
                             $$(SPECIALISDIR)/lib/libavcodec.a \
                             $$(SPECIALISDIR)/lib/libswresample.a \
                             $$(SPECIALISDIR)/lib/libavutil.a \
                             $$(SPECIALISDIR)/lib/libx264.a \
                             -lz -lm -landroid -llog' >> $@
	echo 'include $$(BUILD_SHARED_LIBRARY)' >> $@



.PHONY: jni/Application.mk
jni/Application.mk: Makefile
	mkdir -p jni
	echo 'APP_MODULES           := $(LIBNAME)' 	> $@  
	echo 'APP_PROJECT_PATH      := $$(shell pwd)' >> $@
	echo 'APP_OPTIM             := release' >> $@
	echo 'APP_DEBUGGABLE        := false' >> $@


com_sis_ffplay_CameraPreview.h: com/sis/ffplay/CameraPreview.class
	@echo "BUILD $@"
	javah -o $@ -classpath $(classpath):. com.sis.ffplay.CameraPreview

com/sis/ffplay/CameraPreview.class: com/sis/ffplay/CameraPreview.java
	@echo "BUILD $@"
	javac $^ -classpath $(classpath):.  

.check_jni_signatures: $(JNIHEADERS)
	@echo "BUILD $@"
	@for f in $(shell egrep -oh 'Java_com_sis_ffplay_CameraPreview_\S+' com_sis_ffplay_CameraPreview.h ) ;\
	do \
	 echo $$f ; \
     if ! fgrep -qw $$f com_sis_ffplay_CameraPreview.c ; \
     then \
       echo "POTENTIAL BUG: $$f() is not implemented in com_sis_ffplay_CameraPreview.c. Fix the code and try again"; \
       exit 1;\
     fi; \
     done


clean:
	rm -rf obj jni com/sis/ffplay/*.class

distclean: clean
	rm -rf libs


install: $(prebuiltdir) $(libinstalldir) $(javainstalldir) $(docdir)
	cp libs/$(APP_ABI)/lib$(LIBNAME).so $(libinstalldir) || exit 1
	cp -r com $(javainstalldir) && find $(javainstalldir) -name '*.class' -delete || exit 1
	echo 'LOCAL_PATH := $$(call my-dir)' > $(prebuiltdir)/Android.mk 
	echo 'include $$(CLEAR_VARS)' >> $(prebuiltdir)/Android.mk
	echo 'LOCAL_MODULE := $(LIBNAME)' >> $(prebuiltdir)/Android.mk
	echo 'LOCAL_SRC_FILES := $$(APP_ABI)/lib$(LIBNAME).so' >> $(prebuiltdir)/Android.mk
	echo 'include $$(PREBUILT_SHARED_LIBRARY)' >> $(prebuiltdir)/Android.mk

uninstall:
	rm -rf $(prebuiltdir) $(libinstalldir) $(javainstalldir)
