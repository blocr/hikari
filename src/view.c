#include <hikari/view.h>

#include <assert.h>
#include <string.h>

#include <wlr/types/wlr_cursor.h>

#include <hikari/color.h>
#include <hikari/configuration.h>
#include <hikari/group.h>
#include <hikari/indicator.h>
#include <hikari/layout.h>
#include <hikari/mark.h>
#include <hikari/memory.h>
#include <hikari/operation.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/tile.h>
#include <hikari/workspace.h>
#include <hikari/xdg_view.h>
#include <hikari/xwayland_view.h>

static void
move_to_top(struct hikari_view *view)
{
  assert(view != NULL);

  wl_list_remove(&view->sheet_views);
  wl_list_insert(&view->sheet->views, &view->sheet_views);

  wl_list_remove(&view->group_views);
  wl_list_insert(&view->group->views, &view->group_views);

  wl_list_remove(&view->output_views);
  wl_list_insert(&view->output->views, &view->output_views);
}

static inline void
place_visibly_above(struct hikari_view *view)
{
  wl_list_remove(&view->visible_group_views);
  wl_list_insert(&view->group->visible_views, &view->visible_group_views);

  wl_list_remove(&view->group->visible_server_groups);
  wl_list_insert(
      &hikari_server.visible_groups, &view->group->visible_server_groups);

  wl_list_remove(&view->workspace_views);
  wl_list_insert(&view->sheet->workspace->views, &view->workspace_views);
}

static void
increase_group_visiblity(struct hikari_view *view)
{
  assert(!hikari_view_is_hidden(view));

  struct hikari_group *group = view->group;

  if (wl_list_empty(&group->visible_views)) {
    wl_list_insert(
        &hikari_server.visible_groups, &group->visible_server_groups);
  }

  wl_list_insert(&group->visible_views, &view->visible_group_views);
}

static void
decrease_group_visibility(struct hikari_view *view)
{
  struct hikari_group *group = view->group;

  wl_list_remove(&view->visible_group_views);

  if (wl_list_empty(&group->visible_views)) {
    wl_list_remove(&group->visible_server_groups);
  }
}

static void
detach_from_group(struct hikari_view *view)
{
  struct hikari_group *group = view->group;

  wl_list_remove(&view->group_views);

  if (wl_list_empty(&group->views)) {
    hikari_group_fini(group);
    hikari_free(group);
  }
}

static void
remove_from_group(struct hikari_view *view)
{
  assert(!hikari_view_is_hidden(view));
  assert(!hikari_group_is_sheet(view->group));

  decrease_group_visibility(view);
  detach_from_group(view);
}

static void
remove_from_sheet_group(struct hikari_view *view)
{
  assert(hikari_group_is_sheet(view->group));

  decrease_group_visibility(view);

  wl_list_remove(&view->group_views);
  wl_list_remove(&view->sheet_views);
}

static void
hide(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

#if !defined(NDEBUG)
  printf("HIDE %p\n", view);
#endif

  if (hikari_view_has_focus(view)) {
    hikari_indicator_damage(&hikari_server.indicator, view);
    hikari_workspace_focus_view(view->sheet->workspace, NULL);
  }

  wl_list_remove(&view->workspace_views);

  view->hide(view);

  decrease_group_visibility(view);

  hikari_view_set_hidden(view);
}

static void
migrate_to_workspace(
    struct hikari_view *view, struct hikari_workspace *workspace)
{
  hikari_view_damage_whole(view);
  if (hikari_view_has_focus(view)) {
    hikari_indicator_damage(&hikari_server.indicator, view);
    hikari_workspace_focus_view(view->sheet->workspace, NULL);
  }
  view->output = workspace->output;
}

static void
regroup_from_sheet_to_sheet(
    struct hikari_view *view, struct hikari_group *group)
{
  assert(hikari_group_is_sheet(view->group));
  assert(hikari_group_is_sheet(group));

  struct hikari_sheet *sheets = group->sheet->workspace->sheets;
  bool migrate = group->sheet->workspace->output != view->output;

  if (group->sheet == group->sheet->workspace->sheet ||
      group->sheet == &sheets[0]) {
    if (migrate) {
      migrate_to_workspace(view, group->sheet->workspace);
    }
    remove_from_sheet_group(view);

    view->sheet = group->sheet;
    wl_list_insert(&view->sheet->views, &view->sheet_views);

    view->group = group;
    wl_list_insert(&group->views, &view->group_views);

    increase_group_visiblity(view);

    wl_list_remove(&view->workspace_views);
    wl_list_insert(&group->sheet->workspace->views, &view->workspace_views);

    if (migrate) {
      hikari_view_center_cursor(view);
      hikari_server_cursor_focus();
    }
  } else {
    assert(view != NULL);

    hide(view);

    view->sheet = group->sheet;
    wl_list_remove(&view->sheet_views);
    wl_list_insert(&view->sheet->views, &view->sheet_views);

    view->group = group;
    wl_list_remove(&view->group_views);
    wl_list_insert(&view->group->views, &view->group_views);

    if (migrate) {
      view->output = group->sheet->workspace->output;
    }
  }
}

