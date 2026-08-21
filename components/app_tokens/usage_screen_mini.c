#include "usage_screen.h"

#include <stdio.h>
#include <string.h>

#include "agent_assets.h"
#include "agent_monitor.h"
#include "app_tokens.h"
#include "app_tokens_config.h"
#include "github_status.h"
#include "max_tracker.h"
#include "max_tracker_presenter.h"
#include "project_star_event.h"
#include "project_star_chime.h"
#include "project_star_popup.h"
#include "torget.h"
#include "usage_live_policy.h"
#include "usage_presenter.h"

extern const lv_font_t plex_num_50;
extern const lv_font_t plex_ui_21;
extern const lv_font_t plex_ui_16;
extern const lv_font_t plex_ui_14;
extern const lv_font_t plex_ui_12;

#define SCREEN_W 320
#define SCREEN_H 170
#define COL_BLACK lv_color_hex(0x000000)
#define COL_WHITE lv_color_hex(0xFFFFFF)
#define COL_MUTED lv_color_hex(0x9298A2)
#define COL_META lv_color_hex(0xB2B7C0)
#define COL_TRACK lv_color_hex(0x303238)
#define COL_CLAUDE lv_color_hex(0xD97757)
#define COL_CODEX lv_color_hex(0x6F78FF)
#define COL_STAR lv_color_hex(0xE3B341)
#define COL_DOT lv_color_hex(0x41444A)
#define COL_DOT_ON lv_color_hex(0xCDD2DA)
#define TRACK_W 188
#define PAGER_Y 160

typedef struct {
  lv_obj_t *tile;
  lv_obj_t *icon;
  lv_obj_t *title;
  lv_obj_t *caption;
  lv_obj_t *context;
  lv_obj_t *percent;
  lv_obj_t *percent_suffix;
  lv_obj_t *track;
  lv_obj_t *fill;
  lv_obj_t *detail;
  lv_obj_t *status;
  usage_quota_scope scope;
  usage_provider provider;
} quota_page;

typedef struct {
  lv_obj_t *label;
  lv_obj_t *headline;
  lv_obj_t *detail;
} forecast_row;

typedef struct {
  lv_obj_t *tile;
  lv_obj_t *status;
  lv_obj_t *cells[TK_MT_DAYS];
  lv_obj_t *stats[4];
  bool codex;
} tracker_page;

typedef struct {
  lv_obj_t *tile;
  lv_obj_t *project;
  lv_obj_t *provenance;
  lv_obj_t *stars;
  lv_obj_t *forks;
} github_page;

typedef struct {
  lv_obj_t *tile;
  lv_obj_t *verdict;
  lv_obj_t *hero;
  lv_obj_t *attribution;
  lv_obj_t *track;
  lv_obj_t *fill[2];
  lv_obj_t *api;
  lv_obj_t *paid;
} value_page;

static struct {
  lv_obj_t *tiles;
  quota_page quotas[3];
  forecast_row forecasts[2];
  tracker_page trackers[2];
  github_page github;
  value_page value;
  tk_tokens tokens;
  tk_agent_snapshot agents;
  tk_max_tracker tracker;
  tk_github_status github_status;
  int64_t last_now_us;
  int current;
  bool stale;
  bool have_tokens;
  bool have_agents;
  bool have_tracker;
  bool have_github;
  lv_obj_t *detail_screen;
  lv_obj_t *detail_title;
  lv_obj_t *detail_left_label;
  lv_obj_t *detail_left_value;
  lv_obj_t *detail_left_reset;
  lv_obj_t *detail_right_label;
  lv_obj_t *detail_right_value;
  lv_obj_t *detail_right_reset;
  int detail_index;
} ui;

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                      int x, int y, int w, int h) {
  lv_obj_t *o = lv_label_create(parent);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, color, 0);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
  return o;
}

