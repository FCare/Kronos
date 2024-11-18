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


#include <stdlib.h>
#include <math.h>
#include "ygl.h"
#include "yui.h"
#include "vidshared.h"
#include "debug.h"
#include "error.h"
#include "vdp1_compute.h"

// #define __USE_OPENGL_DEBUG__

//#define WINDOW_DEBUG

#define YGLDEBUG
//#define YGLDEBUG printf
//#define YGLDEBUG LOG
//#define YGLDEBUG yprintf
//#define YGLLOG

extern u8 * Vdp1FrameBuffer[];
static int rebuild_frame_buffer = 0;

extern int WaitVdp2Async(int sync);
extern int YglDrawBackScreen();

static int YglGenerateBackBuffer();
static int YglDestroyBackBuffer();

static int YglGenerateScreenBuffer();
static int YglDestroyScreenBuffer();

static int YglGenerateOriginalBuffer();
static int YglDestroyOriginalBuffer();

static int YglGenerateColorRam();
static int YglDestroyColorRam();

extern int WinS[enBGMAX+1];
extern int WinS_mode[enBGMAX+1];

extern vdp2rotationparameter_struct  Vdp1ParaA;

u32 * YglGetColorRamPointer(int line);
extern void Ygl_prog_Destroy(void);

#define PI 3.1415926535897932384626433832795f

#define ATLAS_BIAS (0.025f)
extern int waitVdp1Textures( int sync);

#if (defined(__ANDROID__) || defined(IOS)) && !defined(__LIBRETRO__)
PFNGLPATCHPARAMETERIPROC glPatchParameteri = NULL;
PFNGLMEMORYBARRIERPROC glMemoryBarrier = NULL;
#endif

#if defined(__USE_OPENGL_DEBUG__)
static void MessageCallback( GLenum source,
                      GLenum type,
                      GLuint id,
                      GLenum severity,
                      GLsizei length,
                      const GLchar* message,
                      const void* userParam )
{
  YuiMsg("GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
}
#endif

void YglGenerate();


void showMatrix(YglMatrix *mat, const char* name) {
  YuiMsg("===== %s =======\n", name);
  for (int l = 0; l<4; l++) {
    for (int c = 0; c<4; c++) {
      YuiMsg("%02.04f ", mat->m[l][c]);
    }
    YuiMsg("\n");
  }
  YuiMsg("===== End =======\n");
}

void YglOrtho(YglMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ)
{
    float       deltaX = right - left;
    float       deltaY = top - bottom;
    float       deltaZ = farZ - nearZ;
    YglMatrix    ortho;

    if ( (deltaX == 0.0f) || (deltaY == 0.0f) || (deltaZ == 0.0f) )
        return;

    YglLoadIdentity(&ortho);
    ortho.m[0][0] = 2.0f / deltaX;
    ortho.m[0][3] = -(right + left) / deltaX;
    ortho.m[1][1] = 2.0f / deltaY;
    ortho.m[1][3] = -(top + bottom) / deltaY;
    ortho.m[2][2] = -2.0f / deltaZ;
    ortho.m[2][3] = -(nearZ + farZ) / deltaZ;

    YglMatrixMultiply(result, &ortho, result);
}

void YglMatrixMultiply(YglMatrix *result, YglMatrix *srcA, YglMatrix *srcB)
{
    YglMatrix    tmp;
    int         i;

    for (i=0; i<4; i++)
    {
        tmp.m[i][0] =   (srcA->m[i][0] * srcB->m[0][0]) +
                        (srcA->m[i][1] * srcB->m[1][0]) +
                        (srcA->m[i][2] * srcB->m[2][0]) +
                        (srcA->m[i][3] * srcB->m[3][0]) ;

        tmp.m[i][1] =   (srcA->m[i][0] * srcB->m[0][1]) +
                        (srcA->m[i][1] * srcB->m[1][1]) +
                        (srcA->m[i][2] * srcB->m[2][1]) +
                        (srcA->m[i][3] * srcB->m[3][1]) ;

        tmp.m[i][2] =   (srcA->m[i][0] * srcB->m[0][2]) +
                        (srcA->m[i][1] * srcB->m[1][2]) +
                        (srcA->m[i][2] * srcB->m[2][2]) +
                        (srcA->m[i][3] * srcB->m[3][2]) ;

        tmp.m[i][3] =   (srcA->m[i][0] * srcB->m[0][3]) +
                        (srcA->m[i][1] * srcB->m[1][3]) +
                        (srcA->m[i][2] * srcB->m[2][3]) +
                        (srcA->m[i][3] * srcB->m[3][3]) ;
    }
    memcpy(result, &tmp, sizeof(YglMatrix));
}


void YglLoadIdentity(YglMatrix *result)
{
    memset(result, 0x0, sizeof(YglMatrix));
    result->m[0][0] = 1.0f;
    result->m[1][1] = 1.0f;
    result->m[2][2] = 1.0f;
    result->m[3][3] = 1.0f;
}


YglTextureManager * YglTM_vdp2 = NULL;
Ygl * _Ygl;

typedef struct
{
   float s, t, r, q;
} texturecoordinate_struct;


extern int GlHeight;
extern int GlWidth;
extern int vdp1cor;
extern int vdp1cog;
extern int vdp1cob;

//////////////////////////////////////////////////////////////////////////////

YglTextureManager * YglTMInit(unsigned int w, unsigned int h) {

  GLuint error;
  YglTextureManager * tm;
  tm = (YglTextureManager *)malloc(sizeof(YglTextureManager));
  memset(tm, 0, sizeof(YglTextureManager));
  tm->width = w;
  tm->height = h;
  tm->mtx =  YabThreadCreateMutex();

  tm->currentX = 0;
  tm->currentY = 0;
  tm->yMax = 0;

  glGenBuffers(1, &tm->pixelBufferID);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, tm->width * tm->height * 4, NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glGenTextures(1, &tm->textureID);
  glBindTexture(GL_TEXTURE_2D, tm->textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tm->width, tm->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  return tm;
}

//////////////////////////////////////////////////////////////////////////////

void YglTMDeInit(YglTextureManager ** tm) {
  glDeleteTextures(1, &(*tm)->textureID);
  glDeleteBuffers(1, &(*tm)->pixelBufferID);
  (*tm)->textureID = 0;
  (*tm)->pixelBufferID = 0;
  //Free is crashing => stack overflow issue
  free(*tm);
  (*tm) = NULL;
}

//////////////////////////////////////////////////////////////////////////////

void YglTMReset(YglTextureManager * tm  ) {
  YabThreadLock(tm->mtx);
  tm->currentX = 0;
  tm->currentY = 0;
  tm->yMax = 0;
  YabThreadUnLock(tm->mtx);
}

void YglTmPush(YglTextureManager * tm){
  WaitVdp2Async(1);
  YabThreadLock(tm->mtx);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tm->textureID);
  if (tm->texture != NULL ) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tm->width, tm->yMax, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    tm->texture = NULL;
  }
  YabThreadUnLock(tm->mtx);
  YglTMReset(tm);
  YglCacheReset(tm);
}

void YglTmPull(YglTextureManager * tm, u32 flg){
  YabThreadLock(tm->mtx);
  if (tm->texture == NULL) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tm->textureID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
    if (flg) {
      tm->texture = (unsigned int*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, tm->width * tm->height * 4, GL_MAP_WRITE_BIT  );
    } else {
      tm->texture = (unsigned int*)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, tm->width * tm->height * 4, GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_WRITE_BIT  );
    }
    if (tm->texture == NULL){
      abort();
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
  }
  YabThreadUnLock(tm->mtx);
}

static void YglTMRealloc(YglTextureManager * tm, unsigned int width, unsigned int height ){
  GLuint new_textureID;
  GLuint new_pixelBufferID;
  unsigned int * new_texture;
  GLuint error;
  int dh;
  WaitVdp2Async(1);

  if (tm->texture != NULL) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tm->textureID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, tm->pixelBufferID);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    tm->texture = NULL;
  }

  glGenTextures(1, &new_textureID);
  glBindTexture(GL_TEXTURE_2D, new_textureID);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


  glGenBuffers(1, &new_pixelBufferID);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, new_pixelBufferID);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, width * height * 4, NULL, GL_DYNAMIC_DRAW);

  dh = tm->height;
  if (dh > height) dh = height;

  glBindBuffer(GL_COPY_READ_BUFFER, tm->pixelBufferID);
  glBindBuffer(GL_COPY_WRITE_BUFFER, new_pixelBufferID);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, tm->width * dh * 4);

  glBindBuffer(GL_COPY_READ_BUFFER, 0);
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);


  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, new_pixelBufferID);
  new_texture = (unsigned int *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, width * height * 4, GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );

  // Free textures
  glDeleteTextures(1, &tm->textureID);
  glDeleteBuffers(1, &tm->pixelBufferID);

  // user new texture
    tm->width = width;
  tm->height = height;
  tm->texture = new_texture;
  tm->textureID = new_textureID;
  tm->pixelBufferID = new_pixelBufferID;
  return;
}

