/*
 * $Id$
 *
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2011 Blender Foundation.
 * All rights reserved.
 *
 *
 * Contributor(s): Blender Foundation,
 *                 Sergey Sharybin
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_clip/clip_draw.c
 *  \ingroup spclip
 */

#include "DNA_gpencil_types.h"
#include "DNA_movieclip_types.h"
#include "DNA_scene_types.h"
#include "DNA_object_types.h"	/* SELECT */

#include "MEM_guardedalloc.h"

#include "BKE_context.h"
#include "BKE_movieclip.h"
#include "BKE_tracking.h"

#include "IMB_imbuf_types.h"
#include "IMB_imbuf.h"

#include "BLI_utildefines.h"
#include "BLI_math.h"
#include "BLI_string.h"
#include "BLI_rect.h"
#include "BLI_math_base.h"

#include "ED_screen.h"
#include "ED_clip.h"
#include "ED_gpencil.h"

#include "BIF_gl.h"
#include "BIF_glutil.h"

#include "WM_api.h"
#include "WM_types.h"

#include "UI_resources.h"
#include "UI_view2d.h"

#include "RNA_access.h"

#include "BLF_api.h"

#include "clip_intern.h"	// own include

/*********************** main area drawing *************************/

static void draw_movieclip_cache(SpaceClip *sc, ARegion *ar, MovieClip *clip, Scene *scene)
{
	float x;
	int *points, totseg, i, a;
	float sfra= SFRA, efra= EFRA, framelen= ar->winx/(efra-sfra+1), fontsize, fontwidth;
	uiStyle *style= U.uistyles.first;
	int fontid= style->widget.uifont_id;
	char str[32];

	glEnable(GL_BLEND);

	/* cache background */
	glColor4ub(128, 128, 255, 64);
	glRecti(0, 0, ar->winx, 8);

	/* cached segments -- could be usefu lto debug caching strategies */
	BKE_movieclip_get_cache_segments(clip, &totseg, &points);
	if(totseg) {
		glColor4ub(128, 128, 255, 128);

		for(a= 0; a<totseg; a++) {
			float x1, x2;

			x1= (points[a*2]-sfra)/(efra-sfra+1)*ar->winx;
			x2= (points[a*2+1]-sfra+1)/(efra-sfra+1)*ar->winx;

			glRecti(x1, 0, x2, 8);
		}
	}

	/* track */
	if(clip->tracking.act_track) {
		MovieTrackingTrack *track= clip->tracking.act_track;

		for(i= sfra, a= 0; i <= efra; i++) {
			int framenr;
			MovieTrackingMarker *marker;

			while(a<track->markersnr) {
				if(track->markers[a].framenr>=i)
					break;

				if(a<track->markersnr-1 && track->markers[a+1].framenr>i)
					break;

				a++;
			}

			if(a<track->markersnr) marker= &track->markers[a];
			else marker= &track->markers[track->markersnr-1];

			if((marker->flag&MARKER_DISABLED)==0) {
				framenr= marker->framenr;

				if(framenr!=i) glColor4ub(128, 128, 0, 96);
				else glColor4ub(255, 255, 0, 96);

				glRecti((i-sfra)*framelen, 0, (i-sfra+1)*framelen, 4);
			}
		}
	}

	/* failed frames */
	if(clip->tracking.reconstruction.flag&TRACKING_RECONSTRUCTED) {
		int n= clip->tracking.reconstruction.camnr;
		MovieReconstructedCamera *cameras= clip->tracking.reconstruction.cameras;

		glColor4ub(255, 0, 0, 96);

		for(i= sfra, a= 0; i <= efra; i++) {
			int ok= 0;

			while(a<n) {
				if(cameras[a].framenr==i) {
					ok= 1;
					break;
				}
				else if(cameras[a].framenr>i) {
					break;
				}

				a++;
			}

			if(!ok)
				glRecti((i-sfra)*framelen, 0, (i-sfra+1)*framelen, 8);
		}
	}

	glDisable(GL_BLEND);

	/* current frame */
	x= (sc->user.framenr-sfra)/(efra-sfra+1)*ar->winx;

	UI_ThemeColor(TH_CFRAME);
	glRecti(x, 0, x+framelen, 8);

	/* frame number */
	BLF_size(fontid, 11.f, U.dpi);
	BLI_snprintf(str, sizeof(str), "%d", sc->user.framenr);
	fontsize= BLF_height(fontid, str);
	fontwidth= BLF_width(fontid, str);

	if(x+fontwidth+6.f<=ar->winx) {
		glRecti(x, 8.f, x+fontwidth+6.f, 12.f+fontsize);

		glColor3f(0.f, 0.f, 0.f);
		BLF_position(fontid, x+2.f, 10.f, 0.f);
		BLF_draw(fontid, str, strlen(str));
	} else {
		glRecti(x+framelen, 8.f, x+framelen-fontwidth-6.f, 12.f+fontsize);

		glColor3f(0.f, 0.f, 0.f);
		BLF_position(fontid, x-2.f-fontwidth+framelen, 10.f, 0.f);
		BLF_draw(fontid, str, strlen(str));
	}
}

