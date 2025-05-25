// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "buffer.h"
#include "common/font.h"
#include "common/graphic-helpers.h"
#include "common/list.h"
#include "common/mem.h"
#include "input/keyboard.h"
#include "labwc.h"
#include "protocols/cosmic-workspaces.h"
#include "protocols/ext-workspace.h"
#include "view.h"
#include "workspaces.h"
#include "xwayland.h"

#define COSMIC_WORKSPACES_VERSION 1
#define EXT_WORKSPACES_VERSION 1

/* Internal helpers */
static size_t
parse_workspace_index(const char *name)
{
	/*
	 * We only want to get positive numbers which span the whole string.
	 *
	 * More detailed requirement:
	 *  .---------------.--------------.
	 *  |     Input     | Return value |
	 *  |---------------+--------------|
	 *  | "2nd desktop" |      0       |
	 *  |    "-50"      |      0       |
	 *  |     "0"       |      0       |
	 *  |    "124"      |     124      |
	 *  |    "1.24"     |      0       |
	 *  `------------------------------´
	 *
	 * As atoi() happily parses any numbers until it hits a non-number we
	 * can't really use it for this case. Instead, we use strtol() combined
	 * with further checks for the endptr (remaining non-number characters)
	 * and returned negative numbers.
	 */
	long index;
	char *endptr;
	errno = 0;
	index = strtol(name, &endptr, 10);
	if (errno || *endptr != '\0' || index < 0) {
		return 0;
	}
	return index;
}

/* cosmic workspace handlers */
static void
handle_cosmic_workspace_activate(struct wl_listener *listener, void *data)
{
	struct workspace *workspace = wl_container_of(listener, workspace, on_cosmic.activate);
	workspaces_switch_to(workspace, /* update_focus */ true);
	wlr_log(WLR_INFO, "cosmic activating workspace %s", workspace->name);
}

/* ext workspace handlers */
static void
handle_ext_workspace_activate(struct wl_listener *listener, void *data)
{
	struct workspace *workspace = wl_container_of(listener, workspace, on_ext.activate);
	workspaces_switch_to(workspace, /* update_focus */ true);
	wlr_log(WLR_INFO, "ext activating workspace %s", workspace->name);
}

/* Internal API */
static void
add_workspace(struct server *server, const char *name)
{
	struct workspace *workspace = znew(*workspace);
	workspace->server = server;
	workspace->name = xstrdup(name);
	workspace->tree = wlr_scene_tree_create(server->view_tree);
	wl_list_append(&server->workspaces.all, &workspace->link);
	if (!server->workspaces.current) {
		server->workspaces.current = workspace;
	} else {
		wlr_scene_node_set_enabled(&workspace->tree->node, false);
	}

	bool active = server->workspaces.current == workspace;

	/* cosmic */
	workspace->cosmic_workspace = lab_cosmic_workspace_create(server->workspaces.cosmic_group);
	lab_cosmic_workspace_set_name(workspace->cosmic_workspace, name);
	lab_cosmic_workspace_set_active(workspace->cosmic_workspace, active);

	workspace->on_cosmic.activate.notify = handle_cosmic_workspace_activate;
	wl_signal_add(&workspace->cosmic_workspace->events.activate,
		&workspace->on_cosmic.activate);

	/* ext */
	workspace->ext_workspace = lab_ext_workspace_create(
		server->workspaces.ext_manager, /*id*/ NULL);
	lab_ext_workspace_assign_to_group(workspace->ext_workspace, server->workspaces.ext_group);
	lab_ext_workspace_set_name(workspace->ext_workspace, name);
	lab_ext_workspace_set_active(workspace->ext_workspace, active);

	workspace->on_ext.activate.notify = handle_ext_workspace_activate;
	wl_signal_add(&workspace->ext_workspace->events.activate,
		&workspace->on_ext.activate);
}

static struct workspace *
get_prev(struct workspace *current, struct wl_list *workspaces, bool wrap)
{
	struct wl_list *target_link = current->link.prev;
	if (target_link == workspaces) {
		/* Current workspace is the first one */
		if (!wrap) {
			return NULL;
		}
		/* Roll over */
		target_link = target_link->prev;
	}
	return wl_container_of(target_link, current, link);
}