static void
regroup_from_sheet_to_group(
    struct hikari_view *view, struct hikari_group *group)
{
  assert(hikari_group_is_sheet(view->group));
  assert(!hikari_group_is_sheet(group));

  remove_from_sheet_group(view);

  wl_list_insert(&view->sheet->views, &view->sheet_views);

  view->group = group;
  wl_list_insert(&group->views, &view->group_views);

  increase_group_visiblity(view);

  wl_list_remove(&view->workspace_views);
  wl_list_insert(&view->sheet->workspace->views, &view->workspace_views);
}

static void
regroup_from_group_to_sheet(
    struct hikari_view *view, struct hikari_group *group)
{
  assert(!hikari_group_is_sheet(view->group));
  assert(hikari_group_is_sheet(group));

  struct hikari_sheet *sheets = group->sheet->workspace->sheets;
  bool migrate = group->sheet->workspace->output != view->output;

  if (group->sheet == group->sheet->workspace->sheet ||
      group->sheet == &sheets[0]) {
    if (migrate) {
      migrate_to_workspace(view, group->sheet->workspace);
    }
    remove_from_group(view);

    view->sheet = group->sheet;
    wl_list_remove(&view->sheet_views);
    wl_list_insert(&view->sheet->views, &view->sheet_views);

    view->group = group;
    wl_list_insert(&group->views, &view->group_views);

    increase_group_visiblity(view);

    wl_list_remove(&view->workspace_views);
    wl_list_insert(&view->sheet->workspace->views, &view->workspace_views);

    if (migrate) {
      hikari_view_center_cursor(view);
      hikari_server_cursor_focus();
    }
  } else {
    hide(view);

    detach_from_group(view);

    view->sheet = group->sheet;
    view->group = group;

    wl_list_remove(&view->sheet_views);
    wl_list_insert(&view->sheet->views, &view->sheet_views);

    wl_list_insert(&view->group->views, &view->group_views);

    if (migrate) {
      view->output = group->sheet->workspace->output;
    }
  }
}

static void
regroup_from_group_to_group(
    struct hikari_view *view, struct hikari_group *group)
{
  assert(!hikari_group_is_sheet(view->group));
  assert(!hikari_group_is_sheet(group));

  remove_from_group(view);

  wl_list_remove(&view->sheet_views);
  wl_list_insert(&view->sheet->views, &view->sheet_views);

  view->group = group;
  wl_list_insert(&group->views, &view->group_views);

  increase_group_visiblity(view);

  wl_list_remove(&view->workspace_views);
  wl_list_insert(&view->sheet->workspace->views, &view->workspace_views);
}

static void
regroup(struct hikari_view *view, struct hikari_group *group)
{
  assert(view != NULL);
  assert(group != NULL);

  if (hikari_group_is_sheet(view->group)) {
    if (hikari_group_is_sheet(group)) {
      regroup_from_sheet_to_sheet(view, group);
    } else {
      regroup_from_sheet_to_group(view, group);
    }
  } else {
    if (hikari_group_is_sheet(group)) {
      regroup_from_group_to_sheet(view, group);
    } else {
      regroup_from_group_to_group(view, group);
    }
  }
}

static inline void
refresh_border_geometry(struct hikari_view *view)
{
  assert(view != NULL);
  hikari_border_refresh_geometry(&view->border, view->current_geometry);
  hikari_indicator_frame_refresh_geometry(&view->indicator_frame, view);
}

static void
move_view(struct hikari_view *view, struct wlr_box *geometry, int x, int y)
{
  if (hikari_view_is_tiled(view)) {
    return;
  }

  if (view->maximized_state != NULL) {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        return;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        if (y != 0) {
          return;
        }
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        if (x != 0) {
          return;
        }
        break;
    }
  }

  int screen_width, screen_height;
  wlr_output_effective_resolution(
      view->output->output, &screen_width, &screen_height);

  int constrained_x = x;
  int constrained_y = y;
  int border = hikari_configuration->border;

  if (constrained_x != 0) {
    if (constrained_x > screen_width - 10) {
      constrained_x = screen_width - 10;
    } else if (constrained_x + geometry->width + border * 2 < 10) {
      constrained_x = -geometry->width - 2 * border + 10;
    }
  }

  if (constrained_y != 0) {
    if (geometry->y > screen_height - 10) {
      constrained_y = screen_height - 10;
    } else if (constrained_y + geometry->height + border * 2 < 10) {
      constrained_y = -geometry->height - 2 * border + 10;
    }
  }

  if (view->move != NULL) {
    view->move(view, constrained_x, constrained_y);
  }

  hikari_view_damage_whole(view);
  hikari_indicator_damage(&hikari_server.indicator, view);

  geometry->x = constrained_x;
  geometry->y = constrained_y;

  refresh_border_geometry(view);

  hikari_view_damage_whole(view);
  hikari_indicator_damage(&hikari_server.indicator, view);
}

