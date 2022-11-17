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

/*! \file video.c
    \brief Video emulation functions.
*/


#include <stdlib.h>
#include "yabause.h"
#include "video.h"
#include "sh2core.h"

VideoInterface_struct *VIDCore=NULL;
extern VideoInterface_struct *VIDCoreList[];

//////////////////////////////////////////////////////////////////////////////

int VideoInit(int coreid) {
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

int VideoSetSetting( int type, int value )
{
	if (VIDCore) VIDCore->SetSettingValue( type, value );
	return 0;
}