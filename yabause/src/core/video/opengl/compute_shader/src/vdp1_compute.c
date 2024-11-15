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
	float G[6];
	u32 misc;
	u32 idx;
} cmd_poly;
#define MIX(A, B, C) (((float)(1.0-C) * (float)(A)+(float)(C) * (float)(B)))

#define NB_COARSE_RAST (NB_COARSE_RAST_X * NB_COARSE_RAST_Y)

extern vdp2rotationparameter_struct  Vdp1ParaA;

static int local_size_x = 8;
static int local_size_y = 8;

#define WORKSIZE_L 8
#define WORKSIZE_P 8

static int tex_width;
static int tex_height;
static int tex_ratio;
static int struct_size;
static int struct_line_size;
static void drawPolygonLine(cmd_poly* cmd_pol, int nbMaxLines, int nbLines, u32 type, int overlap, point A, point B);
static int getProgramLine(cmd_poly* cmd_pol, int type);

static int work_groups_x;
static int work_groups_y;

static int oldProg = -1;

static int generateComputeBuffer(int w, int h);

static GLuint compute_tex[2] = {0};
static GLuint mesh_tex[2] = {0};
static GLuint ssbo_cmd_list_ = 0;
static GLuint ssbo_vdp1ram_ = 0;
static GLuint ssbo_vdp1access_ = 0;
static GLuint prg_vdp1[NB_PRG] = {0};

static GLuint ssbo_cmd_line_list_ = 0;

static u32 write_fb[2][512*256];

static const GLchar * a_prg_vdp1[NB_PRG][6] = {
  //VDP1_MESH_STANDARD - BANDING
	{
		vdp1_start_f,
		vdp1_standard_mesh_f,
		vdp1_banding_f,
		vdp1_continue_no_mesh_f,
		vdp1_end_f,
		NULL
	},
	//VDP1_MESH_IMPROVED - BANDING
	{
		vdp1_start_f,
		vdp1_banding_f,
		vdp1_improved_mesh_f,
		vdp1_continue_mesh_f,
		vdp1_end_mesh_f,
		NULL
	},
	//VDP1_MESH_STANDARD - NO BANDING
	{
		vdp1_start_f,
		vdp1_standard_mesh_f,
		vdp1_no_banding_f,
		vdp1_continue_no_mesh_f,
		vdp1_end_f,
		NULL
	},
	//VDP1_MESH_IMPROVED- NO BANDING
	{
		vdp1_start_f,
		vdp1_no_banding_f,
		vdp1_improved_mesh_f,
		vdp1_continue_mesh_f,
		vdp1_end_mesh_f,
		NULL
	},
	//WRITE
	{
		vdp1_write_f,
		NULL,
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
		NULL,
		NULL
	},
	//CLEAR
	{
		vdp1_clear_f,
		NULL,
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
		NULL,
		NULL
	},
	// DRAW_POLY_MSB_SHADOW_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_REPLACE_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_SHADOW_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_HALF_LUMINANCE_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_POLY_GOURAUD_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_POLY_UNSUPPORTED_NO_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_MSB_SHADOW_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_REPLACE_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_SHADOW_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_HALF_LUMINANCE_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_QUAD_GOURAUD_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_QUAD_UNSUPPORTED_NO_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_no_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_POLY_MSB_SHADOW_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_REPLACE_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_SHADOW_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_HALF_LUMINANCE_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_HALF_TRANSPARENT_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_POLY_GOURAUD_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_POLY_UNSUPPORTED_NO_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_non_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_MSB_SHADOW_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_msb_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_REPLACE_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_replace_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_SHADOW_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_shadow_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_HALF_LUMINANCE_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_HALF_TRANSPARENT_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_QUAD_GOURAUD_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
	// DRAW_QUAD_UNSUPPORTED_MESH
	{
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL
	},
  // DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_luminance_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	},
  // DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH
	{
		vdp1_start_end,
		vdp1_draw_line_start_f_rw,
		vdp1_get_textured_f,
		vdp1_get_pixel_gouraud_half_transparent_f,
		vdp1_draw_mesh_f,
		vdp1_draw_poly_test
	}
};


static int getProgramId() {
	if (_Ygl->meshmode == ORIGINAL_MESH){
	  if (_Ygl->bandingmode == ORIGINAL_BANDING)
    	return VDP1_MESH_STANDARD_BANDING;
		else
			return VDP1_MESH_STANDARD_NO_BANDING;
	}else{
		if (_Ygl->bandingmode == ORIGINAL_BANDING)
	  	return VDP1_MESH_IMPROVED_BANDING;
		else
			return VDP1_MESH_IMPROVED_NO_BANDING;
	}
}

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

	if (ssbo_cmd_list_ != 0) {
    glDeleteBuffers(1, &ssbo_cmd_list_);
  }

	glGenBuffers(1, &ssbo_cmd_list_);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_list_);
	glBufferData(GL_SHADER_STORAGE_BUFFER, struct_size, NULL, GL_DYNAMIC_DRAW);

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

