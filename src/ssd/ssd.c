// SPDX-License-Identifier: GPL-2.0-only

/*
 * Helpers for view server side decorations
 *
 * Copyright (C) Johan Malm 2020-2021
 */

#include <assert.h>
#include <strings.h>
#include "common/mem.h"
#include "common/scene-helpers.h"
#include "labwc.h"
#include "ssd-internal.h"
#include "theme.h"
#include "view.h"


/*
 * Resizing and mouse contexts like 'Left', 'TLCorner', etc. in the vicinity of
 * SSD borders, titlebars and extents can have effective "corner regions" that
 * behave differently from single-edge contexts.
 *
 * Corner regions are active whenever the cursor is within a prescribed size
 * (generally rc.resize_corner_range, but clipped to view size) of the view
 * bounds, so check the cursor against the view here.
 */
static enum ssd_part_type
get_resizing_type(const struct ssd *ssd, struct wlr_cursor *cursor)
{
	struct view *view = ssd ? ssd->view : NULL;
	if (!view || !cursor || !view->ssd_enabled || view->fullscreen) {
		return LAB_SSD_NONE;
	}

	struct wlr_box view_box = view->current;
	view_box.height = view_effective_height(view, /* use_pending */ false);

	if (!view->ssd_titlebar_hidden) {
		/* If the titlebar is visible, consider it part of the view */
		int titlebar_height = view->server->theme->titlebar_height;
		view_box.y -= titlebar_height;
		view_box.height += titlebar_height;
	}

	if (wlr_box_contains_point(&view_box, cursor->x, cursor->y)) {
		/* A cursor in bounds of the view is never in an SSD context */
		return LAB_SSD_NONE;
	}

	int corner_height = MAX(0, MIN(rc.resize_corner_range, view_box.height / 2));
	int corner_width = MAX(0, MIN(rc.resize_corner_range, view_box.width / 2));
	bool left = cursor->x < view_box.x + corner_width;
	bool right = cursor->x > view_box.x + view_box.width - corner_width;
	bool top = cursor->y < view_box.y + corner_height;
	bool bottom = cursor->y > view_box.y + view_box.height - corner_height;

	if (top && left) {
		return LAB_SSD_PART_CORNER_TOP_LEFT;
	} else if (top && right) {
		return LAB_SSD_PART_CORNER_TOP_RIGHT;
	} else if (bottom && left) {
		return LAB_SSD_PART_CORNER_BOTTOM_LEFT;
	} else if (bottom && right) {
		return LAB_SSD_PART_CORNER_BOTTOM_RIGHT;
	} else if (top) {
		return LAB_SSD_PART_TOP;
	} else if (bottom) {
		return LAB_SSD_PART_BOTTOM;
	} else if (left) {
		return LAB_SSD_PART_LEFT;
	} else if (right) {
		return LAB_SSD_PART_RIGHT;
	}

	return LAB_SSD_NONE;
}

enum ssd_part_type
ssd_get_part_type(const struct ssd *ssd, struct wlr_scene_node *node,
		struct wlr_cursor *cursor)
{
	if (!node) {
		return LAB_SSD_NONE;
	} else if (node->type == WLR_SCENE_NODE_BUFFER
			&& lab_wlr_surface_from_node(node)) {
		return LAB_SSD_CLIENT;
	} else if (!ssd) {
		return LAB_SSD_NONE;
	}

	const struct wl_list *part_list = NULL;
	struct wlr_scene_tree *grandparent =
		node->parent ? node->parent->node.parent : NULL;
	struct wlr_scene_tree *greatgrandparent =
		grandparent ? grandparent->node.parent : NULL;

	/* active titlebar */
	if (node->parent == ssd->titlebar.active.tree) {
		part_list = &ssd->titlebar.active.parts;
	} else if (grandparent == ssd->titlebar.active.tree) {
		part_list = &ssd->titlebar.active.parts;
	} else if (greatgrandparent == ssd->titlebar.active.tree) {
		part_list = &ssd->titlebar.active.parts;

	/* extents */
	// } else if (node->parent == ssd->extents.tree) {
	// 	part_list = &ssd->extents.parts;

	/* active border */
	} else if (node->parent == ssd->border.active.tree) {
		part_list = &ssd->border.active.parts;

	/* inactive titlebar */
	} else if (node->parent == ssd->titlebar.inactive.tree) {
		part_list = &ssd->titlebar.inactive.parts;
	} else if (grandparent == ssd->titlebar.inactive.tree) {
		part_list = &ssd->titlebar.inactive.parts;
	} else if (greatgrandparent == ssd->titlebar.inactive.tree) {
		part_list = &ssd->titlebar.inactive.parts;

	/* inactive border */
	} else if (node->parent == ssd->border.inactive.tree) {
		part_list = &ssd->border.inactive.parts;
	}

	enum ssd_part_type part_type = LAB_SSD_NONE;

	if (part_list) {
		struct ssd_part *part;
		wl_list_for_each(part, part_list, link) {
			if (node == part->node) {
				part_type = part->type;
				break;
			}
		}
	}

	if (part_type == LAB_SSD_NONE) {
		return part_type;
	}

	/* Perform cursor-based context checks */
	enum ssd_part_type resizing_type = get_resizing_type(ssd, cursor);
	return resizing_type != LAB_SSD_NONE ? resizing_type : part_type;
}

