/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X platform on the FM-1: plat.h over Felucca's HAL, panel map, USB rings and storage.
 * Part of the unity build (x0x.c), after the HAL, usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    uint32_t raw = fm1_in.buttons, out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return fm1_in.notes & ((1u << NKEYS) - 1u); }

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_q12 = 2048;
uint32_t plat_master(void) { return master_q12; }

/* the battery: ADC ch3 (its divider), smoothed by the main loop. The stock firmware's thresholds, as
 * melodee and dxsloop use them, give 0-3 bars; there is no charger line, so a USB host means power */
static int32_t batt_raw;
int plat_battery(void)
{
    if (usb.config && !usb.suspended)
        return 4;
    if (!batt_raw)
        return -1;
    return batt_raw >= 591 ? 3 : batt_raw >= 561 ? 2 : batt_raw >= 531 ? 1 : 0;
}

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* Glow: every button not lit is lit dim, on one scan frame in GLOW_DIV (the HAL's fm1_led_dim) */
#define GLOW_DIV 4u
static uint8_t glow_on = 1;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
/* PLAY has two LEDs: its red at the button's own position, a green at the keyless matrix position
 * column 8, row PA5 (found on the device). Green, unless recording. */
static uint8_t play_red;
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
static uint32_t keys_dim;
void plat_keys_dim(uint32_t keys) { keys_dim = keys; }
void plat_leds(uint32_t buttons, uint32_t keys)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        uint8_t q = led_pos[id];
        if (i == B_PLAY && !play_red)            /* PLAY's green LED instead (keyless position col 8, row PA5) */
            q = (uint8_t)((8u << 3) | 1u);
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (i < NB ? glow_on : (keys_dim >> (i - NB)) & 1u)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
    fm1_led_dim_div = glow_on || keys_dim ? GLOW_DIV : 0u;
}

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN */
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    mi_r++;
    return 1;
}
void plat_midi_out(uint32_t pkt) { midi_out_event(pkt); }

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }

int plat_slot_zones(int k) { return (k >= 0 && k < SMP_USER_SLOTS) ? usr_nz[k] : 0; }

const uint8_t *plat_slot(int k, int z, uint32_t *nsamples, uint32_t *rate, char name[9])
{
    const smp_user_hdr_t *h;
    uint32_t i;
    if (k < 0 || k >= SMP_USER_SLOTS || z < 0 || z >= usr_nz[k])
        return 0;
    h = (const smp_user_hdr_t *)smp_user_xip((uint32_t)k);
    for (i = 0; i < 8u; i++)
        name[i] = h->name[i] >= 32 && h->name[i] < 127 ? h->name[i] : 0;
    name[8] = 0;
    *nsamples = h->zone[z].n;
    *rate = (h->zone[z].rate * 44100u + 32768u) >> 16;
    return smp_user_xip((uint32_t)k) + SMP_USER_DATA + h->zone[z].off;
}

static volatile uint32_t audio_cpu_pct, audio_xruns, audio_peak_pct;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }

