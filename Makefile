# Makefile.local should contain KDIR variable pointing to kernel source tree
# it can also include all other settings specific to you
-include Makefile.local

obj-m += app_monitor.o
obj-m += cpufreq_dynamic.o

all: dynamic monitor

monitor:
	make -C $(KDIR) M=$(PWD) app_monitor.ko

monitor_install: monitor
	adb shell sysrw
	adb push init.d/99app_monitor /data/local/tmp/
	#adb shell "cd /data/local/tmp; chmod 755 99app_monitor; if [ -d /data/boot.d ]; then mv 99app_monitor /data/boot.d; elif [ -d /system/etc/init.d ]; then mv 99app_monitor /system/etc/init.d/; fi"
	adb shell rmmod app_monitor
	adb push app_monitor.ko /system/lib/modules/
	adb shell insmod /system/lib/modules/app_monitor.ko

dynamic:
	make -C $(KDIR) M=$(PWD) cpufreq_dynamic.ko

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
