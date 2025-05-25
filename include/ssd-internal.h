/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_SSD_INTERNAL_H
#define LABWC_SSD_INTERNAL_H

#include <wlr/util/box.h>
#include "common/macros.h"
#include "ssd.h"
#include "view.h"

#define FOR_EACH(tmp, ...) \
{ \
	__typeof__(tmp) _x[] = { __VA_ARGS__, NULL }; \
	size_t _i = 0; \
	for ((tmp) = _x[_i]; _i < ARRAY_SIZE(_x) - 1; (tmp) = _x[++_i])

#define FOR_EACH_END }

struct ssd_button {
	struct view *view;
	enum ssd_part_type type;
	/*
	 * Bitmap of lab_button_state that represents a combination of
	 * hover/toggled/rounded states.
	 */
	uint8_t state_set;
	/*
	 * Image buffers for each combination of hover/toggled/rounded states.
	 * img_buffers[state_set] is displayed. Some of these can be NULL
	 * (e.g. img_buffers[LAB_BS_ROUNDED] is set only for corner buttons).
	 *
	 * When "type" is LAB_SSD_BUTTON_WINDOW_ICON, these are all NULL and
	 * window_icon is used instead.
	 */
	struct scaled_img_buffer *img_buffers[LAB_BS_ALL + 1];

	struct scaled_icon_buffer *window_icon;

	struct wl_listener destroy;
};

struct ssd_sub_tree {
	struct wlr_scene_tree *tree;
	struct wl_list parts; /* ssd_part.link */
};

struct ssd_state_title_width {
	int width;
	bool truncated;
};

struct ssd {
	struct view *view;
	struct wlr_scene_tree *tree;

	/*
	 * Cache for current values.
	 * Used to detect actual changes so we
	 * don't update things we don't have to.
	 */
	struct {
		/* Button icons need to be swapped on shade or omnipresent toggles */
		bool was_shaded;
		bool was_omnipresent;

		/*
		 * Corners need to be (un)rounded and borders need be shown/hidden
		 * when toggling maximization, and the button needs to be swapped on
		 * maximization toggles.
		 */
		bool was_maximized;

		/*
		 * Corners need to be (un)rounded but borders should be kept shown when
		 * the window is (un)tiled and notified about it or when the window may
		 * become so small that only a squared scene-rect can be used to render
		 * such a small titlebar.
		 */
		bool was_squared;

		struct wlr_box geometry;
		struct ssd_state_title {
			char *text;
			struct ssd_state_title_width active;
			struct ssd_state_title_width inactive;
		} title;
	} state;

	/* An invisible area around the view which allows resizing */
	struct ssd_sub_tree extents;

	/* The top of the view, containing buttons, title, .. */
	struct {
		int height;
		struct wlr_scene_tree *tree;
		struct ssd_sub_tree active;
		struct ssd_sub_tree inactive;
	} titlebar;

	/* Borders allow resizing as well */
	struct {
		struct wlr_scene_tree *tree;
		struct ssd_sub_tree active;
		struct ssd_sub_tree inactive;
	} border;

	struct {
		struct wlr_scene_tree *tree;
		struct ssd_sub_tree active;
		struct ssd_sub_tree inactive;
	} shadow;

	/*
	 * Space between the extremities of the view's wlr_surface
	 * and the max extents of the server-side decorations.
	 * For xdg-shell views with CSD, this margin is zero.
	 */
	struct border margin;
};

struct ssd_part {
	enum ssd_part_type type;

	/* Buffer pointer. May be NULL */
	struct scaled_font_buffer *buffer;

	/* This part represented in scene graph */
	struct wlr_scene_node *node;

	struct wl_list link;
};

struct ssd_hover_state {
	struct view *view;
	struct ssd_button *button;
};

struct wlr_buffer;
struct wlr_scene_tree;


#endif /* LABWC_SSD_INTERNAL_H */