void
hikari_view_init(struct hikari_view *view,
    enum hikari_view_type type,
    struct hikari_workspace *workspace)
{
#if !defined(NDEBUG)
  printf("VIEW INIT %p\n", view);
#endif
  view->type = type;
  view->flags = hikari_view_hidden_flag;
  view->border.state = HIKARI_BORDER_INACTIVE;
  view->sheet = NULL;
  view->mark = NULL;
  view->surface = NULL;
  view->maximized_state = NULL;
  view->sheet = NULL;
  view->output = NULL;
  view->group = NULL;
  view->title = NULL;
  view->tile = NULL;
  view->use_csd = false;
  view->current_geometry = &view->geometry;
  view->current_unmaximized_geometry = &view->geometry;

  hikari_view_unset_dirty(view);
  view->pending_operation.tile = NULL;

  wl_list_init(&view->children);
}

void
hikari_view_fini(struct hikari_view *view)
{
  assert(hikari_view_is_hidden(view));

#if !defined(NDEBUG)
  printf("DESTROY VIEW %p\n", view);
#endif

  hikari_free(view->title);

  if (view->group != NULL) {
    if (hikari_group_is_sheet(view->group)) {
      wl_list_remove(&view->group_views);
    } else {
      detach_from_group(view);
    }
  }

  if (view->sheet != NULL) {
    wl_list_remove(&view->sheet_views);
    wl_list_remove(&view->output_views);
  }

  if (view->maximized_state != NULL) {
    hikari_maximized_state_destroy(view->maximized_state);
  }

  if (view->mark != NULL) {
    hikari_mark_clear(view->mark);
  }

  if (view->tile != NULL) {
    struct hikari_tile *tile = view->tile;

    view->tile = NULL;

    hikari_tile_fini(tile);
    hikari_free(tile);
  }

  if (view->pending_operation.tile != NULL) {
    hikari_tile_fini(view->pending_operation.tile);
    hikari_free(view->pending_operation.tile);
  }
}

void
hikari_view_set_title(struct hikari_view *view, const char *title)
{
  hikari_free(view->title);

  if (title != NULL) {
    view->title = hikari_malloc(strlen(title) + 1);
    strcpy(view->title, title);

    if (hikari_server.workspace->focus_view == view) {
      assert(!hikari_view_is_hidden(view));
      struct wlr_box *geometry = hikari_view_geometry(view);
      struct hikari_output *output = view->output;
      hikari_indicator_update_title(&hikari_server.indicator,
          geometry,
          output,
          title,
          hikari_configuration->indicator_selected);
    }
  } else {
    view->title = NULL;
  }
}

struct damage_data_t {
  struct wlr_box *geometry;
  struct hikari_output *output;
  struct wlr_surface *surface;
  struct hikari_view *view;

  bool whole;
};

static void
damage_whole_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  struct damage_data_t *damage_data = data;
  struct hikari_view *view = damage_data->view;
  struct hikari_output *output = damage_data->output;

  if (view->surface == surface) {
    hikari_output_add_damage(output, hikari_view_border_geometry(view));
  } else {
    struct wlr_box geometry = *damage_data->geometry;

    geometry.x += sx;
    geometry.y += sy;

    geometry.width = surface->current.width;
    geometry.height = surface->current.height;

    hikari_output_add_damage(output, &geometry);
  }
}

void
hikari_view_damage_whole(struct hikari_view *view)
{
  assert(view != NULL);

  // TODO I know, this needs to be done A LOT better
  if (view->use_csd) {
    hikari_output_damage_whole(view->output);
    return;
  }

  struct wlr_box geometry = *hikari_view_geometry(view);
  struct hikari_output *output = view->output;
  struct damage_data_t damage_data;

  damage_data.geometry = &geometry;
  damage_data.output = output;
  damage_data.view = view;

  hikari_view_interface_for_each_surface(
      (struct hikari_view_interface *)view, damage_whole_surface, &damage_data);
}

static struct wlr_box *
refresh_unmaximized_geometry(struct hikari_view *view)
{
  assert(view != NULL);

  if (view->tile != NULL) {
    return &view->tile->view_geometry;
  } else {
    return &view->geometry;
  }
}

static struct wlr_box *
refresh_geometry(struct hikari_view *view)
{
  assert(view != NULL);

  if (view->maximized_state != NULL) {
    return &view->maximized_state->geometry;
  } else {
    return refresh_unmaximized_geometry(view);
  }
}

static void
queue_resize(struct hikari_view *view, struct hikari_operation *op)
{
  uint32_t serial = view->resize(view, op->geometry.width, op->geometry.height);

  op->type = HIKARI_OPERATION_TYPE_RESIZE;
  op->serial = serial;

  hikari_view_set_dirty(view);
}

