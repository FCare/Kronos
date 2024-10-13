#include "vdp1_compute.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "vdp1.h"
#include "yui.h"

//#define VDP1CDEBUG
#ifdef VDP1CDEBUG
#define VDP1CPRINT printf
#else
#define VDP1CPRINT
#endif

typedef struct {
	u32 CMDPMOD;
	u32 CMDSRCA;
	u32 CMDSIZE;
	s32 CMDXA;
	s32 CMDYA;
	s32 CMDXB;
	s32 CMDYB;
	u32 CMDCOLR;
	u32 CMDCTRL;
	float dl;
	float dr;
	float G[16];
	u32 flip;
	u32 pad[4];
} cmd_poly;

extern vdp2rotationparameter_struct  Vdp1ParaA;

static int local_size_x = 8;
static int local_size_y = 8;


static int tex_width;
static int tex_height;
static int tex_ratio;
static int struct_size;
static int struct_line_size;
void drawPolygonLine(cmd_poly* cmd_pol, int nbLines, int nbPointsMax, u32 type);

static int work_groups_x;
static int work_groups_y;

static int cmdRam_update_start[2] = {0x0};
static int cmdRam_update_end[2] = {0x80000};

static int generateComputeBuffer(int w, int h);

static GLuint compute_tex[2] = {0};
static GLuint mesh_tex[2] = {0};
static GLuint ssbo_vdp1ram_ = 0;
static GLuint ssbo_vdp1access_ = 0;
static GLuint prg_vdp1[NB_PRG] = {0};

static GLuint ssbo_cmd_line_list_ = 0;

static u32 write_fb[2][512*256];

static const GLchar * a_prg_vdp1[NB_PRG][5] = {
	//WRITE
	{
		vdp1_write_f,
		NULL,
		NULL,
		NULL,
		NULL
	},
	//READ
	{
		vdp1_read_f,
		NULL,
		NULL,
		NULL,
		NULL
	},
	//CLEAR
	{
		vdp1_clear_f,
		NULL,
		NULL,
		NULL,
		NULL
  },
	//CLEAR_MESH
	{
		vdp1_clear_mesh_f,
		NULL,
		NULL,
		NULL,
		NULL
	},
	// DRAW_POLY_MSB_SHADOW_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_REPLACE_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_SHADOW_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_HALF_LUMINANCE_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_POLY_GOURAUD_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_POLY_UNSUPPORTED_NO_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_MSB_SHADOW_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_REPLACE_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_SHADOW_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_HALF_LUMINANCE_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_QUAD_GOURAUD_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_QUAD_UNSUPPORTED_NO_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_POLY_MSB_SHADOW_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_REPLACE_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_SHADOW_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_HALF_LUMINANCE_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_HALF_TRANSPARENT_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_POLY_GOURAUD_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_POLY_UNSUPPORTED_NO_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_MSB_SHADOW_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_REPLACE_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_SHADOW_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_HALF_LUMINANCE_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_HALF_TRANSPARENT_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_QUAD_GOURAUD_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
	// DRAW_QUAD_UNSUPPORTED_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
  // DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_f
	},
	//Same without end bit detection
	// DRAW_POLY_MSB_SHADOW_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_REPLACE_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_SHADOW_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_HALF_LUMINANCE_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_HALF_TRANSPARENT_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_GOURAUD_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_UNSUPPORTED_NO_MESH_NO_END
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
	// DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_MSB_SHADOW_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_REPLACE_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_SHADOW_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_HALF_LUMINANCE_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_HALF_TRANSPARENT_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_GOURAUD_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_UNSUPPORTED_NO_MESH_NO_END
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
	// DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_MSB_SHADOW_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_REPLACE_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_SHADOW_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_HALF_LUMINANCE_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_HALF_TRANSPARENT_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_GOURAUD_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_UNSUPPORTED_NO_MESH_NO_END
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
	// DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_MSB_SHADOW_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_REPLACE_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_SHADOW_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_HALF_LUMINANCE_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_HALF_TRANSPARENT_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_GOURAUD_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_UNSUPPORTED_MESH_NO_END
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
	// DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	},
	// DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH_NO_END
	{
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_line_no_end_f
	}
};

int ErrorHandle(const char* name)
{
#ifdef VDP1CDEBUG
  GLenum   error_code = glGetError();
  if (error_code == GL_NO_ERROR) {
    return  1;
  }
  do {
    const char* msg = "";
    switch (error_code) {
    case GL_INVALID_ENUM:      msg = "INVALID_ENUM";      break;
    case GL_INVALID_VALUE:     msg = "INVALID_VALUE";     break;
    case GL_INVALID_OPERATION: msg = "INVALID_OPERATION"; break;
    case GL_OUT_OF_MEMORY:     msg = "OUT_OF_MEMORY";     break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:  msg = "INVALID_FRAMEBUFFER_OPERATION"; break;
    default:  msg = "Unknown"; break;
    }
    YuiMsg("GLErrorLayer:ERROR:%04x'%s' %s\n", error_code, msg, name);
    error_code = glGetError();
  } while (error_code != GL_NO_ERROR);
  abort();
  return 0;
#else
  return 1;
#endif
}

static GLuint createProgram(int count, const GLchar** prg_strs) {
  GLint status;
	int exactCount = 0;
  GLuint result = glCreateShader(GL_COMPUTE_SHADER);

  for (int id = 0; id < count; id++) {
		if (prg_strs[id] != NULL) exactCount++;
		else break;
	}
  glShaderSource(result, exactCount, prg_strs, NULL);
  glCompileShader(result);
  glGetShaderiv(result, GL_COMPILE_STATUS, &status);

  if (status == GL_FALSE) {
    GLint length;
    glGetShaderiv(result, GL_INFO_LOG_LENGTH, &length);
    GLchar *info = (GLchar*)malloc(sizeof(GLchar) *length);
    glGetShaderInfoLog(result, length, NULL, info);
    YuiMsg("[COMPILE] %s\n", info);
    free(info);
    abort();
  }
  GLuint program = glCreateProgram();
  glAttachShader(program, result);
  glLinkProgram(program);
  glDetachShader(program, result);
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (status == GL_FALSE) {
    GLint length;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    GLchar *info = (GLchar*)malloc(sizeof(GLchar) *length);
    glGetProgramInfoLog(program, length, NULL, info);
    YuiMsg("[LINK] %s\n", info);
    free(info);
    abort();
  }
  return program;
}