//////////////////////////////////////////////////////////////////////////////
static void YglTMAllocate_in(YglTextureManager * tm, YglTexture * output, unsigned int w, unsigned int h, unsigned int * x, unsigned int * y) {
  if( tm->width < w ){
    YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    YglTMRealloc( tm, w, tm->height);
    YglTMAllocate_in(tm, output, w, h, x, y);
    return;
  }
  if ((tm->height - tm->currentY) < h) {
    YGLDEBUG("can't allocate texture: %dx%d\n", w, h);
    YglTMRealloc( tm, tm->width, tm->height+512);
    YglTMAllocate_in(tm, output, w, h, x, y);
    return;
  }

  if ((tm->width - tm->currentX) >= w) {
    *x = tm->currentX;
    *y = tm->currentY;
    output->w = tm->width - w;
    output->textdata = tm->texture + tm->currentY * tm->width + tm->currentX;
    tm->currentX += w;

    if ((tm->currentY + h) > tm->yMax){
      tm->yMax = tm->currentY + h;
    }
   } else {
     tm->currentX = 0;
     tm->currentY = tm->yMax;
     YglTMAllocate_in(tm, output, w, h, x, y);
   }
}

void YglTMAllocate(YglTextureManager * tm, YglTexture * output, unsigned int w, unsigned int h, unsigned int * x, unsigned int * y) {
  YabThreadLock(tm->mtx);
  YglTMAllocate_in(tm, output, w, h, x, y);
  YabThreadUnLock(tm->mtx);
}

u32 write_fb[2][512*256] = {0};

static void invalidateVDP1ReadFramebuffer(int frame) {
  _Ygl->vdp1fb_read_buf[frame] = NULL;
}

static u32* getVDP1Framebuffer(int frame) {
  //Verifier si le fb est dirty. Arrive apres un write ou un compute fait ou prgrammé
  if (_Ygl->shallVdp1Erase[frame] != 0) {
    _Ygl->shallVdp1Erase[frame] = 0;
    VIDCSEraseWriteVdp1(frame);
    clearVDP1Framebuffer(frame);
  }
  if (_Ygl->vdp1fb_read_buf[frame] == NULL) {
    //Pas bien ca
    //A faire par core video
      // if (frame == _Ygl->drawframe) vdp1_compute();
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_UPDATE_BARRIER_BIT);
      _Ygl->vdp1fb_read_buf[frame] = vdp1_read(frame);
  }
  return _Ygl->vdp1fb_read_buf[frame];
}

u32* getVDP1ReadFramebuffer() {
  //Only drawFrame can be accessed - Read Framebuffer is only used by VDP2
  return getVDP1Framebuffer(_Ygl->drawframe);
}

u32* getVDP1WriteFramebuffer(int frame) {
  if (_Ygl->vdp1fb_write_buf[frame] == NULL) {
    if (_Ygl->vdp1_pbo[0] == 0) YglGenerate();
    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[frame]);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp1_pbo[frame]);
    _Ygl->vdp1fb_write_buf[frame] = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * 256 * 4, GL_MAP_WRITE_BIT );
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  return _Ygl->vdp1fb_write_buf[frame];
}

void syncVdp1FBBuffer(u32 addr) {
  if (_Ygl->vdp1fb_read_buf[_Ygl->drawframe] != NULL) {
    if (_Ygl->vdp1fb_write_buf[_Ygl->drawframe] != NULL) {
      _Ygl->vdp1fb_read_buf[_Ygl->drawframe][addr] = _Ygl->vdp1fb_write_buf[_Ygl->drawframe][addr];
    }
  }
}

void updateVdp1DrawingFBMem(int frame) {
    if (_Ygl->vdp1fb_write_buf[frame] != NULL) {
      glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[frame]);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp1_pbo[frame]);
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 256, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0 );
      glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT|GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_PIXEL_BUFFER_BARRIER_BIT|GL_FRAMEBUFFER_BARRIER_BIT);
      _Ygl->vdp1fb_write_buf[frame] = NULL;
    }
}

void clearVDP1Framebuffer(int frame) {
  invalidateVDP1ReadFramebuffer(frame);
  if (_Ygl->FBDirty[frame] != 0) {
    u32* buf = getVDP1WriteFramebuffer(frame);
    memset(buf, 0, 512*256*4);
    updateVdp1DrawingFBMem(frame);
    _Ygl->FBDirty[frame] = 0;
  }
}

u32 COLOR16TO24(u16 temp) {
  if ((temp & 0x8000) != 0)
    return ((u32)temp & 0x1F) << 3 | ((u32)temp & 0x3E0) << 6 | ((u32)temp & 0x7C00) << 9| 0x10000; //Blue LSB is used for MSB bit.  else
  else
    return ((u32)temp & 0x7FFF);
}

static u16 COLOR24TO16(u32 temp) {
  if (((temp >> 31)&0x1) == 0) return 0x0000;
  if (((temp >> 30)&0x1) == 0)
    return 0x8000 | ((u32)(temp >> 3)& 0x1F) | ((u32)(temp >> 6)& 0x3E0) | ((u32)(temp >> 9)& 0x7C00);
  else
    return (temp & 0x7FFF);
}

static int warning = 0;


void YglDestroy() {

  if (_Ygl->tmpfbo != 0){
    glDeleteFramebuffers(1, &_Ygl->tmpfbo);
    _Ygl->tmpfbo = 0;
    glDeleteTextures(1, &_Ygl->tmpfbotex);
    _Ygl->tmpfbotex = 0;
  }

  if (_Ygl->upfbo != 0){
    glDeleteFramebuffers(1, &_Ygl->upfbo);
    _Ygl->upfbo = 0;
    glDeleteTextures(1, &_Ygl->upfbotex);
    _Ygl->upfbotex = 0;
  }
  if (_Ygl->vdp1FrameBuff[0] != 0) {
    glDeleteTextures(4,_Ygl->vdp1FrameBuff);
    _Ygl->vdp1FrameBuff[0] = 0;
  }
  if (_Ygl->vdp1_pbo[0] != 0) {
    _Ygl->vdp1_pbo[0] = 0;
    _Ygl->vdp1_pbo[1] = 0;
    glDeleteTextures(2, _Ygl->vdp1AccessTex);
    glDeleteBuffers(2, _Ygl->vdp1_pbo);
    glDeleteFramebuffers(2, _Ygl->vdp1AccessFB);
    _Ygl->vdp1fb_write_buf[0] = NULL;
    _Ygl->vdp1fb_read_buf[0] = NULL;
    _Ygl->vdp1fb_write_buf[1] = NULL;
    _Ygl->vdp1fb_read_buf[1] = NULL;
  }
  if (_Ygl->rboid_depth != 0) {
    glDeleteRenderbuffers(1, &_Ygl->rboid_depth);
    _Ygl->rboid_depth = 0;
  }
  if (_Ygl->vdp1fbo != 0) {
    glDeleteFramebuffers(1, &_Ygl->vdp1fbo);
    _Ygl->vdp1fbo = 0;
  }
  YglDestroyOriginalBuffer();
  YglDestroyBackBuffer();
  YglDestroyScreenBuffer();
}

void YglGenerate() {
  int status;
  GLuint error;
  float col[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  warning = 0;
  YglDestroy();
  vdp1_compute_init( _Ygl->vdp1width, _Ygl->vdp1height, _Ygl->vdp1ratio);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glGenTextures(4, _Ygl->vdp1FrameBuff);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->vdp1width, _Ygl->vdp1height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->vdp1width, _Ygl->vdp1height, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[2]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->vdp1width, _Ygl->vdp1height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[3]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->vdp1width, _Ygl->vdp1height, 0, GL_RG, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  _Ygl->vdp1fb_write_buf[0] = NULL;
  _Ygl->vdp1fb_read_buf[0] = NULL;
  _Ygl->vdp1fb_write_buf[1] = NULL;
  _Ygl->vdp1fb_read_buf[1] = NULL;
  glGenTextures(2, _Ygl->vdp1AccessTex);
  glGenBuffers(2, _Ygl->vdp1_pbo);
  YGLDEBUG("glGenBuffers %d %d\n",_Ygl->vdp1_pbo[0], _Ygl->vdp1_pbo[1]);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1_pbo[0]);
  glBufferData(GL_PIXEL_PACK_BUFFER, 512*256*4, NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1AccessTex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, _Ygl->vdp1_pbo[1]);
  glBufferData(GL_PIXEL_PACK_BUFFER, 512*256*4, NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(2, _Ygl->vdp1AccessFB);

  glGenRenderbuffers(1, &_Ygl->rboid_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, _Ygl->rboid_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _Ygl->vdp1width, _Ygl->vdp1height);
  glGenFramebuffers(1, &_Ygl->vdp1fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->vdp1fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[3], 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _Ygl->rboid_depth);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    abort();
  }
  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfv(GL_COLOR, 1, col);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);
  YglGenerateOriginalBuffer();
  YglGenerateBackBuffer();
  YglGenerateScreenBuffer();
  YGLDEBUG("VIDCSGenFrameBuffer OK\n");
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo);
  glBindTexture(GL_TEXTURE_2D, 0);
  rebuild_frame_buffer = 0;
  _Ygl->needWinUpdate = 1;
}

