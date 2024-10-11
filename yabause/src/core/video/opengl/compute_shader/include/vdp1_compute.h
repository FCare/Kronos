#ifndef VDP1_COMPUTE_H
#define VDP1_COMPUTE_H

#include "vdp1_prog_compute.h"
#include "vdp1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NB_LINE_MAX_PER_DRAW 512

enum
{
  WRITE = 0,
  READ,
  CLEAR,
  CLEAR_MESH,
  DRAW_POLY_MSB_SHADOW_NO_MESH,
  DRAW_POLY_REPLACE_NO_MESH,
  DRAW_POLY_SHADOW_NO_MESH,
  DRAW_POLY_HALF_LUMINANCE_NO_MESH,
  DRAW_POLY_HALF_TRANSPARENT_NO_MESH,
  DRAW_POLY_GOURAUD_NO_MESH,
  DRAW_POLY_UNSUPPORTED_NO_MESH,
  DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH,
  DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH,
  DRAW_QUAD_MSB_SHADOW_NO_MESH,
  DRAW_QUAD_REPLACE_NO_MESH,
  DRAW_QUAD_SHADOW_NO_MESH,
  DRAW_QUAD_HALF_LUMINANCE_NO_MESH,
  DRAW_QUAD_HALF_TRANSPARENT_NO_MESH,
  DRAW_QUAD_GOURAUD_NO_MESH,
  DRAW_QUAD_UNSUPPORTED_NO_MESH,
  DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH,
  DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH,
  DRAW_POLY_MSB_SHADOW_MESH,
  DRAW_POLY_REPLACE_MESH,
  DRAW_POLY_SHADOW_MESH,
  DRAW_POLY_HALF_LUMINANCE_MESH,
  DRAW_POLY_HALF_TRANSPARENT_MESH,
  DRAW_POLY_GOURAUD_MESH,
  DRAW_POLY_UNSUPPORTED_MESH,
  DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH,
  DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH,
  DRAW_QUAD_MSB_SHADOW_MESH,
  DRAW_QUAD_REPLACE_MESH,
  DRAW_QUAD_SHADOW_MESH,
  DRAW_QUAD_HALF_LUMINANCE_MESH,
  DRAW_QUAD_HALF_TRANSPARENT_MESH,
  DRAW_QUAD_GOURAUD_MESH,
  DRAW_QUAD_UNSUPPORTED_MESH,
  DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH,
  DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH,
  NB_PRG
};

extern void vdp1_compute_init(int width, int height, float ratio);
extern int get_vdp1_tex(int);
extern int get_vdp1_mesh(int);
extern int vdp1_add(vdp1cmd_struct* cmd, int clipcmd);
extern void vdp1_clear(int id, float *col, int* limits);
extern u32* vdp1_get_directFB();
extern void vdp1_setup(void);

extern void vdp1_compute_reset(void);
extern void vdp1_update_banding(void);
extern void vdp1_update_mesh(void);

#ifdef __cplusplus
}
#endif

#endif //VDP1_COMPUTE_H
