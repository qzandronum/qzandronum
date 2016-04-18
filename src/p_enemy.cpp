// Emacs style mode select	 -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//		Enemy thinking, AI.
//		Action Pointer Functions
//		that are associated with states/frames. 
//
//-----------------------------------------------------------------------------


#include <stdlib.h>

#include "templates.h"
#include "m_random.h"
#include "i_system.h"
#include "doomdef.h"
#include "p_local.h"
#include "p_maputl.h"
#include "d_player.h"
#include "m_bbox.h"
#include "p_lnspec.h"
#include "s_sound.h"
#include "g_game.h"
#include "doomstat.h"
#include "r_state.h"
#include "c_cvars.h"
#include "p_enemy.h"
#include "a_sharedglobal.h"
#include "a_action.h"
#include "thingdef/thingdef.h"
#include "d_dehacked.h"
#include "g_level.h"
#include "r_utility.h"
#include "p_blockmap.h"
#include "r_data/r_translate.h"
#include "teaminfo.h"
#include "p_spec.h"
#include "p_checkposition.h"
#include "math/cmath.h"
// [BB] New #includes.
#include "cl_demo.h"
#include "cooperative.h"
#include "deathmatch.h"
#include "network.h"
#include "team.h"
#include "invasion.h"

#include "gi.h"

static FRandom pr_checkmissilerange ("CheckMissileRange");
static FRandom pr_opendoor ("OpenDoor");
static FRandom pr_trywalk ("TryWalk");
static FRandom pr_newchasedir ("NewChaseDir");
static FRandom pr_lookformonsters ("LookForMonsters");
static FRandom pr_lookforplayers ("LookForPlayers");
static FRandom pr_scaredycat ("Anubis");
	   FRandom pr_chase ("Chase");
static FRandom pr_facetarget ("FaceTarget");
static FRandom pr_railface ("RailFace");
static FRandom pr_dropitem ("DropItem");
static FRandom pr_look2 ("LookyLooky");
static FRandom pr_look3 ("IGotHooky");
static FRandom pr_slook ("SlooK");
static FRandom pr_dropoff ("Dropoff");
static FRandom pr_defect ("Defect");

static FRandom pr_skiptarget("SkipTarget");
static FRandom pr_enemystrafe("EnemyStrafe");

// movement interpolation is fine for objects that are moved by their own
// velocity. But for monsters it is problematic.
// 1. They don't move every tic
// 2. Their animation is not designed for movement interpolation
// The result is that they tend to 'glide' across the floor
// so this CVAR allows to switch it off.
CVAR(Bool, nomonsterinterpolation, false, CVAR_GLOBALCONFIG|CVAR_ARCHIVE)

//
// P_NewChaseDir related LUT.
//

dirtype_t opposite[9] =
{
	DI_WEST, DI_SOUTHWEST, DI_SOUTH, DI_SOUTHEAST,
	DI_EAST, DI_NORTHEAST, DI_NORTH, DI_NORTHWEST, DI_NODIR
};

dirtype_t diags[4] =
{
	DI_NORTHWEST, DI_NORTHEAST, DI_SOUTHWEST, DI_SOUTHEAST
};

fixed_t xspeed[8] = {FRACUNIT,46341,0,-46341,-FRACUNIT,-46341,0,46341};
fixed_t yspeed[8] = {0,46341,FRACUNIT,46341,0,-46341,-FRACUNIT,-46341};

void P_RandomChaseDir (AActor *actor);


//
// ENEMY THINKING
// Enemies are always spawned
// with targetplayer = -1, threshold = 0
// Most monsters are spawned unaware of all players,
// but some can be made preaware
//


//----------------------------------------------------------------------------
//
// PROC P_RecursiveSound
//
// Called by P_NoiseAlert.
// Recursively traverse adjacent sectors,
// sound blocking lines cut off traversal.
//----------------------------------------------------------------------------

void P_RecursiveSound (sector_t *sec, AActor *soundtarget, bool splash, int soundblocks, AActor *emitter, fixed_t maxdist)
{
	int 		i;
	line_t* 	check;
	sector_t*	other;
	AActor*		actor;
		
	// wake up all monsters in this sector
	if (sec->validcount == validcount
		&& sec->soundtraversed <= soundblocks+1)
	{
		return; 		// already flooded
	}
	
	sec->validcount = validcount;
	sec->soundtraversed = soundblocks+1;
	sec->SoundTarget = soundtarget;

	// [RH] Set this in the actors in the sector instead of the sector itself.
	for (actor = sec->thinglist; actor != NULL; actor = actor->snext)
	{
		if (actor != soundtarget && (!splash || !(actor->flags4 & MF4_NOSPLASHALERT)) &&
			(!maxdist || (actor->AproxDistance(emitter) <= maxdist)))
		{
			actor->LastHeard = soundtarget;
		}
	}

	bool checkabove = !sec->PortalBlocksSound(sector_t::ceiling);
	bool checkbelow = !sec->PortalBlocksSound(sector_t::floor);

	for (i = 0; i < sec->linecount; i++)
	{
		check = sec->lines[i];

		// I wish there was a better method to do this than randomly looking through the portal at a few places...
		if (checkabove)
		{
			sector_t *upper =
				P_PointInSector(check->v1->x + check->dx / 2 + FLOAT2FIXED(sec->SkyBoxes[sector_t::ceiling]->Scale.X),
					check->v1->y + check->dy / 2 + FLOAT2FIXED(sec->SkyBoxes[sector_t::ceiling]->Scale.Y));

			P_RecursiveSound(upper, soundtarget, splash, soundblocks, emitter, maxdist);
		}
		if (checkbelow)
		{
			sector_t *lower =
				P_PointInSector(check->v1->x + check->dx / 2 + FLOAT2FIXED(sec->SkyBoxes[sector_t::ceiling]->Scale.X),
					check->v1->y + check->dy / 2 + FLOAT2FIXED(sec->SkyBoxes[sector_t::ceiling]->Scale.Y));

			P_RecursiveSound(lower, soundtarget, splash, soundblocks, emitter, maxdist);
		}
		FLinePortal *port = check->getPortal();
		if (port && (port->mFlags & PORTF_SOUNDTRAVERSE))
		{
			if (port->mDestination)
			{
				P_RecursiveSound(port->mDestination->frontsector, soundtarget, splash, soundblocks, emitter, maxdist);
			}
		}


		if (check->sidedef[1] == NULL ||
			!(check->flags & ML_TWOSIDED))
		{
			continue;
		}
		
		// Early out for intra-sector lines
		if (check->sidedef[0]->sector == check->sidedef[1]->sector) continue;

		if ( check->sidedef[0]->sector == sec)
			other = check->sidedef[1]->sector;
		else
			other = check->sidedef[0]->sector;

		// check for closed door
		if ((sec->floorplane.ZatPoint (check->v1->x, check->v1->y) >=
			 other->ceilingplane.ZatPoint (check->v1->x, check->v1->y) &&
			 sec->floorplane.ZatPoint (check->v2->x, check->v2->y) >=
			 other->ceilingplane.ZatPoint (check->v2->x, check->v2->y))
		 || (other->floorplane.ZatPoint (check->v1->x, check->v1->y) >=
			 sec->ceilingplane.ZatPoint (check->v1->x, check->v1->y) &&
			 other->floorplane.ZatPoint (check->v2->x, check->v2->y) >=
			 sec->ceilingplane.ZatPoint (check->v2->x, check->v2->y))
		 || (other->floorplane.ZatPoint (check->v1->x, check->v1->y) >=
			 other->ceilingplane.ZatPoint (check->v1->x, check->v1->y) &&
			 other->floorplane.ZatPoint (check->v2->x, check->v2->y) >=
			 other->ceilingplane.ZatPoint (check->v2->x, check->v2->y)))
		{
			continue;
		}

		if (check->flags & ML_SOUNDBLOCK)
		{
			if (!soundblocks)
				P_RecursiveSound (other, soundtarget, splash, 1, emitter, maxdist);
		}
		else
		{
			P_RecursiveSound (other, soundtarget, splash, soundblocks, emitter, maxdist);
		}
	}
}



//----------------------------------------------------------------------------
//
// PROC P_NoiseAlert
//
// If a monster yells at a player, it will alert other monsters to the
// player.
//
//----------------------------------------------------------------------------

void P_NoiseAlert (AActor *target, AActor *emitter, bool splash, fixed_t maxdist)
{
	if (emitter == NULL)
		return;

	if (target != NULL && target->player && (target->player->cheats & CF_NOTARGET))
		return;

	validcount++;
	P_RecursiveSound (emitter->Sector, target, splash, 0, emitter, maxdist);
}




//----------------------------------------------------------------------------
//
// AActor :: CheckMeleeRange
//
//----------------------------------------------------------------------------

bool AActor::CheckMeleeRange ()
{
	AActor *pl = target;

	fixed_t dist;
		
	if (!pl)
		return false;
				
	dist = AproxDistance (pl);

	if (dist >= meleerange + pl->_f_radius())
		return false;

	// [RH] If moving toward goal, then we've reached it.
	if (pl == goal)
		return true;

	// [RH] Don't melee things too far above or below actor.
	if (!(flags5 & MF5_NOVERTICALMELEERANGE))
	{
		if (pl->Z() > Top())
			return false;
		if (pl->Top() < Z())
			return false;
	}

	// killough 7/18/98: friendly monsters don't attack other friends
	if (IsFriend(pl))
		return false;
		
	if (!P_CheckSight (this, pl, 0))
		return false;
														
	return true;				
}

//----------------------------------------------------------------------------
//
// FUNC P_CheckMeleeRange2
//
//----------------------------------------------------------------------------

bool P_CheckMeleeRange2 (AActor *actor)
{
	AActor *mo;
	fixed_t dist;


	if (!actor->target)
	{
		return false;
	}
	mo = actor->target;
	dist = mo->AproxDistance (actor);
	if (dist >= (128 << FRACBITS) || dist < actor->meleerange + mo->_f_radius())
	{
		return false;
	}
	if (mo->Z() > actor->Top())
	{ // Target is higher than the attacker
		return false;
	}
	else if (actor->Z() > mo->Top())
	{ // Attacker is higher
		return false;
	}
	else if (actor->IsFriend(mo))
	{
		// killough 7/18/98: friendly monsters don't attack other friends
		return false;
	}

	if (!P_CheckSight(actor, mo))
	{
		return false;
	}
	return true;
}


//=============================================================================
//
// P_CheckMissileRange
//
//=============================================================================
bool P_CheckMissileRange (AActor *actor)
{
	fixed_t dist;
		
	if (!P_CheckSight (actor, actor->target, SF_SEEPASTBLOCKEVERYTHING))
		return false;
		
	if (actor->flags & MF_JUSTHIT)
	{
		// the target just hit the enemy, so fight back!
		actor->flags &= ~MF_JUSTHIT;

		// killough 7/18/98: no friendly fire at corpses
		// killough 11/98: prevent too much infighting among friends
		// Cleaned up and made readable
		if (!(actor->flags & MF_FRIENDLY)) return true;
		if (actor->target->health <= 0) return false;
		if (!actor->IsFriend(actor->target)) return true;
		if (actor->target->player != NULL)
		{
			return (pr_defect() >128);
		}
		else
		{
			return !(actor->target->flags & MF_JUSTHIT) && pr_defect() >128;
		}
	}
		
	if (actor->reactiontime)
		return false;	// do not attack yet

	// killough 7/18/98: friendly monsters don't attack other friendly
	// monsters or players (except when attacked, and then only once)
	if (actor->IsFriend(actor->target))
		return false;
			
	if (actor->flags & MF_FRIENDLY && P_HitFriend(actor))
		return false;

	// OPTIMIZE: get this from a global checksight
	// [RH] What?
	dist = actor->AproxDistance (actor->target) - 64*FRACUNIT;
	
	if (actor->MeleeState == NULL)
		dist -= 128*FRACUNIT;	// no melee attack, so fire more

	return actor->SuggestMissileAttack (dist);
}

bool AActor::SuggestMissileAttack (fixed_t dist)
{
	// new version encapsulates the different behavior in flags instead of virtual functions
	// The advantage is that this allows inheriting the missile attack attributes from the
	// various Doom monsters by custom monsters
	
	if (maxtargetrange > 0 && dist > maxtargetrange)
		return false;	// The Arch Vile's special behavior turned into a property
		
	if (MeleeState != NULL && dist < meleethreshold)
		return false;	// From the Revenant: close enough for fist attack

	if (flags4 & MF4_MISSILEMORE) dist >>= 1;
	if (flags4 & MF4_MISSILEEVENMORE) dist >>= 3;
	
	int mmc = FixedMul(MinMissileChance, G_SkillProperty(SKILLP_Aggressiveness));
	return pr_checkmissilerange() >= MIN<int> (dist >> FRACBITS, mmc);
}

//=============================================================================
//
// P_HitFriend()
//
// killough 12/98
// This function tries to prevent shooting at friends that get in the line of fire
//
// [GrafZahl] Taken from MBF but this has been cleaned up to make it readable.
//
//=============================================================================

bool P_HitFriend(AActor * self)
{
	FTranslatedLineTarget t;

	if (self->flags&MF_FRIENDLY && self->target != NULL)
	{
		DAngle angle = self->AngleTo(self->target);
		double dist = self->Distance2D(self->target);
		P_AimLineAttack (self, angle, dist, &t, 0., true);
		if (t.linetarget != NULL && t.linetarget != self->target)
		{
			return self->IsFriend (t.linetarget);
		}
	}
	return false;
}

//
// P_Move
// Move in the current direction,
// returns false if the move is blocked.
//