void YglGenReset() {
  YglDestroy();
  _Ygl->sync = 0;
  if (YglTM_vdp2!= NULL) YglTMDeInit(&YglTM_vdp2);
  rebuild_frame_buffer = 1;
  _Ygl->default_fbo = -1;
  Ygl_prog_Destroy();
}
//////////////////////////////////////////////////////////////////////////////
int VIDCSGenFrameBuffer() {
    u32* vdp1_framebuffer[2];
  if (rebuild_frame_buffer == 0){
    return 0;
  }
  vdp1_framebuffer[0] = (u32*)malloc(0x20000*4);
  vdp1_framebuffer[1] = (u32*)malloc(0x20000*4);
  if (_Ygl->default_fbo == -1) _Ygl->default_fbo = YuiGetFB();
  if (YglTM_vdp2 == NULL) YglTM_vdp2= YglTMInit(1024, 1024);

  for (int j = 0; j<2; j++) {
    u32* buf = getVDP1Framebuffer(j);
    for (int i=0; i<0x20000; i++) {
      vdp1_framebuffer[j][i] = (T1ReadLong((u8*)buf, i*4) & 0xFFFF);
    }
    invalidateVDP1ReadFramebuffer(j);
  }

  YglDestroy();
  YglGenerate();

  for (int j = 0; j<2; j++) {
    u32 *buf = getVDP1WriteFramebuffer(j);
    for (int i = 0; i < 0x20000; i++) {
      buf[i] = (vdp1_framebuffer[j][i]&0xFFFF)|0xFF000000;
    }
    updateVdp1DrawingFBMem(j);
    _Ygl->vdp1IsNotEmpty[j] = -1;
    _Ygl->FBDirty[j] = 1;
  }
  free(vdp1_framebuffer[0]);
  free(vdp1_framebuffer[1]);
  return 0;
}

