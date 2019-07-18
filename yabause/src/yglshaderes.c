/*  Copyright 2005-2006 Guillaume Duhamel
    Copyright 2005-2006 Theo Berkau
    Copyright 2011-2015 Shinya Miyamoto(devmiyax)

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/


//#ifdef __ANDROID__
#include <stdlib.h>
#include <math.h>
#include "ygl.h"
#include "yui.h"
#include "vidshared.h"
#include "bicubic_shader.h"
#include "scanline_shader.h"

#define YGLLOG

static int saveFB;
static void Ygl_useTmpBuffer();
static void Ygl_releaseTmpBuffer(void);
int YglBlitMosaic(u32 srcTexture, float w, float h, float * matrix, int * mosaic);
int YglBlitPerLineAlpha(u32 srcTexture, float w, float h, float * matrix, u32 perline);

extern int GlHeight;
extern int GlWidth;
static GLuint _prgid[PG_MAX] = { 0 };

#ifdef DEBUG_PROG
#define GLUSEPROG(A) printf("use prog %s (%d) @ %d\n", #A, A, __LINE__);glUseProgram(A)
#else
#define GLUSEPROG(A) glUseProgram(A)
#endif

static void Ygl_printShaderError( GLuint shader )
{
  GLsizei bufSize;

  glGetShaderiv(shader, GL_INFO_LOG_LENGTH , &bufSize);

  if (bufSize > 1) {
    GLchar *infoLog;

    infoLog = (GLchar *)malloc(bufSize);
    if (infoLog != NULL) {
      GLsizei length;
      glGetShaderInfoLog(shader, bufSize, &length, infoLog);
      YuiMsg("Shaderlog:\n%s\n", infoLog);
      free(infoLog);
    }
  }
}

void Ygl_Vdp1CommonGetUniformId(GLuint pgid, YglVdp1CommonParam * param){

  param->texsize = glGetUniformLocation(pgid, (const GLchar *)"u_texsize");
  param->sprite = glGetUniformLocation(pgid, (const GLchar *)"u_sprite");
  param->tessLevelInner = glGetUniformLocation(pgid, (const GLchar *)"TessLevelInner");
  param->tessLevelOuter = glGetUniformLocation(pgid, (const GLchar *)"TessLevelOuter");
  param->fbo = glGetUniformLocation(pgid, (const GLchar *)"u_fbo");
  param->fbo_attr = glGetUniformLocation(pgid, (const GLchar *)"u_fbo_attr");
  param->mtxModelView = glGetUniformLocation(pgid, (const GLchar *)"u_mvpMatrix");
  param->mtxTexture = glGetUniformLocation(pgid, (const GLchar *)"u_texMatrix");
  param->tex0 = glGetUniformLocation(pgid, (const GLchar *)"s_texture");
}

int Ygl_uniformVdp1CommonParam(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id){

  YglProgram * prg;
  YglVdp1CommonParam * param;

  prg = p;
  param = prg->ids;

  glEnableVertexAttribArray(prg->vertexp);
  glEnableVertexAttribArray(prg->texcoordp);
  if (prg->vertexAttribute != NULL)
  {
    glEnableVertexAttribArray(prg->vaid);
  }
  else{
    glDisableVertexAttribArray(prg->vaid);
  }

  if (param == NULL) return 0;

  glUniform2f(param->texsize, YglTM_vdp1[_Ygl->drawframe]->width, YglTM_vdp1[_Ygl->drawframe]->height);

  if (param->sprite != -1){
    glUniform1i(param->sprite, 0);
  }

  if (param->tessLevelInner != -1) {
    glUniform1f(param->tessLevelInner, (float)TESS_COUNT);
  }

  if (param->tessLevelOuter != -1) {
    glUniform1f(param->tessLevelOuter, (float)TESS_COUNT);
  }

  if (param->fbo_attr != -1){
    glUniform1i(param->fbo_attr, 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe*2+1]);
    #if !defined(_OGLES3_)
    if (glTextureBarrier) glTextureBarrier();
    else if (glTextureBarrierNV) glTextureBarrierNV();
    else
    #endif
    if (glMemoryBarrier) glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
  }

  if (param->fbo != -1){
    glUniform1i(param->fbo, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe*2]);
    #if !defined(_OGLES3_)
    if (glTextureBarrier) glTextureBarrier();
    else if (glTextureBarrierNV) glTextureBarrierNV();
    else
    #endif
    if (glMemoryBarrier) glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
  }

  if ((param->fbo_attr != -1) || (param->fbo != -1)){
    #if !defined(_OGLES3_)
    if (glTextureBarrier) glTextureBarrier();
    else if (glTextureBarrierNV) glTextureBarrierNV();
    else
    #endif
    if (glMemoryBarrier) glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    glActiveTexture(GL_TEXTURE0);
  }
  return 0;
}

int Ygl_uniformVdp1ShadowParam(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id){

  YglProgram * prg;
  YglVdp1CommonParam * param;

  prg = p;
  param = prg->ids;

  glEnableVertexAttribArray(prg->vertexp);
  glEnableVertexAttribArray(prg->texcoordp);

  glStencilFunc(GL_GREATER,1,0x01);
  glStencilMask(0x01);
  glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
  glEnable(GL_STENCIL_TEST);

  if (param == NULL) return 0;

  glUniform2f(param->texsize, YglTM_vdp1[_Ygl->drawframe]->width, YglTM_vdp1[_Ygl->drawframe]->height);

  if (param->sprite != -1){
    glUniform1i(param->sprite, 0);
  }

  if (param->tessLevelInner != -1) {
    glUniform1f(param->tessLevelInner, (float)TESS_COUNT);
  }

  if (param->tessLevelOuter != -1) {
    glUniform1f(param->tessLevelOuter, (float)TESS_COUNT);
  }

  if (param->fbo_attr != -1){
    glUniform1i(param->fbo_attr, 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe*2+1]);
    #if !defined(_OGLES3_)
    if (glTextureBarrier) glTextureBarrier();
    else if (glTextureBarrierNV) glTextureBarrierNV();
    else
    #endif
    if (glMemoryBarrier) glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);
    glActiveTexture(GL_TEXTURE0);
  }


  return 0;
}

int Ygl_cleanupVdp1CommonParam(void * p, YglTextureManager *tm){
  YglProgram * prg;
  prg = p;
  glDisableVertexAttribArray(prg->vaid);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  return 0;
}

int Ygl_cleanupVdp1ShadowParam(void * p, YglTextureManager *tm){
  glDisable(GL_STENCIL_TEST);
  return 0;
}


/*------------------------------------------------------------------------------------
 *  Normal Draw
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_normal_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;    \n"
      "layout (location = 0) in vec4 a_position;   \n"
      "layout (location = 1) in vec4 a_texcoord;   \n"
      "out  highp vec4 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "   v_texcoord  = a_texcoord; \n"
      "} ";
const GLchar * pYglprg_vdp2_normal_v[] = {Yglprg_normal_v, NULL};

const GLchar Yglprg_normal_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"precision highp int;\n"
"#endif\n"
"in vec4 v_texcoord;\n"
"uniform vec4 u_color_offset;\n"
"uniform float u_emu_height;\n"
"uniform float u_vheight; \n"
"uniform highp sampler2D s_texture;\n"
"uniform sampler2D s_perline;  \n"
"uniform sampler2D s_window;  \n"
"uniform int is_perline; \n"
"uniform int is_window; \n"
"uniform sampler2D s_color;\n"
"out vec4 fragColor;\n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  ivec2 linepos; \n "
"  vec4 color_offset = u_color_offset; \n"
"  linepos.y = 0; \n "
"  linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"  addr.x = int(v_texcoord.x);  \n"
"  addr.y = int(v_texcoord.y);  \n"
"  if (is_window !=0) {\n"
"    vec4 win = texelFetch( s_window, ivec2(gl_FragCoord.xy), 0 );\n"
"    if (win.a == 0.0) discard;\n"
"  }\n"
"  vec4 txcol = texelFetch( s_texture, addr,0 );         \n"
"  int msb = int(txcol.b * 255.0)&0x1; \n"
"  if (is_perline == 1) {\n"
"    vec4 perline = texelFetch( s_perline, linepos,0 ); \n"
"    if (perline == vec4(0.0)) discard;\n"
"    color_offset.rgb = (perline.rgb - vec3(0.5))*2.0;\n"
"    if (perline.a > 0.0) txcol.a = perline.a;\n"
"  } \n"
"  fragColor.rgb = clamp(txcol.rgb+color_offset.rgb,vec3(0.0),vec3(1.0));\n"
"  int blue = int(fragColor.b * 255.0) & 0xFE;\n"
"  fragColor.b = float(blue|msb)/255.0;\n" //Blue LSB bit is used for special color calculation
"  fragColor.a = txcol.a;\n"
"}  \n";

const GLchar * pYglprg_vdp2_normal_f[] = {Yglprg_normal_f, NULL};
static int id_normal_s_texture = -1;
static int id_normal_color_offset = -1;
static int id_normal_matrix = -1;
static int id_normal_s_window = -1;
static int id_normal_iswindow = -1;
static int id_normal_vheight = -1;
static int id_normal_emu_height = -1;


int Ygl_uniformNormal(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{

  YglProgram * prg;
  prg = p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_normal_s_texture, 0);
  glUniform1i(id_normal_s_window, 3);
  glUniform1i(id_normal_iswindow, _Ygl->use_win[id] != 0);
  glUniform4fv(prg->color_offset, 1, prg->color_offset_val);
  glUniform1i(id_normal_vheight, (float)_Ygl->rheight);
  glUniform1i(id_normal_emu_height,  (float)_Ygl->rheight / (float)_Ygl->rheight);
  return 0;
}

int Ygl_cleanupNormal(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  prg = p;
  return 0;
}


//---------------------------------------------------------
// Draw pxels refering color ram
//---------------------------------------------------------

const GLchar Yglprg_normal_cram_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"precision highp int;\n"
"#endif\n"
"in vec4 v_texcoord;\n"
"uniform vec4 u_color_offset;\n"
"uniform float u_emu_height;\n"
"uniform float u_vheight; \n"
"uniform float u_emu_width;\n"
"uniform float u_vwidth; \n"
"uniform highp sampler2D s_texture;\n"
"uniform sampler2D s_perline;  \n"
"uniform sampler2D s_window;  \n"
"uniform int is_perline; \n"
"uniform int is_window; \n"
"uniform sampler2D s_color;\n"
"out vec4 fragColor;\n"
"void main()\n"
"{\n"
"  ivec2 linepos; \n "
"  vec4 color_offset = u_color_offset; \n"
"  linepos.y = 0; \n "
"  linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"  ivec2 addr = ivec2(int(v_texcoord.x),int(v_texcoord.y));\n"
"  if (is_window !=0) {\n"
"    vec4 win = texelFetch( s_window, ivec2(gl_FragCoord.xy * vec2(u_emu_width, u_emu_height)),0 );\n"
"    if (win.a == 0.0) discard;\n"
"  }\n"
"  vec4 txindex = texelFetch( s_texture, addr ,0 );\n"
"  if(txindex.a == 0.0) { discard; }\n"
"  vec4 txcol = texelFetch( s_color,  ivec2( int(txindex.g*255.0)<<8 | int(txindex.r*255.0) ,0 )  , 0 );\n"
"  int msb = int(txindex.b * 255.0)&0x1; \n"
"  if (is_perline == 1) {\n"
"    vec4 perline = texelFetch( s_perline, linepos,0 ); \n"
"    if (perline == vec4(0.0)) discard;\n"
"    color_offset.rgb = (perline.rgb - vec3(0.5))*2.0;\n"
"    if (perline.a > 0.0) txindex.a = float(int(perline.a * 255.0) | (int(txindex.a * 255.0) & 0x7))/255.0;\n"
"  } \n"
"  fragColor = clamp(txcol+color_offset,vec4(0.0),vec4(1.0));\n"
"  int blue = int(fragColor.b * 255.0) & 0xFE;\n"
"  fragColor.b = float(blue|msb)/255.0;\n" //Blue LSB bit is used for special color calculation
"  fragColor.a = txindex.a; \n"
"}\n";


const GLchar * pYglprg_normal_cram_f[] = { Yglprg_normal_cram_f, NULL };
static int id_normal_cram_s_texture = -1;
static int id_normal_cram_s_perline = -1;
static int id_normal_cram_s_window = -1;
static int id_normal_cram_isperline = -1;
static int id_normal_cram_iswindow = -1;
static int id_normal_cram_emu_height = -1;
static int id_normal_cram_emu_width = -1;
static int id_normal_cram_vheight = -1;
static int id_normal_cram_vwidth = -1;
static int id_normal_cram_s_color = -1;
static int id_normal_cram_color_offset = -1;
static int id_normal_cram_matrix = -1;


int Ygl_uniformNormalCram(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{

  YglProgram * prg;
  prg = p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_normal_cram_s_texture, 0);
  glUniform1i(id_normal_cram_s_color, 1);
  glUniform1i(id_normal_cram_s_perline, 2);
  glUniform1i(id_normal_cram_s_window, 3);
  glUniform1i(id_normal_cram_iswindow, _Ygl->use_win[id] != 0);
  glUniform1i(id_normal_cram_isperline, (_Ygl->perLine[id] != 0));
  if ((id == RBG0) && (_Ygl->rbg_use_compute_shader)){
    glUniform1f(id_normal_cram_emu_height, (float)_Ygl->rheight / (float)_Ygl->height);
    glUniform1f(id_normal_cram_emu_width, (float)_Ygl->rwidth / (float)_Ygl->width);
    glUniform1f(id_normal_cram_vheight, (float)_Ygl->height);
    glUniform1f(id_normal_cram_vwidth, (float)_Ygl->width);
  } else {
    glUniform1f(id_normal_cram_emu_height, 1.0f);
    glUniform1f(id_normal_cram_emu_width, 1.0f);
    glUniform1f(id_normal_cram_vheight, (float)_Ygl->rheight);
    glUniform1f(id_normal_cram_vwidth, (float)_Ygl->rwidth);
  }
  glUniform4fv(prg->color_offset, 1, prg->color_offset_val);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, _Ygl->perLine[id]);
  return 0;
}

int Ygl_cleanupNormalCram(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, 0);
  prg = p;
  return 0;
}

const GLchar Yglprg_normal_cram_addcol_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"precision highp int;\n"
"#endif\n"
"in vec4 v_texcoord;\n"
"uniform vec4 u_color_offset;\n"
"uniform highp sampler2D s_texture;\n"
"uniform sampler2D s_color;\n"
"out vec4 fragColor;\n"
"void main()\n"
"{\n"
"  vec4 txindex = texelFetch( s_texture, ivec2(int(v_texcoord.x),int(v_texcoord.y)) ,0 );\n"
"  if(txindex.a == 0.0) { discard; }\n"
"  vec4 txcol = texelFetch( s_color,  ivec2( ( int(txindex.g*255.0)<<8 | int(txindex.r*255.0)) ,0 )  , 0 );\n"
"  fragColor = txcol+u_color_offset;\n"
"  fragColor.a = txindex.a;\n"
"}\n";

const GLchar * pYglprg_normal_cram_addcol_f[] = { Yglprg_normal_cram_addcol_f, NULL };
static int id_normal_cram_s_texture_addcol = -1;
static int id_normal_cram_s_color_addcol = -1;
static int id_normal_cram_color_offset_addcol = -1;
static int id_normal_cram_matrix_addcol = -1;


int Ygl_uniformAddColCram(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{

  YglProgram * prg;
  prg = p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_normal_cram_s_texture_addcol, 0);
  glUniform1i(id_normal_cram_s_color_addcol, 1);
  glUniform4fv(id_normal_cram_color_offset_addcol, 1, prg->color_offset_val);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
  return 0;
}

int Ygl_cleanupAddColCram(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  glActiveTexture(GL_TEXTURE0);
  prg = p;
  return 0;
}

static void Ygl_useTmpBuffer(){
  float col[4] = {0.0f,0.0f,0.0f,0.0f};
  // Create Screen size frame buffer
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &saveFB);
  if (_Ygl->tmpfbo == 0) {

    GLuint error;
    glGenTextures(1, &_Ygl->tmpfbotex);
    glBindTexture(GL_TEXTURE_2D, _Ygl->tmpfbotex);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_Ygl->tmpfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->tmpfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->tmpfbotex, 0);
  }
  // bind Screen size frame buffer
  else{
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->tmpfbo);
    glClearBufferfv(GL_COLOR, 0, col);
  }
}

static void Ygl_releaseTmpBuffer(void) {
  glBindFramebuffer(GL_FRAMEBUFFER, saveFB);
}

int Ygl_useUpscaleBuffer(void){
  // Create Screen size frame buffer
  int up_scale = 1;
  switch (_Ygl->upmode) {
    case UP_HQ4X:
    case UP_4XBRZ:
      up_scale = 4;
      break;
    case UP_2XBRZ:
      up_scale = 2;
      break;
    default:
      up_scale = 1;
  }
  if (_Ygl->upfbo == 0) {
    GLuint error;
    glGenTextures(1, &_Ygl->upfbotex);
    glBindTexture(GL_TEXTURE_2D, _Ygl->upfbotex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, up_scale*_Ygl->rwidth, up_scale*_Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &_Ygl->upfbo);
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->upfbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->upfbotex, 0);
  }
  // bind Screen size frame buffer
  else{
    glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->upfbo);
  }
  return up_scale;
}

/*------------------------------------------------------------------------------------
*  Mosaic Draw
* ----------------------------------------------------------------------------------*/
int Ygl_uniformMosaic(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{
  YglProgram * prg;
  prg = p;

  Ygl_useTmpBuffer();
  glViewport(0, 0, _Ygl->rwidth, _Ygl->rheight);
  glScissor(0, 0, _Ygl->rwidth, _Ygl->rheight);

  if (prg->prgid == PG_VDP2_MOSAIC_CRAM ) {
    glEnableVertexAttribArray(prg->vertexp);
    glEnableVertexAttribArray(prg->texcoordp);
    glUniform1i(id_normal_cram_s_texture, 0);
    glUniform1i(id_normal_cram_s_color, 1);
    glUniform4fv(prg->color_offset, 1, prg->color_offset_val);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tm->textureID);

  }
  else {
    glEnableVertexAttribArray(prg->vertexp);
    glEnableVertexAttribArray(prg->texcoordp);
    glUniform1i(id_normal_s_texture, 0);
    glUniform1i(id_normal_s_window, 3);
    glUniform1i(id_normal_iswindow, _Ygl->use_win[id] != 0);
    glUniform4fv(prg->color_offset, 1, prg->color_offset_val);
    glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);
  }

  return 0;
}