bool P_Move (AActor *actor)
{

	fixed_t tryx, tryy, deltax, deltay, origx, origy;
	bool try_ok;
	int speed = actor->_f_speed();
	int movefactor = ORIG_FRICTION_FACTOR;
	int friction = ORIG_FRICTION;
	int dropoff = 0;

	if (actor->flags2 & MF2_BLASTED)
	{
		return true;
	}

	if (actor->movedir == DI_NODIR)
	{
		return false;
	}

	// [RH] Walking actors that are not on the ground cannot walk. We don't
	// want to yank them to the ground here as Doom did, since that makes
	// it difficult to thrust them vertically in a reasonable manner.
	// [GZ] Let jumping actors jump.
	if (!((actor->flags & MF_NOGRAVITY) || (actor->flags6 & MF6_CANJUMP))
		&& actor->Z() > actor->floorz && !(actor->flags2 & MF2_ONMOBJ))
	{
		return false;
	}

	if ((unsigned)actor->movedir >= 8)
		I_Error ("Weird actor->movedir!");

	// killough 10/98: allow dogs to drop off of taller ledges sometimes.
	// dropoff==1 means always allow it, dropoff==2 means only up to 128 high,
	// and only if the target is immediately on the other side of the line.
	AActor *target = actor->target;

	if ((actor->flags6 & MF6_JUMPDOWN) && target &&
			!(target->IsFriend(actor)) &&
			actor->AproxDistance(target) < FRACUNIT*144 &&
			pr_dropoff() < 235)
	{
		dropoff = 2;
	}

	// [RH] I'm not so sure this is such a good idea
	// [GZ] That's why it's compat-optioned.
	if (compatflags & COMPATF_MBFMONSTERMOVE)
	{
		// killough 10/98: make monsters get affected by ice and sludge too:
		movefactor = P_GetMoveFactor (actor, &friction);

		if (friction < ORIG_FRICTION)
		{ // sludge
			speed = ((ORIG_FRICTION_FACTOR - (ORIG_FRICTION_FACTOR-movefactor)/2)
			   * speed) / ORIG_FRICTION_FACTOR;
			if (speed == 0)
			{ // always give the monster a little bit of speed
				speed = ksgn(actor->_f_speed());
			}
		}
	}

	tryx = (origx = actor->_f_X()) + (deltax = FixedMul (speed, xspeed[actor->movedir]));
	tryy = (origy = actor->_f_Y()) + (deltay = FixedMul (speed, yspeed[actor->movedir]));

	// Like P_XYMovement this should do multiple moves if the step size is too large

	fixed_t maxmove = actor->_f_radius() - FRACUNIT;
	int steps = 1;

	if (maxmove > 0)
	{ 
		const fixed_t xspeed = abs (deltax);
		const fixed_t yspeed = abs (deltay);

		if (xspeed > yspeed)
		{
			if (xspeed > maxmove)
			{
				steps = 1 + xspeed / maxmove;
			}
		}
		else
		{
			if (yspeed > maxmove)
			{
				steps = 1 + yspeed / maxmove;
			}
		}
	}
	FCheckPosition tm;

	tm.FromPMove = true;

	try_ok = true;
	for(int i=1; i < steps; i++)
	{
		try_ok = P_TryMove(actor, origx + Scale(deltax, i, steps), origy + Scale(deltay, i, steps), dropoff, NULL, tm);
		if (!try_ok) break;
	}

	// killough 3/15/98: don't jump over dropoffs:
	if (try_ok) try_ok = P_TryMove (actor, tryx, tryy, dropoff, NULL, tm);

	// [GrafZahl] Interpolating monster movement as it is done here just looks bad
	// so make it switchable
	if (nomonsterinterpolation)
	{
		actor->ClearInterpolation();
	}

	if (try_ok && friction > ORIG_FRICTION)
	{
		actor->SetOrigin(origx, origy, actor->_f_Z(), false);
		movefactor *= FRACUNIT / ORIG_FRICTION_FACTOR / 4;
		actor->Vel.X += FIXED2DBL(FixedMul (deltax, movefactor));
		actor->Vel.Y += FIXED2DBL(FixedMul (deltay, movefactor));
	}

	// [RH] If a walking monster is no longer on the floor, move it down
	// to the floor if it is within MaxStepHeight, presuming that it is
	// actually walking down a step.
	if (try_ok &&
		!((actor->flags & MF_NOGRAVITY) || (actor->flags6 & MF6_CANJUMP))
			&& actor->Z() > actor->floorz && !(actor->flags2 & MF2_ONMOBJ))
	{
		if (actor->Z() <= actor->floorz + actor->MaxStepHeight)
		{
			double savedz = actor->Z();
			actor->SetZ(actor->floorz);
			// Make sure that there isn't some other actor between us and
			// the floor we could get stuck in. The old code did not do this.
			if (!P_TestMobjZ(actor))
			{
				actor->SetZ(savedz);
			}
			else
			{ // The monster just hit the floor, so trigger any actions.
				if (actor->floorsector->SecActTarget != NULL &&
					actor->_f_floorz() == actor->floorsector->floorplane.ZatPoint(actor->PosRelative(actor->floorsector)))
				{
					actor->floorsector->SecActTarget->TriggerAction(actor, SECSPAC_HitFloor);
				}
				P_CheckFor3DFloorHit(actor);
			}
		}
	}

	if (!try_ok)
	{
		// [BC] Don't float in client mode.
		if (((actor->flags6 & MF6_CANJUMP)||(actor->flags & MF_FLOAT)) && tm.floatok &&
			( NETWORK_InClientMode() == false ))
		{ // must adjust height
			double savedz = actor->Z();

			if (actor->Z() < tm.floorz)
				actor->AddZ(actor->FloatSpeed);
			else
				actor->AddZ(-actor->FloatSpeed);


			// [RH] Check to make sure there's nothing in the way of the float
			if (P_TestMobjZ (actor))
			{
				// [BB] If we're the server, tell clients to update the thing's Z position.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_MoveThingExact( actor, CM_Z );

				actor->flags |= MF_INFLOAT;
				return true;
			}
			actor->SetZ(savedz);
		}

		if (!spechit.Size ())
		{
			return false;
		}

		// open any specials
		actor->movedir = DI_NODIR;

		// [BC] Set the thing's movement direction. Also, update the thing's
		// position.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		{
			SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
		}

		// if the special is not a door that can be opened, return false
		//
		// killough 8/9/98: this is what caused monsters to get stuck in
		// doortracks, because it thought that the monster freed itself
		// by opening a door, even if it was moving towards the doortrack,
		// and not the door itself.
		//
		// killough 9/9/98: If a line blocking the monster is activated,
		// return true 90% of the time. If a line blocking the monster is
		// not activated, but some other line is, return false 90% of the
		// time. A bit of randomness is needed to ensure it's free from
		// lockups, but for most cases, it returns the correct result.
		//
		// Do NOT simply return false 1/4th of the time (causes monsters to
		// back out when they shouldn't, and creates secondary stickiness).

		spechit_t spec;
		int good = 0;
		
		if (!(actor->flags6 & MF6_NOTRIGGER))
		{
			while (spechit.Pop (spec))
			{
				// [RH] let monsters push lines, as well as use them
				if (((actor->flags4 & MF4_CANUSEWALLS) && P_ActivateLine (spec.line, actor, 0, SPAC_Use)) ||
					((actor->flags2 & MF2_PUSHWALL) && P_ActivateLine (spec.line, actor, 0, SPAC_Push)))
				{
					good |= spec.line == actor->BlockingLine ? 1 : 2;
				}
			}
		}
		else spechit.Clear();
		return good && ((pr_opendoor() >= 203) ^ (good & 1));
	}
	else
	{
		actor->flags &= ~MF_INFLOAT;
	}
	return true; 
}


//=============================================================================
//
// TryWalk
// Attempts to move actor on
// in its current (ob->moveangle) direction.
// If blocked by either a wall or an actor
// returns FALSE
// If move is either clear or blocked only by a door,
// returns TRUE and sets...
// If a door is in the way,
// an OpenDoor call is made to start it opening.
//
//=============================================================================

bool P_TryWalk (AActor *actor)
{
	if (!P_Move (actor))
	{
		return false;
	}
	actor->movecount = pr_trywalk() & 15;
	return true;
}

//=============================================================================
//
// P_DoNewChaseDir
//
// killough 9/8/98:
//
// Most of P_NewChaseDir(), except for what
// determines the new direction to take
//
//=============================================================================

void P_DoNewChaseDir (AActor *actor, fixed_t deltax, fixed_t deltay)
{
	dirtype_t	d[2];
	int			tdir;
	dirtype_t	olddir, turnaround;
	bool		attempts[NUMDIRS-1]; // We don't need to attempt DI_NODIR.

	memset(&attempts, false, sizeof(attempts));
	olddir = (dirtype_t)actor->movedir;
	turnaround = opposite[olddir];

	if (deltax>10*FRACUNIT)
		d[0]= DI_EAST;
	else if (deltax<-10*FRACUNIT)
		d[0]= DI_WEST;
	else
		d[0]=DI_NODIR;

	if (deltay<-10*FRACUNIT)
		d[1]= DI_SOUTH;
	else if (deltay>10*FRACUNIT)
		d[1]= DI_NORTH;
	else
		d[1]=DI_NODIR;

	// try direct route
	if (d[0] != DI_NODIR && d[1] != DI_NODIR)
	{
		actor->movedir = diags[((deltay<0)<<1) + (deltax>0)];
		if (actor->movedir != turnaround)
		{
			attempts[actor->movedir] = true;
			if (P_TryWalk(actor))
			{
				// [BC] Set the thing's movement direction. Also, update the thing's
				// position.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				{
					SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
				}

				return;
			}
		}
	}

	// try other directions
	if (!(actor->flags5 & MF5_AVOIDINGDROPOFF))
	{
		if ((pr_newchasedir() > 200 || abs(deltay) > abs(deltax)))
		{
			swapvalues (d[0], d[1]);
		}

		if (d[0] == turnaround)
			d[0] = DI_NODIR;
		if (d[1] == turnaround)
			d[1] = DI_NODIR;
	}
		
	if (d[0] != DI_NODIR && attempts[d[0]] == false)
	{
		actor->movedir = d[0];
		attempts[d[0]] = true;
		if (P_TryWalk (actor))
		{
			// [BC] Set the thing's movement direction. Also, update the thing's
			// position.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
			}

			// either moved forward or attacked
			return;
		}
	}

	if (d[1] != DI_NODIR && attempts[d[1]] == false)
	{
		actor->movedir = d[1];
		attempts[d[1]] = true;
		if (P_TryWalk (actor))
		{
			// [BC] Set the thing's movement direction. Also, update the thing's
			// position.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
			}

			return;
		}
	}

	if (!(actor->flags5 & MF5_AVOIDINGDROPOFF))
	{
		// there is no direct path to the player, so pick another direction.
		if (olddir != DI_NODIR && attempts[olddir] == false)
		{
			actor->movedir = olddir;
			attempts[olddir] = true;
			if (P_TryWalk (actor))
			{
				// [BC] Set the thing's movement direction. Also, update the thing's
				// position.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				{
					SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
				}

				return;
			}
		}
	}

	// randomly determine direction of search
	if (pr_newchasedir() & 1)	
	{
		for (tdir = DI_EAST; tdir <= DI_SOUTHEAST; tdir++)
		{
			if (tdir != turnaround && attempts[tdir] == false)
			{
				actor->movedir = tdir;
				attempts[tdir] = true;
				if ( P_TryWalk(actor) )
				{
					// [BC] Set the thing's movement direction. Also, update the thing's
					// position.
					if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					{
						SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
					}

					return;
				}
			}
		}
	}
	else
	{
		for (tdir = DI_SOUTHEAST; tdir != (DI_EAST-1); tdir--)
		{
			if (tdir != turnaround && attempts[tdir] == false)
			{
				actor->movedir = tdir;
				attempts[tdir] = true;
				if ( P_TryWalk(actor) )
				{
					// [BC] Set the thing's movement direction. Also, update the thing's
					// position.
					if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					{
						SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
					}

					return;
				}
			}
		}
	}

	if (turnaround != DI_NODIR && attempts[turnaround] == false)
	{
		actor->movedir =turnaround;
		if ( P_TryWalk(actor) )
		{
			// [BC] Set the thing's movement direction. Also, update the thing's
			// position.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
			}

			return;
		}
	}

	actor->movedir = DI_NODIR;	// cannot move

	// [BC] Set the thing's movement direction. Also, update the thing's
	// position.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
	}
}


//=============================================================================
//
// killough 11/98:
//
// Monsters try to move away from tall dropoffs.
//
// In Doom, they were never allowed to hang over dropoffs,
// and would remain stuck if involuntarily forced over one.
// This logic, combined with p_map.c (P_TryMove), allows
// monsters to free themselves without making them tend to
// hang over dropoffs.
//=============================================================================

//=============================================================================
//
// P_NewChaseDir
//
// killough 9/8/98: Split into two functions
//
//=============================================================================

