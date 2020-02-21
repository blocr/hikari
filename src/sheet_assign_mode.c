#include <hikari/sheet_assign_mode.h>

#include <stdbool.h>
#include <string.h>

#include <wayland-util.h>

#include <hikari/color.h>
#include <hikari/completion.h>
#include <hikari/configuration.h>
#include <hikari/group.h>
#include <hikari/indicator.h>
#include <hikari/indicator_frame.h>
#include <hikari/keyboard.h>
#include <hikari/memory.h>
#include <hikari/normal_mode.h>
#include <hikari/render_data.h>
#include <hikari/server.h>
#include <hikari/sheet.h>
#include <hikari/view.h>
#include <hikari/workspace.h>

static bool
check_confirmation(
    struct wlr_event_keyboard_key *event, struct hikari_keyboard *keyboard)
{
  uint32_t keycode = event->keycode + 8;
  const xkb_keysym_t *syms;
  int nsyms = xkb_state_key_get_syms(
      keyboard->device->keyboard->xkb_state, keycode, &syms);

  for (int i = 0; i < nsyms; i++) {
    switch (syms[i]) {
      case XKB_KEY_Return:
        return true;
        break;
    }
  }

  return false;
}

static bool
check_cancellation(
    struct wlr_event_keyboard_key *event, struct hikari_keyboard *keyboard)
{
  uint32_t keycode = event->keycode + 8;
  const xkb_keysym_t *syms;
  int nsyms = xkb_state_key_get_syms(
      keyboard->device->keyboard->xkb_state, keycode, &syms);

  for (int i = 0; i < nsyms; i++) {
    switch (syms[i]) {
      case XKB_KEY_Escape:
        return true;
        break;
    }
  }

  return false;
}

#define CYCLE_SHEET(name)                                                      \
  static struct hikari_sheet *name##_sheet(struct hikari_sheet *sheet)         \
  {                                                                            \
    struct wl_list *name = sheet->workspace->server_workspaces.name;           \
                                                                               \
    if (name == &hikari_server.workspaces) {                                   \
      name = hikari_server.workspaces.name;                                    \
    }                                                                          \
                                                                               \
    struct hikari_workspace *workspace =                                       \
        wl_container_of(name, workspace, server_workspaces);                   \
                                                                               \
    assert(workspace != NULL);                                                 \
                                                                               \
    return &workspace->sheets[sheet->nr];                                      \
  }

CYCLE_SHEET(next)
CYCLE_SHEET(prev)
#undef CYCLE_SHEET

static struct hikari_sheet *
process_input_key(struct hikari_workspace *workspace,
    struct wlr_event_keyboard_key *event,
    struct hikari_keyboard *keyboard)
{
  struct hikari_sheet_assign_mode *mode =
      (struct hikari_sheet_assign_mode *)hikari_server.mode;

  const xkb_keysym_t *syms;
  uint32_t keycode = event->keycode + 8;

  int nsyms = xkb_state_key_get_syms(
      keyboard->device->keyboard->xkb_state, keycode, &syms);

  struct hikari_sheet *sheet;

  for (int i = 0; i < nsyms; i++) {
    switch (syms[i]) {
      case XKB_KEY_1:
        sheet = &workspace->sheets[1];
        break;

      case XKB_KEY_2:
        sheet = &workspace->sheets[2];
        break;

      case XKB_KEY_3:
        sheet = &workspace->sheets[3];
        break;

      case XKB_KEY_4:
        sheet = &workspace->sheets[4];
        break;

      case XKB_KEY_5:
        sheet = &workspace->sheets[5];
        break;

      case XKB_KEY_6:
        sheet = &workspace->sheets[6];
        break;

      case XKB_KEY_7:
        sheet = &workspace->sheets[7];
        break;

      case XKB_KEY_8:
        sheet = &workspace->sheets[8];
        break;

      case XKB_KEY_9:
        sheet = &workspace->sheets[9];
        break;

      case XKB_KEY_0:
        sheet = &workspace->sheets[0];
        break;

      case XKB_KEY_Tab:
        sheet = next_sheet(mode->sheet);
        break;

      case XKB_KEY_ISO_Left_Tab:
        sheet = prev_sheet(mode->sheet);
        break;

      default:
        sheet = mode->sheet;
        break;
    }
  }

