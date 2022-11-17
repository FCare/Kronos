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
#include "vidsoft.h"
#include "threads.h"
#include "sh2core.h"
#include "ygl.h"
#include "yui.h"


// #define DEBUG_CMD_LIST
#define DEBUG_BAD_COORD //YuiMsg

#define  CONVERTCMD(A) {\
  s32 toto = (A);\
  if (((A)&0x7000) != 0) (A) |= 0xF000;\
  else (A) &= ~0xF800;\
  ((A) = (s32)(s16)(A));\
  if (((A)) < -1024) { DEBUG_BAD_COORD("Bad(-1024) %x (%d, 0x%x)\n", (A), (A), toto);}\
  if (((A)) > 1023) { DEBUG_BAD_COORD("Bad(1023) %x (%d, 0x%x)\n", (A), (A), toto);}\
}

extern void addVdp1Framecount ();

u8 * Vdp1Ram;
int vdp1Ram_update_start;
int vdp1Ram_update_end;
int VDP1_MASK = 0xFFFF;

Vdp1 * Vdp1Regs;
Vdp1External_struct Vdp1External;
vdp1cmdctrl_struct cmdBufferBeingProcessed[CMD_QUEUE_SIZE];

int vdp1_clock = 0;

#include "vdp1static.c"

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

