// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <cairo.h>
#include <wlr/util/log.h>
#include <wlr/util/box.h>
#include "common/array.h"
#include "common/buf.h"
#include "common/font.h"
#include "common/macros.h"
#include "common/scaled-font-buffer.h"
#include "common/scaled-icon-buffer.h"
#include "common/scaled-rect-buffer.h"
#include "common/scene-helpers.h"
#include "common/string-helpers.h"
#include "config/rcxml.h"
#include "labwc.h"
#include "node.h"
#include "osd.h"
#include "theme.h"
#include "view.h"
#include "window-rules.h"
#include "workspaces.h"

struct osd_scene_item {
	struct view *view;
	struct wlr_scene_node *highlight_outline;
};

/*
 * Returns the view to select next in the window switcher.
 * If !start_view, the second focusable view is returned.
 */
static struct view *
get_next_cycle_view(struct server *server, struct view *start_view,
		enum lab_cycle_dir dir)
{
	struct view *(*iter)(struct wl_list *head, struct view *view,
		enum lab_view_criteria criteria);
	bool forwards = dir == LAB_CYCLE_DIR_FORWARD;
	iter = forwards ? view_next_no_head_stop : view_prev_no_head_stop;

	enum lab_view_criteria criteria = rc.window_switcher.criteria;

	/*
	 * Views are listed in stacking order, topmost first.  Usually the
	 * topmost view is already focused, so when iterating in the forward
	 * direction we pre-select the view second from the top:
	 *
	 *   View #1 (on top, currently focused)
	 *   View #2 (pre-selected)
	 *   View #3
	 *   ...
	 */
	if (!start_view && forwards) {
		start_view = iter(&server->views, NULL, criteria);
	}

	return iter(&server->views, start_view, criteria);
}

void
osd_on_view_destroy(struct view *view)
{
	assert(view);
	struct osd_state *osd_state = &view->server->osd_state;

	if (view->server->input_mode != LAB_INPUT_STATE_WINDOW_SWITCHER) {
		/* OSD not active, no need for clean up */
		return;
	}

	if (osd_state->cycle_view == view) {
		/*
		 * If we are the current OSD selected view, cycle
		 * to the next because we are dying.
		 */

		/* Also resets preview node */
		osd_state->cycle_view = get_next_cycle_view(view->server,
			osd_state->cycle_view, LAB_CYCLE_DIR_BACKWARD);

		/*
		 * If we cycled back to ourselves, then we have no more windows.
		 * Just close the OSD for good.
		 */
		if (osd_state->cycle_view == view || !osd_state->cycle_view) {
			/* osd_finish() additionally resets cycle_view to NULL */
			osd_finish(view->server);
		}
	}


	if (view->scene_tree) {
		struct wlr_scene_node *node = &view->scene_tree->node;
		if (osd_state->preview_anchor == node) {
			/*
			 * If we are the anchor for the current OSD selected view,
			 * replace the anchor with the node before us.
			 */
			osd_state->preview_anchor = lab_wlr_scene_get_prev_node(node);
		}
	}
}

static void
restore_preview_node(struct server *server)
{
	struct osd_state *osd_state = &server->osd_state;
	if (osd_state->preview_node) {
		wlr_scene_node_reparent(osd_state->preview_node,
			osd_state->preview_parent);

		if (osd_state->preview_anchor) {
			wlr_scene_node_place_above(osd_state->preview_node,
				osd_state->preview_anchor);
		} else {
			/* Selected view was the first node */
			wlr_scene_node_lower_to_bottom(osd_state->preview_node);
		}

		/* Node was disabled / minimized before, disable again */
		if (!osd_state->preview_was_enabled) {
			wlr_scene_node_set_enabled(osd_state->preview_node, false);
		}
		osd_state->preview_node = NULL;
		osd_state->preview_parent = NULL;
		osd_state->preview_anchor = NULL;
	}
}

void
osd_begin(struct server *server, enum lab_cycle_dir direction)
{
	if (server->input_mode != LAB_INPUT_STATE_PASSTHROUGH) {
		return;
	}

	server->osd_state.cycle_view = get_next_cycle_view(server,
		server->osd_state.cycle_view, direction);

	seat_focus_override_begin(&server->seat,
		LAB_INPUT_STATE_WINDOW_SWITCHER, LAB_CURSOR_DEFAULT);

	/* Update cursor, in case it is within the area covered by OSD */
	cursor_update_focus(server);
}

void
osd_cycle(struct server *server, enum lab_cycle_dir direction)
{
	assert(server->input_mode == LAB_INPUT_STATE_WINDOW_SWITCHER);

	server->osd_state.cycle_view = get_next_cycle_view(server,
		server->osd_state.cycle_view, direction);
}

void
osd_finish(struct server *server)
{
	restore_preview_node(server);
	seat_focus_override_end(&server->seat);

	server->osd_state.preview_node = NULL;
	server->osd_state.preview_anchor = NULL;
	server->osd_state.cycle_view = NULL;

	if (server->osd_state.preview_outline) {
		/* Destroy the whole multi_rect so we can easily react to new themes */
		wlr_scene_node_destroy(&server->osd_state.preview_outline->tree->node);
		server->osd_state.preview_outline = NULL;
	}

	/* Hiding OSD may need a cursor change */
	cursor_update_focus(server);
}