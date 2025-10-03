dtbo-$(CONFIG_ARCH_PINEAPPLE)	:= pineapple-camera.dtbo
dtbo-$(CONFIG_ARCH_PINEAPPLE)	+= pineapple-camera-v2.dtbo \
									pineapple-camera-sensor-cdp.dtbo \
									pineapple-camera-sensor-mtp.dtbo \
									pineapple-camera-sensor-hdk.dtbo \
									pineapple-camera-sensor-qrd.dtbo \
									shennong-sm8650-camera-sensor.dtbo \
									manet-sm8650-camera-sensor.dtbo \
									houji-sm8650-camera-sensor.dtbo  \
									aurora-sm8650-camera-sensor.dtbo \
									pineapple-camera-sensor-qrd.dtbo \
									pineapple-camera-sensor-aim500.dtbo

dtbo-$(CONFIG_ARCH_CLIFFS)	+= cliffs-camera.dtbo
dtbo-$(CONFIG_ARCH_CLIFFS)	+= cliffs-camera-sensor-cdp.dtbo \
								cliffs-camera-sensor-mtp.dtbo \
								cliffs-camera-sensor-qrd.dtbo \
								peridot-sm8635-camera-sensor.dtbo
