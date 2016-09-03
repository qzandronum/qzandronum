/*
** gl_scene.cpp
** manages the rendering of the player's view
**
**---------------------------------------------------------------------------
** Copyright 2004-2005 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
** 4. When not used as part of GZDoom or a GZDoom derivative, this code will be
**    covered by the terms of the GNU Lesser General Public License as published
**    by the Free Software Foundation; either version 2.1 of the License, or (at
**    your option) any later version.
** 5. Full disclosure of the entire project's source code, except for third
**    party libraries is mandatory. (NOTE: This clause is non-negotiable!)
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include "gl/system/gl_system.h"
#include "gi.h"
#include "m_png.h"
#include "m_random.h"
#include "st_stuff.h"
#include "dobject.h"
#include "doomstat.h"
#include "g_level.h"
#include "r_data/r_interpolate.h"
#include "r_utility.h"
#include "d_player.h"
#include "p_effect.h"
#include "sbar.h"
#include "po_man.h"
#include "r_utility.h"
#include "a_hexenglobal.h"
#include "p_local.h"
#include "gl/gl_functions.h"

#include "gl/dynlights/gl_lightbuffer.h"
#include "gl/system/gl_interface.h"
#include "gl/system/gl_framebuffer.h"
#include "gl/system/gl_cvars.h"
#include "gl/renderer/gl_lightdata.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_renderbuffers.h"
#include "gl/data/gl_data.h"
#include "gl/data/gl_vertexbuffer.h"
#include "gl/dynlights/gl_dynlight.h"
#include "gl/models/gl_models.h"
#include "gl/scene/gl_clipper.h"
#include "gl/scene/gl_drawinfo.h"
#include "gl/scene/gl_portal.h"
#include "gl/shaders/gl_shader.h"
#include "gl/stereo3d/gl_stereo3d.h"
#include "gl/stereo3d/scoped_view_shifter.h"
#include "gl/textures/gl_translate.h"
#include "gl/textures/gl_material.h"
#include "gl/textures/gl_skyboxtexture.h"
#include "gl/utility/gl_clock.h"
#include "gl/utility/gl_convert.h"
#include "gl/utility/gl_templates.h"

//==========================================================================
//
// CVARs
//
//==========================================================================
CVAR(Bool, gl_texture, true, 0)
CVAR(Bool, gl_no_skyclear, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Float, gl_mask_threshold, 0.5f,CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Float, gl_mask_sprite_threshold, 0.5f,CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
CVAR(Bool, gl_sort_textures, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG)

EXTERN_CVAR (Bool, cl_capfps)
EXTERN_CVAR (Bool, r_deathcamera)

// [BB]
CVAR( Bool, cl_disallowfullpitch, false, CVAR_ARCHIVE )
EXTERN_CVAR (Bool, cl_oldfreelooklimit)

extern int viewpitch;
extern bool NoInterpolateView;
extern bool r_showviewer;

DWORD			gl_fixedcolormap;
area_t			in_area;
TArray<BYTE> currentmapsection;

void gl_ParseDefs();

//-----------------------------------------------------------------------------
//
// R_FrustumAngle
//
//-----------------------------------------------------------------------------
angle_t FGLRenderer::FrustumAngle()
{
	float tilt= fabs(mAngles.Pitch.Degrees);

	// If the pitch is larger than this you can look all around at a FOV of 90�
	if (tilt>46.0f) return 0xffffffff;

	// ok, this is a gross hack that barely works...
	// but at least it doesn't overestimate too much...
	double floatangle=2.0+(45.0+((tilt/1.9)))*mCurrentFoV*48.0/BaseRatioSizes[WidescreenRatio][3]/90.0;
	angle_t a1 = DAngle(floatangle).BAMs();
	if (a1>=ANGLE_180) return 0xffffffff;
	return a1;
}

//-----------------------------------------------------------------------------
//
// Sets the area the camera is in
//
//-----------------------------------------------------------------------------
void FGLRenderer::SetViewArea()
{
	// The render_sector is better suited to represent the current position in GL
	viewsector = R_PointInSubsector(ViewPos)->render_sector;

	// Get the heightsec state from the render sector, not the current one!
	if (viewsector->heightsec && !(viewsector->heightsec->MoreFlags & SECF_IGNOREHEIGHTSEC))
	{
		in_area = ViewPos.Z <= viewsector->heightsec->floorplane.ZatPoint(ViewPos) ? area_below :
				(ViewPos.Z > viewsector->heightsec->ceilingplane.ZatPoint(ViewPos) &&
				!(viewsector->heightsec->MoreFlags&SECF_FAKEFLOORONLY)) ? area_above : area_normal;
	}
	else
	{
		in_area = area_default;	// depends on exposed lower sectors
	}
}

//-----------------------------------------------------------------------------
//
// resets the 3D viewport
//
//-----------------------------------------------------------------------------

void FGLRenderer::Reset3DViewport()
{
	glViewport(mScreenViewport.left, mScreenViewport.top, mScreenViewport.width, mScreenViewport.height);
}

//-----------------------------------------------------------------------------
//
// sets 3D viewport and initial state
//
//-----------------------------------------------------------------------------

void FGLRenderer::Set3DViewport(bool mainview)
{
	if (mainview && mBuffers->Setup(mScreenViewport.width, mScreenViewport.height, mSceneViewport.width, mSceneViewport.height))
	{
		mBuffers->BindSceneFB();
	}

	// Always clear all buffers with scissor test disabled.
	// This is faster on newer hardware because it allows the GPU to skip
	// reading from slower memory where the full buffers are stored.
	glDisable(GL_SCISSOR_TEST);
	glClearColor(mSceneClearColor[0], mSceneClearColor[1], mSceneClearColor[2], 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	const auto &bounds = mSceneViewport;
	glViewport(bounds.left, bounds.top, bounds.width, bounds.height);
	glScissor(bounds.left, bounds.top, bounds.width, bounds.height);

	glEnable(GL_SCISSOR_TEST);

	glEnable(GL_MULTISAMPLE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS,0,~0);	// default stencil
	glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);
}

//-----------------------------------------------------------------------------
//
// Setup the camera position
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetViewAngle(DAngle viewangle)
{
	mAngles.Yaw = float(270.0-viewangle.Degrees);
	DVector2 v = ViewAngle.ToVector();
	mViewVector.X = v.X;
	mViewVector.Y = v.Y;

	R_SetViewAngle();
}
	

//-----------------------------------------------------------------------------
//
// SetProjection
// sets projection matrix
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetProjection(float fov, float ratio, float fovratio, float eyeShift) // [BB] Added eyeShift from GZ3Doom.
{

	float fovy = 2 * RAD2DEG(atan(tan(DEG2RAD(fov) / 2) / fovratio));
	// [BB] Added eyeShift from GZ3Doom.
	if ( eyeShift == 0 )
		gl_RenderState.mProjectionMatrix.perspective(fovy, ratio, 5.f, 65536.f);
	else
	{
		const float zNear = 5.0f;
		const float zFar = 65536.0f;
		const float pi = 3.1415926535897932384626433832795;
		double fH = tan( fovy / 360 * pi ) * zNear;
		double fW = fH * ratio;

		float screenZ = 25.0;
		float frustumShift = eyeShift * zNear / screenZ;

		gl_RenderState.mModelMatrix.frustum( -fW - frustumShift, fW - frustumShift, 
			-fH, fH, 
			zNear, zFar);
		gl_RenderState.mModelMatrix.translate(-eyeShift, 0, 0);
	}

}

// raw matrix input from stereo 3d modes
void FGLRenderer::SetProjection(VSMatrix matrix)
{
	gl_RenderState.mProjectionMatrix.loadIdentity();
	gl_RenderState.mProjectionMatrix.multMatrix(matrix);
}

//-----------------------------------------------------------------------------
//
// Setup the modelview matrix
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetViewMatrix(float vx, float vy, float vz, bool mirror, bool planemirror)
{
	float mult = mirror? -1:1;
	float planemult = planemirror? -glset.pixelstretch : glset.pixelstretch;

	gl_RenderState.mViewMatrix.loadIdentity();
	gl_RenderState.mViewMatrix.rotate(GLRenderer->mAngles.Roll.Degrees,  0.0f, 0.0f, 1.0f);
	gl_RenderState.mViewMatrix.rotate(GLRenderer->mAngles.Pitch.Degrees, 1.0f, 0.0f, 0.0f);
	gl_RenderState.mViewMatrix.rotate(GLRenderer->mAngles.Yaw.Degrees,   0.0f, mult, 0.0f);
	gl_RenderState.mViewMatrix.translate(vx * mult, -vz * planemult , -vy);
	gl_RenderState.mViewMatrix.scale(-mult, planemult, 1);
}


//-----------------------------------------------------------------------------
//
// SetupView
// Setup the view rotation matrix for the given viewpoint
//
//-----------------------------------------------------------------------------
void FGLRenderer::SetupView(float vx, float vy, float vz, DAngle va, bool mirror, bool planemirror)
{
	SetViewAngle(va);
	SetViewMatrix(vx, vy, vz, mirror, planemirror);
	gl_RenderState.ApplyMatrices();
}

//-----------------------------------------------------------------------------
//
// CreateScene
//
// creates the draw lists for the current scene
//
//-----------------------------------------------------------------------------

void FGLRenderer::CreateScene()
{
	// reset the portal manager
	GLPortal::StartFrame();
	PO_LinkToSubsectors();

	ProcessAll.Clock();

	// clip the scene and fill the drawlists
	for(unsigned i=0;i<portals.Size(); i++) portals[i]->glportal = NULL;
	gl_spriteindex=0;
	Bsp.Clock();
	GLRenderer->mVBO->Map();
	R_SetView();
	validcount++;	// used for processing sidedefs only once by the renderer.
	gl_RenderBSPNode (nodes + numnodes - 1);
	if (GLRenderer->mCurrentPortal != NULL) GLRenderer->mCurrentPortal->RenderAttached();
	Bsp.Unclock();

	// And now the crappy hacks that have to be done to avoid rendering anomalies:

	gl_drawinfo->HandleMissingTextures();	// Missing upper/lower textures
	gl_drawinfo->HandleHackedSubsectors();	// open sector hacks for deep water
	gl_drawinfo->ProcessSectorStacks();		// merge visplanes of sector stacks
	GLRenderer->mVBO->Unmap();

	ProcessAll.Unclock();

}

//-----------------------------------------------------------------------------
//
// RenderScene
//
// Draws the current draw lists for the non GLSL renderer
//
//-----------------------------------------------------------------------------

void FGLRenderer::RenderScene(int recursion)
{
	RenderAll.Clock();

	glDepthMask(true);
	if (!gl_no_skyclear) GLPortal::RenderFirstSkyPortal(recursion);

	gl_RenderState.SetCameraPos(ViewPos.X, ViewPos.Y, ViewPos.Z);

	gl_RenderState.EnableFog(true);
	gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);

	if (gl_sort_textures)
	{
		gl_drawinfo->drawlists[GLDL_PLAINWALLS].SortWalls();
		gl_drawinfo->drawlists[GLDL_PLAINFLATS].SortFlats();
		gl_drawinfo->drawlists[GLDL_MASKEDWALLS].SortWalls();
		gl_drawinfo->drawlists[GLDL_MASKEDFLATS].SortFlats();
		gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].SortWalls();
	}

	// if we don't have a persistently mapped buffer, we have to process all the dynamic lights up front,
	// so that we don't have to do repeated map/unmap calls on the buffer.
	bool haslights = mLightCount > 0 && gl_fixedcolormap == CM_DEFAULT && gl_lights;
	if (gl.lightmethod == LM_DEFERRED && haslights)
	{
		GLRenderer->mLights->Begin();
		gl_drawinfo->drawlists[GLDL_PLAINWALLS].DrawWalls(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_PLAINFLATS].DrawFlats(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_MASKEDWALLS].DrawWalls(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_MASKEDFLATS].DrawFlats(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].DrawWalls(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_TRANSLUCENTBORDER].Draw(GLPASS_LIGHTSONLY);
		gl_drawinfo->drawlists[GLDL_TRANSLUCENT].Draw(GLPASS_LIGHTSONLY, true);
		GLRenderer->mLights->Finish();
	}

	// Part 1: solid geometry. This is set up so that there are no transparent parts
	glDepthFunc(GL_LESS);
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	glDisable(GL_POLYGON_OFFSET_FILL);

	int pass;

	if (!haslights || gl.lightmethod == LM_DEFERRED)
	{
		pass = GLPASS_PLAIN;
	}
	else if (gl.lightmethod == LM_DIRECT)
	{
		pass = GLPASS_ALL;
	}
	else
	{
		// process everything that needs to handle textured dynamic lights.
		if (haslights) RenderMultipassStuff();

		// The remaining lists which are unaffected by dynamic lights are just processed as normal.
		pass = GLPASS_PLAIN;
	}

	gl_RenderState.EnableTexture(gl_texture);
	gl_RenderState.EnableBrightmap(true);
	gl_drawinfo->drawlists[GLDL_PLAINWALLS].DrawWalls(pass);
	gl_drawinfo->drawlists[GLDL_PLAINFLATS].DrawFlats(pass);


	// Part 2: masked geometry. This is set up so that only pixels with alpha>gl_mask_threshold will show
	if (!gl_texture) 
	{
		gl_RenderState.EnableTexture(true);
		gl_RenderState.SetTextureMode(TM_MASK);
	}
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_threshold);
	gl_drawinfo->drawlists[GLDL_MASKEDWALLS].DrawWalls(pass);
	gl_drawinfo->drawlists[GLDL_MASKEDFLATS].DrawFlats(pass);

	// Part 3: masked geometry with polygon offset. This list is empty most of the time so only waste time on it when in use.
	if (gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].Size() > 0)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-1.0f, -128.0f);
		gl_drawinfo->drawlists[GLDL_MASKEDWALLSOFS].DrawWalls(pass);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(0, 0);
	}

	gl_drawinfo->drawlists[GLDL_MODELS].Draw(pass);

	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Part 4: Draw decals (not a real pass)
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -128.0f);
	glDepthMask(false);

	// this is the only geometry type on which decals can possibly appear
	gl_drawinfo->drawlists[GLDL_PLAINWALLS].DrawDecals();
	if (gl.legacyMode)
	{
		// also process the render lists with walls and dynamic lights
        gl_drawinfo->dldrawlists[GLLDL_WALLS_PLAIN].DrawDecals();
        gl_drawinfo->dldrawlists[GLLDL_WALLS_FOG].DrawDecals();
	}

	gl_RenderState.SetTextureMode(TM_MODULATE);

	glDepthMask(true);


	// Push bleeding floor/ceiling textures back a little in the z-buffer
	// so they don't interfere with overlapping mid textures.
	glPolygonOffset(1.0f, 128.0f);

	// Part 5: flood all the gaps with the back sector's flat texture
	// This will always be drawn like GLDL_PLAIN, depending on the fog settings

	// [BB] We may only do this when drawing the final eye.
	GLint drawBuffer;
	glGetIntegerv ( GL_DRAW_BUFFER, &drawBuffer );
	if ( drawBuffer != GL_BACK_LEFT )
	{
		glDepthMask(false);							// don't write to Z-buffer!
		gl_RenderState.EnableFog(true);
		gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
		gl_RenderState.BlendFunc(GL_ONE,GL_ZERO);
		gl_drawinfo->DrawUnhandledMissingTextures();
	}
	glDepthMask(true);

	glPolygonOffset(0.0f, 0.0f);
	glDisable(GL_POLYGON_OFFSET_FILL);
	RenderAll.Unclock();

}

//-----------------------------------------------------------------------------
//
// RenderTranslucent
//
// Draws the current draw lists for the non GLSL renderer
//
//-----------------------------------------------------------------------------

void FGLRenderer::RenderTranslucent()
{
	RenderAll.Clock();

	glDepthMask(false);
	gl_RenderState.SetCameraPos(ViewPos.X, ViewPos.Y, ViewPos.Z);

	// final pass: translucent stuff
	gl_RenderState.AlphaFunc(GL_GEQUAL, gl_mask_sprite_threshold);
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl_RenderState.EnableBrightmap(true);
	gl_drawinfo->drawlists[GLDL_TRANSLUCENTBORDER].Draw(GLPASS_TRANSLUCENT);
	gl_drawinfo->drawlists[GLDL_TRANSLUCENT].DrawSorted();
	gl_RenderState.EnableBrightmap(false);

	glDepthMask(true);

	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.5f);
	RenderAll.Unclock();
}


//-----------------------------------------------------------------------------
//
// gl_drawscene - this function renders the scene from the current
// viewpoint, including mirrors and skyboxes and other portals
// It is assumed that the GLPortal::EndFrame returns with the 
// stencil, z-buffer and the projection matrix intact!
//
//-----------------------------------------------------------------------------

void FGLRenderer::DrawScene(int drawmode)
{
	static int recursion=0;

	if (camera != nullptr)
	{
		ActorRenderFlags savedflags = camera->renderflags;
		if (drawmode != DM_PORTAL && !r_showviewer)
		{
			camera->renderflags |= RF_INVISIBLE;
		}
		CreateScene();
		camera->renderflags = savedflags;
	}
	else
	{
		CreateScene();
	}
	GLRenderer->mClipPortal = NULL;	// this must be reset before any portal recursion takes place.

	RenderScene(recursion);

	// Handle all portals after rendering the opaque objects but before
	// doing all translucent stuff
	recursion++;
	GLPortal::EndFrame();
	recursion--;
	RenderTranslucent();
}


void gl_FillScreen()
{
	gl_RenderState.AlphaFunc(GL_GEQUAL, 0.f);
	gl_RenderState.EnableTexture(false);
	gl_RenderState.Apply();
	// The fullscreen quad is stored at index 4 in the main vertex buffer.
	GLRenderer->mVBO->RenderArray(GL_TRIANGLE_STRIP, FFlatVertexBuffer::FULLSCREEN_INDEX, 4);
}

//==========================================================================
//
// Draws a blend over the entire view
//
//==========================================================================
void FGLRenderer::DrawBlend(sector_t * viewsector)
{
	float blend[4]={0,0,0,0};
	PalEntry blendv=0;
	float extra_red;
	float extra_green;
	float extra_blue;
	player_t *player = NULL;

	if (players[consoleplayer].camera != NULL)
	{
		player=players[consoleplayer].camera->player;
	}

	// don't draw sector based blends when an invulnerability colormap is active
	if (!gl_fixedcolormap)
	{
		if (!viewsector->e->XFloor.ffloors.Size())
		{
			if (viewsector->heightsec && !(viewsector->MoreFlags&SECF_IGNOREHEIGHTSEC))
			{
				switch (in_area)
				{
				default:
				case area_normal: blendv = viewsector->heightsec->midmap; break;
				case area_above: blendv = viewsector->heightsec->topmap; break;
				case area_below: blendv = viewsector->heightsec->bottommap; break;
				}
			}
		}
		else
		{
			TArray<lightlist_t> & lightlist = viewsector->e->XFloor.lightlist;

			for (unsigned int i = 0; i < lightlist.Size(); i++)
			{
				double lightbottom;
				if (i < lightlist.Size() - 1)
					lightbottom = lightlist[i + 1].plane.ZatPoint(ViewPos);
				else
					lightbottom = viewsector->floorplane.ZatPoint(ViewPos);

				if (lightbottom < ViewPos.Z && (!lightlist[i].caster || !(lightlist[i].caster->flags&FF_FADEWALLS)))
				{
					// 3d floor 'fog' is rendered as a blending value
					blendv = lightlist[i].blend;
					// If this is the same as the sector's it doesn't apply!
					if (blendv == viewsector->ColorMap->Fade) blendv = 0;
					// a little hack to make this work for Legacy maps.
					if (blendv.a == 0 && blendv != 0) blendv.a = 128;
					break;
				}
			}
		}

		if (blendv.a == 0)
		{
			blendv = R_BlendForColormap(blendv);
			if (blendv.a == 255)
			{
				// The calculated average is too dark so brighten it according to the palettes's overall brightness
				int maxcol = MAX<int>(MAX<int>(framebuffer->palette_brightness, blendv.r), MAX<int>(blendv.g, blendv.b));
				blendv.r = blendv.r * 255 / maxcol;
				blendv.g = blendv.g * 255 / maxcol;
				blendv.b = blendv.b * 255 / maxcol;
			}
		}

		if (blendv.a == 255)
		{

			extra_red = blendv.r / 255.0f;
			extra_green = blendv.g / 255.0f;
			extra_blue = blendv.b / 255.0f;

			// If this is a multiplicative blend do it separately and add the additive ones on top of it.
			blendv = 0;

			// black multiplicative blends are ignored
			if (extra_red || extra_green || extra_blue)
			{
				gl_RenderState.BlendFunc(GL_DST_COLOR, GL_ZERO);
				gl_RenderState.SetColor(extra_red, extra_green, extra_blue, 1.0f);
				gl_FillScreen();
			}
		}
		else if (blendv.a)
		{
			V_AddBlend(blendv.r / 255.f, blendv.g / 255.f, blendv.b / 255.f, blendv.a / 255.0f, blend);
		}
	}

	// This mostly duplicates the code in shared_sbar.cpp
	// When I was writing this the original was called too late so that I
	// couldn't get the blend in time. However, since then I made some changes
	// here that would get lost if I switched back so I won't do it.

	if (player)
	{
		V_AddPlayerBlend(player, blend, 0.5, 175);
	}
	
	if (players[consoleplayer].camera != NULL)
	{
		// except for fadeto effects
		player_t *player = (players[consoleplayer].camera->player != NULL) ? players[consoleplayer].camera->player : &players[consoleplayer];
		V_AddBlend (player->BlendR, player->BlendG, player->BlendB, player->BlendA, blend);
	}

	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	if (blend[3]>0.0f)
	{
		gl_RenderState.SetColor(blend[0], blend[1], blend[2], blend[3]);
		gl_FillScreen();
	}
	gl_RenderState.ResetColor();
	gl_RenderState.EnableTexture(true);
}


//-----------------------------------------------------------------------------
//
// Draws player sprites and color blend
//
//-----------------------------------------------------------------------------


void FGLRenderer::EndDrawScene(sector_t * viewsector)
{
	gl_RenderState.EnableFog(false);

	// [BB] HUD models need to be rendered here. Make sure that
	// DrawPlayerSprites is only called once. Either to draw
	// HUD models or to draw the weapon sprites.
	const bool renderHUDModel = gl_IsHUDModelForPlayerAvailable( players[consoleplayer].camera->player );
	if ( renderHUDModel )
	{
		// [BB] The HUD model should be drawn over everything else already drawn.
		glClear(GL_DEPTH_BUFFER_BIT);
		DrawPlayerSprites (viewsector, true);
	}

	glDisable(GL_STENCIL_TEST);

	framebuffer->Begin2D(false);

	Reset3DViewport();
	// [BB] Only draw the sprites if we didn't render a HUD model before.
	if ( renderHUDModel == false )
	{
		DrawPlayerSprites (viewsector, false);
	}
	if (gl.legacyMode)
	{
		int cm = gl_RenderState.GetFixedColormap();
		gl_RenderState.SetFixedColormap(cm);
		gl_RenderState.DrawColormapOverlay();
	}

	gl_RenderState.SetFixedColormap(CM_DEFAULT);
	gl_RenderState.SetSoftLightLevel(-1);
	DrawTargeterSprites();
	if (!FGLRenderBuffers::IsEnabled())
	{
		DrawBlend(viewsector);
	}

	// Restore standard rendering state
	gl_RenderState.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	gl_RenderState.ResetColor();
	gl_RenderState.EnableTexture(true);
	glDisable(GL_SCISSOR_TEST);
}


//-----------------------------------------------------------------------------
//
// R_RenderView - renders one view - either the screen or a camera texture
//
//-----------------------------------------------------------------------------

void FGLRenderer::ProcessScene(bool toscreen)
{
	FDrawInfo::StartDrawInfo();
	iter_dlightf = iter_dlight = draw_dlight = draw_dlightf = 0;
	GLPortal::BeginScene();

	int mapsection = R_PointInSubsector(ViewPos)->mapsection;
	memset(&currentmapsection[0], 0, currentmapsection.Size());
	currentmapsection[mapsection>>3] |= 1 << (mapsection & 7);
	DrawScene(toscreen ? DM_MAINVIEW : DM_OFFSCREEN);
	FDrawInfo::EndDrawInfo();

}

//-----------------------------------------------------------------------------
//
// gl_SetFixedColormap
//
//-----------------------------------------------------------------------------

void FGLRenderer::SetFixedColormap (player_t *player)
{
	gl_fixedcolormap=CM_DEFAULT;

	// check for special colormaps
	player_t * cplayer = player->camera->player;
	if (cplayer) 
	{
		if (cplayer->extralight == INT_MIN)
		{
			gl_fixedcolormap=CM_FIRSTSPECIALCOLORMAP + INVERSECOLORMAP;
			extralight=0;
		}
		else if (cplayer->fixedcolormap != NOFIXEDCOLORMAP)
		{
			gl_fixedcolormap = CM_FIRSTSPECIALCOLORMAP + cplayer->fixedcolormap;
		}
		else if (cplayer->fixedlightlevel != -1)
		{
			for(AInventory * in = cplayer->mo->Inventory; in; in = in->Inventory)
			{
				PalEntry color = in->GetBlend ();

				// Need special handling for light amplifiers 
				if (in->IsKindOf(RUNTIME_CLASS(APowerTorch)))
				{
					gl_fixedcolormap = cplayer->fixedlightlevel + CM_TORCH;
				}
				else if (in->IsKindOf(RUNTIME_CLASS(APowerLightAmp)))
				{
					gl_fixedcolormap = CM_LITE;
				}
			}
		}
	}
	gl_RenderState.SetFixedColormap(gl_fixedcolormap);
}

//-----------------------------------------------------------------------------
//
// Renders one viewpoint in a scene
//
//-----------------------------------------------------------------------------

sector_t * FGLRenderer::RenderViewpoint (AActor * camera, GL_IRECT * bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen)
{       
	sector_t * retval;
	mSceneClearColor[0] = 0.0f;
	mSceneClearColor[1] = 0.0f;
	mSceneClearColor[2] = 0.0f;
	R_SetupFrame (camera);
	SetViewArea();

	// We have to scale the pitch to account for the pixel stretching, because the playsim doesn't know about this and treats it as 1:1.
	double radPitch = ViewPitch.Normalized180().Radians();
	double angx = cos(radPitch);
	double angy = sin(radPitch) * glset.pixelstretch;
	double alen = sqrt(angx*angx + angy*angy);

	mAngles.Pitch = (float)RAD2DEG(asin(angy / alen));
	mAngles.Roll.Degrees = ViewRoll.Degrees;

	// Scroll the sky
	mSky1Pos = (float)fmod(gl_frameMS * level.skyspeed1, 1024.f) * 90.f/256.f;
	mSky2Pos = (float)fmod(gl_frameMS * level.skyspeed2, 1024.f) * 90.f/256.f;



	// [BB] consoleplayer should be able to toggle the chase cam.
	if (camera->player && /*camera->player-players==consoleplayer &&*/
		((/*camera->player->*/players[consoleplayer].cheats & CF_CHASECAM) || (r_deathcamera && camera->health <= 0)) && camera==camera->player->mo)
	{
		mViewActor=NULL;
	}
	else
	{
		mViewActor=camera;
	}

	if (toscreen)
	{
		if (gl_exposure == 0.0f)
		{
			float light = viewsector->lightlevel / 255.0f;
			float exposure = MAX(1.0f + (1.0f - light * light) * 0.9f, 0.5f);
			mCameraExposure = mCameraExposure * 0.995f + exposure * 0.005f;
		}
		else
		{
			mCameraExposure = gl_exposure;
		}
	}

	// 'viewsector' will not survive the rendering so it cannot be used anymore below.
	retval = viewsector;

	// Render (potentially) multiple views for stereo 3d
	float viewShift[3];
	const s3d::Stereo3DMode& stereo3dMode = s3d::Stereo3DMode::getCurrentMode();
	stereo3dMode.SetUp();
	for (int eye_ix = 0; eye_ix < stereo3dMode.eye_count(); ++eye_ix)
	{
		const s3d::EyePose * eye = stereo3dMode.getEyePose(eye_ix);
		eye->SetUp();
		// TODO: stereo specific viewport - needed when implementing side-by-side modes etc.
		SetOutputViewport(bounds);
		Set3DViewport(mainview);
		mDrawingScene2D = true;
		mCurrentFoV = fov;
		// Stereo mode specific perspective projection
		SetProjection( eye->GetProjection(fov, ratio, fovratio) );
		// SetProjection(fov, ratio, fovratio);	// switch to perspective mode and set up clipper
		SetViewAngle(ViewAngle);
		// Stereo mode specific viewpoint adjustment - temporarily shifts global ViewPos
		eye->GetViewShift(GLRenderer->mAngles.Yaw.Degrees, viewShift);
		s3d::ScopedViewShifter viewShifter(viewShift);
		SetViewMatrix(ViewPos.X, ViewPos.Y, ViewPos.Z, false, false);
		gl_RenderState.ApplyMatrices();

		clipper.Clear();
		angle_t a1 = FrustumAngle();
		clipper.SafeAddClipRangeRealAngles(ViewAngle.BAMs() + a1, ViewAngle.BAMs() - a1);

		ProcessScene(toscreen);
		if (mainview && toscreen) EndDrawScene(retval);	// do not call this for camera textures.
		if (mainview && FGLRenderBuffers::IsEnabled())
		{
			mBuffers->BlitSceneToTexture();
			BloomScene();
			TonemapScene();
			ColormapScene();
			LensDistortScene();
			DrawBlend(viewsector);	// This should be done after postprocessing, not before.
		}
		mDrawingScene2D = false;
		eye->TearDown();
	}
	stereo3dMode.TearDown();

	gl_frameCount++;	// This counter must be increased right before the interpolations are restored.
	interpolator.RestoreInterpolations ();
	return retval;
}

