# fm1-clk320-probe

Test firmware for the M-VAVE FM-1: [X0X](https://github.com/charlesvestal/fm1-x0x) 1.0.6 with both
cores running at JieLi's rated **320 MHz** instead of the FM-1's 360 MHz, **without raising any
supply voltage**. It is the reference for the study below: the patches to read, and the packages
to reproduce its tests on your own unit.

The method, the sequence, the reference code and the results so far are in the study:
**[FM-1 320 MHz Dual Core](https://claude.ai/code/artifact/49894621-e5be-4d22-a585-71fdc09626c6)**.

> **One unit tested.** On it, every 360 MHz run failed within its first two PERF TESTs, and a
> 30-minute soak at 320 MHz passed (132 runs, two cores, 0 bad reads, 0 dropouts).
>
> This repository is a read-only reference: issues, pull requests and discussions are off.

X0X's own README is in [README.x0x.md](README.x0x.md).

## What it changes

Everything sits on top of X0X 1.0.6: `git diff v1.0.6` shows the whole change.

- **At boot, the clock goes from 360 to 320 MHz**, in the order JieLi's SDK uses
  (`firmware/hal/fm1_clk320.h`): the flash controller and the cache are stopped around the PLL
  change and retimed after it. It runs from RAM with interrupts off, after the screen is up and
  before the second core, audio and USB start.
- **Nothing else moves**: no voltage is raised, the boot sector is not written, and installing any
  other firmware replaces it as usual. All other behaviour is X0X 1.0.6.
- **It refuses to switch** if the clock registers are not as expected, and says why.
- **PERF** (GLO > PERFORMANCE) gains a bottom line: `SYS320 <state>  FS <audio rate>`.
- **To fit the flash**, the ride and crash samples lose 1,024 frames (23 ms) of their quiet tails.

## The packages

Four packages, on the [releases](../../releases) page or built with `tools/build_probe.sh`:

| Package | Clock | Load |
| --- | --- | --- |
| `x0x-1.0.6-control-360.fwsc` | stays at 360 MHz | PERF TEST by hand |
| `x0x-1.0.6-probe-320.fwsc` | switches to 320 MHz | PERF TEST by hand |
| `x0x-1.0.6-control-360-soak30.fwsc` | stays at 360 MHz | 30 minutes of PERF TEST from boot, then a report |
| `x0x-1.0.6-probe-320-soak30.fwsc` | switches to 320 MHz | 30 minutes of PERF TEST from boot, then a report |

## Before you install

- **At your own risk.** Have a way back: M-VAVE's updater or the web installer of the firmware
  you normally run.
- **Power-cycle after every install.** A soft reset keeps the PLL where the last firmware left
  it, so without a cold start a "360" build may really run at 320, and the probe cannot time its
  switch. A power cycle always returns to the boot loader's 360 MHz.
- **If the switch ever fails**, the reset is not counted as a failed boot and the switch is not
  tried again until power-off, so the FM-1 does not march into safe mode or UBOOT. The FM-1's
  ROM update mode cannot be overwritten and stays the last resort.
- **Saved projects**: X0X 1.0.6 writes its project only when you press SAVE. If your usual
  firmware keeps projects you care about, do not press SAVE.

Install from a terminal (needs `mido` and `python-rtmidi`):

```sh
python3 tools/fm1_install.py x0x-1.0.6-control-360-soak30.fwsc
```

## How to test

1. Install **`control-360-soak30`**, power-cycle, and leave it alone. About 5 s after boot it opens
   PERF and runs PERF TEST back to back on the normal screen. Do not press HOME or PLAY (they stop
   it), and keep it on USB power.
2. When it ends (or as soon as the top right shows `1 CORE: BAD READS`), note the result.
3. Install **`probe-320-soak30`**, power-cycle, and do the same.

The order matters less than keeping both runs alike: same power source, similar temperature, and
the normal PERF TEST screen. On the tested unit, a build that drew its own status screen instead
did not fail at 360 in 6 runs, while the normal screen failed on run 1: screen activity appears
to change the result.

The manual packages do the same with PERF TEST started by hand (GLO > PERFORMANCE > SEL).

## Reading the report

At the end of a soak, PERF shows (until power-off):

- `SOAK PASSED` only with two cores throughout, 0 bad reads and 0 dropouts; otherwise
  `SOAK FAILED`, `SOAK INTERRUPTED AT mm:ss` (a crash or hang: the FM-1 restarted) or
  `SOAK STOPPED` (a key).
- `RUNS`, the cores (and when the second was lost), `BAD READS`, `DROPOUTS`, the worst case and
  its peak.
- `CLOCK nnn MHz (LOOP a > b)`: the core clock from a fixed instruction loop timed against the
  24 MHz crystal before and after the switch (360 × a / b). `?` means the boot did not start
  cold at 360.
- `SYS320 <state>  FS <min-max>`: whether the switch ran (or which check refused it) and the
  audio rate range.

Dropouts with two cores and 0 bad reads mean the CPU ran out (320 MHz is about 11 % slower), not
a misread.

A refused switch (`SKIPPED: …`) means that unit's clock setup differs from the tested one; the
switch did not run and the unit stayed at 360 MHz.

## Building

Set up the X0X build as in [BUILDING.md](BUILDING.md) (the JieLi toolchain and the AC79 SDK),
then:

```sh
tools/build_probe.sh            # all four packages into out/
SOAK_MIN=15 tools/build_probe.sh   # shorter soaks
```

The builds are reproducible: the same source gives the same packages, byte for byte.

## Credits and licence

A fork of [X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, itself a fork of
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments). The
second core's start and its misread detection come from X0X, [Melodee](https://github.com/keremimo/melodee)
(Kerem Kilic) and Jangada (zednaked); the PLL2 register identification from johnskiller's Melodee
PR #27 with Charles Vestal's register reads. The switch sequence follows the JieLi AC79 SDK
(Apache-2.0). The study's Credits section lists more.

GPL-3.0-only, as X0X ([LICENSE](LICENSE)), except `firmware/hal/fm1_clk320.h`, which is MIT so any
FM-1 firmware can use it; see [LICENSING.md](LICENSING.md).