//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
static int YglDestroyScreenBuffer(){
  if (_Ygl->screen_fbotex[0] != 0) {
    glDeleteTextures(SPRITE,&_Ygl->screen_fbotex[0]);
    _Ygl->screen_fbotex[0] = 0;
  }
  if (_Ygl->screen_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->screen_fbo);
    _Ygl->screen_fbo = 0;
  }
  if (_Ygl->linecolorcoef_tex[0] != 0){
    glDeleteTextures(2,&_Ygl->linecolorcoef_tex[0]);
    glDeleteBuffers(2, &_Ygl->linecolorcoef_pbo[0]);
    _Ygl->linecolorcoef_tex[0] = 0;
  }
  if (_Ygl->rbg_compute_fbotex[0] != 0) {
    glDeleteTextures(2, _Ygl->rbg_compute_fbotex);
    _Ygl->rbg_compute_fbotex[0] = 0;
  }
  if (_Ygl->rbg_compute_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->rbg_compute_fbo);
    _Ygl->rbg_compute_fbo = 0;
  }
  return 0;
}
static int YglGenerateScreenBuffer(){

  int status;
  GLuint error;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  YGLDEBUG("YglGenerateScreenBuffer: %d,%d\n", _Ygl->rwidth, _Ygl->rheight);

  glGenTextures(SPRITE, &_Ygl->screen_fbotex[0]);
  for (int i=0; i<SPRITE; i++) {
    glBindTexture(GL_TEXTURE_2D, _Ygl->screen_fbotex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glGenFramebuffers(1, &_Ygl->screen_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->screen_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->screen_fbotex[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->screen_fbotex[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->screen_fbotex[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->screen_fbotex[3], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, _Ygl->screen_fbotex[4], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT5, GL_TEXTURE_2D, _Ygl->screen_fbotex[5], 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }

  //Generate texture for RBG Line Color Screen insertion
  glGenBuffers(2, &_Ygl->linecolorcoef_pbo[0]);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolorcoef_pbo[0]);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, _Ygl->rwidth * _Ygl->rheight * 4, NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolorcoef_pbo[1]);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, _Ygl->rwidth * _Ygl->rheight * 4, NULL, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  glGenTextures(2, &_Ygl->linecolorcoef_tex[0]);
  glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorcoef_tex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorcoef_tex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  //Generate fbo and texture fr rbh compute shader
  glGenTextures(2, &_Ygl->rbg_compute_fbotex[0]);
  glBindTexture(GL_TEXTURE_2D, _Ygl->rbg_compute_fbotex[0]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->width, _Ygl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, _Ygl->rbg_compute_fbotex[1]);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->width, _Ygl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &_Ygl->rbg_compute_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->rbg_compute_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->rbg_compute_fbotex[0], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->rbg_compute_fbotex[1], 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer: RBG Framebuffer status = %08X\n", status);
    abort();
  }

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
static int YglDestroyBackBuffer() {
  if (_Ygl->back_fbotex != 0) {
    glDeleteTextures(1,&_Ygl->back_fbotex);
    _Ygl->back_fbotex = 0;
  }
  if (_Ygl->back_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->back_fbo);
    _Ygl->back_fbo = 0;
  }
  return 0;
}
static int YglGenerateBackBuffer(){
  int status;
  GLuint error;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  YGLDEBUG("YglGenerateBackBuffer: %d,%d\n", _Ygl->width, _Ygl->height);

  glGenTextures(1, &_Ygl->back_fbotex);
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_fbotex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->rwidth, _Ygl->rheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenFramebuffers(1, &_Ygl->back_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->back_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->back_fbotex, 0);
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }
  return 0;
}

static int YglDestroyColorRam(){
   if (_Ygl->cram_tex != 0) {
     glDeleteTextures(1, &_Ygl->cram_tex);
     _Ygl->cram_tex = 0;
   }
   if (_Ygl->cram_map_tex != 0) {
     glDeleteTextures(1, &_Ygl->cram_map_tex);
     _Ygl->cram_map_tex = 0;
   }
}
static int YglGenerateColorRam(){
  int error;
  if (_Ygl->cram_tex == 0) {
    glGenTextures(1, &_Ygl->cram_tex);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2048, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  if (_Ygl->cram_tex_buf == NULL) {
    _Ygl->cram_tex_buf = (u32*)malloc(2048 * 4*512);
    memset(_Ygl->cram_tex_buf, 0, 2048 * 4*512);
  }
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2048, 512,0,GL_RGBA, GL_UNSIGNED_BYTE,_Ygl->cram_tex_buf);

  if (_Ygl->cram_map_tex == 0) {
    glGenTextures(1, &_Ygl->cram_map_tex);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, _Ygl->cram_map_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}


static int YglDestroyOriginalBuffer(){
  if (_Ygl->original_fbotex[0] != 0) {
    glDeleteTextures(NB_RENDER_LAYER,&_Ygl->original_fbotex[0]);
    _Ygl->original_fbotex[0] = 0;
  }
  if (_Ygl->original_fbo != 0){
    glDeleteFramebuffers(1, &_Ygl->original_fbo);
    _Ygl->original_fbo = 0;
  }
  return 0;
}
static int YglGenerateOriginalBuffer(){
  int status;
  GLuint error;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  YGLDEBUG("YglGenerateOriginalBuffer: %d,%d\n", _Ygl->width, _Ygl->height);

  glGenTextures(NB_RENDER_LAYER, &_Ygl->original_fbotex[0]);
  for (int i=0; i<NB_RENDER_LAYER; i++) {
    glBindTexture(GL_TEXTURE_2D, _Ygl->original_fbotex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, _Ygl->width, _Ygl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glGenFramebuffers(1, &_Ygl->original_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->original_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _Ygl->original_fbotex[0], 0);
#ifdef DEBUG_BLIT
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, _Ygl->original_fbotex[1], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, _Ygl->original_fbotex[2], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, _Ygl->original_fbotex[3], 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, _Ygl->original_fbotex[4], 0);
#endif
  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    YGLDEBUG("YglGenerateOriginalBuffer:Framebuffer status = %08X\n", status);
    abort();
  }
  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);
  return 0;
}

//////////////////////////////////////////////////////////////////////////////
int YglScreenInit(int r, int g, int b, int d) {
  return 0;
}

void deinitLevels(YglLevel * levels, int size) {
  int i, j;
  for (i = 0; i < (size); i++)
  {
    for (j = 0; j < levels[i].prgcount; j++)
    {
      if (levels[i].prg[j].quads)
        free(levels[i].prg[j].quads);
      if (levels[i].prg[j].textcoords)
        free(levels[i].prg[j].textcoords);
      if (levels[i].prg[j].vertexAttribute)
        free(levels[i].prg[j].vertexAttribute);
    }
    free(levels[i].prg);
  }
  free(levels);
}

void initLevels(YglLevel** levels, int size) {
  int i, j;

  if ((*levels = (YglLevel *)malloc(sizeof(YglLevel) * (size))) == NULL){
    return;
  }

  memset(*levels,0,sizeof(YglLevel) * size );
  YglLevel* level = *levels;
  for(i = 0;i < size ;i++) {
    level[i].prgcurrent = 0;
    level[i].uclipcurrent = 0;
    level[i].prgcount = 1;
    level[i].prg = (YglProgram*)malloc(sizeof(YglProgram)*level[i].prgcount);
    memset(  level[i].prg,0,sizeof(YglProgram)*level[i].prgcount);
    if (level[i].prg == NULL){
      return;
    }
    for(j = 0;j < level[i].prgcount; j++) {
      level[i].prg[j].prg=0;
      level[i].prg[j].currentQuad = 0;
      level[i].prg[j].maxQuad = 12 * 2000;
      if ((level[i].prg[j].quads = (float *)malloc(level[i].prg[j].maxQuad * sizeof(float))) == NULL){ return; }
      if ((level[i].prg[j].textcoords = (float *)malloc(level[i].prg[j].maxQuad * sizeof(float) * 2)) == NULL){ return; }
      if ((level[i].prg[j].vertexAttribute = (float *)malloc(level[i].prg[j].maxQuad * sizeof(float) * 2)) == NULL){ return; }
    }
  }
}

extern int YglInitDrawFrameBufferShaders(int id, int CS);
//////////////////////////////////////////////////////////////////////////////
int YglInit(int width, int height, unsigned int depth) {
  unsigned int i,j;
  int maj, min;
  void * dataPointer=NULL;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};
  YGLLOG("YglInit(%d,%d,%d);",width,height,depth );

  glGetIntegerv(GL_MAJOR_VERSION, &maj);
  glGetIntegerv(GL_MINOR_VERSION, &min);
#ifndef __LIBRETRO__
  #if defined(_OGLES3_)
    if (!((maj >=3) && (min >=1))){
      YabErrorMsg("Your graphic card is supporting OpenGLES %d.%d. OpenGLES 3.1 is required!\n", maj, min);
      return -1;
    }
  #else
    if (!((maj >=4) && (min >=3))) {
      YabErrorMsg("Your graphic card is supporting OpenGL Core %d.%d. OpenGL Core 4.3 is required!\n", maj, min);
      return -1;
    }
  #endif
#endif

  if ((_Ygl = (Ygl *)malloc(sizeof(Ygl))) == NULL) {
    return -1;
  }

  memset(_Ygl,0,sizeof(Ygl));

  _Ygl->rwidth = 320;
  _Ygl->rheight = 240;
  _Ygl->vdp1width = 512;
  _Ygl->vdp1height = 256;
  _Ygl->widthRatio = 1.0f;
  _Ygl->heightRatio = 1.0f;
  _Ygl->resolution_mode = RES_ORIGINAL;

  _Ygl->vdp1IsNotEmpty[0] = -1;
  _Ygl->vdp1IsNotEmpty[1] = -1;

  initLevels(&_Ygl->vdp2levels, SPRITE);

  if( _Ygl->mutex == NULL){
    _Ygl->mutex = YabThreadCreateMutex();
  }

  if (_Ygl->crammutex == NULL) {
    _Ygl->crammutex = YabThreadCreateMutex();
  }


#if defined(_USEGLEW_) && !defined(__LIBRETRO__)
  glewExperimental=GL_TRUE;
  if (glewInit() != 0) {
    printf("Glew can not init\n");
    YabSetError(YAB_ERR_CANNOTINIT, _("Glew"));
    exit(-1);
  }
#endif

  glGenBuffers(1, &_Ygl->quads_buf);
  glGenBuffers(1, &_Ygl->textcoords_buf);
  glGenBuffers(1, &_Ygl->vertexAttribute_buf);

  glGenVertexArrays(1, &_Ygl->vao);
  glBindVertexArray(_Ygl->vao);
  glGenBuffers(1, &_Ygl->win0v_buf);
  glGenBuffers(1, &_Ygl->win1v_buf);
  glGenBuffers(1, &_Ygl->vertexPosition_buf);
  glGenBuffers(1, &_Ygl->textureCoordFlip_buf);
  glGenBuffers(1, &_Ygl->textureCoord_buf);
  glGenBuffers(1, &_Ygl->vb_buf);
  glGenBuffers(1, &_Ygl->tb_buf);
  //glEnableVertexAttribArray(_Ygl->vao);

#if defined(__USE_OPENGL_DEBUG__)
  // During init, enable debug output
  glEnable              ( GL_DEBUG_OUTPUT );
  glDebugMessageCallback( (GLDEBUGPROC) MessageCallback, 0 );
#endif

#if defined(__ANDROID__) && !defined(__LIBRETRO__)
  glPatchParameteri = (PFNGLPATCHPARAMETERIPROC)eglGetProcAddress("glPatchParameteri");
  glMemoryBarrier = (PFNGLPATCHPARAMETERIPROC)eglGetProcAddress("glMemoryBarrier");
#endif

  _Ygl->default_fbo = YuiGetFB();
  _Ygl->drawframe = 0;
  _Ygl->readframe = 1;

#if !defined(__LIBRETRO__)
  // This line is causing a black screen on the libretro port
  glGetIntegerv(GL_FRAMEBUFFER_BINDING,(int *)&_Ygl->default_fbo);
#endif

  glClearBufferfv(GL_COLOR, 0, col);
  glClearBufferfi(GL_DEPTH_STENCIL, 0, 0, 0);

  glDisable(GL_BLEND);

  glDisable(GL_DEPTH_TEST);
  glDepthFunc(GL_GEQUAL);

  glCullFace(GL_FRONT_AND_BACK);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DITHER);

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  YglGenerateColorRam();

  YglTM_vdp2 = YglTMInit(1024, 1024);

  vdp1_compute_init(512.0f, 256.0f, _Ygl->vdp1ratio);

  _Ygl->vdp2buf = (u8*)malloc(512 * sizeof(int)* NB_VDP2_REG);

  _Ygl->tmpfbo = 0;
  _Ygl->tmpfbotex = 0;
  _Ygl->upfbo = 0;
  _Ygl->upfbotex = 0;
  _Ygl->upfbo = 0;
  _Ygl->upfbotex = 0;

  if (YglProgramInit() != 0) {
    YGLDEBUG("Fail to YglProgramInit\n");
    abort();
  }

    YglInitDrawFrameBufferShaders(2144, 0);
    YglInitDrawFrameBufferShaders(2179, 0);
    YglInitDrawFrameBufferShaders(6665, 0);
    YglInitDrawFrameBufferShaders(6670, 0);
    YglInitDrawFrameBufferShaders(6635, 0);
    YglInitDrawFrameBufferShaders(2149, 0);
    YglInitDrawFrameBufferShaders(1024, 0);
    YglInitDrawFrameBufferShaders(1094, 0);
    YglInitDrawFrameBufferShaders(1129, 0);
    YglInitDrawFrameBufferShaders(1059, 0);
    YglInitDrawFrameBufferShaders(2184, 0);
    YglInitDrawFrameBufferShaders(2669, 0);
    YglInitDrawFrameBufferShaders(2634, 0);
    YglInitDrawFrameBufferShaders(2249, 0);
    YglInitDrawFrameBufferShaders(2259, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, _Ygl->default_fbo );
  glBindTexture(GL_TEXTURE_2D, 0);
  _Ygl->st = 0;
  _Ygl->aamode = AA_NONE;
  _Ygl->interlace = NORMAL_INTERLACE;
  _Ygl->stretch = ORIGINAL_RATIO;
  _Ygl->wireframe_mode = 0;

  return 0;
}

//////////////////////////////////////////////////////////////////////////////
void YglDeInit(void) {
   unsigned int i,j;

   if (YglTM_vdp2 != NULL)    YglTMDeInit(&YglTM_vdp2);

   if (_Ygl)
   {
       YglDestroyColorRam();

       if (_Ygl->vdp2buf != NULL) free(_Ygl->vdp2buf);
       _Ygl->vdp2buf = NULL;

      if(_Ygl->mutex) YabThreadFreeMutex(_Ygl->mutex );

      if (_Ygl->vdp2levels)
      deinitLevels(_Ygl->vdp2levels, SPRITE);

      free(_Ygl);
   }

}


//////////////////////////////////////////////////////////////////////////////

static int progNew = 0;