static void draw_movieclip_buffer(SpaceClip *sc, ARegion *ar, ImBuf *ibuf,
			int width, int height, float zoomx, float zoomy)
{
	int x, y;

	/* set zoom */
	glPixelZoom(zoomx*width/ibuf->x, zoomy*height/ibuf->y);

	/* find window pixel coordinates of origin */
	UI_view2d_to_region_no_clip(&ar->v2d, 0.f, 0.f, &x, &y);

	if(sc->flag&SC_MUTE_FOOTAGE) {
		glColor3f(0.0f, 0.0f, 0.0f);
		glRectf(x, y, x+zoomx*width, y+zoomy*height);
	} else {
		if(ibuf->rect_float && !ibuf->rect) {
			IMB_rect_from_float(ibuf);
		}

		if(ibuf->rect) {
			MovieClip *clip= ED_space_clip(sc);

			glaDrawPixelsSafe(x, y, ibuf->x, ibuf->y, ibuf->x, GL_RGBA, GL_UNSIGNED_BYTE, ibuf->rect);

			/* draw boundary border for frame if stabilization is enabled */
			if(sc->flag&SC_SHOW_STABLE && clip->tracking.stabilization.flag&TRACKING_2D_STABILIZATION) {
				glColor3f(0.f, 0.f, 0.f);
				glLineStipple(3, 0xaaaa);
				glEnable(GL_LINE_STIPPLE);
				glEnable(GL_COLOR_LOGIC_OP);
				glLogicOp(GL_NOR);

				glPushMatrix();
				glTranslatef(x, y, 0);

				glScalef(zoomx, zoomy, 0);
				glMultMatrixf(sc->stabmat);

				glBegin(GL_LINE_LOOP);
					glVertex2f(0.f, 0.f);
					glVertex2f(ibuf->x, 0.f);
					glVertex2f(ibuf->x, ibuf->y);
					glVertex2f(0.f, ibuf->y);
				glEnd();

				glPopMatrix();

				glDisable(GL_COLOR_LOGIC_OP);
				glDisable(GL_LINE_STIPPLE);
			}
		}
	}

	/* reset zoom */
	glPixelZoom(1.f, 1.f);
}

static void draw_track_path(SpaceClip *sc, MovieClip *UNUSED(clip), MovieTrackingTrack *track)
{
	int count= sc->path_length;
	int i, a, b, curindex= -1;
	float path[102][2];
	int tiny= sc->flag&SC_SHOW_TINY_MARKER, framenr;
	MovieTrackingMarker *marker;

	if(count==0)
		return;

	marker= BKE_tracking_get_marker(track, sc->user.framenr);
	if(marker->framenr!=sc->user.framenr || marker->flag&MARKER_DISABLED)
		return;

	framenr= marker->framenr;

	a= count;
	i= framenr-1;
	while(i>=framenr-count) {
		marker= BKE_tracking_get_marker(track, i);

		if(!marker || marker->flag&MARKER_DISABLED)
			break;

		if(marker->framenr==i) {
			add_v2_v2v2(path[--a], marker->pos, track->offset);

			if(marker->framenr==sc->user.framenr)
				curindex= a;
		} else
			break;

		i--;
	}

	b= count;
	i= framenr;
	while(i<=framenr+count) {
		marker= BKE_tracking_get_marker(track, i);

		if(!marker || marker->flag&MARKER_DISABLED)
			break;

		if(marker->framenr==i) {
			if(marker->framenr==sc->user.framenr)
				curindex= b;

			add_v2_v2v2(path[b++], marker->pos, track->offset);
		} else
			break;

		i++;
	}

	if(!tiny) {
		UI_ThemeColor(TH_MARKER_OUTLINE);

		if(TRACK_VIEW_SELECTED(sc, track)) {
			glPointSize(5.0f);
			glBegin(GL_POINTS);
				for(i= a; i<b; i++) {
					if(i!=curindex)
						glVertex2f(path[i][0], path[i][1]);
				}
			glEnd();
		}

		glLineWidth(3.0f);
		glBegin(GL_LINE_STRIP);
			for(i= a; i<b; i++)
				glVertex2f(path[i][0], path[i][1]);
		glEnd();
		glLineWidth(1.0f);
	}

	UI_ThemeColor(TH_PATH_BEFORE);

	if(TRACK_VIEW_SELECTED(sc, track)) {
		glPointSize(3.0f);
		glBegin(GL_POINTS);
			for(i= a; i<b; i++) {
				if(i==count+1)
					UI_ThemeColor(TH_PATH_AFTER);

				if(i!=curindex)
					glVertex2f(path[i][0], path[i][1]);
			}
		glEnd();
	}

	UI_ThemeColor(TH_PATH_BEFORE);

	glBegin(GL_LINE_STRIP);
		for(i= a; i<b; i++) {
			if(i==count+1)
				UI_ThemeColor(TH_PATH_AFTER);

			glVertex2f(path[i][0], path[i][1]);
		}
	glEnd();
	glPointSize(1.0f);
}