static void
resize(struct hikari_view *view,
    int requested_width,
    int requested_height,
    bool center)
{
  assert(view->maximized_state == NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);
  struct hikari_operation *op = &view->pending_operation;

  int min_width;
  int min_height;
  int max_width;
  int max_height;
  view->constraints(view, &min_width, &min_height, &max_width, &max_height);

  int new_width;
  int new_height;
  if (requested_width > max_width) {
    new_width = max_width;
  } else if (requested_width < min_width) {
    new_width = min_width;
  } else {
    new_width = requested_width;
  }

  if (requested_height > max_height) {
    new_height = max_height;
  } else if (requested_height < min_height) {
    new_height = min_height;
  } else {
    new_height = requested_height;
  }

  if (new_height == geometry->height && new_width == geometry->width) {
    return;
  }

  op->geometry.x = geometry->x;
  op->geometry.y = geometry->y;
  op->geometry.width = new_width;
  op->geometry.height = new_height;
  op->center = center;

  queue_resize(view, op);
}

void
hikari_view_resize_absolute(struct hikari_view *view, int width, int height)
{
  assert(view != NULL);
  assert(view->resize != NULL);
  assert(view->constraints != NULL);

  if (view->maximized_state != NULL || hikari_view_is_dirty(view)) {
    return;
  }

  resize(view, width, height, false);
}

void
hikari_view_resize(struct hikari_view *view, int width, int height)
{
  assert(view != NULL);
  assert(view->resize != NULL);
  assert(view->constraints != NULL);

  if (view->maximized_state != NULL || hikari_view_is_dirty(view)) {
    return;
  }

  struct wlr_box *geometry = hikari_view_geometry(view);

  int requested_width = geometry->width + width;
  int requested_height = geometry->height + height;

  resize(view, requested_width, requested_height, true);
}

void
hikari_view_move(struct hikari_view *view, int x, int y)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);

  move_view(view, geometry, geometry->x + x, geometry->y + y);
}

void
hikari_view_move_absolute(struct hikari_view *view, int x, int y)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);

  move_view(view, geometry, x, y);
}

void
hikari_view_manage(struct hikari_view *view,
    struct hikari_sheet *sheet,
    struct hikari_group *group)
{
#if !defined(NDEBUG)
  printf("VIEW MANAGE %p\n", view);
#endif

  assert(view != NULL);
  assert(sheet != NULL);
  assert(group != NULL);

  view->sheet = sheet;
  view->output = sheet->workspace->output;
  view->group = group;

  wl_list_insert(&sheet->views, &view->sheet_views);
  wl_list_insert(&group->views, &view->group_views);
  wl_list_insert(&view->output->views, &view->output_views);
}

void
hikari_view_show(struct hikari_view *view)
{
  assert(view != NULL);
  assert(hikari_view_is_hidden(view));
#if !defined(NDEBUG)
  printf("SHOW %p\n", view);
#endif

  wl_list_insert(&view->sheet->workspace->views, &view->workspace_views);

  view->show(view);

  hikari_view_unset_hidden(view);
  increase_group_visiblity(view);
  hikari_view_damage_whole(view);
}

void
hikari_view_hide(struct hikari_view *view)
{
  hide(view);
  hikari_view_damage_whole(view);
}

static void
raise_view(struct hikari_view *view)
{
  assert(view != NULL);

  if (view == hikari_workspace_first_view(view->sheet->workspace)) {
    return;
  }

  move_to_top(view);
  place_visibly_above(view);
}

void
hikari_view_raise(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  raise_view(view);
  hikari_view_damage_whole(view);
}

void
hikari_view_raise_hidden(struct hikari_view *view)
{
  assert(view != NULL);
  assert(hikari_view_is_hidden(view));

  move_to_top(view);
}

void
hikari_view_lower(struct hikari_view *view)
{
  assert(view != NULL);

  if (view == hikari_workspace_last_view(view->sheet->workspace)) {
    return;
  }

  wl_list_remove(&(view->sheet_views));
  wl_list_insert(view->sheet->views.prev, &(view->sheet_views));

  wl_list_remove(&view->group_views);
  wl_list_insert(view->group->views.prev, &view->group_views);

  wl_list_remove(&view->output_views);
  wl_list_insert(view->output->views.prev, &view->output_views);

  wl_list_remove(&view->visible_group_views);
  wl_list_insert(view->group->visible_views.prev, &view->visible_group_views);

  wl_list_remove(&view->group->visible_server_groups);
  wl_list_insert(
      hikari_server.visible_groups.prev, &view->group->visible_server_groups);

  wl_list_remove(&view->workspace_views);
  wl_list_insert(view->sheet->workspace->views.prev, &view->workspace_views);

  hikari_view_damage_whole(view);
}

static void
tile_view(struct hikari_view *view,
    struct hikari_layout *layout,
    struct hikari_tile *tile)
{
  assert(!hikari_view_is_dirty(view));

  struct hikari_operation *op = &view->pending_operation;

  struct wlr_box *current_geometry = hikari_view_geometry(view);

  op->type = HIKARI_OPERATION_TYPE_TILE;
  op->tile = tile;
  op->geometry = tile->view_geometry;

  hikari_view_set_dirty(view);

