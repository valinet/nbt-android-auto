#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Valentin Radu
#
# Records iDrive's graphics hang (car 2026-11-25 19:40 and 2026-11-26 18:02: HmiMain3D waited for the NVIDIA chip,
# "NVRM: Notifier wait timeout eglSwapBuffer" / "NvGpuNotifierWait" / "stuck semaphore" in the system log, about 3 min
# later the watchdog restarted the head unit). Observes only: nothing is stopped or changed. Started by boot.sh, once
# per boot (touch $P/gpu-watch.off = not started). One sloginfo -w (stays running, gets each new line of the system
# log as it comes, severity 2 and lower only: the NVRM lines are 2) is read line by line; nothing is written to
# storage until one of those lines comes after the ones already there at the start. Then, and every 20 s until the
# restart (a
# few MB once per hang): $P/freeze-<date-time>/<time>/ with every thread's
# state (pidin: who waits for whom), the processes' command lines, the whole system log, the newest nbt_decoder /
# CarPlay logs, native.log, the plugin log, the service state and whether our page was on screen
P=/var/opt/hmi/data/rw/NbtAndroidAutoRT
R=/fs/ram3/nbt-aa-native
mkdir /fs/ram3/nbt-gpu-watch.lock 2>/dev/null || exit 0

hits() {
  sloginfo 2>/dev/null | grep -c -e NvGpuNotifierWait -e 'Notifier wait timeout' -e 'stuck semaphore'
}

snap() {   # $1 = directory
  mkdir -p $1
  {
    date
    cat /fs/ram3/nbt-aa-state
    if test -e /fs/ram3/nbt-aa-hidden; then echo 'page: hidden'; else echo 'page: on screen'; fi
    echo "NVRM wait lines: `hits`"
  } > $1/state.txt 2>&1
  pidin > $1/pidin.txt 2>&1
  pidin arg > $1/pidin-arg.txt 2>&1
  sloginfo > $1/slog.txt 2>&1
  for k in nbt_decoder carplay; do
    for f in `ls -t $R/$k-*.log 2>/dev/null | sed 2q`; do cp $f $1/; done
  done
  cp $R/native.log /fs/ram3/nbt-aa-plugin.log $1/ 2>/dev/null
}

# sloginfo -w first prints the events already in the log, then waits for new ones: the first $base matches are
# those already there. A string as the counter (no expr). Returns 0 on a new match, 1 if sloginfo ends.
watch() {
  seen=
  while read -r line; do
    case $line in
      *NvGpuNotifierWait*|*'Notifier wait timeout'*|*'stuck semaphore'*)
        seen=x$seen
        if test ${#seen} -gt "$base"; then return 0; fi ;;
    esac
  done
  return 1
}

base=`hits`
sloginfo -w -s 2 2>/dev/null | watch || exit 0

D=$P/freeze-`date +%m%d-%H%M%S`
while :; do
  snap $D/`date +%H%M%S`
  sleep 20
done
