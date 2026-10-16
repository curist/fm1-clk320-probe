#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the four probe packages into out/ (see PROBE.md), each with ./build.sh (BUILDING.md):
#   x0x-1.0.6-control-360.fwsc         stays at 360 MHz; PERF TEST by hand
#   x0x-1.0.6-probe-320.fwsc           switches to 320 MHz at boot; PERF TEST by hand
#   x0x-1.0.6-control-360-soak30.fwsc  stays at 360 MHz; 30 minutes of PERF TEST from boot, then a report
#   x0x-1.0.6-probe-320-soak30.fwsc    switches to 320 MHz; 30 minutes of PERF TEST from boot, then a report
# Environment as for build.sh (JIELI_TOOLCHAIN, AC79_SDK). SOAK_MIN overrides the soak length (minutes).
set -e
cd "$(dirname "$0")/.."
mkdir -p out
soak="${SOAK_MIN:-30}"
for clk in 0 1; do
    for min in 0 "$soak"; do
        name=$([ "$clk" = 1 ] && echo probe-320 || echo control-360)
        [ "$min" = 0 ] || name="$name-soak$min"
        echo "== $name"
        X0X_CLK320=$clk X0X_SOAK_MIN=$min ./build.sh
        cp build/x0x.fwsc "out/x0x-1.0.6-$name.fwsc"
    done
done
ls -l out/*.fwsc