  if (current_geometry->width != op->geometry.width ||
      current_geometry->height != op->geometry.height) {
    if (view->move_resize != NULL) {
      op->serial = 0;
      view->move_resize(view,
          op->geometry.x,
          op->geometry.y,
          op->geometry.width,
          op->geometry.height);
    } else {
      op->serial = view->resize(view, op->geometry.width, op->geometry.height);
    }
  } else {
    hikari_view_commit_pending_operation(view);
  }
}

void
hikari_view_tile(struct hikari_view *view, struct wlr_box *geometry)
{
  assert(!hikari_view_is_dirty(view));
  assert(hikari_view_is_tileable(view));

  struct hikari_layout *layout = view->sheet->workspace->sheet->layout;

  struct hikari_tile *tile = hikari_malloc(sizeof(struct hikari_tile));
  hikari_tile_init(tile, view, geometry, geometry);

  tile_view(view, layout, tile);

  wl_list_insert(layout->tiles.prev, &tile->layout_tiles);
}

static void
queue_full_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;

  int width, height;
  wlr_output_effective_resolution(view->output->output, &width, &height);

  op->type = HIKARI_OPERATION_TYPE_FULL_MAXIMIZE;
  op->geometry.x = 0;
  op->geometry.y = 0;
  op->geometry.width = width;
  op->geometry.height = height;

  if (view->move_resize != NULL) {
    op->serial = 0;
    view->move_resize(view,
        op->geometry.x,
        op->geometry.y,
        op->geometry.width,
        op->geometry.height);
  } else {
    op->serial = view->resize(view, op->geometry.width, op->geometry.height);
  }
}

static void
queue_unmaximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;

  op->type = HIKARI_OPERATION_TYPE_UNMAXIMIZE;
  if (view->tile != NULL) {
    op->geometry = view->tile->view_geometry;
  } else {
    op->geometry = view->geometry;
  }

  if (view->move_resize != NULL) {
    op->serial = 0;
    view->move_resize(view,
        op->geometry.x,
        op->geometry.y,
        op->geometry.width,
        op->geometry.height);
  } else {
    op->serial = view->resize(view, op->geometry.width, op->geometry.height);
  }
}

void
hikari_view_toggle_full_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (hikari_view_is_dirty(view)) {
    return;
  }

  if (hikari_view_is_fully_maximized(view)) {
    queue_unmaximize(view);
  } else {
    queue_full_maximize(view);
  }

  hikari_view_set_dirty(view);
}

static void
queue_horizontal_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;

  int width, height;
  wlr_output_effective_resolution(view->output->output, &width, &height);

  struct wlr_box *geometry = view->current_unmaximized_geometry;

  op->type = HIKARI_OPERATION_TYPE_HORIZONTAL_MAXIMIZE;
  op->geometry.x = 0;
  op->geometry.y = geometry->y;
  op->geometry.height = geometry->height;
  op->geometry.width = width;

  if (view->move_resize != NULL) {
    op->serial = 0;
    view->move_resize(view,
        op->geometry.x,
        op->geometry.y,
        op->geometry.width,
        op->geometry.height);
  } else {
    op->serial = view->resize(view, op->geometry.width, op->geometry.height);
  }
}

static void
queue_vertical_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  struct hikari_operation *op = &view->pending_operation;

  int width, height;
  wlr_output_effective_resolution(view->output->output, &width, &height);

  struct wlr_box *geometry = view->current_unmaximized_geometry;

  op->type = HIKARI_OPERATION_TYPE_VERTICAL_MAXIMIZE;
  op->geometry.x = geometry->x;
  op->geometry.y = 0;
  op->geometry.height = height;
  op->geometry.width = geometry->width;

  if (view->move_resize != NULL) {
    op->serial = 0;
    view->move_resize(view,
        op->geometry.x,
        op->geometry.y,
        op->geometry.width,
        op->geometry.height);
  } else {
    op->serial = view->resize(view, op->geometry.width, op->geometry.height);
  }
}

void
hikari_view_toggle_vertical_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (hikari_view_is_dirty(view)) {
    return;
  }

  if (view->maximized_state != NULL) {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        queue_horizontal_maximize(view);
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        queue_unmaximize(view);
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        queue_full_maximize(view);
        break;
    }
  } else {
    queue_vertical_maximize(view);
  }

  hikari_view_set_dirty(view);
}

void
hikari_view_toggle_horizontal_maximize(struct hikari_view *view)
{
  assert(view != NULL);
  assert(!hikari_view_is_hidden(view));

  if (view->maximized_state != NULL) {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        queue_vertical_maximize(view);
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        queue_full_maximize(view);
        break;

      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        queue_unmaximize(view);
        break;
    }
  } else {
    queue_horizontal_maximize(view);
  }

  hikari_view_set_dirty(view);
}

void
hikari_view_toggle_floating(struct hikari_view *view)
{
  if (!hikari_view_is_floating(view)) {
    if (hikari_view_is_tiled(view)) {
      hikari_view_reset_geometry(view);
    }
    hikari_view_set_floating(view);
  } else {
    hikari_view_unset_floating(view);
  }
}

