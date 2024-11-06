/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004 Lawrence Sebald
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

/*! \file vdp1.c
    \brief VDP1 emulation functions.
*/


#include <stdlib.h>
#include <math.h>
#include "yabause.h"
#include "vdp1.h"
#include "debug.h"
#include "scu.h"
#include "vdp2.h"
#include "threads.h"
#include "sh2core.h"
#include "ygl.h"
#include "yui.h"

// #define DEBUG_CMD_LIST
// #define FRAMELOG printf
#define FRAMELOG_CMD //printf
#define PRINT_FB //printf

u8 * Vdp1Ram;
int vdp1Ram_update_start;
int vdp1Ram_update_end;
int VDP1_MASK = 0xFFFF;

extern u32* getVDP1WriteFramebuffer(int frame);
extern u32* getVDP1ReadFramebuffer();
extern void updateVdp1DrawingFBMem(int frame);
extern void YglGenerate();
extern void syncVdp1FBBuffer(u32 addr);

static int getVdp1CyclesPerLine(void);

VideoInterface_struct *VIDCore=NULL;
extern VideoInterface_struct *VIDCoreList[];

Vdp1 * Vdp1Regs;
Vdp1External_struct Vdp1External;

int vdp1_clock = 0;

static int nbCmdToProcess = 0;
static int CmdListInLoop = 0;

static int needVdp1draw = 0;
static int oldNeedVdp1draw = 0;
static void Vdp1NoDraw(void);
static int Vdp1Draw(void);
static void FASTCALL Vdp1ReadCommand(vdp1cmd_struct *cmd, u32 addr, u8* ram);

extern void addVdp1Framecount ();

static void checkFBSync();

#define DEBUG_BAD_COORD //YuiMsg

int CONVERTCMD(s32 *A) {
  s32 toto = (*A);
  if ((((*A)>>12)&0x1)^(((*A)>>11)&0x1) != 0) {
    return 1;
  }
  if (((*A)>>11)&0x1) (*A) |= 0xF800;
  else (*A) &= ~0xF800;
  ((*A) = (s32)(s16)(*A));
  if (((*A)) < -2048) {
    DEBUG_BAD_COORD("Bad(-2048) %x (%d, 0x%x)\n", (*A), (*A), toto);
    return 1;
  }
  if (((*A)) > 2047) {
    DEBUG_BAD_COORD("Bad(2047) %x (%d, 0x%x)\n", (*A), (*A), toto);
    return 1;
  }
  return 0;
}

static void RequestVdp1ToDraw() {
  if (needVdp1draw == 0){
    needVdp1draw = 1;
    CmdListInLoop = 0;
  }
}