static int computeBresenhamLinePoints(int x1, int y1, int x2, int y2, point **data) {
	//Need to handle upscale smoothing
	int dx =  abs (x2 - x1), sx = x1 < x2 ? 1 : -1;
  int dy = -abs (y2 - y1), sy = y1 < y2 ? 1 : -1;
	int val = 0;
  int err = dx + dy, e2; /* error value e_xy */
	int nbMaxPoint = MAX(abs(dx), abs(dy))+ 1;
	*data = (point*)malloc(nbMaxPoint*sizeof(point));
	int i = 0;
  for (;;){  /* loop */
		(*data)[i++] = (point){.x=x1, .y=y1};
    if (x1 == x2 && y1 == y2) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x1 += sx;} /* e_xy+e_x > 0 */
    if (e2 <= dx) { err += dx; y1 += sy;} /* e_xy+e_y < 0 */
  }
	if (i != nbMaxPoint) {
		// printf("Error %d,%d => %d %d,%d => %d %d => %d\n", x1, x2, dx,y1, y2, dy, i, nbMaxPoint);
		exit(-1);
	}
	return i;
}

static void drawQuad(vdp1cmd_struct* cmd) {
	point *dataL, *dataR;
	// printf("Quad\n");
	int li = computeBresenhamLinePoints(cmd->CMDXA, cmd->CMDYA, cmd->CMDXD, cmd->CMDYD, &dataL);
	int ri = computeBresenhamLinePoints(cmd->CMDXB, cmd->CMDYB, cmd->CMDXC, cmd->CMDYC, &dataR);
	int nbCmd = MAX(li,ri);
	cmd_poly *cmd_pol = (cmd_poly*)calloc(nbCmd, sizeof(cmd_poly));
	int idl = 0;
	int idr = 0;
	int a = 0;
	int i = 0;
	int add = 0;
	if(li>ri) {
		for (i = 0; i != li; i++) {
			a += ri;
			idl = i;
			if (((dataL[idl].y < (Vdp1Regs->systemclipY2+1))
			 || (dataL[idl].x < (Vdp1Regs->systemclipX2+1))
			 || (dataR[idr].y < (Vdp1Regs->systemclipY2+1))
			 || (dataR[idr].x < (Vdp1Regs->systemclipX2+1)))
			 &&
				 ((dataL[idl].y >= 0)
			 || (dataL[idl].x >= 0)
			 || (dataR[idr].y >= 0)
			 || (dataR[idr].x >= 0)))
			{
				float dl = (float)((idl)+0.5)/(float)(li);
				float dr = (float)((idr)+0.5)/(float)(ri);
				cmd_pol[add] = (cmd_poly){
					.CMDPMOD = cmd->CMDPMOD,
					.CMDSRCA = cmd->CMDSRCA,
					.CMDSIZE = cmd->CMDSIZE,
					.CMDXA = dataL[idl].x,
					.CMDYA = dataL[idl].y,
					.CMDXB = dataR[idr].x,
					.CMDYB = dataR[idr].y,
					.CMDCOLR = cmd->CMDCOLR,
					.misc = (cmd->flip & 0x3),
					.idx = i
				};
				// printf("(%d) %d,%d => %d,%d\n",i,
				// 	cmd_pol[i].CMDXA,cmd_pol[i].CMDYA,
				// 	cmd_pol[i].CMDXB,cmd_pol[i].CMDYB
				// );
				cmd_pol[add].G[0] = MIX(cmd->G[0], cmd->G[9], dl);
				cmd_pol[add].G[1] = MIX(cmd->G[1], cmd->G[10], dl);
				cmd_pol[add].G[2] = MIX(cmd->G[2], cmd->G[11], dl);
				cmd_pol[add].G[3] = MIX(cmd->G[3], cmd->G[6], dr);
				cmd_pol[add].G[4] = MIX(cmd->G[4], cmd->G[7], dr);
				cmd_pol[add].G[5] = MIX(cmd->G[5], cmd->G[8], dr);
				add++;
			}
			if (abs(a) >= abs(li)) {
				a -= li;
				idr++;
			}
		}
	} else {
		for (i = 0; i != ri; i++) {
			a += li;
			idr = i;
			if (((dataL[idl].y < (Vdp1Regs->systemclipY2+1))
			 || (dataR[idr].y < (Vdp1Regs->systemclipY2+1)))
			 &&((dataL[idl].x < (Vdp1Regs->systemclipX2+1))
			 || (dataR[idr].x < (Vdp1Regs->systemclipX2+1)))
			 &&
			   ((dataL[idl].x >= 0)
			 || (dataR[idr].x >= 0))
			 &&((dataL[idl].y >= 0)
			 || (dataR[idr].y >= 0)))
			{
				float dl = (float)((idl)+0.5)/(float)(li);
				float dr = (float)((idr)+0.5)/(float)(ri);
				cmd_pol[add] = (cmd_poly){
					.CMDPMOD = cmd->CMDPMOD,
					.CMDSRCA = cmd->CMDSRCA,
					.CMDSIZE = cmd->CMDSIZE,
					.CMDXA = dataL[idl].x,
					.CMDYA = dataL[idl].y,
					.CMDXB = dataR[idr].x,
					.CMDYB = dataR[idr].y,
					.CMDCOLR = cmd->CMDCOLR,
					.misc = (cmd->flip & 0x3),
					.idx = i
				};
				// printf("(%d) %d,%d => %d,%d\n",i,
				// 	cmd_pol[i].CMDXA,cmd_pol[i].CMDYA,
				// 	cmd_pol[i].CMDXB,cmd_pol[i].CMDYB
				// );
				cmd_pol[add].G[0] = MIX(cmd->G[0], cmd->G[9], dl);
				cmd_pol[add].G[1] = MIX(cmd->G[1], cmd->G[10], dl);
				cmd_pol[add].G[2] = MIX(cmd->G[2], cmd->G[11], dl);
				cmd_pol[add].G[3] = MIX(cmd->G[3], cmd->G[6], dr);
				cmd_pol[add].G[4] = MIX(cmd->G[4], cmd->G[7], dr);
				cmd_pol[add].G[5] = MIX(cmd->G[5], cmd->G[8], dr);
				add++;
			}
			if (abs(a) >= abs(ri)) {
				a -= ri;
				idl++;
			}
		}
	}
	point A = (point){
		.x= MIN(cmd->CMDXA, MIN(cmd->CMDXB, MIN(cmd->CMDXC, cmd->CMDXD))),
		.y= MIN(cmd->CMDYA, MIN(cmd->CMDYB, MIN(cmd->CMDYC, cmd->CMDYD)))
	};
	point B = (point){
		.x= MAX(cmd->CMDXA, MAX(cmd->CMDXB, MAX(cmd->CMDXC, cmd->CMDXD))),
		.y= MAX(cmd->CMDYA, MAX(cmd->CMDYB, MAX(cmd->CMDYC, cmd->CMDYD)))
	};
	drawPolygonLine(cmd_pol, i, add,cmd->type, li!=ri, A, B);
	free(cmd_pol);
	free(dataL);
	free(dataR);
}