static void regenerateMeshTex(int w, int h) {
	if (mesh_tex[0] != 0) {
		glDeleteTextures(2,&mesh_tex[0]);
	}
	glGenTextures(2, &mesh_tex[0]);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, mesh_tex[0]);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, mesh_tex[1]);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void vdp1_clear_mesh() {
	int progId = CLEAR_MESH;
	if (prg_vdp1[progId] == 0) {
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
  glUseProgram(prg_vdp1[progId]);
	glBindImageTexture(0, mesh_tex[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, mesh_tex[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glDispatchCompute(work_groups_x, work_groups_y, 1); //might be better to launch only the right number of workgroup
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
}

static int generateComputeBuffer(int w, int h) {
  if (compute_tex[0] != 0) {
    glDeleteTextures(2,&compute_tex[0]);
  }

	if (ssbo_vdp1ram_ != 0) {
    glDeleteBuffers(1, &ssbo_vdp1ram_);
	}
	regenerateMeshTex(w, h);

	glGenBuffers(1, &ssbo_vdp1ram_);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_vdp1ram_);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 0x80000, NULL, GL_DYNAMIC_DRAW);
	vdp1Ram_update_start = 0x0;
	vdp1Ram_update_end = 0x80000;
	Vdp1External.updateVdp1Ram = 1;

	if (ssbo_cmd_line_list_ != 0) {
		glDeleteBuffers(1, &ssbo_cmd_line_list_);
	}
	glGenBuffers(1, &ssbo_cmd_line_list_);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_line_list_);
	glBufferData(GL_SHADER_STORAGE_BUFFER, struct_line_size*NB_LINE_MAX_PER_DRAW, NULL, GL_DYNAMIC_DRAW);

	if (ssbo_vdp1access_ != 0) {
    glDeleteBuffers(1, &ssbo_vdp1access_);
	}

	glGenBuffers(1, &ssbo_vdp1access_);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_vdp1access_);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 512*256*4, NULL, GL_DYNAMIC_DRAW);

	float col[4] = {0.0};
	int limits[4] = {0, h, w, 0};
  glGenTextures(2, &compute_tex[0]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, compute_tex[0]);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	vdp1_clear(0, col, limits);
  glBindTexture(GL_TEXTURE_2D, compute_tex[1]);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	vdp1_clear(1, col, limits);

  return 0;
}

typedef struct{
	s32 x, y;
} point;

static int computeSmoothedLinePoints(int x1, int y1, int x2, int y2, point **data, int upscale) {
	int i, a, ax, ay, dx, dy;
	a = i = 0;
	x1 *= upscale;
	x2 *= upscale;
	y1 *= upscale;
	y2 *= upscale;
	dx = (x2 - x1);
	dy = (y2 - y1);
	ax = (dx >= 0) ? 1 : -1;
	ay = (dy >= 0) ? 1 : -1;
	// dx += ax * upscale;
	// dy += ay * upscale;
	int nbMaxPoint = MAX(abs(dx), abs(dy)) + upscale;
	// dx += ax*(upscale -1);
	// dy += ay*(upscale -1);
	// printf("%d %d (%d %d %d %d)\n", dx, dy, x1, x2, y1, y2);

	*data = (point*)malloc(nbMaxPoint*sizeof(point));
	if (abs(dx) >= abs(dy)) {
		x2 += ax*(upscale -1);
		// x2 += ax*(upscale -1);
		if (ax != ay) dx = -dx;

		for (i = 0; x1 != x2; x1 += ax, i++) {
			(*data)[i] = (point){.x=x1, .y=y1};
			// printf("%d P %d,%d\n", __LINE__, x1, y1);
			a += dy;
			if (abs(a) >= abs(dx)) {
				a -= dx;
				y1 += ay;
			}
		}
		(*data)[i++] = (point){.x=x2, .y=y2};
		// printf("%d P %d,%d\n", __LINE__, x2, y2);
	} else {
		y2 += ay*(upscale -1);
		// y2 += ay*(upscale -1);
		if (ax != ay) dy = -dy;
		for (i = 0; y1 != y2; y1 += ay, i++) {
      (*data)[i] = (point){.x=x1, .y=y1};
			// printf("%d P %d,%d\n", __LINE__, x1, y1);
			a += dx;
			if (abs(a) >= abs(dy)) {
				a -= dy;
				x1 += ax;
			}
		}
		(*data)[i++] = (point){.x=x2, .y=y2};
		// printf("%d P %d,%d\n", __LINE__, x2, y2);
	}

	if (i != nbMaxPoint) {
		printf("Error %d,%d => %d %d,%d => %d %d => %d\n", x1, x2, dx,y1, y2, dy, i, nbMaxPoint);
		exit(-1);
	}
	return i;
}

