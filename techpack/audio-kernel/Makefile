USERINCLUDE     += \
		-I$(srctree)/techpack/audio-kernel/include/uapi \
		-I$(srctree)/techpack/audio-kernel/include/uapi

LINUXINCLUDE    += \
                -I$(srctree)/techpack/audio-kernel/include \
		-I$(srctree)/techpack/audio-kernel/include/uapi


ifeq ($(CONFIG_ARCH_PINEAPPLE), y)
LINUXINCLUDE    += -include $(AUDIO_ROOT)/config/pineappleautoconf.h
endif

ifeq ($(CONFIG_ARCH_PINEAPPLE), y)
include $(AUDIO_ROOT)/config/pineappleauto.conf
endif


obj-y := dsp/ ipc/ soc/ asoc/ asoc/codecs/ asoc/codecs/lpass-cdc/ asoc/codecs/bolero/ asoc/codecs/wcd939x/ asoc/codecs/wsa884x/ asoc/codecs/wcd938x/ asoc/codecs/cs35l41/ asoc/codecs/cs35l43/ asoc/codecs/wsa883x/ asoc/codecs/wcd937x/ asoc/codecs/wcd9378/ asoc/codecs/tlv320aic3x/ asoc/codecs/aw882xx/ asoc/codecs/tef668x/

