#!/bin/bash

src=$1
datadir=$src/data
imgdir=$src/images

mkdir -p $datadir $imgdir

if [ ! -f $src/db.sqlite3 ]; then
	echo "$src/db.sqlite3 not found"
	exit 1;
fi

function do_stacked_graph() {
title="$1"
sql="$2"
plotcmd="$3"
gpcmd="$4"
fname=`echo $title | tr A-Z a-z | sed -e "s/ /_/g"`

echo $fname >> $datadir/img_names.txt

echo "Generating $fname"

echo "$sql" | tee "$datadir/$fname.sql" | sqlite3 -csv -separator ' ' -noheader $src/db.sqlite3 | awk -f ./split_lines.awk > $datadir/${fname}.dat

echo "
set terminal jpeg medium
set output \"$imgdir/${fname}.jpg\"
set boxwidth 0.75 absolute
set style fill solid 1.00 border -1
set style histogram rowstacked
set style data histograms
set grid
set title '$fname'
$gpcmd
plot '$datadir/${fname}.dat' $plotcmd " | tee "$datadir/$fname.cmd" | gnuplot
}

function do_multi_stacked_graph() {
fname="$1"
mpopts="$2"
plotcmd="$3"

shift 3

echo $fname >> $datadir/img_names.txt

echo "Generating $fname"

(
echo "
set terminal jpeg small size 640,280
set output \"$imgdir/${fname}.jpg\"

set multiplot $mpopts
set boxwidth 0.75 absolute
set style fill solid 1.00 border -1
set style histogram rowstacked
set style data histograms
set grid

set xtics rotate
#rotate by doesn't work with multiplot
"

while [ $# -gt 0 ]; do
echo "
$3
set title \"$2\"
plot '$datadir/${1}.dat' $plotcmd "
shift 3
done
) | tee "$datadir/$fname.cmd" |gnuplot
}

#nosuspend
do_stacked_graph \
	"time_in_freq_and_efficiency_nosuspend" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_nosuspend" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_nosuspend_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"nosuspend_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"time_in_freq_and_efficiency_nosuspend" "Time in freq" "" \
	"load_in_freq_and_efficiency_nosuspend" "Load in freq" "" \
	"time_in_freq_and_efficiency_nosuspend_norm" "Time in freq norm" "set yrange [0:120];"

#foreground
do_stacked_graph \
	"time_in_freq_and_efficiency_foreground" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 1 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_foreground" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 1  group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_foreground_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and foreground_mode = 1  group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"foreground_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"time_in_freq_and_efficiency_foreground" "Time in freq" "" \
	"load_in_freq_and_efficiency_foreground" "Load in freq" "" \
	"time_in_freq_and_efficiency_foreground_norm" "Time in freq norm" "set yrange [0:120];"

#active
do_stacked_graph \
	"time_in_freq_and_efficiency_active" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and active_mode=1 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_active" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and active_mode=1 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_active_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and active_mode=1 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"active_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"time_in_freq_and_efficiency_active" "Time in freq" "" \
	"load_in_freq_and_efficiency_active" "Load in freq" "" \
	"time_in_freq_and_efficiency_active_norm" "Time in freq norm" "set yrange [0:120];"

#standby
do_stacked_graph \
	"time_in_freq_and_efficiency_standby" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and active_mode=0 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_standby" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and active_mode=0 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_standby_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and active_mode=0 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"standby_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"time_in_freq_and_efficiency_standby" "Time in freq" "" \
	"load_in_freq_and_efficiency_standby" "Load in freq" "" \
	"time_in_freq_and_efficiency_standby_norm" "Time in freq norm" "set yrange [0:120];"

#background
do_stacked_graph \
	"time_in_freq_and_efficiency_background" \
	"select freq, sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 0 group by freq order by freq;"\
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"load_in_freq_and_efficiency_background" \
	"select freq, freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 0  group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"time_in_freq_and_efficiency_background_norm" \
	"select freq, 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and foreground_mode = 0  group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"background_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"time_in_freq_and_efficiency_background" "Time in freq" "" \
	"load_in_freq_and_efficiency_background" "Load in freq" "" \
	"time_in_freq_and_efficiency_background_norm" "Time in freq norm" "set yrange [0:120];"

#suspend
do_stacked_graph \
	"time_in_freq_and_efficiency_suspend" \
	"select freq, sum(cpu_load_time), sum(cpu_idle_time) from _cpu_usage where suspend=1 group by freq order by freq;"\
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'"

do_stacked_graph \
	"load_in_freq_and_efficiency_suspend" \
	"select freq, freq*sum(cpu_load_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=1 group by freq order by freq;" \
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'"

do_stacked_graph \
	"time_in_freq_and_efficiency_suspend_norm" \
	"select freq, 100.0*sum(cpu_load_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=1 group by freq order by freq;" \
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"suspend_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"time_in_freq_and_efficiency_suspend" "Time in freq" "" \
	"load_in_freq_and_efficiency_suspend" "Load in freq" "" \
	"time_in_freq_and_efficiency_suspend_norm" "Time in freq norm" "set yrange [0:120];"

#active foreground
do_stacked_graph \
	"time_in_freq_and_efficiency_foreground_active" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 1 and active_mode = 1 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_foreground_active" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 1 and active_mode = 1 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_foreground_active_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and foreground_mode = 1 and active_mode = 1 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"foreground_active_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"time_in_freq_and_efficiency_foreground_active" "Time in freq" "" \
	"load_in_freq_and_efficiency_foreground_active" "Load in freq" "" \
	"time_in_freq_and_efficiency_foreground_active_norm" "Time in freq norm" "set yrange [0:120];"

#active background
do_stacked_graph \
	"time_in_freq_and_efficiency_background_active" \
	"select freq, sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 0 and active_mode = 1 group by freq order by freq;"\
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"load_in_freq_and_efficiency_background_active" \
	"select freq, freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 0 and active_mode = 1 group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"time_in_freq_and_efficiency_background_active_norm" \
	"select freq, 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and foreground_mode = 0 and active_mode = 1 group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"background_active_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"time_in_freq_and_efficiency_background_active" "Time in freq" "" \
	"load_in_freq_and_efficiency_background_active" "Load in freq" "" \
	"time_in_freq_and_efficiency_background_active_norm" "Time in freq norm" "set yrange [0:120];"

#standby foreground
do_stacked_graph \
	"time_in_freq_and_efficiency_foreground_standby" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 1 and active_mode = 0 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_foreground_standby" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 1 and active_mode = 0 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_foreground_standby_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and foreground_mode = 1 and active_mode = 0 group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"foreground_standby_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"time_in_freq_and_efficiency_foreground_standby" "Time in freq" "" \
	"load_in_freq_and_efficiency_foreground_standby" "Load in freq" "" \
	"time_in_freq_and_efficiency_foreground_standby_norm" "Time in freq norm" "set yrange [0:120];"

#standby background
do_stacked_graph \
	"time_in_freq_and_efficiency_background_standby" \
	"select freq, sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 0 and active_mode = 0 group by freq order by freq;"\
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"load_in_freq_and_efficiency_background_standby" \
	"select freq, freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and foreground_mode = 0 and active_mode = 0 group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"time_in_freq_and_efficiency_background_standby_norm" \
	"select freq, 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and foreground_mode = 0 and active_mode = 0 group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"set yrange [0:120];"

do_multi_stacked_graph \
	"background_standby_multi" \
	"layout 1,3 scale 1.14,1" \
	"using 2:xtic(1) t 'active' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"time_in_freq_and_efficiency_background_standby" "Time in freq" "" \
	"load_in_freq_and_efficiency_background_standby" "Load in freq" "" \
	"time_in_freq_and_efficiency_background_standby_norm" "Time in freq norm" "set yrange [0:120];"

echo '
select suspend, sum(count) as count, sum(time) as time,
	    sum(load) as load, sum(energy) as energy
	        from states_distribution group by suspend;

' | sqlite3 -list -separator "|" $1/db.sqlite3 | awk -f sql2table.awk > $1/data/q1.dat

echo '
select * from states_distribution_nosuspend;
' | sqlite3 -list -separator "|" $1/db.sqlite3 | awk -f sql2table.awk > $1/data/q2.dat
