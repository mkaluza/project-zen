Before anything else, read entire wiki. 

### Instalation

To build these modules, current kernel build directory is needed - that is, if you didn't build the kernel you are running, you won't be able to use them.

1. Create file _Makefile.local_ with contents:

    KDIR=/path/to/your/current/kernel/source

2. Apply patches from _patches_ directory.

    cd /path/to/your/current/kernel/source
    git am /path/to/patch/file/0001-add-notifier-chain-for-oom_adj.patch
    git am /path/to/patch/file/0002-export-additional-symbols-to-modules.patch

3. Try building:

* _make monitor_ builds app monitor
* _make dynamic_ builds dynamic cpufreq governor
* _make_ without parameters builds both above targets

If you get errors about missing symbols, try exporting them like it's done in patch 0002

4. Try installing:

* make monitor_install 
* make dynamic_install
* make install - installs both

This step can fail as your fs layout may be different, something might be missing, or adb isn't running as root. If it does - look into the Makefile and redo steps manually.

### Usage

**Big fat WARNING**

Don't use _cat_ to read those /proc files, as something breaks when you hit ^C on that _cat_. Use *dd if=/proc/app_monitor*. If you use cat anyway, if you the reload the module, the file you cat'ed will be unusabe - only reboot helps. Will try to fix it asap.

The app_monitor module creates two /proc files - app_monitor and app_monitor_raw. The first one is more human readable, the second one is for gathering data for later processing. 
As for now, opening both of them simultaneously, or any of them more than once does wierd things, so don't do it - it's more for debugging and convinience. Will fix it asap...

Along with the app_monitor there is an init.d script which gathers performance data from app_monitor_raw into /data/local/log/app_monitor. Helper scripts to download those data and generate nice graphs are in data directory.

