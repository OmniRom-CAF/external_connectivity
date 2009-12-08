LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

# measurements show that the ARM version of ZLib is about x1.17 faster
# than the thumb one...
LOCAL_ARM_MODE := arm

#this is needed to make sure that the path for stlport is specified first than stdc++
LOCAL_NO_DEFAULT_COMPILER_FLAGS :=true

LOCAL_SRC_FILES:= \
       CRefCne.cpp\
       CRefCneRadio.cpp\
       CneSvc.cpp\
       
LOCAL_MODULE:= librefcne

LOCAL_C_INCLUDES := \
	external/connectivity/reference-cne/inc \
	external/connectivity/include/cne \
        bionic/libstdc++/include \
        system/core/include  \
        hardware/libhardware/include \
        hardware/libhardware_legacy/include \
        hardware/ril/include \
        dalvik/libnativehelper/include \
        frameworks/base/include \
        external/skia/include  \
        out/target/product/dream/obj/include \
        bionic/libc/arch-arm/include \
        bionic/libc/include \
        bionic/libstdc++/include \
        bionic/libc/kernel/common \
        bionic/libc/kernel/arch-arm \
        bionic/libm/include \
        bionic/libm/include/arch/arm \
        bionic/libthread_db/include \
        out/target/product/dream/obj/EXECUTABLES/cnetest_intermediates

LOCAL_CFLAGS+= -fno-exceptions -Wno-multichar -msoft-float -fpic -ffunction-sections -funwind-tables -fstack-protector -fno-short-enums -march=armv5te -mtune=xscale  -D__ARM_ARCH_5__ -D__ARM_ARCH_5T__ -D__ARM_ARCH_5TE__ -include system/core/include/arch/linux-arm/AndroidConfig.h -I system/core/include/arch/linux-arm/ -mthumb-interwork -DANDROID -fmessage-length=0 -W -Wall -Wno-unused -DSK_RELEASE -DNDEBUG -O2 -g -Wstrict-aliasing=2 -finline-functions -fno-inline-functions-called-once -fgcse-after-reload -frerun-cse-after-loop -frename-registers -DNDEBUG -UDEBUG -fvisibility-inlines-hidden   -fomit-frame-pointer -fstrict-aliasing -funswitch-loops -finline-limit=300  -fno-rtti -DFEATURE_XMLLIB

LOCAL_PRELINK_MODULE := false

include $(BUILD_SHARED_LIBRARY)

