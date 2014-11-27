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

echo "Generating $fname"

echo "$sql" | sqlite3 -csv -separator ' ' -noheader $src/db.sqlite3 | awk -f ./split_lines.awk > $datadir/${fname}.dat

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
plot '$datadir/${fname}.dat' $plotcmd " | gnuplot
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

#foreground
do_stacked_graph \
	"time_in_freq_and_efficiency_foreground" \
	"select freq, sum(app_cpu_time), sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and app_cpu_time > 0 group by freq order by freq;"\
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"load_in_freq_and_efficiency_foreground" \
	"select freq, freq*sum(app_cpu_time), freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and app_cpu_time > 0  group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'"

do_stacked_graph \
	"time_in_freq_and_efficiency_foreground_norm" \
	"select freq, 100.0*sum(app_cpu_time)/sum(cpu_total_time), 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and app_cpu_time > 0  group by freq order by freq;" \
	"using 2 t 'app', '' using 3:xtic(1) t 'background', '' using 4 t 'idle'" \
	"set yrange [0:120];"

#background
do_stacked_graph \
	"time_in_freq_and_efficiency_background" \
	"select freq, sum(cpu_bg_time), sum(cpu_idle_time) from _cpu_usage where suspend=0 and app_cpu_time = 0 group by freq order by freq;"\
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"load_in_freq_and_efficiency_background" \
	"select freq, freq*sum(cpu_bg_time), freq*sum(cpu_idle_time) from _cpu_usage where suspend=0 and app_cpu_time = 0  group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \

do_stacked_graph \
	"time_in_freq_and_efficiency_background_norm" \
	"select freq, 100.0*sum(cpu_bg_time)/sum(cpu_total_time), 100.0*sum(cpu_idle_time)/sum(cpu_total_time) from _cpu_usage where suspend=0 and app_cpu_time = 0  group by freq order by freq;" \
	"using 2:xtic(1) t 'background' lc rgb '#00C000', '' using 3 t 'idle' lc rgb '#0080FF'" \
	"set yrange [0:120];"

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