int Ygl_cleanupMosaic(void * p, YglTextureManager *tm)
{
  YglProgram * prg;
  prg = p;

  // Bind Default frame buffer
  Ygl_releaseTmpBuffer();

  // Restore Default Matrix
  glViewport(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);
  glScissor(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);

  // call blit method
  YglBlitMosaic(_Ygl->tmpfbotex, _Ygl->rwidth, _Ygl->rheight, prg->matrix, prg->mosaic);

  glBindTexture(GL_TEXTURE_2D, tm->textureID);

  return 0;
}

/*------------------------------------------------------------------------------------
 *  UserClip Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_userclip_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;    \n"
      "layout (location = 0) in vec4 a_position;               \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "} ";
const GLchar * pYglprg_userclip_v[] = {Yglprg_userclip_v, NULL};

const GLchar Yglprg_userclip_f[] =
      SHADER_VERSION
      "#ifdef GL_ES\n"
      "precision highp float;                            \n"
      "#endif\n"
      "out vec4 fragColor;            \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  fragColor = vec4( 0.0 );\n"
      "}                                                   \n";
const GLchar * pYglprg_userclip_f[] = {Yglprg_userclip_f, NULL};

/*------------------------------------------------------------------------------------
 *  Window Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_window_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;    \n"
      "layout (location = 0) in vec4 a_position;    \n"
      "void main()       \n"
      "{ \n"
      "   gl_Position = a_position; \n"
      "} ";
const GLchar * pYglprg_window_v[] = {Yglprg_window_v, NULL};

const GLchar Yglprg_window_f[] =
      SHADER_VERSION
      "#ifdef GL_ES\n"
      "precision highp float; \n"
      "#endif\n"
      "out vec4 fragColor; \n"
      "uniform sampler2D s_win0;  \n"
      "uniform sampler2D s_win1;  \n"
      "uniform float u_vheight; \n"
      "uniform int win0; \n"
      "uniform int win1; \n"
      "uniform int winOp; \n"
      "uniform int win0mode;  \n"
      "uniform int win1mode;  \n"
      "void main()   \n"
      "{  \n"
      "  ivec2 linepos; \n "
      "  int validw0 = 1; \n"
      "  int validw1 = 1; \n"
      "  linepos.y = 0; \n "
      "  linepos.x = int(u_vheight-gl_FragCoord.y);\n"
      "  int pos = int(gl_FragCoord.x);\n"
      "  int valid = 0; \n"
      "  if (winOp != 0) valid = 1;\n"
      "  if (win0 != 0) {\n"
      "    vec4 lineW0 = texelFetch(s_win0,linepos,0);\n"
      "    int startW0 = int(lineW0.r*255.0) | (int(lineW0.g*255.0)<<8);\n"
      "    int endW0 = int(lineW0.b*255.0) | (int(lineW0.a*255.0)<<8);\n"
      "    if (win0mode != 0) { \n"
      "      if ((startW0 < endW0) && ((pos < startW0) || (pos >= endW0))) validw0 = 0;\n"
      "      if (startW0 == endW0) validw0 = 0;\n"
      "    } else { \n"
      "      if ((startW0 < endW0) && ((pos >= startW0) && (pos < endW0))) validw0 = 0;\n"
      "    }\n"
      "  } else validw0 = valid;\n"
      "  if (win1 != 0) {\n"
      "    vec4 lineW1 = texelFetch(s_win1,linepos,0);\n"
      "    int startW1 = int(lineW1.r*255.0) | (int(lineW1.g*255.0)<<8);\n"
      "    int endW1 = int(lineW1.b*255.0) | (int(lineW1.a*255.0)<<8);\n"
      "    if (win1mode != 0) { \n"
      "      if ((startW1 < endW1) && ((pos < startW1) || (pos >= endW1))) validw1 = 0;\n"
      "      if (startW1 == endW1) validw1 = 0;\n"
      "    } else { \n"
      "      if ((startW1 < endW1) && ((pos >= startW1) && (pos < endW1))) validw1 = 0;\n"
      "    }\n"
      "  } else validw1 = valid;\n"
      "  if (winOp != 0) { \n"
      "    if ((validw0 == 1) && (validw1 == 1)) fragColor = vec4( 0.0, 0.0, 0.0, 1.0 );\n"
      "    else discard;\n"
      "  } else { \n"
      "    if ((validw0 == 1) || (validw1 == 1)) fragColor = vec4( 0.0, 0.0, 0.0, 1.0 );\n"
      "    else discard;\n"
      "  }\n"
      "}  \n";
const GLchar * pYglprg_window_f[] = {Yglprg_window_f, NULL};

int Ygl_uniformWindow(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{
   YglProgram * prg;
   prg = p;
   GLUSEPROG(prg->prgid );
   glUniform1i(_Ygl->windowpg.tex0, 0);
   glUniform1i(_Ygl->windowpg.tex1, 1);
   glUniform1f(_Ygl->windowpg.vheight, (float)_Ygl->rheight);
   glEnableVertexAttribArray(0);
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(2);
   return 0;
}

int Ygl_cleanupWindow(void * p, YglTextureManager *tm )
{
   YglProgram * prg;
   prg = p;
   return 0;
}

/*------------------------------------------------------------------------------------
 *  VDP1 Normal Draw
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_normal_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;    \n"
      "uniform vec2 u_texsize;    \n"
      "layout (location = 0) in vec4 a_position;   \n"
      "layout (location = 1) in vec4 a_texcoord;   \n"
      "out   vec4 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "   v_texcoord  = a_texcoord; \n"
      "   v_texcoord.x  = v_texcoord.x / u_texsize.x;\n"
      "   v_texcoord.y  = v_texcoord.y / u_texsize.y;\n"
      "} ";
const GLchar * pYglprg_vdp1_normal_v[] = {Yglprg_vdp1_normal_v, NULL};

const GLchar Yglprg_vpd1_normal_f[] =
      SHADER_VERSION
      "#ifdef GL_ES\n"
      "precision highp float; \n"
      "#endif\n"
      "in vec4 v_texcoord; \n"
      "uniform sampler2D s_texture;  \n"
      "out vec4 fragColor; \n"
      "void main()   \n"
      "{  \n"
      "  vec2 addr = v_texcoord.st;  \n"
      "  addr.s = addr.s / (v_texcoord.q);      \n"
      "  addr.t = addr.t / (v_texcoord.q);      \n"
      "  vec4 FragColor = texture( s_texture, addr );      \n"
      "  /*if( FragColor.a == 0.0 ) discard;*/     \n"
      "  fragColor = FragColor;\n "
      "}  \n";
const GLchar * pYglprg_vdp1_normal_f[] = {Yglprg_vpd1_normal_f, NULL};
static int id_vdp1_normal_s_texture_size = -1;
static int id_vdp1_normal_s_texture = -1;

int Ygl_uniformVdp1Normal(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id)
{
   YglProgram * prg;
   prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   glUniform1i(id_vdp1_normal_s_texture, 0);
   glUniform2f(id_vdp1_normal_s_texture_size, YglTM_vdp1[_Ygl->drawframe]->width, YglTM_vdp1[_Ygl->drawframe]->height);
   return 0;
}

int Ygl_cleanupVdp1Normal(void * p, YglTextureManager *tm )
{
   YglProgram * prg;
   prg = p;
   return 0;
}