void
hikari_view_reset_geometry(struct hikari_view *view)
{
  uint32_t serial =
      view->resize(view, view->geometry.width, view->geometry.height);

  struct hikari_operation *op = &view->pending_operation;

  op->type = HIKARI_OPERATION_TYPE_RESET;
  op->serial = serial;
  op->geometry = view->geometry;

  if (view->move_resize != NULL) {
    view->move_resize(view,
        op->geometry.x,
        op->geometry.y,
        op->geometry.width,
        op->geometry.height);
  }

  hikari_view_set_dirty(view);
}

void
hikari_view_pin_to_sheet(struct hikari_view *view, struct hikari_sheet *sheet)
{
  assert(view != NULL);
  assert(sheet != NULL);
  assert(!hikari_view_is_hidden(view));

  if (view->sheet == sheet) {
    if (view->sheet->workspace->sheet != sheet && sheet->nr != 0) {
      hide(view);
      view->sheet = sheet;
      move_to_top(view);
    } else {
      raise_view(view);
    }
  } else {
    if (view->sheet->group == view->group) {
      regroup(view, sheet->group);
    } else {
      if (view->sheet->workspace->sheet != sheet && sheet->nr != 0) {
        hide(view);
      } else {
        place_visibly_above(view);
      }

      view->sheet = sheet;
      move_to_top(view);
    }
  }

  hikari_view_damage_whole(view);
}

void
hikari_view_center_cursor(struct hikari_view *view)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);
  struct hikari_output *output = view->output;

  int x = output->geometry.x + geometry->x + geometry->width / 2;
  int y = output->geometry.y + geometry->y + geometry->height / 2;

  wlr_cursor_warp(hikari_server.cursor, NULL, x, y);
}

void
hikari_view_top_left_cursor(struct hikari_view *view)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);
  struct hikari_output *output = view->output;

  int x = output->geometry.x + geometry->x;
  int y = output->geometry.y + geometry->y;

  wlr_cursor_warp(hikari_server.cursor, NULL, x, y);
}

void
hikari_view_bottom_right_cursor(struct hikari_view *view)
{
  assert(view != NULL);

  struct wlr_box *geometry = hikari_view_geometry(view);
  struct hikari_output *output = view->output;

  int x = output->geometry.x + geometry->x + geometry->width;
  int y = output->geometry.y + geometry->y + geometry->height;

  wlr_cursor_warp(hikari_server.cursor, NULL, x, y);
}

void
hikari_view_toggle_iconified(struct hikari_view *view)
{
  if (hikari_view_is_iconified(view)) {
    hikari_view_unset_iconified(view);
  } else {
    if (hikari_view_is_tiled(view)) {
      hikari_view_reset_geometry(view);
    }
    hikari_view_set_iconified(view);
  }
}

void
hikari_view_group(struct hikari_view *view, struct hikari_group *group)
{
  assert(view != NULL);
  assert(group != NULL);
  assert(view->group != NULL);

  if (view->group == group) {
    return;
  }

  regroup(view, group);
  hikari_view_damage_whole(view);
}

void
hikari_view_exchange(struct hikari_view *from, struct hikari_view *to)
{
  assert(from != NULL);
  assert(from->tile != NULL);
  assert(to != NULL);
  assert(to->tile != NULL);
  assert(to->tile->view->sheet == from->tile->view->sheet);
  assert(!hikari_view_is_dirty(from));
  assert(!hikari_view_is_dirty(to));

  struct hikari_layout *layout = from->sheet->workspace->sheet->layout;

  struct wlr_box *from_geometry = hikari_view_geometry(from);
  struct wlr_box *to_geometry = hikari_view_geometry(to);

  struct hikari_tile *from_tile = hikari_malloc(sizeof(struct hikari_tile));
  struct hikari_tile *to_tile = hikari_malloc(sizeof(struct hikari_tile));

  hikari_tile_init(from_tile, from, to_geometry, to_geometry);
  hikari_tile_init(to_tile, to, from_geometry, from_geometry);

  wl_list_insert(&from->tile->layout_tiles, &to_tile->layout_tiles);
  wl_list_insert(&to->tile->layout_tiles, &from_tile->layout_tiles);

  tile_view(from, layout, from_tile);
  tile_view(to, layout, to_tile);
}

static void
destroy_subsurface_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_subsurface *view_subsurface =
      wl_container_of(listener, view_subsurface, destroy);

  hikari_view_subsurface_fini(view_subsurface);

  hikari_free(view_subsurface);
}

void
hikari_view_subsurface_init(struct hikari_view_subsurface *view_subsurface,
    struct hikari_view *parent,
    struct wlr_subsurface *subsurface)
{
  view_subsurface->subsurface = subsurface;

  view_subsurface->destroy.notify = destroy_subsurface_handler;
  wl_signal_add(
      &subsurface->surface->events.destroy, &view_subsurface->destroy);

  hikari_view_child_init(
      (struct hikari_view_child *)view_subsurface, parent, subsurface->surface);
}

void
hikari_view_child_fini(struct hikari_view_child *view_child)
{
  wl_list_remove(&view_child->commit.link);
  wl_list_remove(&view_child->new_subsurface.link);
}