static void draw_marker_outline(SpaceClip *sc, MovieTrackingTrack *track, MovieTrackingMarker *marker, int width, int height)
{
	int tiny= sc->flag&SC_SHOW_TINY_MARKER;
	int show_pat= 0;
	float px[2];

	UI_ThemeColor(TH_MARKER_OUTLINE);

	px[0]= 1.0f/width/sc->zoom;
	px[1]= 1.0f/height/sc->zoom;

	if((marker->flag&MARKER_DISABLED)==0) {
		float pos[2];
		rctf r;

		BLI_init_rctf(&r, track->pat_min[0], track->pat_max[0], track->pat_min[1], track->pat_max[1]);
		add_v2_v2v2(pos, marker->pos, track->offset);

		if(BLI_in_rctf(&r, pos[0]-marker->pos[0], pos[1]-marker->pos[1])) {
			if(tiny) glPointSize(3.0f);
			else glPointSize(4.0f);
			glBegin(GL_POINTS);
				glVertex2f(pos[0], pos[1]);
			glEnd();
			glPointSize(1.0f);
		} else {
			if(!tiny) glLineWidth(3.0f);
			glBegin(GL_LINES);
				glVertex2f(pos[0] + px[0]*2, pos[1]);
				glVertex2f(pos[0] + px[0]*8, pos[1]);

				glVertex2f(pos[0] - px[0]*2, pos[1]);
				glVertex2f(pos[0] - px[0]*8, pos[1]);

				glVertex2f(pos[0], pos[1] - px[1]*2);
				glVertex2f(pos[0], pos[1] - px[1]*8);

				glVertex2f(pos[0], pos[1] + px[1]*2);
				glVertex2f(pos[0], pos[1] + px[1]*8);
			glEnd();
			if(!tiny) glLineWidth(1.0f);
		}
	}

	/* pattern and search outline */
	glPushMatrix();
	glTranslatef(marker->pos[0], marker->pos[1], 0);

	if(!tiny) glLineWidth(3.0f);

	show_pat= ((marker->flag&MARKER_DISABLED)==0 || (sc->flag&SC_SHOW_MARKER_SEARCH)==0);
	if(sc->flag&SC_SHOW_MARKER_PATTERN && show_pat) {
		glBegin(GL_LINE_LOOP);
			glVertex2f(track->pat_min[0], track->pat_min[1]);
			glVertex2f(track->pat_max[0], track->pat_min[1]);
			glVertex2f(track->pat_max[0], track->pat_max[1]);
			glVertex2f(track->pat_min[0], track->pat_max[1]);
		glEnd();
	}

	if(sc->flag&SC_SHOW_MARKER_SEARCH) {
		glBegin(GL_LINE_LOOP);
			glVertex2f(track->search_min[0], track->search_min[1]);
			glVertex2f(track->search_max[0], track->search_min[1]);
			glVertex2f(track->search_max[0], track->search_max[1]);
			glVertex2f(track->search_min[0], track->search_max[1]);
		glEnd();
	}
	glPopMatrix();

	if(!tiny) glLineWidth(1.0f);
}

static void track_colors(MovieTrackingTrack *track, int act, float col[3], float scol[3])
{
	if(track->flag&TRACK_CUSTOMCOLOR) {
		if(act) UI_GetThemeColor3fv(TH_ACT_MARKER, scol);
		else copy_v3_v3(scol, track->color);

		mul_v3_v3fl(col, track->color, 0.5f);
	} else {
		UI_GetThemeColor3fv(TH_MARKER, col);

		if(act) UI_GetThemeColor3fv(TH_ACT_MARKER, scol);
		else UI_GetThemeColor3fv(TH_SEL_MARKER, scol);
	}
}