//-----------------------------------------------------------------------------
//
// renders the view
//
//-----------------------------------------------------------------------------

#ifdef _WIN32 // [BB] Detect some kinds of glBegin hooking.
extern char myGlBeginCharArray[4];
int crashoutTic = 0;
#endif

void FGLRenderer::RenderView (player_t* player)
{
#ifdef _WIN32 // [BB] Detect some kinds of glBegin hooking.
	// [BB] Continuously make this check, otherwise a hack could bypass the check by activating
	// and deactivating itself at the right time interval.
	{
		if ( strncmp(reinterpret_cast<char *>(glBegin), myGlBeginCharArray, 4) )
		{
			I_FatalError ( "OpenGL malfunction encountered.\n" );
		}
		else
		{
			// [BB] Most GL wallhacks deactivate GL_DEPTH_TEST by manipulating glBegin.
			// Here we try check if this is done.
			GLboolean oldValue;
			glGetBooleanv ( GL_DEPTH_TEST, &oldValue );
			glEnable ( GL_DEPTH_TEST );
			glBegin( GL_TRIANGLE_STRIP );
			glEnd();
			GLboolean valueTrue;
			glGetBooleanv ( GL_DEPTH_TEST, &valueTrue );

			// [BB] Also check if glGetBooleanv simply always returns true.
			glDisable ( GL_DEPTH_TEST );
			GLboolean valueFalse;
			glGetBooleanv ( GL_DEPTH_TEST, &valueFalse );

			if ( ( valueTrue == false ) || ( valueFalse == true ) )
			{
				I_FatalError ( "OpenGL malfunction encountered.\n" );
			}

			if ( oldValue )
				glEnable ( GL_DEPTH_TEST );
			else
				glDisable ( GL_DEPTH_TEST );
		}
	}
#endif

	OpenGLFrameBuffer* GLTarget = static_cast<OpenGLFrameBuffer*>(screen);
	AActor *&LastCamera = GLTarget->LastCamera;

	checkBenchActive();
	if (player->camera != LastCamera)
	{
		// If the camera changed don't interpolate
		// Otherwise there will be some not so nice effects.
		R_ResetViewInterpolation();
		LastCamera=player->camera;
	}

	gl_RenderState.SetVertexBuffer(mVBO);
	GLRenderer->mVBO->Reset();

	// reset statistics counters
	ResetProfilingData();

	// Get this before everything else
	if (cl_capfps || r_NoInterpolate) r_TicFracF = 1.;
	else r_TicFracF = I_GetTimeFrac (&r_FrameTime);
	gl_frameMS = I_MSTime();

	P_FindParticleSubsectors ();

	if (!gl.legacyMode) GLRenderer->mLights->Clear();

	// NoInterpolateView should have no bearing on camera textures, but needs to be preserved for the main view below.
	bool saved_niv = NoInterpolateView;
	NoInterpolateView = false;
	// prepare all camera textures that have been used in the last frame
	FCanvasTextureInfo::UpdateAll();
	NoInterpolateView = saved_niv;


	// I stopped using BaseRatioSizes here because the information there wasn't well presented.
	//							4:3				16:9		16:10		17:10		5:4
	static float ratios[]={1.333333f, 1.777777f, 1.6f, 1.7f, 1.25f, 1.7f, 2.333333f};

	// now render the main view
	float fovratio;
	float ratio = ratios[WidescreenRatio];
	if (! Is54Aspect(WidescreenRatio))
	{
		fovratio = 1.333333f;
	}
	else
	{
		fovratio = ratio;
	}

	SetFixedColormap (player);

	// Check if there's some lights. If not some code can be skipped.
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	GLRenderer->mLightCount = ((it.Next()) != NULL);

	sector_t * viewsector = RenderViewpoint(player->camera, NULL, FieldOfView.Degrees, ratio, fovratio, true, true);

	All.Unclock();
}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void FGLRenderer::WriteSavePic (player_t *player, FILE *file, int width, int height)
{
	GL_IRECT bounds;

	bounds.left=0;
	bounds.top=0;
	bounds.width=width;
	bounds.height=height;
	glFlush();
	SetFixedColormap(player);
	gl_RenderState.SetVertexBuffer(mVBO);
	GLRenderer->mVBO->Reset();
	if (!gl.legacyMode) GLRenderer->mLights->Clear();

	// Check if there's some lights. If not some code can be skipped.
	TThinkerIterator<ADynamicLight> it(STAT_DLIGHT);
	GLRenderer->mLightCount = ((it.Next()) != NULL);

	sector_t *viewsector = RenderViewpoint(players[consoleplayer].camera, &bounds, 
								FieldOfView.Degrees, 1.6f, 1.6f, true, false);
	glDisable(GL_STENCIL_TEST);
	gl_RenderState.SetFixedColormap(CM_DEFAULT);
	gl_RenderState.SetSoftLightLevel(-1);
	screen->Begin2D(false);
	if (!FGLRenderBuffers::IsEnabled())
	{
		DrawBlend(viewsector);
	}
	CopyToBackbuffer(&bounds, false);
	glFlush();

	byte * scr = (byte *)M_Malloc(width * height * 3);
	glReadPixels(0,0,width, height,GL_RGB,GL_UNSIGNED_BYTE,scr);
	M_CreatePNG (file, scr + ((height-1) * width * 3), NULL, SS_RGB, width, height, -width*3);
	M_Free(scr);

	// [BC] In GZDoom, this is called every frame, regardless of whether or not
	// the view is active. In Skulltag, we don't so we have to call this here
	// to reset everything, such as the viewport, after rendering our view to
	// a canvas.
	FGLRenderer::EndDrawScene( viewsector );
}


