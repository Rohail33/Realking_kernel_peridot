############ UAPI ############
UAPI_DIR :=	uapi/audio
UAPI_INC :=	-I$(AUDIO_ROOT)/include/$(UAPI_DIR)

############ COMMON ############
COMMON_DIR :=	include
COMMON_INC :=	-I$(AUDIO_ROOT)/$(COMMON_DIR)
AW_INC    :=   -I$(AUDIO_ROOT)/asoc/codecs/aw882xx/inc

# for AW882XX Amplifer
ifdef CONFIG_SND_SOC_AW882XX
	AW882XX_OBJS += src/aw882xx.o
	AW882XX_OBJS += src/aw882xx_bin_parse.o
	AW882XX_OBJS += src/aw882xx_calib.o
	AW882XX_OBJS += src/aw882xx_device.o
	AW882XX_OBJS += src/aw882xx_dsp.o
	AW882XX_OBJS += src/aw882xx_init.o
	AW882XX_OBJS += src/aw882xx_monitor.o
	AW882XX_OBJS += src/aw882xx_spin.o
endif

LINUX_INC += -Iinclude/linux

INCS +=	$(COMMON_INC) \
	$(UAPI_INC) \
	$(AW_INC)

EXTRA_CFLAGS += $(INCS)

CDEFINES +=	-DANI_LITTLE_BYTE_ENDIAN \
		-DANI_LITTLE_BIT_ENDIAN \
		-DDOT11F_LITTLE_ENDIAN_HOST \
		-DANI_COMPILER_TYPE_GCC \
		-DANI_OS_TYPE_ANDROID=6 \
		-DPTT_SOCK_SVC_ENABLE \
		-Wall\
		-Werror\
		-D__linux__ \
		-DDEBUG

ifneq (,$(filter $(TARGET_PRODUCT), ziyi))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_ZIYI
endif


KBUILD_CPPFLAGS += $(CDEFINES)

# Currently, for versions of gcc which support it, the kernel Makefile
# is disabling the maybe-uninitialized warning.  Re-enable it for the
# AUDIO driver.  Note that we must use EXTRA_CFLAGS here so that it
# will override the kernel settings.
ifeq ($(call cc-option-yn, -Wmaybe-uninitialized),y)
EXTRA_CFLAGS += -Wmaybe-uninitialized
endif
#EXTRA_CFLAGS += -Wmissing-prototypes

ifeq ($(call cc-option-yn, -Wheader-guard),y)
EXTRA_CFLAGS += -Wheader-guard
endif

ifeq ($(KERNEL_BUILD), 0)
KBUILD_EXTRA_SYMBOLS +=$(OUT)/obj/vendor/qcom/opensource/audio-kernel/ipc/Module.symvers
KBUILD_EXTRA_SYMBOLS +=$(OUT)/obj/vendor/qcom/opensource/audio-kernel/dsp/Module.symvers
KBUILD_EXTRA_SYMBOLS +=$(OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/Module.symvers
KBUILD_EXTRA_SYMBOLS +=$(OUT)/obj/vendor/qcom/opensource/audio-kernel/asoc/codecs/Module.symvers
KBUILD_EXTRA_SYMBOLS +=$(OUT)/obj/vendor/qcom/opensource/audio-kernel/soc/Module.symvers
endif

# Module information used by KBuild framework
obj-$(CONFIG_SND_SOC_AW882XX)    += aw882xx_dlkm.o
aw882xx_dlkm-y := $(AW882XX_OBJS)
# inject some build related information
DEFINES += -DBUILD_TIMESTAMP=\"$(shell date -u +'%Y-%m-%dT%H:%M:%SZ')\"