static int computeLinePoints(int x1, int y1, int x2, int y2, point **data, int upscale) {
	int i, a, ax, ay, dx, dy;
	a = i = 0;
	dx = (x2 - x1);
	dy = (y2 - y1);
	ax = (dx >= 0) ? 1 : -1;
	ay = (dy >= 0) ? 1 : -1;
	// dx += ax * upscale;
	// dy += ay * upscale;
	int nbMaxPoint = (MAX(abs(dx), abs(dy)) + 1)*upscale;
	// x1 *= upscale;
	// x2 *= upscale;
	// y1 *= upscale;
	// y2 *= upscale;
	// dx *= upscale;
	// dy *= upscale;
	// dy += ay*(upscale -1);
	// printf("%d %d (%d %d %d %d) %d\n", dx, dy, x1, x2, y1, y2, nbMaxPoint);

	*data = (point*)malloc(nbMaxPoint*sizeof(point));
	if (abs(dx) >= abs(dy)) {
		// if (dx == 0) x2 += ax*upscale;
		if (ax != ay) dx = -dx;
		for (i = 0; x1 != x2; x1 += ax) {
			// printf("Line\n");
			// printf("X1 %d X2 %d\n", x1, x2);
			for (int p=0; p<upscale; p++) {
				(*data)[i++] = (point){.x=x1*upscale, .y=y1*upscale+ay*p};
				// printf("%d P %d,%d\n", __LINE__, x1*upscale, y1*upscale+ay*p);
			}
			a += dy;
			if (abs(a) >= abs(dx)) {
				a -= dx;
				y1 += ay;
			}
		}
		for (int p=0; p< upscale; p++) {
			(*data)[i++] = (point){.x=x2*upscale, .y=y2*upscale+ay*p};
			// printf("%d P %d,%d\n", __LINE__, x2*upscale, y2*upscale+ay*p);
		}
	} else {
		// if (dy == 0) y2 += ay*upscale;
		if (ax != ay) dy = -dy;
		for (i = 0; y1 != y2; y1 += ay) {
			// printf("Line\n");
			// printf("Y1 %d Y2 %d\n", y1, y2);
			for (int p=0; p< upscale; p++) {
				(*data)[i++] = (point){.x=x1*upscale+ax*p, .y=y1*upscale};
				// printf("%d P %d,%d\n", __LINE__, x1*upscale+ax*p, y1*upscale);
			}
			a += dx;
			if (abs(a) >= abs(dy)) {
				a -= dy;
				x1 += ax;
			}
		}
		for (int p=0; p<upscale; p++) {
			(*data)[i++] = (point){.x=x2*upscale+ax*p, .y=y2*upscale};
			// printf("%d P %d,%d\n", __LINE__, x2*upscale+ax*p, y2*upscale);
		}
	}

	if (i != nbMaxPoint) {
		printf("Error %d,%d => %d %d,%d => %d %d => %d\n", x1, x2, dx,y1, y2, dy, i, nbMaxPoint);
		exit(-1);
	}
	return i;
}

static void drawQuad(vdp1cmd_struct* cmd) {
	point *dataL, *dataR;
	// printf("Quad\n");
	int nbPmax = 0;
	int li = computeSmoothedLinePoints(cmd->CMDXA, cmd->CMDYA, cmd->CMDXD, cmd->CMDYD, &dataL, tex_ratio);
	int ri = computeSmoothedLinePoints(cmd->CMDXB, cmd->CMDYB, cmd->CMDXC, cmd->CMDYC, &dataR, tex_ratio);
	int nbCmd = MAX(li,ri);
	cmd_poly *cmd_pol = (cmd_poly*)calloc(nbCmd, sizeof(cmd_poly));
	int idl = 0;
	int idr = 0;
	int a = 0;
	int i = 0;
	if(li>ri) {
		for (i = 0; i != li; i++) {
			a += ri;
			idl = i;
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = dataL[idl].x,
				.CMDYA = dataL[idl].y,
				.CMDXB = dataR[idr].x,
				.CMDYB = dataR[idr].y,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = (float)((idl/tex_ratio)+0.5)/(float)(li/tex_ratio),
				.dr = (float)((idr/tex_ratio)+0.5)/(float)(ri/tex_ratio),
				.flip = cmd->flip,
			};
			nbPmax = MAX(nbPmax, MAX(abs(dataL[idl].x-dataR[idr].x), abs(dataL[idl].y-dataR[idr].y)));
			// printf("P %d,%d => %d,%d\n",
			// 	cmd_pol[i].CMDXA,cmd_pol[i].CMDYA,
			// 	cmd_pol[i].CMDXB,cmd_pol[i].CMDYB
			// );
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
			if (abs(a) >= abs(li)) {
				a -= li;
				idr++;
			}
		}
	} else {
		for (i = 0; i != ri; i++) {
			a += li;
			idr = i;
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = dataL[idl].x,
				.CMDYA = dataL[idl].y,
				.CMDXB = dataR[idr].x,
				.CMDYB = dataR[idr].y,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = (float)((idl/tex_ratio)+0.5)/(float)(li/tex_ratio),
				.dr = (float)((idr/tex_ratio)+0.5)/(float)(ri/tex_ratio),
				.flip = cmd->flip,
			};
			nbPmax = MAX(nbPmax, MAX(abs(dataL[idl].x-dataR[idr].x), abs(dataL[idl].y-dataR[idr].y)));
			// printf("P %d,%d => %d,%d\n",
			// 	cmd_pol[i].CMDXA,cmd_pol[i].CMDYA,
			// 	cmd_pol[i].CMDXB,cmd_pol[i].CMDYB
			// );
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));

			if (abs(a) >= abs(ri)) {
				a -= ri;
				idl++;
			}
		}
	}
	drawPolygonLine(cmd_pol, i, nbPmax+tex_ratio,cmd->type);
	free(cmd_pol);
	free(dataL);
	free(dataR);
}

