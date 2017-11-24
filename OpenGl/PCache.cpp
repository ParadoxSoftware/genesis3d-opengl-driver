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
#define GLEW_STATIC
#include "./glew/include/GL/glew.h"
#include "Basetype.h"
#include "getypes.h"
#include "PCache.h"
#include "THandle.h"
#include "Render.h"
#include "OglDrv.h"

#define MAX_WORLD_POLYS				2048
#define MAX_WORLD_POLY_VERTS		8192

#define MAX_MISC_POLYS				2048
#define MAX_MISC_POLY_VERTS			8192

// changed QD Shadows
#define MAX_STENCIL_POLYS			2048
#define MAX_STENCIL_POLY_VERTS		8192

/*  01/28/2003 Wendell Buckner                                                          */
/*   Cache decals so that they can be drawn after all the 3d stuff...                   */
#define MAX_DECAL_RECTS             256

extern void gllog(const char *fmt, ...);

// Driver flags
bool bCanDoVertexBuffers = false;

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
	float x, y, z;
	float u, v, s, t;
	uint8 r, g, b, a;
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

typedef struct _WorldVertex
{
	float pos[3];
	float uv[4];
	float luv[4];
	unsigned char color[4];
} WorldVertex;

typedef struct _WorldPoly
{
	geRDriver_THandle *THandle;

	DRV_LInfo *LInfo;
	uint32 Flags;
	float ShiftU;
	float ShiftV;
	float ScaleU;
	float ScaleV;
	float ShiftU2;
	float ShiftV2;

	uint32 firstVert;
	uint32 numVerts;
} WorldPoly;

typedef struct _WorldCache
{
	WorldPoly Polys[MAX_WORLD_POLYS];
	//WorldPoly *SortedPolys[MAX_WORLD_POLYS];

	WorldVertex Verts[MAX_WORLD_POLY_VERTS];

	uint32 NumPolys;
	uint32 NumVerts;

	GLuint BufferID;
	GLuint vaoID;
} WorldCache;

static WorldCache			gWorldCache;

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

	gWorldCache.NumPolys = 0;
	gWorldCache.NumVerts = 0;

	if (glewIsSupported("GL_ARB_vertex_buffer_object"))
	{
		bCanDoVertexBuffers = true;
		gllog("Vertex Buffers supported...");
	}

	if (bCanDoVertexBuffers)
	{
		glGenBuffers(1, &gMiscCache.BufferID);
		glGenBuffers(1, &gWorldCache.BufferID);
	}
}

void PCache_Shutdown()
{
	if (bCanDoVertexBuffers)
	{
		glDeleteBuffers(1, &gMiscCache.BufferID);
		glDeleteBuffers(1, &gWorldCache.BufferID);
	}
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
	uint8 alpha = 0;
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
		alpha = (uint8)Verts->a;
	else
		alpha = 255;

	for (int i = 0; i < NumVerts; i++)
	{
		zRecip = 1.0f / pPnts->z;

		pVert->x = pPnts->x;
		pVert->y = pPnts->y;
		pVert->z = -1.0f + zRecip;
		
		pVert->u = pPnts->u * zRecip;
		pVert->v = pPnts->v * zRecip;
		pVert->s = 0.0f;
		pVert->t = zRecip;

		pVert->r = (uint8)pPnts->r;
		pVert->g = (uint8)pPnts->g;
		pVert->b = (uint8)pPnts->b;
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
	static GLuint boundTexture = 0;

	if (bCanDoVertexBuffers)
	{
		glBindBuffer(GL_ARRAY_BUFFER, gMiscCache.BufferID);
		glBufferData(GL_ARRAY_BUFFER, gMiscCache.NumVerts * sizeof(MiscVertex), gMiscCache.Verts, GL_STREAM_DRAW);

		size_t bufferLoc = 0;

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, sizeof(MiscVertex), (const void*)bufferLoc);

		bufferLoc += (sizeof(float) * 3);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, sizeof(MiscVertex), (const void*)bufferLoc);

		bufferLoc += (sizeof(float) * 4);
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(MiscVertex), (const void*)bufferLoc);
	}
	else
	{
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, sizeof(MiscVertex), &gMiscCache.Verts[0].x);

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, sizeof(MiscVertex), &gMiscCache.Verts[0].u);

		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(MiscVertex), &gMiscCache.Verts[0].r);
	}

	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_MULTISAMPLE);

	boundTexture = 0;

	for (uint32 i = 0; i < gMiscCache.NumPolys; i++)
	{
		pPoly = &gMiscCache.Poly[i];

		if (boundTexture != pPoly->THandle->TextureID)
		{
			glBindTexture(GL_TEXTURE_2D, pPoly->THandle->TextureID);
			boundTexture = pPoly->THandle->TextureID;
		}

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

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		if (pPoly->flags & DRV_RENDER_CLAMP_UV)
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		}
		else
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		}

		glDrawArrays(GL_TRIANGLE_FAN, pPoly->firstVert, pPoly->numVerts);

		if (pPoly->flags & DRV_RENDER_NO_ZMASK)
			glEnable(GL_DEPTH_TEST);

		if (pPoly->flags & DRV_RENDER_NO_ZWRITE)
			glEnable(GL_DEPTH_WRITEMASK);
	}
	
	glDisable(GL_MULTISAMPLE);

	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	if (bCanDoVertexBuffers)
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	OGLDRV.NumRenderedPolys += gMiscCache.NumPolys;

	gMiscCache.NumPolys = 0;
	gMiscCache.NumVerts = 0;

	return TRUE;
}

