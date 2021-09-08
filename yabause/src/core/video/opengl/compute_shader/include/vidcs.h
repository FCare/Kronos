/*  Copyright 2005-2006 Theo Berkau

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

#ifndef VIDCS_H
#define VIDCS_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(HAVE_LIBGL) || defined(__ANDROID__) || defined(IOS)
#include "vdp1.h"
#define VIDCORE_CS   2
extern VideoInterface_struct VIDCS;
#endif

#ifdef __cplusplus
}
#endif

#endif