void drawPoint(vdp1cmd_struct* cmd) {
	cmd_poly *cmd_pol = (cmd_poly*)calloc(tex_ratio, sizeof(cmd_poly));
	for (int i = 0; i< tex_ratio; i++) {
		cmd_pol[i] = (cmd_poly){
			.CMDPMOD = cmd->CMDPMOD,
			.CMDSRCA = cmd->CMDSRCA,
			.CMDSIZE = cmd->CMDSIZE,
			.CMDXA = cmd->CMDXA * tex_ratio,
			.CMDYA = cmd->CMDYA * tex_ratio + i,
			.CMDXB = cmd->CMDXB * tex_ratio,
			.CMDYB = cmd->CMDYB * tex_ratio + i,
			.CMDCOLR = cmd->CMDCOLR,
			.CMDCTRL = cmd->CMDCTRL,
			.dl = 0.5,
			.dr = 0.5,
			.flip = cmd->flip,
		};
		memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
	}
	drawPolygonLine(cmd_pol, tex_ratio, tex_ratio, cmd->type);
	free(cmd_pol);
}
void drawLine(vdp1cmd_struct* cmd, point A, point B) {
	int dx = B.x - A.x;
	int dy = B.y - A.y;
	cmd_poly *cmd_pol = (cmd_poly*)calloc(tex_ratio, sizeof(cmd_poly));
	if (dx >= dy) {
		for (int i = 0; i< tex_ratio; i++) {
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = A.x * tex_ratio,
				.CMDYA = A.y * tex_ratio + i,
				.CMDXB = B.x * tex_ratio,
				.CMDYB = B.y * tex_ratio + i,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = 0.5,
				.dr = 0.5,
				.flip = cmd->flip,
			};
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
		}
	} else {
		for (int i = 0; i< tex_ratio; i++) {
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = A.x * tex_ratio + i,
				.CMDYA = A.y * tex_ratio,
				.CMDXB = B.x* tex_ratio + i,
				.CMDYB = B.y * tex_ratio,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = 0.5,
				.dr = 0.5,
				.flip = cmd->flip,
			};
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
		}
	}
	drawPolygonLine(cmd_pol, tex_ratio, MAX(dx, dy)*tex_ratio,cmd->type);
	free(cmd_pol);
}


void drawQuadAsLine(vdp1cmd_struct* cmd) {
	int maxX = MAX(cmd->CMDXA, MAX(cmd->CMDXB, MAX(cmd->CMDXC, cmd->CMDXD)));
	int minX = MIN(cmd->CMDXA, MIN(cmd->CMDXB, MIN(cmd->CMDXC, cmd->CMDXD)));
	int maxY = MAX(cmd->CMDYA, MAX(cmd->CMDYB, MAX(cmd->CMDYC, cmd->CMDYD)));
	int minY = MIN(cmd->CMDYA, MIN(cmd->CMDYB, MIN(cmd->CMDYC, cmd->CMDYD)));
	int dx = maxX - minX;
	int dy = maxY - minY;
	cmd_poly *cmd_pol = (cmd_poly*)calloc(tex_ratio, sizeof(cmd_poly));
	if (dx >= dy) {
		for (int i = 0; i< tex_ratio; i++) {
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = minX * tex_ratio,
				.CMDYA = minY * tex_ratio + i,
				.CMDXB = maxX * tex_ratio,
				.CMDYB = maxY * tex_ratio + i,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = 0.0,
				.dr = 1.0,
				.flip = cmd->flip,
			};
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
		}
	} else {
		for (int i = 0; i< tex_ratio; i++) {
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = minX * tex_ratio + i,
				.CMDYA = minY * tex_ratio,
				.CMDXB = maxX * tex_ratio + i,
				.CMDYB = maxY * tex_ratio,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = 0.0,
				.dr = 1.0,
				.flip = cmd->flip,
			};
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
		}
	}
	drawPolygonLine(cmd_pol, tex_ratio, MAX(dx, dy)*tex_ratio, cmd->type);
	free(cmd_pol);
}

void drawHalfLine(vdp1cmd_struct* cmd) {
	//One of the most complicated shape to scale.
	//It is globally a line, with one of the point out of the line
	// Draw as original size and duplicates lines
	point *dataL, *dataR;
	int nbPmax = 0;
	int li = computeLinePoints(cmd->CMDXA, cmd->CMDYA, cmd->CMDXD, cmd->CMDYD, &dataL, tex_ratio);
	int ri = computeLinePoints(cmd->CMDXB, cmd->CMDYB, cmd->CMDXC, cmd->CMDYC, &dataR, tex_ratio);
	// printf("Half Line %d %d\n", li, ri);
	//Draw as size one and duplicate lines depending the orientation of the line
	int nbCmd = MAX(li,ri);
	cmd_poly *cmd_pol = (cmd_poly*)calloc(nbCmd, sizeof(cmd_poly));
	int idl = 0;
	int idr = 0;
	int a = 0;
	int i = 0;
	if(li>ri) {
		for (i = 0; i != li; i++) {
			a += ri;
			idl = i;
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = dataL[idl].x,
				.CMDYA = dataL[idl].y,
				.CMDXB = dataR[idr].x,
				.CMDYB = dataR[idr].y,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = (li>1)?((float)(idl/tex_ratio))/(float)((li/tex_ratio)-1):0.5,
				.dr = (ri>1)?((float)(idr/tex_ratio))/(float)((ri/tex_ratio)-1):0.5,
				.flip = cmd->flip,
			};
			// printf("P %d,%d => %d,%d\n",
			// 	cmd_pol[i].CMDXA,cmd_pol[i].CMDYA,
			// 	cmd_pol[i].CMDXB,cmd_pol[i].CMDYB
			// );
			nbPmax = MAX(nbPmax, MAX(abs(dataL[idl].x-dataR[idr].x), abs(dataL[idl].y-dataR[idr].y)));
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));
			if (abs(a) >= abs(li)) {
				a -= li;
				idr++;
			}
		}
	} else {
		for (i = 0; i != ri; i++) {
			a += li;
			idr = i;
			cmd_pol[i] = (cmd_poly){
				.CMDPMOD = cmd->CMDPMOD,
				.CMDSRCA = cmd->CMDSRCA,
				.CMDSIZE = cmd->CMDSIZE,
				.CMDXA = dataL[idl].x,
				.CMDYA = dataL[idl].y,
				.CMDXB = dataR[idr].x,
				.CMDYB = dataR[idr].y,
				.CMDCOLR = cmd->CMDCOLR,
				.CMDCTRL = cmd->CMDCTRL,
				.dl = (li>1)?((float)(idl/tex_ratio))/(float)((li/tex_ratio)-1):0.5,
				.dr = (ri>1)?((float)(idr/tex_ratio))/(float)((ri/tex_ratio)-1):0.5,
				.flip = cmd->flip,
			};
			// printf("P %d,%d => %d,%d\n",
			// 	cmd_pol[i].CMDXA,cmd_pol[i].CMDYA,
			// 	cmd_pol[i].CMDXB,cmd_pol[i].CMDYB
			// );
			nbPmax = MAX(nbPmax, MAX(abs(dataL[idl].x-dataR[idr].x), abs(dataL[idl].y-dataR[idr].y)));
			memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));

			if (abs(a) >= abs(ri)) {
				a -= ri;
				idl++;
			}
		}
	}
	drawPolygonLine(cmd_pol, i, nbPmax+tex_ratio, cmd->type);
	free(cmd_pol);
	free(dataL);
	free(dataR);
}