//===========================================================================
//
//
//
//===========================================================================

struct FGLInterface : public FRenderer
{
	bool UsesColormap() const;
	void PrecacheTexture(FTexture *tex, int cache);
	void PrecacheSprite(FTexture *tex, SpriteHits &hits);
	void Precache(BYTE *texhitlist, TMap<PClassActor*, bool> &actorhitlist);
	void RenderView(player_t *player);
	void WriteSavePic (player_t *player, FILE *file, int width, int height);
	void StateChanged(AActor *actor);
	void StartSerialize(FArchive &arc);
	void EndSerialize(FArchive &arc);
	void RenderTextureView (FCanvasTexture *self, AActor *viewpoint, int fov);
	sector_t *FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, bool back);
	void SetFogParams(int _fogdensity, PalEntry _outsidefogcolor, int _outsidefogdensity, int _skyfog);
	void PreprocessLevel();
	void CleanLevelData();
	bool RequireGLNodes();

	int GetMaxViewPitch(bool down);
	void ClearBuffer(int color);
	void Init();
};

//===========================================================================
//
// The GL renderer has no use for colormaps so let's
// not create them and save us some time.
//
//===========================================================================

bool FGLInterface::UsesColormap() const
{
	return false;
}

//==========================================================================
//
// DFrameBuffer :: PrecacheTexture
//
//==========================================================================

