#!/bin/bash
# usage: tools/oot_run_watchdog.sh <logname> <seconds> [extra env assignments...]
# Runs dkb (GPU on unless TOTK_GPU=null is passed) and kills it when MemAvailable drops below the floor.
# Logs go to oot-recomp/logs/<name>.log, memory samples to <name>.mem (time, MemAvailable kB, dkb Rss/Anon/Swap kB),
# the smaps of the largest Rss to <name>.smaps, screenshots every 10 world-clock seconds to logs/shots-<name>.
# WD_KB=<kB> sets the MemAvailable floor (default 2500000).
B=/run/media/chiuaua/defe51b8-3560-427d-b86c-adcd5e1e379d/oot-recomp
name=$1; secs=$2; shift 2
cd $B/work/build/oot111
mkdir -p $B/logs/shots-$name
env TOTK_XLATE=$B/oot-pc-recomp/build/xlate/totk_xlate \
    TOTK_PROFILE_DIR=$PWD/../profile-oot111-save \
    TOTK_FAKE_BUTTONS="1@60+0.3,1@70+0.3,1@80+0.3,1@90+0.3,1@100+0.3" \
    TOTK_SHOTS=w0-1000:10 TOTK_SHOTS_DIR=$B/logs/shots-$name \
    "$@" timeout -s KILL $((secs+15)) ./dkb --seconds $secs > $B/logs/$name.log 2>&1 &
pid=$!
while kill -0 $pid 2>/dev/null; do
  avail=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
  if [ "$avail" -lt ${WD_KB:-2500000} ]; then
    echo "WATCHDOG: MemAvailable ${avail} kB, killing" >> $B/logs/$name.log
    pkill -KILL -x dkb
  fi
  p=$(pgrep -x dkb | head -1)
  r=$(awk '/^Rss:/{print $2}' /proc/$p/smaps_rollup 2>/dev/null)
  echo "$(date +%s) $avail $(grep -E "^(Rss|Anonymous|Swap):" /proc/$p/smaps_rollup 2>/dev/null | awk '{printf "%s ", $2}')" >> $B/logs/$name.mem
  if [ -n "$r" ] && [ "$r" -gt "${maxr:-0}" ]; then maxr=$r; cp /proc/$p/smaps $B/logs/$name.smaps.tmp 2>/dev/null && mv $B/logs/$name.smaps.tmp $B/logs/$name.smaps; fi
  timeout 2 tail -f /dev/null
done
wait $pid; echo "exit $?"