static void draw_marker_areas(SpaceClip *sc, MovieTrackingTrack *track, MovieTrackingMarker *marker, int width, int height, int act, int sel)
{
	int tiny= sc->flag&SC_SHOW_TINY_MARKER;
	int show_pat= 0;
	float col[3], scol[3], px[2];

	track_colors(track, act, col, scol);

	px[0]= 1.0f/width/sc->zoom;
	px[1]= 1.0f/height/sc->zoom;

	/* marker position and offset position */
	if((track->flag&SELECT)==sel && (marker->flag&MARKER_DISABLED)==0) {
		float pos[2];
		rctf r;

		if(track->flag&TRACK_LOCKED) {
			if(act) UI_ThemeColor(TH_ACT_MARKER);
			else if(track->flag&SELECT) UI_ThemeColorShade(TH_LOCK_MARKER, 64);
			else UI_ThemeColor(TH_LOCK_MARKER);
		} else {
			if(track->flag&SELECT) glColor3fv(scol);
			else glColor3fv(col);
		}

		add_v2_v2v2(pos, marker->pos, track->offset);

		BLI_init_rctf(&r, track->pat_min[0], track->pat_max[0], track->pat_min[1], track->pat_max[1]);
		add_v2_v2v2(pos, marker->pos, track->offset);

		if(BLI_in_rctf(&r, pos[0]-marker->pos[0], pos[1]-marker->pos[1])) {
			if(!tiny) glPointSize(2.0f);
			glBegin(GL_POINTS);
				glVertex2f(pos[0], pos[1]);
			glEnd();
			if(!tiny) glPointSize(1.0f);
		} else {
			glBegin(GL_LINES);
				glVertex2f(pos[0] + px[0]*3, pos[1]);
				glVertex2f(pos[0] + px[0]*7, pos[1]);

				glVertex2f(pos[0] - px[0]*3, pos[1]);
				glVertex2f(pos[0] - px[0]*7, pos[1]);

				glVertex2f(pos[0], pos[1] - px[1]*3);
				glVertex2f(pos[0], pos[1] - px[1]*7);

				glVertex2f(pos[0], pos[1] + px[1]*3);
				glVertex2f(pos[0], pos[1] + px[1]*7);
			glEnd();

			glColor3f(0.f, 0.f, 0.f);
			glLineStipple(3, 0xaaaa);
			glEnable(GL_LINE_STIPPLE);
			glEnable(GL_COLOR_LOGIC_OP);
			glLogicOp(GL_NOR);

			glBegin(GL_LINES);
				glVertex2fv(pos);
				glVertex2fv(marker->pos);
			glEnd();

			glDisable(GL_COLOR_LOGIC_OP);
			glDisable(GL_LINE_STIPPLE);
		}
	}

	/* pattern */
	glPushMatrix();
	glTranslatef(marker->pos[0], marker->pos[1], 0);

	if(tiny) {
		glLineStipple(3, 0xaaaa);
		glEnable(GL_LINE_STIPPLE);
	}

	show_pat= ((marker->flag&MARKER_DISABLED)==0 || (sc->flag&SC_SHOW_MARKER_SEARCH)==0);
	if((track->pat_flag&SELECT)==sel && show_pat) {
		if(track->flag&TRACK_LOCKED) {
			if(act) UI_ThemeColor(TH_ACT_MARKER);
			else if(track->pat_flag&SELECT) UI_ThemeColorShade(TH_LOCK_MARKER, 64);
			else UI_ThemeColor(TH_LOCK_MARKER);
		}
		else if(marker->flag&MARKER_DISABLED) {
			if(act) UI_ThemeColor(TH_ACT_MARKER);
			else if(track->pat_flag&SELECT) UI_ThemeColorShade(TH_DIS_MARKER, 128);
			else UI_ThemeColor(TH_DIS_MARKER);
		} else {
			if(track->pat_flag&SELECT) glColor3fv(scol);
			else glColor3fv(col);
		}

		if(sc->flag&SC_SHOW_MARKER_PATTERN) {
			glBegin(GL_LINE_LOOP);
				glVertex2f(track->pat_min[0], track->pat_min[1]);
				glVertex2f(track->pat_max[0], track->pat_min[1]);
				glVertex2f(track->pat_max[0], track->pat_max[1]);
				glVertex2f(track->pat_min[0], track->pat_max[1]);
			glEnd();
		}
	}

	/* search */
	if((track->search_flag&SELECT)==sel) {
		if(track->flag&TRACK_LOCKED) {
			if(act) UI_ThemeColor(TH_ACT_MARKER);
			else if(track->search_flag&SELECT) UI_ThemeColorShade(TH_LOCK_MARKER, 64);
			else UI_ThemeColor(TH_LOCK_MARKER);
		}
		else if(marker->flag&MARKER_DISABLED) {
			if(act) UI_ThemeColor(TH_ACT_MARKER);
			else if(track->search_flag&SELECT) UI_ThemeColorShade(TH_DIS_MARKER, 128);
			else UI_ThemeColor(TH_DIS_MARKER);
		} else {
			if(track->search_flag&SELECT) glColor3fv(scol);
			else glColor3fv(col);
		}

		if(sc->flag&SC_SHOW_MARKER_SEARCH) {
			glBegin(GL_LINE_LOOP);
				glVertex2f(track->search_min[0], track->search_min[1]);
				glVertex2f(track->search_max[0], track->search_min[1]);
				glVertex2f(track->search_max[0], track->search_max[1]);
				glVertex2f(track->search_min[0], track->search_max[1]);
			glEnd();
		}
	}

	if(tiny)
		glDisable(GL_LINE_STIPPLE);

	glPopMatrix();
}