void drawLine(vdp1cmd_struct* cmd, point A, point B) {
	int dx = abs(B.x - A.x);
	int dy = abs(B.y - A.y);
	cmd_poly *cmd_pol = (cmd_poly*)calloc(1, sizeof(cmd_poly));
	float dl = 0.5;
	float dr = 0.5;
	int rx = (dx < dy)?0:(((B.x<A.x)?1:-1) + 2)&0x3;
	int ry = (dx < dy)?0:(((B.y<A.y)?1:-1) + 2)&0x3;
	int s = (ry<<2)|rx;
	cmd_pol[0] = (cmd_poly){
		.CMDPMOD = cmd->CMDPMOD,
		.CMDSRCA = cmd->CMDSRCA,
		.CMDSIZE = cmd->CMDSIZE,
		.CMDXA = A.x,
		.CMDYA = A.y,
		.CMDXB = B.x,
		.CMDYB = B.y,
		.CMDCOLR = cmd->CMDCOLR,
		.misc = cmd->flip & 0x3,
		.misc = (cmd->flip & 0x3)|((s&0xF)<<6)|((s&0xF)<<2),
		.idx = 0
	};
	cmd_pol[0].G[0] = MIX(cmd->G[0], cmd->G[9], dl);
	cmd_pol[0].G[1] = MIX(cmd->G[1], cmd->G[10], dl);
	cmd_pol[0].G[2] = MIX(cmd->G[2], cmd->G[11], dl);
	cmd_pol[0].G[3] = MIX(cmd->G[3], cmd->G[6], dr);
	cmd_pol[0].G[4] = MIX(cmd->G[4], cmd->G[7], dr);
	cmd_pol[0].G[5] = MIX(cmd->G[5], cmd->G[8], dr);
	drawPolygonLine(cmd_pol, 1, 1,cmd->type,0,A,B);
	free(cmd_pol);
}

