#ifndef __P_ENEMY_H__
#define __P_ENEMY_H__

#include "thingdef/thingdef.h"
#include "tables.h"

struct sector_t;
class AActor;
class AInventory;
class PClass;


enum dirtype_t
{
	DI_EAST,
	DI_NORTHEAST,
	DI_NORTH,
	DI_NORTHWEST,
	DI_WEST,
	DI_SOUTHWEST,
	DI_SOUTH,
	DI_SOUTHEAST,
	DI_NODIR,
	NUMDIRS
};

extern fixed_t xspeed[8], yspeed[8];

enum LO_Flags
{
	LOF_NOSIGHTCHECK = 1,
	LOF_NOSOUNDCHECK = 2,
	LOF_DONTCHASEGOAL = 4,
	LOF_NOSEESOUND = 8,
	LOF_FULLVOLSEESOUND = 16,
    LOF_NOJUMP = 32,
};

struct FLookExParams
{
	angle_t fov;
	fixed_t mindist;
	fixed_t maxdist;
	fixed_t maxheardist;
	int flags;
	FState *seestate;
};

void P_DaggerAlert (AActor *target, AActor *emitter);
void P_RecursiveSound (sector_t *sec, AActor *soundtarget, bool splash, int soundblocks, AActor *emitter=NULL, fixed_t maxdist=0);
bool P_HitFriend (AActor *self);
void P_NoiseAlert (AActor *target, AActor *emmiter, bool splash=false, fixed_t maxdist=0);
inline void P_NoiseAlert(AActor *target, AActor *emmiter, bool splash, double maxdist)
{
	P_NoiseAlert(target, emmiter, splash, FLOAT2FIXED(maxdist));
}

bool P_CheckMeleeRange2 (AActor *actor);
bool P_Move (AActor *actor);
bool P_TryWalk (AActor *actor);
void P_NewChaseDir (AActor *actor);
AInventory *P_DropItem (AActor *source, PClassActor *type, int special, int chance);
void P_TossItem (AActor *item);
bool P_LookForPlayers (AActor *actor, INTBOOL allaround, FLookExParams *params);
void A_Weave(AActor *self, int xyspeed, int zspeed, double xydist, double zdist);
void A_Unblock(AActor *self, bool drop);

DECLARE_ACTION(A_Look)
DECLARE_ACTION(A_BossDeath)
DECLARE_ACTION(A_Pain)
DECLARE_ACTION(A_MonsterRail)
DECLARE_ACTION(A_NoBlocking)
DECLARE_ACTION(A_Scream)
DECLARE_ACTION(A_FreezeDeath)
DECLARE_ACTION(A_FreezeDeathChunks)
void A_BossDeath(AActor *self);

void A_Wander(AActor *self, int flags = 0);
void A_Chase(VMFrameStack *stack, AActor *self);
void A_FaceTarget(AActor *actor);
void A_Face(AActor *self, AActor *other, angle_t max_turn = 0, angle_t max_pitch = ANGLE_270, angle_t ang_offset = 0, angle_t pitch_offset = 0, int flags = 0, fixed_t z_add = 0);

bool A_RaiseMobj (AActor *, double speed);
bool A_SinkMobj (AActor *, double speed);

bool CheckBossDeath (AActor *);
int P_Massacre ();
bool P_CheckMissileRange (AActor *actor);

#define SKULLSPEED (20.)
void A_SkullAttack(AActor *self, double speed);

#endif //__P_ENEMY_H__