uint32_t
ssd_resize_edges(enum ssd_part_type type)
{
	switch (type) {
	case LAB_SSD_PART_TOP:
		return WLR_EDGE_TOP;
	case LAB_SSD_PART_RIGHT:
		return WLR_EDGE_RIGHT;
	case LAB_SSD_PART_BOTTOM:
		return WLR_EDGE_BOTTOM;
	case LAB_SSD_PART_LEFT:
		return WLR_EDGE_LEFT;
	case LAB_SSD_PART_CORNER_TOP_LEFT:
		return WLR_EDGE_TOP | WLR_EDGE_LEFT;
	case LAB_SSD_PART_CORNER_TOP_RIGHT:
		return WLR_EDGE_RIGHT | WLR_EDGE_TOP;
	case LAB_SSD_PART_CORNER_BOTTOM_RIGHT:
		return WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
	case LAB_SSD_PART_CORNER_BOTTOM_LEFT:
		return WLR_EDGE_BOTTOM | WLR_EDGE_LEFT;
	default:
		return WLR_EDGE_NONE;
	}
}

struct border
ssd_get_margin(const struct ssd *ssd)
{
	return (struct border){ 0 };
}



bool
ssd_part_contains(enum ssd_part_type whole, enum ssd_part_type candidate)
{
	if (whole == candidate || whole == LAB_SSD_ALL) {
		return true;
	}
	if (whole == LAB_SSD_BUTTON) {
		return candidate >= LAB_SSD_BUTTON_CLOSE
			&& candidate <= LAB_SSD_BUTTON_OMNIPRESENT;
	}
	if (whole == LAB_SSD_PART_TITLEBAR) {
		return candidate >= LAB_SSD_BUTTON_CLOSE
			&& candidate <= LAB_SSD_PART_TITLE;
	}
	if (whole == LAB_SSD_PART_TITLE) {
		/* "Title" includes blank areas of "Titlebar" as well */
		return candidate >= LAB_SSD_PART_TITLEBAR
			&& candidate <= LAB_SSD_PART_TITLE;
	}
	if (whole == LAB_SSD_FRAME) {
		return candidate >= LAB_SSD_BUTTON_CLOSE
			&& candidate <= LAB_SSD_CLIENT;
	}
	if (whole == LAB_SSD_PART_TOP) {
		return candidate == LAB_SSD_PART_CORNER_TOP_LEFT
			|| candidate == LAB_SSD_PART_CORNER_TOP_RIGHT;
	}
	if (whole == LAB_SSD_PART_RIGHT) {
		return candidate == LAB_SSD_PART_CORNER_TOP_RIGHT
			|| candidate == LAB_SSD_PART_CORNER_BOTTOM_RIGHT;
	}
	if (whole == LAB_SSD_PART_BOTTOM) {
		return candidate == LAB_SSD_PART_CORNER_BOTTOM_RIGHT
			|| candidate == LAB_SSD_PART_CORNER_BOTTOM_LEFT;
	}
	if (whole == LAB_SSD_PART_LEFT) {
		return candidate == LAB_SSD_PART_CORNER_TOP_LEFT
			|| candidate == LAB_SSD_PART_CORNER_BOTTOM_LEFT;
	}
	return false;
}

enum ssd_mode
ssd_mode_parse(const char *mode)
{
	if (!mode) {
		return LAB_SSD_MODE_INVALID;
	}
	if (!strcasecmp(mode, "none")) {
		return LAB_SSD_MODE_NONE;
	} else if (!strcasecmp(mode, "border")) {
		return LAB_SSD_MODE_BORDER;
	} else if (!strcasecmp(mode, "full")) {
		return LAB_SSD_MODE_FULL;
	} else {
		return LAB_SSD_MODE_INVALID;
	}
}

void
ssd_set_active(struct ssd *ssd, bool active)
{
	if (!ssd) {
		return;
	}
	wlr_scene_node_set_enabled(&ssd->border.active.tree->node, active);
	wlr_scene_node_set_enabled(&ssd->titlebar.active.tree->node, active);
	if (ssd->shadow.active.tree) {
		wlr_scene_node_set_enabled(
			&ssd->shadow.active.tree->node, active);
	}
	wlr_scene_node_set_enabled(&ssd->border.inactive.tree->node, !active);
	wlr_scene_node_set_enabled(&ssd->titlebar.inactive.tree->node, !active);
	if (ssd->shadow.inactive.tree) {
		wlr_scene_node_set_enabled(
			&ssd->shadow.inactive.tree->node, !active);
	}
}


struct ssd_hover_state *
ssd_hover_state_new(void)
{
	return znew(struct ssd_hover_state);
}

enum ssd_part_type
ssd_button_get_type(const struct ssd_button *button)
{
	return button ? button->type : LAB_SSD_NONE;
}

struct view *
ssd_button_get_view(const struct ssd_button *button)
{
	return button ? button->view : NULL;
}

bool
ssd_debug_is_root_node(const struct ssd *ssd, struct wlr_scene_node *node)
{
	if (!ssd || !node) {
		return false;
	}
	return node == &ssd->tree->node;
}

const char *
ssd_debug_get_node_name(const struct ssd *ssd, struct wlr_scene_node *node)
{
	if (!ssd || !node) {
		return NULL;
	}
	if (node == &ssd->tree->node) {
		return "view->ssd";
	}
	if (node == &ssd->titlebar.active.tree->node) {
		return "titlebar.active";
	}
	if (node == &ssd->titlebar.inactive.tree->node) {
		return "titlebar.inactive";
	}
	if (node == &ssd->border.active.tree->node) {
		return "border.active";
	}
	if (node == &ssd->border.inactive.tree->node) {
		return "border.inactive";
	}
	if (node == &ssd->extents.tree->node) {
		return "extents";
	}
	return NULL;
}
