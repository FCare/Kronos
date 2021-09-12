/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include <stdio.h>
#if defined(__APPLE__)
#include <stdlib.h>
#else
#include <malloc.h>
#endif
#include "core.h"
#include "mk68Counter.h"
#include "threads.h"

volatile u64 m68k_counter = 0;
extern "C" YabEventQueue * q_scsp_m68counterCond;

void setM68kCounter(u64 counter) {
	m68k_counter = counter;

    if(counter != 0 && YaGetQueueSize(q_scsp_m68counterCond) == 0)
    {
	    YabAddEventQueue(q_scsp_m68counterCond, 0);
    }
}

u64 getM68KCounter() {
	return m68k_counter;
}