void P_NewChaseDir(AActor * actor)
{
	fixedvec2 delta;

	actor->strafecount = 0;

	if ((actor->flags5&MF5_CHASEGOAL || actor->goal == actor->target) && actor->goal!=NULL)
	{
		delta = actor->_f_Vec2To(actor->goal);
	}
	else if (actor->target != NULL)
	{
		delta = actor->_f_Vec2To(actor->target);

		if (!(actor->flags6 & MF6_NOFEAR))
		{
			if ((actor->target->player != NULL && (actor->target->player->cheats & CF_FRIGHTENING)) || 
				(actor->flags4 & MF4_FRIGHTENED))
			{
				delta.x = -delta.x;
				delta.y = -delta.y;
			}
		}
	}
	else
	{
		// Don't abort if this happens.
		Printf ("P_NewChaseDir: called with no target\n");
		P_RandomChaseDir(actor);
		return;
	}
	
	// Try to move away from a dropoff
	if (actor->_f_floorz() - actor->dropoffz > actor->MaxDropOffHeight && 
		actor->Z() <= actor->floorz && !(actor->flags & MF_DROPOFF) && 
		!(actor->flags2 & MF2_ONMOBJ) &&
		!(actor->flags & MF_FLOAT) && !(i_compatflags & COMPATF_DROPOFF))
	{
		FBoundingBox box(actor->_f_X(), actor->_f_Y(), actor->_f_radius());
		FBlockLinesIterator it(box);
		line_t *line;

		fixed_t deltax = 0;
		fixed_t deltay = 0;
		while ((line = it.Next()))
		{
			if (line->backsector                     && // Ignore one-sided linedefs
				box.Right()  > line->bbox[BOXLEFT]   &&
				box.Left()   < line->bbox[BOXRIGHT]  &&
				box.Top()    > line->bbox[BOXBOTTOM] && // Linedef must be contacted
				box.Bottom() < line->bbox[BOXTOP]    &&
				box.BoxOnLineSide(line) == -1)
		    {
				fixed_t front = line->frontsector->floorplane.ZatPoint(actor->PosRelative(line));
				fixed_t back  = line->backsector->floorplane.ZatPoint(actor->PosRelative(line));
				angle_t angle;
		
				// The monster must contact one of the two floors,
				// and the other must be a tall dropoff.
				
				if (back == actor->_f_Z() && front < actor->_f_Z() - actor->MaxDropOffHeight)
				{
					angle = R_PointToAngle2(0,0,line->dx,line->dy);   // front side dropoff
				}
				else if (front == actor->_f_Z() && back < actor->_f_Z() - actor->MaxDropOffHeight)
				{
					angle = R_PointToAngle2(line->dx,line->dy,0,0); // back side dropoff
				}
				else continue;
		
				// Move away from dropoff at a standard speed.
				// Multiple contacted linedefs are cumulative (e.g. hanging over corner)
				deltax -= finesine[angle >> ANGLETOFINESHIFT]*32;
				deltay += finecosine[angle >> ANGLETOFINESHIFT]*32;
			}
		}

		if (deltax || deltay) 
		{
			// [Graf Zahl] I have changed P_TryMove to only apply this logic when
			// being called from here. AVOIDINGDROPOFF activates the code that
			// allows monsters to move away from a dropoff. This is different from
			// MBF which requires unconditional use of the altered logic and therefore
			// forcing a massive change in the monster behavior to use this.

			// use different dropoff movement logic in P_TryMove
			actor->flags5|=MF5_AVOIDINGDROPOFF;
			P_DoNewChaseDir(actor, deltax, deltay);
			actor->flags5&=~MF5_AVOIDINGDROPOFF;
		
			// If moving away from dropoff, set movecount to 1 so that 
			// small steps are taken to get monster away from dropoff.
			actor->movecount = 1;
			return;
		}
	}

#if 0
	// Move away from friends when too close, except
	// in certain situations (e.g. a crowded lift)

	// MBF code for friends. Cannot be done in ZDoom but left here as a reminder for later implementation.

	if (actor->flags & target->flags & MF_FRIEND &&
	    distfriend << FRACBITS > dist && 
	    !P_IsOnLift(target) && !P_IsUnderDamage(actor))
	  deltax = -deltax, deltay = -deltay;
	else
#endif

	// MBF's monster_backing option. Made an actor flag instead. Also cleaned the code up to make it readable.
	// Todo: implement the movement logic
	AActor *target = actor->target;
	if (target->health > 0 && !actor->IsFriend(target) && target != actor->goal)
    {   // Live enemy target

		if (actor->flags3 & MF3_AVOIDMELEE)
		{
			bool ismeleeattacker = false;
			fixed_t dist = actor->AproxDistance(target);
			if (target->player == NULL)
			{
				ismeleeattacker = (target->MissileState == NULL && dist < (target->meleerange + target->_f_radius())*2);
			}
			else if (target->player->ReadyWeapon != NULL)
			{
				// melee range of player weapon is a parameter of the action function and cannot be checked here.
				// Add a new weapon property?
				ismeleeattacker = (target->player->ReadyWeapon->WeaponFlags & WIF_MELEEWEAPON && dist < (192 << FRACBITS));
			}
			if (ismeleeattacker)
			{
				actor->strafecount = pr_enemystrafe() & 15;
				delta.x = -delta.x, delta.y = -delta.y;
			}
	    }
	}

	P_DoNewChaseDir(actor, delta.x, delta.y);

	// If strafing, set movecount to strafecount so that old Doom
	// logic still works the same, except in the strafing part

	if (actor->strafecount)
		actor->movecount = actor->strafecount;

}




//=============================================================================
//
// P_RandomChaseDir
//
//=============================================================================

void P_RandomChaseDir (AActor *actor)
{
	dirtype_t olddir, turnaround;
	int tdir, i;

	olddir = (dirtype_t)actor->movedir;
	turnaround = opposite[olddir];
	int turndir;

	// Friendly monsters like to head toward a player
	if (actor->flags & MF_FRIENDLY)
	{
		AActor *player;
		fixedvec2 delta;
		dirtype_t d[3];

		// [BB] Don't try to head towards a spectator.
		if ( (actor->FriendPlayer != 0 ) && ( players[actor->FriendPlayer - 1].bSpectating == false ) )
		{
			player = players[i = actor->FriendPlayer - 1].mo;
		}
		else
		{
			if ( ( NETWORK_GetState( ) == NETSTATE_SINGLE )
				// [BB] On the server it's possible that there are no players. In this case the
				// for loop below would get stuck, so we may not enter it.
				|| ( ( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( SERVER_CalcNumPlayers() == 0 ) ) )
			{
				i = 0;
			}
			else for (i = pr_newchasedir() & (MAXPLAYERS-1); !playeringame[i]; i = (i+1) & (MAXPLAYERS-1))
			{
			}
			player = players[i].mo;
		}
		if (player != NULL && playeringame[i])
		{
			// [BB] The else block above possibly selects a spectating player. In that case
			// don't try to move towards the spectator. This is not exactly the same as
			// skipping spectators in the above loop, but should work well enough.
			if ((pr_newchasedir() & 1 || !P_CheckSight (actor, player))
				&& player->player && ( player->player->bSpectating == false ) )
			{
				delta = actor->_f_Vec2To(player);

				if (delta.x>128*FRACUNIT)
					d[1]= DI_EAST;
				else if (delta.x<-128*FRACUNIT)
					d[1]= DI_WEST;
				else
					d[1]=DI_NODIR;

				if (delta.y<-128*FRACUNIT)
					d[2]= DI_SOUTH;
				else if (delta.y>128*FRACUNIT)
					d[2]= DI_NORTH;
				else
					d[2]=DI_NODIR;

				// try direct route
				if (d[1] != DI_NODIR && d[2] != DI_NODIR)
				{
					actor->movedir = diags[((delta.y<0)<<1) + (delta.x>0)];
					if (actor->movedir != turnaround && P_TryWalk(actor))
					{
						// [BC] Set the thing's movement direction. Also, update the thing's
						// position.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						{
							SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
						}
						return;
					}
				}

				// try other directions
				if (pr_newchasedir() > 200 || abs(delta.y) > abs(delta.x))
				{
					swapvalues (d[1], d[2]);
				}

				if (d[1] == turnaround)
					d[1] = DI_NODIR;
				if (d[2] == turnaround)
					d[2] = DI_NODIR;
					
				if (d[1] != DI_NODIR)
				{
					actor->movedir = d[1];
					if (P_TryWalk (actor))
					{
						// [BC] Set the thing's movement direction. Also, update the thing's
						// position.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						{
							SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
						}

						// either moved forward or attacked
						return;
					}
				}

				if (d[2] != DI_NODIR)
				{
					actor->movedir = d[2];
					if (P_TryWalk (actor))
					{
						// [BC] Set the thing's movement direction. Also, update the thing's
						// position.
						if ( NETWORK_GetState( ) == NETSTATE_SERVER )
						{
							SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
						}

						return;
					}
				}
			}
		}
	}

	// If the actor elects to continue in its current direction, let it do
	// so unless the way is blocked. Then it must turn.
	if (pr_newchasedir() < 150)
	{
		if (P_TryWalk (actor))
		{
			// [BB] Set the thing's position.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z );

			return;
		}
	}

	turndir = (pr_newchasedir() & 1) ? -1 : 1;

	if (olddir == DI_NODIR)
	{
		olddir = (dirtype_t)(pr_newchasedir() & 7);
	}
	for (tdir = (olddir + turndir) & 7; tdir != olddir; tdir = (tdir + turndir) & 7)
	{
		if (tdir != turnaround)
		{
			actor->movedir = tdir;
			if (P_TryWalk (actor))
			{
				// [BC] Set the thing's movement direction. Also, update the thing's
				// position.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				{
					SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
				}

				return;
			}
		}
	}
/*
	if (pr_newchasedir() & 1)
	{
		for (tdir = olddir; tdir <= DI_SOUTHEAST; ++tdir)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;
				if (P_TryWalk (actor))
					return;
			}
		}
	}
	else
	{
		for (tdir = DI_SOUTHEAST; tdir >= DI_EAST; --tdir)
		{
			if (tdir != turnaround)
			{
				actor->movedir = tdir;
				if (P_TryWalk (actor))
					return;
			}
		}
	}
*/
	if (turnaround != DI_NODIR)
	{
		actor->movedir = turnaround;
		if (P_TryWalk (actor))
		{
			// [BC] Set the thing's movement direction. Also, update the thing's
			// position.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
			}

			actor->movecount = pr_newchasedir() & 15;
			return;
		}
	}

	actor->movedir = DI_NODIR;	// cannot move

	// [BC] Set the thing's movement direction. Also, update the thing's
	// position.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
	{
		SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z|CM_MOVEDIR );
	}
}

//---------------------------------------------------------------------------
//
// P_IsVisible
//
// killough 9/9/98: whether a target is visible to a monster
// Extended to handle all A_LookEx related checking, too.
//
//---------------------------------------------------------------------------

bool P_IsVisible(AActor *lookee, AActor *other, INTBOOL allaround, FLookExParams *params)
{
	fixed_t maxdist;
	fixed_t mindist;
	angle_t fov;

	if (params != NULL)
	{
		maxdist = params->maxdist;
		mindist = params->mindist;
		fov = params->fov;
	}
	else
	{
		mindist = maxdist = 0;
		fov = allaround ? 0 : ANGLE_180;
	}

	fixed_t dist = lookee->AproxDistance (other);

	if (maxdist && dist > maxdist)
		return false;			// [KS] too far

	if (mindist && dist < mindist)
		return false;			// [KS] too close

	if (fov && fov < ANGLE_MAX)
	{
		angle_t an = lookee->__f_AngleTo(other) - lookee->_f_angle();

		if (an > (fov / 2) && an < (ANGLE_MAX - (fov / 2)))
		{
			// if real close, react anyway
			// [KS] but respect minimum distance rules
			if (mindist || dist > lookee->meleerange + lookee->_f_radius())
				return false;	// outside of fov
		}
	}

	// P_CheckSight is by far the most expensive operation in here so let's do it last.
	return P_CheckSight(lookee, other, SF_SEEPASTSHOOTABLELINES);
}

//---------------------------------------------------------------------------
//
// FUNC P_LookForMonsters
//
//---------------------------------------------------------------------------

#define MONS_LOOK_RANGE (20*64*FRACUNIT)
#define MONS_LOOK_LIMIT 64

bool P_LookForMonsters (AActor *actor)
{
	int count;
	AActor *mo;
	TThinkerIterator<AActor> iterator;

	// [BC] This is handled server side.
	if ( NETWORK_InClientMode() )
		return ( false );

	if (!P_CheckSight (players[0].mo, actor, SF_SEEPASTBLOCKEVERYTHING))
	{ // Player can't see monster
		return false;
	}
	count = 0;
	while ( (mo = iterator.Next ()) )
	{
		if (!(mo->flags3 & MF3_ISMONSTER) || (mo == actor) || (mo->health <= 0))
		{ // Not a valid monster
			continue;
		}
		if (mo->AproxDistance (actor) > MONS_LOOK_RANGE)
		{ // Out of range
			continue;
		}
		if (pr_lookformonsters() < 16)
		{ // Skip
			continue;
		}
		if (++count >= MONS_LOOK_LIMIT)
		{ // Stop searching
			return false;
		}
		if (mo->GetSpecies() == actor->GetSpecies())
		{ // [RH] Don't go after same species
			continue;
		}
		if (!P_CheckSight (actor, mo, SF_SEEPASTBLOCKEVERYTHING))
		{ // Out of sight
			continue;
		}
		// Found a target monster
		actor->target = mo;
		return true;
	}
	return false;
}

//============================================================================
//
// LookForTIDinBlock
//
// Finds a target with the specified TID in a mapblock. Alternatively, it
// can find a target with a specified TID if something in this mapblock is
// already targetting it.
//
//============================================================================

