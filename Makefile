
uname_p := $(shell uname -p)

KVERSION = $(shell uname -r)
KERNELSRC = /KL/ZODIAC-IMX6-HDCAPM-TOOLCHAIN/linux-4.13
BUILDARCH = ARCH=arm CROSS_COMPILE=arm-v7a-linux-gnueabihf-

COLOR_ON = \033[33;01m
COLOR_OFF= \033[0m

EXTRA_CPPFLAGS = ""

DEVICE=/dev/video2

mst3367-objs := mst3367-drv.o
obj-m += mst3367.o

hdcapm-objs := hdcapm-core.o hdcapm-buffer.o hdcapm-i2c.o hdcapm-compressor.o hdcapm-video.o kl-histogram.o
obj-m += hdcapm.o

default: intel

all: intel arm

# Intel for the current system (either x86 or x64)
intel:
	@echo "$(COLOR_ON)Building Intel $(uname_p)$(COLOR_OFF)"
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) KCPPFLAGS=$(EXTRA_CPPFLAGS) modules
	mkdir -p build-$(uname_p)
	cp hdcapm.ko build-$(uname_p)
	cp mst3367.ko build-$(uname_p)

arm:
	@echo "$(COLOR_ON)Building ARM$(COLOR_OFF)"
	make $(BUILDARCH) -C $(KERNELSRC) M=$(PWD) KCPPFLAGS=$(EXTRA_CPPFLAGS) modules
	mkdir -p build-$(@)
	cp hdcapm.ko build-$(@)
	cp mst3367.ko build-$(@)

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
	rm -rf ./build-*

load:	intel
	sudo dmesg -c >/dev/null
	sudo cp v4l-hdcapm-vidfw-01.fw /lib/firmware
	sudo cp v4l-hdcapm-audfw-01.fw /lib/firmware
	sudo modprobe cx23885
	sudo modprobe saa7164
	sudo modprobe v4l2-dv-timings
	sudo /sbin/insmod ./build-$(uname_p)/mst3367.ko debug=1
	sudo /sbin/insmod ./build-$(uname_p)/hdcapm.ko debug=1 i2c_scan=0

unload:
	sudo /sbin/rmmod hdcapm
	sudo /sbin/rmmod mst3367

test:
	dd if=$(DEVICE) of=raw.ts bs=65535

mntcopy:
	mount /KL/mnt
	cp build-arm/* /KL/mnt
	umount /KL/mnt

sshcopy:
	ssh root@192.168.0.11 "cat >/mnt/mst3367.ko" < build-arm/mst3367.ko
	ssh root@192.168.0.11 "cat >/mnt/hdcapm.ko" < build-arm/hdcapm.ko
	#ssh root@192.168.0.11 "cat >/lib/firmware/v4l-hdcapm-vidfw-01.fw" < v4l-hdcapm-vidfw-01.fw
	#ssh root@192.168.0.11 "cat >/lib/firmware/v4l-hdcapm-audfw-01.fw" < v4l-hdcapm-audfw-01.fw

stream:
	v4l2-ctl -d $(DEVICE) --set-ctrl=video_bitrate=10000000
	v4l2-ctl -d $(DEVICE) --set-ctrl=video_peak_bitrate=10000000
	v4l2-ctl -d $(DEVICE) --set-ctrl=video_gop_size=1
	iso13818_util -i $(DEVICE) -o udp://192.168.0.66:5005 -s

gstreamer:
	#gst-launch-1.0 v4l2src device=$(DEVICE) io-mode=1 ! video/mpegts,systemstream=true ! queue ! udpsink host=192.168.0.66 port=5005
	#gst-launch-1.0 v4l2src device=$(DEVICE) io-mode=1 ! video/mpegts,systemstream=true ! queue ! udpsink host=192.168.0.66 port=5005 buffer-size=1316
	gst-launch-1.0 v4l2src device=$(DEVICE) io-mode=1 ! video/mpegts,systemstream=true ! queue ! filesink location=test.ts