void FGLInterface::PrecacheTexture(FTexture *tex, int cache)
{
	if (cache & (FTextureManager::HIT_Wall | FTextureManager::HIT_Flat | FTextureManager::HIT_Sky))
	{
		FMaterial * gltex = FMaterial::ValidateTexture(tex, false);
		if (gltex) gltex->Precache();
	}
}

//==========================================================================
//
// DFrameBuffer :: PrecacheSprite
//
//==========================================================================

void FGLInterface::PrecacheSprite(FTexture *tex, SpriteHits &hits)
{
	FMaterial * gltex = FMaterial::ValidateTexture(tex, true);
	if (gltex) gltex->PrecacheList(hits);
}

//==========================================================================
//
// DFrameBuffer :: Precache
//
//==========================================================================

void FGLInterface::Precache(BYTE *texhitlist, TMap<PClassActor*, bool> &actorhitlist)
{
	SpriteHits *spritelist = new SpriteHits[sprites.Size()];
	SpriteHits **spritehitlist = new SpriteHits*[TexMan.NumTextures()];
	TMap<PClassActor*, bool>::Iterator it(actorhitlist);
	TMap<PClassActor*, bool>::Pair *pair;
	BYTE *modellist = new BYTE[Models.Size()];
	memset(modellist, 0, Models.Size());
	memset(spritehitlist, 0, sizeof(SpriteHits**) * TexMan.NumTextures());

	// this isn't done by the main code so it needs to be done here first:
	// check skybox textures and mark the separate faces as used
	for (int i = 0; i<TexMan.NumTextures(); i++)
	{
		// HIT_Wall must be checked for MBF-style sky transfers. 
		if (texhitlist[i] & (FTextureManager::HIT_Sky | FTextureManager::HIT_Wall))
		{
			FTexture *tex = TexMan.ByIndex(i);
			if (tex->gl_info.bSkybox)
			{
				FSkyBox *sb = static_cast<FSkyBox*>(tex);
				for (int i = 0; i<6; i++)
				{
					if (sb->faces[i])
					{
						int index = sb->faces[i]->id.GetIndex();
						texhitlist[index] |= FTextureManager::HIT_Flat;
					}
				}
			}
		}
	}

	// Check all used actors.
	// 1. mark all sprites associated with its states
	// 2. mark all model data and skins associated with its states
	while (it.NextPair(pair))
	{
		PClassActor *cls = pair->Key;
		int gltrans = GLTranslationPalette::GetInternalTranslation(GetDefaultByType(cls)->Translation);

		for (int i = 0; i < cls->NumOwnedStates; i++)
		{
			spritelist[cls->OwnedStates[i].sprite].Insert(gltrans, true);
			FSpriteModelFrame * smf = gl_FindModelFrame(cls, cls->OwnedStates[i].sprite, cls->OwnedStates[i].Frame, false);
			if (smf != NULL)
			{
				for (int i = 0; i < MAX_MODELS_PER_FRAME; i++)
				{
					if (smf->skinIDs[i].isValid())
					{
						texhitlist[smf->skinIDs[i].GetIndex()] |= FTexture::TEX_Flat;
					}
					else if (smf->modelIDs[i] != -1)
					{
						Models[smf->modelIDs[i]]->PushSpriteMDLFrame(smf, i);
						Models[smf->modelIDs[i]]->AddSkins(texhitlist);
					}
					if (smf->modelIDs[i] != -1)
					{
						modellist[smf->modelIDs[i]] = 1;
					}
				}
			}
		}
	}

	// mark all sprite textures belonging to the marked sprites.
	for (int i = (int)(sprites.Size() - 1); i >= 0; i--)
	{
		if (spritelist[i].CountUsed())
		{
			int j, k;
			for (j = 0; j < sprites[i].numframes; j++)
			{
				const spriteframe_t *frame = &SpriteFrames[sprites[i].spriteframes + j];

				for (k = 0; k < 16; k++)
				{
					FTextureID pic = frame->Texture[k];
					if (pic.isValid())
					{
						spritehitlist[pic.GetIndex()] = &spritelist[i];
					}
				}
			}
		}
	}

	// delete everything unused before creating any new resources to avoid memory usage peaks.

	// delete unused models
	for (unsigned i = 0; i < Models.Size(); i++)
	{
		if (!modellist[i]) Models[i]->DestroyVertexBuffer();
	}

	// delete unused textures
	int cnt = TexMan.NumTextures();
	for (int i = cnt - 1; i >= 0; i--)
	{
		FTexture *tex = TexMan.ByIndex(i);
		if (tex != nullptr)
		{
			if (!texhitlist[i])
			{
				if (tex->gl_info.Material[0]) tex->gl_info.Material[0]->Clean(true);
			}
			if (spritehitlist[i] == nullptr || (*spritehitlist[i]).CountUsed() == 0)
			{
				if (tex->gl_info.Material[1]) tex->gl_info.Material[1]->Clean(true);
			}
		}
	}

	if (gl_precache)
	{
		// cache all used textures
		for (int i = cnt - 1; i >= 0; i--)
		{
			FTexture *tex = TexMan.ByIndex(i);
			if (tex != nullptr)
			{
				PrecacheTexture(tex, texhitlist[i]);
				if (spritehitlist[i] != nullptr && (*spritehitlist[i]).CountUsed() > 0)
				{
					PrecacheSprite(tex, *spritehitlist[i]);
				}
			}
		}

		// cache all used models
		for (unsigned i = 0; i < Models.Size(); i++)
		{
			if (modellist[i]) 
				Models[i]->BuildVertexBuffer();
		}
	}

	delete[] spritehitlist;
	delete[] spritelist;
	delete[] modellist;
}