YglProgram * YglGetProgram( YglSprite * input, int prg, YglTextureManager *tm, int prio)
{
   YglLevel   *level;
   YglProgram *program;

   level = &_Ygl->vdp2levels[input->idScreen];

    if (progNew == 0){
      level->prgcurrent++;
      YglProgramChange(level, prg);
      progNew = 1;
    }

   if( level->prg[level->prgcurrent].prgid != prg ) {
      level->prgcurrent++;
      YglProgramChange(level,prg);
      level->prg[level->prgcurrent].prgid = prg;
   }
   else if (input->idScreen != level->prg[level->prgcurrent].id ){
     level->prgcurrent++;
     YglProgramChange(level, prg);
     level->prg[level->prgcurrent].id = input->idScreen;
   }
   program = &level->prg[level->prgcurrent];

   if ((program->currentQuad + YGL_MAX_NEED_BUFFER) >= program->maxQuad) {
     program->maxQuad += YGL_MAX_NEED_BUFFER*32;
     program->quads = (float *)realloc(program->quads, program->maxQuad * sizeof(float));
      program->textcoords = (float *) realloc(program->textcoords, program->maxQuad * sizeof(float) * 2);
      program->vertexAttribute = (float *) realloc(program->vertexAttribute, program->maxQuad * sizeof(float)*2);
      YglCacheReset(tm);
   }
   return program;
}

static void YglQuadOffset_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy, int cash_flg, YglTextureManager *tm);

void YglQuadOffset(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy, YglTextureManager *tm) {
  YglQuadOffset_in(input, output, c, cx, cy, sx, sy, 1, tm);
}

void YglCachedQuadOffset(vdp2draw_struct * input, YglCache * cache, int cx, int cy, float sx, float sy, YglTextureManager *tm) {
  YglQuadOffset_in(input, NULL, cache, cx, cy, sx, sy, 0, tm);
}

void YglQuadOffset_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy, int cash_flg, YglTextureManager *tm) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  int prg = PG_VDP2_NORMAL;
  float * pos;
  //float * vtxa;

  int vHeight;

  if (input->colornumber >= 3) {
    prg = PG_VDP2_NORMAL;
    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC;
    }
  }
  else {

    prg = PG_VDP2_NORMAL_CRAM;

    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC_CRAM;
    }
  }

  program = YglGetProgram((YglSprite*)input, prg,tm,input->priority);
  if (program == NULL) return;

  program->colornumber = input->colornumber;

  program->mosaic[0] = input->mosaicxmask;
  program->mosaic[1] = input->mosaicymask;

  vHeight = input->vertices[5] - input->vertices[1];

  pos = program->quads + program->currentQuad;

  pos[0] = (input->vertices[0] - cx) * sx;
  pos[1] = input->vertices[1] * sy;
  pos[2] = (input->vertices[2] - cx) * sx;
  pos[3] = input->vertices[3] * sy;
  pos[4] = (input->vertices[4] - cx) * sx;
  pos[5] = input->vertices[5] * sy;
  pos[6] = (input->vertices[0] - cx) * sx;
  pos[7] = (input->vertices[1]) * sy;
  pos[8] = (input->vertices[4] - cx)*sx;
  pos[9] = input->vertices[5] * sy;
  pos[10] = (input->vertices[6] - cx) * sx;
  pos[11] = input->vertices[7] * sy;

  // vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  // memset(vtxa,0,sizeof(float)*24);

  tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

  program->currentQuad += 12;
  if (output != NULL){
    YglTMAllocate(tm, output, input->cellw, input->cellh, &x, &y);
  }
  else{
    x = c->x;
    y = c->y;
  }

  tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0; // these can stay at 0

  /*
  0 +---+ 1
  |   |
  +---+ 2
  3 +---+
  |   |
  5 +---+ 4
  */

  if (input->flipfunction & 0x1) {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x + input->cellw) - ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x)+ATLAS_BIAS;
  }
  else {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x)+ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x + input->cellw) - ATLAS_BIAS;
  }
  if (input->flipfunction & 0x2) {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y + input->cellh - cy) - ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + input->cellh - (cy + vHeight)) + ATLAS_BIAS;
  }
  else {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y + cy) + ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + (cy + vHeight)) - ATLAS_BIAS;
  }

  if (c != NULL && cash_flg == 1)
  {
    c->x = x;
    c->y = y;
  }

  tmp[0].q = 1.0f;
  tmp[1].q = 1.0f;
  tmp[2].q = 1.0f;
  tmp[3].q = 1.0f;
  tmp[4].q = 1.0f;
  tmp[5].q = 1.0f;
}


static int YglQuad_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cash_flg, YglTextureManager *tm);

float * YglQuad(vdp2draw_struct * input, YglTexture * output, YglCache * c, YglTextureManager *tm){
  YglQuad_in(input, output, c, 1, tm);
  return 0;
}

void YglCachedQuad(vdp2draw_struct * input, YglCache * cache, YglTextureManager *tm){
  YglQuad_in(input, NULL, cache, 0, tm);
}

int YglQuad_in(vdp2draw_struct * input, YglTexture * output, YglCache * c, int cash_flg, YglTextureManager *tm) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  int prg;
  float * pos;
  //float * vtxa;

  if (input->colornumber >= 3) {
      prg = PG_VDP2_NORMAL;
      if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
        prg = PG_VDP2_MOSAIC;
      }
  } else {
      prg = PG_VDP2_NORMAL_CRAM;

      if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
        prg = PG_VDP2_MOSAIC_CRAM;
      }
  }

  program = YglGetProgram((YglSprite*)input, prg,tm,input->priority);
  if (program == NULL) return -1;

  program->colornumber = input->colornumber;

  program->mosaic[0] = input->mosaicxmask;
  program->mosaic[1] = input->mosaicymask;

  pos = program->quads + program->currentQuad;
  pos[0] = input->vertices[0];
  pos[1] = input->vertices[1];
  pos[2] = input->vertices[2];
  pos[3] = input->vertices[3];
  pos[4] = input->vertices[4];
  pos[5] = input->vertices[5];
  pos[6] = input->vertices[0];
  pos[7] = input->vertices[1];
  pos[8] = input->vertices[4];
  pos[9] = input->vertices[5];
  pos[10] = input->vertices[6];
  pos[11] = input->vertices[7];

  // vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  // memset(vtxa,0,sizeof(float)*24);

  tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));

  program->currentQuad += 12;

  if (output != NULL){
    YglTMAllocate(tm, output, input->cellw, input->cellh, &x, &y);
  }
  else{
    x = c->x;
    y = c->y;
  }



  tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0; // these can stay at 0

  /*
  0 +---+ 1
  |   |
  +---+ 2
  3 +---+
  |   |
  5 +---+ 4
  */

  if (input->flipfunction & 0x1) {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x + input->cellw) - ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x)+ATLAS_BIAS;
  }
  else {
    tmp[0].s = tmp[3].s = tmp[5].s = (float)(x)+ATLAS_BIAS;
    tmp[1].s = tmp[2].s = tmp[4].s = (float)(x + input->cellw) - ATLAS_BIAS;
  }
  if (input->flipfunction & 0x2) {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y + input->cellh) - ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y)+ATLAS_BIAS;
  }
  else {
    tmp[0].t = tmp[1].t = tmp[3].t = (float)(y)+ATLAS_BIAS;
    tmp[2].t = tmp[4].t = tmp[5].t = (float)(y + input->cellh) - ATLAS_BIAS;
  }

  if (c != NULL && cash_flg == 1)
  {
    switch (input->flipfunction) {
    case 0:
      c->x = *(program->textcoords + ((program->currentQuad - 12) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 12) * 2) + 1); // upper left coordinates(0)
      break;
    case 1:
      c->x = *(program->textcoords + ((program->currentQuad - 10) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 10) * 2) + 1); // upper left coordinates(0)
      break;
    case 2:
      c->x = *(program->textcoords + ((program->currentQuad - 2) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 2) * 2) + 1); // upper left coordinates(0)
      break;
    case 3:
      c->x = *(program->textcoords + ((program->currentQuad - 4) * 2));   // upper left coordinates(0)
      c->y = *(program->textcoords + ((program->currentQuad - 4) * 2) + 1); // upper left coordinates(0)
      break;
    }
  }

  tmp[0].q = 1.0f;
  tmp[1].q = 1.0f;
  tmp[2].q = 1.0f;
  tmp[3].q = 1.0f;
  tmp[4].q = 1.0f;
  tmp[5].q = 1.0f;

  return 0;
}