int isPoint(vdp1cmd_struct* cmd) {
	return (cmd->CMDXA == cmd->CMDXB) &&
		 (cmd->CMDXC == cmd->CMDXD) &&
		 (cmd->CMDXA == cmd->CMDXC);
}

int colinear(point a, point b) {
	int lengtha = a.x*a.x + a.y*a.y;
	int lengthb = b.x*b.x + b.y*b.y;
	if ((lengtha == 0)||(lengthb == 0)) return 0;
	return ((a.x*b.x+a.y*b.y)*(a.x*b.x+a.y*b.y) == lengtha*lengthb);
}

int isLine(vdp1cmd_struct* cmd) {
	point v1 = (point){
		.x = cmd->CMDXB - cmd->CMDXA,
		.y = cmd->CMDYB - cmd->CMDYA
	};
	point v2 = (point){
		.x = cmd->CMDXC - cmd->CMDXA,
		.y = cmd->CMDYC - cmd->CMDYA
	};
	point v3 = (point){
		.x = cmd->CMDXD - cmd->CMDXC,
		.y = cmd->CMDYD - cmd->CMDYC
	};
	point v4 = (point){
		.x = cmd->CMDXB - cmd->CMDXC,
		.y = cmd->CMDYB - cmd->CMDYC
	};
	if ((colinear(v1, v2)==1) && (colinear(v3,v4)==1)) return 1;
	if ((colinear(v1, v4)==1) && (colinear(v2,v3)==1)) return 1;
	if ((colinear(v1, v3)==1) && (cmd->CMDXA==cmd->CMDXC) && (cmd->CMDYA==cmd->CMDYC) && (cmd->CMDXB==cmd->CMDXD) && (cmd->CMDYB==cmd->CMDYD)) return 1;
	if ((colinear(v1, v3)==1) && (cmd->CMDXA==cmd->CMDXD) && (cmd->CMDYA==cmd->CMDYD) && (cmd->CMDXB==cmd->CMDXC) && (cmd->CMDYB==cmd->CMDYC)) return 1;

	return 0;
}

int ishalfLine(vdp1cmd_struct* cmd) {
	//We know that we are not a line, so return true in case of one colinear vector
	point v1 = (point){
		.x = cmd->CMDXB - cmd->CMDXA,
		.y = cmd->CMDYB - cmd->CMDYA
	};
	point v2 = (point){
		.x = cmd->CMDXD - cmd->CMDXA,
		.y = cmd->CMDYD - cmd->CMDYA
	};
	point v3 = (point){
		.x = cmd->CMDXD - cmd->CMDXC,
		.y = cmd->CMDYD - cmd->CMDYC
	};
	point v4 = (point){
		.x = cmd->CMDXB - cmd->CMDXC,
		.y = cmd->CMDYB - cmd->CMDYC
	};
	if (colinear(v1, v2)==1) return 1;
	if (colinear(v1, v4)==1) return 1;
	if (colinear(v2, v3)==1) return 1;
	if (colinear(v3, v4)==1) return 1;
	return 0;
}