/*------------------------------------------------------------------------------------
*  VDP1 GlowShading Operation with tessellation
* ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_gouraudshading_tess_v[] =
SHADER_VERSION_TESS
"layout (location = 0) in vec3 a_position; \n"
"layout (location = 1) in vec4 a_texcoord; \n"
"layout (location = 2) in vec4 a_grcolor;  \n"
"uniform vec2 u_texsize;    \n"
"out vec3 v_position;  \n"
"out vec4 v_texcoord; \n"
"out vec4 v_vtxcolor; \n"
"void main() {     \n"
"   v_position  = a_position; \n"
"   v_vtxcolor  = a_grcolor;  \n"
"   v_texcoord  = a_texcoord; \n"
"   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
"   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
"}\n";
const GLchar * pYglprg_vdp1_gouraudshading_tess_v[] = { Yglprg_vdp1_gouraudshading_tess_v, NULL };

const GLchar Yglprg_tess_c[] =
SHADER_VERSION_TESS
"layout(vertices = 4) out; //<???? what does it means? \n"
"in vec3 v_position[];  \n"
"in vec4 v_texcoord[]; \n"
"in vec4 v_vtxcolor[]; \n"
"out vec3 tcPosition[]; \n"
"out vec4 tcTexCoord[]; \n"
"out vec4 tcColor[]; \n"
"uniform float TessLevelInner; \n"
"uniform float TessLevelOuter; \n"
" \n"
"#define ID gl_InvocationID \n"
" \n"
"void main()  \n"
"{  \n"
"	tcPosition[ID] = v_position[ID];  \n"
"	tcTexCoord[ID] = v_texcoord[ID];  \n"
"	tcColor[ID] = v_vtxcolor[ID];  \n"
" \n"
"	if (ID == 0) {  \n"
"		gl_TessLevelInner[0] = TessLevelInner;  \n"
"		gl_TessLevelInner[1] = TessLevelInner;  \n"
"		gl_TessLevelOuter[0] = TessLevelOuter;  \n"
"		gl_TessLevelOuter[1] = TessLevelOuter; \n"
"		gl_TessLevelOuter[2] = TessLevelOuter; \n"
"		gl_TessLevelOuter[3] = TessLevelOuter; \n"
"	} \n"
"} \n";
const GLchar * pYglprg_vdp1_gouraudshading_tess_c[] = { Yglprg_tess_c, NULL };

const GLchar Yglprg_tess_e[] =
SHADER_VERSION_TESS
"layout(quads, equal_spacing, ccw) in; \n"
"in vec3 tcPosition[]; \n"
"in vec4 tcTexCoord[]; \n"
"in vec4 tcColor[]; \n"
"out vec4 teTexCoord; \n"
"out vec4 teColor; \n"
"uniform mat4 u_mvpMatrix; \n"
" \n"
"void main() \n"
"{ \n"
"	float u = gl_TessCoord.x, v = gl_TessCoord.y; \n"
"	vec3 tePosition; \n"
"	vec3 a = mix(tcPosition[0], tcPosition[3], u); \n"
"	vec3 b = mix(tcPosition[1], tcPosition[2], u); \n"
"	tePosition = mix(a, b, v); \n"
"	gl_Position = vec4(tePosition, 1)*u_mvpMatrix; \n"
"	vec4 ta = mix(tcTexCoord[0], tcTexCoord[3], u); \n"
"	vec4 tb = mix(tcTexCoord[1], tcTexCoord[2], u); \n"
"	teTexCoord = mix(ta, tb, v); \n"
"	vec4 ca = mix(tcColor[0], tcColor[3], u); \n"
"	vec4 cb = mix(tcColor[1], tcColor[2], u); \n"
"	teColor = mix(ca, cb, v); \n"
"} \n";
const GLchar * pYglprg_vdp1_gouraudshading_tess_e[] = { Yglprg_tess_e, NULL };

const GLchar Yglprg_tess_g[] =
SHADER_VERSION_TESS
"uniform mat4 Modelview; \n"
"uniform mat3 NormalMatrix; \n"
"layout(triangles) in; \n"
"layout(triangle_strip, max_vertices = 3) out; \n"
"in vec4 teTexCoord[3]; \n"
"in vec4 teColor[3]; \n"
"out vec4 v_texcoord; \n"
"out vec4 v_vtxcolor; \n"
" \n"
"void main() \n"
"{ \n"
"	v_texcoord = teTexCoord[0]; \n"
"	v_vtxcolor = teColor[0]; \n"
"	gl_Position = gl_in[0].gl_Position; EmitVertex(); \n"
" \n"
"	v_texcoord = teTexCoord[1]; \n"
"	v_vtxcolor = teColor[1]; \n"
"	gl_Position = gl_in[1].gl_Position; EmitVertex(); \n"
" \n"
"	v_texcoord = teTexCoord[2]; \n"
"	v_vtxcolor = teColor[2]; \n"
"	gl_Position = gl_in[2].gl_Position; EmitVertex(); \n"
" \n"
"	EndPrimitive(); \n"
"} \n";
const GLchar * pYglprg_vdp1_gouraudshading_tess_g[] = { Yglprg_tess_g, NULL };

static YglVdp1CommonParam id_gt = { 0 };


/*------------------------------------------------------------------------------------
 *  VDP1 GlowShading Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_gouraudshading_v[] =
SHADER_VERSION
"uniform mat4 u_mvpMatrix;     \n"
"uniform vec2 u_texsize;    \n"
"layout (location = 0) in vec4 a_position;    \n"
"layout (location = 1) in vec4 a_texcoord;    \n"
"layout (location = 2) in vec4 a_grcolor;     \n"
"out  vec4 v_texcoord;    \n"
"out  vec4 v_vtxcolor;    \n"
"void main() { \n"
"   v_vtxcolor  = a_grcolor;   \n"
"   v_texcoord  = a_texcoord; \n"
"   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
"   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
"   gl_Position = a_position*u_mvpMatrix; \n"
"}\n";
const GLchar * pYglprg_vdp1_gouraudshading_v[] = {Yglprg_vdp1_gouraudshading_v, NULL};

const GLchar Yglprg_vdp1_gouraudshading_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"uniform sampler2D u_sprite;\n"
"in vec4 v_texcoord;\n"
"in vec4 v_vtxcolor;\n"
"out vec4 fragColor; \n"
"out vec4 fragColorAttr; \n"
"void main() {\n"
"  ivec2 addr = ivec2(vec2(textureSize(u_sprite, 0)) * v_texcoord.st / v_texcoord.q); \n"
"  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
"  if( spriteColor.a == 0.0 ) discard;\n"
"  int shadow = 0;\n"
"  int additionnal = int(spriteColor.a * 255.0)&0xF8;\n"
"  if (additionnal == 0xC0) {\n"
"    if (((int(spriteColor.b * 255.0)&0xFE)==0x80) && (spriteColor.rg == vec2(0.0))) shadow = 1;\n"
"  }\n"
"  if (shadow != 0) {\n"
"    fragColorAttr.rgb = vec3(0.0);\n"
"    fragColorAttr.a = 0.5;\n"
"    fragColor.rgb = vec3(0.0);\n"
"  } else { \n"
"    fragColorAttr = vec4(0.0);\n"
"    fragColor.rgb  = clamp(spriteColor.rgb+v_vtxcolor.rgb,vec3(0.0),vec3(1.0));     \n"
"  }\n"
"  fragColor.a = spriteColor.a;  \n"
"}\n";
const GLchar * pYglprg_vdp1_gouraudshading_f[] = {Yglprg_vdp1_gouraudshading_f, NULL};

static YglVdp1CommonParam id_g = { 0 };

/*------------------------------------------------------------------------------------
 *  VDP1 MSB shadow Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_msb_shadow_v[] =
SHADER_VERSION
"uniform mat4 u_mvpMatrix;     \n"
"uniform vec2 u_texsize;    \n"
"layout (location = 0) in vec4 a_position;    \n"
"layout (location = 1) in vec4 a_texcoord;    \n"
"out  vec4 v_texcoord;    \n"
"void main() { \n"
"   v_texcoord  = a_texcoord; \n"
"   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
"   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
"   gl_Position = a_position*u_mvpMatrix; \n"
"}\n";
const GLchar * pYglprg_vdp1_msb_shadow_v[] = {Yglprg_vdp1_msb_shadow_v, NULL};

const GLchar Yglprg_vdp1_msb_shadow_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"uniform sampler2D u_sprite;\n"
"uniform highp sampler2D u_fbo_attr;\n"
"in  vec4 v_texcoord;    \n"
"out vec4 fragColorAttr; \n"
"void main() {\n"
"  int mode;\n"
"  ivec2 addr = ivec2(textureSize(u_sprite, 0) * v_texcoord.st / v_texcoord.q); \n"
"  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
"  if( spriteColor.a == 0.0 ) discard;\n"
"  int msb = 0;\n"
"  fragColorAttr = texelFetch(u_fbo_attr,ivec2(gl_FragCoord.xy),0);\n"
"  int oldmsb = (int(fragColorAttr.a * 255.0))>>7;\n"
"  int prio = int(spriteColor.a * 255.0) & 0x7;\n"
"  if ((int(spriteColor.a * 255.0) & 0xC0) == 0xC0) {\n"
"    msb = (int(spriteColor.b*255.0)>>7);\n"
"  }\n"
"  if (msb == 0) discard;\n"
"  fragColorAttr.a = float((msb | oldmsb)<<7 | prio)/255.0;\n"
"}\n";
const GLchar * pYglprg_vdp1_msb_shadow_f[] = {Yglprg_vdp1_msb_shadow_f, NULL};

static YglVdp1CommonParam id_msb_s = { 0 };


/*------------------------------------------------------------------------------------
 *  VDP1 GlowShading and Half Trans Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_gouraudshading_hf_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;     \n"
      "uniform vec2 u_texsize;    \n"
      "layout (location = 0) in vec4 a_position;    \n"
      "layout (location = 1) in vec4 a_texcoord;    \n"
      "layout (location = 2) in vec4 a_grcolor;     \n"
      "out  vec4 v_texcoord;    \n"
      "out  vec4 v_vtxcolor;    \n"
      "void main() { \n"
      "   v_vtxcolor  = a_grcolor;   \n"
      "   v_texcoord  = a_texcoord; \n"
      "   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
      "   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "}\n";
const GLchar * pYglprg_vdp1_gouraudshading_hf_v[] = {Yglprg_vdp1_gouraudshading_hf_v, NULL};

const GLchar Yglprg_vdp1_gouraudshading_hf_f[] =
      SHADER_VERSION
      "#ifdef GL_ES\n"
      "precision highp float;         \n"
      "#endif\n"
      "uniform highp sampler2D u_sprite;      \n"
      "uniform highp sampler2D u_fbo;         \n"
      "in vec4 v_texcoord;         \n"
      "in vec4 v_vtxcolor;         \n"
      "out vec4 fragColor; \n "
      "void main() {    \n"
      "  int mode;\n"
      "  ivec2 addr = ivec2(vec2(textureSize(u_sprite, 0)) * v_texcoord.st / v_texcoord.q); \n"
      "  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
      "  if( spriteColor.a == 0.0 ) discard;         \n"
      "  vec4 fboColor    = texelFetch(u_fbo,ivec2(gl_FragCoord.xy),0);\n"
      "  int additional = int(fboColor.a * 255.0);\n"
      "  mode = int(spriteColor.b*255.0)&0x7; \n"
      "  spriteColor.b = float((int(spriteColor.b*255.0)&0xF8)>>3)/31.0; \n"
      "  spriteColor += vec4(v_vtxcolor.r,v_vtxcolor.g,v_vtxcolor.b,0.0);\n"
      "  if( (additional & 0x40) == 0 ) \n"
      "  { \n"
      "    fragColor = spriteColor*0.5 + fboColor*0.5;     \n"
      "    fragColor.a = spriteColor.a; \n"
      "  }else{         \n"
      "    fragColor = spriteColor;  \n"
      "  }   \n"
      "  fragColor.b = float((int(fragColor.b*255.0)&0xF8)|mode)/255.0; \n"
      "}\n";
const GLchar * pYglprg_vdp1_gouraudshading_hf_f[] = {Yglprg_vdp1_gouraudshading_hf_f, NULL};


static YglVdp1CommonParam id_ght = { 0 };
static YglVdp1CommonParam id_ght_tess = { 0 };


/*------------------------------------------------------------------------------------
 *  VDP1 Half Trans Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_halftrans_v[] =
        SHADER_VERSION
        "uniform mat4 u_mvpMatrix;     \n"
        "uniform vec2 u_texsize;    \n"
        "layout (location = 0) in vec4 a_position;    \n"
        "layout (location = 1) in vec4 a_texcoord;    \n"
        "layout (location = 2) in vec4 a_grcolor;     \n"
        "out  vec4 v_texcoord;    \n"
        "out  vec4 v_vtxcolor;    \n"
        "void main() { \n"
        "   v_vtxcolor  = a_grcolor;   \n"
        "   v_texcoord  = a_texcoord; \n"
        "   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
        "   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
        "   gl_Position = a_position*u_mvpMatrix; \n"
        "}\n";

const GLchar * pYglprg_vdp1_halftrans_v[] = {Yglprg_vdp1_halftrans_v, NULL};

const GLchar Yglprg_vdp1_halftrans_f[] =
      SHADER_VERSION
      "#ifdef GL_ES\n"
      "precision highp float;         \n"
      "#endif\n"
      "uniform highp sampler2D u_sprite;          \n"
      "uniform highp sampler2D u_fbo;   \n"
      "in vec4 v_texcoord;         \n"
      "out vec4 fragColor; \n "
      "void main() {    \n"
      "  int mode; \n"
      "  ivec2 addr = ivec2(vec2(textureSize(u_sprite, 0)) * v_texcoord.st / v_texcoord.q); \n"
      "  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
      "  if( spriteColor.a == 0.0 ) discard;         \n"
      "  mode = int(spriteColor.b*255.0)&0x7; \n"
      "  spriteColor.b = float((int(spriteColor.b*255.0)&0xF8)>>3)/31.0; \n"
      "  vec4 fboColor    = texelFetch(u_fbo,ivec2(gl_FragCoord.xy),0);\n"
      "  int additional = int(fboColor.a * 255.0);\n"
      "  if( (additional & 0x40) == 0 ) \n"
      "  {   \n"
      "    fragColor = spriteColor*0.5 + fboColor*0.5;     \n"
      "    fragColor.a = fboColor.a; \n"
      "  }else{         \n"
      "    fragColor = spriteColor;  \n"
      "  }   \n"
      "  fragColor.b = float((int(fragColor.b*255.0)&0xF8)|mode)/255.0; \n"
      "}\n";
const GLchar * pYglprg_vdp1_halftrans_f[] = {Yglprg_vdp1_halftrans_f, NULL};

static YglVdp1CommonParam hf = {0};
static YglVdp1CommonParam hf_tess = {0};

/*------------------------------------------------------------------------------------
*  VDP1 Mesh Operaion
* ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_mesh_v[] =
SHADER_VERSION
"uniform mat4 u_mvpMatrix;     \n"
"uniform vec2 u_texsize;    \n"
"layout (location = 0) in vec4 a_position;    \n"
"layout (location = 1) in vec4 a_texcoord;    \n"
"layout (location = 2) in vec4 a_grcolor;     \n"
"out  vec4 v_texcoord;    \n"
"out  vec4 v_vtxcolor;    \n"
"void main() { \n"
"   v_vtxcolor  = a_grcolor;   \n"
"   v_texcoord  = a_texcoord; \n"
"   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
"   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
"   gl_Position = a_position*u_mvpMatrix; \n"
"}\n";
const GLchar * pYglprg_vdp1_mesh_v[] = { Yglprg_vdp1_mesh_v, NULL };

const GLchar Yglprg_vdp1_mesh_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;         \n"
"#endif\n"
"uniform sampler2D u_sprite;      \n"
"uniform sampler2D u_fbo;         \n"
"in vec4 v_texcoord;         \n"
"in vec4 v_vtxcolor;         \n"
"out vec4 fragColor; \n "
"out vec4 fragColorAttr; \n"
"void main() {    \n"
"  ivec2 addr = ivec2(vec2(textureSize(u_sprite, 0)) * v_texcoord.st / v_texcoord.q); \n"
"  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
"  if( spriteColor.a == 0.0 ) discard;         \n"
"  if( (int(gl_FragCoord.y) & 0x01) == 0 ){ \n"
"    if( (int(gl_FragCoord.x) & 0x01) == 0 ){ \n"
"       discard;"
"    } \n"
"  }else{ \n"
"    if( (int(gl_FragCoord.x) & 0x01) == 1 ){ \n"
"       discard;"
"    } \n"
"  } \n"
"  fragColorAttr = vec4(0.0);\n"
"  fragColor.rgb  = clamp(spriteColor.rgb+v_vtxcolor.rgb,vec3(0.0),vec3(1.0));     \n"
"  fragColor.a = spriteColor.a;  \n"
"}\n";

//utiliser un bit dans la caouche attribut pour ameliorer le blend et faire une couche a 50%
const GLchar Yglprg_vdp1_mesh_improve_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;         \n"
"#endif\n"
"uniform sampler2D u_sprite;      \n"
"uniform sampler2D u_fbo;         \n"
"in vec4 v_texcoord;         \n"
"in vec4 v_vtxcolor;         \n"
"out vec4 fragColor; \n "
"out vec4 fragColorAttr; \n"
"void main() {    \n"
"  int alpha = 0x0;\n"
"  int prio = 0;"
"  ivec2 addr = ivec2(vec2(textureSize(u_sprite, 0)) * v_texcoord.st / v_texcoord.q); \n"
"  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
"  if( spriteColor.a == 0.0 ) discard;         \n"
"  prio = (int(spriteColor.a *255.0) & 0x7);\n"
"  fragColor = texelFetch(u_fbo,ivec2(gl_FragCoord.xy),0);\n"
"  fragColor.a = float((int(fragColor.a *255.0) & 0xF8)|prio)/255.0;\n"
"  fragColorAttr.rgb  = clamp(spriteColor.rgb+v_vtxcolor.rgb,vec3(0.0),vec3(1.0));\n"
"  fragColorAttr.b = float(int(fragColorAttr.b*255.0)&0xFE)/255.0;\n"
"  if ((int(spriteColor.a * 255.0) & 0x40) == 0) alpha = 0x08;\n"
"  alpha = alpha | 0x40 | prio;\n"
"  fragColorAttr.a = float(alpha)/255.0;\n"
"}\n";

const GLchar * pYglprg_vdp1_mesh_f[] = { Yglprg_vdp1_mesh_f, NULL };
const GLchar * pYglprg_vdp1_mesh_improve_f[] = { Yglprg_vdp1_mesh_improve_f, NULL };

static YglVdp1CommonParam mesh = { 0 };
static YglVdp1CommonParam mesh_improve = { 0 };
static YglVdp1CommonParam grow_tess = { 0 };
static YglVdp1CommonParam mesh_tess = { 0 };
static YglVdp1CommonParam mesh_tess_improve = { 0 };
static YglVdp1CommonParam id_msb_tess = { 0 };

/*------------------------------------------------------------------------------------
*  VDP1 Half luminance Operaion
* ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_half_luminance_v[] =
      SHADER_VERSION
      "uniform mat4 u_mvpMatrix;    \n"
      "uniform vec2 u_texsize;    \n"
      "layout (location = 0) in vec4 a_position;   \n"
      "layout (location = 1) in vec4 a_texcoord;   \n"
      "out   vec4 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "   v_texcoord  = a_texcoord; \n"
      "   v_texcoord.x  = v_texcoord.x / u_texsize.x;\n"
      "   v_texcoord.y  = v_texcoord.y / u_texsize.y;\n"
      "} ";
const GLchar * pYglprg_vdp1_half_luminance_v[] = {Yglprg_vdp1_half_luminance_v, NULL};

const GLchar Yglprg_vpd1_half_luminance_f[] =
      SHADER_VERSION
      "#ifdef GL_ES\n"
      "precision highp float; \n"
      "#endif\n"
      "in vec4 v_texcoord; \n"
      "uniform sampler2D s_texture;  \n"
      "out vec4 fragColor; \n"
      "out vec4 fragColorAttr; \n"
      "void main()   \n"
      "{  \n"
      "  ivec2 addr = ivec2(vec2(textureSize(s_texture, 0)) * v_texcoord.st / v_texcoord.q); \n"
      "  vec4 spriteColor = texelFetch(s_texture,addr,0);\n"
      "  if( spriteColor.a == 0.0 ) discard;         \n"
      "  fragColor.r = spriteColor.r * 0.5;\n "
      "  fragColor.g = spriteColor.g * 0.5;\n "
      "  fragColor.b = spriteColor.b * 0.5;\n "
      "  fragColorAttr = vec4(0.0);\n"
      "  fragColor.a = spriteColor.a;\n "
      "}  \n";
const GLchar * pYglprg_vdp1_half_luminance_f[] = {Yglprg_vpd1_half_luminance_f, NULL};
static YglVdp1CommonParam half_luminance = { 0 };
static YglVdp1CommonParam half_luminance_tess = { 0 };


/*------------------------------------------------------------------------------------
*  VDP1 Shadow Operation
*    hard/vdp1/hon/p06_37.htm
* ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_shadow_v[] =
SHADER_VERSION
"uniform mat4 u_mvpMatrix;     \n"
"uniform vec2 u_texsize;    \n"
"layout (location = 0) in vec4 a_position;    \n"
"layout (location = 1) in vec4 a_texcoord;    \n"
"layout (location = 2) in vec4 a_grcolor;     \n"
"out  vec4 v_texcoord;    \n"
"out  vec4 v_vtxcolor;    \n"
"void main() { \n"
"   v_vtxcolor  = a_grcolor;   \n"
"   v_texcoord  = a_texcoord; \n"
"   v_texcoord.x  = v_texcoord.x / u_texsize.x; \n"
"   v_texcoord.y  = v_texcoord.y / u_texsize.y; \n"
"   gl_Position = a_position*u_mvpMatrix; \n"
"}\n";

const GLchar * pYglprg_vdp1_shadow_v[] = { Yglprg_vdp1_shadow_v, NULL };

const GLchar Yglprg_vdp1_shadow_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float;\n"
"#endif\n"
"uniform sampler2D u_sprite;\n"
"uniform highp sampler2D u_fbo;\n"
"in vec4 v_texcoord;\n"
"out vec4 fragColor; \n "
"void main() { \n"
"  int mode;\n"
"  ivec2 addr = ivec2(vec2(textureSize(u_sprite, 0)) * v_texcoord.st / v_texcoord.q); \n"
"  vec4 spriteColor = texelFetch(u_sprite,addr,0);\n"
"  if( spriteColor.a == 0.0 ) discard;         \n"
"  mode = int(spriteColor.b*255.0)&0x7; \n"
"  spriteColor.b = float((int(spriteColor.b*255.0)&0xF8)>>3)/31.0; \n"
"  vec4 fboColor    = texelFetch(u_fbo,ivec2(gl_FragCoord.xy),0);\n"
"  int additional = int(fboColor.a * 255.0);\n"
"  if( ((additional & 0xC0)==0x80) ) { \n"
"    fragColor = vec4(fboColor.r*0.5,fboColor.g*0.5,fboColor.b*0.5,fboColor.a);\n"
"    fragColor.b = float((int(fragColor.b*255.0)&0xF8)|mode)/255.0; \n"
"  }else{\n"
"    discard;"
"  }\n"
"}\n";
const GLchar * pYglprg_vdp1_shadow_f[] = { Yglprg_vdp1_shadow_f, NULL };

static YglVdp1CommonParam shadow = { 0 };
static YglVdp1CommonParam shadow_tess = { 0 };


/*------------------------------------------------------------------------------------
 *  VDP1 UserClip Operation
 * ----------------------------------------------------------------------------------*/
int Ygl_uniformStartUserClip(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id )
{
   YglProgram * prg;
   prg = p;

   glEnableVertexAttribArray(0);
   glDisableVertexAttribArray(1);

   if( prg->ux1 != -1 )
   {

      GLint vertices[12];
      glColorMask( GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE );
      glStencilMask(0xffffffff);
      glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

      glEnable(GL_STENCIL_TEST);
      glStencilFunc(GL_ALWAYS,0x1,0x01);
      glStencilOp(GL_REPLACE,GL_REPLACE,GL_REPLACE);

      // render
      vertices[0] = (int)((float)prg->ux1 * _Ygl->vdp1wratio);
      vertices[1] = (int)((float)prg->uy1 * _Ygl->vdp1hratio);
      vertices[2] = (int)((float)(prg->ux2+1) * _Ygl->vdp1wratio);
      vertices[3] = (int)((float)prg->uy1 * _Ygl->vdp1hratio);
      vertices[4] = (int)((float)(prg->ux2+1) * _Ygl->vdp1wratio);
      vertices[5] = (int)((float)(prg->uy2+1) * _Ygl->vdp1hratio);

      vertices[6] = (int)((float)prg->ux1 * _Ygl->vdp1wratio);
      vertices[7] = (int)((float)prg->uy1 * _Ygl->vdp1hratio);
      vertices[8] = (int)((float)(prg->ux2+1) * _Ygl->vdp1wratio);
      vertices[9] = (int)((float)(prg->uy2+1) * _Ygl->vdp1hratio);
      vertices[10] = (int)((float)prg->ux1 * _Ygl->vdp1wratio);
      vertices[11] = (int)((float)(prg->uy2+1) * _Ygl->vdp1hratio);

      glUniformMatrix4fv( prg->mtxModelView, 1, GL_FALSE, (GLfloat*) &_Ygl->mtxModelView.m[0][0]  );
      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertices_buf);
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
      glVertexAttribPointer(prg->vertexp,2, GL_INT,GL_FALSE, 0, 0 );
      glEnableVertexAttribArray(prg->vertexp);

      glDrawArrays(GL_TRIANGLES, 0, 6);

      glColorMask( GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE );
      glStencilFunc(GL_ALWAYS,0,0x0);
      glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
      glDisable(GL_STENCIL_TEST);
   }

   glEnable(GL_STENCIL_TEST);
   glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
   if( prg->uClipMode == 0x02 )
   {
      _Ygl->vdp1_stencil_mode = 1;
      glStencilFunc(GL_EQUAL,0x1,0xFF);
   }else if( prg->uClipMode == 0x03 )
   {
      _Ygl->vdp1_stencil_mode = 2;
      glStencilFunc(GL_EQUAL,0x0,0xFF);
   }else{
      _Ygl->vdp1_stencil_mode =3;
      glStencilFunc(GL_ALWAYS,0,0xFF);
   }

   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);

   //glDisable(GL_STENCIL_TEST);

   return 0;
}

int Ygl_cleanupStartUserClip(void * p, YglTextureManager *tm ){return 0;}

int Ygl_uniformEndUserClip(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id )
{

   YglProgram * prg;
   prg = p;
   glDisable(GL_STENCIL_TEST);
   _Ygl->vdp1_stencil_mode = 0;
   glStencilFunc(GL_ALWAYS,0,0xFF);

   return 0;
}

int Ygl_cleanupEndUserClip(void * p, YglTextureManager *tm ){return 0;}



/*------------------------------------------------------------------------------------
 *  VDP2 Draw Frame buffer Operation
 * ----------------------------------------------------------------------------------*/

typedef struct  {
  int idvdp1FrameBuffer;
  int idvdp1FrameBufferAttr;
  int idvdp2regs;
  int idcram;
  int idline;
} DrawFrameBufferUniform;

#define MAX_FRAME_BUFFER_UNIFORM (48)

DrawFrameBufferUniform g_draw_framebuffer_uniforms[MAX_FRAME_BUFFER_UNIFORM];

const GLchar Yglprg_vdp1_drawfb_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      " v_texcoord  = a_texcoord; \n"
      "} ";
const GLchar * pYglprg_vdp2_blit_v[] = {Yglprg_vdp1_drawfb_v, NULL};

/*
+-+-+-+-+-+-+-+-+
|S|C|A|A|A|P|P|P|
+-+-+-+-+-+-+-+-+
S show flag
C index or direct color
A alpha index
P priority

refrence:
  hard/vdp2/hon/p09_10.htm
  hard/vdp2/hon/p12_14.htm#CCCTL_

*/

