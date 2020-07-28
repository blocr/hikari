#include <hikari/view_config.h>

#include <assert.h>
#include <stdlib.h>
#include <ucl.h>

#include <hikari/geometry.h>
#include <hikari/memory.h>
#include <hikari/output.h>
#include <hikari/server.h>
#include <hikari/view.h>

static void
init_properties(struct hikari_view_properties *properties)
{
  properties->group_name = NULL;
  properties->sheet_nr = -1;
  properties->mark = NULL;
  properties->focus = false;
  properties->invisible = false;
  properties->floating = false;
  properties->publicview = false;

  hikari_position_config_init(&properties->position);
}

void
hikari_view_config_init(struct hikari_view_config *view_config)
{
  view_config->app_id = NULL;

  init_properties(&view_config->properties);
}

void
hikari_view_config_fini(struct hikari_view_config *view_config)
{
  assert(view_config != NULL);

  hikari_free(view_config->app_id);
  hikari_free(view_config->properties.group_name);
}

struct hikari_sheet *
hikari_view_config_resolve_sheet(struct hikari_view_config *view_config)
{
  assert(view_config != NULL);

  struct hikari_sheet *sheet;
  struct hikari_view_properties *properties = &view_config->properties;

  if (properties->sheet_nr != -1) {
    sheet = hikari_server.workspace->sheets + properties->sheet_nr;
  } else {
    sheet = hikari_server.workspace->sheet;
  }

  return sheet;
}

struct hikari_group *
hikari_view_config_resolve_group(
    struct hikari_view_config *view_config, const char *app_id)
{
  struct hikari_group *group;

  if (view_config != NULL) {
    struct hikari_view_properties *properties = &view_config->properties;
    if (properties->group_name != NULL && strlen(properties->group_name) > 0) {
      group = hikari_server_find_or_create_group(properties->group_name);
    } else {
      group = hikari_server_find_or_create_group(app_id);
    }
  } else {
    group = hikari_server_find_or_create_group(app_id);
  }

  return group;
}

void
hikari_view_config_resolve_position(struct hikari_view_config *view_config,
    struct hikari_view *view,
    int *x,
    int *y)
{
  assert(view_config != NULL);

  struct hikari_output *output = hikari_server.workspace->output;
  struct wlr_box *geometry = hikari_view_border_geometry(view);
  struct hikari_view_properties *properties = &view_config->properties;

  switch (properties->position.type) {
    case HIKARI_POSITION_CONFIG_TYPE_ABSOLUTE:
      *x = properties->position.config.absolute.x;
      *y = properties->position.config.absolute.y;
      break;

    case HIKARI_POSITION_CONFIG_TYPE_AUTO:
      *x = hikari_server.cursor.wlr_cursor->x - output->geometry.x;
      *y = hikari_server.cursor.wlr_cursor->y - output->geometry.y;
      break;

    case HIKARI_POSITION_CONFIG_TYPE_RELATIVE:
      switch (properties->position.config.relative) {
        case HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_LEFT:
          hikari_geometry_position_bottom_left(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_MIDDLE:
          hikari_geometry_position_bottom_middle(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_BOTTOM_RIGHT:
          hikari_geometry_position_bottom_right(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_CENTER:
          hikari_geometry_position_center(geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_CENTER_LEFT:
          hikari_geometry_position_center_left(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_CENTER_RIGHT:
          hikari_geometry_position_center_right(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_TOP_LEFT:
          hikari_geometry_position_top_left(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_TOP_MIDDLE:
          hikari_geometry_position_top_middle(
              geometry, &output->usable_area, x, y);
          break;

        case HIKARI_POSITION_CONFIG_RELATIVE_TOP_RIGHT:
          hikari_geometry_position_top_right(
              geometry, &output->usable_area, x, y);
      }
      break;
  }
}

bool
hikari_view_config_parse(
    struct hikari_view_config *view_config, const ucl_object_t *view_config_obj)
{
  struct hikari_view_properties *properties = &view_config->properties;
  bool success = false;

  ucl_object_iter_t it = ucl_object_iterate_new(view_config_obj);

  const ucl_object_t *cur;
  while ((cur = ucl_object_iterate_safe(it, false)) != NULL) {
    const char *key = ucl_object_key(cur);

    if (!strcmp(key, "group")) {
      const char *group_name;

      if (!ucl_object_tostring_safe(cur, &group_name)) {
        fprintf(stderr,
            "configuration error: expected string for \"views\" "
            "\"group\"\n");
        goto done;
      }

      if (strlen(group_name) == 0) {
        fprintf(stderr,
            "configuration error: expected non-empty string for \"views\" "
            "\"group\"\n");
        goto done;
      }

      properties->group_name = strdup(group_name);

    } else if (!strcmp(key, "sheet")) {
      int64_t sheet_nr;
      if (!ucl_object_toint_safe(cur, &sheet_nr)) {
        fprintf(stderr,
            "configuration error: expected integer for \"views\" "
            "\"sheet\"\n");
        goto done;
      }

      properties->sheet_nr = sheet_nr;
    } else if (!strcmp(key, "mark")) {
      const char *mark_name;

      if (!ucl_object_tostring_safe(cur, &mark_name)) {
        fprintf(stderr,
            "configuration error: expected string for \"views\" \"mark\"");
        goto done;
      }

      if (strlen(mark_name) != 1) {
        fprintf(stderr,
            "configuration error: invalid \"mark\" register \"%s\" for "
            "\"views\"\n",
            mark_name);
        goto done;
      }

      properties->mark = &hikari_marks[mark_name[0] - 'a'];
    } else if (!strcmp(key, "position")) {
      if (!hikari_position_config_parse(&properties->position, cur)) {
        goto done;
      }
    } else if (!strcmp(key, "focus")) {
      bool focus;

      if (!ucl_object_toboolean_safe(cur, &focus)) {
        fprintf(stderr,
            "configuration error: expected boolean for \"views\" "
            "\"focus\"\n");
        goto done;
      }

      properties->focus = focus;
    } else if (!strcmp(key, "invisible")) {
      bool invisible;

      if (!ucl_object_toboolean_safe(cur, &invisible)) {
        fprintf(stderr,
            "configuration error: expected boolean for \"views\" "
            "\"invisible\"\n");
        goto done;
      }

      properties->invisible = invisible;
    } else if (!strcmp(key, "floating")) {
      bool floating;

      if (!ucl_object_toboolean_safe(cur, &floating)) {
        fprintf(stderr,
            "configuration error: expected boolean for \"views\" "
            "\"floating\"\n");
        goto done;
      }

      properties->floating = floating;
    } else if (!strcmp(key, "public")) {
      bool publicview;

      if (!ucl_object_toboolean_safe(cur, &publicview)) {
        fprintf(stderr,
            "configuration error: expected boolean for \"views\" "
            "\"public\"\n");
        goto done;
      }

      properties->publicview = publicview;
    } else {
      fprintf(
          stderr, "configuration error: unkown \"views\" key \"%s\"\n", key);
      goto done;
    }
  }

  success = true;

done:
  ucl_object_iterate_free(it);

  return success;
}
