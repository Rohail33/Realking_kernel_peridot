

# As per target team, build is done as follows:
# Defconfig : build with default flags
# Slub      : defconfig  + CONFIG_SLUB_DEBUG := y +
#	      CONFIG_SLUB_DEBUG_ON := y + CONFIG_PAGE_POISONING := y
# Perf      : Using appropriate msmXXXX-perf_defconfig
#
# Shipment builds (user variants) should not have any debug feature
# enabled. This is identified using 'TARGET_BUILD_VARIANT'. Slub builds
# are identified using the CONFIG_SLUB_DEBUG_ON configuration. Since
# there is no other way to identify defconfig builds, QTI internal
# representation of perf builds (identified using the string 'perf'),
# is used to identify if the build is a slub or defconfig one. This
# way no critical debug feature will be enabled for perf and shipment
# builds. Other OEMs are also protected using the TARGET_BUILD_VARIANT
# config.

############ UAPI ############
UAPI_DIR :=	uapi/audio/
UAPI_INC :=	-I$(AUDIO_ROOT)/include/$(UAPI_DIR)

############ COMMON ############
COMMON_DIR :=	include
COMMON_INC :=	-I$(AUDIO_ROOT)/$(COMMON_DIR)

############ QDSP6V2 ############

ifdef CONFIG_SND_SOC_MSM_QDSP6V2_INTF
	Q6_OBJS += msm-audio-event-notify.o
	Q6_OBJS += q6_init.o
endif

ifdef CONFIG_SND_SOC_MSM_QDSP6V2_VM
	Q6_OBJS += msm-audio-event-notify.o
	Q6_OBJS += msm_audio_ion_vm.o
	Q6_OBJS += q6_init.o
endif

ifdef CONFIG_MSM_AVTIMER
	Q6_OBJS += avtimer.o
endif

ifdef CONFIG_XT_LOGGING
	Q6_OBJS += sp_params.o
endif

ifdef CONFIG_MSM_ADSP_LOADER
ADSP_LOADER_OBJS += adsp-loader.o
endif

ifdef CONFIG_MSM_QDSP6_PDR
QDSP6_PDR_OBJS += audio_pdr.o
endif

ifdef CONFIG_MSM_QDSP6_NOTIFIER
QDSP6_NOTIFIER_OBJS += audio_notifier.o audio_ssr.o
endif

ifdef CONFIG_SPF_CORE
SPF_CORE_OBJS += spf-core.o
endif

ifdef CONFIG_AUDIO_PRM
AUDIO_PRM += audio_prm.o
endif

ifdef CONFIG_AUDIO_PKT_ION
AUDIO_PKT_ION += msm_audio_ion.o
endif

ifdef CONFIG_VOICE_MHI
VOICE_MHI += voice_mhi.o
endif

ifdef CONFIG_DIGITAL_CDC_RSC_MGR
	SPF_CORE_OBJS += digital-cdc-rsc-mgr.o
endif


LINUX_INC +=	-Iinclude/linux

INCS +=		$(COMMON_INC) \
		$(UAPI_INC)

EXTRA_CFLAGS += $(INCS)


CDEFINES +=	-DANI_LITTLE_BYTE_ENDIAN \
		-DANI_LITTLE_BIT_ENDIAN \
		-DDOT11F_LITTLE_ENDIAN_HOST \
		-DANI_COMPILER_TYPE_GCC \
		-DANI_OS_TYPE_ANDROID=6 \
		-DPTT_SOCK_SVC_ENABLE \
		-Wall\
		-Werror\
		-D__linux__

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), chenfeng))
	CDEFINES += -DAUDIO_SILENT_OBSERVER
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

# If the module name is not "wlan", then the define MULTI_IF_NAME to be the
# same a the QCA CHIP name. The host driver will then append MULTI_IF_NAME to
# any string that must be unique for all instances of the driver on the system.
# This allows multiple instances of the driver with different module names.
# If the module name is wlan, leave MULTI_IF_NAME undefined and the code will
# treat the driver as the primary driver.
ifneq ($(MODNAME), qdsp6v2)
CHIP_NAME ?= $(MODNAME)
CDEFINES += -DMULTI_IF_NAME=\"$(CHIP_NAME)\"
endif


obj-$(CONFIG_SND_SOC_MSM_QDSP6V2_INTF) += q6_dlkm.o
q6_dlkm-y := $(Q6_OBJS)

obj-$(CONFIG_SND_SOC_MSM_QDSP6V2_VM) += q6_dlkm.o
q6_dlkm-y := $(Q6_OBJS)

obj-$(CONFIG_MSM_ADSP_LOADER) += adsp_loader_dlkm.o
adsp_loader_dlkm-y := $(ADSP_LOADER_OBJS)

obj-$(CONFIG_MSM_QDSP6_PDR) += q6_pdr_dlkm.o
q6_pdr_dlkm-y := $(QDSP6_PDR_OBJS)

obj-$(CONFIG_SPF_CORE) += spf_core_dlkm.o
spf_core_dlkm-y := $(SPF_CORE_OBJS)

obj-$(CONFIG_MSM_QDSP6_NOTIFIER) += q6_notifier_dlkm.o
q6_notifier_dlkm-y := $(QDSP6_NOTIFIER_OBJS)

obj-$(CONFIG_AUDIO_PRM) += audio_prm_dlkm.o
audio_prm_dlkm-y := $(AUDIO_PRM)

obj-$(CONFIG_AUDIO_PKT_ION) += audpkt_ion_dlkm.o
audpkt_ion_dlkm-y := $(AUDIO_PKT_ION)

obj-$(CONFIG_VOICE_MHI) += voice_mhi_dlkm.o
voice_mhi_dlkm-y := $(VOICE_MHI)

# inject some build related information
DEFINES += -DBUILD_TIMESTAMP=\"$(shell date -u +'%Y-%m-%dT%H:%M:%SZ')\"
