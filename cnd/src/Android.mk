# For cnd binary
# =======================
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

#this is needed to make sure that the path for stlport is specified before stdc++
LOCAL_NO_DEFAULT_COMPILER_FLAGS :=true

LOCAL_SRC_FILES:= \
        cnd.c \
        cnd_event.cpp \
        cnd_process.cpp \
        cnd_iproute2.cpp

LOCAL_MODULE:= cnd

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libhardware_legacy \
        libcne

LOCAL_C_INCLUDES := \
        external/connectivity/cnd/inc  \
        external/connectivity/include/cne \
        external/STLport-5.2.1/stlport \
        system/core/include \
        frameworks/base/include \
        bionic/libc/arch-arm/include \
        bionic/libc/include \
        bionic/libstdc++/include \
        bionic/libc/kernel/common \
        bionic/libc/kernel/arch-arm \
        bionic/libm/include

LOCAL_CFLAGS+= -fno-exceptions -fno-short-enums -include system/core/include/arch/linux-arm/AndroidConfig.h -DANDROID

include $(BUILD_EXECUTABLE)