const GLchar Yglprg_vdp2_drawfb_cram_f[] =
"  FBCol ret;\n"
"  ret.color = vec4(0.0);\n"
"  ret.prio = 0;\n"
"  ret.meshColor = vec4(0.0);\n"
"  ret.mesh = 0;\n"
"  if (fbon != 1) return ret;\n"
"  fbmode = 1;\n"
"  vdp1mode = 1;\n"
"  vec4 fbColor = texelFetch(s_vdp1FrameBuffer, ivec2(v_texcoord.st * textureSize(s_vdp1FrameBuffer, 0)+ivec2(x, 0)), 0);\n"
"  vec4 fbColorAttr = texelFetch(s_vdp1FrameBufferAttr, ivec2(v_texcoord.st * textureSize(s_vdp1FrameBufferAttr, 0)+ivec2(x, 0)), 0);\n"
"  vec4 tmpColor = vec4(0.0);\n"
"  vec4 tmpmeshColor = vec4(0.0);\n"
"  int line = int((u_vheight-gl_FragCoord.y) * u_emu_height)*24;\n"
"  vec3 u_coloroffset = vec3(texelFetch(s_vdp2reg, ivec2(17 + line,0), 0).r, texelFetch(s_vdp2reg, ivec2(18+line,0), 0).r, texelFetch(s_vdp2reg, ivec2(19+line,0), 0).r);\n"
"  vec3 u_coloroffset_sign = vec3(texelFetch(s_vdp2reg, ivec2(20 + line, 0), 0).r, texelFetch(s_vdp2reg, ivec2(21+line,0), 0).r, texelFetch(s_vdp2reg, ivec2(22+line,0), 0).r);\n"
"  if (u_coloroffset_sign.r != 0.0) u_coloroffset.r = float(int(u_coloroffset.r*255.0)-256.0)/255.0;\n"
"  if (u_coloroffset_sign.g != 0.0) u_coloroffset.g = float(int(u_coloroffset.g*255.0)-256.0)/255.0;\n"
"  if (u_coloroffset_sign.b != 0.0) u_coloroffset.b = float(int(u_coloroffset.b*255.0)-256.0)/255.0;\n"
"  int u_color_ram_offset = int(texelFetch(s_vdp2reg, ivec2(23+line,0), 0).r*255.0)<<8;\n"
"  int u_cctl = int(texelFetch(s_vdp2reg, ivec2(16+line,0), 0).r*255.0);\n"
"  int additional = int(fbColor.a * 255.0);\n"
"  int additionalAttr = int(fbColorAttr.a * 255.0);\n"
"  int additionalAlpha = int(fbColorAttr.r * 255.0);\n"
"  if( ((additional & 0x80) == 0) && ((additionalAttr & 0xC0) == 0) ){ return ret;} // show? \n"
"  int prinumber = (additional&0x07); \n"
"  int primesh = additionalAttr&0x7;\n"
"  int tmpmeshprio = 0;\n"
"  int depth = int(texelFetch(s_vdp2reg, ivec2(prinumber+8+line,0), 0).r*255.0);\n"
"  int alpha = int(texelFetch(s_vdp2reg, ivec2(((additional>>3)&0x07)+line,0), 0).r*255.0)<<3; \n"
"  int opaque = 0xF8;\n"
"  int tmpmesh = 0;\n"
"  int msb = int(fbColor.b*255.0)&0x1;\n"
"  fbColor.b = float(int(fbColor.b * 255.0)&0xFE)/255.0;\n"
"  vec4 txcol=vec4(0.0,0.0,0.0,1.0);\n"
"  if((additional & 0x80) != 0) {\n"
"    if( (additional & 0x40) != 0 ){  // index color? \n"
"      int colindex = ( int(fbColor.g*255.0)<<8 | int(fbColor.r*255.0)); \n"
"      if( colindex == 0 && prinumber == 0 && ((additionalAttr & 0x80) == 0)) { \n"
"        if ((additionalAttr & 0x40) != 0) {\n"
"          if ((additionalAttr & 0x08) != 0) {\n"
"            ret.meshColor.rgb = fbColorAttr.rgb;\n"
"          } else { \n"
"            colindex = ( int(fbColorAttr.g*255.0)<<8 | int(fbColorAttr.r*255.0)); \n"
"            if( colindex != 0) {\n"
"              colindex = colindex + u_color_ram_offset; \n"
"              txcol = texelFetch( s_color,  ivec2( colindex ,0 )  , 0 );\n"
"              ret.meshColor.rgb = txcol.rgb;\n"
"            } else { \n"
"              ret.meshColor.rgb = vec3(0.0);\n"
"            }\n"
"          }\n"
"          ret.meshColor.a = fbColor.a;\n"
"          ret.mesh = 1;\n"
"        }\n"
"        ret.meshPrio = int(texelFetch(s_vdp2reg, ivec2(primesh+8+line,0), 0).r*255.0);\n"
"        return ret; \n"
"      } // hard/vdp1/hon/p02_11.htm 0 data is ignoerd \n"
"      if(( colindex != 0 || prinumber != 0) && ((additionalAttr & 0x80) == 0)){\n"
"        colindex = colindex + u_color_ram_offset; \n"
"        txcol = texelFetch( s_color,  ivec2( colindex ,0 )  , 0 );\n"
"        if (txcol.a != 0.0) msb = 1;\n" //linked to VDP2 ColorRam alpha management
"        else msb = 0;\n"
"        tmpColor = txcol;\n"
"      } else { \n"
"        tmpColor = vec4(0.0);\n"
"      }\n"
"    }else{ // direct color \n"
"      tmpColor = fbColor;\n"
"    } \n"
"    tmpColor.rgb = clamp(tmpColor.rgb + u_coloroffset, vec3(0.0), vec3(1.0));  \n"
"  } else { \n"
"    tmpColor = fbColor;\n"
"  } \n"
"  if ((additionalAttr & 0x80) != 0) {\n"
"    if (tmpColor.rgb == vec3(0.0)) {\n"
"      alpha = 0x78;\n"
"      vdp1mode = 5;\n"
"      fbmode = 0;\n"
"    } else { \n"
"      if (int(texelFetch(s_vdp2reg, ivec2((additionalAttr & 0x7)+8+line,0), 0).r*255.0)-1 == depth) {\n"
"        tmpColor.rgb = tmpColor.rgb * 0.5;\n"
"      }\n"
"    }\n"
"  } \n"
"  if ((additionalAttr & 0x40) != 0) {\n"
"    if ((additionalAttr & 0x08) != 0) {\n"
"      tmpmeshColor.rgb = fbColorAttr.rgb;\n"
"    } else { \n"
"      int colindex = ( int(fbColorAttr.g*255.0)<<8 | int(fbColorAttr.r*255.0)); \n"
"      if( colindex != 0) {\n"
"        colindex = colindex + u_color_ram_offset; \n"
"        txcol = texelFetch( s_color,  ivec2( colindex ,0 )  , 0 );\n"
"        tmpmeshColor.rgb = txcol.rgb;\n"
"      } else { \n"
"        tmpmeshColor.rgb = vec3(0.0);\n"
"      }\n"
"    }\n"
"    tmpmeshColor.a = fbColor.a;\n"
"    tmpmeshprio = int(texelFetch(s_vdp2reg, ivec2(primesh+8+line,0), 0).r*255.0);"
"    tmpmesh = 1;\n"
"  }\n"
"  if (fbmode != 0) {\n";
/*
 Color calculation option
  hard/vdp2/hon/p09_21.htm
*/
const GLchar Yglprg_vdp2_drawfb_cram_no_color_col_f[]    = " alpha = opaque; \n";

const GLchar Yglprg_vdp2_drawfb_cram_less_color_col_f[]  = " if( depth > u_cctl ){ alpha = opaque; fbmode = 0;} \n ";
const GLchar Yglprg_vdp2_drawfb_cram_equal_color_col_f[] = " if( depth != u_cctl ){ alpha = opaque; fbmode = 0;} \n ";
const GLchar Yglprg_vdp2_drawfb_cram_more_color_col_f[]  = " if( depth < u_cctl ){ alpha = opaque; fbmode = 0;} \n ";
const GLchar Yglprg_vdp2_drawfb_cram_msb_color_col_f[]   = " if( msb == 0 ){ alpha = opaque; fbmode = 0;} \n ";


const GLchar Yglprg_vdp2_drawfb_cram_epiloge_none_f[] =
"\n";
const GLchar Yglprg_vdp2_drawfb_cram_epiloge_as_is_f[] =
" if (fbmode == 1) vdp1mode = 2; \n";
const GLchar Yglprg_vdp2_drawfb_cram_epiloge_src_alpha_f[] =
" if (fbmode == 1) vdp1mode = 3; \n";
const GLchar Yglprg_vdp2_drawfb_cram_epiloge_dst_alpha_f[] =
" if (fbmode == 1) vdp1mode = 4; \n";

const GLchar Yglprg_vdp2_drawfb_cram_eiploge_f[] =
" }\n"
" if(additionalAlpha != 0) {\n"
"   alpha = 0x78;\n"
"   vdp1mode = 3;\n"
" }\n"
" tmpColor.a = float(alpha|vdp1mode)/255.0; \n"
" ret.color = tmpColor;\n"
" ret.meshColor = tmpmeshColor;\n"
" ret.mesh = tmpmesh;\n"
" ret.meshPrio = tmpmeshprio;\n"
" ret.prio = depth;\n";


//--------------------------------------------------------------------------------------------------------------

static const char vdp2blit_start_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in vec2 v_texcoord; \n"
"uniform sampler2D s_back;  \n"
"uniform sampler2D s_lncl;  \n"
"uniform sampler2D s_cc_win;  \n"
"uniform int u_lncl[7];  \n"
"out vec4 finalColor; \n"
"uniform sampler2D s_vdp2reg; \n"
"uniform sampler2D s_vdp1FrameBuffer;\n"
"uniform sampler2D s_vdp1FrameBufferAttr;\n"
"uniform sampler2D s_color; \n"
"uniform sampler2D s_line; \n"
"int fbmode = 1;\n"
"int vdp1mode = 1;\n"

"vec4 FBColor = vec4(0.0);\n"
"vec4 vdp2col0 = vec4(0.0);\n"
"vec4 vdp2col1 = vec4(0.0);\n"
"vec4 vdp2col2 = vec4(0.0);\n"
"vec4 vdp2col3 = vec4(0.0);\n"
"vec4 vdp2col4 = vec4(0.0);\n"
"vec4 vdp2col5 = vec4(0.0);\n"
"vec4 FBShadow = vec4(0.0);\n"
"int FBPrio = 0;\n"
"int FBMesh = 0;\n"
"int FBMeshPrio = 0;\n"
"int NoVdp1 = 0;\n"

#ifdef DEBUG_BLIT
"out vec4 topColor; \n"
"out vec4 secondColor; \n"
"out vec4 thirdColor; \n"
"out vec4 fourthColor; \n"
#endif
"uniform float u_emu_height;\n"
"uniform float u_vheight; \n"

"uniform sampler2D s_texture0;  \n"
"uniform sampler2D s_texture1;  \n"
"uniform sampler2D s_texture2;  \n"
"uniform sampler2D s_texture3;  \n"
"uniform sampler2D s_texture4;  \n"
"uniform sampler2D s_texture5;  \n"
"uniform int fbon;  \n"
"uniform int screen_nb;  \n"
"uniform int mode[7];  \n"
"uniform int isRGB[6]; \n"
"uniform int isBlur[7]; \n"
"uniform int ram_mode; \n"
"uniform int extended_cc; \n"
"uniform int use_cc_win; \n"
"int cc_enabled; \n"

"struct Col \n"
"{ \n"
"  vec4 Color; \n"
"  vec4 meshColor;\n"
"  int mesh;\n"
"  int lncl; \n"
"  int mode; \n"
"  int isRGB; \n"
"  int isSprite; \n"
"  int layer; \n"
"}; \n"

"struct FBCol \n"
"{ \n"
"  vec4 color; \n"
"  vec4 meshColor; \n"
"  int mesh;\n"
"  int prio; \n"
"  int meshPrio;\n"
"}; \n"

"FBCol getFB(int x){ \n";

static const char vdp2blit_partial_end_f[] =
" return ret;\n"
"}\n";

static const char vdp2blit_filter_f[] =
"vec4 getPixel(sampler2D tex, vec2 st) {\n"
" ivec2 addr = ivec2(textureSize(tex, 0) * st);\n"
" vec4 result = texelFetch( tex, addr,0 );\n"
//" result.rgb = Filter( tex, st ).rgb;\n"
" return result;\n"
"}\n";

static const char vdp2blit_end_f[] =
"Col getPriorityColor(int prio, int nbPrio)   \n"
"{  \n"
"  Col ret, empty; \n"
"  vec4 tmpColor;\n"
"  int remPrio = nbPrio;\n"
"  empty.Color = vec4(0.0);\n"
"  empty.mode = 0;\n"
"  empty.lncl = 0;\n"
"  empty.isSprite = 0;\n"
"  empty.layer = -1;\n"
"  empty.meshColor = vec4(0.0);\n"
"  empty.mesh = 0;\n"
"  ret = empty;\n"
"  int priority; \n"
"  int alpha; \n"
"  if ((fbon == 1) && (prio == FBPrio)) {\n"
"    ret.mode = int(FBColor.a*255.0)&0x7; \n"
"    ret.lncl = u_lncl[6];\n"
"    ret.Color = FBColor; \n"
"    remPrio = remPrio - 1;\n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.isRGB = 0;\n" //Shall not be the case always... Need to get RGB format per pixel
"    ret.isSprite = 1;\n"
"    ret.layer = 6;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  if (screen_nb == 0) return empty;\n"
"  ret.isSprite = 0;\n"
"  tmpColor = vdp2col0; \n"
"  priority = int(tmpColor.a*255.0)&0x7; \n"
"  if (priority == prio) {\n"
"    remPrio = remPrio - 1;\n"
"    ret.lncl=u_lncl[0];\n"
"    ret.Color = tmpColor; \n"
"    ret.isRGB = isRGB[0];\n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.mode = mode[0]; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.layer = 0;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  if (screen_nb == 1) return empty;\n"
"  tmpColor = vdp2col1; \n"
"  priority = int(tmpColor.a*255.0)&0x7; \n"
"  if (priority == prio) {\n"
"    remPrio = remPrio - 1;\n"
"    ret.lncl=u_lncl[1];\n"
"    ret.isRGB = isRGB[1];\n"
"    ret.Color = tmpColor; \n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.mode = mode[1]; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.layer = 1;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  if (screen_nb == 2) return empty;\n"
"  tmpColor = vdp2col2; \n"
"  priority = int(tmpColor.a*255.0)&0x7; \n"
"  if (priority == prio) {\n"
"    remPrio = remPrio - 1;\n"
"    ret.lncl=u_lncl[2];\n"
"    ret.isRGB = isRGB[2];\n"
"    ret.Color = tmpColor; \n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.mode = mode[2]; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.layer = 2;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  if (screen_nb == 3) return empty;\n"
"  tmpColor = vdp2col3; \n"
"  priority = int(tmpColor.a*255.0)&0x7; \n"
"  if (priority == prio) {\n"
"    remPrio = remPrio - 1;\n"
"    ret.lncl=u_lncl[3];\n"
"    ret.isRGB = isRGB[3];\n"
"    ret.Color = tmpColor; \n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.mode = mode[3]; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.layer = 3;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  if (screen_nb == 4) return empty;\n"
"  tmpColor = vdp2col4; \n"
"  priority = int(tmpColor.a*255.0)&0x7; \n"
"  if (priority == prio) {\n"
"    remPrio = remPrio - 1;\n"
"    ret.Color = tmpColor; \n"
"    ret.lncl=u_lncl[4];\n"
"    ret.isRGB = isRGB[4];\n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.mode = mode[4]; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.layer = 4;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  if (screen_nb == 5) return empty;\n"
"  tmpColor = vdp2col5; \n"
"  priority = int(tmpColor.a*255.0)&0x7; \n"
"  if (priority == prio) {\n"
"    remPrio = remPrio - 1;\n"
"    ret.Color = tmpColor; \n"
"    ret.lncl=u_lncl[5];\n"
"    ret.isRGB = isRGB[5];\n"
"    alpha = int(ret.Color.a*255.0)&0xF8; \n"
"    ret.mode = mode[5]; \n"
"    ret.Color.a = float(alpha>>3)/31.0; \n"
"    ret.layer = 5;\n"
"    if (remPrio == 0) return ret;\n"
"  }\n"
"  return empty;\n"
"}  \n"

"Col getBlur(ivec2 addr, Col pix) \n"
"{  \n"
"  Col ret = pix;\n"
"  vec4 txcoll;\n"
"  vec4 txcolll;\n"
"  vec4 txcol = pix.Color;\n"
"  if (pix.layer == 6) { \n"
"    txcoll = getFB(-1).color;\n"
"    txcolll = getFB(-2).color;\n"
"  }\n"
"  if (pix.layer == 0) { \n"
"    txcoll = texelFetch( s_texture0, ivec2(addr.x-1, addr.y),0 );      \n"
"    txcolll = texelFetch( s_texture0, ivec2(addr.x-2, addr.y),0 );      \n"
"  }\n"
"  if (pix.layer == 1) { \n"
"    txcoll = texelFetch( s_texture1, ivec2(addr.x-1, addr.y),0 );      \n"
"    txcolll = texelFetch( s_texture1, ivec2(addr.x-2, addr.y),0 );      \n"
"  }\n"
"  if (pix.layer == 2) { \n"
"    txcoll = texelFetch( s_texture2, ivec2(addr.x-1, addr.y),0 );      \n"
"    txcolll = texelFetch( s_texture2, ivec2(addr.x-2, addr.y),0 );      \n"
"  }\n"
"  if (pix.layer == 3) { \n"
"    txcoll = texelFetch( s_texture3, ivec2(addr.x-1, addr.y),0 );      \n"
"    txcolll = texelFetch( s_texture3, ivec2(addr.x-2, addr.y),0 );      \n"
"  }\n"
"  if (pix.layer == 4) { \n"
"    txcoll = texelFetch( s_texture4, ivec2(addr.x-1, addr.y),0 );      \n"
"    txcolll = texelFetch( s_texture4, ivec2(addr.x-2, addr.y),0 );      \n"
"  }\n"
"  if (pix.layer == 5) { \n"
"    txcoll = texelFetch( s_texture5, ivec2(addr.x-1, addr.y),0 );      \n"
"    txcolll = texelFetch( s_texture5, ivec2(addr.x-2, addr.y),0 );      \n"
"  }\n"
"  ret.Color.rgb = txcol.rgb/2.0 + txcoll.rgb/4.0 + txcolll.rgb/4.0; \n"
"  return ret; \n"
"}  \n"

