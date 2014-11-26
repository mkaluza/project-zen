# Makefile.local should contain KDIR variable pointing to kernel source tree
# it can also include all other settings specific to you
-include Makefile.local

obj-m += app_monitor.o
obj-m += cpufreq_dynamic.o

all: dynamic monitor

monitor:
	make -C $(KDIR) M=$(PWD) CFLAGS_MODULE=-fno-pic app_monitor.ko

monitor_install: monitor
	adb shell sysrw
	adb shell rmmod app_monitor
	adb push app_monitor.ko /system/lib/modules/
	adb shell insmod /system/lib/modules/app_monitor.ko

	adb push init.d/99app_monitor /data/local/tmp/
	adb shell 'cd /data/local/tmp; chmod 755 99app_monitor; for d in /data/boot.d /system/etc/init.d $$PWD; do [ -d $$d ] && break; done; [ $$d != $$PWD ] && cp 99app_monitor $$d/; nohup $$d/99app_monitor restart&'

dynamic:
	make -C $(KDIR) M=$(PWD) CFLAGS_MODULE=-fno-pic cpufreq_dynamic.ko

dynamic_install: dynamic
	adb shell sysrw
	adb shell "echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
	adb shell rmmod cpufreq_dynamic
	adb push cpufreq_dynamic.ko /system/lib/modules/
	adb shell insmod /system/lib/modules/cpufreq_dynamic.ko
	adb shell "echo dynamic > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"

clean:
	make -C $(KDIR) M=$(PWD) clean

install: monitor_install dynamic_install
