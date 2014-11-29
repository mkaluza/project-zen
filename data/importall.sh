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

--FIXME some errors in cpu time accounting - probably due to cpuidle
update import set cpu_time=cpu0_active+cpu0_idle, cpu1_idle=0 where cpu1_active=0 and cpu1_idle>(jiffies+100)*10000;

create index tsindex on import(ts);

CREATE TABLE cpu_voltage (freq int, voltage real);
_EOF

# put cpu voltages into one of those files in freq,voltage format
f="$1/cpu_voltage.csv";
[ ! -f "$f" -a -f "cpu_voltage.csv" ] && cp cpu_voltage.csv $f

if [ -f "$f" ]; then
sqlite3 $1/db.sqlite3 << _EOF
.mode csv
.import '$f' cpu_voltage
_EOF
else
sqlite3 $1/db.sqlite3 << _EOF
insert into cpu_voltage select distinct freq, 1 from import order by 1;
_EOF
fi

# if cpu_voltage.csv containes raw values (like register values - but have to be decimal! sqlite can't read hex:/)
# instead of absolute voltage, put conversion commands into cpu_power.sql
#
# example: update cpu_voltage set voltage=0.7+0.0125*voltage;

if [ -f cpu_power.sql ]; then
	cat cpu_power.sql | sqlite3 $1/db.sqlite3
fi

INPUT_TIME_MS=50
STANDBY_DELAY_FACTOR=2
sqlite3 $1/db.sqlite3 << _EOF
alter table cpu_voltage add rel_power real default 1;
update cpu_voltage set rel_power=voltage*voltage/(select min(voltage*voltage) from cpu_voltage);

--calculate active/standby flag
alter table import add active int default 0;
--TODO get standby_delay_factor from gov settings
create temp table t1 as select ts, (select min(cc.ts) from import cc where cc.ts>c.ts and cc.input_delta >= ${INPUT_TIME_MS}000 and cc.freq=100 and cc.jiffies>=$STANDBY_DELAY_FACTOR) as active_end from import c where wakeup_input=1;
create temp table t2 as select distinct i.ts from import i, t1 u where i.ts between u.ts and u.active_end;
update import set active=1 where ts in (select ts from t2) and (active=0 or active is null);
update import set active=0 where active is null;

drop VIEW _cpu_usage;
CREATE VIEW _cpu_usage as select suspend, freq, cpu_load as cpu_load_time, cpu_time as cpu_total_time, cpu_time-cpu_load as cpu_idle_time, cpu_load-rtime as cpu_bg_time, 100.0*(cpu_load)/(cpu_time) as cpu_load_pct
	,rtime as app_cpu_time, 100.0*rtime/cpu_time as app_cpu_pct, 100.0*rtime/cpu_load as app_load_pct
	,cpu_max_load, 100.0*rtime/cpu_max_load as app_max_load_pct
	,input_delta, active as active_mode
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

