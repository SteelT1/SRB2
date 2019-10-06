// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2000 by Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze, Andrey Budko (prboom)
// Copyright (C) 1999-2019 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_fps.h
/// \brief Uncapped framerate stuff.

#ifndef __R_FPS_H__
#define __R_FPS_H__

#include "m_fixed.h"
#include "p_local.h"
#include "r_state.h"

enum viewcontext_e
{
	VIEWCONTEXT_PLAYER1 = 0,
	VIEWCONTEXT_PLAYER2,
	VIEWCONTEXT_SKY1,
	VIEWCONTEXT_SKY2
};

typedef struct {
	fixed_t x;
	fixed_t y;
	fixed_t z;
	boolean sky;
	sector_t *sector;
	player_t *player;

	angle_t angle;
	angle_t aim;
	fixed_t cos;
	fixed_t sin;
	mobj_t *mobj;
} viewvars_t;

//extern viewvars_t oldview;
extern viewvars_t *newview;

void R_InterpolateView(fixed_t frac);
void R_UpdateViewInterpolation(void);
void R_SetViewContext(enum viewcontext_e _viewcontext);
void R_SetThinkerOldStates(void);
void R_SetThinkerNewStates(void);
void R_DoThinkerLerp(fixed_t frac);
void R_ResetThinkerLerp(void);

fixed_t R_LerpFixed(fixed_t from, fixed_t to, fixed_t frac);
INT32 R_LerpInt32(INT32 from, INT32 to, fixed_t frac);
UINT32 R_LerpUInt32(UINT32 from, UINT32 to, fixed_t frac);
angle_t R_LerpAngle(angle_t from, angle_t to, fixed_t frac);

#endif
