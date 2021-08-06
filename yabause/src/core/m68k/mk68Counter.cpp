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
#include "yabause.h"

#include <thread>
#include "mk68Counter.hpp"

volatile u64 m68k_counter(0);
volatile u64 m68k_counter_done(0);

const u64 MAX_SCSP_COUNTER = (u64)(44100 * 256 / 60) << SCSP_FRACTIONAL_BITS;

void SyncCPUtoSCSP();

void setM68kCounter(u64 counter) {
	m68k_counter = counter;
}

void setM68kDoneCounter(u64 counter) {
	m68k_counter_done = counter;
}

u64 getM68KCounter() {
	return m68k_counter;
}

extern void syncM68K() {
	int timeout = 0;
	while ( (m68k_counter >> SCSP_FRACTIONAL_BITS) > m68k_counter_done)
	{
	    timeout++;
	    std::this_thread::yield();
	}
}