void
hikari_view_subsurface_fini(struct hikari_view_subsurface *view_subsurface)
{
  hikari_view_child_fini(&view_subsurface->view_child);
  wl_list_remove(&view_subsurface->destroy.link);
}

static void
new_subsurface_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_child *view_child =
      wl_container_of(listener, view_child, new_subsurface);

  struct wlr_subsurface *wlr_subsurface = data;

  struct hikari_view_subsurface *view_subsurface =
      hikari_malloc(sizeof(struct hikari_view_subsurface));

  hikari_view_subsurface_init(
      view_subsurface, view_child->parent, wlr_subsurface);
}

static void
damage_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  struct damage_data_t *damage_data = data;

  if (damage_data->whole) {
    damage_whole_surface(surface, sx, sy, data);
  } else {
    struct wlr_box *geometry = damage_data->geometry;
    struct hikari_output *output = damage_data->output;

    pixman_region32_t damage;
    pixman_region32_init(&damage);

    wlr_surface_get_effective_damage(surface, &damage);
    if (pixman_region32_not_empty(&damage)) {
      pixman_region32_translate(&damage, geometry->x + sx, geometry->y + sy);
      wlr_output_damage_add(output->damage, &damage);
    } else {
      damage_whole_surface(surface, sx, sy, data);
    }

    pixman_region32_fini(&damage);
  }
}

static void
damage_single_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  struct damage_data_t *damage_data = data;

  if (damage_data->surface == surface) {
    damage_surface(surface, sx, sy, data);
  }
}

static void
commit_view_child_handler(struct wl_listener *listener, void *data)
{
  struct hikari_view_child *view_child =
      wl_container_of(listener, view_child, commit);

  hikari_view_damage_surface(view_child->parent, view_child->surface, false);
}

void
hikari_view_child_init(struct hikari_view_child *view_child,
    struct hikari_view *parent,
    struct wlr_surface *surface)
{
  view_child->parent = parent;
  view_child->surface = surface;

  view_child->new_subsurface.notify = new_subsurface_handler;
  wl_signal_add(&surface->events.new_subsurface, &view_child->new_subsurface);

  view_child->commit.notify = commit_view_child_handler;
  wl_signal_add(&surface->events.commit, &view_child->commit);
}

void
hikari_view_damage_surface(
    struct hikari_view *view, struct wlr_surface *surface, bool whole)
{
  assert(view != NULL);

  // TODO I know, this needs to be done A LOT better
  if (view->use_csd) {
    hikari_output_damage_whole(view->output);
    return;
  }

  struct damage_data_t damage_data;

  damage_data.geometry = hikari_view_geometry(view);
  damage_data.output = view->output;
  damage_data.surface = surface;
  damage_data.whole = whole;
  damage_data.view = view;

  hikari_view_interface_for_each_surface((struct hikari_view_interface *)view,
      damage_single_surface,
      &damage_data);
}

void
hikari_view_refresh_geometry(struct hikari_view *view, struct wlr_box *geometry)
{
  struct wlr_box *new_geometry = refresh_geometry(view);

  memcpy(new_geometry, geometry, sizeof(struct wlr_box));

  view->current_geometry = new_geometry;
  view->current_unmaximized_geometry = refresh_unmaximized_geometry(view);

  refresh_border_geometry(view);
}

static void
commit_pending_geometry(struct hikari_view *view,
    struct wlr_box *pending_geometry,
    struct wlr_box *geometry_before)
{
  struct hikari_output *output = view->output;
  hikari_view_refresh_geometry(view, pending_geometry);

  hikari_output_add_damage(output, geometry_before);
  hikari_view_damage_whole(view);
}

static void
commit_resize(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  raise_view(view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);

  if (operation->center) {
    hikari_view_center_cursor(view);
  }
}

static void
commit_reset(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  if (view->tile) {
    struct hikari_tile *tile = view->tile;
    struct hikari_sheet *sheet = view->sheet;
    struct hikari_layout *layout = sheet->layout;

    assert(layout != NULL);

    view->tile = NULL;

    hikari_tile_fini(tile);
    hikari_free(tile);

    if (wl_list_empty(&layout->tiles)) {
      hikari_layout_fini(layout);
      hikari_free(layout);
      sheet->layout = NULL;
    }
  }

  if (view->maximized_state) {
    hikari_maximized_state_destroy(view->maximized_state);
    view->maximized_state = NULL;
    if (!view->use_csd) {
      view->border.state = HIKARI_BORDER_INACTIVE;
    }
  }

  raise_view(view);
  hikari_indicator_damage(&hikari_server.indicator, view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);
  hikari_indicator_damage(&hikari_server.indicator, view);

  hikari_view_center_cursor(view);
}

static void
commit_unmaximize(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  hikari_free(view->maximized_state);
  view->maximized_state = NULL;

  if (!view->use_csd) {
    view->border.state = HIKARI_BORDER_ACTIVE;
  }

  raise_view(view);
  hikari_indicator_damage(&hikari_server.indicator, view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);
  hikari_indicator_damage(&hikari_server.indicator, view);

  hikari_view_center_cursor(view);
}