"void main()   \n"
"{  \n"
"  vec4 topImage = vec4(0.0);; \n"
"  vec4 secondImage = vec4(0.0);; \n"
"  vec4 colortop = vec4(0.0);  \n"
"  vec4 colorsecond = vec4(0.0); \n"
"  vec4 colorthird = vec4(0.0); \n"
"  vec4 colorfourth = vec4(0.0); \n"
"  vec4 colorback = vec4(0.0); \n"
"  int foundColor1 = 0; \n"
"  int foundColor2 = 0; \n"
"  int foundColor3 = 0; \n"
"  int modetop = 1; \n"
"  int modesecond = 1; \n"
"  int modethird = 1; \n"
"  int isRGBtop = 0;\n"
"  int isRGBsecond = 0;\n"
"  int isRGBthird = 0;\n"
"  int isRGBfourth = 0;\n"
"  int use_lncl = 0; \n"
"  int shadow = 0;\n"
"  int mesh = 0;\n"
"  vec3 meshCol = vec3(0.0);\n"
"  float alphatop = 1.0; \n"
"  float alphasecond = 1.0; \n"
"  float alphathird = 1.0; \n"
"  float alphafourth = 1.0; \n"
"  ivec2 addr = ivec2(textureSize(s_back, 0) * v_texcoord.st); \n"
"  colorback = texelFetch( s_back, addr,0 ); \n"
"  addr = ivec2(textureSize(s_texture0, 0) * v_texcoord.st); \n"
"  colortop = colorback; \n"
"  isRGBtop = 1; \n"
"  alphatop = float((int(colorback.a * 255.0)&0xF8)>>3)/31.0;\n"
"  FBCol tmp = getFB(0); \n"
"  FBColor = tmp.color;\n"
"  FBPrio = tmp.prio;\n"
"  FBShadow = tmp.meshColor;\n"
"  FBMeshPrio = tmp.meshPrio;\n"
"  FBMesh = tmp.mesh;\n"
"  if ((int(FBColor.a * 255.0)&0x7) == 5) {\n"
"    FBPrio = 0;\n"
"    shadow = 1;\n"
"  }\n"
"  if (screen_nb > 0) vdp2col0 = getPixel( s_texture0, v_texcoord.st ); \n"
"  if (screen_nb > 1) vdp2col1 = getPixel( s_texture1, v_texcoord.st ); \n"
"  if (screen_nb > 2) vdp2col2 = getPixel( s_texture2, v_texcoord.st ); \n"
"  if (screen_nb > 3) vdp2col3 = getPixel( s_texture3, v_texcoord.st ); \n"
"  if (screen_nb > 4) vdp2col4 = getPixel( s_texture4, v_texcoord.st ); \n"
"  if (screen_nb > 5) vdp2col5 = getPixel( s_texture5, v_texcoord.st ); \n"
"  for (int i = 7; i>0; i--) { \n"
"    if ((foundColor1 == 0) || (foundColor2 == 0) || (foundColor3 == 0)) { \n"
"      int hasColor = 1;\n"
"      while (hasColor != 0) {\n"
"        if ((foundColor1 == 0) && (fbon == 1) && (i == FBMeshPrio)) {\n"
"          if (FBMesh == 1) {\n"
"            mesh = 1;\n"
"            meshCol = FBShadow.rgb;\n"
"          }\n"
"        }\n"
"        Col prio = getPriorityColor(i, hasColor);\n"
"        hasColor = hasColor+1;\n"
"        if (prio.mode != 0) { \n"
"          if (foundColor1 == 0) { \n"
"            prio.mode = (prio.mode & 0x7); \n"
"            if (prio.isSprite == 0) {\n"
"              if ((int(prio.Color.b*255.0)&0x1) == 0) {\n"
                 //Special color calulation mode => CC is off on this pixel
"                prio.mode = 1;\n"
"                prio.Color.a = 1.0;\n"
"              }\n"
"            }\n"
"            if (prio.lncl == 0) { \n"
"              colorsecond = colortop;\n"
"              alphasecond = alphatop;\n"
"              modesecond = modetop;\n"
"              isRGBsecond = isRGBtop;\n"
"            } else { \n"
"              ivec2 linepos; \n "
"              linepos.y = 0; \n "
"              linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"              colorsecond = texelFetch( s_lncl, linepos ,0 );\n"
"              modesecond = mode[6];\n"
"              alphasecond = float((int(colorsecond.a * 255.0)&0xF8)>>3)/31.0;\n"
"              isRGBsecond = 1;\n"
"              use_lncl = 1;\n"
"              foundColor2 = 1; \n"
"            }\n"
"            colortop = prio.Color; \n"
"            modetop = prio.mode&0x7; \n"
"            isRGBtop = prio.isRGB; \n"
"            alphatop = prio.Color.a; \n"
"            foundColor1 = 1; \n"
"            if (isBlur[prio.layer] != 0) { \n"
"              Col blur = getBlur(addr, prio);"
"              modesecond = blur.mode&0x7; \n"
"              colorsecond = blur.Color; \n"
"              alphasecond = blur.Color.a; \n"
"              isRGBsecond = blur.isRGB; \n"
"              foundColor2 = 1; \n" //semble corriger steep slope
//le mesh mode deconne dans steep slope
"            }\n"
"          } else if (foundColor2 == 0) { \n" // A revoir du coup
"            if (prio.lncl == 0) { \n"
"              colorthird = colorsecond;\n"
"              alphathird = alphasecond;\n"
"              modethird = modesecond;\n"
"              isRGBthird = isRGBsecond;\n"
"            } else { \n"
"              ivec2 linepos; \n "
"              linepos.y = 0; \n "
"              linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"              colorthird = texelFetch( s_lncl, linepos ,0 );\n"
"              modethird = mode[6];\n"
"              alphathird = float((int(colorthird.a * 255.0)&0xF8)>>3)/31.0;\n"
"              isRGBthird = 1;\n"
"              use_lncl = 1;\n"
"              foundColor3 = 1; \n"
"            }\n"
"            modesecond = prio.mode&0x7; \n"
"            colorsecond = prio.Color; \n"
"            alphasecond = prio.Color.a; \n"
"            isRGBsecond = prio.isRGB; \n"
"            foundColor2 = 1; \n"
"            if (isBlur[prio.layer] != 0) { \n"
"              Col blur = getBlur(addr, prio);"
"              modesecond = blur.mode&0x7; \n"
"              colorsecond = blur.Color; \n"
"              alphasecond = blur.Color.a; \n"
"              isRGBsecond = blur.isRGB; \n"
"              foundColor2 = 1; \n"
"            }\n"
"          } else if (foundColor3 == 0) { \n"
"            if (prio.lncl == 0) { \n"
"              colorfourth = colorthird;\n"
"              alphafourth = alphathird;\n"
"              isRGBfourth = isRGBthird; \n"
"            } else { \n"
"              ivec2 linepos; \n "
"              linepos.y = 0; \n "
"              linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"              colorfourth = texelFetch( s_lncl, linepos ,0 );\n"
"              alphafourth = float((int(colorfourth.a * 255.0)&0xF8)>>3)/31.0;\n"
"              isRGBfourth = 1;\n"
"              use_lncl = 1;\n"
"            }\n"
"            modethird= prio.mode&0x7; \n"
"            colorthird = prio.Color; \n"
"            alphathird = prio.Color.a; \n"
"            isRGBthird = prio.isRGB; \n"
"            foundColor3 = 1; \n"
"          } \n"
"        } \n"
"        if (((prio.mode&0x7) == 0) || ((foundColor1 == 1)&&(foundColor2 == 1)&&(foundColor3 == 1))) { \n"
"          hasColor = 0; \n"
"        } \n"
"      }\n"
"    } \n"
"  } \n"
"  if ((FBMesh == 1) && (foundColor1 == 0)) {\n"
"    mesh = 1;\n"
"    meshCol = FBShadow.rgb;\n"
"  }\n"
//Take care  of the extended coloration mode
"  cc_enabled = use_cc_win;\n"
"  if (use_cc_win != 0) {\n"
"    ivec2 cc_addr = ivec2(textureSize(s_cc_win, 0) * v_texcoord.st); \n"
"    vec4 tmp = texelFetch( s_cc_win, cc_addr ,0 ); \n"
"    if (tmp.a == 0.0) cc_enabled = 0;\n"
"  }\n"
"  if (cc_enabled == 0) {\n"
"  if (extended_cc != 0) { \n"
"    if (ram_mode == 0) { \n"
"      if (use_lncl == 0) { \n"
"        if (modesecond == 1) \n"
"          secondImage.rgb = vec3(colorsecond.rgb); \n"
"        else \n"
"          secondImage.rgb = vec3(0.5 * colorsecond.rgb + 0.5 * colorthird.rgb); \n"
"      } else {\n"
"        if (modesecond == 1) \n"
"          secondImage.rgb = vec3(colorsecond.rgb); \n"
"        else {\n"
"          if (modethird == 1) \n"
"            secondImage.rgb = vec3(0.5 * colorsecond.rgb + 0.5 * colorthird.rgb); \n"
"          else \n"
"            secondImage.rgb = vec3(0.66666 * colorsecond.rgb + 0.33334 * colorthird.rgb); \n"
"        }\n"
"      }\n"
"    } else {\n"
"      if (use_lncl == 0) { \n"
"       if (isRGBthird == 0) { \n"
"          secondImage.rgb = vec3(colorsecond.rgb); \n"
"       } else { \n"
"         if (modesecond == 1) { \n"
"           secondImage.rgb = vec3(colorsecond.rgb); \n"
"         } else {\n"
"           secondImage.rgb = vec3(0.5 * colorsecond.rgb + 0.5 * colorthird.rgb); \n"
"         } \n"
"       }\n"
"      } else {\n"
"       if (isRGBthird == 0) { \n"
"           secondImage.rgb = vec3(colorsecond.rgb); \n"
"       } else { \n"
"         if (isRGBfourth == 0) {\n"
"           if (modesecond == 1) secondImage.rgb = vec3(colorsecond.rgb);\n"
"           else secondImage.rgb = vec3(0.5 * colorsecond.rgb + 0.5 * colorthird.rgb);\n"
"         } else { \n"
"           if (modesecond == 1) secondImage.rgb = vec3(colorsecond.rgb);\n"
"           else { \n"
"             if (modethird == 1) secondImage.rgb = vec3(0.5 * colorsecond.rgb + 0.5 * colorthird.rgb);\n"
"             else secondImage.rgb = vec3(0.5 * colorsecond.rgb + 0.25 * colorthird.rgb + 0.25 * colorfourth.rgb);\n"
"           }\n"
"         }\n"
"       }\n"
"      }\n"
"    } \n"
"  } else { \n"
"    secondImage.rgb = vec3(colorsecond.rgb); \n"
"  } \n"

"  if (modetop == 1) topImage = vec4(colortop.rgb, 1.0); \n"
"  if (modetop == 2) topImage = vec4(colortop.rgb, 0.0); \n"
"  if (modetop == 3) topImage = vec4(colortop.rgb*alphatop, alphatop); \n"
"  if (modetop == 4) topImage = vec4(colortop.rgb*alphasecond, alphasecond); \n"

"  finalColor = vec4( topImage.rgb + (1.0 - topImage.a) * secondImage.rgb, 1.0); \n"
"  } else {\n"
"  finalColor = vec4(colortop.rgb, 1.0);\n"
"  }\n"
"  if (shadow == 1) finalColor.rgb = finalColor.rgb * 0.5;\n"
"  if (mesh == 1) finalColor.rgb = finalColor.rgb * 0.5 + meshCol.rgb * 0.5;\n"
#ifdef DEBUG_BLIT
"  topColor = topImage;\n"
"  secondColor = secondImage;\n"
"  thirdColor = colortop;\n"
"  fourthColor.rgb = FBShadow.rgb;\n"
"  fourthColor.a = (FBMeshPrio*8.0+mesh*128.0)/255.0;\n"
#endif
"} \n";

const GLchar * pYglprg_vdp2_blit_none_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_no_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_none_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_as_is_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_no_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_as_is_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_src_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_no_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_src_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_dst_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_no_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_dst_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };


const GLchar * pYglprg_vdp2_blit_less_none_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_less_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_none_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_less_as_is_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_less_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_as_is_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_less_src_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_less_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_src_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_less_dst_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_less_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_dst_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };

const GLchar * pYglprg_vdp2_blit_equal_none_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_equal_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_none_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_equal_as_is_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_equal_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_as_is_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_equal_src_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_equal_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_src_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_equal_dst_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_equal_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_dst_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };

const GLchar * pYglprg_vdp2_blit_more_none_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_more_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_none_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_more_as_is_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_more_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_as_is_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_more_src_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_more_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_src_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_more_dst_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_more_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_dst_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };

const GLchar * pYglprg_vdp2_blit_msb_none_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_msb_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_none_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_msb_as_is_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_msb_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_as_is_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_msb_src_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_msb_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_src_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };
const GLchar * pYglprg_vdp2_blit_msb_dst_alpha_f[] = { vdp2blit_start_f, Yglprg_vdp2_drawfb_cram_f, Yglprg_vdp2_drawfb_cram_msb_color_col_f, Yglprg_vdp2_drawfb_cram_epiloge_dst_alpha_f, Yglprg_vdp2_drawfb_cram_eiploge_f, vdp2blit_partial_end_f,  vdp2blit_filter_f, vdp2blit_end_f, NULL };

void Ygl_initDrawFrameBuffershader(int id);

int YglInitDrawFrameBufferShaders(int id) {

  if (id == PG_VDP2_DRAWFRAMEBUFF_NONE) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_NONE --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_NONE, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_none_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_NONE);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_AS_IS) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_AS_IS --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_AS_IS, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_as_is_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_AS_IS);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_SRC_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_SRC_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_SRC_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_src_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_SRC_ALPHA);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_DST_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_DST_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_DST_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_dst_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_DST_ALPHA);


  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_LESS_NONE) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_LESS_NONE --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_LESS_NONE, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_less_none_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_LESS_NONE);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_LESS_AS_IS) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_LESS_AS_IS --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_LESS_AS_IS, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_less_as_is_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_LESS_AS_IS);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_LESS_SRC_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_LESS_SRC_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_LESS_SRC_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_less_src_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_LESS_SRC_ALPHA);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_LESS_DST_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_LESS_DST_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_LESS_DST_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_less_dst_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_LESS_DST_ALPHA);


  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_EUQAL_NONE) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_EUQAL_NONE --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_NONE, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_equal_none_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_NONE);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_EUQAL_AS_IS) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_EUQAL_AS_IS --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_AS_IS, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_equal_as_is_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_AS_IS);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_EUQAL_SRC_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_EUQAL_SRC_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_SRC_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_equal_src_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_SRC_ALPHA);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_EUQAL_DST_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_EUQAL_DST_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_DST_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_equal_dst_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_EUQAL_DST_ALPHA);


  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MORE_NONE) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MORE_NONE --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MORE_NONE, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_more_none_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MORE_NONE);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MORE_AS_IS) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MORE_AS_IS --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MORE_AS_IS, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_more_as_is_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MORE_AS_IS);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MORE_SRC_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MORE_SRC_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MORE_SRC_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_more_src_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MORE_SRC_ALPHA);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MORE_DST_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MORE_DST_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MORE_DST_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_more_dst_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MORE_DST_ALPHA);


  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MSB_NONE) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MSB_NONE --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MSB_NONE, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_msb_none_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MSB_NONE);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MSB_AS_IS) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MSB_AS_IS --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MSB_AS_IS, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_msb_as_is_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MSB_AS_IS);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MSB_SRC_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MSB_SRC_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MSB_SRC_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_msb_src_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MSB_SRC_ALPHA);
  }
  if (id == PG_VDP2_DRAWFRAMEBUFF_MSB_DST_ALPHA) {
    YGLLOG("PG_VDP2_DRAWFRAMEBUFF_MSB_DST_ALPHA --START--\n");
    if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_MSB_DST_ALPHA, pYglprg_vdp2_blit_v, pYglprg_vdp2_blit_msb_dst_alpha_f, 8, NULL, NULL, NULL) != 0) { return -1; }
    Ygl_initDrawFrameBuffershader(PG_VDP2_DRAWFRAMEBUFF_MSB_DST_ALPHA);
  }
  return 0;
}


void Ygl_initDrawFrameBuffershader(int id) {

  int arrayid = id- PG_VDP2_DRAWFRAMEBUFF_NONE;
  if ( arrayid < 0 || arrayid >= MAX_FRAME_BUFFER_UNIFORM) {
    abort();
  }

  g_draw_framebuffer_uniforms[arrayid].idvdp1FrameBuffer = glGetUniformLocation(_prgid[id], (const GLchar *)"s_vdp1FrameBuffer");
  g_draw_framebuffer_uniforms[arrayid].idvdp1FrameBufferAttr = glGetUniformLocation(_prgid[id], (const GLchar *)"s_vdp1FrameBufferAttr");
  g_draw_framebuffer_uniforms[arrayid].idvdp2regs = glGetUniformLocation(_prgid[id], (const GLchar *)"s_vdp2reg");
  g_draw_framebuffer_uniforms[arrayid].idcram = glGetUniformLocation(_prgid[id], (const GLchar *)"s_color");
  g_draw_framebuffer_uniforms[arrayid].idline = glGetUniformLocation(_prgid[id], (const GLchar *)"s_line");
}


/*------------------------------------------------------------------------------------
*  VDP2 Draw Frame buffer Operation( Shadow drawing for ADD color mode )
* ----------------------------------------------------------------------------------*/