int vdp1_add(vdp1cmd_struct* cmd, int clipcmd) {
	int minx = 1024;
	int miny = 1024;
	int maxx = 0;
	int maxy = 0;

	int intersectX = -1;
	int intersectY = -1;
	int requireCompute = 0;

	if (_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] != -1) {
		endVdp1Render();
		vdp1_write();
		startVdp1Render();
		_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = -1;
	}

	if (clipcmd != 0)startVdp1Render();

	if (_Ygl->wireframe_mode != 0) {
		int pos = (cmd->CMDSRCA * 8) & 0x7FFFF;
		switch(cmd->type ) {
			case DISTORTED:
			//By default use the central pixel code
			switch ((cmd->CMDPMOD >> 3) & 0x7) {
				case 0:
				case 1:
				  pos += (cmd->h/2) * cmd->w/2 + cmd->w/4;
					break;
				case 2:
				case 3:
				case 4:
					pos += (cmd->h/2) * cmd->w + cmd->w/2;
					break;
				case 5:
					pos += (cmd->h/2) * cmd->w*2 + cmd->w;
					break;
			}
			// cmd->COLOR[0] = cmdBuffer[_Ygl->drawframe][pos];
			cmd->type = POLYLINE;
			break;
			case POLYGON:
				cmd->type = POLYLINE;
			break;
			case QUAD:
			case QUAD_POLY:
				// if ((abs(cmd->CMDXA - cmd->CMDXB) <= ((2*_Ygl->rwidth)/3)) && (abs(cmd->CMDYA - cmd->CMDYD) <= ((_Ygl->rheight)/2)))
					cmd->type = POLYLINE;
					cmd->CMDCOLR = 0xACE1;
			break;
			default:
				break;
		}
	}

	if ((cmd->type == POLYGON)||(cmd->type == DISTORTED)||(cmd->type == QUAD)) {
		//POINT
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 120;
		// cmd->CMDXC = 120;
		// cmd->CMDXD = 120;
		// cmd->CMDYA = 130;
		// cmd->CMDYB = 130;
		// cmd->CMDYC = 130;
		// cmd->CMDYD = 130;
		//QUAD
		// cmd->CMDCTRL &= ~0xF;
		// // cmd->CMDCOLR = 0x7FFF;
		// cmd->CMDXA = 100;
		// cmd->CMDXD = 100;
		// cmd->CMDXC = 107;
		// cmd->CMDXB = 107;
		// cmd->CMDYA = 100;
		// cmd->CMDYD = 105;
		// cmd->CMDYC = 105;
		// cmd->CMDYB = 100;
		//LINE
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 130;
		// cmd->CMDXC = 140;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 130;
		// cmd->CMDYB = 140;
		// cmd->CMDYC = 150;
		// cmd->CMDYD = 160;
		//TRIANGLE
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 120;
		// cmd->CMDXC = 90;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 130;
		// cmd->CMDYB = 130;
		// cmd->CMDYC = 160;
		// cmd->CMDYD = 160;
		// CONCAVE
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 90;
		// cmd->CMDXC = 120;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 130;
		// cmd->CMDYB = 160;
		// cmd->CMDYC = 130;
		// cmd->CMDYD = 160;
		// SEGA RALLY
		// cmd->CMDXA = 12;
		// cmd->CMDXB = 17;
		// cmd->CMDXC = 19;
		// cmd->CMDXD = 14;
		// cmd->CMDYA = 45;
		// cmd->CMDYB = 45;
		// cmd->CMDYC = 44;
		// cmd->CMDYD = 45;
		//AIGUILLE SEGA RALLY // Triangle
		// cmd->CMDXA = 176-125;
		// cmd->CMDXB = 176-112;
		// cmd->CMDXC = 176-127;
		// cmd->CMDXD = 176-127;
		// cmd->CMDYA = 116+75;
		// cmd->CMDYB = 116+46;
		// cmd->CMDYC = 116+74;
		// cmd->CMDYD = 116+74;
		//DOOM Line
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 176;
		// cmd->CMDXC = 176;
		// cmd->CMDXD = 120;
		// cmd->CMDYA = 129;
		// cmd->CMDYB = 129;
		// cmd->CMDYC = 129;
		// cmd->CMDYD = 129;
		//DOOM Line
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 176;
		// cmd->CMDXC = 120;
		// cmd->CMDXD = 176;
		// cmd->CMDYA = 129;
		// cmd->CMDYB = 129;
		// cmd->CMDYC = 129;
		// cmd->CMDYD = 129;

		//Need to detect lines for sega rally or break point since quad as line are only one pixel wide potentially
		// drawLine(cmd);
		if (isPoint(cmd))
			 drawPoint(cmd);
		else
		if (isLine(cmd)){
			// printf("Line detected %d,%d %d,%d %d,%d %d,%d\n",
			// 	cmd->CMDXA,
			// 	cmd->CMDYA,
			// 	cmd->CMDXB,
			// 	cmd->CMDYB,
			// 	cmd->CMDXC,
			// 	cmd->CMDYC,
			// 	cmd->CMDXD,
			// 	cmd->CMDYD
			// );
			drawQuadAsLine(cmd);
		}
		else if (ishalfLine(cmd))
			drawHalfLine(cmd);
		else
			drawQuad(cmd);
	}

	if (cmd->type == POLYLINE) {
		// drawPolyLine(cmd);
		drawLine(cmd, (point){.x=cmd->CMDXA, .y=cmd->CMDYA}, (point){.x=cmd->CMDXB, .y=cmd->CMDYB});
		drawLine(cmd, (point){.x=cmd->CMDXB, .y=cmd->CMDYB}, (point){.x=cmd->CMDXC, .y=cmd->CMDYC});
		drawLine(cmd, (point){.x=cmd->CMDXC, .y=cmd->CMDYC}, (point){.x=cmd->CMDXD, .y=cmd->CMDYD});
		drawLine(cmd, (point){.x=cmd->CMDXD, .y=cmd->CMDYD}, (point){.x=cmd->CMDXA, .y=cmd->CMDYA});
	}

	if (cmd->type == LINE) {
	  drawLine(cmd, (point){.x=cmd->CMDXA, .y=cmd->CMDYA}, (point){.x=cmd->CMDXB, .y=cmd->CMDYB});
	}

  return 0;
}