int vdp1_add_upscale(vdp1cmd_struct* cmd, int clipcmd) {
	int minx = 1024;
	int miny = 1024;
	int maxx = 0;
	int maxy = 0;
	point A,B;

	if ((Vdp1Regs->TVMR & 0x1)!=0) {
		// Color calculation is working only on framebuffer 16 bits
		cmd->CMDPMOD &= ~0x7; //Force a replace on framebuffer 8 bits
	}

	if (_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] != -1) {
		if (VIDCore->endVdp1Render) VIDCore->endVdp1Render();
		vdp1_write();
		if (VIDCore->startVdp1Render) VIDCore->startVdp1Render();
		_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = -1;
	}
	if ((clipcmd != 0) && (VIDCore->startVdp1Render)) VIDCore->startVdp1Render();

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
			cmd->type = POLYLINE;
			break;
			case POLYGON:
				cmd->type = POLYLINE;
			break;
			case QUAD:
			case QUAD_POLY:
				if ((abs(cmd->CMDXA - cmd->CMDXB) <= ((2*_Ygl->rwidth)/3)) && (abs(cmd->CMDYA - cmd->CMDYD) <= ((_Ygl->rheight)/2)))
					cmd->type = POLYLINE;
			break;
			default:
				break;
		}
	}
	if (clipcmd == 0) {
		if (_Ygl->meshmode != ORIGINAL_MESH) {
			//Hack for Improved MESH
			//Games like J.League Go Go Goal or Sailor Moon are using MSB shadow with VDP2 in RGB/Palette mode
			//In that case, the pixel is considered as RGB by the VDP2 displays it a black surface
			// To simualte a transparent shadow, on improved mesh, we force the shadow mode and the usage of mesh
			if ((cmd->CMDPMOD & 0x8000) && ((Vdp2Regs->SPCTL & 0x20)!=0)) {
				//MSB is set to be used but VDP2 do not use it. Consider as invalid and remove the MSB
				//Use shadow mode with Mesh to simulate the final effect
				cmd->CMDPMOD &= ~0x8007;
				cmd->CMDPMOD |= 0x101; //Use shadow mode and mesh then
			}
		}

		point A = (point){
			.x= MIN(cmd->CMDXA, MIN(cmd->CMDXB, MIN(cmd->CMDXC, cmd->CMDXD))),
			.y= MIN(cmd->CMDYA, MIN(cmd->CMDYB, MIN(cmd->CMDYC, cmd->CMDYD)))
		};
		point B = (point){
			.x= MAX(cmd->CMDXA, MAX(cmd->CMDXB, MAX(cmd->CMDXC, cmd->CMDXD))),
			.y= MAX(cmd->CMDYA, MAX(cmd->CMDYB, MAX(cmd->CMDYC, cmd->CMDYD)))
		};
		A.x = MIN(A.x, Vdp1Regs->systemclipX2);
		A.y = MIN(A.y, Vdp1Regs->systemclipY2);
		B.x = MIN(B.x, Vdp1Regs->systemclipX2);
		B.y = MIN(B.y, Vdp1Regs->systemclipY2);
		A.x = MAX(A.x, 0);
		A.y = MAX(A.y, 0);
		B.x = MAX(B.x, 0);
		B.y = MAX(B.y, 0);

		A.x *= tex_ratio;
		A.y *= tex_ratio;
		B.x *= tex_ratio;
		B.y *= tex_ratio;

		point tl = (point){
			.x = MIN(A.x, B.x),
			.y = MIN(A.y, B.y)
		};
		if ((tl.x == Vdp1Regs->systemclipX2*tex_ratio) || (tl.y == Vdp1Regs->systemclipY2*tex_ratio))
		{
			//Top left point is at limit, so quad will not be displayed, do not compute
			return 0;
		}

		point br = (point){
			.x = MAX(A.x, B.x),
			.y = MAX(A.y, B.y)
		};

		glUniform2i(10, tl.x, tl.y);
		vdp1_compute(cmd, tl, br);
	}

  return 0;
}

void vdp1_update_performance() {
	vdp1_compute_reset();
}

