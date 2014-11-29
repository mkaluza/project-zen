**Note:** this is a very preliminary version to showcase things this can do so far - I intend to add more detailed charts, more detailed analysis and any improvements I made using those results.

Below are sets of three charts

1. time_in_freq* charts show proportions of time cpu spent at each frequency and how it was distributed between foreground app, other apps and how much was idle. This chart type shows how much time governor chose to spend at each freqency.
2. load_in_freq* show the same proportions as time_in_freq, but using actual work cpu did (time*freq) - this chart type better than previous shows how much power was used at each frequency. It's not perfect yet as it doesn't take cpu voltage into account, which can have signifficant influence on power consumption at higher freqs (on my phone each cpu clock tick uses almost twice as much power at 1000MHz as at 100MHz).
3. time_in_freq*_norm charts show distribution between app, background and idle time at each freq regardles of how much time cpu spent there. Those charts are better for measuring governor efficiency at making decisions to lower the frequency.

More things coming...

# Some high level statistics

## Screen on vs screen off

``` sql
select suspend, sum(count) as count, sum(time) as time,
    sum(load) as load, sum(energy) as energy 
    from states_distribution group by suspend;
```

{% include q1_results %}

Here's screen off load distribution:

![][screen off cpu]

## Screen on cpu usage

![][screen on cpu]

Let's take a closer look at what's happening when the screen is on:

``` sql
select * from states_distribution_nosuspend;
```

{% include q2_results %}


Here are the charts (all of them done independently for active/standby and fg/bg), which confirm this:

*Active mode*

![][active cpu usage]

*Standby mode*

![][standby cpu usage]

*Foreground app active*

![][foreground cpu usage]

*Foreground app inactive*

![][background cpu usage]

Here are correlated charts:

*Active mode, foreground app active*

![][foreground active cpu usage]

*Active mode, foreground app inactive*

![][background active cpu usage]

*Standby mode, foreground app active*

![][foreground standby cpu usage]

*Standby mode, foreground app inactive*

![][background Standby cpu usage]

## Conclusons

### Parameter changes

### Algorithm improvements

### New features

{% include images %}