void FASTCALL Vdp1RamWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
   addr &= 0x7FFFF;
   Vdp1External.updateVdp1Ram = 1;
   if( Vdp1External.status == VDP1_STATUS_RUNNING) vdp1_clock -= 1;
   if (vdp1Ram_update_start > addr) vdp1Ram_update_start = addr;
   if (vdp1Ram_update_end < addr+1) vdp1Ram_update_end = addr + 1;
   T1WriteByte(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1RamWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
   addr &= 0x7FFFF;
   Vdp1External.updateVdp1Ram = 1;
   if( Vdp1External.status == VDP1_STATUS_RUNNING) vdp1_clock -= 2;
   if (vdp1Ram_update_start > addr) vdp1Ram_update_start = addr;
   if (vdp1Ram_update_end < addr+2) vdp1Ram_update_end = addr + 2;
   T1WriteWord(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1RamWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
   addr &= 0x7FFFF;
   Vdp1External.updateVdp1Ram = 1;
   if( Vdp1External.status == VDP1_STATUS_RUNNING) vdp1_clock -= 4;
   if (vdp1Ram_update_start > addr) vdp1Ram_update_start = addr;
   if (vdp1Ram_update_end < addr+4) vdp1Ram_update_end = addr + 4;
   T1WriteLong(mem, addr, val);
}

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Vdp1FrameBufferReadByte(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   if (VIDCore->Vdp1ReadFrameBuffer){
     u8 val;
     VIDCore->Vdp1ReadFrameBuffer(0, addr, &val);
     return val;
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Vdp1FrameBufferReadWord(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   if (VIDCore->Vdp1ReadFrameBuffer){
     u16 val;
     VIDCore->Vdp1ReadFrameBuffer(1, addr, &val);
     return val;
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Vdp1FrameBufferReadLong(SH2_struct *context, u8* mem, u32 addr) {
   addr &= 0x3FFFF;
   if (VIDCore->Vdp1ReadFrameBuffer){
     u32 val;
     VIDCore->Vdp1ReadFrameBuffer(2, addr, &val);
     return val;
   }
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBufferWriteByte(SH2_struct *context, u8* mem, u32 addr, u8 val) {
   addr &= 0x7FFFF;

   if (VIDCore->Vdp1WriteFrameBuffer)
   {
      if (addr < 0x40000) VIDCore->Vdp1WriteFrameBuffer(0, addr, val);
      return;
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBufferWriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
  addr &= 0x7FFFF;

   if (VIDCore->Vdp1WriteFrameBuffer)
   {
      if (addr < 0x40000) VIDCore->Vdp1WriteFrameBuffer(1, addr, val);
      return;
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Vdp1FrameBufferWriteLong(SH2_struct *context, u8* mem, u32 addr, u32 val) {
  addr &= 0x7FFFF;

   if (VIDCore->Vdp1WriteFrameBuffer)
   {
     if (addr < 0x40000) VIDCore->Vdp1WriteFrameBuffer(2, addr, val);
     return;
   }
}

//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////

int Vdp1Init(void) {
   if ((Vdp1Regs = (Vdp1 *) malloc(sizeof(Vdp1))) == NULL)
      return -1;

   if ((Vdp1Ram = T1MemoryInit(0x80000)) == NULL)
      return -1;

   Vdp1External.disptoggle = 1;

   Vdp1Regs->regs.TVMR = 0;
   Vdp1Regs->regs.FBCR = 0;
   Vdp1Regs->regs.PTMR = 0;

   Vdp1Regs->userclipX1=0;
   Vdp1Regs->userclipY1=0;
   Vdp1Regs->userclipX2=1024;
   Vdp1Regs->userclipY2=512;

   Vdp1Regs->localX=0;
   Vdp1Regs->localY=0;

   VDP1_MASK = 0xFFFF;

   vdp1Ram_update_start = 0x80000;
   vdp1Ram_update_end = 0x0;

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

void Vdp1Reset(void) {
   Vdp1Regs->regs.PTMR = 0;
   Vdp1Regs->regs.MODR = 0x1000; // VDP1 Version 1
   Vdp1Regs->regs.TVMR = 0;
   Vdp1Regs->regs.EWDR = 0;
   Vdp1Regs->regs.EWLR = 0;
   Vdp1Regs->regs.EWRR = 0;
   Vdp1Regs->regs.ENDR = 0;
   VDP1_MASK = 0xFFFF;
   VIDCore->Vdp1Reset(); //A voir
   vdp1_clock = 0;
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
        FRAMELOG("Read EDSR %X line = %d\n", Vdp1Regs->regs.EDSR, yabsys.LineCount);
        if (Vdp1External.checkEDSR == 0) {
          if (VIDCore != NULL)
            if (VIDCore->FinsihDraw != NULL)
              VIDCore->FinsihDraw();
        }
        Vdp1External.checkEDSR = 1;
        return Vdp1Regs->regs.EDSR;
      case 0x12:
        FRAMELOG("Read LOPR %X line = %d\n", Vdp1Regs->regs.LOPR, yabsys.LineCount);
         return Vdp1Regs->regs.LOPR;
      case 0x14:
        FRAMELOG("Read COPR %X line = %d\n", Vdp1Regs->regs.COPR, yabsys.LineCount);
         return Vdp1Regs->regs.COPR;
      case 0x16:
         return 0x1000 | ((Vdp1Regs->regs.PTMR & 2) << 7) | ((Vdp1Regs->regs.FBCR & 0x1E) << 3) | (Vdp1Regs->regs.TVMR & 0xF);
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

void FASTCALL Vdp1WriteWord(SH2_struct *context, u8* mem, u32 addr, u16 val) {
  addr &= 0xFF;
  switch(addr) {
    case 0x0:
      if ((Vdp1Regs->regs.FBCR & 3) != 3) val = (val & (~0x4));
      Vdp1Regs->regs.TVMR = val;
      Vdp1Regs->dirty.TVMR = 1;
    break;
    case 0x2:
      Vdp1Regs->regs.FBCR = val;
      Vdp1Regs->dirty.FBCR = 1;
      break;
    case 0x4:
      if ((val & 0x3)==0x3) {
          val = 0x2;
      }
      Vdp1Regs->regs.PTMR = val;
      Vdp1Regs->dirty.PTMR = 1;
      break;
    case 0x6:
       Vdp1Regs->regs.EWDR = val;
       Vdp1Regs->dirty.EWDR = 1;
       break;
    case 0x8:
       Vdp1Regs->regs.EWLR = val;
       Vdp1Regs->dirty.EWLR = 1;
       break;
    case 0xA:
       Vdp1Regs->regs.EWRR = val;
       Vdp1Regs->dirty.EWRR = 1;
       break;
    case 0xC:
       Vdp1Regs->regs.ENDR = val;
       Vdp1Regs->dirty.ENDR = 1;
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

void Vdp1DrawCommands(u8 * ram, Vdp1 * regs, u8* back_framebuffer)
{
  int cylesPerLine  = getVdp1CyclesPerLine();

  if (Vdp1External.status == VDP1_STATUS_IDLE) {
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
    if (usrClipCmd != NULL) free(usrClipCmd);
    if (sysClipCmd != NULL) free(sysClipCmd);
    if (localCoordCmd != NULL) free(localCoordCmd);
    usrClipCmd = NULL;
    sysClipCmd = NULL;
    localCoordCmd = NULL;
    nbCmdToProcess = 0;
  }
  CmdListLimit = 0;

   Vdp1External.status = VDP1_STATUS_RUNNING;
   if (regs->addr > 0x7FFFF) {
      Vdp1External.status = VDP1_STATUS_IDLE;
      return; // address error
    }

   u16 command = Vdp1RamReadWord(NULL, ram, regs->addr);
   u32 commandCounter = 0;

   Vdp1External.updateVdp1Ram = 0;
   vdp1Ram_update_start = 0x80000;
   vdp1Ram_update_end = 0x0;
   Vdp1External.checkEDSR = 0;

   vdp1cmd_struct oldCmd;

   yabsys.vdp1cycles = 0;
   while (!(command & 0x8000) && nbCmdToProcess < CMD_QUEUE_SIZE) { // fix me
     int ret;
      regs->regs.COPR = (regs->addr & 0x7FFFF) >> 3;
      // First, process the command
      if (!(command & 0x4000)) { // if (!skip)
         vdp1cmdctrl_struct *ctrl = NULL;
         int ret;
         if (vdp1_clock <= 0) {
           //No more clock cycle, wait next line
           return;
         }
         switch (command & 0x000F) {
         case 0: // normal sprite draw
            ctrl = &cmdBufferBeingProcessed[nbCmdToProcess];
            ctrl->dirty = 0;
            Vdp1ReadCommand(&ctrl->cmd, regs->addr, ram);
            if (!sameCmd(&ctrl->cmd, &oldCmd)) {
              oldCmd = ctrl->cmd;
              checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
              ctrl->ignitionLine = MIN(yabsys.LineCount + yabsys.vdp1cycles/cylesPerLine,yabsys.MaxLineCount-1);
              ret = Vdp1NormalSpriteDraw(&ctrl->cmd, ram, regs, back_framebuffer);
              if (ret == 1) nbCmdToProcess++;
              else vdp1_clock = 0; //Incorrect command, wait next line to continue
              setupSpriteLimit(ctrl);
            }
            break;
         case 1: // scaled sprite draw
            ctrl = &cmdBufferBeingProcessed[nbCmdToProcess];
            ctrl->dirty = 0;
            Vdp1ReadCommand(&ctrl->cmd, regs->addr, ram);
            if (!sameCmd(&ctrl->cmd, &oldCmd)) {
              oldCmd = ctrl->cmd;
              ctrl->ignitionLine = MIN(yabsys.LineCount + yabsys.vdp1cycles/cylesPerLine,yabsys.MaxLineCount-1);
              checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
              ret = Vdp1ScaledSpriteDraw(&ctrl->cmd, ram, regs, back_framebuffer);
              if (ret == 1) nbCmdToProcess++;
              else vdp1_clock = 0; //Incorrect command, wait next line to continue
              setupSpriteLimit(ctrl);
            }
            break;
         case 2: // distorted sprite draw
         case 3: /* this one should be invalid, but some games
                 (Hardcore 4x4 for instance) use it instead of 2 */
            ctrl = &cmdBufferBeingProcessed[nbCmdToProcess];
            ctrl->dirty = 0;
            Vdp1ReadCommand(&ctrl->cmd, regs->addr, ram);
            if (!sameCmd(&ctrl->cmd, &oldCmd)) {
              oldCmd = ctrl->cmd;
              ctrl->ignitionLine = MIN(yabsys.LineCount + yabsys.vdp1cycles/cylesPerLine,yabsys.MaxLineCount-1);
              checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
              ret = Vdp1DistortedSpriteDraw(&ctrl->cmd, ram, regs, back_framebuffer);
              if (ret == 1) nbCmdToProcess++;
              else vdp1_clock = 0; //Incorrect command, wait next line to continue
              setupSpriteLimit(ctrl);
            }
            break;
         case 4: // polygon draw
            ctrl = &cmdBufferBeingProcessed[nbCmdToProcess];
            ctrl->dirty = 0;
            Vdp1ReadCommand(&ctrl->cmd, regs->addr, ram);
            if (!sameCmd(&ctrl->cmd, &oldCmd)) {
              oldCmd = ctrl->cmd;
              ctrl->ignitionLine = MIN(yabsys.LineCount + yabsys.vdp1cycles/cylesPerLine,yabsys.MaxLineCount-1);
              checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
              nbCmdToProcess += Vdp1PolygonDraw(&ctrl->cmd, ram, regs, back_framebuffer);
              setupSpriteLimit(ctrl);
            }
            break;
         case 5: // polyline draw
         case 7: // undocumented mirror
            ctrl = &cmdBufferBeingProcessed[nbCmdToProcess];
            ctrl->dirty = 0;
            Vdp1ReadCommand(&ctrl->cmd, regs->addr, ram);
            if (!sameCmd(&ctrl->cmd, &oldCmd)) {
              oldCmd = ctrl->cmd;
              ctrl->ignitionLine = MIN(yabsys.LineCount + yabsys.vdp1cycles/cylesPerLine,yabsys.MaxLineCount-1);
              checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
              nbCmdToProcess += Vdp1PolylineDraw(&ctrl->cmd, ram, regs, back_framebuffer);
              setupSpriteLimit(ctrl);
            }
            break;
         case 6: // line draw
            ctrl = &cmdBufferBeingProcessed[nbCmdToProcess];
            ctrl->dirty = 0;
            Vdp1ReadCommand(&ctrl->cmd, regs->addr, ram);
            if (!sameCmd(&ctrl->cmd, &oldCmd)) {
              oldCmd = ctrl->cmd;
              ctrl->ignitionLine = MIN(yabsys.LineCount + yabsys.vdp1cycles/cylesPerLine,yabsys.MaxLineCount-1);
              checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
              nbCmdToProcess += Vdp1LineDraw(&ctrl->cmd, ram, regs, back_framebuffer);
              setupSpriteLimit(ctrl);
            }
            break;
         case 8: // user clipping coordinates
         case 11: // undocumented mirror
            checkClipCmd(&sysClipCmd, NULL, &localCoordCmd, ram, regs);
            yabsys.vdp1cycles += 16;
            usrClipCmd = (vdp1cmd_struct *)malloc(sizeof(vdp1cmd_struct));
            Vdp1ReadCommand(usrClipCmd, regs->addr, ram);
            oldCmd = *usrClipCmd;
            break;
         case 9: // system clipping coordinates
            checkClipCmd(NULL, &usrClipCmd, &localCoordCmd, ram, regs);
            yabsys.vdp1cycles += 16;
            sysClipCmd = (vdp1cmd_struct *)malloc(sizeof(vdp1cmd_struct));
            Vdp1ReadCommand(sysClipCmd, regs->addr, ram);
            oldCmd = *sysClipCmd;
            break;
         case 10: // local coordinate
            checkClipCmd(&sysClipCmd, &usrClipCmd, NULL, ram, regs);
            yabsys.vdp1cycles += 16;
            localCoordCmd = (vdp1cmd_struct *)malloc(sizeof(vdp1cmd_struct));
            Vdp1ReadCommand(localCoordCmd, regs->addr, ram);
            oldCmd = *localCoordCmd;
            break;
         default: // Abort
            VDP1LOG("vdp1\t: Bad command: %x\n", command);
            checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
      	    Vdp1External.status = VDP1_STATUS_IDLE;
            regs->regs.EDSR |= 2;
            regs->regs.COPR = (regs->addr & 0x7FFFF) >> 3;
            CmdListLimit = MAX((regs->addr & 0x7FFFF), regs->addr);
            return;
         }
      } else {
        yabsys.vdp1cycles += 16;
      }
      vdp1_clock -= yabsys.vdp1cycles;
      yabsys.vdp1cycles = 0;

	  // Force to quit internal command error( This technic(?) is used by BATSUGUN )
	  if (regs->regs.EDSR & 0x02){
		  checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
		  Vdp1External.status = VDP1_STATUS_IDLE;
		  regs->regs.COPR = (regs->addr & 0x7FFFF) >> 3;
      CmdListLimit = MAX((regs->addr & 0x7FFFF), regs->addr);
		  return;
	  }

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
            regs->lCOPR = (regs->addr & 0x7FFFF) >> 3;
            vdp1_clock = 0;
            CmdListInLoop = 1;
            CmdListLimit = MAX((regs->addr & 0x7FFFF), regs->addr);
            checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
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
      CmdListLimit = MAX((regs->addr & 0x7FFFF), regs->addr);
      //If we change directly CPR to last value, scorcher will not boot.
      //If we do not change it, Noon will not start
      //So store the value and update COPR with last value at VBlank In
      regs->lCOPR = (regs->addr & 0x7FFFF) >> 3;
   }
   if (command & 0x8000) {
        LOG("VDP1: Command Finished! count = %d @ %08X", commandCounter, regs->addr);
        Vdp1External.status = VDP1_STATUS_IDLE;
        regs->regs.COPR = (regs->addr & 0x7FFFF) >> 3;
        regs->lCOPR = (regs->addr & 0x7FFFF) >> 3;
   }
   CmdListLimit = MAX((regs->addr & 0x7FFFF), regs->addr);
   checkClipCmd(&sysClipCmd, &usrClipCmd, &localCoordCmd, ram, regs);
}

//ensure that registers are set correctly
void Vdp1FakeDrawCommands(u8 * ram, Vdp1 * regs)
{
   u16 command = T1ReadWord(ram, regs->addr);
   u32 commandCounter = 0;
   u32 returnAddr = 0xffffffff;
   vdp1cmd_struct cmd;

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
            break;
         case 8: // user clipping coordinates
         case 11: // undocumented mirror
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
            VDP1LOG("vdp1\t: Bad command: %x\n", command);
            regs->regs.EDSR |= 2;
            regs->regs.COPR = regs->addr >> 3;
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
      back_framebuffer[i] = Vdp1FrameBufferReadByte(NULL, NULL, i);

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
   vdp1Ram_update_start = 0x80000;
   vdp1Ram_update_end = 0x0;
#ifdef IMPROVED_SAVESTATES
   MemStateRead((void *)back_framebuffer, 0x40000, 1, stream);

   for (i = 0; i < 0x40000; i++)
      Vdp1FrameBufferWriteByte(NULL, NULL, i, back_framebuffer[i]);
#endif
   if (version > 1) {
     int size = 0;
     MemStateRead((void *)(&size), sizeof(int), 1, stream);
     if (size == sizeof(Vdp1External_struct)) {
        MemStateRead((void *)(&Vdp1External), sizeof(Vdp1External_struct),1,stream);
     } else {
       YuiMsg("Too old savestate, can not restore Vdp1External\n");
       memset((void *)(&Vdp1External), 0, sizeof(Vdp1External_struct));
       Vdp1External.disptoggle = 1;
     }
   } else {
     YuiMsg("Too old savestate, can not restore Vdp1External\n");
     memset((void *)(&Vdp1External), 0, sizeof(Vdp1External_struct));
     Vdp1External.disptoggle = 1;
   }
   Vdp1External.updateVdp1Ram = 1;

   return size;
}

//////////////////////////////////////////////////////////////////////////////

void ToggleVDP1(void)
{
   Vdp1External.disptoggle ^= 1;
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1HBlankIN(void)
{
  int needToCompose = 0;
  if (nbCmdToProcess > 0) {
    for (int i = 0; i<nbCmdToProcess; i++) {
      if (cmdBufferBeingProcessed[i].ignitionLine == (yabsys.LineCount+1)) {
        if (!((cmdBufferBeingProcessed[i].start_addr >= vdp1Ram_update_end) ||
            (cmdBufferBeingProcessed[i].end_addr <= vdp1Ram_update_start))) {
              needToCompose = 1;
          if (Vdp1External.checkEDSR == 0) {
            if (VIDCore->Vdp1RegenerateCmd != NULL) {
              VIDCore->Vdp1RegenerateCmd(&cmdBufferBeingProcessed[i].cmd);
            }
          }
        }
        cmdBufferBeingProcessed[i].ignitionLine = -1;
      }
    }
    nbCmdToProcess = 0;
    if (needToCompose == 1) {
      //We need to evaluate end line and not ignition line? It is improving doom if we better take care of the concurrency betwwen vdp1 update and command list"
      vdp1Ram_update_start = 0x80000;
      vdp1Ram_update_end = 0x0;
      if (VIDCore != NULL) {
        if (VIDCore->composeVDP1 != NULL) VIDCore->composeVDP1();
      }
      Vdp1Regs->regs.COPR = Vdp1Regs->lCOPR;
    }
  }
  if(yabsys.LineCount == 0) {
    startField();
  }
  if (Vdp1Regs->regs.PTMR == 0x1){
    if (Vdp1External.plot_trigger_line == yabsys.LineCount){
      if(Vdp1External.plot_trigger_done == 0) {
        vdp1_clock = 0;
        RequestVdp1ToDraw();
        Vdp1External.plot_trigger_done = 1;
      }
    }
  }
  #if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
    if (VIDCore != NULL && VIDCore->id != VIDCORE_SOFT) YglTMCheck();
  #endif
}
//////////////////////////////////////////////////////////////////////////////

void Vdp1HBlankOUT(void)
{
  vdp1_clock += getVdp1CyclesPerLine();
  Vdp1TryDraw();
}

//////////////////////////////////////////////////////////////////////////////
extern void vdp1_compute();
void Vdp1VBlankIN(void)
{
  // if (VIDCore != NULL) {
  //   if (VIDCore->composeVDP1 != NULL) VIDCore->composeVDP1();
  // }
}

//////////////////////////////////////////////////////////////////////////////

void Vdp1VBlankOUT(void)
{
  //Out of VBlankOut : Break Batman
  if (needVBlankErase()) {
    int id = 0;
    if (_Ygl != NULL) id = _Ygl->readframe;
    Vdp1EraseWrite(id);
  }
}

void vdp1Exec(int us) {
  updateRegisters();
  if (yabsys.HBlank != Vdp1Regs->HBlank) {
    //We entered/exited Horizontal blanking
    Vdp1Regs->HBlank = yabsys.HBlank;
    if (Vdp1Regs->HBlank) {
      FRAMELOG("VDP1 H-Blank In\n");
      Vdp1HBlankIN();
    } else {
      FRAMELOG("VDP1 H-Blank Out\n");
      Vdp1HBlankOUT();
    }

  }
  if (yabsys.VBlank != Vdp1Regs->VBlank) {
    //We entered/exited Horizontal blanking
    Vdp1Regs->VBlank = yabsys.VBlank;
    if (Vdp1Regs->VBlank) {
      FRAMELOG("VDP1 V-Blank In\n");
      Vdp1VBlankIN();
    } else {
      FRAMELOG("VDP1 V-Blank Out\n");
      Vdp1VBlankOUT();
    }

  }
}