static void draw_marker_slide_zones(SpaceClip *sc, MovieTrackingTrack *track, MovieTrackingMarker *marker, int outline, int sel, int act, int width, int height)
{
	float x, y, dx, dy, patdx, patdy, searchdx, searchdy, tdx, tdy;
	int tiny= sc->flag&SC_SHOW_TINY_MARKER;
	float col[3], scol[3], px[2];

	if((tiny && outline) || (marker->flag&MARKER_DISABLED))
		return;

	if(!TRACK_VIEW_SELECTED(sc, track) || track->flag&TRACK_LOCKED)
		return;

	track_colors(track, act, col, scol);

	if(outline) {
		glLineWidth(3.0f);
		UI_ThemeColor(TH_MARKER_OUTLINE);
	}

	glPushMatrix();
	glTranslatef(marker->pos[0], marker->pos[1], 0);

	dx= 6.0f/width/sc->zoom;
	dy= 6.0f/height/sc->zoom;

	patdx= MIN2(dx*2.f/3.f, (track->pat_max[0]-track->pat_min[0])/6.f);
	patdy= MIN2(dy*2.f/3.f, (track->pat_max[1]-track->pat_min[1])/6.f);

	searchdx= MIN2(dx, (track->search_max[0]-track->search_min[0])/6.f);
	searchdy= MIN2(dy, (track->search_max[1]-track->search_min[1])/6.f);

	px[0]= 1.0f/sc->zoom/width/sc->scale;
	px[1]= 1.0f/sc->zoom/height/sc->scale;

	if((sc->flag&SC_SHOW_MARKER_SEARCH) && ((track->search_flag&SELECT)==sel || outline)) {
		if(!outline) {
			if(track->search_flag&SELECT) glColor3fv(scol);
			else glColor3fv(col);
		}

		/* search offset square */
		x= track->search_min[0];
		y= track->search_max[1];

		tdx= searchdx;
		tdy= searchdy;

		if(outline) {
			tdx+= px[0];
			tdy+= px[1];
		}

		glBegin(GL_QUADS);
			glVertex3f(x-tdx, y+tdy, 0);
			glVertex3f(x+tdx, y+tdy, 0);
			glVertex3f(x+tdx, y-tdy, 0);
			glVertex3f(x-tdx, y-tdy, 0);
		glEnd();

		/* search resizing triangle */
		x= track->search_max[0];
		y= track->search_min[1];

		tdx= searchdx*2.f;
		tdy= searchdy*2.f;

		if(outline) {
			tdx+= px[0];
			tdy+= px[1];
		}

		glBegin(GL_TRIANGLES);
			glVertex3f(x, y, 0);
			glVertex3f(x-tdx, y, 0);
			glVertex3f(x, y+tdy, 0);
		glEnd();
	}

	if((sc->flag&SC_SHOW_MARKER_PATTERN) && ((track->pat_flag&SELECT)==sel || outline)) {
		if(!outline) {
			if(track->pat_flag&SELECT) glColor3fv(scol);
			else glColor3fv(col);
		}

		/* pattern offset square */
		x= track->pat_min[0];
		y= track->pat_max[1];

		tdx= patdx;
		tdy= patdy;

		if(outline) {
			tdx+= px[0];
			tdy+= px[1];
		}

		glBegin(GL_QUADS);
			glVertex3f(x-tdx, y+tdy, 0);
			glVertex3f(x+tdx, y+tdy, 0);
			glVertex3f(x+tdx, y-tdy, 0);
			glVertex3f(x-tdx, y-tdy, 0);
		glEnd();

		/* pattern resizing triangle */
		x= track->pat_max[0];
		y= track->pat_min[1];

		tdx= patdx*2.f;
		tdy= patdy*2.f;

		if(outline) {
			tdx+= px[0];
			tdy+= px[1];
		}

		glBegin(GL_TRIANGLES);
			glVertex3f(x, y, 0);
			glVertex3f(x-tdx, y, 0);
			glVertex3f(x, y+tdy, 0);
		glEnd();
	}

	glPopMatrix();

	if(outline)
		glLineWidth(1.0f);
}