AActor *LookForTIDInBlock (AActor *lookee, int index, void *extparams)
{
	FLookExParams *params = (FLookExParams *)extparams;
	FBlockNode *block;
	AActor *link;
	AActor *other;
	
	for (block = blocklinks[index]; block != NULL; block = block->NextActor)
	{
		link = block->Me;

        if (!(link->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (link == lookee)
			continue;

		if (link->health <= 0)
			continue;			// dead

		if (link->flags2 & MF2_DORMANT)
			continue;			// don't target dormant things

		if (link->tid == lookee->TIDtoHate)
		{
			other = link;
		}
		else if (link->target != NULL && link->target->tid == lookee->TIDtoHate)
		{
			other = link->target;
			if (!(other->flags & MF_SHOOTABLE) ||
				other->health <= 0 ||
				(other->flags2 & MF2_DORMANT))
			{
				continue;
			}
		}
		else
		{
			continue;
		}

		if (!(lookee->flags3 & MF3_NOSIGHTCHECK))
		{
			if (!P_IsVisible(lookee, other, true, params))
				continue;			// out of sight
		}

		return other;
	}
	return NULL;
}

//============================================================================
//
// P_LookForTID
//
// Selects a live monster with the given TID
//
//============================================================================

bool P_LookForTID (AActor *actor, INTBOOL allaround, FLookExParams *params)
{
	AActor *other;
	bool reachedend = false;
	bool chasegoal = params? (!(params->flags & LOF_DONTCHASEGOAL)) : true;

	other = P_BlockmapSearch (actor, 0, LookForTIDInBlock, params);

	if (other != NULL)
	{
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = other;
		actor->LastLookActor = other;
		return true;
	}

	// The actor's TID could change because of death or because of
	// Thing_ChangeTID. If it's not what we expect, then don't use
	// it as a base for the iterator.
	if (actor->LastLookActor != NULL &&
		actor->LastLookActor->tid != actor->TIDtoHate)
	{
		actor->LastLookActor = NULL;
	}

	FActorIterator iterator (actor->TIDtoHate, actor->LastLookActor);
	int c = (pr_look3() & 31) + 7;	// Look for between 7 and 38 hatees at a time
	while ((other = iterator.Next()) != actor->LastLookActor)
	{
		if (other == NULL)
		{
			if (reachedend)
			{
				// we have cycled through the entire list at least once
				// so let's abort because even if we continue nothing can
				// be found.
				break;
			}
			reachedend = true;
			continue;
		}

		if (!(other->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (other == actor)
			continue;			// don't hate self

		if (other->health <= 0)
			continue;			// dead

		if (other->flags2 & MF2_DORMANT)
			continue;			// don't target dormant things

		if (--c == 0)
			break;

		if (!(actor->flags3 & MF3_NOSIGHTCHECK))
		{
			if (!P_IsVisible (actor, other, !!allaround, params))
				continue;			// out of sight
		}
		
		// [RH] Need to be sure the reactiontime is 0 if the monster is
		//		leaving its goal to go after something else.
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = other;
		actor->LastLookActor = other;
		return true;
	}
	actor->LastLookActor = other;
	if (actor->target == NULL)
	{
		// [RH] use goal as target
		if (actor->goal != NULL && chasegoal)
		{
			actor->target = actor->goal;
			return true;
		}
		// Use last known enemy if no hatee sighted -- killough 2/15/98:
		if (actor->lastenemy != NULL && actor->lastenemy->health > 0)
		{
			if (!actor->IsFriend(actor->lastenemy))
			{
				actor->target = actor->lastenemy;
				actor->lastenemy = NULL;
				return true;
			}
			else
			{
				actor->lastenemy = NULL;
			}
		}
	}
	return false;
}

//============================================================================
//
// LookForEnemiesinBlock
//
// Finds a non-friendly monster in a mapblock. It can also use targets of
// friendlies in this mapblock to find non-friendlies in other mapblocks.
//
//============================================================================

AActor *LookForEnemiesInBlock (AActor *lookee, int index, void *extparam)
{
	FBlockNode *block;
	AActor *link;
	AActor *other;
	FLookExParams *params = (FLookExParams *)extparam;
	
	for (block = blocklinks[index]; block != NULL; block = block->NextActor)
	{
		link = block->Me;

        if (!(link->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (link == lookee)
			continue;

		if (link->health <= 0)
			continue;			// dead

		if (link->flags2 & MF2_DORMANT)
			continue;			// don't target dormant things

		if (!(link->flags3 & MF3_ISMONSTER))
			continue;			// don't target it if it isn't a monster (could be a barrel)

		if (link->flags7 & MF7_NEVERTARGET)
			continue;

		other = NULL;
		if (link->flags & MF_FRIENDLY)
		{
			if (!lookee->IsFriend(link))
			{
				// This is somebody else's friend, so go after it
				other = link;
			}
			else if (link->target != NULL && !(link->target->flags & MF_FRIENDLY))
			{
				other = link->target;
				if (!(other->flags & MF_SHOOTABLE) ||
					other->health <= 0 ||
					(other->flags2 & MF2_DORMANT))
				{
					other = NULL;;
				}
			}
		}
		else
		{
			other = link;
		}

		// [MBF] If the monster is already engaged in a one-on-one attack
		// with a healthy friend, don't attack around 60% the time.
		
		// [GrafZahl] This prevents friendlies from attacking all the same 
		// target.
		
		if (other)
		{
			AActor *targ = other->target;
			if (targ && targ->target == other && pr_skiptarget() > 100 && lookee->IsFriend (targ) &&
				targ->health*2 >= targ->SpawnHealth())
			{
				continue;
			}
		}

		// [KS] Hey, shouldn't there be a check for MF3_NOSIGHTCHECK here?

		if (other == NULL || !P_IsVisible (lookee, other, true, params))
			continue;			// out of sight


		return other;
	}
	return NULL;
}

//============================================================================
//
// P_LookForEnemies
//
// Selects a live enemy monster
//
//============================================================================

bool P_LookForEnemies (AActor *actor, INTBOOL allaround, FLookExParams *params)
{
	AActor *other;

	other = P_BlockmapSearch (actor, 10, LookForEnemiesInBlock, params);

	if (other != NULL)
	{
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = other;
//		actor->LastLook.Actor = other;
		return true;
	}

	if (actor->target == NULL)
	{
		// [RH] use goal as target
		if (actor->goal != NULL)
		{
			actor->target = actor->goal;
			return true;
		}
		// Use last known enemy if no hatee sighted -- killough 2/15/98:
		if (actor->lastenemy != NULL && actor->lastenemy->health > 0)
		{
			if (!actor->IsFriend(actor->lastenemy))
			{
				actor->target = actor->lastenemy;
				actor->lastenemy = NULL;
				return true;
			}
			else
			{
				actor->lastenemy = NULL;
			}
		}
	}
	return false;
}

/*
================
=
= P_LookForPlayers
=
= If allaround is false, only look 180 degrees in front
= returns true if a player is targeted
================
*/

bool P_LookForPlayers (AActor *actor, INTBOOL allaround, FLookExParams *params)
{
	int			pnum;
	player_t*	player;
	bool chasegoal = params? (!(params->flags & LOF_DONTCHASEGOAL)) : true;
	// [BC] Could new variables.
	ULONG		ulIdx;
	bool		abSearched[MAXPLAYERS];
	bool		bAllDone;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return ( false );
	}

	// [BC]
	//
	// This function had to be completely rewritten. Because of how it was written, for some
	// reason, the actors passed into this function were doing far more sight checks than
	// actually necessary (it seemed to be doing them repeatedly on a single player, with the
	// number of times somehow linked with MAXPLAYERS). This problem became extremely apparent
	// on levels with many, many actors (nuts2.wad for example), after I upped MAXPLAYERS to 32.
	// The framerate became unbearable. Anyway, this function has been completely rewritten (as
	// well as cleaned), and now works properly. Framerates are sky high once again.

	if (actor->TIDtoHate != 0)
	{
		if (P_LookForTID (actor, allaround, params))
		{
			return true;
		}
		if (!(actor->flags3 & MF3_HUNTPLAYERS))
		{
			return false;
		}
	}
	else if (actor->flags & MF_FRIENDLY)
	{
		bool result = P_LookForEnemies (actor, allaround, params);

#ifdef MBF_FRIENDS
		if (!result && (actor->flags & MF_FRIEND_MBF))
		{  // killough 9/9/98: friendly monsters go about players differently

			// Go back to a player, no matter whether it's visible or not
			for (int anyone=0; anyone<=1; anyone++)
			{
				for (int c=0; c<MAXPLAYERS; c++)
				{
					if (playeringame[c] && players[c].playerstate==PST_LIVE &&
						actor->IsFriend(players[c].mo) &&
						(anyone || P_IsVisible(actor, players[c].mo, allaround)))
					{
						actor->target = players[c].mo;

						// killough 12/98:
						// get out of refiring loop, to avoid hitting player accidentally

						if (actor->MissileState != NULL)
						{
							actor->SetState(actor->SeeState, true);
							actor->flags &= ~MF_JUSTHIT;
						}

						return true;
					}
				}
			}
		}
#endif
		// [SP] If you don't see any enemies in deathmatch, look for players (but only when friend to a specific player.)
		// [BB] TEAM_NONE -> TEAM_None
		if (actor->FriendPlayer == 0 && (!teamplay || actor->GetTeam() == TEAM_None)) return result;
		// [BB] Adapted to take into account teamgame.
		if (result || (!deathmatch && !teamgame)) return true;


	}	// [SP] if false, and in deathmatch, intentional fall-through

	if (!(gameinfo.gametype & (GAME_DoomStrifeChex)) &&
		( NETWORK_GetState( ) == NETSTATE_SINGLE ) &&
		players[0].health <= 0 && 
		actor->goal == NULL &&
		gamestate != GS_TITLELEVEL
		)
	{ // Single player game and player is dead; look for monsters
		return P_LookForMonsters (actor);
	}

	bAllDone = false;
	for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		abSearched[ulIdx] = false;

	// Begin searching for players.
	while ( 1 )
	{
		if ( bAllDone )
		{
			pnum = MAXPLAYERS;
			break;
		}

		// [BB] We have to make sure that P_Random (the old RNG from Doom) is not used here
		// (it is called by M_Random() if ZACOMPATF_OLD_RANDOM_GENERATOR) since it never
		// returns a few of the integers in [0,255] causing this while loop to never terminate.
		pnum = M_Random( MAXPLAYERS );
		if ( abSearched[pnum] == true )
			continue;

		abSearched[pnum] = true;
		bAllDone = true;
		for ( ulIdx = 0; ulIdx < MAXPLAYERS; ulIdx++ )
		{
			if ( abSearched[ulIdx] == false )
			{
				bAllDone = false;
				break;
			}
		}

		if (!playeringame[pnum])
			continue;

		if (actor->TIDtoHate == 0)
		{
			actor->LastLookPlayerNumber = pnum;
		}

		player = &players[pnum];
		if ( player->mo == NULL )
			continue;

		if (!(player->mo->flags & MF_SHOOTABLE))
			continue;			// not shootable (observer or dead)

		if (player->cheats & CF_NOTARGET)
			continue;			// no target

		if (player->health <= 0)
			continue;			// dead

		// [BC] In invasion mode, player doesn't have to be visible to be chased by monsters.
		if ((!P_IsVisible (actor, player->mo, allaround, params)) && ( invasion == false ))
			continue;			// out of sight

		// [SP] Deathmatch fixes - if we have MF_FRIENDLY we're definitely in deathmatch
		// We're going to ignore our master, but go after his enemies.
		if ( actor->flags & MF_FRIENDLY )
		{
			if ( actor->IsFriend(player->mo) )
				continue;
		}

		// [RC] Well, let's let special monsters with this flag active be able to see
		// the player then, eh?
		if(!(actor->flags6 & MF6_SEEINVISIBLE)) 
		{
			if ((player->mo->flags & MF_SHADOW && !(i_compatflags & COMPATF_INVISIBILITY)) ||
				player->mo->flags3 & MF3_GHOST)
			{
				if ((player->mo->AproxDistance (actor) > (128 << FRACBITS))
					&& P_AproxDistance (player->mo->_f_velx(), player->mo->_f_vely())	< 5*FRACUNIT)
				{ // Player is sneaking - can't detect
					continue;
				}
				if (pr_lookforplayers() < 225)
				{ // Player isn't sneaking, but still didn't detect
					continue;
				}
			}
		}
		
		// [RH] Need to be sure the reactiontime is 0 if the monster is
		//		leaving its goal to go after a player.
		if (actor->goal && actor->target == actor->goal)
			actor->reactiontime = 0;

		actor->target = player->mo;
		return true;
	}

	if ( pnum == MAXPLAYERS )
	{
		// done looking
		if (actor->target == NULL)
		{
			// [RH] use goal as target
			if (actor->goal != NULL)
			{
				actor->target = actor->goal;
				return true;
			}
			// Use last known enemy if no players sighted -- killough 2/15/98:
			if (actor->lastenemy != NULL && actor->lastenemy->health > 0)
			{
				if (!actor->IsFriend(actor->lastenemy))
				{
					actor->target = actor->lastenemy;
					actor->lastenemy = NULL;
					return true;
				}
				else
				{
					actor->lastenemy = NULL;
				}
			}
		}
		return actor->target == actor->goal && actor->goal != NULL;
	}

	return false;
}

//
// ACTION ROUTINES
//

//
// A_Look
// Stay in state until a player is sighted.
// [RH] Will also leave state to move to goal.
//
DEFINE_ACTION_FUNCTION(AActor, A_Look)
{
	PARAM_ACTION_PROLOGUE;

	AActor *targ;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		// [RH] Andy Baker's stealth monsters
		if (self->flags & MF_STEALTH)
		{
			self->visdir = -1;
		}

		return 0;
	}

	if (self->flags5 & MF5_INCONVERSATION)
		return 0;

	// [RH] Set goal now if appropriate
	if (self->special == Thing_SetGoal && self->args[0] == 0) 
	{
		NActorIterator iterator (NAME_PatrolPoint, self->args[1]);
		self->special = 0;
		self->goal = iterator.Next ();
		self->reactiontime = self->args[2] * TICRATE + level.maptime;
		if (self->args[3] == 0) self->flags5 &= ~MF5_CHASEGOAL;
		else self->flags5 |= MF5_CHASEGOAL;
	}

	self->threshold = 0;		// any shot will wake up

	if (self->TIDtoHate != 0)
	{
		targ = self->target;
	}
	else
	{
		targ = (i_compatflags & COMPATF_SOUNDTARGET || self->flags & MF_NOSECTOR)? 
			self->Sector->SoundTarget : self->LastHeard;

		// [RH] If the soundtarget is dead, don't chase it
		if (targ != NULL && targ->health <= 0)
		{
			targ = NULL;
		}

		if (targ && targ->player && (targ->player->cheats & CF_NOTARGET))
		{
			return 0;
		}
	}

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = -1;
	}

	if (targ && (targ->flags & MF_SHOOTABLE))
	{
		if (self->IsFriend (targ))	// be a little more precise!
		{
			// If we find a valid target here, the wandering logic should *not*
			// be activated! It would cause the seestate to be set twice.
			if (P_LookForPlayers (self, self->flags4 & MF4_LOOKALLAROUND, NULL))
				goto seeyou;

			// Let the self wander around aimlessly looking for a fight
			if (self->SeeState != NULL)
			{
				// [BC] Tell clients to set the thing's state.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( self, STATE_SEE );

				self->SetState (self->SeeState);
			}
			else
			{
				A_Wander(self);
			}
		}
		else
		{
			self->target = targ;

			if (self->flags & MF_AMBUSH)
			{
				if (P_CheckSight (self, self->target, SF_SEEPASTBLOCKEVERYTHING))
					goto seeyou;
			}
			else
				goto seeyou;
		}
	}
	
	if (!P_LookForPlayers (self, self->flags4 & MF4_LOOKALLAROUND, NULL))
		return 0;
				
	// go into chase state
  seeyou:
	// [RH] Don't start chasing after a goal if it isn't time yet.
	if (self->target == self->goal)
	{
		if (self->reactiontime > level.maptime)
			self->target = NULL;
	}
	else if (self->SeeSound)
	{
		if (self->flags2 & MF2_BOSS)
		{ // full volume
			S_Sound (self, CHAN_VOICE, self->SeeSound, 1, ATTN_NONE, true );	// [BC] Inform the clients.
		}
		else
		{
			S_Sound (self, CHAN_VOICE, self->SeeSound, 1, ATTN_NORM, true );	// [BC] Inform the clients.
		}
	}

	if (self->target)
	{
		// [BC] If we are the server, tell clients about the state change.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingState( self, STATE_SEE );

		self->SetState (self->SeeState);
	}
	return 0;
}


//==========================================================================
//
// A_LookEx (int flags, fixed minseedist, fixed maxseedist, fixed maxheardist, fixed fov, state wakestate)
// [KS] Borrowed the A_Look code to make a parameterized version.
//
//==========================================================================

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_LookEx)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_INT_OPT	(flags)			{ flags = 0; }
	PARAM_FIXED_OPT	(minseedist)	{ minseedist = 0; }
	PARAM_FIXED_OPT	(maxseedist)	{ maxseedist = 0; }
	PARAM_FIXED_OPT (maxheardist)	{ maxheardist = 0; }
	PARAM_FLOAT_OPT (fov_f)			{ fov_f = 0; }
	PARAM_STATE_OPT	(seestate)		{ seestate = NULL; }

	AActor *targ = NULL; // Shuts up gcc
	fixed_t dist;
	angle_t fov = (fov_f == 0) ? ANGLE_180 : FLOAT2ANGLE(fov_f);
	FLookExParams params = { fov, minseedist, maxseedist, maxheardist, flags, seestate };

	// [TP] The client does not execute this
	if ( NETWORK_InClientMode() )
	{
		// [RH] Andy Baker's stealth monsters
		if (self->flags & MF_STEALTH)
		{
			self->visdir = -1;
		}

		return 0;
	}

	if (self->flags5 & MF5_INCONVERSATION)
		return 0;

	// [RH] Set goal now if appropriate
	if (self->special == Thing_SetGoal && self->args[0] == 0) 
	{
		NActorIterator iterator(NAME_PatrolPoint, self->args[1]);
		self->special = 0;
		self->goal = iterator.Next ();
		self->reactiontime = self->args[2] * TICRATE + level.maptime;
		if (self->args[3] == 0)
			self->flags5 &= ~MF5_CHASEGOAL;
		else
			self->flags5 |= MF5_CHASEGOAL;
	}

	self->threshold = 0;		// any shot will wake up

	if (self->TIDtoHate != 0)
	{
		targ = self->target;
	}
	else
	{
		if (!(flags & LOF_NOSOUNDCHECK))
		{
			targ = (i_compatflags & COMPATF_SOUNDTARGET || self->flags & MF_NOSECTOR)?
				self->Sector->SoundTarget : self->LastHeard;
			if (targ != NULL)
			{
				// [RH] If the soundtarget is dead, don't chase it
				if (targ->health <= 0)
				{
					targ = NULL;
				}
				else
				{
					dist = self->AproxDistance (targ);

					// [KS] If the target is too far away, don't respond to the sound.
					if (maxheardist && dist > maxheardist)
					{
						targ = NULL;
						self->LastHeard = NULL;
					}
				}
			}
        }
        
        if (targ && targ->player && (targ->player->cheats & CF_NOTARGET))
        {
            return 0;
        }
	}

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = -1;
	}

	if (targ && (targ->flags & MF_SHOOTABLE))
	{
		if (self->IsFriend (targ))	// be a little more precise!
		{
			if (!(self->flags4 & MF4_STANDSTILL))
			{
				if (!(flags & LOF_NOSIGHTCHECK))
				{
					// If we find a valid target here, the wandering logic should *not*
					// be activated! If would cause the seestate to be set twice.
					if (P_LookForPlayers(self, true, &params))
						goto seeyou;
				}

				// Let the self wander around aimlessly looking for a fight
                if (!(self->flags & MF_INCHASE))
                {
                    if (seestate)
                    {
						// [TP] Tell clients to set the thing's state.
						if ( NETWORK_GetState() == NETSTATE_SERVER )
							SERVERCOMMANDS_SetThingFrame( self, seestate );

                        self->SetState (seestate);
                    }
                    else
                    {
                        if (self->SeeState != NULL)
                        {
							// [TP] Tell clients to set the thing's state.
							if ( NETWORK_GetState() == NETSTATE_SERVER )
								SERVERCOMMANDS_SetThingState( self, STATE_SEE );

                            self->SetState (self->SeeState);
                        }
                        else
                        {
                            A_Wander(self);
                        }
                    }
                }
			}
		}
		else
		{
			self->target = targ; //We already have a target?
            
            // [KS] The target can become ourselves in rare circumstances (like
            // if we committed suicide), so if that's the case, just ignore it.
            if (self->target == self) self->target = NULL;

			if (self->target != NULL)
			{
				if (self->flags & MF_AMBUSH)
				{
					dist = self->AproxDistance (self->target);
					if (P_CheckSight (self, self->target, SF_SEEPASTBLOCKEVERYTHING) &&
						(!minseedist || dist > minseedist) &&
						(!maxseedist || dist < maxseedist))
					{
						goto seeyou;
					}
				}
				else
					goto seeyou;
			}
		}
	}

	if (!(flags & LOF_NOSIGHTCHECK))
	{
		if (!P_LookForPlayers(self, true, &params))
			return 0;
	}
	else
	{
		return 0;
	}
				
	// go into chase state
  seeyou:
	// [RH] Don't start chasing after a goal if it isn't time yet.
	if (self->target == self->goal)
	{
		if (self->reactiontime > level.maptime)
			self->target = NULL;
	}
	else if (self->SeeSound && !(flags & LOF_NOSEESOUND))
	{
		if (flags & LOF_FULLVOLSEESOUND)
		{ // full volume
			S_Sound (self, CHAN_VOICE, self->SeeSound, 1, ATTN_NONE, true );	// [TP] Inform the clients.
		}
		else
		{
			S_Sound (self, CHAN_VOICE, self->SeeSound, 1, ATTN_NORM, true );	// [TP] Inform the clients.
		}
	}

	if (self->target && !(self->flags & MF_INCHASE))
	{
        if (!(flags & LOF_NOJUMP))
        {
            if (seestate)
            {
			// [TP] Tell clients to set the thing's state.
			if ( NETWORK_GetState() == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingFrame( self, seestate );

                self->SetState (seestate);
            }
            else
            {
			// [TP] Tell clients to set the thing's state.
			if ( NETWORK_GetState() == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingState( self, STATE_SEE );

                self->SetState (self->SeeState);
            }
        }
	}
	return 0;
}

// [KS] *** End additions by me ***

//==========================================================================
//
// A_ClearLastHeard
//
//==========================================================================

DEFINE_ACTION_FUNCTION(AActor, A_ClearLastHeard)
{
	PARAM_ACTION_PROLOGUE;
	self->LastHeard = NULL;
	return 0;
}

//==========================================================================
//
// A_Wander
//
//==========================================================================
enum ChaseFlags
{
	CHF_FASTCHASE = 1,
	CHF_NOPLAYACTIVE = 2,
	CHF_NIGHTMAREFAST = 4,
	CHF_RESURRECT = 8,
	CHF_DONTMOVE = 16,
	CHF_NORANDOMTURN = 32,
	CHF_NODIRECTIONTURN = 64,
	CHF_NOPOSTATTACKTURN = 128,
	CHF_STOPIFBLOCKED = 256,
};

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_Wander)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_INT_OPT(flags) { flags = 0; }	
	A_Wander(self, flags);
	return 0;
}