int Ygl_uniformVDP2DrawFramebuffer(float * offsetcol, SpriteMode mode, Vdp2* varVdp2Regs)
{
   int arrayid;

   int pgid = PG_VDP2_DRAWFRAMEBUFF_NONE;

   const int SPCCN = ((varVdp2Regs->CCCTL >> 6) & 0x01); // hard/vdp2/hon/p12_14.htm#NxCCEN_
   const int CCRTMD = ((varVdp2Regs->CCCTL >> 9) & 0x01); // hard/vdp2/hon/p12_14.htm#CCRTMD_
   const int CCMD = ((varVdp2Regs->CCCTL >> 8) & 0x01);  // hard/vdp2/hon/p12_14.htm#CCMD_
   const int SPLCEN = (varVdp2Regs->LNCLEN & 0x20); // hard/vdp2/hon/p11_30.htm#NxLCEN_

   if ( SPCCN ) {
     const int SPCCCS = (varVdp2Regs->SPCTL >> 12) & 0x3;
     switch (SPCCCS)
     {
       case 0:
         pgid = PG_VDP2_DRAWFRAMEBUFF_LESS_NONE;
         break;
       case 1:
         pgid = PG_VDP2_DRAWFRAMEBUFF_EUQAL_NONE;
         break;
       case 2:
         pgid = PG_VDP2_DRAWFRAMEBUFF_MORE_NONE;
         break;
       case 3:
         pgid = PG_VDP2_DRAWFRAMEBUFF_MSB_NONE;
         break;
    }
  }
  pgid += mode-NONE;

  arrayid = pgid - PG_VDP2_DRAWFRAMEBUFF_NONE;

  if (_prgid[pgid] == 0) {
    if (YglInitDrawFrameBufferShaders(pgid) != 0) {
      return -1;
    }
  }
  GLUSEPROG(_prgid[pgid]);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  glDisableVertexAttribArray(3);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idcram, 11);
  glActiveTexture(GL_TEXTURE11);
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idvdp2regs, 12);
  glActiveTexture(GL_TEXTURE12);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);

  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idvdp1FrameBufferAttr, 10);
  glUniform1i(g_draw_framebuffer_uniforms[arrayid].idvdp1FrameBuffer, 9);
  return _prgid[pgid];
}

/*------------------------------------------------------------------------------------
 *  VDP2 Add Blend operaiotn
 * ----------------------------------------------------------------------------------*/
int Ygl_uniformAddBlend(void * p, YglTextureManager *tm, Vdp2 *varVdp2Regs, int id )
{
   glBlendFunc(GL_ONE,GL_ONE);
   return 0;
}

int Ygl_cleanupAddBlend(void * p, YglTextureManager *tm)
{
   glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
   return 0;
}

int YglGetProgramId( int prg )
{
   return _prgid[prg];
}

int YglInitShader(int id, const GLchar * vertex[], const GLchar * frag[], int fcount, const GLchar * tc[], const GLchar * te[], const GLchar * g[] )
{
    GLint compiled,linked;
    GLuint vshader;
    GLuint fshader;
  GLuint tcsHandle = 0;
  GLuint tesHandle = 0;
  GLuint gsHandle = 0;

   _prgid[id] = glCreateProgram();
    if (_prgid[id] == 0 ) return -1;

    vshader = glCreateShader(GL_VERTEX_SHADER);
  fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vertex, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
       YGLLOG( "Compile error in vertex shader. %d\n", id );
       Ygl_printShaderError(vshader);
       _prgid[id] = 0;
       return -1;
    }

    glShaderSource(fshader, fcount, frag, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
       YGLLOG( "Compile error in fragment shader.%d \n", id);
       Ygl_printShaderError(fshader);
       _prgid[id] = 0;
       return -1;
     }

    glAttachShader(_prgid[id], vshader);
    glAttachShader(_prgid[id], fshader);

  if (tc != NULL){
    tcsHandle = glCreateShader(GL_TESS_CONTROL_SHADER);
    if (tcsHandle == 0){
      YGLLOG("GL_TESS_CONTROL_SHADER is not supported\n");
    }
    glShaderSource(tcsHandle, 1, tc, NULL);
    glCompileShader(tcsHandle);
    glGetShaderiv(tcsHandle, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in GL_TESS_CONTROL_SHADER shader.\n");
      Ygl_printShaderError(tcsHandle);
      _prgid[id] = 0;
      return -1;
    }
    glAttachShader(_prgid[id], tcsHandle);
  }
  if (te != NULL){
    tesHandle = glCreateShader(GL_TESS_EVALUATION_SHADER);
    if (tesHandle == 0){
      YGLLOG("GL_TESS_EVALUATION_SHADER is not supported\n");
    }
    glShaderSource(tesHandle, 1, te, NULL);
    glCompileShader(tesHandle);
    glGetShaderiv(tesHandle, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in GL_TESS_EVALUATION_SHADER shader.\n");
      Ygl_printShaderError(tesHandle);
      _prgid[id] = 0;
      return -1;
    }
    glAttachShader(_prgid[id], tesHandle);
  }
  if (g != NULL){
    gsHandle = glCreateShader(GL_GEOMETRY_SHADER);
    if (gsHandle == 0){
      YGLLOG("GL_GEOMETRY_SHADER is not supported\n");
    }
    glShaderSource(gsHandle, 1, g, NULL);
    glCompileShader(gsHandle);
    glGetShaderiv(gsHandle, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in GL_TESS_EVALUATION_SHADER shader.\n");
      Ygl_printShaderError(gsHandle);
      _prgid[id] = 0;
      return -1;
    }
    glAttachShader(_prgid[id], gsHandle);
  }

    glLinkProgram(_prgid[id]);
    glGetProgramiv(_prgid[id], GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
       YGLLOG("Link error..\n");
       Ygl_printShaderError(_prgid[id]);
       _prgid[id] = 0;
       return -1;
    }
    return 0;
}

int YglProgramInit()
{
   YGLLOG("PG_VDP2_NORMAL\n");
   //
   if (YglInitShader(PG_VDP2_NORMAL, pYglprg_vdp2_normal_v, pYglprg_vdp2_normal_f, 1, NULL, NULL, NULL) != 0)
      return -1;
//vdp2 normal looks not to be setup as it should
  id_normal_s_texture = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"s_texture");
  //id_normal_s_texture_size = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"u_texsize");
  id_normal_color_offset = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"u_color_offset");
  id_normal_s_window = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"s_window");
  id_normal_iswindow = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"is_window");
  id_normal_matrix = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"u_mvpMatrix");
  id_normal_vheight = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"u_vheight");
  id_normal_emu_height = glGetUniformLocation(_prgid[PG_VDP2_NORMAL], (const GLchar *)"u_emu_height");

   YGLLOG("PG_VDP2_NORMAL_CRAM\n");

  if (YglInitShader(PG_VDP2_NORMAL_CRAM, pYglprg_vdp2_normal_v, pYglprg_normal_cram_f, 1, NULL, NULL, NULL) != 0)
    return -1;

  id_normal_cram_s_texture = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_texture");
  id_normal_cram_s_color = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_color");
  id_normal_cram_s_perline = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_perline");
  id_normal_cram_s_window = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"s_window");
  id_normal_cram_isperline = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"is_perline");
  id_normal_cram_iswindow = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"is_window");
  id_normal_cram_color_offset = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_color_offset");
  id_normal_cram_matrix = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_mvpMatrix");
  id_normal_cram_emu_height = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_emu_height");
  id_normal_cram_vheight = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_vheight");
  id_normal_cram_emu_width = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_emu_width");
  id_normal_cram_vwidth = glGetUniformLocation(_prgid[PG_VDP2_NORMAL_CRAM], (const GLchar *)"u_vwidth");


#if 0
  YGLLOG("PG_VDP2_MOSAIC\n");
  if (YglInitShader(PG_VDP2_MOSAIC, pYglprg_vdp1_normal_v, pYglprg_mosaic_f, NULL, NULL, NULL) != 0)
    return -1;
  id_mosaic_s_texture = glGetUniformLocation(_prgid[PG_VDP2_MOSAIC], (const GLchar *)"s_texture");
  id_mosaic = glGetUniformLocation(_prgid[PG_VDP2_MOSAIC], (const GLchar *)"u_mosaic");
  id_mosaic_color_offset = glGetUniformLocation(_prgid[PG_VDP2_MOSAIC], (const GLchar *)"u_color_offset");
#endif

   _prgid[PG_VDP2_MOSAIC] = _prgid[PG_VDP2_NORMAL];

   _prgid[PG_VDP2_MOSAIC_CRAM] = _prgid[PG_VDP2_NORMAL_CRAM];

   YGLLOG("PG_VDP1_NORMAL\n");
   //
   if (YglInitShader(PG_VDP1_NORMAL, pYglprg_vdp1_normal_v, pYglprg_vdp1_normal_f,1, NULL, NULL, NULL) != 0)
      return -1;

   id_vdp1_normal_s_texture = glGetUniformLocation(_prgid[PG_VDP1_NORMAL], (const GLchar *)"s_texture");
   id_vdp1_normal_s_texture_size = glGetUniformLocation(_prgid[PG_VDP1_NORMAL], (const GLchar *)"u_texsize");




   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_GOURAUDSHADING\n");

   if (YglInitShader(PG_VDP1_GOURAUDSHADING, pYglprg_vdp1_gouraudshading_v, pYglprg_vdp1_gouraudshading_f, 1, NULL, NULL, NULL) != 0)
      return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_GOURAUDSHADING], &id_g);

   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_MSB_SHADOW\n");

   //
   if (YglInitShader(PG_VDP1_MSB_SHADOW, pYglprg_vdp1_msb_shadow_v, pYglprg_vdp1_msb_shadow_f, 1, NULL, NULL, NULL) != 0)
      return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_MSB_SHADOW], &id_msb_s);

   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_HALFTRANS\n");

   //
   if (YglInitShader(PG_VDP1_HALFTRANS, pYglprg_vdp1_halftrans_v, pYglprg_vdp1_halftrans_f, 1, NULL, NULL, NULL) != 0)
      return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_HALFTRANS], &hf);


   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_SHADOW\n");

   //
   if (YglInitShader(PG_VDP1_SHADOW, pYglprg_vdp1_shadow_v, pYglprg_vdp1_shadow_f, 1, NULL, NULL, NULL) != 0)
     return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_SHADOW], &shadow);

   shadow.sprite = glGetUniformLocation(_prgid[PG_VDP1_SHADOW], (const GLchar *)"u_sprite");
   shadow.fbo = glGetUniformLocation(_prgid[PG_VDP1_SHADOW], (const GLchar *)"u_fbo");


   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_GOURAUDSHADING_HALFTRANS\n");

   if (YglInitShader(PG_VDP1_GOURAUDSHADING_HALFTRANS, pYglprg_vdp1_gouraudshading_hf_v, pYglprg_vdp1_gouraudshading_hf_f, 1, NULL, NULL, NULL) != 0)
      return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_GOURAUDSHADING_HALFTRANS], &id_ght);

   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_HALF_LUMINANCE\n");

   if (YglInitShader(PG_VDP1_HALF_LUMINANCE, pYglprg_vdp1_half_luminance_v, pYglprg_vdp1_half_luminance_f, 1, NULL, NULL, NULL) != 0)
     return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_HALF_LUMINANCE], &half_luminance);

   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_MESH\n");

   if (YglInitShader(PG_VDP1_MESH, pYglprg_vdp1_mesh_v, pYglprg_vdp1_mesh_f, 1, NULL, NULL, NULL) != 0)
     return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_MESH], &mesh);

   //-----------------------------------------------------------------------------------------------------------
   YGLLOG("PG_VDP1_MESH_IMPROVE\n");

   if (YglInitShader(PG_VDP1_MESH_IMPROVE, pYglprg_vdp1_mesh_v, pYglprg_vdp1_mesh_improve_f, 1, NULL, NULL, NULL) != 0)
     return -1;

   Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_MESH_IMPROVE], &mesh_improve);

   YGLLOG("PG_WINDOW\n");
   //
   if (YglInitShader(PG_WINDOW, pYglprg_window_v, pYglprg_window_f, 1, NULL, NULL, NULL) != 0)
      return -1;

   _Ygl->windowpg.prgid=_prgid[PG_WINDOW];
   _Ygl->windowpg.setupUniform    = Ygl_uniformWindow;
   _Ygl->windowpg.cleanupUniform  = Ygl_cleanupWindow;
   _Ygl->windowpg.vertexp         = glGetAttribLocation(_prgid[PG_WINDOW],(const GLchar *)"a_position");
   _Ygl->windowpg.mtxModelView    = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"u_mvpMatrix");
   _Ygl->windowpg.vheight    = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"u_vheight");
   _Ygl->windowpg.var1   = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"win0");
   _Ygl->windowpg.var2   = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"win0mode");
   _Ygl->windowpg.var3   = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"win1");
   _Ygl->windowpg.var4   = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"win1mode");
   _Ygl->windowpg.var5   = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"winOp");
   _Ygl->windowpg.tex0 = glGetUniformLocation(_prgid[PG_WINDOW], (const GLchar *)"s_win0");
   _Ygl->windowpg.tex1 = glGetUniformLocation(_prgid[PG_WINDOW], (const GLchar *)"s_win1");

   YGLLOG("PG_VDP1_STARTUSERCLIP\n");
   //
   if (YglInitShader(PG_VDP1_STARTUSERCLIP, pYglprg_userclip_v, pYglprg_userclip_f, 1, NULL, NULL, NULL) != 0)
      return -1;

  _prgid[PG_VDP1_ENDUSERCLIP] = _prgid[PG_VDP1_STARTUSERCLIP];

   return 0;
}

int YglTesserationProgramInit()
{
  //-----------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_GOURAUDSHADING_TESS\n");
    if (YglInitShader(PG_VDP1_GOURAUDSHADING_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_gouraudshading_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_GOURAUDSHADING_TESS], &grow_tess);

  //-----------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_MSB_SHADOW_TESS\n");
    if (YglInitShader(PG_VDP1_MSB_SHADOW_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_msb_shadow_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_MSB_SHADOW_TESS], &id_msb_tess);

    //---------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_MESH_TESS\n");
    if (YglInitShader(PG_VDP1_MESH_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_mesh_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_MESH_TESS], &mesh_tess);

    //---------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_MESH_TESS_IMPROVE\n");
    if (YglInitShader(PG_VDP1_MESH_TESS_IMPROVE,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_mesh_improve_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_MESH_TESS_IMPROVE], &mesh_tess_improve);


    //---------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_GOURAUDSHADING_HALFTRANS_TESS\n");
    if (YglInitShader(PG_VDP1_GOURAUDSHADING_HALFTRANS_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_gouraudshading_hf_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_GOURAUDSHADING_HALFTRANS_TESS], &id_ght_tess);

    //---------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_HALF_LUMINANCE_TESS\n");
    if (YglInitShader(PG_VDP1_HALF_LUMINANCE_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_half_luminance_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_HALF_LUMINANCE_TESS], &half_luminance_tess);

    //---------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_SHADOW_TESS\n");
    if (YglInitShader(PG_VDP1_SHADOW_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_shadow_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

    shadow_tess.sprite = glGetUniformLocation(_prgid[PG_VDP1_SHADOW_TESS], (const GLchar *)"u_sprite");
    shadow_tess.tessLevelInner = glGetUniformLocation(_prgid[PG_VDP1_SHADOW_TESS], (const GLchar *)"TessLevelInner");
    shadow_tess.tessLevelOuter = glGetUniformLocation(_prgid[PG_VDP1_SHADOW_TESS], (const GLchar *)"TessLevelOuter");
    shadow_tess.fbo = glGetUniformLocation(_prgid[PG_VDP1_SHADOW_TESS], (const GLchar *)"u_fbo");
    shadow_tess.fbo_attr = glGetUniformLocation(_prgid[PG_VDP1_SHADOW_TESS], (const GLchar *)"u_fbo_attr");

    //---------------------------------------------------------------------------------------------------------
    YGLLOG("PG_VDP1_HALFTRANS_TESS\n");
    if (YglInitShader(PG_VDP1_HALFTRANS_TESS,
      pYglprg_vdp1_gouraudshading_tess_v,
      pYglprg_vdp1_halftrans_f,
      1,
      pYglprg_vdp1_gouraudshading_tess_c,
      pYglprg_vdp1_gouraudshading_tess_e,
      pYglprg_vdp1_gouraudshading_tess_g) != 0)
      return -1;

  Ygl_Vdp1CommonGetUniformId(_prgid[PG_VDP1_HALFTRANS_TESS], &hf_tess);

  return 0;
}

int YglProgramChange( YglLevel * level, int prgid )
{
   YglProgram* tmp;
   YglProgram* current;
#if  USEVBO
   int maxsize;
#endif

   level->prgcurrent++;

   if( level->prgcurrent >= level->prgcount)
   {
      level->prgcount++;
      tmp = (YglProgram*)malloc(sizeof(YglProgram)*level->prgcount);
      if( tmp == NULL ) return -1;
      memset(tmp,0,sizeof(YglProgram)*level->prgcount);
      memcpy(tmp,level->prg,sizeof(YglProgram)*(level->prgcount-1));
      free(level->prg);
      level->prg = tmp;

      level->prg[level->prgcurrent].currentQuad = 0;
#if  USEVBO
       level->prg[level->prgcurrent].maxQuad = 14692;
      maxsize = level->prg[level->prgcurrent].maxQuad;
      if( YglGetVertexBuffer(maxsize,
  (void**)&level->prg[level->prgcurrent].quads,
  (void**)&level->prg[level->prgcurrent].textcoords,
  (void**)&level->prg[level->prgcurrent].vertexAttribute  ) != 0 ) {
          return -1;
      }
      if( level->prg[level->prgcurrent].quads == 0 )
      {
          int a=0;
      }
#else
      level->prg[level->prgcurrent].maxQuad = 12*64;
      if ((level->prg[level->prgcurrent].quads = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float))) == NULL)
         return -1;

      if ((level->prg[level->prgcurrent].textcoords = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float) * 2)) == NULL)
         return -1;

       if ((level->prg[level->prgcurrent].vertexAttribute = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float)*2)) == NULL)
         return -1;
