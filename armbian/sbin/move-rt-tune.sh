#!/bin/sh
# Realtime tuning for the Move's SPI/audio path.
# Ported from stock /etc/init.d/move, which does not run under Armbian.
# Without this the ablspi kernel thread competes with the JACK audio
# callback and supernova misses its deadline every period.
set -u

pids_for() { pgrep "$1" 2>/dev/null; }

echo "[rt] raising SPI kernel threads"
for p in $(pids_for "spi0"); do chrt -f -p 90 "$p" 2>/dev/null && echo "   spi0 pid $p -> FIFO 90"; done
for p in $(pids_for "ablspi"); do chrt -f -p 91 "$p" 2>/dev/null && echo "   ablspi pid $p -> FIFO 91"; done

echo "[rt] pinning generic IRQ threads to cores 0-2"
for p in $(pgrep "irq/" 2>/dev/null); do taskset -p 7 "$p" >/dev/null 2>&1; done

echo "[rt] restricting workqueues to cores 0-2"
for m in $(find /sys/devices/virtual/workqueue/ -name cpumask 2>/dev/null); do
    echo 7 > "$m" 2>/dev/null || true
done

echo "[rt] isolating audio/SPI IRQ threads on core 3"
for p in $(pgrep "DMA IRQ" 2>/dev/null) $(pids_for "ablspi") $(pids_for "spi0"); do
    taskset -p 8 "$p" >/dev/null 2>&1 && echo "   pid $p -> core 3"
done

echo "[rt] done"