int YglQuadRbg0(RBGDrawInfo * rbg, YglTexture * output, YglCache * c, int rbg_type, YglTextureManager *tm, Vdp2 *varVdp2Regs) {
  unsigned int x, y;
  YglProgram *program;
  texturecoordinate_struct *tmp;
  vdp2draw_struct * input = &rbg->ctrl.info;
  int prg;
  float * pos;

  if(input->colornumber >= 3 ) {
    prg = PG_VDP2_NORMAL;
    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC;
    }
  }
  else {

    if (input->mosaicxmask != 1 || input->mosaicymask != 1) {
      prg = PG_VDP2_MOSAIC_CRAM;
    }
    else {
        prg = PG_VDP2_NORMAL_CRAM;
    }
  }

  program = YglGetProgram((YglSprite*)input, prg,tm,input->priority);
  if (program == NULL) return -1;

  program->colornumber = input->colornumber;

  program->mosaic[0] = input->mosaicxmask;
  program->mosaic[1] = input->mosaicymask;

  pos = program->quads + program->currentQuad;
  pos[0] = input->vertices[0];
  pos[1] = input->vertices[1];
  pos[2] = input->vertices[2];
  pos[3] = input->vertices[3];
  pos[4] = input->vertices[4];
  pos[5] = input->vertices[5];
  pos[6] = input->vertices[0];
  pos[7] = input->vertices[1];
  pos[8] = input->vertices[4];
  pos[9] = input->vertices[5];
  pos[10] = input->vertices[6];
  pos[11] = input->vertices[7];

  // vtxa = (program->vertexAttribute + (program->currentQuad * 2));
  // memset(vtxa,0,sizeof(float)*24);

// printf("(%f %f) (%f %f) (%f %f) (%f %f)\n", input->vertices[0],input->vertices[1],input->vertices[2],input->vertices[3],input->vertices[4],input->vertices[5],input->vertices[6],input->vertices[7]);
    if (varVdp2Regs == NULL) {
      printf("varVdp2Regs is NULL %d\n", __LINE__);
      abort();
    }

    if(rbg_type == 0 )
    program->interuput_texture = 1;
    else
    program->interuput_texture = 2;

    tmp = (texturecoordinate_struct *)(program->textcoords + (program->currentQuad * 2));
    program->currentQuad += 12;

    tmp[0].s = 0 + ATLAS_BIAS;
    tmp[0].t = (rbg->vres * rbg->ctrl.info.startLine)/yabsys.VBlankLineCount + ATLAS_BIAS;
    tmp[1].s = rbg->hres - ATLAS_BIAS;
    tmp[1].t = (rbg->vres * rbg->ctrl.info.startLine)/yabsys.VBlankLineCount + ATLAS_BIAS;
    tmp[2].s = rbg->hres - ATLAS_BIAS;
    tmp[2].t = (rbg->vres * rbg->ctrl.info.endLine)/yabsys.VBlankLineCount - ATLAS_BIAS;
    tmp[3].s = 0 + ATLAS_BIAS;
    tmp[3].t = (rbg->vres * rbg->ctrl.info.startLine)/yabsys.VBlankLineCount + ATLAS_BIAS;
    tmp[4].s = rbg->hres - ATLAS_BIAS;
    tmp[4].t = (rbg->vres * rbg->ctrl.info.endLine)/yabsys.VBlankLineCount - ATLAS_BIAS;
    tmp[5].s = 0 + ATLAS_BIAS;
    tmp[5].t = (rbg->vres * rbg->ctrl.info.endLine)/yabsys.VBlankLineCount - ATLAS_BIAS;

    // glActiveTexture(GL_TEXTURE0);
    // glBindTexture(GL_TEXTURE_2D, RBGGenerator_getTexture(program->interuput_texture));

    RBGGenerator_update(rbg, varVdp2Regs);

    tmp[0].r = tmp[1].r = tmp[2].r = tmp[3].r = tmp[4].r = tmp[5].r = 0;
    tmp[0].q = tmp[1].q = tmp[2].q = tmp[3].q = tmp[4].q = tmp[5].q = 0;
  return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void updateColorOffset(Vdp2 *varVdp2Regs) {
  if (varVdp2Regs->CLOFEN & 0x40)
  {
    // color offset enable
    if (varVdp2Regs->CLOFSL & 0x40)
    {
      // color offset B
      vdp1cor = varVdp2Regs->COBR & 0xFF;
      if (varVdp2Regs->COBR & 0x100)
        vdp1cor |= 0xFFFFFF00;
      vdp1cog = varVdp2Regs->COBG & 0xFF;
      if (varVdp2Regs->COBG & 0x100)
        vdp1cog |= 0xFFFFFF00;

      vdp1cob = varVdp2Regs->COBB & 0xFF;
      if (varVdp2Regs->COBB & 0x100)
        vdp1cob |= 0xFFFFFF00;
    }
    else
    {
      // color offset A
      vdp1cor = varVdp2Regs->COAR & 0xFF;
      if (varVdp2Regs->COAR & 0x100)
        vdp1cor |= 0xFFFFFF00;

      vdp1cog = varVdp2Regs->COAG & 0xFF;
      if (varVdp2Regs->COAG & 0x100)
        vdp1cog |= 0xFFFFFF00;

      vdp1cob = varVdp2Regs->COAB & 0xFF;
      if (varVdp2Regs->COAB & 0x100)
        vdp1cob |= 0xFFFFFF00;
    }
  }
  else // color offset disable
    vdp1cor = vdp1cog = vdp1cob = 0;
}