static void abortVdp1() {
  if ((Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_RUNNING) {
    FRAMELOG("Aborting VDP1 %d\n", yabsys.LineCount);
    // The vdp1 is still running and a new draw command request has been received
    // Abort the current command list
    Vdp1External.status &= ~VDP1_STATUS_MASK;
    Vdp1External.status |= VDP1_STATUS_IDLE;
    endVdp1Render();
    CmdListInLoop = 0;
    vdp1_clock = 0;
    nbCmdToProcess = 0;
    needVdp1draw = 0;
  }
}
//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp1RamReadByte(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x7FFFF;
   return T1ReadByte(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp1RamReadWord(SH2_struct *context, u8* mem, u32 addr) {
    addr &= 0x07FFFF;
    return T1ReadWord(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp1RamReadLong(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x7FFFF;
   return T1ReadLong(mem, addr);
}

//////////////////////////////////////////////////////////////////////////////

static int Vdp1LoopAddr = -1;

void FASTCALL Vdp1RamWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
   addr &= 0x7FFFF;
   // printf("Write 0x%x @ 0x%x (%d %d)\n", val, addr, yabsys.LineCount, yabsys.DecilineCount);
   Vdp1External.updateVdp1Ram = 1;
   if( (Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_RUNNING) vdp1_clock -= 1;
   if (vdp1Ram_update_start > addr) vdp1Ram_update_start = addr;
   if (vdp1Ram_update_end < addr+1) vdp1Ram_update_end = addr + 1;
   T1WriteByte(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1RamWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
   addr &= 0x7FFFF;
   // printf("Write 0x%x @ 0x%x (%d %d)\n", val, addr, yabsys.LineCount, yabsys.DecilineCount);
   Vdp1External.updateVdp1Ram = 1;
   if( (Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_RUNNING) vdp1_clock -= 2;
   if (vdp1Ram_update_start > addr) vdp1Ram_update_start = addr;
   if (vdp1Ram_update_end < addr+2) vdp1Ram_update_end = addr + 2;
   T1WriteWord(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1RamWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
   addr &= 0x7FFFF;
   // printf("Write 0x%x @ 0x%x (%d %d)\n", val, addr, yabsys.LineCount, yabsys.DecilineCount);
   Vdp1External.updateVdp1Ram = 1;
   if( (Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_RUNNING) vdp1_clock -= 4;
   if (vdp1Ram_update_start > addr) vdp1Ram_update_start = addr;
   if (vdp1Ram_update_end < addr+4) vdp1Ram_update_end = addr + 4;
   T1WriteLong(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp1FrameBuffer16bReadByte(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr>>1;
   if (pixIdx/512 >= 256) return 0;
   u32* buf = getVDP1ReadFramebuffer();
   vdp1_clock -= 2;
   if (context != NULL) context->cycles += 2;
   PRINT_FB("R B 0x%x@0x%x\n", buf[pixIdx]&0xFF,addr);
   //take care of half pixel.
   return T1ReadLong((u8*)buf, pixIdx*4) & 0xFF;
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp1FrameBuffer16bReadWord(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr>>1;
   if (pixIdx/512 >= 256) return 0;
   u32* buf = getVDP1ReadFramebuffer();
   vdp1_clock -= 2;
   if (context != NULL) context->cycles += 2;
   PRINT_FB("R W 0x%x@0x%x (%d, %d)\n", T1ReadLong((u8*)buf, pixIdx*4) & 0xFFFF, addr, yabsys.LineCount, yabsys.DecilineCount);
   return T1ReadLong((u8*)buf, pixIdx*4) & 0xFFFF;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp1FrameBuffer16bReadLong(SH2_struct *context, u8* mem, u32 addr) {
   PRINT_FB("FB R L 16b @%x\n", addr);
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr>>1;
   if (pixIdx/512 >= 256) {
     return 0;
   }
   u32* buf = getVDP1ReadFramebuffer();
   vdp1_clock -= 4;
   if (context != NULL) context->cycles += 4;
   u16 val1 = T1ReadLong((u8*)buf, pixIdx*4) & 0xFFFF;
   u16 val2 = T1ReadLong((u8*)buf, (pixIdx+1)*4) & 0xFFFF;
   return (val1<<16) | val2;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBuffer16bWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr>>1;
   if (pixIdx/512 >= 256) return;
   u32* buf = getVDP1WriteFramebuffer(_Ygl->drawframe);
   PRINT_FB("W B 0x%x@0x%x line %d(%d) frame %d\n", val, pixIdx, yabsys.LineCount, yabsys.DecilineCount, _Ygl->drawframe);
   buf[pixIdx] = (val&0xFF)|0xFF000000;
   syncVdp1FBBuffer(pixIdx);
   vdp1_clock -= 2;
   if (context != NULL) context->cycles += 2;
   _Ygl->FBDirty[_Ygl->drawframe] = 1;
   _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = yabsys.LineCount;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBuffer16bWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
  addr &= 0x3FFFF;
  //Bad limitation, FB shall be different size depending mode
  u32 pixIdx = addr>>1;
  if (pixIdx/512 >= 256) return;
  u32* buf = getVDP1WriteFramebuffer(_Ygl->drawframe);
  PRINT_FB("W W 0x%x@0x%x line %d(%d) frame %d\n", val, pixIdx, yabsys.LineCount, yabsys.DecilineCount, _Ygl->drawframe);
  buf[pixIdx] = (val&0xFFFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx);
  vdp1_clock -= 2;
  if (context != NULL) context->cycles += 2;
  _Ygl->FBDirty[_Ygl->drawframe] = 1;
  _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = yabsys.LineCount;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBuffer16bWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
  addr &= 0x3FFFF;
  //Bad limitation, FB shall be different size depending mode
  u32 pixIdx = addr>>1;
  if (pixIdx/512 >= 256) return;
  u32* buf = getVDP1WriteFramebuffer(_Ygl->drawframe);
  PRINT_FB("W L 0x%x@0x%x line %d(%d) frame %d %s\n", val, addr, yabsys.LineCount, yabsys.DecilineCount, _Ygl->drawframe, (context==NULL)?"DMA":"CPU");
  buf[pixIdx] = ((val>>16)&0xFFFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx);
  buf[pixIdx+1] = ((val)&0xFFFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx+1);
  vdp1_clock -= 4;
  if (context != NULL) context->cycles += 4;
  _Ygl->FBDirty[_Ygl->drawframe] = 1;
  _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = yabsys.LineCount;
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp1FrameBuffer8bReadByte(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr;
   if (pixIdx/512 >= 256) return 0;
   u32* buf = getVDP1ReadFramebuffer();
   vdp1_clock -= 2;
   if (context != NULL) context->cycles += 2;
   PRINT_FB("R B 0x%x@0x%x\n", buf[pixIdx]&0xFF,addr);
   return T1ReadLong((u8*)buf, pixIdx*4) & 0xFF;
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp1FrameBuffer8bReadWord(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr;
   if (pixIdx/512 >= 256) return 0;
   u32* buf = getVDP1ReadFramebuffer();
   vdp1_clock -= 2;
   if (context != NULL) context->cycles += 2;
   PRINT_FB("R W 0x%x@0x%x (%d, %d)\n", T1ReadLong((u8*)buf, pixIdx*4) & 0xFFFF, addr, yabsys.LineCount, yabsys.DecilineCount);
   u8 val1 = T1ReadLong((u8*)buf, pixIdx*4) & 0xFF;
   u8 val2 = T1ReadLong((u8*)buf, (pixIdx+1)*4) & 0xFF;
   return (val1<<8) | val2;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp1FrameBuffer8bReadLong(SH2_struct *context, u8* mem, u32 addr) {
   PRINT_FB("FB R L 8b @%x\n", addr);
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr;
   if (pixIdx/512 >= 256) return 0;
   u32* buf = getVDP1ReadFramebuffer();
   vdp1_clock -= 4;
   if (context != NULL) context->cycles += 4;
   u8 val1 = T1ReadLong((u8*)buf, pixIdx*4) & 0xFF;
   u8 val2 = T1ReadLong((u8*)buf, (pixIdx+1)*4) & 0xFF;
   u8 val3 = T1ReadLong((u8*)buf, (pixIdx+2)*4) & 0xFF;
   u8 val4 = T1ReadLong((u8*)buf, (pixIdx+3)*4) & 0xFF;
   PRINT_FB("R L 0x%x@0x%x\n", (val1<<24) | (val2<<16) | (val3<<8) | (val4),addr);
   return (val1<<24) | (val2<<16) | (val3<<8) | (val4) ;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBuffer8bWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
   addr &= 0x3FFFF;
   //Bad limitation, FB shall be different size depending mode
   u32 pixIdx = addr;
   if (pixIdx/512 >= 256) return;
   u32* buf = getVDP1WriteFramebuffer(_Ygl->drawframe);
   PRINT_FB("W B 0x%x@0x%x line %d(%d) frame %d\n", val, pixIdx, yabsys.LineCount, yabsys.DecilineCount, _Ygl->drawframe);
   buf[pixIdx] = (val&0xFF)|0xFF000000;
   syncVdp1FBBuffer(pixIdx);
   vdp1_clock -= 2;
   if (context != NULL) context->cycles += 2;
   _Ygl->FBDirty[_Ygl->drawframe] = 1;
   _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = yabsys.LineCount;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBuffer8bWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
  addr &= 0x3FFFF;
  //Bad limitation, FB shall be different size depending mode
  u32 pixIdx = addr;
  if (pixIdx/512 >= 256) return;
  u32* buf = getVDP1WriteFramebuffer(_Ygl->drawframe);
  PRINT_FB("W W 0x%x@0x%x line %d(%d) frame %d\n", val, pixIdx, yabsys.LineCount, yabsys.DecilineCount, _Ygl->drawframe);
  buf[pixIdx] = ((val>>8)&0xFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx);
  buf[pixIdx+1] = (val&0xFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx+1);
  vdp1_clock -= 2;
  if (context != NULL) context->cycles += 2;
  _Ygl->FBDirty[_Ygl->drawframe] = 1;
  _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = yabsys.LineCount;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBuffer8bWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
  addr &= 0x3FFFF;
  //Bad limitation, FB shall be different size depending mode
  u32 pixIdx = addr;
  if (pixIdx/512 >= 256) return;
  u32* buf = getVDP1WriteFramebuffer(_Ygl->drawframe);
  PRINT_FB("W L 0x%x@0x%x line %d(%d) frame %d %s\n", val, pixIdx, yabsys.LineCount, yabsys.DecilineCount, _Ygl->drawframe, (context==NULL)?"DMA":"CPU");
  buf[pixIdx] = ((val>>24)&0xFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx);
  buf[pixIdx+1] = ((val>>16)&0xFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx+1);
  buf[pixIdx+2] = ((val>>8)&0xFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx+2);
  buf[pixIdx+3] = (val&0xFF)|0xFF000000;
  syncVdp1FBBuffer(pixIdx+3);
  vdp1_clock -= 4;
  if (context != NULL) context->cycles += 4;
  _Ygl->FBDirty[_Ygl->drawframe] = 1;
  _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = yabsys.LineCount;
}
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////

int Vdp1Init(void) {
   if ((Vdp1Regs = (Vdp1 *) malloc(sizeof(Vdp1))) == NULL)
      return -1;

   if ((Vdp1Ram = T1MemoryInit(0x80000)) == NULL)
      return -1;

   Vdp1External.disptoggle = 1;

   Vdp1Regs->TVMR = 0;
   Vdp1Regs->FBCR = 0;
   Vdp1Regs->PTMR = 0;

   Vdp1Regs->userclipX1=0;
   Vdp1Regs->userclipY1=0;
   Vdp1Regs->userclipX2=1024;
   Vdp1Regs->userclipY2=512;

   Vdp1Regs->localX=0;
   Vdp1Regs->localY=0;

   VDP1_MASK = 0xFFFF;

   vdp1Ram_update_start = 0x80000;
   vdp1Ram_update_end = 0x0;

   _Ygl->shallVdp1Erase[0] = 1;
   _Ygl->shallVdp1Erase[1] = 1;

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1DeInit(void) {
   if (Vdp1Regs)
      free(Vdp1Regs);
   Vdp1Regs = NULL;

   if (Vdp1Ram)
      T1MemoryDeInit(Vdp1Ram);
   Vdp1Ram = NULL;

}

//////////////////////////////////////////////////////////////////////////////

int VideoInit(int coreid) {
   return VideoChangeCore(coreid);
}

//////////////////////////////////////////////////////////////////////////////

int VideoChangeCore(int coreid)
{
   int i;

   // Make sure the old core is freed
   VideoDeInit();

   // So which core do we want?
   if (coreid == VIDCORE_DEFAULT)
      coreid = 0; // Assume we want the first one

   // Go through core list and find the id
   for (i = 0; VIDCoreList[i] != NULL; i++)
   {
      if (VIDCoreList[i]->id == coreid)
      {
         // Set to current core
         VIDCore = VIDCoreList[i];
         break;
      }
   }

   if (VIDCore == NULL)
      return -1;

   if (VIDCore->Init() != 0)
      return -1;

   // Reset resolution/priority variables
   if (Vdp2Regs)
   {
      VIDCore->Vdp1Reset();
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void VideoDeInit(void) {
   if (VIDCore)
      VIDCore->DeInit();
   VIDCore = NULL;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1Reset(void) {
   FRAMELOG("Reset Vdp1\n");
   Vdp1Regs->PTMR = 0;
   Vdp1Regs->MODR = 0x1000; // VDP1 Version 1
   Vdp1Regs->TVMR = 0;
   switchFB16bit();
   Vdp1Regs->ENDR = 0;
   VDP1_MASK = 0xFFFF;
   VIDCore->Vdp1Reset();
   vdp1_clock = 0;
}

int VideoSetSetting( int type, int value )
{
	if (VIDCore) VIDCore->SetSettingValue( type, value );
	return 0;
}


//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp1ReadByte(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0xFF;
   LOG("trying to byte-read a Vdp1 register\n");
   return 0;
}

//////////////////////////////////////////////////////////////////////////////
u16 FASTCALL Vdp1ReadWord(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0xFF;
   switch(addr) {
      case 0x10:
        FRAMELOG("Read EDSR %X line = %d (%d)\n", Vdp1Regs->EDSR, yabsys.LineCount, yabsys.DecilineCount);
        if (Vdp1External.checkEDSR == 0) {
          if (VIDCore != NULL)
            if (VIDCore->FinsihDraw != NULL)
              VIDCore->FinsihDraw();
        }
        Vdp1External.checkEDSR = 1;
        return Vdp1Regs->EDSR;
      case 0x12:
        FRAMELOG("Read LOPR %X line = %d\n", Vdp1Regs->LOPR, yabsys.LineCount);
         return Vdp1Regs->LOPR;
      case 0x14:
        FRAMELOG("Read COPR %X line = %d\n", Vdp1Regs->COPR, yabsys.LineCount);
         return Vdp1Regs->COPR;
      case 0x16:
         return 0x1000 | ((Vdp1Regs->PTMR & 2) << 7) | ((Vdp1Regs->FBCR & 0x1E) << 3) | (Vdp1Regs->TVMR & 0xF);
      default:
         LOG("trying to read a Vdp1 write-only register\n");
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp1ReadLong(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0xFF;
   LOG("trying to long-read a Vdp1 register - %08X\n", addr);
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1WriteByte(SH2_struct *context, u8* mem, u32 addr, UNUSED u8 val) {
   addr &= 0xFF;
   LOG("trying to byte-write a Vdp1 register - %08X\n", addr);
}

//////////////////////////////////////////////////////////////////////////////

static u8 FBCRChangeUpdated = 0;
static void updateFBCRChange() {
  if (FBCRChangeUpdated == 0) return;
  Vdp1External.manualchange = 0;
  Vdp1External.onecyclechange = 0;
  if (((Vdp1Regs->TVMR >> 3) & 0x01) == 1){ //VBE is set
    if ((Vdp1Regs->FBCR & 3) == 3) {
      Vdp1External.manualchange = 1;
    } else {
      //VBE can be one only when FCM and FCT are 1
      LOG("Prohibited FBCR/TVMR values\n");
      // Assume prohibited modes behave like if VBE/FCT/FCM were all 1
      Vdp1External.manualchange = 1;
    }
  } else {
    //Manual erase shall not be reseted but need to save its current value
    // Only at frame change the order is executed.
    //This allows to have both a manual clear and a manual change at the same frame without continuously clearing the VDP1
    //The mechanism is used by the official bios animation
    Vdp1External.onecyclechange = ((Vdp1Regs->FBCR & 3) == 0) || ((Vdp1Regs->FBCR & 3) == 1);
    Vdp1External.manualchange = ((Vdp1Regs->FBCR & 3) == 3);
  }
  FBCRChangeUpdated = 0;
}

static u8 FBCREraseUpdated = 0;
static void updateFBCRErase() {
  if (FBCREraseUpdated == 0) return;
  Vdp1External.onecycleerase = 0;
  Vdp1External.manualerase = 0;
  if (((Vdp1Regs->TVMR >> 3) & 0x01) != 1){ //VBE is not set
    //Manual erase shall not be reseted but need to save its current value
    // Only at frame change the order is executed.
    //This allows to have both a manual clear and a manual change at the same frame without continuously clearing the VDP1
    //The mechanism is used by the official bios animation
    Vdp1External.onecycleerase = ((Vdp1Regs->FBCR & 3) == 0) || ((Vdp1Regs->FBCR & 3) == 1);
    Vdp1External.manualerase = ((Vdp1Regs->FBCR & 3) == 2);
  }
  FBCREraseUpdated = 0;
}
static void updateFBCRVBE() {
  Vdp1External.useVBlankErase = 0;
  if (((Vdp1Regs->TVMR >> 3) & 0x01) == 1){ //VBE is set
    if ((Vdp1Regs->FBCR & 3) == 3) {
      Vdp1External.useVBlankErase = 1;
    } else {
      //VBE can be one only when FCM and FCT are 1
      LOG("Prohibited FBCR/TVMR values\n");
      // Assume prohibited modes behave like if VBE/FCT/FCM were all 1
      Vdp1External.useVBlankErase = 1;
    }
  }
}

static void Vdp1TryDraw(void) {
  if ((yabsys.LineCount >= yabsys.MaxLineCount-2) && (yabsys.LineCount <= yabsys.MaxLineCount-1)) return;
  if ((oldNeedVdp1draw == 0) && (needVdp1draw != 0)) {
    FRAMELOG("Shift EDSR\n");
    Vdp1Regs->EDSR >>= 1;
    checkFBSync();
    FRAMELOG("Will drawn on frame %d\n",_Ygl->drawframe);
  }
  oldNeedVdp1draw = needVdp1draw;
  if (needVdp1draw == 1) {
    CmdListInLoop = 0;
    needVdp1draw = Vdp1Draw();
  }
}

static int Vdp1FBDraw(void) {
  if (VIDCore->Vdp1FBDraw){
    VIDCore->Vdp1FBDraw();
  }
  return 1;
}

static void checkFBSync() {
  int needClearFB = 0;
  if (_Ygl->vdp1IsNotEmpty[_Ygl->drawframe] != -1) {
    //FB has been accessed
    FRAMELOG("Update FB Direct access on frame %d line %d(%d)\n", _Ygl->drawframe, yabsys.LineCount, yabsys.DecilineCount);
    updateVdp1DrawingFBMem(_Ygl->drawframe);
    needClearFB = 1;
    Vdp1FBDraw();
    _Ygl->vdp1IsNotEmpty[_Ygl->drawframe] = -1;
    if (needClearFB != 0) clearVDP1Framebuffer(_Ygl->drawframe);
  }
}

void FASTCALL Vdp1WriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
  u16 oldVal = 0;
  addr &= 0xFF;
  switch(addr) {
    case 0x0:
      if ((Vdp1Regs->FBCR & 3) != 3) val = (val & (~0x4));
      if ((val & 0x1)!=(Vdp1Regs->TVMR & 0x1)) {
        if (val & 0x1) switchFB8bit();
        else switchFB16bit();
      }
      Vdp1Regs->TVMR = val;
      FRAMELOG("TVMR => Write VBE=%d FCM=%d FCT=%d line = %d (%d)\n", (Vdp1Regs->TVMR >> 3) & 0x01, (Vdp1Regs->FBCR & 0x02) >> 1, (Vdp1Regs->FBCR & 0x01),  yabsys.LineCount, yabsys.DecilineCount);
    break;
    case 0x2:
      Vdp1Regs->FBCR = val;
      FRAMELOG("FBCR => Write %x VBE=%d FCM=%d FCT=%d line = %d (%d) (VBlank %d, max %d)\n", val, (Vdp1Regs->TVMR >> 3) & 0x01, (Vdp1Regs->FBCR & 0x02) >> 1, (Vdp1Regs->FBCR & 0x01),  yabsys.LineCount, yabsys.DecilineCount, yabsys.VBlankLineCount, yabsys.MaxLineCount);
      FBCREraseUpdated = 1;
      FBCRChangeUpdated = 1;
      break;
    case 0x4:
      FRAMELOG("Write PTMR = %X line = %d (%d) %d\n", val, yabsys.LineCount, yabsys.DecilineCount, yabsys.VBlankLineCount);
      if ((val & 0x3)==0x3) {
        //Skeleton warriors is writing 0xFFF to PTMR. It looks like the behavior is 0x2
          val = 0x2;
      }
      oldVal = Vdp1Regs->PTMR;
      Vdp1Regs->PTMR = val;
      if (val == 1){
        FRAMELOG("VDP1: VDPEV_DIRECT_DRAW\n");
        checkFBSync();
        abortVdp1();
        vdp1_clock += getVdp1CyclesPerLine();
        RequestVdp1ToDraw();
        Vdp1TryDraw();
      }
      break;
      case 0x6:
         Vdp1Regs->EWDR = val;
         break;
      case 0x8:
         Vdp1Regs->EWLR = val;
         break;
      case 0xA:
         Vdp1Regs->EWRR = val;
         break;
      case 0xC:
         Vdp1Regs->ENDR = val;
         FRAMELOG("Abort from ENDR %d\n", yabsys.LineCount);
         abortVdp1();
         break;
      default:
         LOG("trying to write a Vdp1 read-only register - %08X\n", addr);
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1WriteLong(SH2_struct *context, u8* mem, u32 addr, UNUSED u32 val) {
   addr &= 0xFF;
   LOG("trying to long-write a Vdp1 register - %08X\n", addr);
}

static void printCommand(vdp1cmd_struct *cmd) {
  printf("===== CMD =====\n");
  printf("CMDCTRL = 0x%x\n",cmd->CMDCTRL );
  printf("CMDLINK = 0x%x\n",cmd->CMDLINK );
  printf("CMDPMOD = 0x%x\n",cmd->CMDPMOD );
  printf("CMDCOLR = 0x%x\n",cmd->CMDCOLR );
  printf("CMDSRCA = 0x%x\n",cmd->CMDSRCA );
  printf("CMDSIZE = 0x%x\n",cmd->CMDSIZE );
  printf("CMDXA = 0x%x\n",cmd->CMDXA );
  printf("CMDYA = 0x%x\n",cmd->CMDYA );
  printf("CMDXB = 0x%x\n",cmd->CMDXB );
  printf("CMDYB = 0x%x\n",cmd->CMDYB );
  printf("CMDXC = 0x%x\n",cmd->CMDXC );
  printf("CMDYC = 0x%x\n",cmd->CMDYC );
  printf("CMDXD = 0x%x\n",cmd->CMDXD );
  printf("CMDYD = 0x%x\n",cmd->CMDYD );
  printf("CMDGRDA = 0x%x\n",cmd->CMDGRDA );
}

static int emptyCmd(vdp1cmd_struct *cmd) {
  return (
    (cmd->CMDCTRL == 0) &&
    (cmd->CMDLINK == 0) &&
    (cmd->CMDPMOD == 0) &&
    (cmd->CMDCOLR == 0) &&
    (cmd->CMDSRCA == 0) &&
    (cmd->CMDSIZE == 0) &&
    (cmd->CMDXA == 0) &&
    (cmd->CMDYA == 0) &&
    (cmd->CMDXB == 0) &&
    (cmd->CMDYB == 0) &&
    (cmd->CMDXC == 0) &&
    (cmd->CMDYC == 0) &&
    (cmd->CMDXD == 0) &&
    (cmd->CMDYD == 0) &&
    (cmd->CMDGRDA == 0));
}

//////////////////////////////////////////////////////////////////////////////

static int getNormalCycles(vdp1cmd_struct *cmd) {
  int rw = MAX(cmd->w, 1);
  if (Vdp1Regs->TVMR & 0x1) rw >>= 1;
  return rw * MAX(cmd->h, 1);
}

#define CAP(L,A,H) (((A)<(L))?(L):(((A)>(H))?(H):(A)))

static int getScaledCycles(vdp1cmd_struct *cmd) {
  int ax = cmd->CMDXA;
  int ay = cmd->CMDYA;
  int bx = cmd->CMDXB;
  int dy = cmd->CMDYD;
  if (!(cmd->CMDPMOD & 0x800)) {
    int lx = Vdp1Regs->userclipX1;
    int hx = Vdp1Regs->userclipX2;
    int ly = Vdp1Regs->userclipY1;
    int hy = Vdp1Regs->userclipY2;
    ax = CAP(lx,ax,hx);
    bx = CAP(lx,bx,hx);
    ay = CAP(ly,ay,hy);
    dy = CAP(ly,dy,hy);
  }
  int cmdW = MAX(cmd->w, 1);
   switch ((cmd->CMDPMOD >> 3) & 0x7) {
    case 0:
    case 1:
      // 4 pixels per 16 bits
      cmdW  = cmdW >> 2;
      break;
    case 2:
    case 3:
    case 4:
      // 2 pixels per 16 bits
      cmdW = cmdW >> 1;
      break;
    default:
      break;
  }
  int rh = abs(dy - ay);
  int rw = abs(bx - ax);
  if (Vdp1Regs->TVMR & 0x1) rw >>= 1;
  if (((cmd->CMDPMOD>>12)&0x1) && (rw < cmd->w)) cmdW >>= 1; //HSS
  return MAX(rw, cmdW) * MAX(rh, 1);
}

static int getDistortedCycles(vdp1cmd_struct *cmd) {
  int ax = cmd->CMDXA;
  int ay = cmd->CMDYA;
  int bx = cmd->CMDXB;
  int by = cmd->CMDYB;
  int cx = cmd->CMDXC;
  int cy = cmd->CMDYC;
  int dx = cmd->CMDXD;
  int dy = cmd->CMDYD;
  if (!(cmd->CMDPMOD & 0x800)) {
    int lx = Vdp1Regs->userclipX1;
    int hx = Vdp1Regs->userclipX2;
    int ly = Vdp1Regs->userclipY1;
    int hy = Vdp1Regs->userclipY2;
    ax = CAP(lx,ax,hx);
    bx = CAP(lx,bx,hx);
    cx = CAP(lx,cx,hx);
    dx = CAP(lx,dx,hx);
    ay = CAP(ly,ay,hy);
    by = CAP(ly,by,hy);
    cy = CAP(ly,cy,hy);
    dy = CAP(ly,dy,hy);
  }

  int rw = (abs(bx-ax)
          + abs(cx-dx)
           )/2;
  if (Vdp1Regs->TVMR & 0x1) rw >>= 1;
  int cmdW = cmd->w;
  switch ((cmd->CMDPMOD >> 3) & 0x7) {
   case 0:
   case 1:
     // 4 pixels per 16 bits
     cmdW  = cmdW >> 2;
     break;
   case 2:
   case 3:
   case 4:
     // 2 pixels per 16 bits
     cmdW = cmdW >> 1;
     break;
   default:
     break;
 }
  if (((cmd->CMDPMOD>>12)&0x1) && (rw < cmd->w))  cmdW >>= 1; //HSS
  rw = MAX(cmdW, rw);
  int rh = MAX(abs(ay-dy),
               abs(cy-by)
              );

  return (int)((float)MAX(rw, 1) * (float)MAX(rh, 1));
}

static int getPolygonCycles(vdp1cmd_struct *cmd) {
  int lx = -1024;
  int ly = -1024;
  int hx = 1023;
  int hy = 1023;
  if (!(cmd->CMDPMOD & 0x800)) {
    lx = Vdp1Regs->userclipX1;
    hx = Vdp1Regs->userclipX2;
    ly = Vdp1Regs->userclipY1;
    hy = Vdp1Regs->userclipY2;
  }
  int rw = (abs(cmd->CMDXB-cmd->CMDXA)
          + abs(cmd->CMDXC-cmd->CMDXD)
           )/2;
  if (Vdp1Regs->TVMR & 0x1) rw >>= 1;
  int rh = MAX(abs(cmd->CMDYA-cmd->CMDYD),
               abs(cmd->CMDYC-cmd->CMDYB)
              );
  return (int)((float)MAX(rw, 1) * (float)MAX(rh, 1));
}

static int Vdp1NormalSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs){
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];
  int ret = 1;
  if (emptyCmd(cmd)) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return -1;
  }

  if ((cmd->CMDSIZE & 0x8000)) {
    yabsys.vdp1cycles += 70;
    return -1; // BAD Command
  }
  if (((cmd->CMDPMOD >> 3) & 0x7) > 5) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return -1;
  }
  cmd->w = ((cmd->CMDSIZE >> 8) & 0x3F) * 8;
  cmd->h = cmd->CMDSIZE & 0xFF;
  if ((cmd->w == 0) || (cmd->h == 0)) {
    yabsys.vdp1cycles += 70;
    ret = 0;
  }

  cmd->flip = (cmd->CMDCTRL & 0x30) >> 4;
  cmd->priority = 0;

  if ( CONVERTCMD(&cmd->CMDXA) ||
       CONVERTCMD(&cmd->CMDYA)) {
         // damaged data
         yabsys.vdp1cycles += 70;
         return -1;
       }

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;

  cmd->CMDXB = cmd->CMDXA + MAX(1,cmd->w);
  cmd->CMDYB = cmd->CMDYA;
  cmd->CMDXC = cmd->CMDXA + MAX(1,cmd->w);
  cmd->CMDYC = cmd->CMDYA + MAX(1,cmd->h);
  cmd->CMDXD = cmd->CMDXA;
  cmd->CMDYD = cmd->CMDYA + MAX(1,cmd->h);

  yabsys.vdp1cycles+= getNormalCycles(cmd);

  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, ram, (Vdp1RamReadWord(NULL, ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }

  VIDCore->Vdp1NormalSpriteDraw(cmd, ram, regs);
  return ret;
}

static int Vdp1ScaledSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];
  s16 rw = 0, rh = 0;
  s16 x, y;
  int ret = 1;

  if (emptyCmd(cmd)) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return -1;
  }

  cmd->w = ((cmd->CMDSIZE >> 8) & 0x3F) * 8;
  cmd->h = cmd->CMDSIZE & 0xFF;
  if ((cmd->w == 0) || (cmd->h == 0)) {
    yabsys.vdp1cycles += 70;
    ret = 0;
  }

  cmd->flip = (cmd->CMDCTRL & 0x30) >> 4;
  cmd->priority = 0;

  switch ((cmd->CMDCTRL & 0xF00) >> 8)
  {
    case 0x0:
      if ( CONVERTCMD(&cmd->CMDXA) ||
           CONVERTCMD(&cmd->CMDYA) ||
           CONVERTCMD(&cmd->CMDXC) ||
           CONVERTCMD(&cmd->CMDYC)) {
             // damaged data
             yabsys.vdp1cycles += 70;
             return -1;
           }
       break;
    default:
      if ( CONVERTCMD(&cmd->CMDXA) ||
           CONVERTCMD(&cmd->CMDYA) ||
           CONVERTCMD(&cmd->CMDXB) ||
           CONVERTCMD(&cmd->CMDYB)) {
             // damaged data
             yabsys.vdp1cycles += 70;
             return -1;
           }
       break;
  }


  x = cmd->CMDXA;
  y = cmd->CMDYA;
  // Setup Zoom Point
  switch ((cmd->CMDCTRL & 0xF00) >> 8)
  {
  case 0x0: // Only two coordinates
    rw = cmd->CMDXC - cmd->CMDXA;
    rh = cmd->CMDYC - cmd->CMDYA;
    break;
  case 0x5: // Upper-left
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  case 0x6: // Upper-Center
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw / 2;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  case 0x7: // Upper-Right
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  case 0x9: // Center-left
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    y = y - rh / 2;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  case 0xA: // Center-center
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw / 2;
    y = y - rh / 2;
    break;
  case 0xB: // Center-right
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw;
    y = y - rh / 2;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  case 0xD: // Lower-left
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    y = y - rh;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  case 0xE: // Lower-center
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw / 2;
    y = y - rh;
    break;
  case 0xF: // Lower-right
    rw = cmd->CMDXB;
    rh = cmd->CMDYB;
    x = x - rw;
    y = y - rh;
    if ((rw < 0)||(rh <0)) {
      return 0;
    }
    break;
  default: break;
  }

  cmd->CMDXA = x + regs->localX;
  cmd->CMDYA = y + regs->localY;
  cmd->CMDXB = x + rw  + regs->localX;
  cmd->CMDYB = y + regs->localY;
  cmd->CMDXC = x + rw  + regs->localX;
  cmd->CMDYC = y + rh + regs->localY;
  cmd->CMDXD = x + regs->localX;
  cmd->CMDYD = y + rh + regs->localY;

  // Setup Zoom Point
  switch ((cmd->CMDCTRL & 0xF00) >> 8)
  {
  case 0x0: // Only two coordinates
    if ((s16)cmd->CMDXC > (s16)cmd->CMDXA){ cmd->CMDXB += 1; cmd->CMDXC += 1;} else { cmd->CMDXA += 1; cmd->CMDXB +=1; cmd->CMDXC += 1; cmd->CMDXD += 1;}
    if ((s16)cmd->CMDYC > (s16)cmd->CMDYA){ cmd->CMDYC += 1; cmd->CMDYD += 1;} else { cmd->CMDYA += 1; cmd->CMDYB += 1; cmd->CMDYC += 1; cmd->CMDYD += 1;}
    break;
  case 0x5: // Upper-left
  case 0x6: // Upper-Center
  case 0x7: // Upper-Right
  case 0x9: // Center-left
  case 0xA: // Center-center
  case 0xB: // Center-right
  case 0xD: // Lower-left
  case 0xE: // Lower-center
  case 0xF: // Lower-right
    cmd->CMDXB += 1;
    cmd->CMDXC += 1;
    cmd->CMDYC += 1;
    cmd->CMDYD += 1;
    break;
  default: break;
  }

  //mission 1 of burning rangers is loading a lot the vdp1.
  yabsys.vdp1cycles+= getScaledCycles(cmd);

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }

  VIDCore->Vdp1ScaledSpriteDraw(cmd, ram, regs);
  return ret;
}

static int Vdp1DistortedSpriteDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];
  int ret = 1;

  if (emptyCmd(cmd)) {
    // damaged data
    yabsys.vdp1cycles += 70;
    return 0;
  }

  cmd->w = ((cmd->CMDSIZE >> 8) & 0x3F) * 8;
  cmd->h = cmd->CMDSIZE & 0xFF;
  if ((cmd->w == 0) || (cmd->h == 0)) {
    yabsys.vdp1cycles += 70;
    ret = 0;
  }

  cmd->flip = (cmd->CMDCTRL & 0x30) >> 4;
  cmd->priority = 0;

  if ( CONVERTCMD(&cmd->CMDXA) ||
       CONVERTCMD(&cmd->CMDYA) ||
       CONVERTCMD(&cmd->CMDXB) ||
       CONVERTCMD(&cmd->CMDYB) ||
       CONVERTCMD(&cmd->CMDXC) ||
       CONVERTCMD(&cmd->CMDYC) ||
       CONVERTCMD(&cmd->CMDXD) ||
       CONVERTCMD(&cmd->CMDYD)) {
         // damaged data
         yabsys.vdp1cycles += 70;
         return 0;
       }

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC += regs->localX;
  cmd->CMDYC += regs->localY;
  cmd->CMDXD += regs->localX;
  cmd->CMDYD += regs->localY;

  //mission 1 of burning rangers is loading a lot the vdp1.
  yabsys.vdp1cycles+= getDistortedCycles(cmd);

  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }

  VIDCore->Vdp1DistortedSpriteDraw(cmd, ram, regs);
  return ret;
}

static int Vdp1PolygonDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];

  if ( CONVERTCMD(&cmd->CMDXA) ||
       CONVERTCMD(&cmd->CMDYA) ||
       CONVERTCMD(&cmd->CMDXB) ||
       CONVERTCMD(&cmd->CMDYB) ||
       CONVERTCMD(&cmd->CMDXC) ||
       CONVERTCMD(&cmd->CMDYC) ||
       CONVERTCMD(&cmd->CMDXD) ||
       CONVERTCMD(&cmd->CMDYD)) {
         // damaged data
         yabsys.vdp1cycles += 70;
         return 0;
       }

  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC += regs->localX;
  cmd->CMDYC += regs->localY;
  cmd->CMDXD += regs->localX;
  cmd->CMDYD += regs->localY;

  yabsys.vdp1cycles += getPolygonCycles(cmd);
  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }
  cmd->priority = 0;
  cmd->w = 1;
  cmd->h = 1;
  cmd->flip = 0;

  VIDCore->Vdp1PolygonDraw(cmd, ram, regs);
  return 1;
}

static int Vdp1PolylineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs) {

  Vdp2 *varVdp2Regs = &Vdp2Lines[0];

  cmd->priority = 0;
  cmd->w = 1;
  cmd->h = 1;
  cmd->flip = 0;

  if ( CONVERTCMD(&cmd->CMDXA) ||
       CONVERTCMD(&cmd->CMDYA) ||
       CONVERTCMD(&cmd->CMDXB) ||
       CONVERTCMD(&cmd->CMDYB) ||
       CONVERTCMD(&cmd->CMDXC) ||
       CONVERTCMD(&cmd->CMDYC) ||
       CONVERTCMD(&cmd->CMDXD) ||
       CONVERTCMD(&cmd->CMDYD)) {
         // damaged data
         yabsys.vdp1cycles += 70;
         return 0;
       }


  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC += regs->localX;
  cmd->CMDYC += regs->localY;
  cmd->CMDXD += regs->localX;
  cmd->CMDYD += regs->localY;

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
    for (int i = 0; i < 4; i++){
      u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
      cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
      cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
    }
  }
  VIDCore->Vdp1PolylineDraw(cmd, ram, regs);

  return 1;
}

static int Vdp1LineDraw(vdp1cmd_struct *cmd, u8 * ram, Vdp1 * regs) {
  Vdp2 *varVdp2Regs = &Vdp2Lines[0];


  if ( CONVERTCMD(&cmd->CMDXA) ||
       CONVERTCMD(&cmd->CMDYA) ||
       CONVERTCMD(&cmd->CMDXB) ||
       CONVERTCMD(&cmd->CMDYB)) {
         // damaged data
         yabsys.vdp1cycles += 70;
         return 0;
       }


  cmd->CMDXA += regs->localX;
  cmd->CMDYA += regs->localY;
  cmd->CMDXB += regs->localX;
  cmd->CMDYB += regs->localY;
  cmd->CMDXC = cmd->CMDXB;
  cmd->CMDYC = cmd->CMDYB;
  cmd->CMDXD = cmd->CMDXA;
  cmd->CMDYD = cmd->CMDYA;

  //gouraud
  memset(cmd->G, 0, sizeof(float)*16);
  if ((cmd->CMDPMOD & 4))
  {
  for (int i = 0; i < 4; i++){
    u16 color2 = Vdp1RamReadWord(NULL, Vdp1Ram, (Vdp1RamReadWord(NULL, Vdp1Ram, regs->addr + 0x1C) << 3) + (i << 1));
    cmd->G[(i << 2) + 0] = (float)((color2 & 0x001F)) / (float)(0x1F) - 0.5f;
    cmd->G[(i << 2) + 1] = (float)((color2 & 0x03E0) >> 5) / (float)(0x1F) - 0.5f;
    cmd->G[(i << 2) + 2] = (float)((color2 & 0x7C00) >> 10) / (float)(0x1F) - 0.5f;
  }
  }
  cmd->priority = 0;
  cmd->w = 1;
  cmd->h = 1;
  cmd->flip = 0;

  VIDCore->Vdp1LineDraw(cmd, ram, regs);

  return 1;
}

static int rasterValue = 1708;

void Vdp1SetRaster(int is352) {
  if (is352)
    rasterValue = 1820;
  else
    rasterValue = 1708;
}

static int getVdp1CyclesPerLine(void)
{
  return rasterValue;
}

static u32 returnAddr = 0xffffffff;

#ifdef DEBUG_CMD_LIST
void debugCmdList() {
  YuiMsg("Draw %d (%d)\n", yabsys.LineCount, _Ygl->drawframe);
  for (int i=0;;i++)
  {
     char *string;
     u32 addr = Vdp1DebugGetCommandAddr(i);
     if ((string = Vdp1DebugGetCommandNumberName(addr)) == NULL)
        break;

     YuiMsg("\t%s\n", string);
  }
}
#endif
static u32 Vdp1DebugGetCommandNumberAddr(u32 number);

int EvaluateCmdListHash(Vdp1 * regs){
  int hash = 0;
  u32 addr = 0;
  u32 returnAddr = 0xFFFFFFFF;
  u32 commandCounter = 0;
  u16 command;

  command = T1ReadWord(Vdp1Ram, addr);

  while (!(command & 0x8000) && (commandCounter < 2000))
  {
      vdp1cmd_struct cmd;
     // Make sure we're still dealing with a valid command
     if ((command & 0x000C) == 0x000C)
        // Invalid, abort
        return hash;
      Vdp1ReadCommand(&cmd, addr, Vdp1Ram);
      hash ^= (cmd.CMDCTRL << 16) | cmd.CMDLINK;
      hash ^= (cmd.CMDPMOD << 16) | cmd.CMDCOLR;
      hash ^= (cmd.CMDSRCA << 16) | cmd.CMDSIZE;
      hash ^= (cmd.CMDXA << 16) | cmd.CMDYA;
      hash ^= (cmd.CMDXB << 16) | cmd.CMDYB;
      hash ^= (cmd.CMDXC << 16) | cmd.CMDYC;
      hash ^= (cmd.CMDXD << 16) | cmd.CMDYD;
      hash ^= (cmd.CMDGRDA << 16) | _Ygl->drawframe;

     // Determine where to go next
     switch ((command & 0x3000) >> 12)
     {
        case 0: // NEXT, jump to following table
           addr += 0x20;
           break;
        case 1: // ASSIGN, jump to CMDLINK
           addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
           break;
        case 2: // CALL, call a subroutine
           if (returnAddr == 0xFFFFFFFF)
              returnAddr = addr + 0x20;

           addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
           break;
        case 3: // RETURN, return from subroutine
           if (returnAddr != 0xFFFFFFFF) {
              addr = returnAddr;
              returnAddr = 0xFFFFFFFF;
           }
           else
              addr += 0x20;
           break;
     }

     if (addr > 0x7FFE0)
        return hash;
     command = T1ReadWord(Vdp1Ram, addr);
     commandCounter++;
  }
  return hash;
}

static int sameCmd(vdp1cmd_struct* a, vdp1cmd_struct* b) {
  if (a == NULL) return 0;
  if (b == NULL) return 0;
  if (emptyCmd(a)) return 0;
  int cmp = memcmp(a, b, 15*sizeof(int));
  if (cmp == 0) {
    return 1;
  }
  return 0;
}

static int lastHash = -1;
void Vdp1DrawCommands(u8 * ram, Vdp1 * regs)
{
  int cylesPerLine  = getVdp1CyclesPerLine();

  if ((Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_IDLE) {
    FRAMELOG("Start vdp1 Draw %d(%d)\n", yabsys.LineCount, yabsys.DecilineCount);
    #if 0
    int newHash = EvaluateCmdListHash(regs);
    // Breaks megamanX4
    if (newHash == lastHash) {
      #ifdef DEBUG_CMD_LIST
      YuiMsg("Abort same command %x %x (%d) (%d)\n", newHash, lastHash, _Ygl->drawframe, yabsys.LineCount);
      #endif
      return;
    }
    lastHash = newHash;
    YuiMsg("The last list is 0x%x (%d) (%d)\n", newHash, _Ygl->drawframe, yabsys.LineCount);
    #endif
    #ifdef DEBUG_CMD_LIST
    debugCmdList();
    #endif

    returnAddr = 0xffffffff;
    nbCmdToProcess = 0;

     // Vdp1Regs->EDSR >>= 1;
     Vdp1Regs->addr = 0;
     // BEF <- CEF
     // CEF <- 0
     Vdp1Regs->COPR = 0;
     Vdp1Regs->lCOPR = 0;
     startVdp1Render();
  }

   Vdp1External.status &= ~VDP1_STATUS_MASK;
   Vdp1External.status |= VDP1_STATUS_RUNNING;
   if (regs->addr > 0x7FFFF) {
     FRAMELOG("Address Error\n");
      Vdp1External.status &= ~VDP1_STATUS_MASK;
      Vdp1External.status |= VDP1_STATUS_IDLE;
      endVdp1Render();
      return; // address error
    }

    u16 command = Vdp1RamReadWord(NULL, ram, regs->addr);


   FRAMELOG_CMD("Command is 0x%x @ 0x%x available cycles %d %d(%d)\n", command, regs->addr, vdp1_clock, yabsys.LineCount, yabsys.DecilineCount);

   Vdp1External.checkEDSR = 0;

   vdp1cmd_struct oldCmd = {0};

   yabsys.vdp1cycles = 0;
   //Shall continue is used for prohibited usage of ENd bit. In case a command is valid (like polygon drawing) but with a end bit set, the command is executed then stopped.
   //Not sure it is really stopped in that case, maybe end bit is ignored for other code than 0x8000
   while (!(command & 0x8000) && (nbCmdToProcess < CMD_QUEUE_SIZE) && (CmdListInLoop == 0)) {
     int ret;
     vdp1cmd_struct cmd = {0};
      regs->COPR = (regs->addr & 0x7FFFF) >> 3;
      // First, process the command
      if (!(command & 0x4000)) { // if (!skip)
         int ret;
         if (vdp1_clock <= 0) {
           //No more clock cycle, wait next line
           return;
         }
         oldCmd = cmd;
         Vdp1ReadCommand(&cmd, regs->addr, ram);
         switch (command & 0x000F) {
           case 0: // normal sprite draw
           if (!sameCmd(&cmd, &oldCmd)) {
             ret = Vdp1NormalSpriteDraw(&cmd, ram, regs);
             if (ret == 1) nbCmdToProcess++;
             else {
               FRAMELOG_CMD("Reset vdp1_clock %d %d\n", yabsys.LineCount, __LINE__);
               vdp1_clock = 0; //Incorrect command, wait next line to continue
             }
           }
           break;
           case 1: // scaled sprite draw
           if (!sameCmd(&cmd, &oldCmd)) {
             ret = Vdp1ScaledSpriteDraw(&cmd, ram, regs);
             if (ret == 1) nbCmdToProcess++;
             else {
               FRAMELOG_CMD("Reset vdp1_clock %d %d\n", yabsys.LineCount, __LINE__);
               vdp1_clock = 0; //Incorrect command, wait next line to continue
             }
           }
           break;
           case 2: // distorted sprite draw
           case 3: /* this one should be invalid, but some games
           (Hardcore 4x4 for instance) use it instead of 2 */
           if (!sameCmd(&cmd, &oldCmd)) {
             ret = Vdp1DistortedSpriteDraw(&cmd, ram, regs);
             if (ret == 1) nbCmdToProcess++;
             else {
               FRAMELOG_CMD("Reset vdp1_clock %d %d\n", yabsys.LineCount, __LINE__);
               vdp1_clock = 0; //Incorrect command, wait next line to continue
             }
           }
           break;
           case 4: // polygon draw
           // if (!sameCmd(&cmd, &oldCmd)) {
             nbCmdToProcess += Vdp1PolygonDraw(&cmd, ram, regs);
             // }
             break;
             case 5: // polyline draw
             case 7: // undocumented mirror
             if (!sameCmd(&cmd, &oldCmd)) {
               nbCmdToProcess += Vdp1PolylineDraw(&cmd, ram, regs);
             }
             break;
             case 6: // line draw
             if (!sameCmd(&cmd, &oldCmd)) {
               nbCmdToProcess += Vdp1LineDraw(&cmd, ram, regs);
             }
             break;
             case 8: // user clipping coordinates
             yabsys.vdp1cycles += 16;
             if (!sameCmd(&cmd, &oldCmd)) {
               VIDCore->Vdp1UserClipping(&cmd, ram, regs);
             }
             break;
             case 11: // undocumented command
              //Do nothing as we are skipping it.
             break;
             case 9: // system clipping coordinates
             yabsys.vdp1cycles += 16;
             if (!sameCmd(&cmd, &oldCmd)) {
               VIDCore->Vdp1SystemClipping(&cmd, ram, regs);
             }
             break;
             case 10: // local coordinate
             yabsys.vdp1cycles += 16;
             if (!sameCmd(&cmd, &oldCmd)) {
               VIDCore->Vdp1LocalCoordinate(&cmd, ram, regs);
             }
             break;
             default: // Abort
             FRAMELOG("vdp1\t: Bad command: %x\n", command);
             Vdp1External.status &= ~VDP1_STATUS_MASK;
             Vdp1External.status |= VDP1_STATUS_IDLE;
             endVdp1Render();
             regs->COPR = (regs->addr & 0x7FFFF) >> 3;
             FRAMELOG("Clear EDSR\n");
             regs->EDSR = 0;
             return;
           }
      } else {
        yabsys.vdp1cycles += 16;
      }
      vdp1_clock -= yabsys.vdp1cycles;
      yabsys.vdp1cycles = 0;

      // Next, determine where to go next
      switch ((command & 0x3000) >> 12) {
      case 0: // NEXT, jump to following table
         regs->addr += 0x20;
         break;
      case 1: // ASSIGN, jump to CMDLINK
        {
          u32 oldAddr = regs->addr;
          regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
          if (((regs->addr == oldAddr) && (command & 0x4000)) || (regs->addr == 0))   {
            //The next adress is the same as the old adress and the command is skipped => Exit
            //The next adress is the start of the command list. It means the list has an infinte loop => Exit (used by Burning Rangers)
            //another example is Kanzen Chuukei Pro Yakyuu
            Vdp1LoopAddr = regs->addr + 2;
            regs->lCOPR = (regs->addr & 0x7FFFF) >> 3;
            FRAMELOG_CMD("Reset vdp1_clock addr = %x %d %d\n", regs->addr, yabsys.LineCount, __LINE__);
            vdp1_clock = 0;
            CmdListInLoop = 1;
            return;
          }
        }
         break;
      case 2: // CALL, call a subroutine
         if (returnAddr == 0xFFFFFFFF)
            returnAddr = regs->addr + 0x20;

         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
         break;
      case 3: // RETURN, return from subroutine
         if (returnAddr != 0xFFFFFFFF) {
            regs->addr = returnAddr;
            returnAddr = 0xFFFFFFFF;
         }
         else
            regs->addr += 0x20;
         break;
      }

      command = Vdp1RamReadWord(NULL,ram, regs->addr);
      FRAMELOG_CMD("Command is 0x%x @ 0x%x\n", command, regs->addr);
      //If we change directly CPR to last value, scorcher will not boot.
      //If we do not change it, Noon will not start
      //So store the value and update COPR with last value at VBlank In
      regs->lCOPR = (regs->addr & 0x7FFFF) >> 3;
   }
   if (command & 0x8000) {
     if (vdp1_clock >= 0) {
       FRAMELOG("VDP1: Command Finished! count = %d @ %08X\n", nbCmdToProcess, regs->addr);
       Vdp1External.status &= ~VDP1_STATUS_MASK;
       Vdp1External.status |= VDP1_STATUS_IDLE;
       endVdp1Render();
       regs->COPR = (regs->addr & 0x7FFFF) >> 3;
       regs->lCOPR = (regs->addr & 0x7FFFF) >> 3;
       FRAMELOG("Set EDSR\n");
       regs->EDSR |= 2;
       Vdp1LoopAddr = -1;
     } else {
       FRAMELOG("Wait a bit before the stop. Enough for the command to end\n");
     }
   }
}

//ensure that registers are set correctly
void Vdp1FakeDrawCommands(u8 * ram, Vdp1 * regs)
{
   u16 command = T1ReadWord(ram, regs->addr);
   u32 commandCounter = 0;
   u32 returnAddr = 0xffffffff;
   vdp1cmd_struct cmd;
   // Vdp1Regs->EDSR >>= 1;

   while (!(command & 0x8000) && commandCounter < 2000) { // fix me
      // First, process the command
      if (!(command & 0x4000)) { // if (!skip)
         switch (command & 0x000F) {
         case 0: // normal sprite draw
         case 1: // scaled sprite draw
         case 2: // distorted sprite draw
         case 3: /* this one should be invalid, but some games
                 (Hardcore 4x4 for instance) use it instead of 2 */
         case 4: // polygon draw
         case 5: // polyline draw
         case 6: // line draw
         case 7: // undocumented polyline draw mirror
         case 11: // undocumented command - do nnothing
            break;
         case 8: // user clipping coordinates
            Vdp1ReadCommand(&cmd, regs->addr, ram);
            VIDCore->Vdp1UserClipping(&cmd, ram, regs);
            break;
         case 9: // system clipping coordinates
            Vdp1ReadCommand(&cmd, regs->addr, ram);
            VIDCore->Vdp1SystemClipping(&cmd, ram, regs);
            break;
         case 10: // local coordinate
            Vdp1ReadCommand(&cmd, regs->addr, ram);
            VIDCore->Vdp1LocalCoordinate(&cmd, ram, regs);
            break;
         default: // Abort
            FRAMELOG("vdp1\t: Bad command: %x\n", command);
            // regs->EDSR |= 2;
            regs->COPR = regs->addr >> 3;
            return;
         }
      }

      // Next, determine where to go next
      switch ((command & 0x3000) >> 12) {
      case 0: // NEXT, jump to following table
         regs->addr += 0x20;
         break;
      case 1: // ASSIGN, jump to CMDLINK
         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
         break;
      case 2: // CALL, call a subroutine
         if (returnAddr == 0xFFFFFFFF)
            returnAddr = regs->addr + 0x20;

         regs->addr = T1ReadWord(ram, regs->addr + 2) * 8;
         break;
      case 3: // RETURN, return from subroutine
         if (returnAddr != 0xFFFFFFFF) {
            regs->addr = returnAddr;
            returnAddr = 0xFFFFFFFF;
         }
         else
            regs->addr += 0x20;
         break;
      }

      command = T1ReadWord(ram, regs->addr);
      commandCounter++;
   }
   if (command & 0x8000) {
     regs->EDSR |= 2;
   }
}

static int Vdp1Draw(void)
{
  FRAMELOG_CMD("Vdp1Draw %d\n", yabsys.LineCount);
  VIDCore->Vdp1Draw();
   if ((Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_IDLE) {
     FRAMELOG("Vdp1Draw end at line %d \n", yabsys.LineCount);
     ScuSendDrawEnd();
   }
   if ((Vdp1External.status&VDP1_STATUS_MASK) == VDP1_STATUS_IDLE) return 0;
   else return 1;
}

//////////////////////////////////////////////////////////////////////////////

static void Vdp1NoDraw(void) {
   Vdp1Regs->lCOPR = 0;
   Vdp1External.status &= ~VDP1_STATUS_MASK;
   Vdp1External.status |= VDP1_STATUS_IDLE;
   endVdp1Render();
   Vdp1FakeDrawCommands(Vdp1Ram, Vdp1Regs);
}

//////////////////////////////////////////////////////////////////////////////

static void FASTCALL Vdp1ReadCommand(vdp1cmd_struct *cmd, u32 addr, u8* ram) {
   cmd->CMDCTRL = T1ReadWord(ram, addr);
   cmd->CMDLINK = T1ReadWord(ram, addr + 0x2);
   cmd->CMDPMOD = T1ReadWord(ram, addr + 0x4);
   cmd->CMDCOLR = T1ReadWord(ram, addr + 0x6);
   cmd->CMDSRCA = T1ReadWord(ram, addr + 0x8);
   cmd->CMDSIZE = T1ReadWord(ram, addr + 0xA);
   cmd->CMDXA = T1ReadWord(ram, addr + 0xC);
   cmd->CMDYA = T1ReadWord(ram, addr + 0xE);
   cmd->CMDXB = T1ReadWord(ram, addr + 0x10);
   cmd->CMDYB = T1ReadWord(ram, addr + 0x12);
   cmd->CMDXC = T1ReadWord(ram, addr + 0x14);
   cmd->CMDYC = T1ReadWord(ram, addr + 0x16);
   cmd->CMDXD = T1ReadWord(ram, addr + 0x18);
   cmd->CMDYD = T1ReadWord(ram, addr + 0x1A);
   cmd->CMDGRDA = T1ReadWord(ram, addr + 0x1C);
}

//////////////////////////////////////////////////////////////////////////////

int Vdp1SaveState(void ** stream)
{
   int offset;
#ifdef IMPROVED_SAVESTATES
   int i = 0;
   u8 back_framebuffer[0x40000] = { 0 };
#endif

   offset = MemStateWriteHeader(stream, "VDP1", 2);

   // Write registers
   MemStateWrite((void *)Vdp1Regs, sizeof(Vdp1), 1, stream);

   // Write VDP1 ram
   MemStateWrite((void *)Vdp1Ram, 0x80000, 1, stream);

#ifdef IMPROVED_SAVESTATES
   for (i = 0; i < 0x40000; i++)
      back_framebuffer[i] = Vdp1FrameBuffer16bReadByte(NULL, NULL, i);

   MemStateWrite((void *)back_framebuffer, 0x40000, 1, stream);
#endif

    // VDP1 status
   int size = sizeof(Vdp1External_struct);
   MemStateWrite((void *)(&size), sizeof(int),1,stream);
   MemStateWrite((void *)(&Vdp1External), sizeof(Vdp1External_struct),1,stream);
   return MemStateFinishHeader(stream, offset);
}

//////////////////////////////////////////////////////////////////////////////

int Vdp1LoadState(const void * stream, UNUSED int version, int size)
{
#ifdef IMPROVED_SAVESTATES
   int i = 0;
   u8 back_framebuffer[0x40000] = { 0 };
#endif

   // Read registers
   MemStateRead((void *)Vdp1Regs, sizeof(Vdp1), 1, stream);

   // Read VDP1 ram
   MemStateRead((void *)Vdp1Ram, 0x80000, 1, stream);
   vdp1Ram_update_start = 0x0;
   vdp1Ram_update_end = 0x80000;
#ifdef IMPROVED_SAVESTATES
   MemStateRead((void *)back_framebuffer, 0x40000, 1, stream);

   YglGenerate();

   for (i = 0; i < 0x40000; i++)
      Vdp1FrameBuffer16bWriteByte(NULL, NULL, i, back_framebuffer[i]);
#endif
   if (version > 1) {
     int size = 0;
     MemStateRead((void *)(&size), sizeof(int), 1, stream);
     if (size == sizeof(Vdp1External_struct)) {
        MemStateRead((void *)(&Vdp1External), sizeof(Vdp1External_struct),1,stream);
     } else {
       YuiMsg("Too old savestate, can not restore Vdp1External\n");
       memset((void *)(&Vdp1External), 0, sizeof(Vdp1External_struct));
     }
   } else {
     YuiMsg("Too old savestate, can not restore Vdp1External\n");
     memset((void *)(&Vdp1External), 0, sizeof(Vdp1External_struct));
   }
   Vdp1External.updateVdp1Ram = 1;
   if (Vdp1Regs->TVMR & 0x1) switchFB8bit();
   else switchFB16bit();
   return size;
}

//////////////////////////////////////////////////////////////////////////////

static u32 Vdp1DebugGetCommandNumberAddr(u32 number)
{
   u32 addr = 0;
   u32 returnAddr = 0xFFFFFFFF;
   u32 commandCounter = 0;
   u16 command;

   command = T1ReadWord(Vdp1Ram, addr);

   while (!(command & 0x8000) && (commandCounter != number) && (commandCounter<2000))
   {
      // Determine where to go next
      switch ((command & 0x3000) >> 12)
      {
         case 0: // NEXT, jump to following table
            addr += 0x20;
            break;
         case 1: // ASSIGN, jump to CMDLINK
            addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
            break;
         case 2: // CALL, call a subroutine
            if (returnAddr == 0xFFFFFFFF)
               returnAddr = addr + 0x20;

            addr = T1ReadWord(Vdp1Ram, addr + 2) * 8;
            break;
         case 3: // RETURN, return from subroutine
            if (returnAddr != 0xFFFFFFFF) {
               addr = returnAddr;
               returnAddr = 0xFFFFFFFF;
            }
            else
               addr += 0x20;
            break;
      }

      if (addr > 0x7FFE0)
         return 0xFFFFFFFF;
      command = T1ReadWord(Vdp1Ram, addr);
      commandCounter++;
   }

   if (commandCounter == number)
      return addr;
   else
      return 0xFFFFFFFF;
}

//////////////////////////////////////////////////////////////////////////////

Vdp1CommandType Vdp1DebugGetCommandType(u32 number)
{
   u32 addr;
   if ((addr = Vdp1DebugGetCommandNumberAddr(number)) != 0xFFFFFFFF)
   {
      const u16 command = T1ReadWord(Vdp1Ram, addr);
      if (command & 0x8000)
        return VDPCT_DRAW_END;
      else if ((command & 0x000F) < VDPCT_INVALID)
        return (Vdp1CommandType) (command & 0x000F);
   }

   return VDPCT_INVALID;
}

u32 Vdp1DebugGetCommandAddr(u32 number) {
  return Vdp1DebugGetCommandNumberAddr(number);
}

char *Vdp1DebugGetCommandRaw(u32 addr)
{
   u16 command;
   char *out;

   if (addr == 0xFFFFFFFF)
      return NULL;

   out = (char*)malloc(128*sizeof(char));
   command = T1ReadWord(Vdp1Ram, addr);

   if (command & 0x8000) {
     snprintf(out, 128, "END");
     return out;
   }

   // Next, determine where to go next
   switch ((command & 0x3000) >> 12) {
   case 0: // NEXT, jump to following table
      snprintf(out, 128, "NEXT 0x%x", addr+0x20);
      break;
   case 1: // ASSIGN, jump to CMDLINK
      snprintf(out, 128, "ASSIGN 0x%x", T1ReadWord(Vdp1Ram, addr + 2) * 8);
      break;
   case 2: // CALL, call a subroutine
      snprintf(out, 128, "CALL 0x%x", T1ReadWord(Vdp1Ram, addr + 2) * 8);
      break;
   case 3: // RETURN, return from subroutine
      snprintf(out, 128, "RETURN");
      break;
   default:
      free(out);
      out = NULL;
   }
   return out;
}

char *Vdp1DebugGetCommandNumberName(u32 addr)
{
   u16 command;

   if (addr != 0xFFFFFFFF)
   {
      command = T1ReadWord(Vdp1Ram, addr);

      if (command & 0x8000)
         return "Draw End";

      // Figure out command name
      switch (command & 0x000F)
      {
         case 0:
            return "Normal Sprite";
         case 1:
            return "Scaled Sprite";
         case 2:
            return "Distorted Sprite";
         case 3:
            return "Distorted Sprite *";
         case 4:
            return "Polygon";
         case 5:
            return "Polyline";
         case 6:
            return "Line";
         case 7:
            return "Polyline *";
         case 8:
            return "User Clipping Coordinates";
         case 9:
            return "System Clipping Coordinates";
         case 10:
            return "Local Coordinates";
         case 11:
            return "Command 0xB undocumented";
         default:
             return "Bad command - Abort";
      }
   }
   else
      return NULL;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1DebugCommand(u32 number, char *outstring)
{
   u16 command;
   vdp1cmd_struct cmd;
   u32 addr;

   if ((addr = Vdp1DebugGetCommandNumberAddr(number)) == 0xFFFFFFFF)
      return;

   command = T1ReadWord(Vdp1Ram, addr);

   if (command & 0x8000)
   {
      // Draw End
      outstring[0] = 0x00;
      return;
   }

   if (command & 0x4000)
   {
      AddString(outstring, "Command is skipped\r\n");
      return;
   }

   Vdp1ReadCommand(&cmd, addr, Vdp1Ram);

   if ((cmd.CMDCTRL & 0x000F) < 4) {
     cmd.w = ((cmd.CMDSIZE >> 8) & 0x3F) * 8;
     cmd.h = cmd.CMDSIZE & 0xFF;
   }

   int invalid = 0;
   switch (cmd.CMDCTRL & 0x000F)
   {
      case 0:
         AddString(outstring, "Normal Sprite\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x = %d, y = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA);
           int cycles = getNormalCycles(&cmd);
           AddString(outstring, "estimated cycles=%d\n", cycles);
         }

         break;
      case 1:
         AddString(outstring, "Scaled Sprite\r\n");

         AddString(outstring, "Zoom Point: ");

         switch ((cmd.CMDCTRL >> 8) & 0xF)
         {
            case 0x0:
               AddString(outstring, "Only two coordinates\r\n");
               invalid = CONVERTCMD(&cmd.CMDXC) || CONVERTCMD(&cmd.CMDYC);
               break;
            case 0x5:
               AddString(outstring, "Upper-left\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0x6:
               AddString(outstring, "Upper-center\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0x7:
               AddString(outstring, "Upper-right\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0x9:
               AddString(outstring, "Center-left\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0xA:
               AddString(outstring, "Center-center\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0xB:
               AddString(outstring, "Center-right\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0xC:
               AddString(outstring, "Lower-left\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0xE:
               AddString(outstring, "Lower-center\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            case 0xF:
               AddString(outstring, "Lower-right\r\n");
               invalid = CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
               break;
            default: break;
         }

         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         AddString(outstring, "CMDXC = 0x%04x, CMDYC = 0x%04x\r\n", cmd.CMDXC&0xFFFF, cmd.CMDYC&0xFFFF);
         invalid |= CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           s16 x = cmd.CMDXA;
           s16 y = cmd.CMDYA;
           s16 rh = 0;
           s16 rw = 0;
           // Setup Zoom Point
           switch ((cmd.CMDCTRL & 0xF00) >> 8)
           {
           case 0x0: // Only two coordinates
             rw = cmd.CMDXC - cmd.CMDXA;
             rh = cmd.CMDYC - cmd.CMDYA;
             break;
           case 0x5: // Upper-left
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           case 0x6: // Upper-Center
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             x = x - rw / 2;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           case 0x7: // Upper-Right
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             x = x - rw;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           case 0x9: // Center-left
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             y = y - rh / 2;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           case 0xA: // Center-center
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             x = x - rw / 2;
             y = y - rh / 2;
             break;
           case 0xB: // Center-right
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             x = x - rw;
             y = y - rh / 2;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           case 0xD: // Lower-left
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             y = y - rh;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           case 0xE: // Lower-center
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             x = x - rw / 2;
             y = y - rh;
             break;
           case 0xF: // Lower-right
             rw = cmd.CMDXB;
             rh = cmd.CMDYB;
             x = x - rw;
             y = y - rh;
             if ((rw < 0)||(rh <0)) {
               return;
             }
             break;
           default: break;
           }
           cmd.CMDXA = x;
           cmd.CMDYA = y;
           cmd.CMDXB = x + rw;
           cmd.CMDYB = y;
           cmd.CMDXC = x + rw;
           cmd.CMDYC = y + rh;
           cmd.CMDXD = x;
           cmd.CMDYD = y + rh;
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
           AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC, (s16)cmd.CMDXD, (s16)cmd.CMDYD);
           int cycles = getScaledCycles(&cmd);
           AddString(outstring, "estimated cycles=%d\n", cycles);
         }
         break;
      case 2:
         AddString(outstring, "Distorted Sprite\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         AddString(outstring, "CMDXC = 0x%04x, CMDYC = 0x%04x\r\n", cmd.CMDXC&0xFFFF, cmd.CMDYC&0xFFFF);
         AddString(outstring, "CMDXD = 0x%04x, CMDYD = 0x%04x\r\n", cmd.CMDXD&0xFFFF, cmd.CMDYD&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         invalid |= CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
         invalid |= CONVERTCMD(&cmd.CMDXC) || CONVERTCMD(&cmd.CMDYC);
         invalid |= CONVERTCMD(&cmd.CMDXD) || CONVERTCMD(&cmd.CMDYD);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
           AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC, (s16)cmd.CMDXD, (s16)cmd.CMDYD);
           int cycles = getDistortedCycles(&cmd);
           AddString(outstring, "estimated cycles=%d\n", cycles);
         }
         break;
      case 3:
         AddString(outstring, "Distorted Sprite *\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         AddString(outstring, "CMDXC = 0x%04x, CMDYC = 0x%04x\r\n", cmd.CMDXC&0xFFFF, cmd.CMDYC&0xFFFF);
         AddString(outstring, "CMDXD = 0x%04x, CMDYD = 0x%04x\r\n", cmd.CMDXD&0xFFFF, cmd.CMDYD&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         invalid |= CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
         invalid |= CONVERTCMD(&cmd.CMDXC) || CONVERTCMD(&cmd.CMDYC);
         invalid |= CONVERTCMD(&cmd.CMDXD) || CONVERTCMD(&cmd.CMDYD);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
           AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC, (s16)cmd.CMDXD, (s16)cmd.CMDYD);
           int cycles = getDistortedCycles(&cmd);
           AddString(outstring, "estimated cycles=%d\n", cycles);
         }
         break;
      case 4:
         AddString(outstring, "Polygon\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         AddString(outstring, "CMDXC = 0x%04x, CMDYC = 0x%04x\r\n", cmd.CMDXC&0xFFFF, cmd.CMDYC&0xFFFF);
         AddString(outstring, "CMDXD = 0x%04x, CMDYD = 0x%04x\r\n", cmd.CMDXD&0xFFFF, cmd.CMDYD&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         invalid |= CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
         invalid |= CONVERTCMD(&cmd.CMDXC) || CONVERTCMD(&cmd.CMDYC);
         invalid |= CONVERTCMD(&cmd.CMDXD) || CONVERTCMD(&cmd.CMDYD);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
           AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC, (s16)cmd.CMDXD, (s16)cmd.CMDYD);
           int cycles = getPolygonCycles(&cmd);
           AddString(outstring, "estimated cycles=%d\n", cycles);
         }
         break;
      case 5:
         AddString(outstring, "Polyline\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         AddString(outstring, "CMDXC = 0x%04x, CMDYC = 0x%04x\r\n", cmd.CMDXC&0xFFFF, cmd.CMDYC&0xFFFF);
         AddString(outstring, "CMDXD = 0x%04x, CMDYD = 0x%04x\r\n", cmd.CMDXD&0xFFFF, cmd.CMDYD&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         invalid |= CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
         invalid |= CONVERTCMD(&cmd.CMDXC) || CONVERTCMD(&cmd.CMDYC);
         invalid |= CONVERTCMD(&cmd.CMDXD) || CONVERTCMD(&cmd.CMDYD);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
           AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC, (s16)cmd.CMDXD, (s16)cmd.CMDYD);
         }
         break;
      case 6:
         AddString(outstring, "Line\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         invalid |= CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
         }
         break;
      case 7:
         AddString(outstring, "Polyline *\r\n");
         AddString(outstring, "CMDXA = 0x%04x, CMDYA = 0x%04x\r\n", cmd.CMDXA&0xFFFF, cmd.CMDYA&0xFFFF);
         AddString(outstring, "CMDXB = 0x%04x, CMDYB = 0x%04x\r\n", cmd.CMDXB&0xFFFF, cmd.CMDYB&0xFFFF);
         AddString(outstring, "CMDXC = 0x%04x, CMDYC = 0x%04x\r\n", cmd.CMDXC&0xFFFF, cmd.CMDYC&0xFFFF);
         AddString(outstring, "CMDXD = 0x%04x, CMDYD = 0x%04x\r\n", cmd.CMDXD&0xFFFF, cmd.CMDYD&0xFFFF);
         invalid = CONVERTCMD(&cmd.CMDXA) || CONVERTCMD(&cmd.CMDYA);
         invalid |= CONVERTCMD(&cmd.CMDXB) || CONVERTCMD(&cmd.CMDYB);
         invalid |= CONVERTCMD(&cmd.CMDXC) || CONVERTCMD(&cmd.CMDYC);
         invalid |= CONVERTCMD(&cmd.CMDXD) || CONVERTCMD(&cmd.CMDYD);
         if (invalid) {
           AddString(outstring, "Invalid coordinates - Not drawn\n");
         } else {
           AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXB, (s16)cmd.CMDYB);
           AddString(outstring, "x3 = %d, y3 = %d, x4 = %d, y4 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC, (s16)cmd.CMDXD, (s16)cmd.CMDYD);
         }
         break;
      case 8:
         AddString(outstring, "User Clipping\r\n");
         AddString(outstring, "x1 = %d, y1 = %d, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA, (s16)cmd.CMDXC, (s16)cmd.CMDYC);
         break;
      case 9:
         AddString(outstring, "System Clipping\r\n");
         AddString(outstring, "x1 = 0, y1 = 0, x2 = %d, y2 = %d\r\n", (s16)cmd.CMDXC, (s16)cmd.CMDYC);
         break;
      case 10:
         AddString(outstring, "Local Coordinates\r\n");
         AddString(outstring, "x = %d, y = %d\r\n", (s16)cmd.CMDXA, (s16)cmd.CMDYA);
         break;
      default:
         AddString(outstring, "Invalid command\r\n");
         return;
   }

   // Only Sprite commands use CMDSRCA, CMDSIZE
   if (!(cmd.CMDCTRL & 0x000C))
   {
      AddString(outstring, "Texture address = %08X\r\n", ((unsigned int)cmd.CMDSRCA) << 3);
      AddString(outstring, "Texture width = %d, height = %d\r\n", MAX(1, (cmd.CMDSIZE & 0x3F00) >> 5), MAX(1,cmd.CMDSIZE & 0xFF));
      if ((((cmd.CMDSIZE & 0x3F00) >> 5)==0) || ((cmd.CMDSIZE & 0xFF)==0)) AddString(outstring, "Texture malformed \r\n");
      AddString(outstring, "Texture read direction: ");

      switch ((cmd.CMDCTRL >> 4) & 0x3)
      {
         case 0:
            AddString(outstring, "Normal\r\n");
            break;
         case 1:
            AddString(outstring, "Reversed horizontal\r\n");
            break;
         case 2:
            AddString(outstring, "Reversed vertical\r\n");
            break;
         case 3:
            AddString(outstring, "Reversed horizontal and vertical\r\n");
            break;
         default: break;
      }
   }

   // Only draw commands use CMDPMOD
   if (!(cmd.CMDCTRL & 0x0008))
   {
      if (cmd.CMDPMOD & 0x8000)
      {
         AddString(outstring, "MSB set\r\n");
      }

      if (cmd.CMDPMOD & 0x1000)
      {
         AddString(outstring, "High Speed Shrink Enabled\r\n");
      }

      if (!(cmd.CMDPMOD & 0x0800))
      {
         AddString(outstring, "Pre-clipping Enabled\r\n");
      }

      if (cmd.CMDPMOD & 0x0400)
      {
         AddString(outstring, "User Clipping Enabled\r\n");
         AddString(outstring, "Clipping Mode = %d\r\n", (cmd.CMDPMOD >> 9) & 0x1);
      }

      if (cmd.CMDPMOD & 0x0100)
      {
         AddString(outstring, "Mesh Enabled\r\n");
      }

      if (!(cmd.CMDPMOD & 0x0080))
      {
         AddString(outstring, "End Code Enabled\r\n");
      }

      if (!(cmd.CMDPMOD & 0x0040))
      {
         AddString(outstring, "Transparent Pixel Enabled\r\n");
      }

      if (cmd.CMDCTRL & 0x0004){
          AddString(outstring, "Non-textured color: %04X\r\n", cmd.CMDCOLR);
      } else {
          AddString(outstring, "Color mode: ");

          switch ((cmd.CMDPMOD >> 3) & 0x7)
          {
             case 0:
                AddString(outstring, "4 BPP(16 color bank)\r\n");
                AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR));
                break;
             case 1:
                AddString(outstring, "4 BPP(16 color LUT)\r\n");
                AddString(outstring, "Color lookup table: %08X\r\n", (cmd.CMDCOLR));
                break;
             case 2:
                AddString(outstring, "8 BPP(64 color bank)\r\n");
                AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR));
                break;
             case 3:
                AddString(outstring, "8 BPP(128 color bank)\r\n");
                AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR));
                break;
             case 4:
                AddString(outstring, "8 BPP(256 color bank)\r\n");
                AddString(outstring, "Color bank: %08X\r\n", (cmd.CMDCOLR));
                break;
             case 5:
                AddString(outstring, "15 BPP(RGB)\r\n");
                break;
             default: break;
          }
        }

      AddString(outstring, "Color Calc. mode: ");

      switch (cmd.CMDPMOD & 0x7)
      {
         case 0:
            AddString(outstring, "Replace\r\n");
            break;
         case 1:
            AddString(outstring, "Cannot overwrite/Shadow\r\n");
            break;
         case 2:
            AddString(outstring, "Half-luminance\r\n");
            break;
         case 3:
            AddString(outstring, "Replace/Half-transparent\r\n");
            break;
         case 4:
            AddString(outstring, "Gouraud Shading\r\n");
            AddString(outstring, "Gouraud Shading Table = %08X\r\n", ((unsigned int)cmd.CMDGRDA) << 3);
            break;
         case 6:
            AddString(outstring, "Gouraud Shading + Half-luminance\r\n");
            AddString(outstring, "Gouraud Shading Table = %08X\r\n", ((unsigned int)cmd.CMDGRDA) << 3);
            break;
         case 7:
            AddString(outstring, "Gouraud Shading/Gouraud Shading + Half-transparent\r\n");
            AddString(outstring, "Gouraud Shading Table = %08X\r\n", ((unsigned int)cmd.CMDGRDA) << 3);
            break;
         default: break;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

static u32 ColorRamGetColor(u32 colorindex)
{
   switch(Vdp2Internal.ColorMode)
   {
      case 0:
      case 1:
      {
         u32 tmp;
         colorindex <<= 1;
         tmp = T2ReadWord(Vdp2ColorRam, colorindex & 0xFFF);
         return SAT2YAB1(0xFF, tmp);
      }
      case 2:
      {
         u32 tmp1, tmp2;
         colorindex <<= 2;
         colorindex &= 0xFFF;
         tmp1 = T2ReadWord(Vdp2ColorRam, colorindex);
         tmp2 = T2ReadWord(Vdp2ColorRam, colorindex+2);
         return SAT2YAB2(0xFF, tmp1, tmp2);
      }
      default: break;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static INLINE int CheckEndcode(int dot, int endcode, int *code)
{
   if (dot == endcode)
   {
      code[0]++;
      if (code[0] == 2)
      {
         code[0] = 0;
         return 2;
      }
      return 1;
   }

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static INLINE int DoEndcode(int count, u32 *charAddr, u32 **textdata, int width, int xoff, int oddpixel, int pixelsize)
{
   if (count > 1)
   {
      float divisor = (float)(8 / pixelsize);

      if(divisor != 0)
         charAddr[0] += (int)((float)(width - xoff + oddpixel) / divisor);
      memset(textdata[0], 0, sizeof(u32) * (width - xoff));
      textdata[0] += (width - xoff);
      return 1;
   }
   else
      *textdata[0]++ = 0;

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 *Vdp1DebugTexture(u32 number, int *w, int *h)
{
   u16 command;
   vdp1cmd_struct cmd;
   u32 addr;
   u32 *texture;
   u32 charAddr;
   u32 dot;
   u8 SPD;
   u32 alpha;
   u32 *textdata;
   int isendcode=0;
   int code=0;
   int ret;

   if ((addr = Vdp1DebugGetCommandNumberAddr(number)) == 0xFFFFFFFF)
      return NULL;

   command = T1ReadWord(Vdp1Ram, addr);

   if (command & 0x8000)
      // Draw End
      return NULL;

   if (command & 0x4000)
      // Command Skipped
      return NULL;

   Vdp1ReadCommand(&cmd, addr, Vdp1Ram);

   switch (cmd.CMDCTRL & 0x000F)
   {
      case 0: // Normal Sprite
      case 1: // Scaled Sprite
      case 2: // Distorted Sprite
      case 3: // Distorted Sprite *
         w[0] = MAX(1, (cmd.CMDSIZE & 0x3F00) >> 5);
         h[0] = MAX(1, cmd.CMDSIZE & 0xFF);

         if ((texture = (u32 *)malloc(sizeof(u32) * w[0] * h[0])) == NULL)
            return NULL;

         if (!(cmd.CMDPMOD & 0x80))
         {
            isendcode = 1;
            code = 0;
         }
         else
            isendcode = 0;
         break;
      case 4: // Polygon
      case 5: // Polyline
      case 6: // Line
      case 7: // Polyline *
         // Do 1x1 pixel
         w[0] = 1;
         h[0] = 1;
         if ((texture = (u32 *)malloc(sizeof(u32))) == NULL)
            return NULL;

         if (cmd.CMDCOLR & 0x8000)
            texture[0] = SAT2YAB1(0xFF, cmd.CMDCOLR);
         else
            texture[0] = ColorRamGetColor(cmd.CMDCOLR);

         return texture;
      case 8: // User Clipping
      case 9: // System Clipping
      case 10: // Local Coordinates
      case 11: // undocumented
         return NULL;
      default: // Invalid command
         return NULL;
   }

   charAddr = cmd.CMDSRCA * 8;
   SPD = ((cmd.CMDPMOD & 0x40) != 0);
   alpha = 0xFF;
   textdata = texture;

   switch((cmd.CMDPMOD >> 3) & 0x7)
   {
      case 0:
      {
         // 4 bpp Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;
         u16 i;

         for(i = 0;i < h[0];i++)
         {
            u16 j;
            j = 0;
            while(j < w[0])
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF);

               // Pixel 1
               if (isendcode && (ret = CheckEndcode(dot >> 4, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 0, 4))
                     break;
               }
               else
               {
                  if (((dot >> 4) == 0) && !SPD) *textdata++ = 0;
                  else *textdata++ = ColorRamGetColor(((dot >> 4) | colorBank) + colorOffset);
               }

               j += 1;

               // Pixel 2
               if (isendcode && (ret = CheckEndcode(dot & 0xF, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 1, 4))
                     break;
               }
               else
               {
                  if (((dot & 0xF) == 0) && !SPD) *textdata++ = 0;
                  else *textdata++ = ColorRamGetColor(((dot & 0xF) | colorBank) + colorOffset);
               }

               j += 1;
               charAddr += 1;
            }
         }
         break;
      }
      case 1:
      {
         // 4 bpp LUT mode
         u32 temp;
         u32 colorLut = cmd.CMDCOLR * 8;
         u16 i;

         for(i = 0;i < h[0];i++)
         {
            u16 j;
            j = 0;
            while(j < w[0])
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF);

               if (isendcode && (ret = CheckEndcode(dot >> 4, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 0, 4))
                     break;
               }
               else
               {
                  if (((dot >> 4) == 0) && !SPD)
                     *textdata++ = 0;
                  else
                  {
                     temp = T1ReadWord(Vdp1Ram, ((dot >> 4) * 2 + colorLut) & 0x7FFFF);
                     if (temp & 0x8000)
                        *textdata++ = SAT2YAB1(0xFF, temp);
                     else
                        *textdata++ = ColorRamGetColor(temp);
                  }
               }

               j += 1;

               if (isendcode && (ret = CheckEndcode(dot & 0xF, 0xF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 1, 4))
                     break;
               }
               else
               {
                  if (((dot & 0xF) == 0) && !SPD)
                     *textdata++ = 0;
                  else
                  {
                     temp = T1ReadWord(Vdp1Ram, ((dot & 0xF) * 2 + colorLut) & 0x7FFFF);
                     if (temp & 0x8000)
                        *textdata++ = SAT2YAB1(0xFF, temp);
                     else
                        *textdata++ = ColorRamGetColor(temp);
                  }
               }

               j += 1;

               charAddr += 1;
            }
         }
         break;
      }
      case 2:
      {
         // 8 bpp(64 color) Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;

         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF) & 0x3F;
               charAddr++;

               if ((dot == 0) && !SPD) *textdata++ = 0;
               else *textdata++ = ColorRamGetColor((dot | colorBank) + colorOffset);
            }
         }
         break;
      }
      case 3:
      {
         // 8 bpp(128 color) Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;
         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF) & 0x7F;
               charAddr++;

               if ((dot == 0) && !SPD) *textdata++ = 0;
               else *textdata++ = ColorRamGetColor((dot | colorBank) + colorOffset);
            }
         }
         break;
      }
      case 4:
      {
         // 8 bpp(256 color) Bank mode
         u32 colorBank = cmd.CMDCOLR;
         u32 colorOffset = (Vdp2Regs->CRAOFB & 0x70) << 4;
         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadByte(Vdp1Ram, charAddr & 0x7FFFF);
               charAddr++;

               if ((dot == 0) && !SPD) *textdata++ = 0;
               else *textdata++ = ColorRamGetColor((dot | colorBank) + colorOffset);
            }
         }
         break;
      }
      case 5:
      {
         // 16 bpp Bank mode
         u16 i, j;

         for(i = 0;i < h[0];i++)
         {
            for(j = 0;j < w[0];j++)
            {
               dot = T1ReadWord(Vdp1Ram, charAddr & 0x7FFFF);

               if (isendcode && (ret = CheckEndcode(dot, 0x7FFF, &code)) > 0)
               {
                  if (DoEndcode(ret, &charAddr, &textdata, w[0], j, 0, 16))
                     break;
               }
               else
               {
                  //if (!(dot & 0x8000) && (Vdp2Regs->SPCTL & 0x20)) printf("mixed mode\n");
                  if (!(dot & 0x8000) && !SPD) *textdata++ = 0;
                  else *textdata++ = SAT2YAB1(0xFF, dot);
               }

               charAddr += 2;
            }
         }
         break;
      }
      default:
         break;
   }

   return texture;
}

u8 *Vdp1DebugRawTexture(u32 cmdNumber, int *width, int *height, int *numBytes)
{
   u16 cmdRaw;
   vdp1cmd_struct cmd;
   u32 cmdAddress;
   u8 *texture = NULL;

   // Initial number of bytes written to texture
   *numBytes = 0;

   if ((cmdAddress = Vdp1DebugGetCommandNumberAddr(cmdNumber)) == 0xFFFFFFFF)
      return NULL;

   cmdRaw = T1ReadWord(Vdp1Ram, cmdAddress);

   if (cmdRaw & 0x8000)
      // Draw End
      return NULL;

   if (cmdRaw & 0x4000)
      // Command Skipped
      return NULL;

   Vdp1ReadCommand(&cmd, cmdAddress, Vdp1Ram);

   const int spriteCmdType = ((cmd.CMDPMOD >> 3) & 0x7);
   switch (cmd.CMDCTRL & 0x000F)
   {
      case 0: // Normal Sprite
      case 1: // Scaled Sprite
      case 2: // Distorted Sprite
      case 3: // Distorted Sprite *
         width[0] = (cmd.CMDSIZE & 0x3F00) >> 5;
         height[0] = cmd.CMDSIZE & 0xFF;

         switch (spriteCmdType) {
            // 0: 4 bpp Bank mode
            // 1: 4 bpp LUT mode
            case 0:
            case 1:
               numBytes[0] = 0.5 * width[0] * height[0];
               texture = (u8*) malloc(numBytes[0]);
               break;
            // 2: 8 bpp(64 color) Bank mode
            // 3: 8 bpp(128 color) Bank mode
            // 4: 8 bpp(256 color) Bank mode
            case 2:
            case 3:
            case 4:
               numBytes[0] = width[0] * height[0];
               texture = (u8*) malloc(numBytes[0]);
               break;
            // 5: 16 bpp Bank mode
            case 5:
               numBytes[0] = 2 * width[0] * height[0];
               texture = (u8*) malloc(numBytes[0]);
               break;
            default:
               texture = NULL;
               break;
         }

         if (texture == NULL)
            return NULL;

         break;
      case 4: // Polygon
      case 5: // Polyline
      case 6: // Line
      case 7: // Polyline *
         // Do 1x1 pixel
         width[0] = 1;
         height[0] = 1;
         texture = (u8*) malloc(sizeof(u16));

         if (texture == NULL)
            return NULL;

         *numBytes = 2;
         memcpy(texture, &cmd.CMDCOLR, sizeof(u16));
         return texture;
      case 8:  // User Clipping
      case 9:  // System Clipping
      case 10: // Local Coordinates
      case 11: // Undocumented
         return NULL;
      default: // Invalid command
         return NULL;
   }

   // Read texture data directly from VRAM.
   for (u32 i = 0; i < *numBytes; ++i)
   {
     texture[ i ] = T1ReadByte(Vdp1Ram, ((cmd.CMDSRCA * 8) + i) & 0x7FFFF);
   }

   return texture;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleVDP1(void)
{
   Vdp1External.disptoggle ^= 1;
}
//////////////////////////////////////////////////////////////////////////////

static int getVdp1ErasePixelLine() {
  int shift = ((Vdp1Regs->TVMR & 0x1) == 1)?4:3;
  int limits[4] = {0};
  limits[0] = ((Vdp1Regs->EWLR>>9)&0x3F)<<shift;
  limits[1] = ((Vdp1Regs->EWLR)&0x1FF); //TODO: manage double interlace
  limits[2] = (((Vdp1Regs->EWRR>>9)&0x7F)<<shift) - 1;
  limits[3] = ((Vdp1Regs->EWRR)&0x1FF); //TODO: manage double interlace

  //Prohibited value - Example Quake first screens
  if ((limits[2] == -1)||(limits[3] == 0)) return 0;

  if ((limits[0]>=limits[2])||(limits[1]>limits[3])) {
    return 0; //No erase write when invalid area - Should be done only for one dot but no idea of which dot it shall be
  }
  int nbPix = ((limits[2]-limits[0])*(limits[3]-limits[1]))>>(Vdp1Regs->TVMR & 0x1);
  if (yabsys.IsPal == 0) {
    return nbPix/(1820 - 200);
  } else {
    return nbPix/(1708 - 200);
  }
}

static void Vdp1EraseWrite(int id){
  lastHash = -1;
  _Ygl->shallVdp1Erase[id] = 1;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1HBlankIN(void)
{
  int changeDelay = 0;
  if (yabsys.LineCount == (yabsys.VBlankLineCount + 1)) {
    //First HBlankIn after VBlankIn // Evaluate erase
    updateFBCRVBE();
    int eraseId = 0;
    if (_Ygl != NULL) eraseId = _Ygl->readframe;
    if (Vdp1External.useVBlankErase != 0) {
      //Vblank time - VBE on, erase read frame
      FRAMELOG("##### VBlank on %d (%d %d)\n", eraseId, yabsys.LineCount, yabsys.DecilineCount);
      Vdp1EraseWrite(eraseId);
      changeDelay = getVdp1ErasePixelLine();
      if ((yabsys.VBlankLineCount + 1 + changeDelay) > (yabsys.MaxLineCount - 1)) {
        changeDelay = yabsys.MaxLineCount - 1 - (yabsys.VBlankLineCount + 1);
      }
      updateFBCRChange();
    }
  }
  if (yabsys.LineCount == (yabsys.MaxLineCount - 1)) {

    FRAMELOG("### Update FBCR for next field %d %d\n", yabsys.LineCount, yabsys.DecilineCount);
    updateFBCRErase();
    updateFBCRChange();
    int swap_frame_buffer = (Vdp1External.manualchange == 1);
    swap_frame_buffer |= (Vdp1External.onecyclechange == 1);
    // Frame Change
    if (swap_frame_buffer == 1)
    {
      addVdp1Framecount();
      FRAMELOG("####Swap Line %d (v=%d,m=%d)\n", yabsys.LineCount , yabsys.VBlankLineCount, yabsys.MaxLineCount);
      lastHash = -1;
      Vdp1SwitchFrame();
    }
    Vdp1External.manualchange = 0;

    int eraseId = 0;
    if (_Ygl != NULL) eraseId = _Ygl->readframe;
    //Vblank time - VBE of, erase read frame if erase mode or oncyclemode
    if ((Vdp1External.manualerase == 1) || (Vdp1External.onecycleerase == 1))
    {
      FRAMELOG("########frame %d was erased in this field\n", eraseId);
      Vdp1EraseWrite(eraseId);
    }
    Vdp1External.manualerase = 0;
  }
  int needToCompose = 0;

  int cyclesPerLine  = getVdp1CyclesPerLine();
  if (vdp1_clock > 0) vdp1_clock = 0;
  vdp1_clock += cyclesPerLine;
  Vdp1TryDraw();
}
//////////////////////////////////////////////////////////////////////////////

void Vdp1StartVisibleLine(void)
{
}

//////////////////////////////////////////////////////////////////////////////
void Vdp1VBlankIN(void)
{
}
void Vdp1VBlankOUT(void) {
  //at field change, frame is changing in case of VblankErase - one cyclemode or manualchange
  // if (yabsys.LineCount == (yabsys.MaxLineCount - 1)) {
    //First blankin after VBlankOut
    //Evaluate FBCR
  // }
}

void Vdp1VBlankIN_It(void)
{
  FRAMELOG("VBLANKIn line %d (%d)\n", yabsys.LineCount, yabsys.DecilineCount);
  checkFBSync();
}

void Vdp1SwitchFrame(void)
{
  FRAMELOG("Change frames before draw %d, read %d (%d)\n", _Ygl->drawframe, _Ygl->readframe, yabsys.LineCount);
  checkFBSync();
  FRAMELOG("Switch Frame change VDP1 %d(%d)\n", yabsys.LineCount, yabsys.DecilineCount);
  VIDCore->Vdp1FrameChange();
  FRAMELOG("Change frames now draw %d, read %d (%d)\n", _Ygl->drawframe, _Ygl->readframe, yabsys.LineCount);
  Vdp1External.current_frame = !Vdp1External.current_frame;
  Vdp1Regs->LOPR = Vdp1Regs->COPR;
  Vdp1Regs->COPR = 0;
  Vdp1Regs->lCOPR = 0;

  if (Vdp1Regs->PTMR == 0x2) {
    FRAMELOG("[VDP1] PTMR == 0x2 start drawing immidiatly %d %d EDSR %x PTMR %x\n", yabsys.LineCount, yabsys.DecilineCount, Vdp1Regs->EDSR,  Vdp1Regs->PTMR);
    int cylesPerLine = getVdp1CyclesPerLine();
    checkFBSync();
    abortVdp1();
    vdp1_clock = (vdp1_clock + cylesPerLine)%(cylesPerLine+1);
    RequestVdp1ToDraw();
    // Vdp1TryDraw();
  } else {
    Vdp1Regs->EDSR >>= 1;
  }
}
