LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := \
	jcapimin.c jcapistd.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c \
	jcinit.c jcmainct.c jcmarker.c jcmaster.c jcomapi.c jcparam.c \
	jcphuff.c jcprepct.c jcsample.c jctrans.c jdapimin.c jdapistd.c \
	jdatadst.c jdatasrc.c jdcoefct.c jdcolor.c jddctmgr.c jdhuff.c \
	jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdphuff.c \
	jdpostct.c jdsample.c jdtrans.c jerror.c jfdctflt.c jfdctfst.c \
	jfdctint.c jidctflt.c jidctfst.c jidctint.c jidctred.c jquant1.c \
	jquant2.c jutils.c jmemmgr.c armv6_idct.S

LOCAL_C_INCLUDES := \
        $(TARGET_OUT_HEADERS)/ipp

# use ashmem as libjpeg decoder's backing store
LOCAL_CFLAGS += -DUSE_ANDROID_ASHMEM
LOCAL_SRC_FILES += \
	jmem-ashmem.c

# the original android memory manager.
# use sdcard as libjpeg decoder's backing store
#LOCAL_SRC_FILES += \
#	jmem-android.c

LOCAL_CFLAGS += -DAVOID_TABLES 
LOCAL_CFLAGS += -O3 -fstrict-aliasing -fprefetch-loop-arrays
#LOCAL_CFLAGS += -march=armv6j

# enable tile based decode
LOCAL_CFLAGS += -DANDROID_TILE_BASED_DECODE

ifeq ($(TARGET_ARCH_VARIANT),x86-atom)
LOCAL_CFLAGS += -DANDROID_INTELSSE2_IDCT
LOCAL_SRC_FILES += jidctintelsse.c
else
# enable armv6 idct assembly
LOCAL_CFLAGS += -DANDROID_ARMV6_IDCT
endif

# enable encoding IPP optimization
LOCAL_CFLAGS += -DIPP_ENCODE

LOCAL_MODULE:= libjpeg

LOCAL_SHARED_LIBRARIES := \
	libcutils

LOCAL_STATIC_LIBRARIES := \
        libippj \
        libippi \
        libipps \
        libippcore

# Add source codes for Merrifield
MERRIFIELD_PRODUCT := \
        mrfl_vp \
	mrfl_hvp \
	mrfl_sle
ifneq ($(filter $(TARGET_PRODUCT),$(MERRIFIELD_PRODUCT)),)
LOCAL_SRC_FILES += \
    jd_libva.c

LOCAL_C_INCLUDES += \
       $(TARGET_OUT_HEADERS)/libva \
       $(TARGET_OUT_HEADERS)/libjpeg_hw

LOCAL_SHARED_LIBRARIES += \
        libjpeg_hw

LOCAL_LDLIBS += -lpthread
LOCAL_CFLAGS += -Wno-multichar
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -DUSE_INTEL_JPEGDEC
endif

include $(BUILD_SHARED_LIBRARY)