// [MC] I had to move this out from within A_Wander in order to allow flags to 
// pass into it. That meant replacing the CALL_ACTION(A_Wander) functions with
// just straight up defining A_Wander in order to compile. Looking around though,
// actors from the games themselves just do a straight A_Chase call itself so
// I saw no harm in it.

void A_Wander(AActor *self, int flags)
{
	// [RH] Strife probably clears this flag somewhere, but I couldn't find where.
	// This seems as good a place as any.
	self->flags4 &= ~MF4_INCOMBAT;

	if (self->flags5 & MF5_INCONVERSATION)
		return;

	if (self->flags4 & MF4_STANDSTILL)
		return;

	if (self->reactiontime != 0)
	{
		self->reactiontime--;
		return;
	}

	// turn towards movement direction if not there yet
	if (!(flags & CHF_NODIRECTIONTURN) && (self->movedir < DI_NODIR))
	{
		self->Angles.Yaw = floor(self->Angles.Yaw.Degrees / 45) * 45.;
		DAngle delta = deltaangle(self->Angles.Yaw, (self->movedir * 45));
		if (delta < 0)
		{
			self->Angles.Yaw -= 45;
		}
		else if (delta > 0)
		{
			self->Angles.Yaw += 45;
		}
	}

	// [BC] In client mode, just keep walking until the server tells us to
	// change directions.
	if ( NETWORK_InClientMode() )
	{
		P_Move( self );
		return;
	}

	if ((--self->movecount < 0 && !(flags & CHF_NORANDOMTURN)) || (!P_Move(self) && !(flags & CHF_STOPIFBLOCKED)))
	{
		P_RandomChaseDir(self);
		self->movecount += 5;
	}
	return;
}

//==========================================================================
//
// A_Look2
//
//==========================================================================
DEFINE_ACTION_FUNCTION(AActor, A_Look2)
{
	PARAM_ACTION_PROLOGUE;

	AActor *targ;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return 0;
	}

	if (self->flags5 & MF5_INCONVERSATION)
		return 0;

	self->threshold = 0;
	targ = self->LastHeard;

	if (targ != NULL && targ->health <= 0)
	{
		targ = NULL;
	}

	if (targ && (targ->flags & MF_SHOOTABLE))
	{
		if ((level.flags & LEVEL_NOALLIES) ||
			(self->flags & MF_FRIENDLY) != (targ->flags & MF_FRIENDLY))
		{
			if (self->flags & MF_AMBUSH)
			{
				if (!P_CheckSight (self, targ, SF_SEEPASTBLOCKEVERYTHING))
					goto nosee;
			}
			self->target = targ;
			self->threshold = 10;

			// [BC] Tell clients to set the thing's state.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingState( self, STATE_SEE );

			self->SetState (self->SeeState);
			return 0;
		}
		else
		{
			if (!P_LookForPlayers (self, self->flags4 & MF4_LOOKALLAROUND, NULL))
				goto nosee;

			// [BC] Tell clients to set the thing's state.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingState( self, STATE_SEE );

			self->SetState (self->SeeState);
			self->flags4 |= MF4_INCOMBAT;
			return 0;
		}
	}
nosee:
	if (pr_look2() < 30)
	{
		// [BC] Tell clients to set the thing's state.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingFrame( self, self->SpawnState + ( pr_look2( ) & 1 ) + 1 );

		self->SetState (self->SpawnState + (pr_look2() & 1) + 1);
	}
	if (!(self->flags4 & MF4_STANDSTILL) && pr_look2() < 40)
	{
		// [BC] Tell clients to set the thing's state.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingFrame( self, self->SpawnState + 3 );

		self->SetState (self->SpawnState + 3);
	}
	return 0;
}

//=============================================================================
//
// A_Chase
//
// Actor has a melee attack, so it tries to close as fast as possible
//
// [GrafZahl] integrated A_FastChase, A_SerpentChase and A_SerpentWalk into this
// to allow the monsters using those functions to take advantage of the
// enhancements.
//
//=============================================================================
#define CLASS_BOSS_STRAFE_RANGE	64*10*FRACUNIT