static void trace_prog(int progId) {
	switch(progId) {
		case DRAW_POLY_MSB_SHADOW_NO_MESH:
			printf("DRAW_POLY_MSB_SHADOW_NO_MESH\n");
		break;
	  case DRAW_POLY_REPLACE_NO_MESH:
			printf("DRAW_POLY_REPLACE_NO_MESH\n");
		break;
	  case DRAW_POLY_SHADOW_NO_MESH:
			printf("DRAW_POLY_SHADOW_NO_MESH\n");
		break;
	  case DRAW_POLY_HALF_LUMINANCE_NO_MESH:
			printf("DRAW_POLY_HALF_LUMINANCE_NO_MESH\n");
		break;
	  case DRAW_POLY_HALF_TRANSPARENT_NO_MESH:
			printf("DRAW_POLY_HALF_TRANSPARENT_NO_MESH\n");
		break;
	  case DRAW_POLY_GOURAUD_NO_MESH:
			printf("DRAW_POLY_GOURAUD_NO_MESH\n");
		break;
	  case DRAW_POLY_UNSUPPORTED_NO_MESH:
			printf("DRAW_POLY_UNSUPPORTED_NO_MESH\n");
		break;
	  case DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH:
			printf("DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH\n");
		break;
	  case DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH:
			printf("DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH\n");
		break;
	  case DRAW_QUAD_MSB_SHADOW_NO_MESH:
			printf("DRAW_QUAD_MSB_SHADOW_NO_MESH\n");
		break;
	  case DRAW_QUAD_REPLACE_NO_MESH:
			printf("DRAW_QUAD_REPLACE_NO_MESH\n");
		break;
	  case DRAW_QUAD_SHADOW_NO_MESH:
			printf("DRAW_QUAD_SHADOW_NO_MESH\n");
		break;
	  case DRAW_QUAD_HALF_LUMINANCE_NO_MESH:
			printf("DRAW_QUAD_HALF_LUMINANCE_NO_MESH\n");
		break;
	  case DRAW_QUAD_HALF_TRANSPARENT_NO_MESH:
			printf("DRAW_QUAD_HALF_TRANSPARENT_NO_MESH\n");
		break;
	  case DRAW_QUAD_GOURAUD_NO_MESH:
			printf("DRAW_QUAD_GOURAUD_NO_MESH\n");
		break;
	  case DRAW_QUAD_UNSUPPORTED_NO_MESH:
			printf("DRAW_QUAD_UNSUPPORTED_NO_MESH\n");
		break;
		case DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH:
			printf("DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH\n");
		break;
	  case DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH:
			printf("DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH\n");
		break;
		case DRAW_POLY_MSB_SHADOW_MESH:
			printf("DRAW_POLY_MSB_SHADOW_MESH\n");
		break;
	  case DRAW_POLY_REPLACE_MESH:
			printf("DRAW_POLY_REPLACE_MESH\n");
		break;
	  case DRAW_POLY_SHADOW_MESH:
			printf("DRAW_POLY_SHADOW_MESH\n");
		break;
	  case DRAW_POLY_HALF_LUMINANCE_MESH:
			printf("DRAW_POLY_HALF_LUMINANCE_MESH\n");
		break;
	  case DRAW_POLY_HALF_TRANSPARENT_MESH:
			printf("DRAW_POLY_HALF_TRANSPARENT_MESH\n");
		break;
	  case DRAW_POLY_GOURAUD_MESH:
			printf("DRAW_POLY_GOURAUD_MESH\n");
		break;
	  case DRAW_POLY_UNSUPPORTED_MESH:
			printf("DRAW_POLY_UNSUPPORTED_MESH\n");
		break;
	  case DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH:
			printf("DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH\n");
		break;
	  case DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH:
			printf("DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH\n");
		break;
	  case DRAW_QUAD_MSB_SHADOW_MESH:
			printf("DRAW_QUAD_MSB_SHADOW_MESH\n");
		break;
	  case DRAW_QUAD_REPLACE_MESH:
			printf("DRAW_QUAD_REPLACE_MESH\n");
		break;
	  case DRAW_QUAD_SHADOW_MESH:
			printf("DRAW_QUAD_SHADOW_MESH\n");
		break;
	  case DRAW_QUAD_HALF_LUMINANCE_MESH:
			printf("DRAW_QUAD_HALF_LUMINANCE_MESH\n");
		break;
	  case DRAW_QUAD_HALF_TRANSPARENT_MESH:
			printf("DRAW_QUAD_HALF_TRANSPARENT_MESH\n");
		break;
	  case DRAW_QUAD_GOURAUD_MESH:
			printf("DRAW_QUAD_GOURAUD_MESH\n");
		break;
	  case DRAW_QUAD_UNSUPPORTED_MESH:
			printf("DRAW_QUAD_UNSUPPORTED_MESH\n");
		break;
		case DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH:
			printf("DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH\n");
		break;
	  case DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH:
			printf("DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH\n");
		break;
		default: printf("Not a vdp1 prog %d\n", progId);
	}
}

static int getProgramLine(cmd_poly* cmd_pol, int type){
	int progId = DRAW_POLY_MSB_SHADOW_NO_MESH;
	if((type == DISTORTED) || (type == QUAD)) {
		progId = DRAW_QUAD_MSB_SHADOW_NO_MESH;
	}
	int delta = 0;
	if ((cmd_pol->CMDPMOD & 0x8000) == 0) {
		delta += 1;
		if ((Vdp1Regs->TVMR & 0x1)==0) {
			// Color calculation is working only on framebuffer 16 bits
		  delta += (cmd_pol->CMDPMOD & 0x7);
		}
	}

	if (cmd_pol->CMDPMOD & 0x0100)
		delta += DRAW_POLY_MSB_SHADOW_MESH - DRAW_POLY_MSB_SHADOW_NO_MESH;
	if ((cmd_pol->CMDPMOD & 0x80)!=0) {
		//It shoould take care of HSS bit too and upscale or shrink
		delta += DRAW_POLY_MSB_SHADOW_NO_MESH_NO_END - DRAW_POLY_MSB_SHADOW_NO_MESH;
	}


	return progId+delta;
}

void vdp1_update_banding(void) {
	//Change char * reference et incvalide les progIds
	a_prg_vdp1[DRAW_POLY_GOURAUD_NO_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_NO_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH][2] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	vdp1_compute_reset();
}

void vdp1_update_mesh(void) {
	for (int i=DRAW_POLY_MSB_SHADOW_NO_MESH; i<=DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH; i++) {
		a_prg_vdp1[i][3] = (_Ygl->meshmode == IMPROVED_MESH)?vdp1_draw_no_mesh_improved_f:vdp1_draw_no_mesh_f;
	}
	for (int i=DRAW_POLY_MSB_SHADOW_MESH; i<=DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH; i++) {
		a_prg_vdp1[i][3] = (_Ygl->meshmode == IMPROVED_MESH)?vdp1_draw_improved_mesh_f:vdp1_draw_mesh_f;
	}
	vdp1_compute_reset();
}

static int oldProg = -1;

void startVdp1Render() {
	if (oldProg == -1) return;
	glUseProgram(prg_vdp1[oldProg]);
	glBindImageTexture(0, compute_tex[_Ygl->drawframe], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	if (_Ygl->meshmode == IMPROVED_MESH) glBindImageTexture(1, get_vdp1_mesh(_Ygl->drawframe), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo_cmd_line_list_);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo_vdp1ram_);
	glUniform2i(7, tex_ratio, tex_ratio);
	glUniform2i(8, (Vdp1Regs->systemclipX2+1)*tex_ratio-1, (Vdp1Regs->systemclipY2+1)*tex_ratio-1);
	glUniform4i(9, Vdp1Regs->userclipX1*tex_ratio, Vdp1Regs->userclipY1*tex_ratio, (Vdp1Regs->userclipX2+1)*tex_ratio-1, (Vdp1Regs->userclipY2+1)*tex_ratio-1);
}

static void flushVdp1Render(int nbWork, int nbPoints) {
	if (nbWork>0) {
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		glDispatchCompute(nbWork, nbPoints, 1); //might be better to launch only the right number of workgroup
	}
}