static struct workspace *
get_next(struct workspace *current, struct wl_list *workspaces, bool wrap)
{
	struct wl_list *target_link = current->link.next;
	if (target_link == workspaces) {
		/* Current workspace is the last one */
		if (!wrap) {
			return NULL;
		}
		/* Roll over */
		target_link = target_link->next;
	}
	return wl_container_of(target_link, current, link);
}


/* Public API */
void
workspaces_init(struct server *server)
{
	server->workspaces.cosmic_manager = lab_cosmic_workspace_manager_create(
		server->wl_display, /* capabilities */ CW_CAP_WS_ACTIVATE,
		COSMIC_WORKSPACES_VERSION);

	server->workspaces.ext_manager = lab_ext_workspace_manager_create(
		server->wl_display, /* capabilities */ WS_CAP_WS_ACTIVATE,
		EXT_WORKSPACES_VERSION);

	server->workspaces.cosmic_group = lab_cosmic_workspace_group_create(
		server->workspaces.cosmic_manager);

	server->workspaces.ext_group = lab_ext_workspace_group_create(
		server->workspaces.ext_manager);

	wl_list_init(&server->workspaces.all);

	struct workspace *conf;
	wl_list_for_each(conf, &rc.workspace_config.workspaces, link) {
		add_workspace(server, conf->name);
	}
}

/*
 * update_focus should normally be set to true. It is set to false only
 * when this function is called from desktop_focus_view(), in order to
 * avoid unnecessary extra focus changes and possible recursion.
 */
void
workspaces_switch_to(struct workspace *target, bool update_focus)
{
	assert(target);
	struct server *server = target->server;
	if (target == server->workspaces.current) {
		return;
	}

	/* Disable the old workspace */
	wlr_scene_node_set_enabled(
		&server->workspaces.current->tree->node, false);

	lab_cosmic_workspace_set_active(
		server->workspaces.current->cosmic_workspace, false);
	lab_ext_workspace_set_active(
		server->workspaces.current->ext_workspace, false);

	/* Move Omnipresent views to new workspace */
	struct view *view;
	enum lab_view_criteria criteria =
		LAB_VIEW_CRITERIA_CURRENT_WORKSPACE;
	for_each_view_reverse(view, &server->views, criteria) {
		if (view->visible_on_all_workspaces) {
			view_move_to_workspace(view, target);
		}
	}

	/* Enable the new workspace */
	wlr_scene_node_set_enabled(&target->tree->node, true);

	/* Save the last visited workspace */
	server->workspaces.last = server->workspaces.current;

	/* Make sure new views will spawn on the new workspace */
	server->workspaces.current = target;

	/*
	 * Make sure we are focusing what the user sees. Only refocus if
	 * the focus is not already on an omnipresent or always-on-top view.
	 *
	 * TODO: Decouple always-on-top views from the omnipresent state.
	 *       One option for that would be to create a new scene tree
	 *       as child of every workspace tree and then reparent a-o-t
	 *       windows to that one. Combined with adjusting the condition
	 *       below that should take care of the issue.
	 */
	if (update_focus) {
		struct view *view = server->active_view;
		if (!view || (!view->visible_on_all_workspaces
				&& !view_is_always_on_top(view))) {
			desktop_focus_topmost_view(server);
		}
	}

	/* And finally show the OSD */
	/*
	 * Make sure we are not carrying around a
	 * cursor image from the previous desktop
	 */
	cursor_update_focus(server);

	/* Ensure that only currently visible fullscreen windows hide the top layer */
	desktop_update_top_layer_visibility(server);

	lab_cosmic_workspace_set_active(target->cosmic_workspace, true);
	lab_ext_workspace_set_active(target->ext_workspace, true);
}

void
workspaces_osd_hide(struct seat *seat)
{
	assert(seat);
	struct output *output;
	struct server *server = seat->server;
	wl_list_for_each(output, &server->outputs, link) {
		if (!output->workspace_osd) {
			continue;
		}
		wlr_scene_node_set_enabled(&output->workspace_osd->node, false);
		wlr_scene_buffer_set_buffer(output->workspace_osd, NULL);
	}
	seat->workspace_osd_shown_by_modifier = false;

	/* Update the cursor focus in case it was on top of the OSD before */
	cursor_update_focus(server);
}