static void
commit_full_maximize(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  if (!view->maximized_state) {
    view->maximized_state =
        hikari_malloc(sizeof(struct hikari_maximized_state));
  }

  view->maximized_state->maximization = HIKARI_MAXIMIZATION_FULLY_MAXIMIZED;
  view->maximized_state->geometry = operation->geometry;

  if (!view->use_csd) {
    view->border.state = HIKARI_BORDER_NONE;
  }

  raise_view(view);
  hikari_indicator_damage(&hikari_server.indicator, view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);
  hikari_indicator_damage(&hikari_server.indicator, view);

  hikari_view_center_cursor(view);
}

static void
commit_vertical_maximize(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  if (!view->maximized_state) {
    view->maximized_state =
        hikari_malloc(sizeof(struct hikari_maximized_state));
  } else {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        commit_full_maximize(view, operation, geometry_before);
        return;

      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        if (!view->use_csd) {
          view->border.state = HIKARI_BORDER_INACTIVE;
        }
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        assert(false);
        break;
    }
  }

  view->maximized_state->maximization =
      HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED;
  view->maximized_state->geometry = operation->geometry;

  raise_view(view);
  hikari_indicator_damage(&hikari_server.indicator, view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);
  hikari_indicator_damage(&hikari_server.indicator, view);

  hikari_view_center_cursor(view);
}

static void
commit_horizontal_maximize(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  if (!view->maximized_state) {
    view->maximized_state =
        hikari_malloc(sizeof(struct hikari_maximized_state));
  } else {
    switch (view->maximized_state->maximization) {
      case HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED:
        commit_full_maximize(view, operation, geometry_before);
        return;

      case HIKARI_MAXIMIZATION_FULLY_MAXIMIZED:
        if (!view->use_csd) {
          view->border.state = HIKARI_BORDER_INACTIVE;
        }
        break;

      case HIKARI_MAXIMIZATION_VERTICALLY_MAXIMIZED:
        assert(false);
        break;
    }
  }

  view->maximized_state->maximization =
      HIKARI_MAXIMIZATION_HORIZONTALLY_MAXIMIZED;
  view->maximized_state->geometry = operation->geometry;

  raise_view(view);
  hikari_indicator_damage(&hikari_server.indicator, view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);
  hikari_indicator_damage(&hikari_server.indicator, view);

  hikari_view_center_cursor(view);
}

static void
commit_tile(struct hikari_view *view,
    struct hikari_operation *operation,
    struct wlr_box *geometry_before)
{
  if (view->maximized_state) {
    hikari_maximized_state_destroy(view->maximized_state);
    view->maximized_state = NULL;
    if (!view->use_csd) {
      view->border.state = HIKARI_BORDER_INACTIVE;
    }
  }

  if (view->tile != NULL) {
    struct hikari_tile *tile = view->tile;

    view->tile = NULL;

    hikari_tile_fini(tile);
    hikari_free(tile);
  }

  view->tile = operation->tile;
  operation->tile = NULL;

  hikari_indicator_damage(&hikari_server.indicator, view);
  commit_pending_geometry(view, &operation->geometry, geometry_before);
  hikari_indicator_damage(&hikari_server.indicator, view);
}

static void
commit_operation(struct hikari_operation *operation, struct hikari_view *view)
{
  struct wlr_box geometry_before = *hikari_view_border_geometry(view);

  switch (operation->type) {
    case HIKARI_OPERATION_TYPE_RESIZE:
      commit_resize(view, operation, &geometry_before);
      break;

    case HIKARI_OPERATION_TYPE_RESET:
      commit_reset(view, operation, &geometry_before);
      break;

    case HIKARI_OPERATION_TYPE_UNMAXIMIZE:
      commit_unmaximize(view, operation, &geometry_before);
      break;

    case HIKARI_OPERATION_TYPE_FULL_MAXIMIZE:
      commit_full_maximize(view, operation, &geometry_before);
      break;

    case HIKARI_OPERATION_TYPE_VERTICAL_MAXIMIZE:
      commit_vertical_maximize(view, operation, &geometry_before);
      break;

    case HIKARI_OPERATION_TYPE_HORIZONTAL_MAXIMIZE:
      commit_horizontal_maximize(view, operation, &geometry_before);
      break;

    case HIKARI_OPERATION_TYPE_TILE:
      commit_tile(view, operation, &geometry_before);
      break;
  }
}

void
hikari_view_commit_pending_operation(struct hikari_view *view)
{
  assert(view != NULL);
  assert(view->pending_operation.dirty);

  commit_operation(&view->pending_operation, view);
  hikari_view_unset_dirty(view);
}

void
hikari_view_activate(struct hikari_view *view, bool active)
{
  assert(view != NULL);

  if (view->activate) {
    if (view->border.state != HIKARI_BORDER_NONE) {
      view->border.state =
          active ? HIKARI_BORDER_ACTIVE : HIKARI_BORDER_INACTIVE;
    }
    view->activate(view, active);
  }
}