int plat_cpu2_set(int on)
{
    int rc = 0;
    if (!on) {
        fm1_cpu1_hold();
        return 0;
    }
    {
        int locked = fm1_guard_top_locked();            /* at boot the top is not locked yet: leave it so */
        if (locked)
            fm1_guard_unlock_top();                     /* its entry word is in the protected top of RAM */
        fm1_guard_pc_open();                            /* it starts in the chip's ROM */
        rc = fm1_cpu1_start();
        fm1_guard_enable(FM1_GUARD_PC);
        if (locked)
            fm1_guard_lock_top();
    }
    return rc;
}
int plat_cpu2_on(void) { return fm1_c1_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
int plat_cpu2_wait(void) { return fm1_cpu1_wait(); }
int plat_cpu2_gave_up(void) { return fm1_c1_gave_up; }
uint32_t plat_cpu2_bad(void) { return fm1_c1_mb.bad; }

/* PROBE: SYS 360 -> 320 MHz at boot in JieLi's order (hal/fm1_clk320.h). The state
 * survives resets (.noinit) and is forgotten at power-off. A reset while the switch is under way
 * (ARMED) is not counted as a failed boot, and no switch is tried again until power-off (TRIPPED). */
#define CLKP_MAGIC 0x434C4B33u                          /* "CLK3" */
enum { CLKP_NONE, CLKP_ARMED, CLKP_DONE, CLKP_SKIPPED, CLKP_TRIPPED, CLKP_KEPT };
static const char *const CLKP_N[] = {"-", "ARMED", "320 OK", "SKIPPED", "TRIPPED: 360 UNTIL OFF", "320 (KEPT BY RESET)"};
static const char *const CLKP_E[] = {"", "ALREADY 320", "SYS MODE", "PLL2 NOT 270", "PLL2 REF", "DIVIDERS",
                                     "SYS NOT ON PLL", "USB ON PLL2", "SDRAM", "CACHE B9", "VERIFY", "CONTROL: STAYS 360"};
struct clkp_s {
    uint32_t magic, state, reason, tries, trips, mult_before, mult_after;
    uint32_t was_sys_div, was_sfc_baud, was_sfc_con, was_cache, was_clk_con2;
    uint32_t loop_before, loop_after;                   /* fm1_clk320_loop_ticks around the switch */
} clkp __attribute__((section(".noinit")));
uint32_t plat_fs_meas;                                  /* the audio rate, measured against TIMER5 (main_fm1.c) */

/* SOAK (probe build, X0X_SOAK_MIN > 0): PERF TEST back to back for that many minutes from boot (ui.c
 * soak_tick); kept across resets so a crash mid-soak is reported after the reboot, forgotten at power-off */
#define SOAK_MAGIC 0x534F414Bu                          /* "SOAK" */
enum { SOAK_IDLE, SOAK_RUNNING, SOAK_DONE, SOAK_INTERRUPTED, SOAK_STOPPED };
struct soak_s {
    uint32_t magic, state, runs, elapsed_s, lost_at_s, bad, gave_up, xruns, xr0;
    uint32_t max_load10, max_peak, fs_min, fs_max, mhz10, shown;
} soak __attribute__((section(".noinit")));

void plat_clk320_power_on(int power_on)                 /* fm1_cstart, before the boot guard counts */
{
    if (power_on || clkp.magic != CLKP_MAGIC) {
        memset(&clkp, 0, sizeof clkp);
        clkp.magic = CLKP_MAGIC;
    }
    if (power_on || soak.magic != SOAK_MAGIC) {
        memset(&soak, 0, sizeof soak);
        soak.magic = SOAK_MAGIC;
    }
    if (soak.state == SOAK_RUNNING) {                   /* a reset during the soak: a crash or a hang */
        soak.state = SOAK_INTERRUPTED;
        soak.shown = 0;
    }
    if (clkp.state == CLKP_ARMED) {                     /* reset during the switch */
        clkp.state = CLKP_TRIPPED;
        clkp.trips++;
        bootguard.pending = 0;                          /* not a failed boot: not towards safe mode or UBOOT */
    }
}

void plat_clk320_run(void)                              /* fm1_main: one core, IRQs off, nothing running */
{
    fm1_clk320_t p;
    uint32_t f;
    int rc;
    if (clkp.state == CLKP_TRIPPED)
        return;
    clkp.mult_before = fm1_clk320_mult();
    f = irq_save();
    clkp.loop_before = fm1_clk320_loop_ticks();         /* at the boot loader's clock (360 if PLL2 is 270) */
    irq_restore(f);
    clkp.loop_after = clkp.loop_before;
    rc = fm1_clk320_check(&p);
#if !X0X_CLK320
    if (rc == FM1_CLK320_OK)
        rc = FM1_CLK320_ECONTROL;                       /* the control build: checked, not switched */
#endif
    if (rc) {
        clkp.state = rc == FM1_CLK320_EALREADY ? CLKP_KEPT : CLKP_SKIPPED;
        clkp.reason = (uint32_t)rc;
        clkp.mult_after = clkp.mult_before;
        return;
    }
    clkp.tries++;
    clkp.state = CLKP_ARMED;
    fm1_wdt_feed();
    f = irq_save();
    FL_FAR(fm1_clk320_switch)(&p);
    clkp.loop_after = fm1_clk320_loop_ticks();
    irq_restore(f);
    fm1_wdt_feed();
    clkp.mult_after = fm1_clk320_mult();
    if (clkp.mult_after == FM1_CLK320_MULT_320) {
        clkp.state = CLKP_DONE;
        clkp.reason = 0;
    } else {
        clkp.state = CLKP_SKIPPED;
        clkp.reason = FM1_CLK320_EVERIFY;
    }
}

const char *plat_clk320_state(void) { return clkp.state < 6u ? CLKP_N[clkp.state] : "?"; }
const char *plat_clk320_reason(void) { return clkp.state == CLKP_SKIPPED && clkp.reason < 12u ? CLKP_E[clkp.reason] : ""; }
uint32_t plat_clk320_mult(void) { return clkp.mult_after; }
/* the clock now x 10, from the loop timed at boot: 360 x before / after; 0 when the boot did not start at 360 */
uint32_t plat_clk320_loop_mhz10(void)
{
    if (clkp.mult_before != FM1_CLK320_MULT_360 || !clkp.loop_after)
        return 0;
    return (3600u * clkp.loop_before + clkp.loop_after / 2u) / clkp.loop_after;
}
uint32_t plat_clk320_loop_before(void) { return clkp.loop_before; }
uint32_t plat_clk320_loop_after(void) { return clkp.loop_after; }
uint32_t plat_xruns(void) { return audio_xruns; }

static int perf_cyc_ok, perf_stalls_on;
void plat_perf_init(void) { perf_cyc_ok = fm1_perf_cycles_run(); }
uint32_t plat_cycles(void) { return perf_cyc_ok ? fm1_perf_cycles() : fm1_ticks(); }
int plat_cycles_cpu(void) { return perf_cyc_ok; }
uint32_t plat_cycles_hz(void) { return 24000000u; }   /* the timer; the CPU's rate is measured */
uint32_t plat_ticks24(void) { return fm1_ticks(); }
/* the interrupts' stack (fm1_guard.h): filled with a mark before any interrupt runs (irq_stack_mark,
 * fm1_cstart), so the deepest point reached is the lowest word no longer marked */
#define IRQ_STACK_MARK 0x49525153u                  /* "IRQS" */
static void irq_stack_mark(void)
{
    uint32_t *p = (uint32_t *)(void *)_sstack_lo, *top = (uint32_t *)(void *)_sstack_top - 16;
    uint32_t here = (uint32_t)(uintptr_t)&p;        /* never mark below a live frame of our own */
    if (here > (uint32_t)(uintptr_t)p && here < (uint32_t)(uintptr_t)top)
        top = (uint32_t *)(uintptr_t)((here - 256u) & ~3u);
    for (; p < top; p++)
        *p = IRQ_STACK_MARK;
}
uint32_t plat_irq_stack_size(void) { return (uint32_t)(_sstack_top - _sstack_lo); }
uint32_t plat_irq_stack_used(void)
{
    const uint32_t *p = (const uint32_t *)(const void *)_sstack_lo, *top = (const uint32_t *)(const void *)_sstack_top;
    while (p < top && *p == IRQ_STACK_MARK)
        p++;
    return (uint32_t)((const char *)top - (const char *)p);
}

uint32_t plat_cpu_peak_pct(void)
{
    uint32_t p = audio_peak_pct;
    audio_peak_pct = 0;
    return p;
}
int plat_stalls(uint32_t s[3])
{
    if (!perf_stalls_on)
        return -1;
    fm1_perf_stalls(s);
    return 0;
}
void plat_stalls_enable(int on)
{
    perf_stalls_on = on;
    fm1_perf_stalls_enable(on);
}
