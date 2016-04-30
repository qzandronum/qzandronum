#ifndef __DSECTOREFFECT_H__
#define __DSECTOREFFECT_H__

#include "dthinker.h"
#include "r_defs.h"

class DSectorEffect : public DThinker
{
	DECLARE_CLASS (DSectorEffect, DThinker)
public:
	DSectorEffect (sector_t *sector);

	void Serialize (FArchive &arc);
	void Destroy();

	sector_t *GetSector() const { return m_Sector; }

protected:
	DSectorEffect ();
	sector_t *m_Sector;
};

class DMover : public DSectorEffect
{
	DECLARE_CLASS (DMover, DSectorEffect)
	HAS_OBJECT_POINTERS
public:
	DMover (sector_t *sector);
protected:
	TObjPtr<DInterpolation> interpolation;
private:
protected:
	DMover ();
	void Serialize (FArchive &arc);
	// [BB] Changed Destroy to public, so that it can be called in cl_main.cpp.
public:
	void Destroy();
protected:
	void StopInterpolation(bool force = false);
};

class DMovingFloor : public DMover
{
	DECLARE_CLASS (DMovingFloor, DMover)
public:
	DMovingFloor (sector_t *sector);
protected:
	DMovingFloor ();
};

class DMovingCeiling : public DMover
{
	DECLARE_CLASS (DMovingCeiling, DMover)
public:
	DMovingCeiling (sector_t *sector);
protected:
	DMovingCeiling ();
};

#endif //__DSECTOREFFECT_H__