#endif
   }

   current = &level->prg[level->prgcurrent];
   level->prg[level->prgcurrent].prgid=prgid;
   level->prg[level->prgcurrent].prg=_prgid[prgid];
   level->prg[level->prgcurrent].vaid = 0;
   level->prg[level->prgcurrent].id = 0;

   if (prgid == PG_VDP2_NORMAL)
   {
     current->setupUniform = Ygl_uniformNormal;
     current->cleanupUniform = Ygl_cleanupNormal;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_matrix;
     current->color_offset = id_normal_color_offset;
   }
   else if (prgid == PG_VDP2_NORMAL_CRAM)
   {
     current->setupUniform = Ygl_uniformNormalCram;
     current->cleanupUniform = Ygl_cleanupNormalCram;

     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_cram_matrix;
     current->color_offset = id_normal_cram_color_offset;

   }
   else if (prgid == PG_VDP2_MOSAIC)
   {
     current->setupUniform = Ygl_uniformMosaic;
     current->cleanupUniform = Ygl_cleanupMosaic;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_matrix;

   }
   else if (prgid == PG_VDP2_MOSAIC_CRAM)
   {
     current->setupUniform = Ygl_uniformMosaic;
     current->cleanupUniform = Ygl_cleanupMosaic;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_normal_cram_matrix;
   }else if( prgid == PG_VDP1_NORMAL )
   {
      current->setupUniform    = Ygl_uniformVdp1Normal;
      current->cleanupUniform  = Ygl_cleanupVdp1Normal;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_NORMAL],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VDP1_NORMAL],(const GLchar *)"u_texMatrix");
      current->tex0 = glGetUniformLocation(_prgid[PG_VDP1_NORMAL], (const GLchar *)"s_texture");

   }else if( prgid == PG_VDP1_GOURAUDSHADING )
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &id_g;
     current->vertexp = 0;
     current->texcoordp = 1;
      level->prg[level->prgcurrent].vaid = 2;
      current->mtxModelView = id_g.mtxModelView;
      current->mtxTexture = id_g.mtxTexture;
      current->tex0 = id_g.tex0;
   }
   else if (prgid == PG_VDP1_MSB_SHADOW)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1ShadowParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1ShadowParam;
     level->prg[level->prgcurrent].ids = &id_msb_s;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = id_msb_s.mtxModelView;
     current->mtxTexture = id_msb_s.mtxTexture;
     current->tex0 = id_msb_s.tex0;
   }
   else if (prgid == PG_VDP1_GOURAUDSHADING_TESS ){
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &grow_tess;
     current->vertexp = 0;
     current->texcoordp = 1;
     level->prg[level->prgcurrent].vaid = 2;
     current->mtxModelView = grow_tess.mtxModelView;
     current->mtxTexture = -1; // glGetUniformLocation(_prgid[PG_VDP1_GOURAUDSHADING], (const GLchar *)"u_texMatrix");
     current->tex0 = -1; // glGetUniformLocation(_prgid[PG_VDP1_GOURAUDSHADING], (const GLchar *)"s_texture");
   }
   else if (prgid == PG_VDP1_MSB_SHADOW_TESS){
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1ShadowParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1ShadowParam;
     level->prg[level->prgcurrent].ids = &id_msb_tess;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = grow_tess.mtxModelView;
     current->mtxTexture = -1; // glGetUniformLocation(_prgid[PG_VDP1_GOURAUDSHADING], (const GLchar *)"u_texMatrix");
     current->tex0 = -1; // glGetUniformLocation(_prgid[PG_VDP1_GOURAUDSHADING], (const GLchar *)"s_texture");
   }
   else if (prgid == PG_VDP1_STARTUSERCLIP)
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformStartUserClip;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupStartUserClip;
      current->vertexp         = 0;
      current->texcoordp       = -1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_STARTUSERCLIP],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = -1; //glGetUniformLocation(_prgid[PG_VDP1_NORMAL],(const GLchar *)"u_texMatrix");

   }
   else if( prgid == PG_VDP1_ENDUSERCLIP )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformEndUserClip;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupEndUserClip;
      current->vertexp         = 0;
      current->texcoordp       = -1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_ENDUSERCLIP],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = -1; //glGetUniformLocation(_prgid[PG_VDP1_NORMAL],(const GLchar *)"u_texMatrix");
   }
   else if( prgid == PG_VDP1_HALFTRANS )
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
    level->prg[level->prgcurrent].ids = &hf;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_HALFTRANS],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VDP1_HALFTRANS],(const GLchar *)"u_texMatrix");

   }
   else if (prgid == PG_VDP1_HALFTRANS_TESS)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &hf_tess;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_HALFTRANS_TESS], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;

   }
   else if (prgid == PG_VDP1_SHADOW_TESS)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &shadow_tess;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_SHADOW_TESS], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1; // glGetUniformLocation(_prgid[PG_VDP1_SHADOW], (const GLchar *)"u_texMatrix");

   }
   else if (prgid == PG_VDP1_SHADOW)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &shadow;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_SHADOW], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = glGetUniformLocation(_prgid[PG_VDP1_SHADOW], (const GLchar *)"u_texMatrix");

   }
   else if (prgid == PG_VDP1_MESH)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &mesh;
     level->prg[level->prgcurrent].vaid = 2;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_MESH], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;

   }
   else if (prgid == PG_VDP1_MESH_IMPROVE)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &mesh_improve;
     level->prg[level->prgcurrent].vaid = 2;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_MESH_IMPROVE], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;

   }
   else if( prgid == PG_VDP1_HALF_LUMINANCE )
   {
      current->setupUniform    = Ygl_uniformVdp1Normal;
      current->cleanupUniform  = Ygl_cleanupVdp1Normal;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_HALF_LUMINANCE],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VDP1_HALF_LUMINANCE],(const GLchar *)"u_texMatrix");
      current->tex0 = glGetUniformLocation(_prgid[PG_VDP1_HALF_LUMINANCE], (const GLchar *)"s_texture");
   }
   else if( prgid == PG_VDP1_HALF_LUMINANCE_TESS )
   {
      current->setupUniform    = Ygl_uniformVdp1CommonParam;
      current->cleanupUniform  = Ygl_cleanupVdp1CommonParam;
      level->prg[level->prgcurrent].ids = &half_luminance_tess;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_HALF_LUMINANCE_TESS],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VDP1_HALF_LUMINANCE_TESS],(const GLchar *)"u_texMatrix");
      current->tex0 = glGetUniformLocation(_prgid[PG_VDP1_HALF_LUMINANCE_TESS], (const GLchar *)"s_texture");
   }
   else if (prgid == PG_VDP1_MESH_TESS)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &mesh_tess;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_MESH_TESS], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;
   }
   else if (prgid == PG_VDP1_MESH_TESS_IMPROVE)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &mesh_tess_improve;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_MESH_TESS_IMPROVE], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;
   }
   else if (prgid == PG_VDP1_GOURAUDSHADING_HALFTRANS)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &id_ght;
     current->vertexp = 0;
     current->texcoordp = 1;
     level->prg[level->prgcurrent].vaid = 2;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_GOURAUDSHADING_HALFTRANS], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;
   }
   else if (prgid == PG_VDP1_GOURAUDSHADING_HALFTRANS_TESS)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformVdp1CommonParam;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupVdp1CommonParam;
     level->prg[level->prgcurrent].ids = &id_ght_tess;
     current->vertexp = 0;
     current->texcoordp = 1;
     level->prg[level->prgcurrent].vaid = 2;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_VDP1_GOURAUDSHADING_HALFTRANS_TESS], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = -1;
   }else{
      level->prg[level->prgcurrent].setupUniform = NULL;
      level->prg[level->prgcurrent].cleanupUniform = NULL;
   }
   return 0;

}

//----------------------------------------------------------------------------------------
static int clear_prg = -1;

static const char vclear_img[] =
  SHADER_VERSION
  "layout (location = 0) in vec2 aPosition;   \n"
  "  \n"
  " void main(void) \n"
  " { \n"
  " gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0); \n"
  " } \n";


static const char fclear_img[] =
  SHADER_VERSION
  "#ifdef GL_ES\n"
  "precision highp float;       \n"
  "#endif\n"
  "uniform float u_emu_height; \n"
  "uniform float u_vheight; \n"
  "uniform sampler2D u_Clear;     \n"
  "out vec4 fragColor; \n"
  "void main()   \n"
  "{  \n"
"    ivec2 linepos; \n "
"    linepos.y = 0; \n "
"    linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
  "  fragColor = texelFetch( u_Clear, linepos,0 ); \n"
  "} \n";

int YglDrawBackScreen() {

  float const vertexPosition[] = {
    1.0f, -1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, 1.0f };

  if (clear_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;

    const GLchar * vclear_img_v[] = { vclear_img, NULL };
    const GLchar * fclear_img_v[] = { fclear_img, NULL };

    clear_prg = glCreateProgram();
    if (clear_prg == 0){
      return -1;
    }

    YGLLOG("DRAW_BACK_SCREEN\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vclear_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      clear_prg = -1;
      return -1;
    }
    glShaderSource(fshader, 1, fclear_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      clear_prg = -1;
      return -1;
    }

    glAttachShader(clear_prg, vshader);
    glAttachShader(clear_prg, fshader);
    glLinkProgram(clear_prg);
    glGetProgramiv(clear_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(clear_prg);
      clear_prg = -1;
      return -1;
    }

    GLUSEPROG(clear_prg);
    glUniform1i(glGetUniformLocation(clear_prg, "u_Clear"), 0);
  }
  else{
    GLUSEPROG(clear_prg);
  }
  glUniform1f(glGetUniformLocation(clear_prg, "u_emu_height"), (float)_Ygl->rheight / (float)_Ygl->rheight);
  glUniform1f(glGetUniformLocation(clear_prg, "u_vheight"), (float)_Ygl->rheight);

  glDisable(GL_STENCIL_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glDisableVertexAttribArray(0);

  return 0;
}

extern vdp2rotationparameter_struct  Vdp1ParaA;

int YglBlitTexture(YglPerLineInfo *bg, int* prioscreens, int* modescreens, int* isRGB, int * isBlur, int* lncl, GLuint* vdp1fb, Vdp2 *varVdp2Regs) {
  int perLine = 0;
  int nbScreen = 6;
  int vdp2blit_prg;

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };
    float offsetcol[4];

    glBindVertexArray(_Ygl->vao);

    vdp2blit_prg = Ygl_uniformVDP2DrawFramebuffer(offsetcol, getSpriteRenderMode(varVdp2Regs), varVdp2Regs );

    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, vdp1fb[0]);

    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, vdp1fb[1]);

  int gltext[14] = {GL_TEXTURE0, GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_TEXTURE4, GL_TEXTURE5, GL_TEXTURE6, GL_TEXTURE7, GL_TEXTURE8, GL_TEXTURE9, GL_TEXTURE10, GL_TEXTURE11, GL_TEXTURE12, GL_TEXTURE13};


#ifdef DEBUG_BLIT
    glBindFragDataLocation(vdp2blit_prg, 1, "topColor");
    glBindFragDataLocation(vdp2blit_prg, 2, "secondColor");
    glBindFragDataLocation(vdp2blit_prg, 3, "thirdColor");
    glBindFragDataLocation(vdp2blit_prg, 4, "fourthColor");
#endif
    glBindFragDataLocation(vdp2blit_prg, 0, "finalColor");

  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture0"), 0);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture1"), 1);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture2"), 2);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture3"), 3);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture4"), 4);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_texture5"), 5);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_back"), 7);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_lncl"), 8);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "s_cc_win"), 13);

  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "mode"), 7, modescreens);
  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "isRGB"), 6, isRGB);
  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "isBlur"), 7, isBlur);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "fbon"), (_Ygl->vdp1On[_Ygl->readframe] != 0));
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "ram_mode"), Vdp2Internal.ColorMode);
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "extended_cc"), ((varVdp2Regs->CCCTL & 0x400) != 0) );
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "use_cc_win"), (_Ygl->use_cc_win != 0) );
  glUniform1f(glGetUniformLocation(vdp2blit_prg, "u_emu_height"),(float)_Ygl->rheight / (float)_Ygl->height);
  glUniform1f(glGetUniformLocation(vdp2blit_prg, "u_vheight"), (float)_Ygl->height);

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);

  if (_Ygl->use_cc_win != 0) {
    glActiveTexture(GL_TEXTURE13);
    glBindTexture(GL_TEXTURE_2D, _Ygl->window_cc_fbotex);
  }

  int id = 0;
  for (int i=0; i<nbScreen; i++) {
    if (prioscreens[i] != 0) {
      glActiveTexture(gltext[i]);
      glBindTexture(GL_TEXTURE_2D, prioscreens[i]);
      id++;
    }
  }
  glUniform1i(glGetUniformLocation(vdp2blit_prg, "screen_nb"), id);

  glActiveTexture(gltext[7]);
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_fbotex[0]);

  glActiveTexture(gltext[8]);
  glBindTexture(GL_TEXTURE_2D, _Ygl->lincolor_tex);

  glUniform1iv(glGetUniformLocation(vdp2blit_prg, "u_lncl"), 7, lncl); //_Ygl->prioVa

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  for (int i = 0; i<14; i++) {
    glActiveTexture(gltext[i]);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisable(GL_BLEND);

  return 0;
}

static int opaque_prg = -1;

static const char opaque_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      " v_texcoord  = a_texcoord; \n"
      "} ";

static const char opaque_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr = ivec2(textureSize(s_texture, 0) * v_texcoord.st); \n"
"  fragColor = texelFetch( s_texture, addr,0 );         \n"
"  if (fragColor.a == 0.0) discard;  \n"
"  fragColor.a = 1.0;  \n"
"}  \n";

int YglBlitOpaque(int texture) {
  const GLchar * fblit_opaque_v[] = { opaque_v, NULL };
  const GLchar * fblit_opaque_f[] = { opaque_f, NULL };

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };

  if (opaque_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    if (opaque_prg != -1) glDeleteProgram(opaque_prg);
    opaque_prg = glCreateProgram();
    if (opaque_prg == 0){
      return -1;
    }

    YGLLOG("BLIT_OPAQUE\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_opaque_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      opaque_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_opaque_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      opaque_prg = -1;
      abort();
    }

    glAttachShader(opaque_prg, vshader);
    glAttachShader(opaque_prg, fshader);
    glLinkProgram(opaque_prg);
    glGetProgramiv(opaque_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(opaque_prg);
      opaque_prg = -1;
      abort();
    }

    GLUSEPROG(opaque_prg);
    glUniform1i(glGetUniformLocation(opaque_prg, "s_texture"), 0);
  }
  else{
    GLUSEPROG(opaque_prg);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  return 0;
}

//--------------------------------------------------------------------------------------------------------------
static int winprio_prg = -1;

static const char winprio_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      " v_texcoord  = a_texcoord; \n"
      "} ";

static const char winprio_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr = ivec2(textureSize(s_texture, 0) * v_texcoord.st); \n"
"  fragColor = texelFetch( s_texture, addr,0 );         \n"
"  if (fragColor.a == 0.0) discard;  \n"
//"  fragColor.a = 1.0;  \n"
"}  \n";

int YglBlitSimple(int texture, int blend) {
  const GLchar * fblit_winprio_v[] = { winprio_v, NULL };
  const GLchar * fblit_winprio_f[] = { winprio_f, NULL };

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f
  };

  if (winprio_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    if (winprio_prg != -1) glDeleteProgram(winprio_prg);
    winprio_prg = glCreateProgram();
    if (winprio_prg == 0){
      return -1;
    }

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_winprio_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      winprio_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_winprio_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      winprio_prg = -1;
      abort();
    }

    glAttachShader(winprio_prg, vshader);
    glAttachShader(winprio_prg, fshader);
    glLinkProgram(winprio_prg);
    glGetProgramiv(winprio_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(winprio_prg);
      winprio_prg = -1;
      abort();
    }

    GLUSEPROG(winprio_prg);
    glUniform1i(glGetUniformLocation(winprio_prg, "s_texture"), 0);
  }
  else{
    GLUSEPROG(winprio_prg);
  }


  if (blend) {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisable(GL_BLEND);

  return 0;
}



//--------------------------------------------------------------------------------------------------------------
static int vdp1_write_prg = -1;
static int vdp1_read_prg = -1;
static GLint vdp1MtxModelView = 0;

static const char vdp1_v[] =
      SHADER_VERSION
      "layout (location = 0) in vec2 a_position;   \n"
      "layout (location = 1) in vec2 a_texcoord;   \n"
      "out  highp vec2 v_texcoord;     \n"
      "void main()       \n"
      "{ \n"
      " gl_Position = vec4(a_position.x, a_position.y, 0.0, 1.0); \n"
      "   v_texcoord  = a_texcoord; \n"
      "} ";

static const char vdp1_write_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in highp vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  addr.x = int( v_texcoord.x);          \n"
"  addr.y = int(v_texcoord.y);          \n"
"  vec4 tex = texelFetch( s_texture, addr,0 );         \n"
//"  if (tex.agb == vec3(0.0)) tex.ragb = vec4(0.5, 0.0, 0.0, 0.0);   \n"
"  if ((int(tex.r * 255.0)&0x80) == 0) discard;\n"
"  fragColor.r = tex.a;         \n"
"  fragColor.g = tex.b;         \n"
"  fragColor.b = tex.g;         \n"
"  fragColor.a = tex.r;         \n"
"}  \n";

static const char vdp1_read_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in highp vec2 v_texcoord; \n"
"uniform sampler2D s_texture;  \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  addr.x = int( v_texcoord.x);          \n"
"  addr.y = int(v_texcoord.y);          \n"
"  vec4 tex = texelFetch( s_texture, addr,0 );         \n"
//"  if (tex.agb == vec3(0.0)) tex.ragb = vec4(0.5, 0.0, 0.0, 0.0);   \n"
"  if ((int(tex.a * 255.0)&0x80) == 0) discard;\n"
"  fragColor.r = tex.a;         \n"
"  fragColor.g = tex.b;         \n"
"  fragColor.b = tex.g;         \n"
"  fragColor.a = tex.r;         \n"
"}  \n";