  return sheet;
}

static void
confirm_sheet_assign(struct hikari_workspace *workspace)
{
  struct hikari_sheet_assign_mode *mode =
      (struct hikari_sheet_assign_mode *)&hikari_server.sheet_assign_mode;

  assert(mode == (struct hikari_sheet_assign_mode *)hikari_server.mode);

  struct hikari_view *focus_view = workspace->focus_view;
  struct wlr_box *geometry = hikari_view_geometry(focus_view);

  hikari_indicator_update_sheet(&hikari_server.indicator,
      geometry,
      workspace->output,
      mode->sheet,
      hikari_configuration->indicator_selected,
      hikari_view_is_iconified(focus_view),
      hikari_view_is_floating(focus_view));

  hikari_view_pin_to_sheet(focus_view, mode->sheet);
  hikari_server_enter_normal_mode(NULL);
}

static void
cancel_sheet_assign(struct hikari_workspace *workspace)
{
  struct hikari_view *focus_view = workspace->focus_view;
  struct wlr_box *geometry = hikari_view_geometry(focus_view);

  hikari_indicator_update_sheet(&hikari_server.indicator,
      geometry,
      workspace->output,
      focus_view->sheet,
      hikari_configuration->indicator_selected,
      hikari_view_is_iconified(focus_view),
      hikari_view_is_floating(focus_view));

  hikari_server_enter_normal_mode(NULL);
}

static void
update_state(struct hikari_workspace *workspace,
    struct hikari_keyboard *keyboard,
    struct wlr_event_keyboard_key *event)
{
  struct hikari_sheet_assign_mode *mode =
      (struct hikari_sheet_assign_mode *)&hikari_server.sheet_assign_mode;

  assert(mode == (struct hikari_sheet_assign_mode *)hikari_server.mode);

  struct hikari_view *focus_view = workspace->focus_view;
  struct wlr_box *geometry = hikari_view_border_geometry(focus_view);

  mode->sheet = process_input_key(workspace, event, keyboard);

  hikari_indicator_update_sheet(&hikari_server.indicator,
      geometry,
      workspace->output,
      mode->sheet,
      hikari_configuration->indicator_insert,
      hikari_view_is_iconified(focus_view),
      hikari_view_is_floating(focus_view));
}

static void
assign_sheet(struct hikari_workspace *workspace,
    struct wlr_event_keyboard_key *event,
    struct hikari_keyboard *keyboard)
{
  if (check_confirmation(event, keyboard)) {
    confirm_sheet_assign(workspace);
  } else if (check_cancellation(event, keyboard)) {
    cancel_sheet_assign(workspace);
  } else {
    update_state(workspace, keyboard, event);
  }
}

static void
key_handler(struct wl_listener *listener, void *data)
{
  struct hikari_keyboard *keyboard = wl_container_of(listener, keyboard, key);
  struct wlr_event_keyboard_key *event = data;

  struct hikari_workspace *workspace = hikari_server.workspace;

  if (event->state == WLR_KEY_PRESSED) {
    assign_sheet(workspace, event, keyboard);
  }
}

static void
modifier_handler(struct wl_listener *listener, void *data)
{}

static void
render(struct hikari_output *output, struct hikari_render_data *render_data)
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

static void
cancel(void)
{
  hikari_server.sheet_assign_mode.sheet = NULL;
}

static void
button_handler(struct wl_listener *listener, void *data)
{}

static void
cursor_move(void)
{}

void
hikari_sheet_assign_mode_init(
    struct hikari_sheet_assign_mode *sheet_assign_mode)
{
  sheet_assign_mode->mode.type = HIKARI_MODE_TYPE_ASSIGN_SHEET;
  sheet_assign_mode->mode.key_handler = key_handler;
  sheet_assign_mode->mode.button_handler = button_handler;
  sheet_assign_mode->mode.modifier_handler = modifier_handler;
  sheet_assign_mode->mode.render = render;
  sheet_assign_mode->mode.cancel = cancel;
  sheet_assign_mode->mode.cursor_move = cursor_move;
  sheet_assign_mode->sheet = NULL;
}