void A_DoChase (VMFrameStack *stack, AActor *actor, bool fastchase, FState *meleestate, FState *missilestate, bool playactive, bool nightmarefast, bool dontmove, int flags)
{

	if (actor->flags5 & MF5_INCONVERSATION)
		return;

	if (actor->flags & MF_INCHASE)
	{
		return;
	}
	actor->flags |= MF_INCHASE;

	// [RH] Andy Baker's stealth monsters
	if (actor->flags & MF_STEALTH)
	{
		actor->visdir = -1;
	}

	if (actor->reactiontime)
	{
		actor->reactiontime--;
	}

	// [RH] Don't chase invisible targets
	if (actor->target != NULL &&
		actor->target->renderflags & RF_INVISIBLE &&
		actor->target != actor->goal)
	{
		actor->target = NULL;
	}

	// [BC] Clients do not know what the target is.
	if ( NETWORK_InClientMode() )
	{
		actor->target = NULL;
		actor->goal = NULL;
	}

	// modify target threshold
	if (actor->threshold)
	{
		if (actor->target == NULL || actor->target->health <= 0)
		{
			actor->threshold = 0;
		}
		else
		{
			actor->threshold--;
		}
	}

	if (nightmarefast && G_SkillProperty(SKILLP_FastMonsters))
	{ // Monsters move faster in nightmare mode
		if (actor->tics > 3)
		{
			actor->tics -= actor->tics / 2;
			if (actor->tics < 3)
			{
				actor->tics = 3;
			}
		}
	}

	// turn towards movement direction if not there yet
	if (actor->strafecount)
	{
		A_FaceTarget(actor);
	}
	else if (!(flags & CHF_NODIRECTIONTURN) && actor->movedir < 8)
	{
		actor->Angles.Yaw = floor(actor->Angles.Yaw.Degrees / 45) * 45.;
		DAngle delta = deltaangle(actor->Angles.Yaw, (actor->movedir * 45));
		if (delta < 0)
		{
			actor->Angles.Yaw -= 45;
		}
		else if (delta > 0)
		{
			actor->Angles.Yaw += 45;
		}
	}

	// [RH] If the target is dead or a friend (and not a goal), stop chasing it.
	// [BC] Also stop chasing the target if it's a spectator.
	if (actor->target && actor->target != actor->goal && (actor->target->health <= 0 || actor->IsFriend(actor->target) || ( actor->target->player && actor->target->player->bSpectating )))
		actor->target = NULL;

	// [RH] Friendly monsters will consider chasing whoever hurts a player if they
	// don't already have a target.
	if (actor->flags & MF_FRIENDLY && actor->target == NULL)
	{
		player_t *player;

		if (actor->FriendPlayer != 0)
		{
			player = &players[actor->FriendPlayer - 1];
		}
		else
		{
			int i;
			if ( ( NETWORK_GetState( ) == NETSTATE_SINGLE )
				// [BB] On the server it's possible that there are no players. In this case the
				// for loop below would get stuck, so we may not enter it.
				|| ( ( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( SERVER_CalcNumPlayers() == 0 ) ) )
			{
				i = 0;
			}
			else for (i = pr_newchasedir() & (MAXPLAYERS-1); !playeringame[i]; i = (i+1) & (MAXPLAYERS-1))
			{
			}
			player = &players[i];
		}
		if (player->attacker && player->attacker->health > 0 && player->attacker->flags & MF_SHOOTABLE && pr_newchasedir() < 80)
		{
			if (!(player->attacker->flags & MF_FRIENDLY) ||
				(deathmatch && actor->FriendPlayer != 0 && player->attacker->FriendPlayer != 0 &&
				actor->FriendPlayer != player->attacker->FriendPlayer))
			{
				actor->target = player->attacker;
			} 
		}
	}
	if (!actor->target || !(actor->target->flags & MF_SHOOTABLE))
	{ // look for a new target
		if (actor->target != NULL && (actor->target->flags2 & MF2_NONSHOOTABLE))
		{
			// Target is only temporarily unshootable, so remember it.
			actor->lastenemy = actor->target;
			// Switch targets faster, since we're only changing because we can't
			// hurt our old one temporarily.
			actor->threshold = 0;
		}
		if (P_LookForPlayers (actor, true, NULL) && actor->target != actor->goal)
		{ // got a new target
			actor->flags &= ~MF_INCHASE;
			return;
		}
		// [BC/BB] In client mode, just keep moving along.
		if ( (actor->target == NULL) && ( NETWORK_InClientMode() == false ) )
		{
			if (actor->flags & MF_FRIENDLY)
			{
				//CALL_ACTION(A_Look, actor);
				if (actor->target == NULL)
				{
					if (!dontmove) A_Wander(actor);
					actor->flags &= ~MF_INCHASE;
					return;
				}
			}
			else
			{
				// [BC] If we are the server, tell clients about the state change.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( actor, STATE_IDLE );

				actor->SetIdle();
				actor->flags &= ~MF_INCHASE;
				return;
			}
		}
	}
	
	// do not attack twice in a row
	if (actor->flags & MF_JUSTATTACKED)
	{
		actor->flags &= ~MF_JUSTATTACKED;
		if (!actor->isFast() && !dontmove && !(flags & CHF_NOPOSTATTACKTURN) && !(flags & CHF_STOPIFBLOCKED) &&
			// [BC] Don't decide a new chase dir in client mode.
			( NETWORK_InClientMode() == false ))
		{
			P_NewChaseDir (actor);
		}
		//Because P_TryWalk would never be reached if the actor is stopped by a blocking object,
		//need to make sure the movecount is reset, otherwise they will just keep attacking
		//over and over again.
		if (flags & CHF_STOPIFBLOCKED)
			actor->movecount = pr_trywalk() & 15;
		actor->flags &= ~MF_INCHASE;
		return;
	}
	
	// [RH] Don't attack if just moving toward goal
	if (actor->target == actor->goal || (actor->flags5&MF5_CHASEGOAL && actor->goal != NULL))
	{
		AActor * savedtarget = actor->target;
		actor->target = actor->goal;
		bool result = actor->CheckMeleeRange();
		actor->target = savedtarget;

		if (result)
		{
			// reached the goal
			NActorIterator iterator (NAME_PatrolPoint, actor->goal->args[0]);
			NActorIterator specit (NAME_PatrolSpecial, actor->goal->tid);
			AActor *spec;

			// Execute the specials of any PatrolSpecials with the same TID
			// as the goal.
			while ( (spec = specit.Next()) )
			{
				P_ExecuteSpecial(spec->special, NULL, actor, false, spec->args[0],
					spec->args[1], spec->args[2], spec->args[3], spec->args[4]);
			}

			DAngle lastgoalang = actor->goal->Angles.Yaw;
			int delay;
			AActor * newgoal = iterator.Next ();
			if (newgoal != NULL && actor->goal == actor->target)
			{
				delay = newgoal->args[1];
				actor->reactiontime = delay * TICRATE + level.maptime;
			}
			else
			{
				delay = 0;
				actor->reactiontime = actor->GetDefault()->reactiontime;
				actor->Angles.Yaw = lastgoalang;		// Look in direction of last goal

				// [BC] Send the state change to clients.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingAngle( actor );
			}
			if (actor->target == actor->goal) actor->target = NULL;
			actor->flags |= MF_JUSTATTACKED;
			if (newgoal != NULL && delay != 0)
			{
				actor->flags4 |= MF4_INCOMBAT;

				// [BC] Send the state change to clients.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( actor, STATE_IDLE );

				actor->SetIdle();
			}
			actor->flags &= ~MF_INCHASE;
			actor->goal = newgoal;
			return;
		}
		if (actor->goal == actor->target) goto nomissile;
	}

	// Strafe	(Hexen's class bosses)
	// This was the sole reason for the separate A_FastChase function but
	// it can be just as easily handled by a simple flag so the monsters
	// can take advantage of all the other enhancements of A_Chase.

	if (fastchase && !dontmove &&
		// [BC] Don't fast chase in client mode.
		( NETWORK_InClientMode() == false ))
	{
		if (actor->FastChaseStrafeCount > 0)
		{
			actor->FastChaseStrafeCount--;

			// [Dusk] update strafecount
			if ( actor->FastChaseStrafeCount == 0 && NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetFastChaseStrafeCount( actor );
		}
		else
		{
			actor->FastChaseStrafeCount = 0;
			actor->Vel.X = actor->Vel.Y = 0;
			fixed_t dist = actor->AproxDistance (actor->target);
			if (dist < CLASS_BOSS_STRAFE_RANGE)
			{
				if (pr_chase() < 100)
				{
					DAngle ang = actor->AngleTo(actor->target);
					if (pr_chase() < 128) ang += 90.;
					else ang -= 90.;
					actor->VelFromAngle(ang, 13.);
					actor->FastChaseStrafeCount = 3;		// strafe time

					// [Dusk] Update the strafecount to the clients. It is necessary
					// to let clients know of this variable so that they can
					// prevent the chasing during strafing
					if( NETWORK_GetState( ) == NETSTATE_SERVER )
						SERVERCOMMANDS_SetFastChaseStrafeCount( actor );
				}
			}

			// [BC] If we're the server, update the thing's velocity.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z );
				SERVERCOMMANDS_MoveThingExact( actor, CM_VELX|CM_VELY );
			}
		}

	}

	// [RH] Scared monsters attack less frequently
	if (((actor->target->player == NULL ||
		!(actor->target->player->cheats & CF_FRIGHTENING)) &&
		!(actor->flags4 & MF4_FRIGHTENED)) ||
		pr_scaredycat() < 43)
	{
		// check for melee attack
		if (meleestate && actor->CheckMeleeRange ())
		{
			if (actor->AttackSound)
				S_Sound (actor, CHAN_WEAPON, actor->AttackSound, 1, ATTN_NORM, true );	// [BC] Inform the clients.

			// [BC] If we are the server, tell clients about the state change.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_SetThingFrame( actor, meleestate );
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z );
			}

			actor->SetState (meleestate);
			actor->flags &= ~MF_INCHASE;
			return;
		}
		
		// check for missile attack
		if (missilestate)
		{
			if (!actor->isFast() && actor->movecount)
			{
				goto nomissile;
			}
			
			if (!P_CheckMissileRange (actor))
				goto nomissile;
			
			// [BC] If we are the server, tell clients about the state change.
			// Also, update the thing's position.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			{
				SERVERCOMMANDS_SetThingFrame( actor, missilestate );
				SERVERCOMMANDS_MoveThing( actor, CM_X|CM_Y|CM_Z );
			}

			actor->SetState (missilestate);
			actor->flags |= MF_JUSTATTACKED;
			actor->flags4 |= MF4_INCOMBAT;
			actor->flags &= ~MF_INCHASE;
			return;
		}
	}

 nomissile:
	// possibly choose another target
	if ((( NETWORK_GetState( ) != NETSTATE_SINGLE ) || actor->TIDtoHate)
		&& !actor->threshold
		&& ( NETWORK_InClientMode() == false )
		// [BB] In invasion mode, player doesn't have to be visible to be chased by monsters.
		// [BB] The flags argument of P_CheckSight has to be the same here as it is in P_LookForPlayers.
		&& !P_CheckSight (actor, actor->target, SF_SEEPASTBLOCKEVERYTHING) && ( invasion == false ) )
	{
		bool lookForBetter = false;
		bool gotNew;
		if (actor->flags3 & MF3_NOSIGHTCHECK)
		{
			actor->flags3 &= ~MF3_NOSIGHTCHECK;
			lookForBetter = true;
		}
		AActor * oldtarget = actor->target;
		gotNew = P_LookForPlayers (actor, true, NULL);
		if (lookForBetter)
		{
			actor->flags3 |= MF3_NOSIGHTCHECK;
		}
		if (gotNew && actor->target != oldtarget)
		{
			actor->flags &= ~MF_INCHASE;
			return; 	// got a new target
		}
	}

	//
	// chase towards player
	//

	if (actor->strafecount)
		actor->strafecount--;
	
	// class bosses don't do this when strafing
	if ((!fastchase || !actor->FastChaseStrafeCount) && !dontmove)
	{
		// CANTLEAVEFLOORPIC handling was completely missing in the non-serpent functions.
		fixed_t oldX = actor->_f_X();
		fixed_t oldY = actor->_f_Y();
		int oldgroup = actor->PrevPortalGroup;
		FTextureID oldFloor = actor->floorpic;

		// [BC] In client mode, just keep walking until the server tells us to
		// change directions.
		if ( NETWORK_InClientMode() )
		{
			P_Move( actor );
		}
		// chase towards player
		else if ((--actor->movecount < 0 && !(flags & CHF_NORANDOMTURN)) || (!P_Move(actor) && !(flags & CHF_STOPIFBLOCKED)))
		{
			P_NewChaseDir(actor);
		}
		// if the move was illegal, reset it 
		// (copied from A_SerpentChase - it applies to everything with CANTLEAVEFLOORPIC!)
		if (actor->flags2&MF2_CANTLEAVEFLOORPIC && actor->floorpic != oldFloor )
		{
			// [BC] In client mode, just keep walking until the server tells us to
			// change directions.
			if ( NETWORK_InClientMode() )
			{
				P_TryMove( actor, oldX, oldY, false );
			}
			else
			{
				if (P_TryMove(actor, oldX, oldY, false))
				{
					if (nomonsterinterpolation)
					{
						actor->PrevX = oldX;
						actor->PrevY = oldY;
					actor->PrevPortalGroup = oldgroup;
					}
				}
				if (!(flags & CHF_STOPIFBLOCKED))
					P_NewChaseDir(actor);
			}
		}
	}
	else if (dontmove && actor->movecount > 0) actor->movecount--;
	
	// make active sound
	if (playactive && pr_chase() < 3)
	{
		actor->PlayActiveSound ();
	}

	actor->flags &= ~MF_INCHASE;
}


//==========================================================================
//
// P_CheckForResurrection (formerly part of A_VileChase)
// Check for ressurecting a body
//
//==========================================================================

static bool P_CheckForResurrection(AActor *self, bool usevilestates)
{
	const AActor *info;
	AActor *temp;
		
	// [BC] Movement is server-side.
	if ( NETWORK_InClientMode() )
	{
		// Return to normal attack.
		//A_Chase (self);
		// [BB] Some changes here to adapt to the movement of the A_VileChase code
		// to P_CheckForResurrection.
		return false;
	}

	if (self->movedir != DI_NODIR)
	{
		const fixed_t absSpeed = abs (self->_f_speed());
		fixedvec2 viletry = self->Vec2Offset(
			FixedMul (absSpeed, xspeed[self->movedir]),
			FixedMul (absSpeed, yspeed[self->movedir]), true);

		FPortalGroupArray check(FPortalGroupArray::PGA_Full3d);

		FMultiBlockThingsIterator it(check, viletry.x, viletry.y, self->_f_Z() - 64* FRACUNIT, self->_f_Top() + 64 * FRACUNIT, 32 * FRACUNIT, false, NULL);
		FMultiBlockThingsIterator::CheckResult cres;
		while (it.Next(&cres))
		{
			AActor *corpsehit = cres.thing;
			FState *raisestate = corpsehit->GetRaiseState();
			if (raisestate != NULL)
			{
				// use the current actor's _f_radius() instead of the Arch Vile's default.
				fixed_t maxdist = corpsehit->GetDefault()->_f_radius() + self->_f_radius();

				if (abs(corpsehit->_f_Pos().x - cres.position.x) > maxdist ||
					abs(corpsehit->_f_Pos().y - cres.position.y) > maxdist)
					continue;			// not actually touching
				// Let's check if there are floors in between the archvile and its target

				if (corpsehit->Sector->PortalGroup != self->Sector->PortalGroup)
				{
					// if in a different section of the map, only consider possible if a line of sight exists.
					if (!P_CheckSight(self, corpsehit))
						continue;
				}
				else
				{
					sector_t *vilesec = self->Sector;
					sector_t *corpsec = corpsehit->Sector;
					// We only need to test if at least one of the sectors has a 3D floor.
					sector_t *testsec = vilesec->e->XFloor.ffloors.Size() ? vilesec :
						(vilesec != corpsec && corpsec->e->XFloor.ffloors.Size()) ? corpsec : NULL;
					if (testsec)
					{
						fixed_t zdist1, zdist2;
						if (P_Find3DFloor(testsec, corpsehit->_f_Pos(), false, true, zdist1)
							!= P_Find3DFloor(testsec, self->_f_Pos(), false, true, zdist2))
						{
							// Not on same floor
							if (vilesec == corpsec || abs(zdist1 - self->_f_Z()) > self->_f_height())
								continue;
						}
					}
				}

				corpsehit->Vel.X = corpsehit->Vel.Y = 0;
				// [RH] Check against real height and _f_radius()

				double oldheight = corpsehit->Height;
				double oldradius = corpsehit->radius;
				ActorFlags oldflags = corpsehit->flags;

				corpsehit->flags |= MF_SOLID;
				corpsehit->Height = corpsehit->GetDefault()->Height;
				bool check = P_CheckPosition(corpsehit, corpsehit->Pos());
				corpsehit->flags = oldflags;
				corpsehit->radius = oldradius;
				corpsehit->Height = oldheight;
				if (!check) continue;

				// got one!
				temp = self->target;
				self->target = corpsehit;
				A_FaceTarget(self);
				if (self->flags & MF_FRIENDLY)
				{
					// If this is a friendly Arch-Vile (which is turning the resurrected monster into its friend)
					// and the Arch-Vile is currently targetting the resurrected monster the target must be cleared.
					if (self->lastenemy == temp) self->lastenemy = NULL;
					if (self->lastenemy == corpsehit) self->lastenemy = NULL;
					if (temp == self->target) temp = NULL;
				}
				self->target = temp;

				// [BC] If we are the server, tell clients about the state change.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( self, STATE_HEAL );

				// Make the state the monster enters customizable.
				FState * state = self->FindState(NAME_Heal);
				if (state != NULL)
				{
					self->SetState(state);
				}
				else if (usevilestates)
				{
					// For Dehacked compatibility this has to use the Arch Vile's
					// heal state as a default if the actor doesn't define one itself.
					PClassActor *archvile = PClass::FindActor("Archvile");
					if (archvile != NULL)
					{
						self->SetState(archvile->FindState(NAME_Heal));
					}
				}
				S_Sound(corpsehit, CHAN_BODY, "vile/raise", 1, ATTN_IDLE);
				info = corpsehit->GetDefault();

				if (GetTranslationType(corpsehit->Translation) == TRANSLATION_Blood)
				{
					corpsehit->Translation = info->Translation; // Clean up bloodcolor translation from crushed corpses
				}
				if (ib_compatflags & BCOMPATF_VILEGHOSTS)
				{
					corpsehit->Height *= 4;
					// [GZ] This was a commented-out feature, so let's make use of it,
					// but only for ghost monsters so that they are visibly different.
					if (corpsehit->Height == 0)
					{
						// Make raised corpses look ghostly
						if (corpsehit->Alpha > 0.5)
						{
							corpsehit->Alpha /= 2;
						}
						// This will only work if the render style is changed as well.
						if (corpsehit->RenderStyle == LegacyRenderStyles[STYLE_Normal])
						{
							corpsehit->RenderStyle = STYLE_Translucent;
						}
					}
				}
				else
				{
					corpsehit->Height = info->Height;	// [RH] Use real mobj height
					corpsehit->radius = info->radius;	// [RH] Use real radius
				}

				corpsehit->Revive();

				// You are the Archvile's minion now, so hate what it hates
				corpsehit->CopyFriendliness(self, false);

				// [BC] If we're the server, tell clients to put the thing into its raise state.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
					SERVERCOMMANDS_SetThingState( corpsehit, STATE_RAISE );

				corpsehit->SetState(raisestate);

				return true;
			}
		}
	}
	return false;
}

