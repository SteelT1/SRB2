// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2016 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_segs.h
/// \brief Refresh module, drawing LineSegs from BSP

#ifndef __R_SEGS__
#define __R_SEGS__

#ifdef __GNUG__
#pragma interface
#endif

void R_RenderMaskedSegRange(drawseg_t *ds, INT32 x1, INT32 x2);
void R_RenderThickSideRange(drawseg_t *ds, INT32 x1, INT32 x2, ffloor_t *pfloor);
void R_StoreWallRange(INT32 start, INT32 stop);

/// JimitaMPC
typedef struct
{
	INT32 x1, x2;
	fixed_t distance;

	angle_t angle1, angle2;
	angle_t normalangle, centerangle;
	angle_t offset;
	angle_t offset2;		/// Unused

	fixed_t scale;
	fixed_t scalestep;

	fixed_t midtexturemid, toptexturemid, bottomtexturemid;

	#ifdef ESLOPE
	fixed_t toptextureslide, midtextureslide, bottomtextureslide;
	fixed_t midtextureback, midtexturebackslide;
	#endif
} viswall_t;
extern viswall_t rw;

#endif
