#include <hikari/render.h>

#include <assert.h>

#include <hikari/color.h>
#include <hikari/output.h>
#include <hikari/render_data.h>
#include <hikari/view.h>

#ifdef HAVE_XWAYLAND
#include <hikari/xwayland_unmanaged_view.h>
#include <hikari/xwayland_view.h>
#endif

#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_damage.h>

#ifdef HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif

static inline void
clear_output(struct hikari_render_data *render_data)
{
  float *clear_color = hikari_configuration->clear;
  struct wlr_renderer *renderer = render_data->renderer;
  struct wlr_output *wlr_output = render_data->output;
  pixman_region32_t *damage = render_data->damage;

#ifndef NDEBUG
  if (hikari_server.track_damage) {
    float damage_color[4];
    hikari_color_convert(damage_color, 0x000000);
    wlr_renderer_clear(renderer, damage_color);
  }
#endif

  int nrects;
  pixman_box32_t *rects = pixman_region32_rectangles(damage, &nrects);
  for (int i = 0; i < nrects; ++i) {
    hikari_output_scissor_render(wlr_output, renderer, &rects[i]);
    wlr_renderer_clear(renderer, clear_color);
  }
}

static inline void
renderer_end(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct wlr_renderer *renderer = render_data->renderer;
  struct wlr_output *wlr_output = render_data->output;

  wlr_renderer_scissor(renderer, NULL);
  wlr_output_render_software_cursors(wlr_output, NULL);
  wlr_renderer_end(renderer);

  int width, height;
  wlr_output_transformed_resolution(wlr_output, &width, &height);

  pixman_region32_t frame_damage;
  pixman_region32_init(&frame_damage);

  enum wl_output_transform transform =
      wlr_output_transform_invert(wlr_output->transform);
  wlr_region_transform(
      &frame_damage, &output->damage->current, transform, width, height);

  wlr_output_set_damage(wlr_output, &frame_damage);
  pixman_region32_fini(&frame_damage);

  wlr_output_commit(wlr_output);
}

static inline void
render_texture(struct wlr_texture *texture,
    struct wlr_output *output,
    pixman_region32_t *damage,
    struct wlr_renderer *renderer,
    const float matrix[static 9],
    struct wlr_box *box,
    float alpha)
{
  pixman_region32_t local_damage;
  pixman_region32_init(&local_damage);
  pixman_region32_union_rect(
      &local_damage, &local_damage, box->x, box->y, box->width, box->height);

  pixman_region32_intersect(&local_damage, &local_damage, damage);

  bool damaged = pixman_region32_not_empty(&local_damage);
  if (!damaged) {
    goto damage_finish;
  }

  int nrects;
  pixman_box32_t *rects = pixman_region32_rectangles(&local_damage, &nrects);
  for (int i = 0; i < nrects; ++i) {
    hikari_output_scissor_render(output, renderer, &rects[i]);
    wlr_render_texture_with_matrix(renderer, texture, matrix, alpha);
  }

damage_finish:
  pixman_region32_fini(&local_damage);
}

void
render_surface(struct wlr_surface *surface, int sx, int sy, void *data)
{
  assert(surface != NULL);

  struct wlr_texture *texture = wlr_surface_get_texture(surface);

  if (texture == NULL) {
    return;
  }

  struct hikari_render_data *render_data = data;
  struct wlr_box *geometry = render_data->geometry;
  struct wlr_output *wlr_output = render_data->output;

  double ox = geometry->x + sx;
  double oy = geometry->y + sy;

  struct wlr_box box = { .x = ox * wlr_output->scale,
    .y = oy * wlr_output->scale,
    .width = surface->current.width * wlr_output->scale,
    .height = surface->current.height * wlr_output->scale };

  float matrix[9];
  enum wl_output_transform transform =
      wlr_output_transform_invert(surface->current.transform);

  wlr_matrix_project_box(
      matrix, &box, transform, 0, wlr_output->transform_matrix);

  render_texture(texture,
      wlr_output,
      render_data->damage,
      render_data->renderer,
      matrix,
      &box,
      1);
}