//==========================================================================
//
// A_Chase and variations
//
//==========================================================================

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_Chase)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_STATE_OPT	(melee)		{ melee = NULL; }
	PARAM_STATE_OPT	(missile)	{ missile = NULL; }
	PARAM_INT_OPT	(flags)		{ flags = 0; }

	if (numparam >= NAP + 1)
	{
		if ((flags & CHF_RESURRECT) && P_CheckForResurrection(self, false))
			return 0;
		
		A_DoChase(stack, self, !!(flags&CHF_FASTCHASE), melee, missile, !(flags&CHF_NOPLAYACTIVE), 
					!!(flags&CHF_NIGHTMAREFAST), !!(flags&CHF_DONTMOVE), flags);
	}
	else // this is the old default A_Chase
	{
		A_DoChase(stack, self, false, self->MeleeState, self->MissileState, true, gameinfo.nightmarefast, false, flags);
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_FastChase)
{
	PARAM_ACTION_PROLOGUE;
	A_DoChase(stack, self, true, self->MeleeState, self->MissileState, true, true, false, 0);
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_VileChase)
{
	PARAM_ACTION_PROLOGUE;
	if (!P_CheckForResurrection(self, true))
	{
		A_DoChase(stack, self, false, self->MeleeState, self->MissileState, true, gameinfo.nightmarefast, false, 0);
	}
	return 0;
}

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_ExtChase)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_BOOL		(domelee);
	PARAM_BOOL		(domissile);
	PARAM_BOOL_OPT	(playactive)	{ playactive = true; }
	PARAM_BOOL_OPT	(nightmarefast)	{ nightmarefast = false; }

	// Now that A_Chase can handle state label parameters, this function has become rather useless...
	A_DoChase(stack, self, false,
		domelee ? self->MeleeState : NULL, domissile ? self->MissileState : NULL,
		playactive, nightmarefast, false, 0);
	return 0;
}

// for internal use
void A_Chase(VMFrameStack *stack, AActor *self)
{
	A_DoChase(stack, self, false, self->MeleeState, self->MissileState, true, gameinfo.nightmarefast, false, 0);
}

//=============================================================================
//
// A_FaceTarget
// A_FaceMaster
// A_FaceTracer
//
//=============================================================================
enum FAF_Flags
{
	FAF_BOTTOM = 1,
	FAF_MIDDLE = 2,
	FAF_TOP = 4,
	FAF_NODISTFACTOR = 8,	// deprecated
};
void A_Face (AActor *self, AActor *other, angle_t _max_turn, angle_t _max_pitch, angle_t _ang_offset, angle_t _pitch_offset, int flags, fixed_t z_add)
{
	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		// [RH] Andy Baker's stealth monsters
		if (self->flags & MF_STEALTH)
		{
			self->visdir = 1;
		}

		return;
	}

	if (!other)
		return;

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = 1;
	}

	self->flags &= ~MF_AMBUSH;

	DAngle max_turn = ANGLE2DBL(_max_turn);
	DAngle ang_offset = ANGLE2DBL(_ang_offset);
	DAngle max_pitch = ANGLE2DBL(_max_pitch);
	DAngle pitch_offset = ANGLE2DBL(_pitch_offset);
	DAngle other_angle = self->AngleTo(other);

	DAngle delta = deltaangle(self->Angles.Yaw, other_angle);

	// 0 means no limit. Also, if we turn in a single step anyways, no need to go through the algorithms.
	// It also means that there is no need to check for going past the other.
	if (max_turn != 0 && (max_turn < fabs(delta)))
	{
		if (delta > 0)
		{
			self->Angles.Yaw -= max_turn + ang_offset;
		}
		else
		{
			self->Angles.Yaw += max_turn + ang_offset;
		}
	}
	else
	{
		self->Angles.Yaw = other_angle + ang_offset;
	}

	// [DH] Now set pitch. In order to maintain compatibility, this can be
	// disabled and is so by default.
	if (max_pitch <= 180.)
	{
		fixedvec2 pos = self->_f_Vec2To(other);
		DVector2 dist(pos.x, pos.y);
		
		// Positioning ala missile spawning, 32 units above foot level
		fixed_t source_z = self->_f_Z() + 32*FRACUNIT + self->GetBobOffset();
		fixed_t target_z = other->_f_Z() + 32*FRACUNIT + other->GetBobOffset();

		// If the target z is above the target's head, reposition to the middle of
		// its body.		
		if (target_z >= other->_f_Top())
		{
			target_z = other->_f_Z() + (other->_f_height() / 2);
		}

		//Note there is no +32*FRACUNIT on purpose. This is for customization sake. 
		//If one doesn't want this behavior, just don't use FAF_BOTTOM.
		if (flags & FAF_BOTTOM)
			target_z = other->_f_Z() + other->GetBobOffset(); 
		if (flags & FAF_MIDDLE)
			target_z = other->_f_Z() + (other->_f_height() / 2) + other->GetBobOffset();
		if (flags & FAF_TOP)
			target_z = other->_f_Z() + (other->_f_height()) + other->GetBobOffset();

		target_z += z_add;

		double dist_z = target_z - source_z;
		double ddist = g_sqrt(dist.X*dist.X + dist.Y*dist.Y + dist_z*dist_z);

		DAngle other_pitch = DAngle(ToDegrees(g_asin(dist_z / ddist))).Normalized180();
		
		if (max_pitch != 0)
		{
			if (self->Angles.Pitch > other_pitch)
			{
				max_pitch = MIN(max_pitch, (self->Angles.Pitch - other_pitch).Normalized360());
				self->Angles.Pitch -= max_pitch;
			}
			else
			{
				max_pitch = MIN(max_pitch, (other_pitch - self->Angles.Pitch).Normalized360());
				self->Angles.Pitch += max_pitch;
			}
		}
		else
		{
			self->Angles.Pitch = other_pitch;
		}
		self->Angles.Pitch += pitch_offset;
	}
	


	// This will never work well if the turn angle is limited.
	if (max_turn == 0 && (self->Angles.Yaw == other_angle) && other->flags & MF_SHADOW && !(self->flags6 & MF6_SEEINVISIBLE) )
    {
		self->Angles.Yaw += pr_facetarget.Random2() * (45 / 256.);
    }

	// [BC] Update the thing's angle.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetThingAngle( self );
}

void A_FaceTarget(AActor *self)
{
	A_Face(self, self->target);
}

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_FaceTarget)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_ANGLE_OPT(max_turn)		{ max_turn = 0; }
	PARAM_ANGLE_OPT(max_pitch)		{ max_pitch = 270; }
	PARAM_ANGLE_OPT(ang_offset)		{ ang_offset = 0; }
	PARAM_ANGLE_OPT(pitch_offset)	{ pitch_offset = 0; }
	PARAM_INT_OPT(flags)			{ flags = 0; }
	PARAM_FIXED_OPT(z_add)			{ z_add = 0; }

	A_Face(self, self->target, max_turn, max_pitch, ang_offset, pitch_offset, flags, z_add);
	return 0;
}

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_FaceMaster)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_ANGLE_OPT(max_turn)		{ max_turn = 0; }
	PARAM_ANGLE_OPT(max_pitch)		{ max_pitch = 270; }
	PARAM_ANGLE_OPT(ang_offset)		{ ang_offset = 0; }
	PARAM_ANGLE_OPT(pitch_offset)	{ pitch_offset = 0; }
	PARAM_INT_OPT(flags)			{ flags = 0; }
	PARAM_FIXED_OPT(z_add)			{ z_add = 0; }

	A_Face(self, self->master, max_turn, max_pitch, ang_offset, pitch_offset, flags, z_add);
	return 0;
}

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_FaceTracer)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_ANGLE_OPT(max_turn)		{ max_turn = 0; }
	PARAM_ANGLE_OPT(max_pitch)		{ max_pitch = 270; }
	PARAM_ANGLE_OPT(ang_offset)		{ ang_offset = 0; }
	PARAM_ANGLE_OPT(pitch_offset)	{ pitch_offset = 0; }
	PARAM_INT_OPT(flags)			{ flags = 0; }
	PARAM_FIXED_OPT(z_add)			{ z_add = 0; }

	A_Face(self, self->tracer, max_turn, max_pitch, ang_offset, pitch_offset, flags, z_add);
	return 0;
}

