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
	float dl;
	float dr;
	float G[16];
	u32 flip;
	u32 pad[5];
} cmd_poly;

extern vdp2rotationparameter_struct  Vdp1ParaA;

static int local_size_x = 8;
static int local_size_y = 8;


static int tex_width;
static int tex_height;
static int tex_ratio;
static int struct_size;
static int struct_line_size;
void drawPolygonLine(cmd_poly* cmd_pol, int nbLines, u32 type);

static int work_groups_x;
static int work_groups_y;

static int cmdRam_update_start[2] = {0x0};
static int cmdRam_update_end[2] = {0x80000};

static int generateComputeBuffer(int w, int h);

static GLuint compute_tex[2] = {0};
static GLuint mesh_tex[2] = {0};
static GLuint ssbo_vdp1ram_ = 0;
static GLuint ssbo_nbcmd_ = 0;
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
	}
};

static int progMask = 0;

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

	if (ssbo_cmd_line_list_ != 0) {
		glDeleteBuffers(1, &ssbo_cmd_line_list_);
	}
	glGenBuffers(1, &ssbo_cmd_line_list_);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_line_list_);
	glBufferData(GL_SHADER_STORAGE_BUFFER, struct_line_size*32, NULL, GL_DYNAMIC_DRAW);

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

static int computeLinePoints(int x1, int y1, int x2, int y2, point **data) {
	int i, a, ax, ay, dx, dy;
	a = i = 0;
	dx = x2 - x1;
	dy = y2 - y1;
	ax = (dx >= 0) ? 1 : -1;
	ay = (dy >= 0) ? 1 : -1;
	int nbMaxPoint = MAX(abs(dx), abs(dy))+1;

	*data = (point*)malloc(nbMaxPoint*sizeof(point));
	if (abs(dx) > abs(dy)) {
		if (ax != ay) dx = -dx;

		for (i = 0; x1 != x2; x1 += ax, i++) {
			(*data)[i] = (point){.x=x1, .y=y1};
			a += dy;
			if (abs(a) >= abs(dx)) {
				a -= dx;
				y1 += ay;
			}
		}
		(*data)[i++] = (point){.x=x2, .y=y2};
	} else {
		if (ax != ay) dy = -dy;

		for (i = 0; y1 != y2; y1 += ay, i++) {
      (*data)[i] = (point){.x=x1, .y=y1};
			a += dx;
			if (abs(a) >= abs(dy)) {
				a -= dy;
				x1 += ax;
			}
		}
		(*data)[i++] = (point){.x=x2, .y=y2};
	}

	if (i != nbMaxPoint) {
		printf("Error %d,%d %d,%d %d => %d\n", x1, y1, x2, y2, i, nbMaxPoint);
		exit(-1);
	}
	return i;
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
		vdp1_write();
		_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = -1;
	}

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
		point *dataL, *dataR;

		cmd->CMDXA = (cmd->CMDXA*tex_ratio);
		cmd->CMDXB = (cmd->CMDXB*tex_ratio);
		cmd->CMDXC = (cmd->CMDXC*tex_ratio);
		cmd->CMDXD = (cmd->CMDXD*tex_ratio);
		cmd->CMDYA = (cmd->CMDYA*tex_ratio);
		cmd->CMDYB = (cmd->CMDYB*tex_ratio);
		cmd->CMDYC = (cmd->CMDYC*tex_ratio);
		cmd->CMDYD = (cmd->CMDYD*tex_ratio);

