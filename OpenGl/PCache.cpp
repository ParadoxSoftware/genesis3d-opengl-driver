/*
	@file PCache.c

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
#include <Windows.h>
#include <list>
#include "./glew/include/GL/glew.h"
#include "PCache.h"
#include "THandle.h"
#include "Render.h"

#define MAX_WORLD_POLYS				256
#define MAX_WORLD_POLY_VERTS		1024

#define MAX_MISC_POLYS				256
#define MAX_MISC_POLY_VERTS			1024

// changed QD Shadows
#define MAX_STENCIL_POLYS			2048
#define MAX_STENCIL_POLY_VERTS		8192

/*  01/28/2003 Wendell Buckner                                                          */
/*   Cache decals so that they can be drawn after all the 3d stuff...                   */
#define MAX_DECAL_RECTS             256

typedef struct DecalRect
{
	geRDriver_THandle *THandle;
	RECT SrcRect;
	int32 x;
	int32 y;
} DecalRect;

typedef struct DecalCache
{
	DecalRect Decals[MAX_DECAL_RECTS];
	uint32 NumDecals;
} DecalCache;

static DecalCache					gDecalCache;

typedef struct _MiscVertex
{
	float x, y, z, u, v, zRecip;
	uint32 r, g, b, a;
} MiscVertex;

typedef struct _MiscPoly
{
	uint32 firstVert;
	uint32 numVerts;
	uint32 flags;

	geRDriver_THandle *THandle;
} MiscPoly;

typedef struct MiscCache
{
	MiscPoly Poly[MAX_MISC_POLYS];
	MiscPoly *SortedPolys[MAX_MISC_POLYS];

	MiscVertex Verts[MAX_MISC_POLY_VERTS];

	uint32 NumPolys;
	uint32 NumVerts;

	GLuint BufferID;
} MiscCache;

static MiscCache				gMiscCache;

__inline DWORD F2DW(float f)
{
	DWORD            retval = 0;

	_asm {
		fld            f
			lea            eax, [retval]
			fistp         dword ptr[eax]
	}

	return retval;
}

void PCache_Initialize()
{
	gDecalCache.NumDecals = 0;

	gMiscCache.NumPolys = 0;
	gMiscCache.NumVerts = 0;

	//glGenBuffers(1, &gMiscCache.BufferID);
}

BOOL DRIVERCC PCache_InsertDecal(geRDriver_THandle *THandle, RECT *SrcRect, int32 x, int32 y)
{
	DecalRect *pDecal = NULL;
	
	if (gDecalCache.NumDecals >= MAX_DECAL_RECTS)
	{
		PCache_FlushDecals();
	}

	pDecal = &gDecalCache.Decals[gDecalCache.NumDecals];
	pDecal->THandle = THandle;

	pDecal->SrcRect.bottom = -1;
	pDecal->SrcRect.left = -1;
	pDecal->SrcRect.right = -1;
	pDecal->SrcRect.top = -1;

	if (SrcRect != NULL)
	{
		pDecal->SrcRect.bottom = SrcRect->bottom;
		pDecal->SrcRect.left = SrcRect->left;
		pDecal->SrcRect.right = SrcRect->right;
		pDecal->SrcRect.top = SrcRect->top;
	}

	pDecal->x = x;
	pDecal->y = y;

	gDecalCache.NumDecals++;
	return TRUE;
}

BOOL PCache_FlushDecals()
{
	DecalRect *pRect = NULL;

	if (gDecalCache.NumDecals == 0)
		return TRUE;

	for (uint32 i = 0; i < gDecalCache.NumDecals; i++)
	{
		pRect = &gDecalCache.Decals[i];
		if (pRect->SrcRect.bottom == -1)
			DrawDecal(pRect->THandle, NULL, pRect->x, pRect->y);
		else
			DrawDecal(pRect->THandle, &pRect->SrcRect, pRect->x, pRect->y);
	}

	gDecalCache.NumDecals = 0;
	return TRUE;
}

BOOL PCache_InsertMiscPoly(DRV_TLVertex *Verts, int32 NumVerts, geRDriver_THandle *THandle, uint32 Flags)
{
	float zRecip = 0.0f;
	GLuint alpha = 0;
	MiscPoly *pPoly = NULL;
	MiscVertex *pVert = NULL;

	if ((gMiscCache.NumPolys + 1) >= MAX_MISC_POLYS || (gMiscCache.NumVerts + NumVerts) >= MAX_MISC_POLY_VERTS)
	{
		PCache_FlushMiscPolys();
	}

	pPoly = &gMiscCache.Poly[gMiscCache.NumPolys];

	pPoly->THandle = THandle;
	pPoly->flags = Flags;
	pPoly->firstVert = gMiscCache.NumVerts;
	pPoly->numVerts = NumVerts;

	DRV_TLVertex *pPnts = Verts;
	pVert = &gMiscCache.Verts[pPoly->firstVert];

	if (Flags & DRV_RENDER_ALPHA)
		alpha = F2DW(Verts->a);
	else
		alpha = 255;

	for (int i = 0; i < NumVerts; i++)
	{
		zRecip = 1.0f / pPnts->z;

		pVert->x = pPnts->x;
		pVert->y = pPnts->y;
		pVert->z = -1.0f + zRecip;
		pVert->zRecip = zRecip;

		pVert->u = pPnts->u * zRecip;
		pVert->v = pPnts->v * zRecip;

		pVert->r = F2DW(pPnts->r);
		pVert->g = F2DW(pPnts->g);
		pVert->b = F2DW(pPnts->b);
		pVert->a = alpha;

		pVert++;
		pPnts++;
	}

	gMiscCache.NumPolys++;
	gMiscCache.NumVerts += NumVerts;
	return TRUE;
}

BOOL PCache_FlushMiscPolys()
{
	MiscVertex *pVert = NULL;
	MiscPoly *pPoly = NULL;

	for (int i = 0; i < gMiscCache.NumPolys; i++)
	{
		pPoly = &gMiscCache.Poly[i];

		if (pPoly->THandle->Flags & THANDLE_UPDATE)
			THandle_Update(pPoly->THandle);

		if (pPoly->flags & DRV_RENDER_NO_ZMASK)
			glDisable(GL_DEPTH_TEST);
		else
			glEnable(GL_DEPTH_TEST);

		if (pPoly->flags & DRV_RENDER_NO_ZWRITE)
			glDisable(GL_DEPTH_WRITEMASK);
		else
			glEnable(GL_DEPTH_WRITEMASK);

		pVert = &gMiscCache.Verts[pPoly->firstVert];

		glBegin(GL_TRIANGLE_FAN);
		for (int j = 0; j < pPoly->numVerts; j++)
		{
			glColor4ui(pVert->r, pVert->g, pVert->b, pVert->a);
			glTexCoord4f(pVert->u, pVert->v, 0.0f, pVert->zRecip);
			glVertex3f(pVert->x, pVert->y, pVert->z);

			pVert++;
		}
		glEnd();

		if (pPoly->flags & DRV_RENDER_NO_ZMASK)
			glEnable(GL_DEPTH_TEST);

		if (pPoly->flags & DRV_RENDER_NO_ZWRITE)
			glEnable(GL_DEPTH_WRITEMASK);
	}

	gMiscCache.NumPolys = 0;
	gMiscCache.NumVerts = 0;

	return TRUE;
}