struct workspace *
workspaces_find(struct workspace *anchor, const char *name, bool wrap)
{
	assert(anchor);
	if (!name) {
		return NULL;
	}
	size_t index = 0;
	struct workspace *target;
	size_t wants_index = parse_workspace_index(name);
	struct wl_list *workspaces = &anchor->server->workspaces.all;

	if (wants_index) {
		wl_list_for_each(target, workspaces, link) {
			if (wants_index == ++index) {
				return target;
			}
		}
	} else if (!strcasecmp(name, "current")) {
		return anchor;
	} else if (!strcasecmp(name, "last")) {
		return anchor->server->workspaces.last;
	} else if (!strcasecmp(name, "left")) {
		return get_prev(anchor, workspaces, wrap);
	} else if (!strcasecmp(name, "right")) {
		return get_next(anchor, workspaces, wrap);
	} else {
		wl_list_for_each(target, workspaces, link) {
			if (!strcasecmp(target->name, name)) {
				return target;
			}
		}
	}
	wlr_log(WLR_ERROR, "Workspace '%s' not found", name);
	return NULL;
}

static void
destroy_workspace(struct workspace *workspace)
{
	wlr_scene_node_destroy(&workspace->tree->node);
	zfree(workspace->name);
	wl_list_remove(&workspace->link);
	wl_list_remove(&workspace->on_cosmic.activate.link);
	wl_list_remove(&workspace->on_ext.activate.link);

	lab_cosmic_workspace_destroy(workspace->cosmic_workspace);
	lab_ext_workspace_destroy(workspace->ext_workspace);
	free(workspace);
}

void
workspaces_reconfigure(struct server *server)
{
	/*
	 * Compare actual workspace list with the new desired configuration to:
	 *   - Update names
	 *   - Add workspaces if more workspaces are desired
	 *   - Destroy workspaces if fewer workspace are desired
	 */

	struct wl_list *actual_workspace_link = server->workspaces.all.next;

	struct workspace *configured_workspace;
	wl_list_for_each(configured_workspace,
			&rc.workspace_config.workspaces, link) {
		struct workspace *actual_workspace = wl_container_of(
			actual_workspace_link, actual_workspace, link);

		if (actual_workspace_link == &server->workspaces.all) {
			/* # of configured workspaces increased */
			wlr_log(WLR_DEBUG, "Adding workspace \"%s\"",
				configured_workspace->name);
			add_workspace(server, configured_workspace->name);
			continue;
		}
		if (strcmp(actual_workspace->name, configured_workspace->name)) {
			/* Workspace is renamed */
			wlr_log(WLR_DEBUG, "Renaming workspace \"%s\" to \"%s\"",
				actual_workspace->name, configured_workspace->name);
			free(actual_workspace->name);
			actual_workspace->name = xstrdup(configured_workspace->name);
			lab_cosmic_workspace_set_name(
				actual_workspace->cosmic_workspace, actual_workspace->name);
			lab_ext_workspace_set_name(
				actual_workspace->ext_workspace, actual_workspace->name);
		}
		actual_workspace_link = actual_workspace_link->next;
	}

	if (actual_workspace_link == &server->workspaces.all) {
		return;
	}

	/* # of configured workspaces decreased */
	overlay_hide(&server->seat);
	struct workspace *first_workspace =
		wl_container_of(server->workspaces.all.next, first_workspace, link);

	while (actual_workspace_link != &server->workspaces.all) {
		struct workspace *actual_workspace = wl_container_of(
			actual_workspace_link, actual_workspace, link);

		wlr_log(WLR_DEBUG, "Destroying workspace \"%s\"",
			actual_workspace->name);

		struct view *view;
		wl_list_for_each(view, &server->views, link) {
			if (view->workspace == actual_workspace) {
				view_move_to_workspace(view, first_workspace);
			}
		}

		if (server->workspaces.current == actual_workspace) {
			workspaces_switch_to(first_workspace,
				/* update_focus */ true);
		}
		if (server->workspaces.last == actual_workspace) {
			server->workspaces.last = first_workspace;
		}

		actual_workspace_link = actual_workspace_link->next;
		destroy_workspace(actual_workspace);
	}
}

void
workspaces_destroy(struct server *server)
{
	struct workspace *workspace, *tmp;
	wl_list_for_each_safe(workspace, tmp, &server->workspaces.all, link) {
		destroy_workspace(workspace);
	}
	assert(wl_list_empty(&server->workspaces.all));
}