//Standard Quad upscale
	// if (cmd->CMDXD != cmd->CMDXA) {
	// 	if (cmd->CMDXD >= cmd->CMDXA) cmd->CMDXD += tex_ratio - 1;
	// 	else cmd->CMDXA += tex_ratio - 1;
	// }
	// if (cmd->CMDYD != cmd->CMDYA) {
	// 	if (cmd->CMDYD >= cmd->CMDYA) cmd->CMDYD += tex_ratio - 1;
	// 	else cmd->CMDYA += tex_ratio - 1;
	// }
	// if (cmd->CMDXC != cmd->CMDXB) {
	// 	if (cmd->CMDXC >= cmd->CMDXB) cmd->CMDXC += tex_ratio - 1;
	// 	else cmd->CMDXB += tex_ratio - 1;
	// }
	// if (cmd->CMDYC != cmd->CMDYB) {
	// 	if (cmd->CMDYC >= cmd->CMDYB) cmd->CMDYC += tex_ratio - 1;
	// 	else cmd->CMDYB += tex_ratio - 1;
  // }
	//
	// //Handle closed point
	// if ((cmd->CMDXD == cmd->CMDXA) && (cmd->CMDYD == cmd->CMDYA)) {
	// 	cmd->CMDXD += tex_ratio - 1;
	// 	cmd->CMDYD += tex_ratio = - 1;
	// }
	// if ((cmd->CMDXC == cmd->CMDXB) && (cmd->CMDYC == cmd->CMDYB)) {
	// 	cmd->CMDXC += tex_ratio - 1;
	// 	cmd->CMDYC += tex_ratio - 1;
	// }

	//Need to detect lines for sega rally or break point since quad as line are only one pixel wide potentially
		int li = computeLinePoints(cmd->CMDXA, cmd->CMDYA, cmd->CMDXD, cmd->CMDYD, &dataL);
		int ri = computeLinePoints(cmd->CMDXB, cmd->CMDYB, cmd->CMDXC, cmd->CMDYC, &dataR);

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
					.dl = (li>1)?((float)(idl/tex_ratio))/(float)((li/tex_ratio)-1):0.5,
					.dr = (ri>1)?((float)(idr/tex_ratio))/(float)((ri/tex_ratio)-1):0.5,
					.flip = cmd->flip,
				};
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
					.dl = (li>1)?((float)(idl/tex_ratio))/(float)((li/tex_ratio)-1):0.5,
					.dr = (ri>1)?((float)(idr/tex_ratio))/(float)((ri/tex_ratio)-1):0.5,
					.flip = cmd->flip,
				};
				memcpy(&cmd_pol[i].G[0], &cmd->G[0], 16*sizeof(float));

				if (abs(a) >= abs(ri)) {
					a -= ri;
					idl++;
				}
		 	}
		}
		drawPolygonLine(cmd_pol, i, cmd->type);
		free(cmd_pol);
		free(dataL);
		free(dataR);
	}

	if (cmd->type == POLYLINE) {
// A revoir l'epaisseur des lignes
		cmd->CMDXA = (cmd->CMDXA*tex_ratio);
		cmd->CMDXB = (cmd->CMDXB*tex_ratio);
		cmd->CMDXC = (cmd->CMDXC*tex_ratio);
		cmd->CMDXD = (cmd->CMDXD*tex_ratio);
		cmd->CMDYA = (cmd->CMDYA*tex_ratio);
		cmd->CMDYB = (cmd->CMDYB*tex_ratio);
		cmd->CMDYC = (cmd->CMDYC*tex_ratio);
		cmd->CMDYD = (cmd->CMDYD*tex_ratio);

		cmd_poly *cmd_pol = (cmd_poly*)calloc(4, sizeof(cmd_poly));
		cmd_pol[0] = (cmd_poly){
			.CMDPMOD = cmd->CMDPMOD,
			.CMDSRCA = cmd->CMDSRCA,
			.CMDSIZE = cmd->CMDSIZE,
			.CMDXA = cmd->CMDXA,
			.CMDYA = cmd->CMDYA,
			.CMDXB = cmd->CMDXB,
			.CMDYB = cmd->CMDYB,
			.CMDCOLR = cmd->CMDCOLR,
			.dl = 0.5,
			.dr = 0.5,
			.flip = cmd->flip
		};
		memcpy(&cmd_pol[0].G[0], &cmd->G[0], 16*sizeof(float));
		cmd_pol[1] = (cmd_poly){
			.CMDPMOD = cmd->CMDPMOD,
			.CMDSRCA = cmd->CMDSRCA,
			.CMDSIZE = cmd->CMDSIZE,
			.CMDXA = cmd->CMDXB,
			.CMDYA = cmd->CMDYB,
			.CMDXB = cmd->CMDXC,
			.CMDYB = cmd->CMDYC,
			.CMDCOLR = cmd->CMDCOLR,
			.dl = 0.5,
			.dr = 0.5,
			.flip = cmd->flip
		};
		memcpy(&cmd_pol[1].G[0], &cmd->G[0], 16*sizeof(float));
		cmd_pol[2] = (cmd_poly){
			.CMDPMOD = cmd->CMDPMOD,
			.CMDSRCA = cmd->CMDSRCA,
			.CMDSIZE = cmd->CMDSIZE,
			.CMDXA = cmd->CMDXC,
			.CMDYA = cmd->CMDYC,
			.CMDXB = cmd->CMDXD,
			.CMDYB = cmd->CMDYD,
			.CMDCOLR = cmd->CMDCOLR,
			.dl = 0.5,
			.dr = 0.5,
			.flip = cmd->flip
		};
		memcpy(&cmd_pol[2].G[0], &cmd->G[0], 16*sizeof(float));
		cmd_pol[3] = (cmd_poly){
			.CMDPMOD = cmd->CMDPMOD,
			.CMDSRCA = cmd->CMDSRCA,
			.CMDSIZE = cmd->CMDSIZE,
			.CMDXA = cmd->CMDXD,
			.CMDYA = cmd->CMDYD,
			.CMDXB = cmd->CMDXA,
			.CMDYB = cmd->CMDYA,
			.CMDCOLR = cmd->CMDCOLR,
			.dl = 0.5,
			.dr = 0.5,
			.flip = cmd->flip
		};
		memcpy(&cmd_pol[3].G[0], &cmd->G[0], 16*sizeof(float));
		drawPolygonLine(cmd_pol, 4, POLYLINE);
		free(cmd_pol);
	}

	if (cmd->type == LINE) {
		// A revoir l'epaisseur des lignes
		cmd->CMDXA = (cmd->CMDXA*tex_ratio);
		cmd->CMDXB = (cmd->CMDXB*tex_ratio);
		cmd->CMDYA = (cmd->CMDYA*tex_ratio);
		cmd->CMDYB = (cmd->CMDYB*tex_ratio);
		cmd_poly *cmd_pol = (cmd_poly*)calloc(1, sizeof(cmd_poly));
		cmd_pol[0] = (cmd_poly){
			.CMDPMOD = cmd->CMDPMOD,
			.CMDSRCA = cmd->CMDSRCA,
			.CMDSIZE = cmd->CMDSIZE,
			.CMDXA = cmd->CMDXA,
			.CMDYA = cmd->CMDYA,
			.CMDXB = cmd->CMDXB,
			.CMDYB = cmd->CMDYB,
			.CMDCOLR = cmd->CMDCOLR,
			.dl = 0.5,
			.dr = 0.5,
			.flip = cmd->flip
		};
		memcpy(&cmd_pol[0].G[0], &cmd->G[0], 16*sizeof(float));
		drawPolygonLine(cmd_pol, 1, cmd->type);
		free(cmd_pol);
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
	if ((cmd_pol->CMDPMOD & 0x8000) == 0)
		delta += 1 + (cmd_pol->CMDPMOD & 0x7);

	if (cmd_pol->CMDPMOD & 0x0100)
		delta += DRAW_POLY_MSB_SHADOW_MESH - DRAW_POLY_MSB_SHADOW_NO_MESH;

	return progId+delta;
}
static int oldProg = -1;
void drawPolygonLine(cmd_poly* cmd_pol, int nbLines, u32 type) {
	int progId = getProgramLine(&cmd_pol[0], type);
	// trace_prog(progId);
	if (progId == DRAW_POLY_UNSUPPORTED_MESH) return;
	if (progId == DRAW_POLY_UNSUPPORTED_NO_MESH) return;
	if (progId == DRAW_QUAD_UNSUPPORTED_MESH) return;
	if (progId == DRAW_QUAD_UNSUPPORTED_NO_MESH) return;

	if (prg_vdp1[progId] == 0) {
		prg_vdp1[progId] = createProgram(sizeof(a_prg_vdp1[progId]) / sizeof(char*), (const GLchar**)a_prg_vdp1[progId]);
	}
	if ((oldProg != -1) && (oldProg != progId)) {
		//CleanUp mesh texture
		vdp1_clear_mesh();
	}
	oldProg = progId;

	glUseProgram(prg_vdp1[progId]);

	glBindImageTexture(0, compute_tex[_Ygl->drawframe], 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, ssbo_cmd_line_list_);

	if (Vdp1External.updateVdp1Ram != 0) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_vdp1ram_);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, vdp1Ram_update_start, vdp1Ram_update_end-vdp1Ram_update_start, (void*)&Vdp1Ram[0]);
		vdp1Ram_update_start = 0x80000;
		vdp1Ram_update_end = 0x0;
		Vdp1External.updateVdp1Ram = 0;
	}
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo_vdp1ram_);

	glUniform2i(7, tex_ratio, tex_ratio);
	glUniform2i(8, (Vdp1Regs->systemclipX2+1)*tex_ratio-1, (Vdp1Regs->systemclipY2+1)*tex_ratio-1);
	glUniform4i(9, Vdp1Regs->userclipX1*tex_ratio, Vdp1Regs->userclipY1*tex_ratio, (Vdp1Regs->userclipX2+1)*tex_ratio-1, (Vdp1Regs->userclipY2+1)*tex_ratio-1);

	for (int i = 0; i<nbLines; i+=32) {
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo_cmd_line_list_);
		int nbUpload = MIN(32,(nbLines - i));
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, nbUpload*sizeof(cmd_poly), (void*)&cmd_pol[i]);
		glUniform1i(10, nbUpload);
		glDispatchCompute(1, 1, 1); //might be better to launch only the right number of workgroup
		ErrorHandle("glDispatchCompute");
	}
	progMask = 0;

	glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
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
	snprintf(vdp1_draw_line_start_f,length,vdp1_draw_line_start_f_base,1,32);

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
	progMask = 0;
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