BOOL PCache_InsertWorldPoly(DRV_TLVertex *Verts, int32 NumVerts, geRDriver_THandle *THandle, DRV_TexInfo *TexInfo, DRV_LInfo *LInfo, uint32 Flags)
{
	float zRecip, DrawScaleU, DrawScaleV;
	WorldPoly *pPoly = NULL;
	DRV_TLVertex *pVerts = NULL;
	WorldVertex *pWVerts = NULL;
	uint8 alpha = 0;

	if ((gWorldCache.NumVerts + NumVerts) >= MAX_WORLD_POLY_VERTS)
	{
		if (!PCache_FlushWorldPolys())
			return GE_FALSE;
	}

	if (gWorldCache.NumPolys + 1 >= MAX_WORLD_POLYS)
	{
		if (!PCache_FlushWorldPolys())
			return GE_FALSE;
	}

	DrawScaleU = 1.0f / TexInfo->DrawScaleU;
	DrawScaleV = 1.0f / TexInfo->DrawScaleV;

	pVerts = Verts;
	pPoly = &gWorldCache.Polys[gWorldCache.NumPolys];

	pPoly->THandle = THandle;
	pPoly->LInfo = LInfo;
	pPoly->Flags = Flags;
	pPoly->firstVert = gWorldCache.NumVerts;
	pPoly->numVerts = NumVerts;
	pPoly->ShiftU = TexInfo->ShiftU;
	pPoly->ShiftV = TexInfo->ShiftV;
	pPoly->ScaleU = DrawScaleU;
	pPoly->ScaleV = DrawScaleV;

	if (pPoly->LInfo)
	{
		pPoly->ShiftU2 = (float)LInfo->MinU - 8.0f;
		pPoly->ShiftV2 = (float)LInfo->MinV - 8.0f;
	}

	pWVerts = &gWorldCache.Verts[gWorldCache.NumVerts];

	if (Flags & DRV_RENDER_ALPHA)
		alpha = (uint8)F2DW(pVerts->a);
	else
		alpha = 255;

	for (int i = 0; i < NumVerts; i++)
	{
		zRecip = 1.0f / pVerts->z;

		pWVerts->pos[0] = pVerts->x;
		pWVerts->pos[1] = pVerts->y;
		pWVerts->pos[2] = (-1.0f + zRecip);

		float tu = pVerts->u * pPoly->ScaleU + pPoly->ShiftU;
		float tv = pVerts->v * pPoly->ScaleV + pPoly->ShiftV;

		pWVerts->uv[0] = tu * THandle->InvScale * zRecip;
		pWVerts->uv[1] = tv * THandle->InvScale * zRecip;
		pWVerts->uv[2] = 0.0f;
		pWVerts->uv[3] = zRecip;

		if (pPoly->LInfo)
		{
			float lu, lv;

			lu = pVerts->u - pPoly->ShiftU2;
			lv = pVerts->v - pPoly->ShiftV2;

			pWVerts->luv[0] = lu * THandle->InvScale * zRecip;
			pWVerts->luv[1] = lv * THandle->InvScale * zRecip;
			pWVerts->luv[2] = 0.0f;
			pWVerts->luv[3] = zRecip;
		}

		pWVerts->color[0] = (uint8)F2DW(pVerts->r);
		pWVerts->color[1] = (uint8)F2DW(pVerts->g);
		pWVerts->color[2] = (uint8)F2DW(pVerts->b);
		pWVerts->color[3] = alpha;

		pWVerts++;
		pVerts++;
	}

	gWorldCache.NumVerts += NumVerts;
	gWorldCache.NumPolys++;

	return TRUE;
}

