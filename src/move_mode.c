#include <hikari/move_mode.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>

#include <hikari/configuration.h>
#include <hikari/keybinding.h>
#include <hikari/keyboard.h>
#include <hikari/render_data.h>
#include <hikari/server.h>
#include <hikari/view.h>

static void
cancel(void)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view != NULL) {
    hikari_indicator_update(&hikari_server.indicator,
        focus_view,
        hikari_configuration.indicator_selected);

    hikari_view_center_cursor(focus_view);
  }
}

static void
key_handler(struct wl_listener *listener, void *data)
{
  struct hikari_keyboard *keyboard = wl_container_of(listener, keyboard, key);

  struct wlr_event_keyboard_key *event = data;

  if (event->state == WLR_KEY_RELEASED) {
    hikari_server_enter_normal_mode(NULL);
  }
}

static void
modifier_handler(struct wl_listener *listener, void *data)
{}

static void
render(struct hikari_output *output, struct hikari_render_data *render_data)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  if (focus_view->output == output) {
    render_data->geometry = hikari_view_border_geometry(focus_view);

    hikari_indicator_frame_render(&focus_view->indicator_frame,
        hikari_configuration.indicator_insert,
        render_data);

    hikari_indicator_render(&hikari_server.indicator, render_data);
  }
}

static void
cursor_move(void)
{
  struct hikari_view *focus_view = hikari_server.workspace->focus_view;

  assert(focus_view != NULL);

  struct hikari_output *output = focus_view->output;

  hikari_view_move_absolute(focus_view,
      hikari_server.cursor->x - output->geometry.x,
      hikari_server.cursor->y - output->geometry.y);
}

static void
button_handler(struct wl_listener *listener, void *data)
{
  struct wlr_event_pointer_button *event = data;

  if (event->state == WLR_BUTTON_RELEASED) {
    hikari_server_enter_normal_mode(NULL);
  }
}

void
hikari_move_mode_init(struct hikari_move_mode *move_mode)
{
  move_mode->mode.type = HIKARI_MODE_TYPE_MOVE;
  move_mode->mode.key_handler = key_handler;
  move_mode->mode.button_handler = button_handler;
  move_mode->mode.modifier_handler = modifier_handler;
  move_mode->mode.render = render;
  move_mode->mode.cancel = cancel;
  move_mode->mode.cursor_move = cursor_move;
}