static inline void
render_background(struct hikari_output *output,
    struct hikari_render_data *render_data,
    float alpha)
{
  if (output->background == NULL) {
    return;
  }

  float matrix[9];
  struct wlr_output *wlr_output = output->wlr_output;

  struct wlr_box geometry = { .x = 0, .y = 0 };
  wlr_output_transformed_resolution(
      wlr_output, &geometry.width, &geometry.height);

  wlr_matrix_project_box(matrix, &geometry, 0, 0, wlr_output->transform_matrix);

  render_texture(output->background,
      wlr_output,
      render_data->damage,
      render_data->renderer,
      matrix,
      &geometry,
      alpha);
}

#ifdef HAVE_LAYERSHELL
static inline void
render_layer(struct wl_list *layers, struct hikari_render_data *render_data)
{
  struct hikari_layer *layer;
  wl_list_for_each (layer, layers, layer_surfaces) {
    render_data->geometry = &layer->geometry;
    wlr_layer_surface_v1_for_each_surface(
        layer->surface, render_surface, render_data);
  }
}
#endif

static inline void
render_workspace(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
#ifdef HAVE_LAYERSHELL
  render_layer(
      &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND], render_data);
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM], render_data);
#endif

  struct hikari_view *view;
  wl_list_for_each_reverse (view, &output->workspace->views, workspace_views) {
    render_data->geometry = hikari_view_border_geometry(view);

    if (hikari_view_wants_border(view)) {
      hikari_border_render(&view->border, render_data);
    }

    render_data->geometry = hikari_view_geometry(view);

    hikari_view_interface_for_each_surface(
        (struct hikari_view_interface *)view, render_surface, render_data);
  }

#ifdef HAVE_LAYERSHELL
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP], render_data);
#endif

#ifdef HAVE_XWAYLAND
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view;
  wl_list_for_each_reverse (xwayland_unmanaged_view,
      &output->unmanaged_xwayland_views,
      unmanaged_server_views) {

    render_data->geometry = &xwayland_unmanaged_view->geometry;

    wlr_surface_for_each_surface(
        xwayland_unmanaged_view->surface->surface, render_surface, render_data);
  }
#endif
}

void
hikari_render_texture(struct wlr_texture *texture,
    struct wlr_output *output,
    pixman_region32_t *damage,
    struct wlr_renderer *renderer,
    const float matrix[static 9],
    struct wlr_box *box,
    float alpha)
{
  render_texture(texture, output, damage, renderer, matrix, box, alpha);
}

#ifdef HAVE_LAYERSHELL
static inline void
render_overlay(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  render_layer(&output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY], render_data);
}
#endif

static inline void
render_output(struct hikari_output *output,
    pixman_region32_t *damage,
    struct timespec *now)
{
  struct wlr_output *wlr_output = output->wlr_output;
  struct wlr_renderer *renderer = wlr_backend_get_renderer(wlr_output->backend);

  struct hikari_render_data render_data = {
    .output = wlr_output, .renderer = renderer, .when = now, .damage = damage
  };

  wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);

  if (pixman_region32_not_empty(damage)) {
    clear_output(&render_data);

    render_background(output, &render_data, 1);
    render_workspace(output, &render_data);

    hikari_server.mode->render(output, &render_data);

#ifdef HAVE_LAYERSHELL
    render_overlay(output, &render_data);
#endif
  }

  renderer_end(output, &render_data);
}

#ifdef HAVE_LAYERSHELL
static inline void
layer_for_each(struct wl_list *layers,
    void (*func)(struct wlr_surface *, int, int, void *),
    void *data)
{
  struct hikari_layer *layer;
  wl_list_for_each (layer, layers, layer_surfaces) {
    wlr_layer_surface_v1_for_each_surface(layer->surface, func, data);
  }
}
#endif

static inline void
render_empty_output(struct hikari_output *output,
    pixman_region32_t *damage,
    struct timespec *now)
{
  struct wlr_output *wlr_output = output->wlr_output;
  struct wlr_renderer *renderer = wlr_backend_get_renderer(wlr_output->backend);

  struct hikari_render_data render_data = {
    .output = wlr_output, .renderer = renderer, .when = now, .damage = damage
  };

  wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);

  if (pixman_region32_not_empty(damage)) {
    clear_output(&render_data);

    hikari_server.mode->render(output, &render_data);
  }

  renderer_end(output, &render_data);
}

