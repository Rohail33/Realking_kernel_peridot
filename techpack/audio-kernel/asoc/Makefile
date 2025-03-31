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

AUDIO_ROOT=$(srctree)

############ UAPI ############
UAPI_DIR :=	uapi/audio
UAPI_INC :=	-I$(AUDIO_ROOT)/include/$(UAPI_DIR)

############ COMMON ############
COMMON_DIR :=	include
COMMON_INC :=	-I$(AUDIO_ROOT)/$(COMMON_DIR)

############ ASoC Drivers ############

MACHINE_OBJS += msm_common.o

# for SM8150 sound card driver
ifdef CONFIG_SND_SOC_SM8150
	MACHINE_OBJS += sm8150.o
	MACHINE_OBJS += machine_815x_init.o
endif

# for SM6150 sound card driver
ifdef CONFIG_SND_SOC_SM6150
	MACHINE_OBJS += sm6150.o
	MACHINE_OBJS += machine_615x_init.o
endif

# For sa6155 sound card driver
ifdef CONFIG_SND_SOC_SA6155
	MACHINE_OBJS += sa6155.o
endif

# for qcs405 sound card driver
ifdef CONFIG_SND_SOC_QCS405
	MACHINE_OBJS += qcs405.o
endif

# for KONA sound card driver
ifdef CONFIG_SND_SOC_KONA
	MACHINE_OBJS += kona.o
endif

# for LAHAINA sound card driver
ifdef CONFIG_SND_SOC_LAHAINA
	MACHINE_OBJS += lahaina.o
endif

# for WAIPIO sound card driver
ifdef CONFIG_SND_SOC_WAIPIO
	MACHINE_OBJS += waipio.o
endif

# for KALAMA sound card driver
ifdef CONFIG_SND_SOC_KALAMA
	MACHINE_OBJS += kalama.o
endif

# for PINEAPPLE sound card driver
ifdef CONFIG_SND_SOC_PINEAPPLE
	MACHINE_OBJS += pineapple.o
endif

# for PITTI sound card driver
ifdef CONFIG_SND_SOC_PITTI
	MACHINE_OBJS += pineapple.o
endif

# for HOLI sound card driver
ifdef CONFIG_SND_SOC_HOLI
	MACHINE_OBJS += holi.o
endif

ifdef CONFIG_SND_SOC_LITO
	MACHINE_OBJS += kona.o
endif

# for BENGAL sound card driver
ifdef CONFIG_SND_SOC_BENGAL
	MACHINE_OBJS += bengal.o
endif

# for sa8155 sound card driver
ifdef CONFIG_SND_SOC_SA8155
	MACHINE_OBJS += sa8155.o
endif

# for sa7255 sound card driver
ifdef CONFIG_SND_SOC_SA7255_AUTO_SPF
    SPF_MACHINE_OBJS += msm_common.o
    SPF_MACHINE_OBJS += auto_spf_dummy.o
endif

ifdef CONFIG_SND_SOC_QDSP6V2
	PLATFORM_OBJS += platform_init.o
endif

ifdef CONFIG_SND_SOC_SDX
	MACHINE_OBJS += sdx-target.o
endif

ifdef CONFIG_SND_SOC_GVM_AUTO_SPF
    SPF_MACHINE_OBJS += gvm_auto_spf_dummy.o
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

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), shennong))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_SHENNONG
endif

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), houji))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_HOUJI
endif

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), manet))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_MANET
endif

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), aurora))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_AURORA
endif

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), chenfeng))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_CHENFENG
endif

ifneq (, $(filter $(word 2,$(subst _, ,$(TARGET_PRODUCT))), peridot))
	CDEFINES += -DCONFIG_TARGET_PRODUCT_PERIDOT
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

ifeq ($(KERNEL_BUILD), 1)
	obj-y += codecs/
endif
# Module information used by KBuild framework
obj-$(CONFIG_SND_SOC_QDSP6V2) += platform_dlkm.o
platform_dlkm-y := $(PLATFORM_OBJS)

obj-$(CONFIG_SND_SOC_SM8150) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_SM6150) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_SA6155) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_QCS405) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_KONA) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_LAHAINA) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_WAIPIO) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_KALAMA) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_PINEAPPLE) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_PITTI) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_HOLI) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_LITO) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_BENGAL) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_SA8155) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_SDX) += machine_dlkm.o
machine_dlkm-y := $(MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_GVM_AUTO_SPF) += spf_machine_dlkm.o
spf_machine_dlkm-y := $(SPF_MACHINE_OBJS)

obj-$(CONFIG_SND_SOC_SA7255_AUTO_SPF) += spf_machine_dlkm.o
spf_machine_dlkm-y := $(SPF_MACHINE_OBJS)

# inject some build related information
DEFINES += -DBUILD_TIMESTAMP=\"$(shell date -u +'%Y-%m-%dT%H:%M:%SZ')\"
