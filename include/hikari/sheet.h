#if !defined(HIKARI_SHEET_H)
#define HIKARI_SHEET_H

#include <wayland-util.h>

#include <wlr/types/wlr_box.h>

struct hikari_group;
struct hikari_layout;
struct hikari_split;
struct hikari_workspace;

struct hikari_sheet {
  uint8_t nr;
  struct wl_list views;
  struct hikari_layout *layout;

  struct hikari_group *group;
  struct hikari_workspace *workspace;
};

void
hikari_sheet_init(
    struct hikari_sheet *sheet, int nr, struct hikari_workspace *workspace);

void
hikari_sheet_fini(struct hikari_sheet *sheet);

static const int HIKARI_NR_OF_SHEETS = 10;

struct hikari_view *
hikari_sheet_first_view(struct hikari_sheet *sheet);

struct hikari_view *
hikari_sheet_last_view(struct hikari_sheet *sheet);

struct hikari_view *
hikari_sheet_next_view(struct hikari_sheet *sheet, struct hikari_view *view);

struct hikari_view *
hikari_sheet_prev_view(struct hikari_sheet *sheet, struct hikari_view *view);

struct hikari_view *
hikari_sheet_first_tileable_view(struct hikari_sheet *sheet);

struct hikari_view *
hikari_sheet_vertical_layout(struct hikari_sheet *sheet,
    struct hikari_view *first,
    struct wlr_box *frame,
    int max);

struct hikari_view *
hikari_sheet_horizontal_layout(struct hikari_sheet *sheet,
    struct hikari_view *first,
    struct wlr_box *frame,
    int max);

struct hikari_view *
hikari_sheet_grid_layout(struct hikari_sheet *sheet,
    struct hikari_view *first,
    struct wlr_box *frame,
    int max);

struct hikari_view *
hikari_sheet_full_layout(struct hikari_sheet *sheet,
    struct hikari_view *first,
    struct wlr_box *frame,
    int max);

struct hikari_view *
hikari_sheet_single_layout(struct hikari_sheet *sheet,
    struct hikari_view *first,
    struct wlr_box *frame,
    int max);

struct hikari_view *
hikari_sheet_empty_layout(struct hikari_sheet *sheet,
    struct hikari_view *first,
    struct wlr_box *frame,
    int max);

int
hikari_sheet_tileable_views(struct hikari_sheet *sheet);

void
hikari_sheet_reset_layout(struct hikari_sheet *sheet);

struct hikari_sheet *
hikari_sheet_next(struct hikari_sheet *sheet);

struct hikari_sheet *
hikari_sheet_prev(struct hikari_sheet *sheet);

struct hikari_sheet *
hikari_sheet_next_inhabited(struct hikari_sheet *sheet);

struct hikari_sheet *
hikari_sheet_prev_inhabited(struct hikari_sheet *sheet);

void
hikari_sheet_apply_split(
    struct hikari_sheet *sheet, struct hikari_split *split);

#endif