int YglBlitVDP1(u32 srcTexture, float w, float h, int write) {
  const GLchar * fblit_vdp1_v[] = { vdp1_v, NULL };
  const GLchar * fblit_vdp1_write_f[] = { vdp1_write_f, NULL };
  const GLchar * fblit_vdp1_read_f[] = { vdp1_read_f, NULL };
  const int flip = 0;

  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };

  float const textureCoord[] = {
    w, h,
    0.0f, h,
    w, 0.0f,
    0.0f, 0.0f
  };
  float const textureCoordFlip[] = {
    w, 0.0f,
    0.0f, 0.0f,
    w, h,
    0.0f, h
  };

  if (vdp1_write_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    if (vdp1_write_prg != -1) glDeleteProgram(vdp1_write_prg);
    vdp1_write_prg = glCreateProgram();
    if (vdp1_write_prg == 0){
      return -1;
    }

    YGLLOG("BLIT_VDP1_WRITE\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_vdp1_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      vdp1_write_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_vdp1_write_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      vdp1_write_prg = -1;
      abort();
    }

    glAttachShader(vdp1_write_prg, vshader);
    glAttachShader(vdp1_write_prg, fshader);
    glLinkProgram(vdp1_write_prg);
    glGetProgramiv(vdp1_write_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(vdp1_write_prg);
      vdp1_write_prg = -1;
      abort();
    }
    glUniform1i(glGetUniformLocation(vdp1_write_prg, "s_texture"), 0);
  }

  if (vdp1_read_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    if (vdp1_read_prg != -1) glDeleteProgram(vdp1_read_prg);
    vdp1_read_prg = glCreateProgram();
    if (vdp1_read_prg == 0){
      return -1;
    }

    YGLLOG("BLIT_VDP1_WRITE\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, fblit_vdp1_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      vdp1_read_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_vdp1_read_f, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      vdp1_read_prg = -1;
      abort();
    }

    glAttachShader(vdp1_read_prg, vshader);
    glAttachShader(vdp1_read_prg, fshader);
    glLinkProgram(vdp1_read_prg);
    glGetProgramiv(vdp1_read_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(vdp1_read_prg);
      vdp1_read_prg = -1;
      abort();
    }
    glUniform1i(glGetUniformLocation(vdp1_read_prg, "s_texture"), 0);
  }

  if (write == 0){
    GLUSEPROG(vdp1_read_prg);
  } else {
    GLUSEPROG(vdp1_write_prg);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  if (flip == 1){
     glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoordFlip_buf);
     glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoordFlip), textureCoordFlip, GL_STREAM_DRAW);
     glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
     glEnableVertexAttribArray(1);
  }
  else{
     glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textureCoord_buf);
     glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoord), textureCoord, GL_STREAM_DRAW);
     glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
     glEnableVertexAttribArray(1);
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTexture);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  return 0;
}

//----------------------------------------------------------------------------------------
static int blit_prg = -1;
static int blit_mode = -1;
static int scanline = -1;
static int u_w = -1;
static int u_h = -1;
static int u_l = -1;
static int u_d = -1;
static int outputSize = -1;
static int inputSize = -1;

static const char vblit_img[] =
  SHADER_VERSION
  "layout (location = 0) in vec2 aPosition;   \n"
  "layout (location = 1) in vec2 aTexCoord;   \n"
  "out  highp vec2 vTexCoord;     \n"
  "  \n"
  " void main(void) \n"
  " { \n"
  " vTexCoord = aTexCoord;     \n"
  " gl_Position = vec4(aPosition.x, aPosition.y, 0.0, 1.0); \n"
  " } \n";


static const char fblit_head[] =
  SHADER_VERSION
  "#ifdef GL_ES\n"
  "precision highp float;       \n"
  "#endif\n"
  "uniform float fWidth; \n"
  "uniform float fHeight; \n"
  "uniform vec2 lineNumber; \n"
  "uniform float decim; \n"
  "in highp vec2 vTexCoord;     \n"
  "uniform sampler2D u_Src;     \n"
  "out vec4 fragColor; \n";

static const char fblit_img[] =
  "void main()   \n"
  "{   \n"
"	fragColor = Filter( u_Src, vTexCoord ); \n";

static const char fblit_img_end[] =
  "} \n";

/////////////
static const char fblitnear_img[] =
  "vec4 Filter( sampler2D textureSampler, vec2 TexCoord ) \n"
  "{ \n"
  "     return texture( textureSampler, TexCoord ) ; \n"
  "} \n";

static const char fblitbilinear_img[] =
  "// Function to get a texel data from a texture with GL_NEAREST property. \n"
  "// Bi-Linear interpolation is implemented in this function with the  \n"
  "// help of nearest four data. \n"
  "vec4 Filter( sampler2D textureSampler_i, vec2 texCoord_i ) \n"
  "{ \n"
  "	float texelSizeX = 1.0 / fWidth; //size of one texel  \n"
  "	float texelSizeY = 1.0 / fHeight; //size of one texel  \n"
  "	int nX = int( texCoord_i.x * fWidth ); \n"
  "	int nY = int( texCoord_i.y * fHeight ); \n"
  "	vec2 texCoord_New = vec2( ( float( nX ) + 0.5 ) / fWidth, ( float( nY ) + 0.5 ) / fHeight ); \n"
  "	// Take nearest two data in current row. \n"
  "    vec4 p0q0 = texture(textureSampler_i, texCoord_New); \n"
  "    vec4 p1q0 = texture(textureSampler_i, texCoord_New + vec2(texelSizeX, 0)); \n"
  "	// Take nearest two data in bottom row. \n"
  "    vec4 p0q1 = texture(textureSampler_i, texCoord_New + vec2(0, texelSizeY)); \n"
  "    vec4 p1q1 = texture(textureSampler_i, texCoord_New + vec2(texelSizeX , texelSizeY)); \n"
  "    float a = fract( texCoord_i.x * fWidth ); // Get Interpolation factor for X direction. \n"
  "	// Fraction near to valid data. \n"
  "	// Interpolation in X direction. \n"
  "    vec4 pInterp_q0 = mix( p0q0, p1q0, a ); // Interpolates top row in X direction. \n"
  "    vec4 pInterp_q1 = mix( p0q1, p1q1, a ); // Interpolates bottom row in X direction. \n"
  "    float b = fract( texCoord_i.y * fHeight ); // Get Interpolation factor for Y direction. \n"
  "    return mix( pInterp_q0, pInterp_q1, b ); // Interpolate in Y direction. \n"
  "} \n";

/////

GLuint textureCoord_buf[2] = {0,0};

static int last_upmode = 0;

int YglBlitFramebuffer(u32 srcTexture, float w, float h, float dispw, float disph) {
  float width = w;
  float height = h;
  int decim;
  u32 tex = srcTexture;
  const GLchar * fblit_img_v[] = { fblit_head, fblitnear_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblitbilinear_img_v[] = { fblit_head, fblitnear_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblitbicubic_img_v[] = { fblit_head, fblitbicubic_img, fblit_img, fblit_img_end, NULL };
  const GLchar * fblit_img_scanline_v[] = { fblit_head, fblitnear_img, fblit_img, Yglprg_blit_scanline_f, fblit_img_end, NULL };
  const GLchar * fblitbilinear_img_scanline_v[] = { fblit_head, fblitnear_img, fblit_img, Yglprg_blit_scanline_f, fblit_img_end, NULL };
  const GLchar * fblitbicubic_img_scanline_v[] = { fblit_head, fblitbicubic_img, fblit_img, Yglprg_blit_scanline_f, fblit_img_end, NULL };
  int aamode = _Ygl->aamode;

  float const vertexPosition[] = {
    1.0f, -1.0f,
    -1.0f, -1.0f,
    1.0f, 1.0f,
    -1.0f, 1.0f };

  float const textureCoord[16] = {
    1.0f, 0.0f,
    0.0f, 0.0f,
    1.0f, 1.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    1.0f, 1.0f };

  float nbLines = yabsys.IsPal?625.0f:525.0f;

  if (_Ygl->upmode != UP_NONE) {
    int scale = 1;
    if (last_upmode != _Ygl->upmode) {
      if (_Ygl->upfbotex != 0) glDeleteTextures(1, &_Ygl->upfbotex);
      if (_Ygl->upfbo != 0) glDeleteFramebuffers(1, &_Ygl->upfbo);
      _Ygl->upfbo = 0;
      _Ygl->upfbotex = 0;
      last_upmode = _Ygl->upmode;
    }
    scale = Ygl_useUpscaleBuffer();
    glGetIntegerv( GL_VIEWPORT, _Ygl->m_viewport );
    glViewport(0, 0, scale*_Ygl->rwidth, scale*_Ygl->rheight);
    glScissor(0, 0, scale*_Ygl->rwidth, scale*_Ygl->rheight);
    YglUpscaleFramebuffer(srcTexture, _Ygl->upfbo, _Ygl->rwidth, _Ygl->rheight, _Ygl->width, _Ygl->height);
    glViewport(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);
    glScissor(_Ygl->m_viewport[0], _Ygl->m_viewport[1], _Ygl->m_viewport[2], _Ygl->m_viewport[3]);
    tex = _Ygl->upfbotex;
    width = scale*_Ygl->rwidth;
    height = scale*_Ygl->rheight;
  }

  //if ((aamode == AA_NONE) && ((w != dispw) || (h != disph))) aamode = AA_BILINEAR_FILTER;

  if ((blit_prg == -1) || (blit_mode != aamode) || (scanline != _Ygl->scanline)){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    const GLchar * vblit_img_v[] = { vblit_img, NULL };
    if (blit_prg != -1) glDeleteProgram(blit_prg);
    blit_prg = glCreateProgram();
    if (blit_prg == 0){
      return -1;
    }

    blit_mode = aamode;
    scanline = _Ygl->scanline;

    YGLLOG("BLIT_FRAMEBUFFER\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vblit_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      blit_prg = -1;
      return -1;
    }
    if (_Ygl->scanline == 0) {
      switch(aamode) {
        case AA_NONE:
          glShaderSource(fshader, 4, fblit_img_v, NULL);
          break;
        case AA_BILINEAR_FILTER:
          glShaderSource(fshader, 4, fblitbilinear_img_v, NULL);
          break;
        case AA_BICUBIC_FILTER:
          glShaderSource(fshader, 4, fblitbicubic_img_v, NULL);
          break;
      }
    } else {
      switch(aamode) {
        case AA_NONE:
          glShaderSource(fshader, 5, fblit_img_scanline_v, NULL);
          break;
        case AA_BILINEAR_FILTER:
          glShaderSource(fshader, 5, fblitbilinear_img_scanline_v, NULL);
          break;
        case AA_BICUBIC_FILTER:
          glShaderSource(fshader, 5, fblitbicubic_img_scanline_v, NULL);
          break;
      }
    }
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YuiMsg("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      blit_prg = -1;
	  return -1;
    }

    glAttachShader(blit_prg, vshader);
    glAttachShader(blit_prg, fshader);
    glLinkProgram(blit_prg);
    glGetProgramiv(blit_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
		YuiMsg("Link error..\n");
      Ygl_printShaderError(blit_prg);
      blit_prg = -1;
	  return -1;
    }

    GLUSEPROG(blit_prg);
    glUniform1i(glGetUniformLocation(blit_prg, "u_Src"), 0);
    u_w = glGetUniformLocation(blit_prg, "fWidth");
    u_h = glGetUniformLocation(blit_prg, "fHeight");
    u_l = glGetUniformLocation(blit_prg, "lineNumber");
    u_d = glGetUniformLocation(blit_prg, "decim");
  }
  else{
    GLUSEPROG(blit_prg);
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  if (textureCoord_buf[yabsys.isRotated] == 0)
     glGenBuffers(1, &textureCoord_buf[yabsys.isRotated]);
  glBindBuffer(GL_ARRAY_BUFFER, textureCoord_buf[yabsys.isRotated]);
  glBufferData(GL_ARRAY_BUFFER, 8*sizeof(float), &textureCoord[yabsys.isRotated * 8], GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);
  glUniform1f(u_w, width);
  glUniform1f(u_h, height);
  glUniform2f(u_l, nbLines, disph);
  decim = (disph + nbLines) / nbLines;
  if (decim < 2) decim = 2;
  glUniform1f(u_d, (float)decim);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (aamode == AA_BILINEAR_FILTER) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);

  return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

const GLchar vclearb_img[] =
      SHADER_VERSION
      "layout (location = 0) in vec4 a_position;   \n"
      "void main()       \n"
      "{ \n"
      "   gl_Position = a_position; \n"
      "} ";

const GLchar fclearb_img[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  fragColor = vec4(0.0,0.0,0.0,1.0);\n   "
"}  \n";

int YglClear() {
  float const vertexPosition[] = {
    1.0, -1.0f,
    -1.0, -1.0f,
    1.0, 1.0f,
    -1.0, 1.0f };
  if (clear_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;
    const GLchar * vclearb_img_v[] = { vclearb_img, NULL };
    const GLchar * fclearb_img_v[] = { fclearb_img, NULL };

    clear_prg = glCreateProgram();
    if (clear_prg == 0){
      return -1;
    }

    YGLLOG("CLEAR\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vclearb_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      clear_prg = -1;
      return -1;
    }
    glShaderSource(fshader, 1, fclearb_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      clear_prg = -1;
      abort();
    }

    glAttachShader(clear_prg, vshader);
    glAttachShader(clear_prg, fshader);
    glLinkProgram(clear_prg);
    glGetProgramiv(clear_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(clear_prg);
      clear_prg = -1;
      abort();
    }

    GLUSEPROG(clear_prg);
  }
  else{
    GLUSEPROG(clear_prg);
  }

  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexPosition_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertexPosition), vertexPosition, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Clean up
  glDisableVertexAttribArray(0);
  return 0;
}

/*
hard/vdp2/hon/p12_13.htm
*/

const GLchar mosaic_blit_v[] =
SHADER_VERSION
"uniform mat4 u_mvpMatrix;    \n"
"layout (location = 0) in vec4 a_position;   \n"
"layout (location = 1) in vec2 a_texcoord;   \n"
"out  highp vec2 v_texcoord;     \n"
"void main()       \n"
"{ \n"
"   gl_Position = a_position*u_mvpMatrix; \n"
"   v_texcoord  = a_texcoord; \n"
"} ";

const GLchar mosaic_blit_f[] =
SHADER_VERSION
"#ifdef GL_ES\n"
"precision highp float; \n"
"#endif\n"
"in highp vec2 v_texcoord; \n"
"uniform sampler2D u_Src;  \n"
"uniform float u_tw; \n"
"uniform float u_th; \n"
"uniform ivec2 u_mosaic;			 \n"
"out vec4 fragColor; \n"
"void main()   \n"
"{  \n"
"  ivec2 addr; \n"
"  addr.x = int(u_tw * v_texcoord.x);          \n"
"  addr.y = int(u_th) - int(u_th * v_texcoord.y);          \n"
"  addr.x = addr.x / u_mosaic.x * u_mosaic.x;    \n"
"  addr.y = addr.y / u_mosaic.y * u_mosaic.y;    \n"
"  vec4 txcol = texelFetch( u_Src, addr,0 ) ;      \n"
"  if(txcol.a > 0.0)\n      "
"     fragColor = txcol; \n  "
"  else \n      "
"     discard;\n"
"}  \n";

static int mosaic_prg = -1;
static int u_mosaic_mtxModelView = -1;
static int u_mosaic_tw = -1;
static int u_mosaic_th = -1;
static int u_mosaic = -1;

int YglBlitMosaic(u32 srcTexture, float w, float h, GLfloat* matrix, int * mosaic) {

  float vb[] = { 0, 0,
    2.0, 0.0,
    2.0, 2.0,
    0, 2.0, };

  float tb[] = { 0.0, 0.0,
    1.0, 0.0,
    1.0, 1.0,
    0.0, 1.0 };

  vb[0] = 0;
  vb[1] = 0 - 1.0;
  vb[2] = w;
  vb[3] = 0 - 1.0;
  vb[4] = w;
  vb[5] = h - 1.0;
  vb[6] = 0;
  vb[7] = h - 1.0;

  if (mosaic_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;

    const GLchar * vblit_img_v[] = { mosaic_blit_v, NULL };
    const GLchar * fblit_img_v[] = { mosaic_blit_f, NULL };

    mosaic_prg = glCreateProgram();
    if (mosaic_prg == 0) return -1;

    YGLLOG("BLIT_MOSAIC\n");

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vblit_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      mosaic_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      mosaic_prg = -1;
      return -1;
    }

    glAttachShader(mosaic_prg, vshader);
    glAttachShader(mosaic_prg, fshader);
    glLinkProgram(mosaic_prg);
    glGetProgramiv(mosaic_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(mosaic_prg);
      mosaic_prg = -1;
      return -1;
    }

    glUniform1i(glGetUniformLocation(mosaic_prg, "u_Src"), 0);
    u_mosaic_mtxModelView = glGetUniformLocation(mosaic_prg, (const GLchar *)"u_mvpMatrix");
    u_mosaic_tw = glGetUniformLocation(mosaic_prg, "u_tw");
    u_mosaic_th = glGetUniformLocation(mosaic_prg, "u_th");
    u_mosaic = glGetUniformLocation(mosaic_prg, "u_mosaic");

  }
  else{
    GLUSEPROG(mosaic_prg);
  }

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vb_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vb), vb, GL_STREAM_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, _Ygl->tb_buf);
  glBufferData(GL_ARRAY_BUFFER, sizeof(tb), tb, GL_STREAM_DRAW);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);
  glEnableVertexAttribArray(1);
  glUniformMatrix4fv(u_mosaic_mtxModelView, 1, GL_FALSE, matrix);
  glUniform1f(u_mosaic_tw, w);
  glUniform1f(u_mosaic_th, h);
  glUniform2iv(u_mosaic, 1, mosaic);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTexture);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  return 0;
}
