/*
	@file PCache.h

	@author Anthony Rufrano (paradoxnj@comcast.net)
	@brief Polygon rasterization functions for OpenGL driver

	@par
	The contents of this file are subject to the Genesis3D Public License
	Version 1.01 (the "License"); you may not use this file except in
	compliance with the License. You may obtain a copy of the License at
	http://www.genesis3d.com

	@par
	Software distributed under the License is distributed on an "AS IS"
	basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See
	the License for the specific language governing rights and limitations
	under the License.
*/
#ifndef __PCACHE_H__
#define __PCACHE_H__

#include "dcommon.h"

void PCache_Initialize();

BOOL DRIVERCC PCache_InsertDecal(geRDriver_THandle *THandle, RECT *SrcRect, int32 x, int32 y);
BOOL PCache_FlushDecals(void);

BOOL PCache_InsertMiscPoly(DRV_TLVertex *Verts, int32 NumVerts, geRDriver_THandle *THandle, uint32 Flags);
BOOL PCache_FlushMiscPolys(void);

#endif