void endVdp1Render() {
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void drawPolygonLine(cmd_poly* cmd_pol, int nbLines, int nbPointsMax, u32 type) {
	int progId = getProgramLine(&cmd_pol[0], type);
	// trace_prog(progId);
	if (progId == DRAW_POLY_UNSUPPORTED_MESH) return;
	if (progId == DRAW_POLY_UNSUPPORTED_NO_MESH) return;
	if (progId == DRAW_QUAD_UNSUPPORTED_MESH) return;
	if (progId == DRAW_QUAD_UNSUPPORTED_NO_MESH) return;

	if (progId < DRAW_POLY_MSB_SHADOW_NO_MESH_NO_END) nbPointsMax = 1;

	if (prg_vdp1[progId] == 0) {
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
	if (oldProg != progId) {
		// 	Might be some stuff to clean
			oldProg = progId;
			startVdp1Render();
	}

	if (Vdp1External.updateVdp1Ram != 0) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_vdp1ram_);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, vdp1Ram_update_start, vdp1Ram_update_end-vdp1Ram_update_start, (void*)&Vdp1Ram[vdp1Ram_update_start]);
		vdp1Ram_update_start = 0x80000;
		vdp1Ram_update_end = 0x0;
		Vdp1External.updateVdp1Ram = 0;
	}

	for (int i = 0; i<nbLines; i+=NB_LINE_MAX_PER_DRAW) {
		int nbUpload = MIN(NB_LINE_MAX_PER_DRAW,(nbLines - i));
		// if ((buffer_pos + nbUpload) >= NB_LINE_MAX_PER_DRAW) flushVdp1Render();
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_line_list_);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, nbUpload*sizeof(cmd_poly), (void*)&cmd_pol[i]);
		flushVdp1Render(nbUpload, nbPointsMax);
	}

	// glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void vdp1_clear(int id, float *col, int* lim) {
	int progId = CLEAR;
	int limits[4];
	memcpy(limits, lim, 4*sizeof(int));
	if (prg_vdp1[progId] == 0) {
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
	limits[0] = limits[0]*_Ygl->vdp1width/512;
	limits[1] = limits[1]*_Ygl->vdp1height/256;
	limits[2] = limits[2]*_Ygl->vdp1width/512+tex_ratio-1;
	limits[3] = limits[3]*_Ygl->vdp1height/256+tex_ratio-1;
  glUseProgram(prg_vdp1[progId]);
	glBindImageTexture(0, get_vdp1_tex(id), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, get_vdp1_mesh(id), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glUniform4fv(2, 1, col);
	glUniform4iv(3, 1, limits);
	glDispatchCompute(work_groups_x, work_groups_y, 1); //might be better to launch only the right number of workgroup
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
}

void vdp1_write() {
	int progId = WRITE;
	float ratio = 1.0f/_Ygl->vdp1ratio;

	if (prg_vdp1[progId] == 0) {
    prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
  glUseProgram(prg_vdp1[progId]);

	glBindImageTexture(0, get_vdp1_tex(_Ygl->drawframe), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, _Ygl->vdp1AccessTex[_Ygl->drawframe], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	glUniform2f(2, ratio, ratio);

	glDispatchCompute(work_groups_x, work_groups_y, 1); //might be better to launch only the right number of workgroup
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
}

u32* vdp1_read(int frame) {
	int progId = READ;
	float ratio = 1.0f/_Ygl->vdp1ratio;
	if (prg_vdp1[progId] == 0){
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
  glUseProgram(prg_vdp1[progId]);

	glBindImageTexture(0, get_vdp1_tex(frame), 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo_vdp1access_);
	glUniform2f(2, ratio, ratio);

	glDispatchCompute(work_groups_x, work_groups_y, 1); //might be better to launch only the right number of workgroup

  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);

	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);

#ifdef _OGL3_
	glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0x0, 512*256*4, (void*)(&write_fb[frame][0]));
#endif

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	return &write_fb[frame][0];
}


void vdp1_compute_init(int width, int height, float ratio)
{
	int length = sizeof(vdp1_write_f_base) + 64;
	snprintf(vdp1_write_f,length,vdp1_write_f_base,local_size_x,local_size_y);

	length = sizeof(vdp1_read_f_base) + 64;
	snprintf(vdp1_read_f,length,vdp1_read_f_base,local_size_x,local_size_y);

	length = sizeof(vdp1_clear_f_base) + 64;
	snprintf(vdp1_clear_f,length,vdp1_clear_f_base,local_size_x,local_size_y);

	length = sizeof(vdp1_clear_mesh_f_base) + 64;
	snprintf(vdp1_clear_mesh_f,length,vdp1_clear_mesh_f_base,local_size_x,local_size_y);

	length = sizeof(vdp1_draw_line_start_f_base) + 64;
	snprintf(vdp1_draw_line_start_f,length,vdp1_draw_line_start_f_base,1,1);

  int am = sizeof(vdp1cmd_struct) % 16;
  tex_width = width;
  tex_height = height;
	tex_ratio = (int)ratio;
  struct_size = sizeof(vdp1cmd_struct);
  if (am != 0) {
    struct_size += 16 - am;
  }
  struct_line_size = sizeof(cmd_poly);
  if (am != 0) {
    struct_line_size += 16 - am;
  }
  work_groups_x = _Ygl->vdp1width / local_size_x;
  work_groups_y = _Ygl->vdp1height / local_size_y;
  generateComputeBuffer(_Ygl->vdp1width, _Ygl->vdp1height);
	return;
}

int get_vdp1_tex(int id) {
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	return compute_tex[id];
}

int get_vdp1_mesh(int id) {
	return mesh_tex[id];
}

void vdp1_compute_reset(void) {
	for(int i = 0; i<NB_PRG; i++) {
		if(prg_vdp1[i] != 0) {
			glDeleteProgram(prg_vdp1[i]);
			prg_vdp1[i] = 0;
		}
	}
	oldProg = -1;
}
