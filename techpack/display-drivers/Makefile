# SPDX-License-Identifier: GPL-2.0-only

ifeq ($(DISPLAY_ROOT),)
DISPLAY_ROOT=$(srctree)/techpack/display
endif


ifeq ($(CONFIG_ARCH_PINEAPPLE), y)
LINUXINCLUDE    += -include $(DISPLAY_ROOT)/config/gki_pineappledispconf.h
endif

ifeq ($(CONFIG_ARCH_PINEAPPLE), y)
include $(DISPLAY_ROOT)/config/gki_pineappledisp.conf
endif




LINUXINCLUDE    += \
		   -I$(DISPLAY_ROOT)/include/uapi/display \
		   -I$(DISPLAY_ROOT)/include
USERINCLUDE     += -I$(DISPLAY_ROOT)/include/uapi/display

obj-$(CONFIG_DRM_MSM) += msm/