//===========================================================================
//
// [RH] A_MonsterRail
//
// New function to let monsters shoot a railgun
//
//===========================================================================
DEFINE_ACTION_FUNCTION(AActor, A_MonsterRail)
{
	PARAM_ACTION_PROLOGUE;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		// [RH] Andy Baker's stealth monsters
		if (self->flags & MF_STEALTH)
		{
			self->visdir = 1;
		}

		return 0;
	}

	if (!self->target)
		return 0;

	DAngle saved_pitch = self->Angles.Pitch;
	FTranslatedLineTarget t;

	// [RH] Andy Baker's stealth monsters
	if (self->flags & MF_STEALTH)
	{
		self->visdir = 1;
	}

	self->flags &= ~MF_AMBUSH;
		
	self->Angles.Yaw = self->AngleTo(self->target);

	self->Angles.Pitch = P_AimLineAttack (self, self->Angles.Yaw, MISSILERANGE, &t, 60., 0, self->target);
	if (t.linetarget == NULL)
	{
		// We probably won't hit the target, but aim at it anyway so we don't look stupid.
		DVector2 xydiff = self->Vec2To(self->target);
		double zdiff = self->target->Center() - self->Center() - self->Floorclip;
		self->Angles.Pitch = -VecToAngle(xydiff.Length(), zdiff);
	}

	// Let the aim trail behind the player
	self->Angles.Yaw = self->AngleTo(self->target, -self->target->Vel.X * 3, -self->target->Vel.Y * 3);

	if (self->target->flags & MF_SHADOW && !(self->flags6 & MF6_SEEINVISIBLE))
	{
		self->Angles.Yaw += pr_railface.Random2() * 45./256;
	}

	// [BC] Set the thing's angle.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SetThingAngle( self );

	P_RailAttack (self, self->GetMissileDamage (0, 1), 0);
	self->Angles.Pitch = saved_pitch;
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_Scream)
{
	PARAM_ACTION_PROLOGUE;
	if (self->DeathSound)
	{
		// Check for bosses.
		if (self->flags2 & MF2_BOSS)
		{
			// full volume
			S_Sound (self, CHAN_VOICE, self->DeathSound, 1, ATTN_NONE);
		}
		else
		{
			S_Sound (self, CHAN_VOICE, self->DeathSound, 1, ATTN_NORM);
		}
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_XScream)
{
	PARAM_ACTION_PROLOGUE;
	if (self->player)
		S_Sound (self, CHAN_VOICE, "*gibbed", 1, ATTN_NORM);
	else
		S_Sound (self, CHAN_VOICE, "misc/gibbed", 1, ATTN_NORM);
	return 0;
}

//===========================================================================
//
// A_ScreamAndUnblock
//
//===========================================================================

DEFINE_ACTION_FUNCTION(AActor, A_ScreamAndUnblock)
{
	PARAM_ACTION_PROLOGUE;
	CALL_ACTION(A_Scream, self);
	A_Unblock(self, true);
	return 0;
}

//===========================================================================
//
// A_ActiveSound
//
//===========================================================================

DEFINE_ACTION_FUNCTION(AActor, A_ActiveSound)
{
	PARAM_ACTION_PROLOGUE;
	if (self->ActiveSound)
	{
		S_Sound (self, CHAN_VOICE, self->ActiveSound, 1, ATTN_NORM);
	}
	return 0;
}

//===========================================================================
//
// A_ActiveAndUnblock
//
//===========================================================================

DEFINE_ACTION_FUNCTION(AActor, A_ActiveAndUnblock)
{
	PARAM_ACTION_PROLOGUE;
	CALL_ACTION(A_ActiveSound, self);
	A_Unblock(self, true);
	return 0;
}

//---------------------------------------------------------------------------
//
// Modifies the drop amount of this item according to the current skill's
// settings (also called by ADehackedPickup::TryPickup)
//
//---------------------------------------------------------------------------
void ModifyDropAmount(AInventory *inv, int dropamount)
{
	int flagmask = IF_IGNORESKILL;
	fixed_t dropammofactor = G_SkillProperty(SKILLP_DropAmmoFactor);
	// Default drop amount is half of regular amount * regular ammo multiplication
	if (dropammofactor == -1) 
	{
		dropammofactor = FRACUNIT/2;
		flagmask = 0;
	}

	if (dropamount > 0)
	{
		if (flagmask != 0 && inv->IsKindOf(RUNTIME_CLASS(AAmmo)))
		{
			inv->Amount = FixedMul(dropamount, dropammofactor);
			inv->ItemFlags |= IF_IGNORESKILL;
		}
		else
		{
			inv->Amount = dropamount;
		}
	}
	else if (inv->IsKindOf (RUNTIME_CLASS(AAmmo)))
	{
		// Half ammo when dropped by bad guys.
		int amount = static_cast<PClassAmmo *>(inv->GetClass())->DropAmount;
		if (amount <= 0)
		{
			amount = MAX(1, FixedMul(inv->Amount, dropammofactor));
		}
		inv->Amount = amount;
		inv->ItemFlags |= flagmask;
	}
	else if (inv->IsKindOf (RUNTIME_CLASS(AWeaponGiver)))
	{
		static_cast<AWeaponGiver *>(inv)->DropAmmoFactor = dropammofactor;
		inv->ItemFlags |= flagmask;
	}
	else if (inv->IsKindOf (RUNTIME_CLASS(AWeapon)))
	{
		// The same goes for ammo from a weapon.
		static_cast<AWeapon *>(inv)->AmmoGive1 = FixedMul(static_cast<AWeapon *>(inv)->AmmoGive1, dropammofactor);
		static_cast<AWeapon *>(inv)->AmmoGive2 = FixedMul(static_cast<AWeapon *>(inv)->AmmoGive2, dropammofactor);
		inv->ItemFlags |= flagmask;
	}			
	else if (inv->IsKindOf (RUNTIME_CLASS(ADehackedPickup)))
	{
		// For weapons and ammo modified by Dehacked we need to flag the item.
		static_cast<ADehackedPickup *>(inv)->droppedbymonster = true;
	}
}

//---------------------------------------------------------------------------
//
// PROC P_DropItem
//
//---------------------------------------------------------------------------
CVAR(Int, sv_dropstyle, 0, CVAR_SERVERINFO | CVAR_ARCHIVE);

AInventory *P_DropItem (AActor *source, PClassActor *type, int dropamount, int chance)
{
	// [BC] This is handled server side.
	if ( NETWORK_InClientMode() )
	{
		return ( NULL );
	}

	if (type != NULL && pr_dropitem() <= chance)
	{
		AActor *mo;
		fixed_t spawnz;

		spawnz = source->_f_Z();
		if (!(i_compatflags & COMPATF_NOTOSSDROPS))
		{
			int style = sv_dropstyle;
			if (style == 0)
			{
				style = (gameinfo.gametype == GAME_Strife) ? 2 : 1;
			}
			if (style == 2)
			{
				spawnz += 24*FRACUNIT;
			}
			else
			{
				spawnz += source->_f_height() / 2;
			}
		}
		mo = Spawn(type, source->_f_X(), source->_f_Y(), spawnz, ALLOW_REPLACE);

		if (mo != NULL)
		{
			// [BC] If we're the server, tell clients to spawn the thing.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SpawnThing( mo );

			mo->flags |= MF_DROPPED;
			mo->flags &= ~MF_NOGRAVITY;	// [RH] Make sure it is affected by gravity

			// [Dusk] and be sure to tell the clients about that!
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingFlags( mo, FLAGSET_FLAGS );

			if (!(i_compatflags & COMPATF_NOTOSSDROPS))
			{
				P_TossItem (mo);
			}
			if (mo->IsKindOf (RUNTIME_CLASS(AInventory)))
			{
				AInventory *inv = static_cast<AInventory *>(mo);
				ModifyDropAmount(inv, dropamount);
				inv->ItemFlags |= IF_TOSSED;

				// [BB] Now that the ammo amount from weapon pickups is handled on the server
				// this shouldn't be necessary anymore. Remove after thorough testing.
				// [BC] If we're the server, tell clients that the thing is dropped.
				//if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				//	SERVERCOMMANDS_SetWeaponAmmoGive( mo );
				if (inv->SpecialDropAction (source))
				{
					// The special action indicates that the item should not spawn
					inv->Destroy();
					return NULL;
				}
				return inv;
			}
			// we can't really return an AInventory pointer to a non-inventory item here, can we?
		}
	}
	return NULL;
}

//============================================================================
//
// P_TossItem
//
//============================================================================

void P_TossItem (AActor *item)
{
	int style = sv_dropstyle;
	if (style==0) style= gameinfo.defaultdropstyle;
	
	if (style==2)
	{
		item->Vel.X += pr_dropitem.Random2(7);
		item->Vel.Y += pr_dropitem.Random2(7);
	}
	else
	{
		item->Vel.X += pr_dropitem.Random2() / 256.;
		item->Vel.Y += pr_dropitem.Random2() / 256.;
		item->Vel.Z = 5. + pr_dropitem() / 64.;
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_Pain)
{
	PARAM_ACTION_PROLOGUE;

	// [RH] Vary player pain sounds depending on health (ala Quake2)
	if (self->player && self->player->morphTics == 0)
	{
		const char *pain_amount;
		FSoundID sfx_id;

		if (self->health < 25)
			pain_amount = "*pain25";
		else if (self->health < 50)
			pain_amount = "*pain50";
		else if (self->health < 75)
			pain_amount = "*pain75";
		else
			pain_amount = "*pain100";

		// Try for damage-specific sounds first.
		if (self->player->LastDamageType != NAME_None)
		{
			FString pain_sound = pain_amount;
			pain_sound += '-';
			pain_sound += self->player->LastDamageType;
			sfx_id = pain_sound;
			if (sfx_id == 0)
			{
				// Try again without a specific pain amount.
				pain_sound = "*pain-";
				pain_sound += self->player->LastDamageType;
				sfx_id = pain_sound;
			}
		}
		if (sfx_id == 0)
		{
			sfx_id = pain_amount;
		}

		S_Sound (self, CHAN_VOICE, sfx_id, 1, ATTN_NORM);
	}
	else if (self->PainSound)
	{
		S_Sound (self, CHAN_VOICE, self->PainSound, 1, ATTN_NORM);
	}
	return 0;
}

// killough 11/98: kill an object
DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_Die)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_NAME_OPT	(damagetype)	{ damagetype = NAME_None; }
	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return 0;
	}


		P_DamageMobj(self, NULL, NULL, self->health, damagetype, DMG_FORCED);
	return 0;
}

//
// A_Detonate
// killough 8/9/98: same as A_Explode, except that the damage is variable
//

DEFINE_ACTION_FUNCTION(AActor, A_Detonate)
{
	PARAM_ACTION_PROLOGUE;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return 0;
	}

	int damage = self->GetMissileDamage(0, 1);
	P_RadiusAttack (self, self->target, damage, damage, self->DamageType, RADF_HURTSOURCE);
	P_CheckSplash(self, damage);

	// [BC] If this explosion originated from a player, and it hit something, give the player
	// credit for it.
	if (( self->target ) && ( self->target->player ))
	{
		if ( self->target->player->bStruckPlayer )
			PLAYER_StruckPlayer( self->target->player );
		else
			self->target->player->ulConsecutiveHits = 0;
	}

	return 0;
}

bool CheckBossDeath (AActor *actor)
{
	int i;

	// make sure there is a player alive for victory
	for (i = 0; i < MAXPLAYERS; i++)
		if (playeringame[i] && players[i].health > 0)
			break;
	
	if (i == MAXPLAYERS)
		return false; // no one left alive, so do not end game
	
	// Make sure all bosses are dead
	TThinkerIterator<AActor> iterator;
	AActor *other;

	while ( (other = iterator.Next ()) )
	{
		if (other != actor &&
			(other->health > 0 || (other->flags & MF_ICECORPSE))
			&& other->GetClass() == actor->GetClass()
			// [BB] Don't count actors hidden by HideOrDestroyIfSafe()
			&& !( other->ulSTFlags & STFL_HIDDEN_INSTEAD_OF_DESTROYED ) )
		{ // Found a living boss
		  // [RH] Frozen bosses don't count as dead until they shatter
			return false;
		}
	}
	// The boss death is good
	return true;
}

//
// A_BossDeath
// Possibly trigger special effects if on a boss level
//
void A_BossDeath(AActor *self)
{
	FName mytype = self->GetClass()->TypeName;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	// Ugh...
	FName type = self->GetClass()->GetReplacee()->TypeName;
	
	// Do generic special death actions first
	bool checked = false;
	for (unsigned i = 0; i < level.info->specialactions.Size(); i++)
	{
		FSpecialAction *sa = &level.info->specialactions[i];

		if (type == sa->Type || mytype == sa->Type)
		{
			if (!checked && !CheckBossDeath(self))
			{
				return;
			}
			checked = true;

			P_ExecuteSpecial(sa->Action, NULL, self, false, 
				sa->Args[0], sa->Args[1], sa->Args[2], sa->Args[3], sa->Args[4]);
		}
	}

	// [RH] These all depend on the presence of level flags now
	//		rather than being hard-coded to specific levels/episodes.

	if ((level.flags & (LEVEL_MAP07SPECIAL|
						LEVEL_BRUISERSPECIAL|
						LEVEL_CYBORGSPECIAL|
						LEVEL_SPIDERSPECIAL|
						LEVEL_HEADSPECIAL|
						LEVEL_MINOTAURSPECIAL|
						LEVEL_SORCERER2SPECIAL)) == 0)
		return;

	if ((i_compatflags & COMPATF_ANYBOSSDEATH) || ( // [GZ] Added for UAC_DEAD
		((level.flags & LEVEL_MAP07SPECIAL) && (type == NAME_Fatso || type == NAME_Arachnotron)) ||
		((level.flags & LEVEL_BRUISERSPECIAL) && (type == NAME_BaronOfHell)) ||
		((level.flags & LEVEL_CYBORGSPECIAL) && (type == NAME_Cyberdemon)) ||
		((level.flags & LEVEL_SPIDERSPECIAL) && (type == NAME_SpiderMastermind)) ||
		((level.flags & LEVEL_HEADSPECIAL) && (type == NAME_Ironlich)) ||
		((level.flags & LEVEL_MINOTAURSPECIAL) && (type == NAME_Minotaur)) ||
		((level.flags & LEVEL_SORCERER2SPECIAL) && (type == NAME_Sorcerer2))
	   ))
		;
	else
		return;

	if (!CheckBossDeath (self))
	{
		return;
	}

	// victory!
	if (level.flags & LEVEL_SPECKILLMONSTERS)
	{ // Kill any remaining monsters
		P_Massacre ();
	}
	if (level.flags & LEVEL_MAP07SPECIAL)
	{
		if (type == NAME_Fatso)
		{
			EV_DoFloor (DFloor::floorLowerToLowest, NULL, 666, FRACUNIT, 0, -1, 0, false);
			return;
		}
		
		if (type == NAME_Arachnotron)
		{
			EV_DoFloor (DFloor::floorRaiseByTexture, NULL, 667, FRACUNIT, 0, -1, 0, false);
			return;
		}
	}
	else
	{
		switch (level.flags & LEVEL_SPECACTIONSMASK)
		{
		case LEVEL_SPECLOWERFLOOR:
			EV_DoFloor (DFloor::floorLowerToLowest, NULL, 666, FRACUNIT, 0, -1, 0, false);
			return;
		
		case LEVEL_SPECLOWERFLOORTOHIGHEST:
			EV_DoFloor (DFloor::floorLowerToHighest, NULL, 666, FRACUNIT, 0, -1, 0, false);
			return;
		
		case LEVEL_SPECOPENDOOR:
			EV_DoDoor (DDoor::doorOpen, NULL, NULL, 666, 8*FRACUNIT, 0, 0, 0);
			return;
		}
	}

	// [RH] If noexit, then don't end the level.
	// [BC] Teamgame, too.
	if ((deathmatch || teamgame || alwaysapplydmflags) && (dmflags & DF_NO_EXIT))
		return;

	G_ExitLevel (0, false);
}

DEFINE_ACTION_FUNCTION(AActor, A_BossDeath)
{
	PARAM_ACTION_PROLOGUE;
	A_BossDeath(self);
	return 0;
}

//----------------------------------------------------------------------------
//
// PROC P_Massacre
//
// Kills all monsters.
//
//----------------------------------------------------------------------------

int P_Massacre ()
{
	// jff 02/01/98 'em' cheat - kill all monsters
	// partially taken from Chi's .46 port
	//
	// killough 2/7/98: cleaned up code and changed to use dprintf;
	// fixed lost soul bug (LSs left behind when PEs are killed)

	int killcount = 0;
	AActor *actor;
	TThinkerIterator<AActor> iterator;

	// [BC] This is handled server side.
	if ( NETWORK_InClientMode() )
	{
		return ( 0 );
	}

	while ( (actor = iterator.Next ()) )
	{
		if (!(actor->flags2 & MF2_DORMANT) && (actor->flags3 & MF3_ISMONSTER))
		{
			killcount += actor->Massacre();
		}
	}
	return killcount;
}

//
// A_SinkMobj
// Sink a mobj incrementally into the floor
//

bool A_SinkMobj (AActor *actor, double speed)
{
	if (actor->Floorclip < actor->Height)
	{
		actor->Floorclip += speed;
		return false;
	}
	return true;
}

//
// A_RaiseMobj
// Raise a mobj incrementally from the floor to 
// 

bool A_RaiseMobj (AActor *actor, double speed)
{
	bool done = true;

	// Raise a mobj from the ground
	if (actor->Floorclip > 0)
	{
		actor->Floorclip -= speed;
		if (actor->Floorclip <= 0)
		{
			actor->Floorclip = 0;
			done = true;
		}
		else
		{
			done = false;
		}
	}
	return done;		// Reached target height
}

DEFINE_ACTION_FUNCTION(AActor, A_ClassBossHealth)
{
	PARAM_ACTION_PROLOGUE;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return 0;
	}

	// [BC] Teamgame, too.
	if (( NETWORK_GetState( ) != NETSTATE_SINGLE ) && (( deathmatch || teamgame ) == false ))		// co-op only
	{
		if (!self->special1)
		{
			self->health *= 5;
			self->special1 = true;   // has been initialized
		}
	}
	return 0;
}
