/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2006 Theo Berkau

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

#ifndef VDP1_H
#define VDP1_H

#include "memory.h"
#include "vdp2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDCORE_DEFAULT         -1

#define CMD_QUEUE_SIZE 2048

//#define YAB_ASYNC_RENDERING 1

typedef struct {
   u16 TVMR;
   u16 FBCR;
   u16 PTMR;
   u16 EWDR;
   u16 EWLR;
   u16 EWRR;
   u16 ENDR;
   u16 EDSR;
   u16 LOPR;
   u16 COPR;
   u16 MODR;

   u16 lCOPR;

   u32 addr;

   s16 localX;
   s16 localY;

   u16 systemclipX1;
   u16 systemclipY1;
   u16 systemclipX2;
   u16 systemclipY2;

   u16 userclipX1;
   u16 userclipY1;
   u16 userclipX2;
   u16 userclipY2;


} Vdp1;

// struct for Vdp1 part that shouldn't be saved
typedef struct {
   int disptoggle;
   int manualerase;
   int manualchange;
   int onecycleerase;
   int onecyclechange;
   int useVBlankErase;
   int current_frame;
   int updateVdp1Ram;
   int checkEDSR;
   int status;
} Vdp1External_struct;

extern Vdp1External_struct Vdp1External;

typedef enum {
  VDPCT_NORMAL_SPRITE = 0,
  VDPCT_SCALED_SPRITE = 1,
  VDPCT_DISTORTED_SPRITE = 2,
  VDPCT_DISTORTED_SPRITEN = 3,
  VDPCT_POLYGON = 4,
  VDPCT_POLYLINE = 5,
  VDPCT_LINE = 6,
  VDPCT_POLYLINEN = 7,
  VDPCT_USER_CLIPPING_COORDINATES = 8,
  VDPCT_SYSTEM_CLIPPING_COORDINATES = 9,
  VDPCT_LOCAL_COORDINATES = 10,
  VDPCT_USER_CLIPPING_COORDINATESN = 11,

  VDPCT_INVALID = 12,
  VDPCT_DRAW_END

} Vdp1CommandType;

typedef struct
{
  u32 CMDCTRL;
  u32 CMDLINK;
  u32 CMDPMOD;
  u32 CMDCOLR;
  u32 CMDSRCA;
  u32 CMDSIZE;
  s32 CMDXA;
  s32 CMDYA;
  s32 CMDXB;
  s32 CMDYB;
  s32 CMDXC;
  s32 CMDYC;
  s32 CMDXD;
  s32 CMDYD;
  u32 CMDGRDA;
  u32 w;
  u32 h;
  u32 flip;
  u32 type;
  u32 nbStep;
  float uAstepx;
  float uAstepy;
  float uBstepx;
  float uBstepy;
  float G[12];
} vdp1cmd_struct;

typedef struct
{
   int id;
   const char *Name;
   int (*Init)(void);
   void (*DeInit)(void);
   void (*Resize)(int,int,unsigned int, unsigned int, int);
   void (*getScale)(float *xRatio, float *yRatio, int *xUp, int *yUp);
   int (*IsFullscreen)(void);
   // VDP1 specific
   int (*Vdp1Reset)(void);
   void (*Vdp1Draw)();
   void(*Vdp1NormalSpriteDraw)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1ScaledSpriteDraw)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1DistortedSpriteDraw)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1PolygonDraw)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1PolylineDraw)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1LineDraw)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1UserClipping)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1SystemClipping)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   void(*Vdp1LocalCoordinate)(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs);
   int(*Vdp1EraseWrite)(int id);
   void(*Vdp1FrameChange)(void);
   void(*Vdp1RegenerateCmd)(vdp1cmd_struct* cmd);
   // VDP2 specific
   int (*Vdp2Reset)(void);
   void (*Vdp2Draw)(void);
   void (*GetGlSize)(int *width, int *height);
   void (*SetSettingValue)(int type, int value);
   void(*Sync)();
   void(*Vdp2DispOff)(void);
   void (*composeFB)(Vdp2 *regs);
   void (*composeVDP1)(void);
   int (*setupFrame)();
   void (*FinsihDraw)(void);
   void (*Vdp1FBDraw)(void);
   pixel_t* (*getVdp2ScreenExtract)(u32 screen, int * w, int * h);
   void (*SetupVdp1Scale)(int scale);
   void (*startVdp1Render)(void);
   void (*endVdp1Render)(void);
} VideoInterface_struct;

extern VideoInterface_struct *VIDCore;