static void
send_frame_done(struct wlr_surface *surface, int sx, int sy, void *data)
{
  assert(surface != NULL);

  struct timespec *now = data;
  wlr_surface_send_frame_done(surface, now);
}

static inline void
frame_done(struct hikari_output *output, struct timespec *now)
{
  struct hikari_view *view;
  wl_list_for_each_reverse (view, &output->views, output_views) {
    hikari_view_interface_for_each_surface(
        (struct hikari_view_interface *)view, send_frame_done, now);
  }

#ifdef HAVE_XWAYLAND
  struct hikari_xwayland_unmanaged_view *xwayland_unmanaged_view;
  wl_list_for_each_reverse (xwayland_unmanaged_view,
      &output->unmanaged_xwayland_views,
      unmanaged_server_views) {
    wlr_surface_for_each_surface(
        xwayland_unmanaged_view->surface->surface, send_frame_done, now);
  }
#endif

#ifdef HAVE_LAYERSHELL
  for (int i = 0; i < 4; i++) {
    layer_for_each(&output->layers[i], send_frame_done, now);
  }
#endif
}

#define DAMAGE_HANDLER(name, render)                                           \
  void name##_handler(struct wl_listener *listener, void *data)                \
  {                                                                            \
    bool needs_frame;                                                          \
    struct timespec now;                                                       \
    struct hikari_output *output =                                             \
        wl_container_of(listener, output, damage_frame);                       \
                                                                               \
    pixman_region32_t buffer_damage;                                           \
    pixman_region32_init(&buffer_damage);                                      \
                                                                               \
    clock_gettime(CLOCK_MONOTONIC, &now);                                      \
                                                                               \
    if (wlr_output_damage_attach_render(                                       \
            output->damage, &needs_frame, &buffer_damage) &&                   \
        needs_frame) {                                                         \
      render(output, &buffer_damage, &now);                                    \
    }                                                                          \
                                                                               \
    pixman_region32_fini(&buffer_damage);                                      \
                                                                               \
    frame_done(output, &now);                                                  \
  }

DAMAGE_HANDLER(damage_frame, render_output)
DAMAGE_HANDLER(damage_empty_frame, render_empty_output)
#undef DAMAGE_HANDLER

void
hikari_render_visible_views(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_view *view;
  wl_list_for_each_reverse (view, &output->views, output_views) {
    if (!hikari_view_is_hidden(view)) {
      render_data->geometry = hikari_view_border_geometry(view);

      if (hikari_view_wants_border(view)) {
        hikari_border_render(&view->border, render_data);
      }

      render_data->geometry = hikari_view_geometry(view);

      hikari_view_interface_for_each_surface(
          (struct hikari_view_interface *)view, render_surface, render_data);
    }
  }
}

void
hikari_render_background(struct hikari_output *output,
    struct hikari_render_data *render_data,
    float alpha)
{
  render_background(output, render_data, alpha);
}

void
hikari_render_normal_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (hikari_server.keyboard_state.mod_pressed && focus_view != NULL) {
    struct hikari_group *group = focus_view->group;
    struct hikari_view *first = hikari_group_first_view(group);
    float *indicator_first = hikari_configuration->indicator_first;
    float *indicator_grouped = hikari_configuration->indicator_grouped;

    struct hikari_view *view;
    wl_list_for_each_reverse (
        view, &group->visible_views, visible_group_views) {
      if (view != focus_view && view->output == output) {
        render_data->geometry = hikari_view_border_geometry(view);

        if (first == view) {
          hikari_indicator_frame_render(
              &view->indicator_frame, indicator_first, render_data);
        } else {
          hikari_indicator_frame_render(
              &view->indicator_frame, indicator_grouped, render_data);
        }
      }
    }

    if (focus_view->output == output) {
      render_data->geometry = hikari_view_border_geometry(focus_view);

      hikari_indicator_frame_render(&focus_view->indicator_frame,
          hikari_configuration->indicator_selected,
          render_data);

      hikari_indicator_render(&hikari_server.indicator, render_data);
    }
  }
}

