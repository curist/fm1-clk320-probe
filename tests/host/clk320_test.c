/* SPDX-License-Identifier: MIT */
/* fm1_clk320.h's reading of the clock registers, on the host: what each boot loader leaves, and what the check and
 * fm1_clk320_mhz() make of it. Register values: a V15 unit (felucca-bsp chip/fm1_clock.h, the study's reads) and a
 * V16 unit (the same reads: SYS source 2, PLL2 never programmed). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* not CHECK(): NDEBUG must not turn the test off */
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static uint32_t regs[16];
static uint32_t *reg(uint32_t a)
{
    static const uint32_t addr[] = {0x10008u, 0x1000Cu, 0x10014u, 0x119A0u, 0x119A8u, 0x119ACu, 0x16A00u, 0x16A08u,
                                    0x40400u, 0x40404u, 0x40500u, 0x40204u, 0x40200u, 0x1EEE008u};
    unsigned i;
    for (i = 0; i < sizeof addr / sizeof addr[0]; i++)
        if (addr[i] == a)
            return &regs[i];
    fprintf(stderr, "unexpected register 0x%X\n", (unsigned)a);
    exit(1);
}
#define REG32(a) (*reg(a))
#define RAMFN
#define SFC_CON REG32(0x40200u)
#define CACHE_CON REG32(0x1EEE008u)
#include "../../firmware/hal/fm1_clk320.h"

/* SYS on PLL2's VCO x 2/3 at x270 (540 MHz VCO, 2 MHz reference: 24 MHz / 12), HSB /2, LSB /3, on the PLL */
static void v15(void)
{
    memset(regs, 0, sizeof regs);
    REG32(0x10008u) = 0x00010200u;
    REG32(0x1000Cu) = 0x000001C1u;
    REG32(0x10014u) = 0x00000006u;
    REG32(0x119A0u) = 0x45400203u;
    REG32(0x119A8u) = 10u << 2;
    REG32(0x119ACu) = 268u;
}

/* SYS source 2: the main PLL's 320 MHz; PLL2 left at 0 */
static void v16(void)
{
    v15();
    REG32(0x10014u) = 0x00000002u;
    REG32(0x119A8u) = 0;
    REG32(0x119ACu) = 0;
}

int main(void)
{
    fm1_clk320_t p;

    v15();
    CHECK(fm1_clk320_mhz() == 360u);
    CHECK(!fm1_clk320_main());
    CHECK(fm1_clk320_check(&p) == 0);                  /* switched */
    CHECK((p.pll2_con1 & 0xFFFu) + 2u == 240u);

    v15();                                              /* a soft reset after a switch: PLL2 kept at x240 */
    REG32(0x119ACu) = 238u;
    CHECK(fm1_clk320_mhz() == 320u);
    CHECK(fm1_clk320_check(&p) == 1);

    v16();
    CHECK(fm1_clk320_mhz() == 320u);
    CHECK(fm1_clk320_main());
    CHECK(fm1_clk320_check(&p) == 1);                  /* already 320, not "SYS mode" */

    v16();                                              /* source 2 with a divider field set: not 320, refused */
    REG32(0x10014u) = 0x00000012u;
    CHECK(fm1_clk320_mhz() == 0u);
    CHECK(fm1_clk320_check(&p) == 2);

    v16();                                              /* source 2, SYS still on RC16M: not 320 */
    REG32(0x1000Cu) &= ~0x100u;
    CHECK(fm1_clk320_mhz() == 0u);
    CHECK(fm1_clk320_check(&p) == 2);

    v15();                                              /* PLL2 at x270 off another reference: not 360 */
    REG32(0x119A8u) = 4u << 2;
    CHECK(fm1_clk320_mhz() == 0u);
    CHECK(fm1_clk320_check(&p) == 4);

    v15();                                              /* source 6 at another multiplier: unknown, refused */
    REG32(0x119ACu) = 298u;
    CHECK(fm1_clk320_mhz() == 0u);
    CHECK(fm1_clk320_check(&p) == 3);

    puts("clk320: ok");
    return 0;
}