static void draw_marker_texts(SpaceClip *sc, MovieTrackingTrack *track, MovieTrackingMarker *marker, int act,
			int width, int height, float zoomx, float zoomy)
{
	char str[128]= {0}, state[64]= {0};
	float x, y, dx= 0.f, dy= 0.f, fontsize;
	uiStyle *style= U.uistyles.first;
	int fontid= style->widget.uifont_id;

	if(!TRACK_VIEW_SELECTED(sc, track))
		return;

	BLF_size(fontid, 11.f, U.dpi);
	fontsize= BLF_height_max(fontid);

	if(marker->flag&MARKER_DISABLED) {
		if(act) UI_ThemeColor(TH_ACT_MARKER);
		else UI_ThemeColorShade(TH_DIS_MARKER, 128);
	} else {
		if(act) UI_ThemeColor(TH_ACT_MARKER);
		else UI_ThemeColor(TH_SEL_MARKER);
	}

	if(sc->flag&SC_SHOW_MARKER_SEARCH) {
		dx= track->search_min[0];
		dy= track->search_min[1];
	} else if(sc->flag&SC_SHOW_MARKER_PATTERN) {
		dx= track->pat_min[0];
		dy= track->pat_min[1];
	}

	x= (marker->pos[0]+dx)*width*sc->scale*zoomx+sc->loc[0]*zoomx;
	y= (marker->pos[1]+dy)*height*sc->scale*zoomy-fontsize+sc->loc[1]*zoomy;

	if(marker->flag&MARKER_DISABLED) strcpy(state, "disabled");
	else if(marker->framenr!=sc->user.framenr) strcpy(state, "estimated");
	else if(marker->flag&MARKER_TRACKED) strcpy(state, "tracked");
	else strcpy(state, "keyframed");

	if(state[0])
		BLI_snprintf(str, sizeof(str), "%s: %s", track->name, state);
	else
		BLI_snprintf(str, sizeof(str), "%s", track->name);

	BLF_position(fontid, x, y, 0.f);
	BLF_draw(fontid, str, strlen(str));
	y-= fontsize;

	if(track->flag&TRACK_HAS_BUNDLE) {
		BLI_snprintf(str, sizeof(str), "Average error: %.3f", track->error);
		BLF_position(fontid, x, y, 0.f);
		BLF_draw(fontid, str, strlen(str));
		y-= fontsize;
	}

	if(track->flag&TRACK_LOCKED) {
		BLF_position(fontid, x, y, 0.f);
		BLF_draw(fontid, "locked", 6);
	}
}

static void view2d_to_region_float(View2D *v2d, float x, float y, float *regionx, float *regiony)
{
	/* express given coordinates as proportional values */
	x= -v2d->cur.xmin / (v2d->cur.xmax-v2d->cur.xmin);
	y= -v2d->cur.ymin / (v2d->cur.ymax-v2d->cur.ymin);

	/* convert proportional distances to screen coordinates */
	*regionx= v2d->mask.xmin + x*(v2d->mask.xmax-v2d->mask.xmin);
	*regiony= v2d->mask.ymin + y*(v2d->mask.ymax-v2d->mask.ymin);
}

static void draw_tracking_tracks(SpaceClip *sc, ARegion *ar, MovieClip *clip,
			int width, int height, float zoomx, float zoomy)
{
	float x, y;
	MovieTracking* tracking= &clip->tracking;
	MovieTrackingMarker *marker;
	MovieTrackingTrack *track, *act_track;
	int framenr= sc->user.framenr;

	/* ** find window pixel coordinates of origin ** */

	/* UI_view2d_to_region_no_clip return integer values, this could
	   lead to 1px flickering when view is locked to selection during playbeck.
	   to avoid this flickering, calclate base point in the same way as it happens
	   in UI_view2d_to_region_no_clip, but do it in floats here */

	view2d_to_region_float(&ar->v2d, 0.0f, 0.0f, &x, &y);

	glPushMatrix();
	glTranslatef(x, y, 0);

	glPushMatrix();
	glScalef(zoomx, zoomy, 0);
	glMultMatrixf(sc->stabmat);
	glScalef(width, height, 0);

	act_track= clip->tracking.act_track;

	if(sc->flag&SC_SHOW_TRACK_PATH) {
		track= tracking->tracks.first;
		while(track) {
			if((track->flag&TRACK_HIDDEN)==0)
				draw_track_path(sc, clip, track);

			track= track->next;
		}
	}

	/* markers outline and non-selected areas */
	track= tracking->tracks.first;
	while(track) {
		if((track->flag&TRACK_HIDDEN)==0) {
			marker= BKE_tracking_get_marker(track, framenr);

			if(MARKER_VISIBLE(sc, marker)) {
				draw_marker_outline(sc, track, marker, width, height);
				draw_marker_areas(sc, track, marker, width, height, 0, 0);
				draw_marker_slide_zones(sc, track, marker, 1, 0, 0, width, height);
				draw_marker_slide_zones(sc, track, marker, 0, 0, 0, width, height);
			}
		}

		track= track->next;
	}

	/* selected areas only, so selection wouldn't be overlapped by
	   non-selected areas */
	track= tracking->tracks.first;
	while(track) {
		if((track->flag&TRACK_HIDDEN)==0) {
			int act= track==act_track;

			if(!act) {
				marker= BKE_tracking_get_marker(track, framenr);

				if(MARKER_VISIBLE(sc, marker)) {
					draw_marker_areas(sc, track, marker, width, height, 0, 1);
					draw_marker_slide_zones(sc, track, marker, 0, 1, 0, width, height);
				}
			}
		}

		track= track->next;
	}

	/* active marker would be displayed on top of everything else */
	if(act_track) {
		if((act_track->flag&TRACK_HIDDEN)==0) {
			marker= BKE_tracking_get_marker(act_track, framenr);

			if(MARKER_VISIBLE(sc, marker)) {
				draw_marker_areas(sc, act_track, marker, width, height, 1, 1);
				draw_marker_slide_zones(sc, act_track, marker, 0, 1, 1, width, height);
			}
		}
	}

	if(sc->flag&SC_SHOW_BUNDLES) {
		float pos[4], vec[4], mat[4][4], aspy;

		glEnable(GL_POINT_SMOOTH);
		glPointSize(3.0f);

		aspy= 1.f/clip->tracking.camera.pixel_aspect;
		BKE_tracking_projection_matrix(tracking, framenr, width, height*aspy, mat);

		track= tracking->tracks.first;
		while(track) {
			if((track->flag&TRACK_HIDDEN)==0 && track->flag&TRACK_HAS_BUNDLE) {
				marker= BKE_tracking_get_marker(track, framenr);

				if(MARKER_VISIBLE(sc, marker)) {
					copy_v4_v4(vec, track->bundle_pos);
					vec[3]=1;

					mul_v4_m4v4(pos, mat, vec);

					pos[0]= (pos[0]/(pos[3]*2.0f)+0.5f)*width;
					pos[1]= (pos[1]/(pos[3]*2.0f)+0.5f)*height;

					BKE_tracking_apply_intrinsics(tracking, pos, pos);

					vec[0]= (marker->pos[0]+track->offset[0])*width;
					vec[1]= (marker->pos[1]+track->offset[1])*height;
					sub_v2_v2(vec, pos);

					if(len_v2(vec)<3) glColor3f(0.0f, 1.0f, 0.0f);
					else glColor3f(1.0f, 0.0f, 0.0f);

					glBegin(GL_POINTS);
						glVertex3f(pos[0]/width, pos[1]/height, 0);
					glEnd();
				}
			}

			track= track->next;
		}

		glPointSize(1.0f);
		glDisable(GL_POINT_SMOOTH);
	}

	glPopMatrix();

	if(sc->flag&SC_SHOW_NAMES) {
		/* scaling should be cleared before drawing texts, otherwise font would also be scaled */
		track= tracking->tracks.first;
		while(track) {
			if((track->flag&TRACK_HIDDEN)==0) {
				marker= BKE_tracking_get_marker(track, framenr);

				if(MARKER_VISIBLE(sc, marker)) {
					int act= track==act_track;

					draw_marker_texts(sc, track, marker, act, width, height, zoomx, zoomy);
				}
			}

			track= track->next;
		}
	}

	glPopMatrix();
}

