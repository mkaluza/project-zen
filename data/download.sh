set -x

src=/data/local/log/app_monitor
cnt=`ls data/ | wc -l`
cnt=$((cnt+1))
dest="data/data.$cnt"
mkdir $dest

adb pull $src $dest/
for f in $dest/*; 
do
	ff=`basename $f`
	if [ -s "$f" ]; then
		adb shell "rm $src/$ff"
	else
		rm -f "$f"
	fi
done

adb shell showprop.sh /sys/devices/system/cpu/cpufreq/\*/ > $dest/settings