void vdp1_update_banding(void) {
	//Change char * reference et incvalide les progIds
	a_prg_vdp1[DRAW_POLY_GOURAUD_NO_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_NO_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_f:vdp1_get_pixel_gouraud_extended_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_LUMINANCE_NO_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_TRANSPARENT_NO_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_LUMINANCE_NO_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_LUMINANCE_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_POLY_GOURAUD_HALF_TRANSPARENT_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_LUMINANCE_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;
	a_prg_vdp1[DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH][3] = (_Ygl->bandingmode==ORIGINAL_BANDING)?vdp1_get_pixel_gouraud_half_luminance_f:vdp1_get_pixel_gouraud_extended_half_luminance_f;

	vdp1_compute_reset();
}

void vdp1_update_mesh(void) {
	for (int i=DRAW_POLY_MSB_SHADOW_NO_MESH; i<=DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_NO_MESH; i++) {
		a_prg_vdp1[i][4] = (_Ygl->meshmode == IMPROVED_MESH)?vdp1_draw_no_mesh_improved_f:vdp1_draw_no_mesh_f;
	}
	for (int i=DRAW_POLY_MSB_SHADOW_MESH; i<=DRAW_QUAD_GOURAUD_HALF_TRANSPARENT_MESH; i++) {
		a_prg_vdp1[i][4] = (_Ygl->meshmode == IMPROVED_MESH)?vdp1_draw_improved_mesh_f:vdp1_draw_mesh_f;
	}
	vdp1_compute_reset();
}

void startVdp1Render() {
	if (oldProg == -1) return;
	glUseProgram(prg_vdp1[oldProg]);
	if (a_prg_vdp1[oldProg][1] == vdp1_draw_line_start_f_rw)
		glBindImageTexture(0, get_vdp1_tex(_Ygl->drawframe), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	else
		glBindImageTexture(0, get_vdp1_tex(_Ygl->drawframe), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	if (_Ygl->meshmode == IMPROVED_MESH) glBindImageTexture(1, get_vdp1_mesh(_Ygl->drawframe), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, ssbo_cmd_line_list_);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo_vdp1ram_);
	glUniform1i(4, (Vdp1Regs->TVMR & 0x1));
	glUniform2i(8, Vdp1Regs->systemclipX2, Vdp1Regs->systemclipY2);
	glUniform4i(9, Vdp1Regs->userclipX1, Vdp1Regs->userclipY1, Vdp1Regs->userclipX2, Vdp1Regs->userclipY2);
	glUniform1i(11, 0);
	glUniform1i(12, 0);
}

static void flushVdp1Render(int nbWork, int nbPoints) {
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		glDispatchCompute(nbWork, nbPoints, 1); //might be better to launch only the right number of workgroup
}

void endVdp1Render() {
	oldProg = -1;
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void drawPolygonLine(cmd_poly* cmd_pol, int nbTotalLines, int nbLines, u32 type, int overlap, point A, point B) {
	if (nbLines == 0) return;
	// nbLines = nbTotalLines = 1;
	int progId = getProgramLine(&cmd_pol[0], type);
	// trace_prog(progId);
	if (progId == DRAW_POLY_UNSUPPORTED_MESH) return;
	if (progId == DRAW_POLY_UNSUPPORTED_NO_MESH) return;
	if (progId == DRAW_QUAD_UNSUPPORTED_MESH) return;
	if (progId == DRAW_QUAD_UNSUPPORTED_NO_MESH) return;


	if (prg_vdp1[progId] == 0) {
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
	if (oldProg != progId) {
		// 	Might be some stuff to clean
			oldProg = progId;
			if (VIDCore->startVdp1Render) VIDCore->startVdp1Render();
	}

	if (Vdp1External.updateVdp1Ram != 0) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_vdp1ram_);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, vdp1Ram_update_start, vdp1Ram_update_end-vdp1Ram_update_start, (void*)&Vdp1Ram[vdp1Ram_update_start]);
		vdp1Ram_update_start = 0x80000;
		vdp1Ram_update_end = 0x0;
		Vdp1External.updateVdp1Ram = 0;
	}
	glUniform1i(11, (type==DISTORTED)||(type==POLYGON));
	// glUniform1i(11, greed);
	A.x = MIN(A.x, Vdp1Regs->systemclipX2);
	A.y = MIN(A.y, Vdp1Regs->systemclipY2);
	B.x = MIN(B.x, Vdp1Regs->systemclipX2);
	B.y = MIN(B.y, Vdp1Regs->systemclipY2);
	A.x = MAX(A.x, 0);
	A.y = MAX(A.y, 0);
	B.x = MAX(B.x, 0);
	B.y = MAX(B.y, 0);

	int dx = abs(B.x-A.x)+1;
	int dy = abs(B.y-A.y)+1;
	point Bound = (point){
		.x = MIN(A.x, B.x),
		.y = MIN(A.y, B.y)
	};
	if ((Bound.x == Vdp1Regs->systemclipX2) || (Bound.y == Vdp1Regs->systemclipY2))
	{
		//Top left point is at limit, so quad will not be displayed, do not compute
		return;
	}
	glUniform2i(14, Bound.x, Bound.y);
	glUniform1i(12, nbTotalLines);
	for (int i = 0; i<nbLines; i+=NB_LINE_MAX_PER_DRAW) {
		int drawNbLines = MIN(NB_LINE_MAX_PER_DRAW,(nbLines - i));
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_line_list_);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, drawNbLines*sizeof(cmd_poly), (void*)&cmd_pol[i]);
		glUniform1i(13, drawNbLines);
		flushVdp1Render((dx+WORKSIZE_L-1)/WORKSIZE_L, (dy+WORKSIZE_P-1)/WORKSIZE_P); //might be better to launch only the right number of workgroup
	}
}