//==========================================================================
//
// DFrameBuffer :: StateChanged
//
//==========================================================================

void FGLInterface::StateChanged(AActor *actor)
{
	gl_SetActorLights(actor);
}

//===========================================================================
//
// notify the renderer that serialization of the curent level is about to start/end
//
//===========================================================================

void FGLInterface::StartSerialize(FArchive &arc)
{
	gl_DeleteAllAttachedLights();
}

void gl_SerializeGlobals(FArchive &arc)
{
	arc << fogdensity << outsidefogdensity << skyfog;
}

void FGLInterface::EndSerialize(FArchive &arc)
{
	gl_RecreateAllAttachedLights();
	if (arc.IsLoading()) gl_InitPortals();
}

//===========================================================================
//
// Get max. view angle (renderer specific information so it goes here now)
//
//===========================================================================

EXTERN_CVAR(Float, maxviewpitch)

int FGLInterface::GetMaxViewPitch(bool down)
{
	// [BB] Zandronum clamps the pitch differently
	if (cl_disallowfullpitch) return down? 56 : ( cl_oldfreelooklimit ? 32 : 56 );
	else return maxviewpitch;
}

//===========================================================================
//
// 
//
//===========================================================================

void FGLInterface::ClearBuffer(int color)
{
	PalEntry pe = GPalette.BaseColors[color];
	GLRenderer->mSceneClearColor[0] = pe.r / 255.f;
	GLRenderer->mSceneClearColor[1] = pe.g / 255.f;
	GLRenderer->mSceneClearColor[2] = pe.b / 255.f;
}