static void draw_distortion(SpaceClip *sc, ARegion *ar, MovieClip *clip, int width, int height, float zoomx, float zoomy)
{
	float x, y;
	const int n= 10;
	int i, j, a;
	float pos[2], tpos[2], grid[11][11][2];
	float dx= (float)width/n, dy= (float)height/n;
	MovieTracking *tracking= &clip->tracking;

	if(sc->mode!=SC_MODE_DISTORTION)
		return;

	if(!tracking->camera.focal)
		return;

	if((sc->flag&SC_SHOW_GRID)==0 && (sc->flag&SC_MANUAL_CALIBRATION)==0)
		return;

	view2d_to_region_float(&ar->v2d, 0.0f, 0.0f, &x, &y);

	glPushMatrix();
	glTranslatef(x, y, 0);
	glScalef(zoomx, zoomy, 0);
	glMultMatrixf(sc->stabmat);
	glScalef(width, height, 0);

	/* grid */
	if(sc->flag&SC_SHOW_GRID) {
		float val[4][2], idx[4][2];
		float min[2], max[2];

		for(a=0; a<4; a++) {
			if(a<2) val[a][a%2]= FLT_MAX;
			else val[a][a%2]= -FLT_MAX;
		}

		zero_v2(pos);
		for(i= 0; i<=n; i++) {
			for(j= 0; j<=n; j++) {
				if(i==0 || j==0 || i==n || j==n) {
					BKE_tracking_apply_intrinsics(tracking, pos, tpos);

					for(a=0; a<4; a++) {
						int ok;

						if(a<2) ok= tpos[a%2] < val[a][a%2];
						else ok= tpos[a%2] > val[a][a%2];

						if(ok) {
							copy_v2_v2(val[a], tpos);
							idx[a][0]= j;
							idx[a][1]= i;
						}
					}
				}

				pos[0]+= dx;
			}

			pos[0]= 0.f;
			pos[1]+= dy;
		}

		INIT_MINMAX2(min, max);

		for(a= 0; a<4; a++) {
			pos[0]= idx[a][0]*dx;
			pos[1]= idx[a][1]*dy;

			BKE_tracking_invert_intrinsics(tracking, pos, tpos);

			DO_MINMAX2(tpos, min, max);
		}

		copy_v2_v2(pos, min);
		dx= (max[0]-min[0])/n;
		dy= (max[1]-min[1])/n;

		for(i= 0; i<=n; i++) {
			for(j= 0; j<=n; j++) {
				BKE_tracking_apply_intrinsics(tracking, pos, grid[i][j]);

				grid[i][j][0]/= width;
				grid[i][j][1]/= height;

				pos[0]+= dx;
			}

			pos[0]= min[0];
			pos[1]+= dy;
		}

		glColor3f(1.f, 0.f, 0.f);

		for(i= 0; i<=n; i++) {
			glBegin(GL_LINE_STRIP);
				for(j= 0; j<=n; j++) {
					glVertex2fv(grid[i][j]);
				}
			glEnd();
		}

		for(j= 0; j<=n; j++) {
			glBegin(GL_LINE_STRIP);
				for(i= 0; i<=n; i++) {
					glVertex2fv(grid[i][j]);
				}
			glEnd();
		}
	}

	if(sc->flag&SC_MANUAL_CALIBRATION && clip->gpd) {
		bGPDlayer *layer= clip->gpd->layers.first;

		while(layer) {
			bGPDframe *frame= layer->frames.first;

			glColor4fv(layer->color);
			glLineWidth(layer->thickness);
			glPointSize((float)(layer->thickness + 2));

			while(frame) {
				bGPDstroke *stroke= frame->strokes.first;

				while(stroke) {
					if(stroke->flag&GP_STROKE_2DSPACE) {
						if(stroke->totpoints>1) {
							glBegin(GL_LINE_STRIP);
								for(i= 0; i<stroke->totpoints-1; i++) {
									float npos[2], dpos[2], len;
									int steps;

									pos[0]= stroke->points[i].x*width;
									pos[1]= stroke->points[i].y*height;

									npos[0]= stroke->points[i+1].x*width;
									npos[1]= stroke->points[i+1].y*height;

									len= len_v2v2(pos, npos);
									steps= ceil(len/5.f);

									/* we want to distort only long straight lines */
									if(stroke->totpoints==2) {
										BKE_tracking_invert_intrinsics(tracking, pos, pos);
										BKE_tracking_invert_intrinsics(tracking, npos, npos);
									}

									sub_v2_v2v2(dpos, npos, pos);
									mul_v2_fl(dpos, 1.f/steps);

									for(j= 0; j<steps; j++) {
										BKE_tracking_apply_intrinsics(tracking, pos, tpos);
										glVertex2f(tpos[0]/width, tpos[1]/height);

										add_v2_v2(pos, dpos);
									}
								}
							glEnd();
						}
						else if(stroke->totpoints==1) {
							glBegin(GL_POINTS);
								glVertex2f(stroke->points[0].x, stroke->points[0].y);
							glEnd();
						}
					}

					stroke= stroke->next;
				}

				frame= frame->next;
			}

			layer= layer->next;
		}

		glLineWidth(1.f);
		glPointSize(1.f);
	}

	glPopMatrix();
}

