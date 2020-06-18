#include <hikari/mark_select_mode.h>

#include <assert.h>
#include <stdbool.h>

#include <hikari/keyboard.h>
#include <hikari/mark.h>
#include <hikari/render.h>
#include <hikari/server.h>
#include <hikari/workspace.h>

static void
mark_select(struct hikari_workspace *workspace,
    struct wlr_event_keyboard_key *event,
    struct hikari_keyboard *keyboard)
{
  uint32_t keycode = event->keycode + 8;
  uint32_t codepoint = hikari_keyboard_get_codepoint(keyboard, keycode);

  hikari_server_enter_normal_mode(NULL);

  struct hikari_mark *mark;
  if (hikari_mark_get(codepoint, &mark)) {
    assert(mark != NULL);
    if (hikari_server.mark_select_mode.switch_workspace) {
      hikari_server_switch_to_mark(mark);
    } else {
      hikari_server_show_mark(mark);
    }
  }
}

static void
key_handler(struct wl_listener *listener, void *data)
{
  struct hikari_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct wlr_event_keyboard_key *event = data;

  struct hikari_workspace *workspace = hikari_server.workspace;

  if (event->state == WLR_KEY_PRESSED) {
    mark_select(workspace, event, keyboard);
  }
}

static void
modifier_handler(struct wl_listener *listener, void *data)
{}

static void
cancel(void)
{}

static void
button_handler(struct wl_listener *listener, void *data)
{}

static void
cursor_move(uint32_t time_msec)
{}

void
hikari_mark_select_mode_init(struct hikari_mark_select_mode *mark_select_mode)
{
  mark_select_mode->mode.key_handler = key_handler;
  mark_select_mode->mode.button_handler = button_handler;
  mark_select_mode->mode.modifier_handler = modifier_handler;
  mark_select_mode->mode.render = hikari_render_mark_select_mode;
  mark_select_mode->mode.cancel = cancel;
  mark_select_mode->mode.cursor_move = cursor_move;

  mark_select_mode->switch_workspace = false;
}

void
hikari_mark_select_mode_enter(bool switch_workspace)
{
  struct hikari_server *server = &hikari_server;

  assert(server->workspace != NULL);

  server->mark_select_mode.switch_workspace = switch_workspace;
  server->mode = (struct hikari_mode *)&server->mark_select_mode;
}