int vdp1_add(vdp1cmd_struct* cmd, int clipcmd) {

	if (_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] != -1) {
		if (VIDCore->endVdp1Render) VIDCore->endVdp1Render();
		vdp1_write();
		if (VIDCore->startVdp1Render) VIDCore->startVdp1Render();
		_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = -1;
	}

	if ((clipcmd != 0) && (VIDCore->startVdp1Render)) VIDCore->startVdp1Render();

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
				  if ((abs(cmd->CMDXA - cmd->CMDXB) <= ((2*_Ygl->rwidth)/3)) && (abs(cmd->CMDYA - cmd->CMDYD) <= ((_Ygl->rheight)/2)))
					cmd->type = POLYLINE;
			break;
			default:
				break;
		}
	}
	//POLYLINE
	// if (clipcmd == 0) {
	// 	cmd->type = POLYLINE;
	// 	cmd->CMDXA = 10;
	// 	cmd->CMDXB = 90;
	// 	cmd->CMDXC = 90;
	// 	cmd->CMDXD = 10;
	// 	cmd->CMDYA = 29;
	// 	cmd->CMDYB = 29;
	// 	cmd->CMDYC = 9;
	// 	cmd->CMDYD = 9;
	// }
	//POLYLINE AS A POINT
	// if (clipcmd == 0) {
	// 	cmd->type = POLYLINE;
	// 	cmd->CMDXA = 90;
	// 	cmd->CMDXB = 90;
	// 	cmd->CMDXC = 90;
	// 	cmd->CMDXD = 90;
	// 	cmd->CMDYA = 29;
	// 	cmd->CMDYB = 29;
	// 	cmd->CMDYC = 29;
	// 	cmd->CMDYD = 29;
	// }
	//LINE
	// if (clipcmd == 0) {
	// 	cmd->type = LINE;
	// 	cmd->CMDXA = 10;
	// 	cmd->CMDXB = 14;
	// 	cmd->CMDYA = 9;
	// 	cmd->CMDYB = 10;
	// }
	//LINE
	// if (clipcmd == 0) {
	// 	cmd->type = LINE;
	// 	cmd->CMDXA = 10;
	// 	cmd->CMDXB = 14;
	// 	cmd->CMDYA = 9;
	// 	cmd->CMDYB = 10;
	// }
	// LINE - Test Bresenham
	// if (clipcmd == 0) {
	// 	cmd->type = LINE;
	// 	cmd->CMDXA = 10;
	// 	cmd->CMDXB = 40;
	// 	cmd->CMDYA = 10;
	// 	cmd->CMDYB = 20;
	// }
	// LINE AS A POINT
	// if (clipcmd == 0) {
	// 	cmd->type = LINE;
	// 	cmd->CMDXA = 90;
	// 	cmd->CMDXB = 90;
	// 	cmd->CMDYA = 29;
	// 	cmd->CMDYB = 29;
	// }

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
		// cmd->CMDCOLR = 0x7FFF;
		// cmd->CMDXA = 98;
		// cmd->CMDXB = 103;
		// cmd->CMDXC = 130;
		// cmd->CMDXD = 122;
		// cmd->CMDYA = 32;
		// cmd->CMDYB = 9;
		// cmd->CMDYC = 22;
		// cmd->CMDYD = 47;
		//QUAD
		// cmd->CMDCOLR = 0x7FFF;
		// //sx=1, sy=1 , a.x=1, a.y=1
		// cmd->CMDXA = 100;
		// cmd->CMDXB = 150;
		// cmd->CMDXC = 160;
		// cmd->CMDXD = 110;
		// cmd->CMDYA = 100;
		// cmd->CMDYB = 100;
		// cmd->CMDYC = 150;
		// cmd->CMDYD = 150;
		// //sx=1, sy=1 , a.x=-1, a.y=1
		// cmd->CMDXA = 150;
		// cmd->CMDXB = 100;
		// cmd->CMDXC = 110;
		// cmd->CMDXD = 160;
		// cmd->CMDYA = 100;
		// cmd->CMDYB = 100;
		// cmd->CMDYC = 150;
		// cmd->CMDYD = 150;
		// //sx=1, sy=1 , a.x=1, a.y=-1
		// cmd->CMDXA = 100;
		// cmd->CMDXB = 150;
		// cmd->CMDXC = 160;
		// cmd->CMDXD = 110;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;
		// //sx=1, sy=1 , a.x=-1, a.y = -1
		// cmd->CMDXA = 150;
		// cmd->CMDXB = 100;
		// cmd->CMDXC = 110;
		// cmd->CMDXD = 160;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;
		// //sx=-1, sy=1, a.x = 1, a.y=1
		// cmd->CMDXA = 110;
		// cmd->CMDXB = 160;
		// cmd->CMDXC = 150;
		// cmd->CMDXD = 100;
		// cmd->CMDYA = 100;
		// cmd->CMDYB = 100;
		// cmd->CMDYC = 150;
		// cmd->CMDYD = 150;
		// //sx=-1, sy=1, a.x = -1, a.y = 1
		// cmd->CMDXA = 160;
		// cmd->CMDXB = 110;
		// cmd->CMDXC = 100;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 100;
		// cmd->CMDYB = 100;
		// cmd->CMDYC = 150;
		// cmd->CMDYD = 150;
		// //sx=-1, sy=1, a.x = 1, a.y=-1
		// cmd->CMDXA = 110;
		// cmd->CMDXB = 160;
		// cmd->CMDXC = 150;
		// cmd->CMDXD = 100;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;
		// //sx=-1, sy=1, a.x = -1, a.y = -1
		// cmd->CMDXA = 160;
		// cmd->CMDXB = 110;
		// cmd->CMDXC = 100;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 140;
		// cmd->CMDYC = 90;
		// cmd->CMDYD = 100;
		// //sx=-1, sy=-1, a.x = 1, a.y=1
		// cmd->CMDXA = 110;
		// cmd->CMDXB = 160;
		// cmd->CMDXC = 150;
		// cmd->CMDXD = 100;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;
		// //sx=-1, sy=-1, a.x = -1, a.y=1
		// cmd->CMDXA = 160;
		// cmd->CMDXB = 110;
		// cmd->CMDXC = 100;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;
		// //sx=-1, sy=-1, a.x = 1, a.y=-1
		// cmd->CMDXA = 110;
		// cmd->CMDXB = 160;
		// cmd->CMDXC = 150;
		// cmd->CMDXD = 100;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 140;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 90;
		// //sx=-1, sy=-1, a.x = -1, a.y=-1
		// cmd->CMDXA = 160;
		// cmd->CMDXB = 110;
		// cmd->CMDXC = 100;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;
		// //sx=1, sy=-1
		// cmd->CMDXA = 100;
		// cmd->CMDXB = 150;
		// cmd->CMDXC = 160;
		// cmd->CMDXD = 110;
		// cmd->CMDYA = 150;
		// cmd->CMDYB = 150;
		// cmd->CMDYC = 100;
		// cmd->CMDYD = 100;

		//LINE
		// cmd->CMDXA = 120;
		// cmd->CMDXB = 130;
		// cmd->CMDXC = 140;
		// cmd->CMDXD = 150;
		// cmd->CMDYA = 130;
		// cmd->CMDYB = 140;
		// cmd->CMDYC = 150;
		// cmd->CMDYD = 160;
		//QUAD
		// cmd->CMDXA = 10;
		// cmd->CMDXB = 12;
		// cmd->CMDXC = 11;
		// cmd->CMDXD = 9;
		// cmd->CMDYA = 10;
		// cmd->CMDYB = 10;
		// cmd->CMDYC = 14;
		// cmd->CMDYD = 14;
		//QUAD TOMB RAIDER BOOK
		// cmd->CMDXA = 181;
		// cmd->CMDXB = 183;
		// cmd->CMDXC = 158;
		// cmd->CMDXD = 156;
		// cmd->CMDYA = 155;
		// cmd->CMDYB = 156;
		// cmd->CMDYC = 195;
		// cmd->CMDYD = 193;
		//QUAD AS A LINE - A revoir
		// cmd->CMDXA = 50;
		// cmd->CMDXB = 50;
		// cmd->CMDXC = 100;
		// cmd->CMDXD = 100;
		// cmd->CMDYA = 100;
		// cmd->CMDYB = 100;
		// cmd->CMDYC = 50;
		// cmd->CMDYD = 50;
		//QUAD AS A LINE
		// cmd->CMDXA = 181;
		// cmd->CMDXB = 182;
		// cmd->CMDXC = 182;
		// cmd->CMDXD = 181;
		// cmd->CMDYA = 156;
		// cmd->CMDYB = 159;
		// cmd->CMDYC = 159;
		// cmd->CMDYD = 156;
		//QUAD AS A LINE
		// greed = 1;
		// cmd->CMDXA = 9;
		// cmd->CMDXB = 10;
		// cmd->CMDXC = 10;
		// cmd->CMDXD = 9;
		// cmd->CMDYA = 10;
		// cmd->CMDYB = 14;
		// cmd->CMDYC = 14;
		// cmd->CMDYD = 10;
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
		// SEGA RALLY
		// cmd->CMDXA = 79 + 176;
		// cmd->CMDXB = -21 + 176;
		// cmd->CMDXC = 512 + 176;
		// cmd->CMDXD = 391 + 176;
		// cmd->CMDYA = 44 + 128;
		// cmd->CMDYB = 304 + 128;
		// cmd->CMDYC = 310 + 128;
		// cmd->CMDYD = 86 + 128;
		//AIGUILLE SEGA RALLY // Triangle
		// cmd->CMDXA = 176-125;
		// cmd->CMDXB = 176-112;
		// cmd->CMDXC = 176-127;
		// cmd->CMDXD = 176-127;
		// cmd->CMDYA = 116+75;
		// cmd->CMDYB = 116+46;
		// cmd->CMDYC = 116+74;
		// cmd->CMDYD = 116+74;
		//AIGUILLE SEGA RALLY // Triangle
		// cmd->CMDXA = 176-126;
		// cmd->CMDXB = 176-105;
		// cmd->CMDXC = 176-127;
		// cmd->CMDXD = 176-127;
		// cmd->CMDYA = 128+90;
		// cmd->CMDYB = 128+68;
		// cmd->CMDYC = 128+89;
		// cmd->CMDYD = 128+89;
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
		// DOOM patch
		// cmd->CMDXA = 69;
		// cmd->CMDXB = 69;
		// cmd->CMDXC = 69;
		// cmd->CMDXD = 69;
		// cmd->CMDYA = 63;
		// cmd->CMDYB = 121;
		// cmd->CMDYC = 121;
		// cmd->CMDYD = 63;
		// SONIC 3D RED polygon
		// cmd->CMDXA = 320;
		// cmd->CMDXB = 320;
		// cmd->CMDXC = 9;
		// cmd->CMDXD = 16;
		// cmd->CMDYA = 192;
		// cmd->CMDYB = 199;
		// cmd->CMDYC = 199;
		// cmd->CMDYD = 192;
		//ROBOTICA US
		// cmd->CMDXA = 368+160;
		// cmd->CMDXB = 215+160;
		// cmd->CMDXC = 268+160;
		// cmd->CMDXD = 396+160;
		// cmd->CMDYA = 86+96;
		// cmd->CMDYB = 86+96;
		// cmd->CMDYC = 316+96;
		// cmd->CMDYD = 316+96;
		//TOMB RAIDER LAMP
		// cmd->type = POLYGON;
		// cmd->CMDXA = 208;
		// cmd->CMDYA = 190;
		// cmd->CMDXB = 207;
		// cmd->CMDYB = 192;
		// cmd->CMDXC = 201;
		// cmd->CMDYC = 191;
		// cmd->CMDXD = 202;
		// cmd->CMDYD = 188;
		// cmd->type = POLYGON;
		// cmd->CMDXA = 201;
		// cmd->CMDYA = 190;
		// cmd->CMDXB = 200;
		// cmd->CMDYB = 192;
		// cmd->CMDXC = 193;
		// cmd->CMDYC = 188;
		// cmd->CMDXD = 194;
		// cmd->CMDYD = 187;

			drawQuad(cmd);
	}

	if (cmd->type == POLYLINE) {
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

	return progId+delta;
}


