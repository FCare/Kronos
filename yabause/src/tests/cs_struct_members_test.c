/* Compile+run test: fields required by OpenGL compute-shader path (vidcs.c).
 * Fails to compile if Vdp1 / vdp2draw_struct / Ygl are missing members used
 * by VIDCSVdp1UserClipping*, Vdp2DrawNBG*, VIDCSReadColorOffset (Kronos#1596).
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "core.h"
#include "vdp1.h"
#include "vidshared.h"

#define HAVE_LIBGL 1
#define _USEGLEW_ 1
#define _OGL3_ 1
#include <GL/glew.h>
#include "ygl.h"

/* Mirror the assignments in vidcs.c — exercises real struct members. */
static u16 latch_userclip_mode(u16 cmdpmod)
{
    /* ST-013 §6.3: Cmod = CMDPMOD bit 9 */
    return (u16)((cmdpmod >> 9) & 0x1);
}

static void apply_vdp1_userclip(Vdp1 *regs, u16 cmdpmod)
{
    regs->userclipMode = latch_userclip_mode(cmdpmod);
}

static void apply_bitmap_window(vdp2draw_struct *info, u32 charaddr, u32 wrap)
{
    info->bitmap_base = 0;
    info->bitmap_wrap_size = 0;
    info->charaddr = charaddr;
    info->bitmap_wrap_size = wrap;
    info->bitmap_base = info->charaddr;
}

static void apply_ygl_line(Ygl *ygl, int line, int spclmd, int prio, u8 msb)
{
    ygl->sprite_rgb_priority_per_line[line] = spclmd ? prio : -1;
    ygl->msb_shadow_enabled_per_line[line] = msb;
}

int main(void)
{
    Vdp1 regs;
    vdp2draw_struct info;
    Ygl ygl;
    memset(&regs, 0, sizeof(regs));
    memset(&info, 0, sizeof(info));
    memset(&ygl, 0, sizeof(ygl));

    apply_vdp1_userclip(&regs, 0x0200u); /* Cmod=1 */
    if (regs.userclipMode != 1) {
        fprintf(stderr, "FAIL: userclipMode expected 1 got %u\n", (unsigned)regs.userclipMode);
        return 1;
    }
    apply_vdp1_userclip(&regs, 0x0000u);
    if (regs.userclipMode != 0) {
        fprintf(stderr, "FAIL: userclipMode expected 0 got %u\n", (unsigned)regs.userclipMode);
        return 1;
    }

    apply_bitmap_window(&info, 0x10000u, 0x80000u);
    if (info.bitmap_base != 0x10000u || info.bitmap_wrap_size != 0x80000u) {
        fprintf(stderr, "FAIL: bitmap window base=%u wrap=%u\n",
                (unsigned)info.bitmap_base, (unsigned)info.bitmap_wrap_size);
        return 1;
    }

    apply_ygl_line(&ygl, 10, 1, 5, 1);
    if (ygl.sprite_rgb_priority_per_line[10] != 5 || ygl.msb_shadow_enabled_per_line[10] != 1) {
        fprintf(stderr, "FAIL: ygl per-line state\n");
        return 1;
    }
    apply_ygl_line(&ygl, 10, 0, 5, 0);
    if (ygl.sprite_rgb_priority_per_line[10] != -1) {
        fprintf(stderr, "FAIL: palette path should set priority -1\n");
        return 1;
    }

    /* Touch offsetof so members cannot be optimized away at compile time only */
    printf("PASS offsetof userclipMode=%zu bitmap_base=%zu wrap=%zu spr=%zu msb=%zu\n",
           offsetof(Vdp1, userclipMode),
           offsetof(vdp2draw_struct, bitmap_base),
           offsetof(vdp2draw_struct, bitmap_wrap_size),
           offsetof(Ygl, sprite_rgb_priority_per_line),
           offsetof(Ygl, msb_shadow_enabled_per_line));
    return 0;
}