void
hikari_render_group_assign_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_group_assign_mode *mode = &hikari_server.group_assign_mode;

  assert(mode == (struct hikari_group_assign_mode *)hikari_server.mode);

  struct hikari_group *group = mode->group;
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  assert(focus_view != NULL);

  if (group != NULL) {
    struct hikari_view *first = hikari_group_first_view(group);
    float *indicator_first = hikari_configuration->indicator_first;
    float *indicator_grouped = hikari_configuration->indicator_grouped;

    struct hikari_view *view;
    wl_list_for_each_reverse (
        view, &group->visible_views, visible_group_views) {
      if (view->output == output && view != focus_view) {
        render_data->geometry = hikari_view_border_geometry(view);

        if (first == view) {
          hikari_indicator_frame_render(
              &view->indicator_frame, indicator_first, render_data);
        } else {
          hikari_indicator_frame_render(
              &view->indicator_frame, indicator_grouped, render_data);
        }
      }
    }
  }

  if (focus_view->output == output) {
    render_data->geometry = hikari_view_border_geometry(focus_view);

    hikari_indicator_frame_render(&focus_view->indicator_frame,
        hikari_configuration->indicator_selected,
        render_data);

    hikari_indicator_render(&hikari_server.indicator, render_data);
  }
}

void
hikari_render_input_grab_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  assert(hikari_server.workspace->focus_view != NULL);

  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (view->output == output) {
    render_data->geometry = hikari_view_border_geometry(view);
    hikari_indicator_frame_render(&view->indicator_frame,
        hikari_configuration->indicator_insert,
        render_data);
  }
}

void
hikari_render_lock_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_lock_mode *mode = &hikari_server.lock_mode;

  assert(mode == (struct hikari_lock_mode *)hikari_server.mode);

  hikari_render_background(output, render_data, 0.1);
  hikari_render_visible_views(output, render_data);
  hikari_lock_indicator_render(mode->lock_indicator, render_data);
}

void
hikari_render_mark_assign_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_mark_assign_mode *mode = &hikari_server.mark_assign_mode;

  assert(mode == (struct hikari_mark_assign_mode *)hikari_server.mode);
  assert(hikari_server.workspace->focus_view != NULL);

  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (mode->pending_mark != NULL && mode->pending_mark->view != NULL &&
      mode->pending_mark->view->output == output) {
    render_data->geometry =
        hikari_view_border_geometry(mode->pending_mark->view);

    hikari_indicator_frame_render(&mode->pending_mark->view->indicator_frame,
        hikari_configuration->indicator_conflict,
        render_data);
    hikari_indicator_render(&mode->indicator, render_data);
  }

  if (view->output == output) {
    render_data->geometry = hikari_view_border_geometry(view);

    hikari_indicator_frame_render(&view->indicator_frame,
        hikari_configuration->indicator_selected,
        render_data);

    hikari_indicator_render(&hikari_server.indicator, render_data);
  }
}

void
hikari_render_mark_select_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{}

void
hikari_render_move_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view->output == output && !hikari_view_is_hidden(focus_view)) {
    render_data->geometry = hikari_view_border_geometry(focus_view);

    hikari_indicator_frame_render(&focus_view->indicator_frame,
        hikari_configuration->indicator_insert,
        render_data);

    hikari_indicator_render(&hikari_server.indicator, render_data);
  }
}

void
hikari_render_resize_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view->output == output) {
    render_data->geometry = hikari_view_border_geometry(focus_view);

    hikari_indicator_frame_render(&focus_view->indicator_frame,
        hikari_configuration->indicator_insert,
        render_data);

    hikari_indicator_render(&hikari_server.indicator, render_data);
  }
}

void
hikari_render_sheet_assign_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{
  assert(hikari_server.workspace->focus_view != NULL);
  struct hikari_view *view = hikari_server.workspace->focus_view;

  if (view->output == output) {
    render_data->geometry = hikari_view_border_geometry(view);

    hikari_indicator_frame_render(&view->indicator_frame,
        hikari_configuration->indicator_selected,
        render_data);

    hikari_indicator_render(&hikari_server.indicator, render_data);
  }
}

void
hikari_render_dnd_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{}

void
hikari_render_layout_select_mode(
    struct hikari_output *output, struct hikari_render_data *render_data)
{}