void startVdp1RenderUpscale() {
	if (oldProg == -1) return;
	glUseProgram(prg_vdp1[oldProg]);
	glBindImageTexture(0, get_vdp1_tex(_Ygl->drawframe), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	glBindImageTexture(1, get_vdp1_mesh(_Ygl->drawframe), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo_vdp1ram_);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, ssbo_cmd_list_);
	glUniform1i(4, (Vdp1Regs->TVMR & 0x1));
	glUniform2f(7, tex_ratio, tex_ratio);
	glUniform2i(8, Vdp1Regs->systemclipX2, Vdp1Regs->systemclipY2);
	glUniform4i(9, Vdp1Regs->userclipX1, Vdp1Regs->userclipY1, Vdp1Regs->userclipX2, Vdp1Regs->userclipY2);
	glUniform2i(10, 0, 0);
}

void endVdp1RenderUpscale() {
	oldProg = -1;
	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
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

	length = sizeof(vdp1_start_f_base) + 64;
	snprintf(vdp1_start_f,length,vdp1_start_f_base,local_size_x,local_size_y);

	length = sizeof(vdp1_start_end_base) + 64;
	snprintf(vdp1_start_end,length,vdp1_start_end_base,WORKSIZE_L,WORKSIZE_P);

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
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	return compute_tex[id];
}

