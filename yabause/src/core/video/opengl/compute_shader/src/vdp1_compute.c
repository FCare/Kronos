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

#define NB_COARSE_RAST (NB_COARSE_RAST_X * NB_COARSE_RAST_Y)

extern vdp2rotationparameter_struct  Vdp1ParaA;

static int local_size_x = 8;
static int local_size_y = 8;


static int tex_width;
static int tex_height;
static int tex_ratio;
static int struct_size;

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


static u32 write_fb[2][512*256];

static const GLchar * a_prg_vdp1[NB_PRG][5] = {
  //VDP1_MESH_STANDARD - BANDING
	{
		vdp1_start_f,
		vdp1_standard_mesh_f,
		vdp1_banding_f,
		vdp1_continue_no_mesh_f,
		vdp1_end_f
	},
	//VDP1_MESH_IMPROVED - BANDING
	{
		vdp1_start_f,
		vdp1_banding_f,
		vdp1_improved_mesh_f,
		vdp1_continue_mesh_f,
		vdp1_end_mesh_f
	},
	//VDP1_MESH_STANDARD - NO BANDING
	{
		vdp1_start_f,
		vdp1_standard_mesh_f,
		vdp1_no_banding_f,
		vdp1_continue_no_mesh_f,
		vdp1_end_f
	},
	//VDP1_MESH_IMPROVED- NO BANDING
	{
		vdp1_start_f,
		vdp1_no_banding_f,
		vdp1_improved_mesh_f,
		vdp1_continue_mesh_f,
		vdp1_end_mesh_f
	},
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

int vdp1_add(vdp1cmd_struct* cmd, int clipcmd) {
	int minx = 1024;
	int miny = 1024;
	int maxx = 0;
	int maxy = 0;
	point A,B;

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

void startVdp1Render() {
	if (oldProg == -1) return;
	glUseProgram(prg_vdp1[oldProg]);
	glBindImageTexture(0, get_vdp1_tex(_Ygl->drawframe), 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
	glBindImageTexture(1, get_vdp1_mesh(_Ygl->drawframe), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, ssbo_vdp1ram_);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, ssbo_cmd_list_);
	glUniform2f(7, tex_ratio, tex_ratio);
	glUniform2i(8, Vdp1Regs->systemclipX2, Vdp1Regs->systemclipY2);
	glUniform4i(9, Vdp1Regs->userclipX1, Vdp1Regs->userclipY1, Vdp1Regs->userclipX2, Vdp1Regs->userclipY2);
	glUniform2i(10, 0, 0);
}

void endVdp1Render() {
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

  int am = sizeof(vdp1cmd_struct) % 16;
  tex_width = width;
  tex_height = height;
	tex_ratio = (int)ratio;
  struct_size = sizeof(vdp1cmd_struct);
  if (am != 0) {
    struct_size += 16 - am;
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
		startVdp1Render();
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
