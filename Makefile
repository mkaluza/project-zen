# Makefile.local should contain KDIR variable pointing to kernel source tree
# it can also include all other settings specific to you
-include Makefile.local

obj-m += app_monitor.o
obj-m += cpufreq_dynamic.o

all:
	make -C $(KDIR) M=$(PWD) modules
clean:
	make -C $(KDIR) M=$(PWD) clean
install: all
	adb shell sysrw; adb shell rmmod app_monitor; adb push app_monitor.ko /system/lib/modules/; adb shell insmod /system/lib/modules/app_monitor.ko
	adb shell "echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
	adb shell rmmod cpufreq_dynamic; adb push cpufreq_dynamic.ko /system/lib/modules/; adb shell insmod /system/lib/modules/cpufreq_dynamic.ko
	adb shell "echo dynamic > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