extern u8 * Vdp1Ram;
extern int vdp1Ram_update_start;
extern int vdp1Ram_update_end;

u8 FASTCALL	Vdp1RamReadByte(SH2_struct *context, u8*, u32);
u16 FASTCALL	Vdp1RamReadWord(SH2_struct *context, u8*, u32);
u32 FASTCALL	Vdp1RamReadLong(SH2_struct *context, u8*, u32);
void FASTCALL	Vdp1RamWriteByte(SH2_struct *context, u8*, u32, u8);
void FASTCALL	Vdp1RamWriteWord(SH2_struct *context, u8*, u32, u16);
void FASTCALL	Vdp1RamWriteLong(SH2_struct *context, u8*, u32, u32);
u8 FASTCALL Vdp1FrameBuffer16bReadByte(SH2_struct *context, u8*, u32);
u16 FASTCALL Vdp1FrameBuffer16bReadWord(SH2_struct *context, u8*, u32);
u32 FASTCALL Vdp1FrameBuffer16bReadLong(SH2_struct *context, u8*, u32);
void FASTCALL Vdp1FrameBuffer16bWriteByte(SH2_struct *context, u8*, u32, u8);
void FASTCALL Vdp1FrameBuffer16bWriteWord(SH2_struct *context, u8*, u32, u16);
void FASTCALL Vdp1FrameBuffer16bWriteLong(SH2_struct *context, u8*, u32, u32);
u8 FASTCALL Vdp1FrameBuffer8bReadByte(SH2_struct *context, u8*, u32);
u16 FASTCALL Vdp1FrameBuffer8bReadWord(SH2_struct *context, u8*, u32);
u32 FASTCALL Vdp1FrameBuffer8bReadLong(SH2_struct *context, u8*, u32);
void FASTCALL Vdp1FrameBuffer8bWriteByte(SH2_struct *context, u8*, u32, u8);
void FASTCALL Vdp1FrameBuffer8bWriteWord(SH2_struct *context, u8*, u32, u16);
void FASTCALL Vdp1FrameBuffer8bWriteLong(SH2_struct *context, u8*, u32, u32);

extern void Vdp1SetRaster(int is352);

void Vdp1DrawCommands(u8 * ram, Vdp1 * regs);
void Vdp1FakeDrawCommands(u8 * ram, Vdp1 * regs);

extern Vdp1 * Vdp1Regs;

enum VDP1STATUS {
  VDP1_STATUS_IDLE = 0x0,
  VDP1_STATUS_RUNNING = 0x1,
  VDP1_STATUS_MASK = 0x1,
  VDP1_SWITCHING = 0x2,
  VDP1_SWITCH_REQUEST = 0x4
};

int Vdp1Init(void);
void Vdp1DeInit(void);
int VideoInit(int coreid);
int VideoChangeCore(int coreid);
void VideoDeInit(void);
void Vdp1Reset(void);
int VideoSetSetting(int type, int value);

u8 FASTCALL	Vdp1ReadByte(SH2_struct *context, u8*, u32);
u16 FASTCALL	Vdp1ReadWord(SH2_struct *context, u8*, u32);
u32 FASTCALL	Vdp1ReadLong(SH2_struct *context, u8*, u32);
void FASTCALL	Vdp1WriteByte(SH2_struct *context, u8*, u32, u8);
void FASTCALL	Vdp1WriteWord(SH2_struct *context, u8*, u32, u16);
void FASTCALL	Vdp1WriteLong(SH2_struct *context, u8*, u32, u32);

int Vdp1SaveState(void ** stream);
int Vdp1LoadState(const void * stream, int version, int size);

u32 Vdp1DebugGetCommandAddr(u32 number);
char *Vdp1DebugGetCommandNumberName(u32 addr);
char *Vdp1DebugGetCommandRaw(u32 addr);
Vdp1CommandType Vdp1DebugGetCommandType(u32 number);
void Vdp1DebugCommand(u32 number, char *outstring);
u32 *Vdp1DebugTexture(u32 number, int *w, int *h);
u8 *Vdp1DebugRawTexture(u32 number, int *w, int *h, int *numBytes);
void ToggleVDP1(void);

void Vdp1HBlankIN(void);
void Vdp1StartVisibleLine(void);
void Vdp1VBlankOUT(void);
void Vdp1VBlankIN(void);
void Vdp1VBlankIN_It(void);
void Vdp1SwitchFrame(void);

#ifdef __cplusplus
}
#endif

#endif
