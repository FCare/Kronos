#ifndef VDP1_COMPUTE_H
#define VDP1_COMPUTE_H

#include "vdp1_prog_compute.h"
#include "vdp1.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
  VDP1_MESH_STANDARD_BANDING = 0,
  VDP1_MESH_IMPROVED_BANDING,
  VDP1_MESH_STANDARD_NO_BANDING,
  VDP1_MESH_IMPROVED_NO_BANDING,
  WRITE,
  READ,
  CLEAR,
  CLEAR_MESH,
  DRAW_POLY_MSB_SHADOW,
  DRAW_POLY_REPLACE,
  DRAW_POLY_SHADOW,
  DRAW_POLY_HALF_LUMINANCE,
  DRAW_POLY_HALF_TRANSPARENT,
  DRAW_POLY_GOURAUD,
  DRAW_POLY_UNSUPPORTED,
  DRAW_POLY_GOURAUD_HALF_LUMINANCE,
  DRAW_POLY_GOURAUD_HALF_TRANSPARENT,
  DRAW_QUAD_MSB_SHADOW,
  DRAW_QUAD_REPLACE,
  DRAW_QUAD_SHADOW,
  DRAW_QUAD_HALF_LUMINANCE,
  DRAW_QUAD_HALF_TRANSPARENT,
  DRAW_QUAD_GOURAUD,
  DRAW_QUAD_UNSUPPORTED,
  DRAW_QUAD_GOURAUD_HALF_LUMINANCE,
  DRAW_QUAD_GOURAUD_HALF_TRANSPARENT,
  NB_PRG
};

extern void vdp1_compute_init(int width, int height, float ratiow, float ratioh);
extern void vdp1_compute();
extern int get_vdp1_tex(int);
extern int get_vdp1_mesh(int);
extern int vdp1_add(vdp1cmd_struct* cmd, int clipcmd);
extern void vdp1_clear(int id, float *col, int* limits);
extern u32* vdp1_get_directFB();
extern void vdp1_setup(void);

#ifdef __cplusplus
}
#endif

#endif //VDP1_COMPUTE_H