BOOL PCache_FlushWorldPolys(void)
{
	static uint32 wBoundTexture = 0;
	static uint32 wBoundTexture2 = 0;
	WorldPoly *pPoly = NULL;

	if (gWorldCache.NumPolys == 0)
		return GE_TRUE;

	wBoundTexture = 0;
	wBoundTexture2 = 0;

	if (bCanDoVertexBuffers)
	{
		glBindBuffer(GL_ARRAY_BUFFER, gWorldCache.BufferID);
		glBufferData(GL_ARRAY_BUFFER, gWorldCache.NumVerts * sizeof(WorldVertex), gWorldCache.Verts, GL_STREAM_DRAW);

		size_t bufferLoc = 0;

		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, sizeof(WorldVertex), (const void*)bufferLoc);

		bufferLoc += (sizeof(float) * 3);
		glActiveTexture(GL_TEXTURE0);
		glClientActiveTexture(GL_TEXTURE0);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, sizeof(WorldVertex), (const void*)bufferLoc);

		bufferLoc += (sizeof(float) * 4);
		glActiveTexture(GL_TEXTURE1);
		glClientActiveTexture(GL_TEXTURE1);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, sizeof(WorldVertex), (const void*)bufferLoc);

		bufferLoc += (sizeof(float) * 4);
		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(WorldVertex), (const void*)bufferLoc);
	}
	else
	{
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, sizeof(_WorldVertex), &gWorldCache.Verts[0].pos[0]);

		glActiveTexture(GL_TEXTURE0);
		glClientActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, sizeof(_WorldVertex), &gWorldCache.Verts[0].uv[0]);

		glActiveTexture(GL_TEXTURE1);
		glClientActiveTexture(GL_TEXTURE1);
		glEnable(GL_TEXTURE_2D);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glTexCoordPointer(4, GL_FLOAT, sizeof(_WorldVertex), &gWorldCache.Verts[0].luv[0]);

		glEnableClientState(GL_COLOR_ARRAY);
		glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(_WorldVertex), &gWorldCache.Verts[0].color[0]);
	}

	glEnable(GL_MULTISAMPLE);

	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	for (uint32 i = 0; i < gWorldCache.NumPolys; i++)
	{
		pPoly = &gWorldCache.Polys[i];

		if (pPoly->Flags & DRV_RENDER_NO_ZMASK)
			glDisable(GL_DEPTH_TEST);

		if (pPoly->Flags & DRV_RENDER_NO_ZWRITE)
			glDepthMask(GL_FALSE);

		if (pPoly->THandle)
		{
			glActiveTexture(GL_TEXTURE0);
			glClientActiveTexture(GL_TEXTURE0);
			glEnable(GL_TEXTURE_2D);

			if (wBoundTexture != pPoly->THandle->TextureID)
			{
				glBindTexture(GL_TEXTURE_2D, pPoly->THandle->TextureID);
				wBoundTexture = pPoly->THandle->TextureID;
			}

			if (pPoly->Flags & DRV_RENDER_CLAMP_UV)
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			}
			else
			{
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			}

			if (pPoly->THandle->Flags & THANDLE_UPDATE)
				THandle_Update(pPoly->THandle);

			if (pPoly->LInfo)
			{
				if (wBoundTexture2 != pPoly->LInfo->THandle->TextureID)
				{
					geBoolean Dynamic;

					wBoundTexture2 = pPoly->LInfo->THandle->TextureID;

					glActiveTexture(GL_TEXTURE1);
					glClientActiveTexture(GL_TEXTURE1);
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, pPoly->LInfo->THandle->TextureID);
					
					OGLDRV.SetupLightmap(pPoly->LInfo, &Dynamic);
					if (Dynamic || pPoly->LInfo->THandle->Flags & THANDLE_UPDATE_LM)
					{
						THandle_DownloadLightmap(pPoly->LInfo);
						if (Dynamic)
							pPoly->LInfo->THandle->Flags |= THANDLE_UPDATE_LM;
						else
							pPoly->LInfo->THandle->Flags &= ~THANDLE_UPDATE_LM;
					}
				}

				if (pPoly->LInfo->THandle->Flags & THANDLE_UPDATE)
					THandle_Update(pPoly->LInfo->THandle);

				glActiveTexture(GL_TEXTURE0);
				glClientActiveTexture(GL_TEXTURE0);
			}
			else
			{
				glActiveTexture(GL_TEXTURE1);
				glClientActiveTexture(GL_TEXTURE1);
				glDisable(GL_TEXTURE_2D);

				glActiveTexture(GL_TEXTURE0);
				glClientActiveTexture(GL_TEXTURE0);
			}
		}

		glDrawArrays(GL_TRIANGLE_FAN, pPoly->firstVert, pPoly->numVerts);

		if (pPoly->Flags & DRV_RENDER_NO_ZMASK)
			glEnable(GL_DEPTH_TEST);

		if (pPoly->Flags & DRV_RENDER_NO_ZWRITE)
			glDepthMask(GL_TRUE);
	}

	glDisable(GL_MULTISAMPLE);

	glDisableClientState(GL_COLOR_ARRAY);
	glActiveTexture(GL_TEXTURE1);
	glClientActiveTexture(GL_TEXTURE1);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	glActiveTexture(GL_TEXTURE1);
	glClientActiveTexture(GL_TEXTURE1);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
	//glActiveTexture(GL_TEXTURE0);

	if (bCanDoVertexBuffers)
		glBindBuffer(GL_ARRAY_BUFFER, 0);

	OGLDRV.NumRenderedPolys += gWorldCache.NumPolys;
	gWorldCache.NumPolys = 0;
	gWorldCache.NumVerts = 0;

	return TRUE;
}

