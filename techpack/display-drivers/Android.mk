# Android makefile for display kernel modules
ifneq (, $(filter $(call get-component-name), miodm))
DISPLAY_DLKM_ENABLE := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_DISPLAY_OVERRIDE), false)
		DISPLAY_DLKM_ENABLE := false
	endif
endif

ifeq ($(DISPLAY_DLKM_ENABLE),  true)
ifeq ($(call get-miodm-device-name), suiren)
	LOCAL_PATH := $(call my-dir)
	include $(LOCAL_PATH)/display_drivers_auto/msm/Android.mk
else
	LOCAL_PATH := $(call my-dir)
	include $(LOCAL_PATH)/msm/Android.mk
endif
endif
endif