u8 * YglGetVDP2RegPointer(){
  int error;
  if (_Ygl->vdp2reg_tex == 0){
    glGenTextures(1, &_Ygl->vdp2reg_tex);

    glGenBuffers(1, &_Ygl->vdp2reg_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp2reg_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * NB_VDP2_REG, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, NB_VDP2_REG, 512, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp2reg_pbo);
  _Ygl->vdp2reg_buf = (u8 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * NB_VDP2_REG, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  return (u8*)_Ygl->vdp2reg_buf;
}

void YglSetVDP2Reg(u8 * pbuf, int start, int size){
  glBindTexture(GL_TEXTURE_2D, _Ygl->vdp2reg_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->vdp2reg_pbo);
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glTexSubImage2D(GL_TEXTURE_2D, 0, NB_VDP2_REG * start, 0, NB_VDP2_REG, size, GL_RED, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  _Ygl->vdp2reg_buf = NULL;
  glBindTexture(GL_TEXTURE_2D, 0 );
  return;
}

void YglUpdateVdp2Reg() {
  int needupdate = 0;
  int size = sizeof(char);
  int step = (_Ygl->interlace == DOUBLE_INTERLACE)?2:1;
  for (int i = 0; i<_Ygl->rheight; i++) {
    Vdp2 *varVdp2Regs = &Vdp2Lines[i/step];
    u8 bufline[NB_VDP2_REG*4] = {0};
    updateColorOffset(varVdp2Regs);

    bufline[S0CCRT*size] = (0x1F - ((varVdp2Regs->CCRSA >> 0) & 0x1F));
    bufline[S1CCRT*size] = (0x1F - ((varVdp2Regs->CCRSA >> 8) & 0x1F));
    bufline[S2CCRT*size] = (0x1F - ((varVdp2Regs->CCRSB >> 0) & 0x1F));
    bufline[S3CCRT*size] = (0x1F - ((varVdp2Regs->CCRSB >> 8) & 0x1F));
    bufline[S4CCRT*size] = (0x1F - ((varVdp2Regs->CCRSC >> 0) & 0x1F));
    bufline[S5CCRT*size] = (0x1F - ((varVdp2Regs->CCRSC >> 8) & 0x1F));
    bufline[S6CCRT*size] = (0x1F - ((varVdp2Regs->CCRSD >> 0) & 0x1F));
    bufline[S7CCRT*size] = (0x1F - ((varVdp2Regs->CCRSD >> 8) & 0x1F));
    bufline[S0PRI*size] = ((varVdp2Regs->PRISA >> 0) & 0x7);
    bufline[S1PRI*size] = ((varVdp2Regs->PRISA >> 8) & 0x7);
    bufline[S2PRI*size] = ((varVdp2Regs->PRISB >> 0) & 0x7);
    bufline[S3PRI*size] = ((varVdp2Regs->PRISB >> 8) & 0x7);
    bufline[S4PRI*size] = ((varVdp2Regs->PRISC >> 0) & 0x7);
    bufline[S5PRI*size] = ((varVdp2Regs->PRISC >> 8) & 0x7);
    bufline[S6PRI*size] = ((varVdp2Regs->PRISD >> 0) & 0x7);
    bufline[S7PRI*size] = ((varVdp2Regs->PRISD >> 8) & 0x7);
    bufline[SPCC*size] = ((varVdp2Regs->SPCTL >> 8) & 0x07);
    bufline[VDP1COR*size] = vdp1cor & 0xFF;
    bufline[VDP1COG*size] = vdp1cog & 0xFF;
    bufline[VDP1COB*size] = vdp1cob & 0xFF;
    bufline[VDP1CORS*size] = (vdp1cor >> 8) & 0xFF;
    bufline[VDP1COGS*size] = (vdp1cog >> 8) & 0xFF;
    bufline[VDP1COBS*size] = (vdp1cob >> 8) & 0xFF;
    bufline[CRAOFB*size] = ((varVdp2Regs->CRAOFB>>4) & 0x7);

    if (memcmp(bufline, &_Ygl->vdp2buf[i*NB_VDP2_REG*size], NB_VDP2_REG*size) != 0){
      needupdate = 1;
      memcpy(&_Ygl->vdp2buf[i*NB_VDP2_REG*size], bufline, NB_VDP2_REG*size);
    }
  }
  if (needupdate) {
      u8 * pbuf = YglGetVDP2RegPointer();
      memcpy(pbuf, _Ygl->vdp2buf, _Ygl->rheight*size*NB_VDP2_REG);
      YglSetVDP2Reg(pbuf, 0, _Ygl->rheight);
      needupdate = 0;
  }
}

SpriteMode getSpriteRenderMode(Vdp2* varVdp2Regs) {
  SpriteMode ret = NONE;
  // if ((Vdp1Regs->TVMR & 0x1) == 1) return NONE;
  if (varVdp2Regs->CCCTL & (1<<6)) {
    if (((varVdp2Regs->CCCTL>>8)&0x1) == 0x1) {
      ret = AS_IS;
    } else {
      if (((varVdp2Regs->CCCTL >> 9) & 0x01) == 0x01 ) {
        ret = DST_ALPHA;
      } else {
        ret = SRC_ALPHA;
      }
    }
  }

  return ret;
}

void YglSetClearColor(float r, float g, float b){
  _Ygl->clear[0] = r;
  _Ygl->clear[1] = g;
  _Ygl->clear[2] = b;
  _Ygl->clear[3] = (float)(0xF8|NONE)/255.0f;
  _Ygl->last_back_color[0] = r;
  _Ygl->last_back_color[1] = g;
  _Ygl->last_back_color[2] = b;
  _Ygl->last_back_color[3] = 1.0;
}

void YglCheckFBSwitch(int sync) {
  GLenum ret = GL_WAIT_FAILED;
  if (_Ygl->sync == 0) return;
  ret = glClientWaitSync(_Ygl->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
  if (sync != 0) {
    int end = 0;
    while (end == 0) {
     ret = glClientWaitSync(_Ygl->sync, GL_SYNC_FLUSH_COMMANDS_BIT, 20000000);
     if ((ret == GL_CONDITION_SATISFIED) || (ret == GL_ALREADY_SIGNALED)) end = 1;
    }
  }
  if ((ret == GL_CONDITION_SATISFIED) || (ret == GL_ALREADY_SIGNALED)) {
    glDeleteSync(_Ygl->sync);
    _Ygl->sync = 0;
    YuiTimedSwapBuffers();
  }
}

int DrawVDP2Screen(Vdp2 *varVdp2Regs, int id) {
  YglLevel * level;
  int cprg = -1;
  int clear = 1;
  int ret = 0;
  float col[4] = {0.0f,0.0f,0.0f,0.0f};

  level = &_Ygl->vdp2levels[id];

  if (level->prgcurrent == 0) return 0;

  for (int j = 0; j < (level->prgcurrent + 1); j++)
  {
    if (level->prg[j].currentQuad != 0) {
      if (clear) glClearBufferfv(GL_COLOR, 0, col);
      clear = 0;
      glActiveTexture(GL_TEXTURE0);
      if (level->prg[j].interuput_texture == 0)
        glBindTexture(GL_TEXTURE_2D, YglTM_vdp2->textureID);
      else
        glBindTexture(GL_TEXTURE_2D, RBGGenerator_getTexture(level->prg[j].interuput_texture));
      ret = 1;

      if (level->prg[j].prgid != cprg)
      {
        cprg = level->prg[j].prgid;
        glUseProgram(level->prg[j].prg);
      }
      if (level->prg[j].setupUniform)
      {
        level->prg[j].setupUniform((void*)&level->prg[j], YglTM_vdp2, varVdp2Regs, id);
      }

      glUniformMatrix4fv(level->prg[j].mtxModelView, 1, GL_FALSE, (GLfloat*)&_Ygl->rbgModelView.m[0][0]);
      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->quads_buf);
      glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float), level->prg[j].quads, GL_STREAM_DRAW);
      glVertexAttribPointer(level->prg[j].vertexp, 2, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(level->prg[j].vertexp);

      glBindBuffer(GL_ARRAY_BUFFER, _Ygl->textcoords_buf);
      glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float) * 2, level->prg[j].textcoords, GL_STREAM_DRAW);
      glVertexAttribPointer(level->prg[j].texcoordp, 4, GL_FLOAT, GL_FALSE, 0, 0);
      glEnableVertexAttribArray(level->prg[j].texcoordp);

      if (level->prg[j].vaid > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, _Ygl->vertexAttribute_buf);
        glBufferData(GL_ARRAY_BUFFER, level->prg[j].currentQuad * sizeof(float) * 2, level->prg[j].vertexAttribute, GL_STREAM_DRAW);
        glVertexAttribPointer(level->prg[j].vaid, 4, GL_FLOAT, GL_FALSE, 0, 0);
        glEnableVertexAttribArray(level->prg[j].vaid);
      }

      glDrawArrays(GL_TRIANGLES, 0, level->prg[j].currentQuad / 2);

      if (level->prg[j].cleanupUniform)
      {
        level->prg[j].matrix = (GLfloat*)&_Ygl->rbgModelView.m[0][0];
        level->prg[j].cleanupUniform((void*)&level->prg[j], YglTM_vdp2);
      }
    }
    level->prg[j].currentQuad = 0;
  }
  level->prgcurrent = 0;
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, 0);
  return ret;
}
int setupBlur(Vdp2 *varVdp2Regs, int layer) {
  int val = (varVdp2Regs->CCCTL & 0xF000) >> 12;
  if (Vdp2Internal.ColorMode != 0) return 0;
  if ((val & 0x8) == 0) return 0;
  switch (layer) {
    case RBG0:
      return ((val & 0x7) == 1);
    break;
    case RBG1:
      return ((val & 0x7) == 2);
    break;
    case NBG0:
      return ((val & 0x7) == 2);
    break;
    case NBG1:
      return ((val & 0x7) == 4);
    break;
    case NBG2:
      return ((val & 0x7) == 5);
    break;
    case NBG3:
      return ((val & 0x7) == 6);
    break;
    case SPRITE:
      return ((val & 0x7) == 0);
    break;
    default:
      return 0;
  }
  return 0;
}

int setupColorMode(Vdp2 *varVdp2Regs, int layer) {
  //Return 1 if color format is RGB / 0 otherwise
  switch (layer) {
    case NBG0:
    case RBG1:
       return ((varVdp2Regs->CHCTLA >> 4)&0x7) > 2;
    break;
    case NBG1:
       return ((varVdp2Regs->CHCTLA >> 12)&0x4) > 2;
    break;
    case NBG2:
    case NBG3:
       //Always in palette mode
       return 0;
    break;
    case RBG0:
       return ((varVdp2Regs->CHCTLB >> 12)&0x7) > 2;
    default:
       return 0;
  }
  return 0;
}

int setupShadow(Vdp2 *varVdp2Regs, int layer) {
  //Return 1 if color format is RGB / 0 otherwise
  switch (layer) {
    case NBG0:
    case RBG1:
       return ((varVdp2Regs->SDCTL)&0x1);
    break;
    case NBG1:
       return ((varVdp2Regs->SDCTL >> 1)&0x1);
    break;
    case NBG2:
        return ((varVdp2Regs->SDCTL >> 2)&0x1);
    break;
    case NBG3:
        return ((varVdp2Regs->SDCTL >> 3)&0x1);
    break;
    case RBG0:
        return ((varVdp2Regs->SDCTL >> 4)&0x1);
    break;
    case SPRITE:
        return ((varVdp2Regs->SDCTL >> 5)&0x1);
    break;
    default:
       return 0;
  }
  return 0;
}

SpriteMode setupBlend(Vdp2 *varVdp2Regs, int layer) {
  SpriteMode ret = NONE;

  const int enableBit[7] = {0, 1, 2, 3, 4, 0, 6};
  if ((layer == 6) && ((Vdp1Regs->TVMR & 0x1) == 1)) return NONE;
  if (varVdp2Regs->CCCTL & (1<<enableBit[layer])) {
    if (((varVdp2Regs->CCCTL>>8)&0x1) == 0x1) {
      ret = AS_IS;
      YGLDEBUG("Layer %d as_is\n", layer);
    } else {
      //Add as calculation rate
      if (((varVdp2Regs->CCCTL >> 9) & 0x01) == 0x01 ) {
        YGLDEBUG("Layer %d src_alpha\n", layer);
        ret = DST_ALPHA;
      } else {
        YGLDEBUG("Layer %d dst_alpha\n", layer);
        ret = SRC_ALPHA;
      }
    }
  }
  return ret;
}
//////////////////////////////////////////////////////////////////////////////

void YglReset(YglLevel level) {
  unsigned int i,j;
  level.prgcurrent = 0;
  level.uclipcurrent = 0;
  level.ux1 = 0;
  level.uy1 = 0;
  level.ux2 = 0;
  level.uy2 = 0;
  for( j=0; j< level.prgcount; j++ )
  {
    level.prg[j].currentQuad = 0;
  }
}

//////////////////////////////////////////////////////////////////////////////

