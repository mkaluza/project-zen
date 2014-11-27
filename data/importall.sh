#!/bin/bash

set -x

pipe=`mktemp -u`
mkpipe $pipe
zcat $1/*.gz | sed -e "s/[a-z][^\ ]*.//g" > $pipe&

sqlite3 $1/db.sqlite3 << _EOF
create table import (ts real, jiffies int, wakeup_timeout int,
	wakeup_suspend int, suspend int,
	wakeup_freq int, freq int,
	wakeup_input int, input_delta int, input_time int,
	wakeup_app int, gid int, utime int, stime int, rtime int,
	cpu_load int, cpu_time int, cpu_max_load int,
	cpu0_active int, cpu0_idle int, cpu1_active int, cpu1_idle int);

.separator " "
.import $pipe import

--delete invalid entries
--FIXME stime+utime is an error in app time accounting

delete from import where freq=0 or jiffies < 0 or stime+utime>(jiffies+1)*10;

--TODO calculate active/standby flag

drop VIEW _cpu_usage;
CREATE VIEW _cpu_usage as select suspend, freq, cpu_load as cpu_load_time, cpu_time as cpu_total_time, cpu_time-cpu_load as cpu_idle_time, cpu_load-rtime as cpu_bg_time, 100.0*(cpu_load)/(cpu_time) as cpu_load_pct
	,rtime as app_cpu_time, 100.0*rtime/cpu_time as app_cpu_pct, 100.0*rtime/cpu_load as app_load_pct
	,cpu_max_load, 100.0*rtime/cpu_max_load as app_max_load_pct
	,input_delta
	from import
	where cpu_load > 0 and cpu_time > 0 and cpu_max_load > 0;

drop VIEW _cpu_usage_dist;
CREATE VIEW _cpu_usage_dist as select suspend, freq, round(cpu_load_pct) as cpu_load_pct, sum(cpu_total_time) as cpu_total_time
	from _cpu_usage group by suspend, freq, round(cpu_load_pct);
drop table cpu_usage_dist;
create table cpu_usage_dist as select * from _cpu_usage_dist;

drop view _cpu_usage_dist_cumul;
create view _cpu_usage_dist_cumul as select suspend, freq, cpu_load_pct, (select sum(cpu_total_time) from cpu_usage_dist cc where c.freq=cc.freq and c.suspend=cc.suspend and cc.cpu_load_pct<=c.cpu_load_pct) as cumul_time_us from cpu_usage_dist c group by suspend, freq, cpu_load_pct;
drop table cpu_usage_dist_cumul;
create table cpu_usage_dist_cumul as select * from _cpu_usage_dist_cumul;

drop view cpu_usage_dist_cumul_norm;
create view cpu_usage_dist_cumul_norm as select suspend, freq, cpu_load_pct, 100.0*cumul_time_us/(select max(cumul_time_us) from cpu_usage_dist_cumul cc where cc.suspend=c.suspend and cc.freq=c.freq) as norm_val from cpu_usage_dist_cumul c;

drop view cpu_usage_dist_norm;
create view cpu_usage_dist_norm as select suspend, freq, cpu_load_pct, 100.0*cpu_total_time/(select max(cumul_time_us) from cpu_usage_dist_cumul cc where cc.suspend=c.suspend and cc.freq=c.freq) as norm_val from cpu_usage_dist c;
_EOF