static lv_obj_t *bare(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t *new_tile(int index) {
  lv_dir_t dir = index == 0 ? LV_DIR_RIGHT :
                 index == TK_USAGE_SCREEN_VIEWS - 1 ? LV_DIR_LEFT : LV_DIR_HOR;
  lv_obj_t *tile = lv_tileview_add_tile(ui.tiles, index, 0, dir);
  lv_obj_set_style_bg_color(tile, COL_BLACK, 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  return tile;
}

static void create_pager(lv_obj_t *tile, int active) {
  const int dot = 4;
  const int active_w = 10;
  const int gap = 3;
  int width = (TK_USAGE_SCREEN_VIEWS - 1) * (dot + gap) + active_w;
  int x = (SCREEN_W - width) / 2;
  for (int i = 0; i < TK_USAGE_SCREEN_VIEWS; i++) {
    int w = i == active ? active_w : dot;
    lv_obj_t *mark = bare(tile);
    lv_obj_set_pos(mark, x, PAGER_Y);
    lv_obj_set_size(mark, w, dot);
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(mark, i == active ? COL_DOT_ON : COL_DOT, 0);
    x += w + gap;
  }
}

static void add_provider_icon(lv_obj_t *tile, bool claude) {
  lv_obj_t *icon = lv_image_create(tile);
  lv_image_set_src(icon, claude ? &tk_img_claude_32 : &tk_img_codex_32);
  lv_obj_set_pos(icon, 16, 8);
  if (claude) {
    lv_obj_set_style_image_recolor(icon, COL_CLAUDE, 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
  }
}

static void create_quota_page(quota_page *page, int index,
                              usage_quota_scope scope,
                              usage_provider provider) {
  bool claude = provider == USAGE_PROVIDER_CLAUDE;
  page->scope = scope;
  page->provider = provider;
  page->tile = new_tile(index);
  add_provider_icon(page->tile, claude);
  page->title = text(page->tile, &plex_ui_21, COL_WHITE, 56, 8, 150, 24);
  lv_label_set_text(page->title, claude ? "CLAUDE" : "CODEX");
  page->context = text(page->tile, &plex_ui_12, COL_META, 170, 12, 134, 16);
  lv_obj_set_style_text_align(page->context, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->context, "NO DATA");
  page->caption = text(page->tile, &plex_ui_12, COL_MUTED, 56, 32, 248, 16);
  page->percent = text(page->tile, &plex_num_50, COL_WHITE, 16, 50, 170, 52);
  lv_label_set_text(page->percent, "–");
  page->percent_suffix = text(page->tile, &plex_ui_21, COL_WHITE, 128, 72, 24, 24);
  page->track = bare(page->tile);
  lv_obj_set_pos(page->track, 16, 118);
  lv_obj_set_size(page->track, TRACK_W, 10);
  lv_obj_set_style_bg_color(page->track, COL_TRACK, 0);
  lv_obj_set_style_bg_opa(page->track, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(page->track, LV_RADIUS_CIRCLE, 0);
  page->fill = bare(page->track);
  lv_obj_set_size(page->fill, 0, 10);
  lv_obj_set_style_bg_color(page->fill, claude ? COL_CLAUDE : COL_CODEX, 0);
  lv_obj_set_style_bg_opa(page->fill, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(page->fill, LV_RADIUS_CIRCLE, 0);
  page->detail = text(page->tile, &plex_ui_16, COL_WHITE, 214, 104, 90, 22);
  lv_obj_set_style_text_align(page->detail, LV_TEXT_ALIGN_RIGHT, 0);
  page->status = text(page->tile, &plex_ui_12, COL_MUTED, 200, 128, 104, 16);
  lv_obj_set_style_text_align(page->status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->detail, "–");
  lv_label_set_text(page->status, "TO RESET");
  create_pager(page->tile, index);
}

static void apply_quota(quota_page *page) {
  usage_quota_page_view view = {0};
  usage_presenter_build_quota_page(&ui.tokens, page->scope, &view);
  lv_label_set_text(page->caption, view.quota.label);
  bool has = view.quota.has_pct;
  lv_obj_set_style_text_font(page->percent, has ? &plex_num_50 : &plex_ui_21, 0);
  lv_obj_set_pos(page->percent, 16, has ? 50 : 62);
  if (has) {
    char number[12];
    snprintf(number, sizeof number, "%.0f", view.quota.pct);
    lv_label_set_text(page->percent, number);
    lv_label_set_text(page->percent_suffix, "%");
    int fill = (int)(TRACK_W * view.quota.pct / 100.0);
    if (fill < 0) fill = 0;
    if (fill > TRACK_W) fill = TRACK_W;
    lv_obj_set_width(page->fill, fill);
  } else {
    lv_label_set_text(page->percent, view.quota.pct_text);
    lv_label_set_text(page->percent_suffix, "");
    lv_obj_set_width(page->fill, 0);
  }
  lv_label_set_text(page->detail,
                    view.quota.reset_short_text[0] ? view.quota.reset_short_text
                                                   : "–");
  usage_live_header_view header = {0};
  const tk_agent_provider_status *agent =
      page->provider == USAGE_PROVIDER_CLAUDE ? &ui.agents.claude
                                              : &ui.agents.codex;
  usage_live_build_header(agent, 0, ui.stale, ui.have_agents, &header);
  lv_label_set_text(page->context,
                    usage_presenter_quota_status_text(
                        ui.have_tokens && has, ui.stale || view.quota.stale,
                        header.context));
  if (view.quota.has_delta) lv_label_set_text(page->status, view.quota.delta_text);
  else lv_label_set_text(page->status, has ? "TO RESET" : "NO DATA");
}

static void create_burn_rate_page(void) {
  lv_obj_t *tile = new_tile(VIEW_BURN_RATE);
  lv_obj_t *heading = text(tile, &plex_ui_21, COL_WHITE, 16, 8, 180, 24);
  lv_label_set_text(heading, "BURN RATE");
  lv_obj_t *sub = text(tile, &plex_ui_12, COL_MUTED, 180, 12, 124, 16);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(sub, "WEEKLY FORECAST");
  for (int i = 0; i < 2; i++) {
    int y = 36 + i * 58;
    forecast_row *row = &ui.forecasts[i];
    row->label = text(tile, &plex_ui_12, i == 0 ? COL_CLAUDE : COL_CODEX,
                      16, y, 288, 16);
    row->headline = text(tile, &plex_ui_21, COL_WHITE, 16, y + 16, 288, 24);
    row->detail = text(tile, &plex_ui_12, COL_MUTED, 16, y + 40, 288, 16);
    lv_label_set_text(row->label, i == 0 ? "CLAUDE" : "CODEX");
    lv_label_set_text(row->headline, "–");
    lv_label_set_text(row->detail, "NO RELIABLE FORECAST");
  }
  create_pager(tile, VIEW_BURN_RATE);
}

static void apply_forecasts(void) {
  usage_forecast_page_view view = {0};
  usage_presenter_build_forecasts(&ui.tokens, &view);
  for (int i = 0; i < 2; i++) {
    const usage_forecast_row_view *row = &view.rows[i];
    lv_label_set_text(ui.forecasts[i].label, row->label[0] ? row->label :
                      (i == 0 ? "CLAUDE" : "CODEX"));
    if (!row->visible) {
      lv_label_set_text(ui.forecasts[i].headline, "–");
      lv_label_set_text(ui.forecasts[i].detail, "NO RELIABLE FORECAST");
      continue;
    }
    lv_label_set_text(ui.forecasts[i].headline, row->headline);
    lv_label_set_text(ui.forecasts[i].detail, row->detail);
  }
}

static lv_color_t tracker_color(bool codex, const tk_mt_day *day) {
  if (day->pct >= 0) {
    tk_mt_rgb rgb = tk_mt_cell_rgb(codex, day->pct);
    return lv_color_make(rgb.r, rgb.g, rgb.b);
  }
  if (day->lvl >= 0) {
    tk_mt_rgb rgb = tk_mt_gray_rgb(day->lvl);
    return lv_color_make(rgb.r, rgb.g, rgb.b);
  }
  return lv_color_hex(0x0C0E13);
}

static void create_tracker_page(tracker_page *page, int index, bool codex) {
  static const char *captions[] = {"STREAK", "MAX WKS", "AVG PEAK", "MAX DAYS"};
  page->codex = codex;
  page->tile = new_tile(index);
  add_provider_icon(page->tile, !codex);
  lv_obj_t *title = text(page->tile, &plex_ui_21, COL_WHITE, 56, 8, 150, 24);
  lv_label_set_text(title, codex ? "CODEX" : "CLAUDE");
  lv_obj_t *caption = text(page->tile, &plex_ui_12, COL_MUTED, 56, 32, 140, 16);
  lv_label_set_text(caption, "MAX TRACKER");
  page->status = text(page->tile, &plex_ui_12, COL_MUTED, 196, 12, 108, 16);
  lv_obj_set_style_text_align(page->status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->status, "NO DATA");
  for (int col = 0; col < TK_MT_WEEKS; ++col) {
    for (int row = 0; row < 7; ++row) {
      lv_obj_t *cell = lv_obj_create(page->tile);
      lv_obj_remove_style_all(cell);
      lv_obj_set_pos(cell, 16 + col * 14, 50 + row * 8);
      lv_obj_set_size(cell, 12, 6);
      lv_obj_set_style_radius(cell, 2, 0);
      page->cells[col * 7 + row] = cell;
    }
  }
  for (int i = 0; i < 4; ++i) {
    int x = 16 + i * 76;
    lv_obj_t *cap = text(page->tile, &plex_ui_12, COL_MUTED, x, 110, 72, 14);
    lv_label_set_text(cap, captions[i]);
    page->stats[i] = text(page->tile, &plex_ui_16, COL_WHITE, x, 124, 72, 20);
    lv_label_set_text(page->stats[i], "–");
  }
  create_pager(page->tile, index);
}

static void apply_tracker(tracker_page *page) {
  const tk_mt_provider *src = page->codex ? &ui.tracker.codex : &ui.tracker.claude;
  for (int i = 0; i < TK_MT_DAYS; ++i) {
    const tk_mt_day *day = &src->days[i];
    lv_obj_t *cell = page->cells[i];
    lv_obj_set_style_bg_color(cell, tracker_color(page->codex, day), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell, day->pct < 0 && day->lvl < 0 ? 1 : 0, 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(0x343A43), 0);
  }
  tk_mt_tile tiles[4];
  tk_mt_tiles(&ui.tracker, page->codex, tiles);
  for (int i = 0; i < 4; i++) {
    char value[24];
    if (tiles[i].unit[0])
      snprintf(value, sizeof value, "%s%s", tiles[i].value, tiles[i].unit);
    else
      snprintf(value, sizeof value, "%s", tiles[i].value);
    lv_label_set_text(page->stats[i], value);
  }
  lv_label_set_text(page->status, src->has_plan ? src->plan_label :
                    (src->has_avg ? "MAX TRACKER" : "LIVE ONLY"));
}

static void create_github_page(void) {
  github_page *page = &ui.github;
  page->tile = new_tile(VIEW_GITHUB);
  lv_obj_t *title = text(page->tile, &plex_ui_21, COL_WHITE, 16, 8, 160, 24);
  lv_label_set_text(title, "GITHUB");
  page->provenance = text(page->tile, &plex_ui_12, COL_MUTED, 180, 12, 124, 16);
  lv_obj_set_style_text_align(page->provenance, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->provenance, "WAITING");
  page->project = text(page->tile, &plex_ui_12, COL_STAR, 16, 32, 288, 16);
  lv_label_set_text(page->project, "NO REPOSITORY");
  page->stars = text(page->tile, &plex_num_50, COL_WHITE, 16, 52, 200, 52);
  lv_label_set_text(page->stars, "–");
  page->forks = text(page->tile, &plex_ui_16, COL_META, 16, 118, 288, 22);
  lv_label_set_text(page->forks, "FORKS  –");
  create_pager(page->tile, VIEW_GITHUB);
}

static void apply_github_page(const tk_github_status *status) {
  github_page *page = &ui.github;
  lv_label_set_text(page->project,
                    status->project[0] ? status->project : "NO REPOSITORY");
  lv_label_set_text(page->provenance,
                    !status->has_data ? "WAITING" :
                    status->stale ? "CACHED" : "LIVE");
  if (!status->has_data) {
    lv_label_set_text(page->stars, "–");
    lv_label_set_text(page->forks, "FORKS  –");
    return;
  }
  char stars[16], forks[32];
  snprintf(stars, sizeof stars, "%ld", (long)status->stars);
  snprintf(forks, sizeof forks, "FORKS  %ld", (long)status->forks);
  lv_label_set_text(page->stars, stars);
  lv_label_set_text(page->forks, forks);
}

static void create_value_page(void) {
  value_page *page = &ui.value;
  page->tile = new_tile(VIEW_VALUE);
  lv_obj_t *title = text(page->tile, &plex_ui_21, COL_WHITE, 16, 8, 140, 24);
  lv_label_set_text(title, "VALUE");
  lv_obj_t *sub = text(page->tile, &plex_ui_12, COL_MUTED, 160, 12, 144, 16);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(sub, "AT LIST API PRICES");
  page->verdict = text(page->tile, &plex_ui_12, COL_META, 16, 32, 288, 16);
  lv_label_set_text(page->verdict, "SET YOUR PLAN COST");
  page->hero = text(page->tile, &plex_ui_21, COL_WHITE, 16, 52, 288, 28);
  lv_label_set_text(page->hero, "–");
  page->attribution = text(page->tile, &plex_ui_12, COL_MUTED, 16, 84, 288, 16);
  lv_label_set_text(page->attribution, "");
  page->track = bare(page->tile);
  lv_obj_set_pos(page->track, 16, 106);
  lv_obj_set_size(page->track, 288, 8);
  lv_obj_set_style_bg_color(page->track, COL_TRACK, 0);
  lv_obj_set_style_bg_opa(page->track, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(page->track, LV_RADIUS_CIRCLE, 0);
  for (int i = 0; i < 2; i++) {
    page->fill[i] = bare(page->track);
    lv_obj_set_size(page->fill[i], 0, 8);
    lv_obj_set_style_bg_opa(page->fill[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(page->fill[i], LV_RADIUS_CIRCLE, 0);
  }
  page->api = text(page->tile, &plex_ui_12, COL_WHITE, 16, 122, 140, 16);
  page->paid = text(page->tile, &plex_ui_12, COL_WHITE, 164, 122, 140, 16);
  lv_obj_set_style_text_align(page->paid, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->api, "VIA API  –");
  lv_label_set_text(page->paid, "YOU PAID  –");
  create_pager(page->tile, VIEW_VALUE);
}

static void apply_value(void) {
  usage_value_page_view view = {0};
  usage_presenter_build_value(&ui.tokens, &view);
  lv_label_set_text(ui.value.verdict, view.verdict);
  lv_label_set_text(ui.value.hero, view.hero_text);
  lv_label_set_text(ui.value.attribution, view.attribution);
  char api[40], paid[40];
  snprintf(api, sizeof api, "VIA API  %s",
           view.api_cost[0] ? view.api_cost : "–");
  snprintf(paid, sizeof paid, "YOU PAID  %s",
           view.paid[0] ? view.paid : "–");
  lv_label_set_text(ui.value.api, api);
  lv_label_set_text(ui.value.paid, paid);
  int width = (int)(view.bar_fraction * 288.0 + 0.5);
  if (width > 288) width = 288;
  int x = 0;
  for (int i = 0; i < 2; i++) {
    const bool live = view.show_bar && i < view.row_count &&
                      view.rows[i].counted && view.rows[i].share > 0;
    if (!live) {
      lv_obj_set_pos(ui.value.fill[i], 0, 0);
      lv_obj_set_size(ui.value.fill[i], 0, 8);
      continue;
    }
    int seg = (int)(view.rows[i].share * width + 0.5);
    if (x + seg > width) seg = width - x;
    lv_obj_set_style_bg_color(ui.value.fill[i],
                              view.rows[i].provider == USAGE_PROVIDER_CLAUDE
                                  ? COL_CLAUDE : COL_CODEX, 0);
    lv_obj_set_pos(ui.value.fill[i], x, 0);
    lv_obj_set_size(ui.value.fill[i], seg, 8);
    x += seg;
  }
}

static void render_detail(void) {
  usage_quota_page_view view = {0};
  int index = ui.detail_index;
  if (index == VIEW_CLAUDE_FABLE) {
    usage_presenter_build_quota_page(&ui.tokens, USAGE_QUOTA_CLAUDE_MODEL, &view);
    lv_label_set_text(ui.detail_title, "CLAUDE MODEL WEEK");
  } else if (index == VIEW_CLAUDE_ALL) {
    usage_presenter_build_quota_page(&ui.tokens, USAGE_QUOTA_CLAUDE_ALL, &view);
    lv_label_set_text(ui.detail_title, "CLAUDE ALL MODELS");
  } else if (index == VIEW_CODEX_WEEKLY) {
    usage_presenter_build_quota_page(&ui.tokens, USAGE_QUOTA_CODEX_WEEK, &view);
    lv_label_set_text(ui.detail_title, "CODEX WEEKLY");
  } else {
    return;
  }
  const tk_limit *session = index == VIEW_CODEX_WEEKLY
                                ? &ui.tokens.codex_session
                                : &ui.tokens.claude_session;
  char session_pct[16], session_reset[32];
  if (session->has_pct) snprintf(session_pct, sizeof session_pct, "%.0f%%",
                                 session->pct);
  else snprintf(session_pct, sizeof session_pct, "–");
  if (session->has_reset)
    snprintf(session_reset, sizeof session_reset, "%dd %02dh",
             session->reset_min / 1440, (session->reset_min / 60) % 24);
  else snprintf(session_reset, sizeof session_reset, "–");
  lv_label_set_text(ui.detail_left_label, view.quota.label);
  lv_label_set_text(ui.detail_left_value, view.quota.pct_text);
  lv_label_set_text(ui.detail_left_reset, view.quota.reset_short_text);
  lv_label_set_text(ui.detail_right_label, "SESSION");
  lv_label_set_text(ui.detail_right_value, session_pct);
  lv_label_set_text(ui.detail_right_reset, session_reset);
}

void usage_screen_create(lv_obj_t *root) {
  memset(&ui, 0, sizeof ui);
  ui.tiles = lv_tileview_create(root);
  lv_obj_set_size(ui.tiles, SCREEN_W, SCREEN_H);
  lv_obj_set_scrollbar_mode(ui.tiles, LV_SCROLLBAR_MODE_OFF);
  create_quota_page(&ui.quotas[0], VIEW_CLAUDE_FABLE,
                    USAGE_QUOTA_CLAUDE_MODEL, USAGE_PROVIDER_CLAUDE);
  create_quota_page(&ui.quotas[1], VIEW_CLAUDE_ALL,
                    USAGE_QUOTA_CLAUDE_ALL, USAGE_PROVIDER_CLAUDE);
  create_quota_page(&ui.quotas[2], VIEW_CODEX_WEEKLY,
                    USAGE_QUOTA_CODEX_WEEK, USAGE_PROVIDER_CODEX);
  create_burn_rate_page();
  create_tracker_page(&ui.trackers[0], VIEW_TRACKER_CLAUDE, false);
  create_tracker_page(&ui.trackers[1], VIEW_TRACKER_CODEX, true);
  create_github_page();
  create_value_page();

  ui.detail_screen = lv_obj_create(root);
  lv_obj_remove_style_all(ui.detail_screen);
  lv_obj_set_size(ui.detail_screen, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(ui.detail_screen, COL_BLACK, 0);
  lv_obj_set_style_bg_opa(ui.detail_screen, LV_OPA_COVER, 0);
  ui.detail_title = text(ui.detail_screen, &plex_ui_21, COL_WHITE, 16, 12, 288, 24);
  ui.detail_left_label = text(ui.detail_screen, &plex_ui_12, COL_MUTED, 16, 48, 140, 16);
  ui.detail_left_value = text(ui.detail_screen, &plex_ui_21, COL_WHITE, 16, 66, 140, 28);
  ui.detail_left_reset = text(ui.detail_screen, &plex_ui_12, COL_META, 16, 98, 140, 16);
  ui.detail_right_label = text(ui.detail_screen, &plex_ui_12, COL_MUTED, 168, 48, 136, 16);
  ui.detail_right_value = text(ui.detail_screen, &plex_ui_21, COL_WHITE, 168, 66, 136, 28);
  ui.detail_right_reset = text(ui.detail_screen, &plex_ui_12, COL_META, 168, 98, 136, 16);
  lv_obj_t *hint = text(ui.detail_screen, &plex_ui_12, COL_MUTED, 16, 140, 288, 16);
  lv_label_set_text(hint, "SHORT PRESS: BACK");
  lv_obj_add_flag(ui.detail_screen, LV_OBJ_FLAG_HIDDEN);

#if TK_GITHUB_NOTIFICATIONS_ENABLED
  tk_project_star_popup_create(root);
#endif
  tk_agent_monitor_create(root);
  usage_screen_apply_tokens(&ui.tokens);
}

void usage_screen_apply_tokens(const tk_tokens *tokens) {
  if (!tokens) return;
  ui.tokens = *tokens;
  ui.have_tokens = true;
  for (int i = 0; i < 3; i++) apply_quota(&ui.quotas[i]);
  apply_forecasts();
  apply_value();
}

void usage_screen_apply_agent(const tk_agent_snapshot *snapshot, int64_t now_us) {
  if (!snapshot) return;
  ui.agents = *snapshot;
  ui.have_agents = true;
  ui.last_now_us = now_us;
  for (int i = 0; i < 3; i++) apply_quota(&ui.quotas[i]);
  tk_agent_monitor_apply(snapshot, now_us);
}

void usage_screen_apply_max_tracker(const tk_max_tracker *t) {
  if (!t) return;
  ui.tracker = *t;
  ui.have_tracker = true;
  apply_tracker(&ui.trackers[0]);
  apply_tracker(&ui.trackers[1]);
}

void usage_screen_apply_github(const tk_github_status *status) {
  if (!status) return;
  ui.github_status = *status;
  ui.have_github = true;
  apply_github_page(status);
#if TK_GITHUB_NOTIFICATIONS_ENABLED
  if (status->enabled && status->has_event && !status->stale) {
    tk_project_star_event event = {0};
    snprintf(event.id, sizeof event.id, "%s", status->event_id);
    snprintf(event.source, sizeof event.source, "GITHUB");
    snprintf(event.repo, sizeof event.repo, "%s", status->repo);
    snprintf(event.project, sizeof event.project, "%s", status->project);
    event.stars = status->event_stars;
    event.has_actor = status->has_actor;
    if (status->has_actor)
      snprintf(event.actor, sizeof event.actor, "%s", status->actor);
    if (tk_project_star_popup_show(&event, ui.last_now_us)) {
      torget_keep_awake();
      (void)tk_project_star_chime_request();
    }
  }
#endif
}

void usage_screen_tick(int64_t now_us) {
  ui.last_now_us = now_us;
  tk_agent_monitor_tick(now_us);
#if TK_GITHUB_NOTIFICATIONS_ENABLED
  tk_project_star_popup_tick(now_us);
#endif
}

void usage_screen_set_stale(bool stale) {
  ui.stale = stale;
  if (ui.have_tokens) usage_screen_apply_tokens(&ui.tokens);
}

void usage_screen_show_view(int index) {
  if (index < 0) index = 0;
  if (index >= TK_USAGE_SCREEN_VIEWS) index = TK_USAGE_SCREEN_VIEWS - 1;
  ui.current = index;
  lv_obj_set_tile_id(ui.tiles, ui.current, 0, LV_ANIM_OFF);
}

int usage_screen_current_view(void) { return ui.current; }

void usage_screen_show_detail(int index) {
  if (index != VIEW_CLAUDE_FABLE && index != VIEW_CLAUDE_ALL &&
      index != VIEW_CODEX_WEEKLY) return;
  ui.detail_index = index;
  render_detail();
  lv_obj_remove_flag(ui.detail_screen, LV_OBJ_FLAG_HIDDEN);
}

void usage_screen_hide_detail(void) {
  lv_obj_add_flag(ui.detail_screen, LV_OBJ_FLAG_HIDDEN);
}