//===========================================================================
//
// Render the view to a savegame picture
//
//===========================================================================

void FGLInterface::WriteSavePic (player_t *player, FILE *file, int width, int height)
{
	GLRenderer->WriteSavePic(player, file, width, height);
}

//===========================================================================
//
// 
//
//===========================================================================

void FGLInterface::RenderView(player_t *player)
{
	GLRenderer->RenderView(player);
}

//===========================================================================
//
// 
//
//===========================================================================

void FGLInterface::Init()
{
	gl_ParseDefs();
	gl_InitData();
}

//===========================================================================
//
// Camera texture rendering
//
//===========================================================================
CVAR(Bool, gl_usefb, false , CVAR_ARCHIVE|CVAR_GLOBALCONFIG)
extern TexFilter_s TexFilter[];

void FGLInterface::RenderTextureView (FCanvasTexture *tex, AActor *Viewpoint, int FOV)
{
	FMaterial * gltex = FMaterial::ValidateTexture(tex, false);

	int width = gltex->TextureWidth();
	int height = gltex->TextureHeight();

	gl_fixedcolormap=CM_DEFAULT;
	gl_RenderState.SetFixedColormap(CM_DEFAULT);

	bool usefb = gl_usefb || gltex->GetWidth() > screen->GetWidth() || gltex->GetHeight() > screen->GetHeight();
	if (!usefb)
	{
		glFlush();
	}
	else
	{
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
		__try
#endif
		{
			GLRenderer->StartOffscreen();
			gltex->BindToFrameBuffer();
		}
#if defined(_WIN32) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
		__except(1)
		{
			usefb = false;
			gl_usefb = false;
			GLRenderer->EndOffscreen();
			glFlush();
		}
#endif
	}

	GL_IRECT bounds;
	bounds.left=bounds.top=0;
	bounds.width=FHardwareTexture::GetTexDimension(gltex->GetWidth());
	bounds.height=FHardwareTexture::GetTexDimension(gltex->GetHeight());

	GLRenderer->RenderViewpoint(Viewpoint, &bounds, FOV, (float)width/height, (float)width/height, false, false);

	if (!usefb)
	{
		glFlush();
		gl_RenderState.SetMaterial(gltex, 0, 0, -1, false);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, bounds.width, bounds.height);
	}
	else
	{
		GLRenderer->EndOffscreen();
	}

	tex->SetUpdated();
}

//==========================================================================
//
//
//
//==========================================================================

sector_t *FGLInterface::FakeFlat(sector_t *sec, sector_t *tempsec, int *floorlightlevel, int *ceilinglightlevel, bool back)
{
	if (floorlightlevel != NULL)
	{
		*floorlightlevel = sec->GetFloorLight ();
	}
	if (ceilinglightlevel != NULL)
	{
		*ceilinglightlevel = sec->GetCeilingLight ();
	}
	return gl_FakeFlat(sec, tempsec, back);
}

//===========================================================================
//
// 
//
//===========================================================================

void FGLInterface::SetFogParams(int _fogdensity, PalEntry _outsidefogcolor, int _outsidefogdensity, int _skyfog)
{
	gl_SetFogParams(_fogdensity, _outsidefogcolor, _outsidefogdensity, _skyfog);
}

void FGLInterface::PreprocessLevel() 
{
	gl_PreprocessLevel();
}

void FGLInterface::CleanLevelData() 
{
	gl_CleanLevelData();
}

bool FGLInterface::RequireGLNodes() 
{ 
	return true; 
}

//===========================================================================
//
// 
//
//===========================================================================

FRenderer *gl_CreateInterface()
{
	return new FGLInterface;
}


