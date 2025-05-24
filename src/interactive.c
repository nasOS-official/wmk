// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include "edges.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "regions.h"
#include "resize-indicator.h"
#include "view.h"
#include "window-rules.h"

/*
 *   pos_old  pos_cursor
 *      v         v
 *      +---------+-------------------+
 *      <-----------size_old---------->
 *
 *      return value
 *           v
 *           +----+---------+
 *           <---size_new--->
 */
static int
max_move_scale(double pos_cursor, double pos_old, double size_old,
		double size_new)
{
	double anchor_frac = (pos_cursor - pos_old) / size_old;
	int pos_new = pos_cursor - (size_new * anchor_frac);
	if (pos_new < pos_old) {
		/* Clamp by using the old offsets of the maximized window */
		pos_new = pos_old;
	}
	return pos_new;
}

void
interactive_anchor_to_cursor(struct server *server, struct wlr_box *geo)
{
	assert(server->input_mode == LAB_INPUT_STATE_MOVE);
	if (wlr_box_empty(geo)) {
		return;
	}
	/* Resize grab_box while anchoring it to grab_box.{x,y} */
	server->grab_box.x = max_move_scale(server->grab_x, server->grab_box.x,
		server->grab_box.width, geo->width);
	server->grab_box.y = max_move_scale(server->grab_y, server->grab_box.y,
		server->grab_box.height, geo->height);
	server->grab_box.width = geo->width;
	server->grab_box.height = geo->height;

	geo->x = server->grab_box.x + (server->seat.cursor->x - server->grab_x);
	geo->y = server->grab_box.y + (server->seat.cursor->y - server->grab_y);
}


enum view_edge
edge_from_cursor(struct seat *seat, struct output **dest_output)
{
	if (!view_is_floating(seat->server->grabbed_view)) {
		return VIEW_EDGE_INVALID;
	}

	int snap_range = rc.snap_edge_range;
	if (!snap_range) {
		return VIEW_EDGE_INVALID;
	}

	struct output *output = output_nearest_to_cursor(seat->server);
	if (!output_is_usable(output)) {
		wlr_log(WLR_ERROR, "output at cursor is unusable");
		return VIEW_EDGE_INVALID;
	}
	*dest_output = output;

	/* Translate into output local coordinates */
	double cursor_x = seat->cursor->x;
	double cursor_y = seat->cursor->y;
	wlr_output_layout_output_coords(seat->server->output_layout,
		output->wlr_output, &cursor_x, &cursor_y);

	struct wlr_box *area = &output->usable_area;
	if (cursor_x <= area->x + snap_range) {
		return VIEW_EDGE_LEFT;
	} else if (cursor_x >= area->x + area->width - snap_range) {
		return VIEW_EDGE_RIGHT;
	} else if (cursor_y <= area->y + snap_range) {
		if (rc.snap_top_maximize) {
			return VIEW_EDGE_CENTER;
		} else {
			return VIEW_EDGE_UP;
		}
	} else if (cursor_y >= area->y + area->height - snap_range) {
		return VIEW_EDGE_DOWN;
	} else {
		/* Not close to any edge */
		return VIEW_EDGE_INVALID;
	}
}



/*
 * Cancels interactive move/resize without changing the state of the of
 * the view in any way. This may leave the tiled state inconsistent with
 * the actual geometry of the view.
 */
void
interactive_cancel(struct view *view)
{
	if (view->server->grabbed_view != view) {
		return;
	}

	overlay_hide(&view->server->seat);


	view->server->grabbed_view = NULL;

	/* Restore keyboard/pointer focus */
	seat_focus_override_end(&view->server->seat);
}