void draw_clip_main(SpaceClip *sc, ARegion *ar, Scene *scene)
{
	MovieClip *clip= ED_space_clip(sc);
	ImBuf *ibuf;
	int width, height;
	float zoomx, zoomy;

	/* if no clip, nothing to do */
	if(!clip)
		return;

	ED_space_clip_size(sc, &width, &height);
	ED_space_clip_zoom(sc, ar, &zoomx, &zoomy);

	if(sc->flag&SC_SHOW_STABLE) {
		ibuf= ED_space_clip_acquire_stable_buffer(sc, sc->loc, &sc->scale);
		BKE_tracking_stabdata_to_mat4(sc->loc, sc->scale, sc->stabmat);
	} else {
		ibuf= ED_space_clip_acquire_buffer(sc);

		zero_v2(sc->loc);
		sc->scale= 1.f;
		unit_m4(sc->stabmat);
	}

	if(ibuf) {
		draw_movieclip_buffer(sc, ar, ibuf, width, height, zoomx, zoomy);
		IMB_freeImBuf(ibuf);

		draw_tracking_tracks(sc, ar, clip, width, height, zoomx, zoomy);
		draw_distortion(sc, ar, clip, width, height, zoomx, zoomy);
	}

	draw_movieclip_cache(sc, ar, clip, scene);
}

/* draw grease pencil */
void draw_clip_grease_pencil(bContext *C, int onlyv2d)
{
	SpaceClip *sc= CTX_wm_space_clip(C);
	MovieClip *clip= ED_space_clip(sc);
	ImBuf *ibuf;

	if((sc->flag&SC_SHOW_GPENCIL)==0 || !clip)
		return;

	if(onlyv2d) {
		/* if manual calibration is used then grase pencil data is already
		    drawed in draw_distortion */
		if((sc->flag&SC_MANUAL_CALIBRATION)==0 || sc->mode!=SC_MODE_DISTORTION) {
			ibuf= ED_space_clip_acquire_buffer(sc);

			if(ibuf) {
				draw_gpencil_2dimage(C, ibuf);

				IMB_freeImBuf(ibuf);
			}
		}
	} else {
		draw_gpencil_view2d(C, 0);
	}
}