int get_vdp1_mesh(int id) {
	return mesh_tex[id];
}
void vdp1_compute(vdp1cmd_struct* cmd, point tl, point br) {
  GLuint error;
	int progId = getProgramId();

	if (prg_vdp1[progId] == 0){
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}

	VDP1CPRINT("Draw VDP1 on %d\n", _Ygl->drawframe);
	if (oldProg != progId) {
		// 	Might be some stuff to clean
		oldProg = progId;
		if (VIDCore->startVdp1Render) VIDCore->startVdp1Render();
	}

	if (Vdp1External.updateVdp1Ram != 0) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_vdp1ram_);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, vdp1Ram_update_start, vdp1Ram_update_end-vdp1Ram_update_start, (void*)&Vdp1Ram[vdp1Ram_update_start]);
		vdp1Ram_update_start = 0x80000;
		vdp1Ram_update_end = 0x0;
		Vdp1External.updateVdp1Ram = 0;
	}

	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_list_);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(vdp1cmd_struct), (void*)cmd);

	int wx = ((br.x-tl.x+1)+local_size_x-1)/local_size_x;
	int wy = ((br.y-tl.y+1)+local_size_y-1)/local_size_y;

  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  glDispatchCompute(wx, wy, 1); //might be better to launch only the right number of workgroup
  ErrorHandle("glDispatchCompute");

  return;
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
