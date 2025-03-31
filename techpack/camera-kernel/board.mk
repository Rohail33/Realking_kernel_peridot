# Build camera kernel driver
CAMERA_DLKM_ENABLED := true
ifeq ($(TARGET_KERNEL_DLKM_DISABLE), true)
	ifeq ($(TARGET_KERNEL_DLKM_CAMERA_OVERRIDE), false)
		CAMERA_DLKM_ENABLED := false;
	endif
endif

ifeq ($(CAMERA_DLKM_ENABLED),true)
ifneq ($(TARGET_BOARD_AUTO),true)
ifeq ($(call is-board-platform-in-list,$(TARGET_BOARD_PLATFORM)),true)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/camera.ko
# MIUI ADD: Camera_CameraSkyNet
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/cameralog.ko
# END Camera_CameraSkyNet

# MIUI ADD: Camera_CamSched
ifeq ($(call get-miodm-device-name), aurora)
BOARD_VENDOR_KERNEL_MODULES += $(KERNEL_MODULES_OUT)/mi_cam.ko
endif
# END Camera_CamSched
endif
endif
endif