void YglShowTexture(void) {
   _Ygl->st = !_Ygl->st;
}

u32 * YglGetColorRamPointer(int line) {
  return &_Ygl->cram_tex_buf[2048*line];
}

void syncVDP2ColorLine(int line) {
  //Force a dedicate dline to sync with the current VDP2ColorRam and set it as reference for the other lines
  _Ygl->colorRamIndex = line;
  _Ygl->ColorRamNeedSync = 1;
  u32 *buf;

  buf = YglGetColorRamPointer(line);
  if (buf == NULL) {
    return;
  }
  for (int addr = 0x0; addr < 0x1000; addr+=2) {
    switch (Vdp2Internal.ColorMode)
    {
      case 0:
      case 1:
      {
        u16 tmp;
        u32 alpha = 0;
        tmp = T2ReadWord(Vdp2ColorRam, addr);
        if (tmp & 0x8000) alpha = 0xF8;
        buf[(addr >> 1) & 0x7FF] = SAT2YAB1(alpha, tmp);
        break;
      }
      case 2:
      {
        u32 tmp1 = T2ReadWord(Vdp2ColorRam, (addr&0xFFC));
        u32 tmp2 = T2ReadWord(Vdp2ColorRam, (addr&0xFFC)+2);
        u32 alpha = 0;
        if (tmp1 & 0x8000) alpha = 0xF8;
        buf[(addr >> 2) & 0x7FF] = SAT2YAB2(alpha, tmp1, tmp2);
        break;
      }
      default:
      break;
    }
  }
  glBindTexture(GL_TEXTURE_2D, _Ygl->cram_tex);
  glTexSubImage2D(GL_TEXTURE_2D,
      0,
      0x0, line,
      0x800, 1,
      GL_RGBA, GL_UNSIGNED_BYTE,
      &buf[0] );
}

u32 * YglGetLineColorScreenPointer(){
  int error;
  if (_Ygl->linecolorscreen_tex == 0){
    glGenTextures(1, &_Ygl->linecolorscreen_tex);

    glGenBuffers(1, &_Ygl->linecolorscreen_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolorscreen_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * 4, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorscreen_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorscreen_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolorscreen_pbo);
  _Ygl->linecolorscreen_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * 4, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  return _Ygl->linecolorscreen_buf;
}

void YglSetLineColorScreen(u32 * pbuf, int size){

  glBindTexture(GL_TEXTURE_2D, _Ygl->linecolorscreen_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->linecolorscreen_pbo);
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  _Ygl->linecolorscreen_buf = NULL;
  glBindTexture(GL_TEXTURE_2D, 0 );
  return;
}

//////////////////////////////////////////////////////////////////////////////

void YglSetWindow(int id) {
  if (_Ygl->window_tex[0] == 0) {
    glGenTextures(2, &_Ygl->window_tex[0]);

    glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  glBindTexture(GL_TEXTURE_2D, _Ygl->window_tex[id]);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RGBA, GL_UNSIGNED_BYTE, _Ygl->win[id] );
  glBindTexture(GL_TEXTURE_2D, 0);
  return;
}

//////////////////////////////////////////////////////////////////////////////
u32* YglGetBackColorPointer() {
  int status;
  GLuint error;

  YGLDEBUG("YglGetBackColorPointer: %d,%d", _Ygl->width, _Ygl->height);


  if (_Ygl->back_tex == 0) {
    glGenTextures(1, &_Ygl->back_tex);

    glGenBuffers(1, &_Ygl->back_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->back_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * 4, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
#if 0
    if( _Ygl->backcolor_buf == NULL ){
        _Ygl->backcolor_buf = malloc(512 * 4);
    }
#else
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->back_pbo);
  if( _Ygl->backcolor_buf != NULL ){
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  }
  _Ygl->backcolor_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * 4, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
#endif

  return _Ygl->backcolor_buf;
}

void YglSetBackColor(int size) {
  glBindTexture(GL_TEXTURE_2D, _Ygl->back_tex);
#if 0
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, _Ygl->backcolor_buf);
#else
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->back_pbo);
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  _Ygl->backcolor_buf = NULL;
#endif
  glBindTexture(GL_TEXTURE_2D, 0);
  return;
}
void setupMaxSize() {
  int oldWidth = _Ygl->width;
  int oldHeight = _Ygl->height;

  if ((_Ygl->rwidth != 0) && (_Ygl->width > GlWidth)) _Ygl->width = _Ygl->rwidth * (GlWidth/_Ygl->rwidth + 1);
  if ((_Ygl->rheight != 0) && (_Ygl->height > GlHeight)) _Ygl->height = _Ygl->rheight * (GlHeight/_Ygl->rheight + 1);
  if (oldWidth != _Ygl->width) rebuild_frame_buffer = 1;
  if (oldHeight != _Ygl->height) rebuild_frame_buffer = 1;
}
//////////////////////////////////////////////////////////////////////////////

void YglChangeResolution(int w, int h) {
  YglLoadIdentity(&_Ygl->rbgModelView);
  float ratio = (float)w/(float)h;

  if (_Ygl->interlace == DOUBLE_INTERLACE) {
    //Single interlace So we need Twice number of lines
    h *= 2;
  }

  int scale = 1;
  int scaleLimit = 1;
  int upHeight = 4096;
  int uh = h;
  int uw = w;
  int maxRes = GlHeight;

  if ((GlHeight * uw) > (GlWidth * uh)) {
    maxRes = GlWidth * uh / uw;
  }
  scaleLimit = (int)(floor(maxRes/(float)uh))&~0x1;
  if (_Ygl->interlace == NORMAL_INTERLACE) scaleLimit>>=1;
  if (scaleLimit == 0)scaleLimit = 1;
  // printf("Request resolution %d %d (%d) [%d %d]\n", _Ygl->resolution_mode, uh, h, uw, w);
  switch (_Ygl->resolution_mode) {
    break;
    case RES_2X: //1080p
    scale = 2; //floor(540.0/(float)uh);
    break;
    case RES_4X: //2160
    scale = 4; //floor(1080/(float)uh);
    break;
    case RES_NATIVE: //Native
    scale = scaleLimit;
    break;
    case RES_ORIGINAL:
    case RES_SD:
    case RES_1X: //720p
    default:
    scale = 1; //floor(360.0/(float)uh);
  }
  if (scale == 0){
    scale = 1;
  };
  // if (scale > scaleLimit) {
  //   scale = scaleLimit;
  // }
  _Ygl->rwidth = w;
  _Ygl->rheight = h;
  _Ygl->height = uh * scale;
  _Ygl->width = uw * scale;

  YGLDEBUG("YglChangeResolution %dx%d => %d => %dx%d (%.1f,%.1f) (%d %d)\n",w,h, scale, _Ygl->width, _Ygl->height,_Ygl->vdp2wdensity,_Ygl->vdp2hdensity, uw, uh);

  // Texture size for vdp1
  _Ygl->vdp1width = 512*scale*_Ygl->vdp1wdensity;
  _Ygl->vdp1height = 256*scale*_Ygl->vdp1hdensity;

  //upscale Ratio of effective original vdp1FB
  if ((float) scale != _Ygl->vdp1ratio) {
    _Ygl->vdp1ratio = (float)scale;
    if (VIDCore->SetupVdp1Scale) VIDCore->SetupVdp1Scale(scale);
  }

  rebuild_frame_buffer = 1;

  //Effective vdp2 upscale ratio
  _Ygl->widthRatio = (float)_Ygl->width/(float)_Ygl->rwidth;
  _Ygl->heightRatio = (float)_Ygl->height/(float)_Ygl->rheight;

  if (_Ygl->rwidth >= 640) _Ygl->widthRatio *= 2.0f;
  YglOrtho(&_Ygl->rbgModelView, 0.0f, (float)_Ygl->rwidth, (float)_Ygl->rheight, 0.0f, 10.0f, 0.0f);
}

void VIDCSSync(){
}

///////////////////////////////////////////////////////////////////////////////
// Per line operation
u32 * YglGetPerlineBuf(void){
  int error;
  if (_Ygl->coloroffset_tex == 0){
    glGenTextures(1, &_Ygl->coloroffset_tex);

    glGenBuffers(1, &_Ygl->coloroffset_pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->coloroffset_pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 512 * 4 * 8, NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    glBindTexture(GL_TEXTURE_2D, _Ygl->coloroffset_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glBindTexture(GL_TEXTURE_2D, _Ygl->coloroffset_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->coloroffset_pbo);
  _Ygl->coloroffset_buf = (u32 *)glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, 512 * 4 * 8, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT );
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

  return &_Ygl->coloroffset_buf[0];
}

void YglSetPerlineBuf(u32 * pbuf){

  glBindTexture(GL_TEXTURE_2D, _Ygl->coloroffset_tex);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, _Ygl->coloroffset_pbo);
  glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 8, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  _Ygl->coloroffset_buf = NULL;
  glBindTexture(GL_TEXTURE_2D, 0);
  return;
}
