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
#define COL_GROK lv_color_hex(0x33E1ED)
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
  lv_obj_t *grid;
  lv_obj_t *stats[4];
  int kind; /* 0 claude, 1 codex, 2 grok */
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
  quota_page quotas[4];
  forecast_row forecasts[3];
  tracker_page trackers[3];
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

static void add_provider_icon(lv_obj_t *tile, usage_provider provider) {
  if (provider == USAGE_PROVIDER_GROK) {
    lv_obj_t *icon = lv_obj_create(tile);
    lv_obj_remove_style_all(icon);
    lv_obj_set_pos(icon, 16, 8);
    lv_obj_set_size(icon, 32, 32);
    lv_obj_set_style_bg_color(icon, COL_GROK, 0);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *mark = text(icon, &plex_ui_16, lv_color_hex(0x050608), 0, 6, 32, 20);
    lv_label_set_text(mark, "G");
    lv_obj_set_style_text_align(mark, LV_TEXT_ALIGN_CENTER, 0);
    return;
  }
  lv_obj_t *icon = lv_image_create(tile);
  lv_image_set_src(icon, provider == USAGE_PROVIDER_CLAUDE ? &tk_img_claude_32
                                                           : &tk_img_codex_32);
  lv_obj_set_pos(icon, 16, 8);
  if (provider == USAGE_PROVIDER_CLAUDE) {
    lv_obj_set_style_image_recolor(icon, COL_CLAUDE, 0);
    lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);
  }
}

static void create_quota_page(quota_page *page, int index,
                              usage_quota_scope scope,
                              usage_provider provider) {
  page->scope = scope;
  page->provider = provider;
  page->tile = new_tile(index);
  add_provider_icon(page->tile, provider);
  page->title = text(page->tile, &plex_ui_21, COL_WHITE, 56, 8, 150, 24);
  lv_label_set_text(page->title,
                    provider == USAGE_PROVIDER_CLAUDE ? "CLAUDE" :
                    provider == USAGE_PROVIDER_GROK ? "GROK" : "CODEX");
  page->context = text(page->tile, &plex_ui_12, COL_META, 170, 12, 134, 16);
  lv_obj_set_style_text_align(page->context, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->context, "NO DATA");
  page->caption = text(page->tile, &plex_ui_12, COL_MUTED, 56, 32, 248, 16);
  page->percent = text(page->tile, &plex_num_50, COL_WHITE, 16, 50, 200, 52);
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
  lv_obj_set_style_bg_color(page->fill,
                            provider == USAGE_PROVIDER_CLAUDE ? COL_CLAUDE :
                            provider == USAGE_PROVIDER_GROK ? COL_GROK : COL_CODEX, 0);
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
  bool grok_volume = page->provider == USAGE_PROVIDER_GROK &&
                     ui.tokens.has_grok_day_tokens && !has;
  lv_obj_set_style_text_font(page->percent, has ? &plex_num_50 : &plex_ui_21, 0);
  lv_obj_set_pos(page->percent, 16, has ? 50 : 62);
  if (has) {
    char number[12];
    snprintf(number, sizeof number, "%.0f", view.quota.pct);
    lv_label_set_text(page->percent, number);
    lv_label_set_text(page->percent_suffix, "%");
    int suffix_x = 20 + (int)strlen(number) * 28;
    lv_obj_set_pos(page->percent_suffix, suffix_x, 72);
    int fill = (int)(TRACK_W * view.quota.pct / 100.0);
    if (fill < 0) fill = 0;
    if (fill > TRACK_W) fill = TRACK_W;
    lv_obj_set_width(page->fill, fill);
  } else if (grok_volume) {
    char volume[16];
    double tokens = ui.tokens.grok_day_tokens;
    if (tokens >= 1000000.0) snprintf(volume, sizeof volume, "%.1fM", tokens / 1e6);
    else if (tokens >= 1000.0) snprintf(volume, sizeof volume, "%.0fK", tokens / 1000.0);
    else snprintf(volume, sizeof volume, "%.0f", tokens);
    lv_label_set_text(page->percent, volume);
    lv_label_set_text(page->percent_suffix, "");
    lv_label_set_text(page->caption, "TODAY TOKENS");
    lv_obj_set_width(page->fill, 0);
    if (ui.tokens.has_grok_month_tokens) {
      char month[16];
      double month_tokens = ui.tokens.grok_month_tokens;
      if (month_tokens >= 1000000.0)
        snprintf(month, sizeof month, "%.1fM", month_tokens / 1e6);
      else if (month_tokens >= 1000.0)
        snprintf(month, sizeof month, "%.0fK", month_tokens / 1000.0);
      else
        snprintf(month, sizeof month, "%.0f", month_tokens);
      lv_label_set_text(page->detail, month);
    }
    if (ui.tokens.has_grok_model)
      lv_label_set_text(page->status, ui.tokens.grok_model);
    else
      lv_label_set_text(page->status, "LOCAL SESSIONS");
  } else {
    lv_label_set_text(page->percent, "NO DATA");
    lv_label_set_text(page->percent_suffix, "");
    lv_obj_set_width(page->fill, 0);
  }
  if (!grok_volume) {
    lv_label_set_text(page->detail,
                      view.quota.reset_short_text[0] ? view.quota.reset_short_text
                                                     : "–");
  }
  usage_live_header_view header = {0};
  const tk_agent_provider_status *agent =
      page->provider == USAGE_PROVIDER_CLAUDE ? &ui.agents.claude :
      page->provider == USAGE_PROVIDER_GROK ? &ui.agents.grok :
                                              &ui.agents.codex;
  usage_live_build_header(agent, 0, ui.stale, ui.have_agents, &header);
  lv_label_set_text(page->context,
                    usage_presenter_quota_status_text(
                        ui.have_tokens && (has || grok_volume),
                        ui.stale || view.quota.stale,
                        header.context));
  if (grok_volume) {
    /* month + model already set */
  } else if (view.quota.has_delta) {
    lv_label_set_text(page->status, view.quota.delta_text);
  } else {
    lv_label_set_text(page->status, has ? "TO RESET" : "NO DATA");
  }
}

static void create_burn_rate_page(void) {
  lv_obj_t *tile = new_tile(4);
  lv_obj_t *heading = text(tile, &plex_ui_21, COL_WHITE, 16, 8, 180, 24);
  lv_label_set_text(heading, "BURN RATE");
  lv_obj_t *sub = text(tile, &plex_ui_12, COL_MUTED, 180, 12, 124, 16);
  lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(sub, "WEEKLY FORECAST");
  static const uint32_t row_color[] = {0xD97757, 0x6F78FF, 0x33E1ED};
  static const char *row_name[] = {"CLAUDE", "CODEX", "GROK"};
  for (int i = 0; i < 3; i++) {
    int y = 32 + i * 40;
    forecast_row *row = &ui.forecasts[i];
    row->label = text(tile, &plex_ui_12, lv_color_hex(row_color[i]),
                      16, y, 288, 14);
    row->headline = text(tile, &plex_ui_16, COL_WHITE, 16, y + 14, 288, 18);
    row->detail = text(tile, &plex_ui_12, COL_MUTED, 180, y + 14, 124, 18);
    lv_obj_set_style_text_align(row->detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(row->label, row_name[i]);
    lv_label_set_text(row->headline, "–");
    lv_label_set_text(row->detail, "NO RELIABLE FORECAST");
  }
  create_pager(tile, 4);
}

static void apply_forecasts(void) {
  usage_forecast_page_view view = {0};
  usage_presenter_build_forecasts(&ui.tokens, &view);
  for (int i = 0; i < 3; i++) {
    const usage_forecast_row_view *row = &view.rows[i];
    lv_label_set_text(ui.forecasts[i].label, row->label[0] ? row->label :
                      (i == 0 ? "CLAUDE" : i == 1 ? "CODEX" : "GROK"));
    if (!row->visible) {
      lv_label_set_text(ui.forecasts[i].headline, "–");
      lv_label_set_text(ui.forecasts[i].detail, "NO RELIABLE FORECAST");
      continue;
    }
    lv_label_set_text(ui.forecasts[i].headline, row->headline);
    lv_label_set_text(ui.forecasts[i].detail, row->detail);
  }
}

static lv_color_t tracker_color(int kind, const tk_mt_day *day) {
  if (day->pct >= 0) {
    tk_mt_rgb rgb = tk_mt_cell_rgb(kind == 1, day->pct);
    if (kind == 2) return lv_color_hex(day->pct >= 100 ? 0xFF2D1F : 0x33E1ED);
    return lv_color_make(rgb.r, rgb.g, rgb.b);
  }
  if (day->lvl >= 0) {
    /* Studio greys vanish on 6 px Mini cells. Lift volume floors per
     * provider so Claude/Grok activity is visible beside Codex quota. */
    static const uint32_t claude_vol[] = {0x4A2A24, 0x6E3D34, 0x8F5346};
    static const uint32_t codex_vol[] = {0x2A2E58, 0x3C447C, 0x535CAD};
    static const uint32_t grok_vol[] = {0x0E4A4E, 0x147A80, 0x1BA8B0};
    int lvl = day->lvl;
    if (lvl > 2) lvl = 2;
    if (kind == 1) return lv_color_hex(codex_vol[lvl]);
    if (kind == 2) return lv_color_hex(grok_vol[lvl]);
    return lv_color_hex(claude_vol[lvl]);
  }
  return lv_color_hex(0x1A1C22);
}

static void tracker_grid_draw(lv_event_t *e) {
  const tracker_page *page = lv_event_get_user_data(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t origin;
  lv_obj_get_coords(lv_event_get_target(e), &origin);
  const tk_mt_provider *src = page->kind == 1 ? &ui.tracker.codex :
                              page->kind == 2 ? &ui.tracker.grok :
                                                &ui.tracker.claude;
  for (int i = 0; i < TK_MT_DAYS; i++) {
    const tk_mt_day *day = &src->days[i];
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = 2;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = tracker_color(page->kind, day);
    if (day->pct < 0 && day->lvl < 0) {
      dsc.border_width = 1;
      dsc.border_opa = LV_OPA_COVER;
      dsc.border_color = lv_color_hex(0x343A43);
    }
    int col = i / 7;
    int row = i % 7;
    lv_area_t cell = {origin.x1 + col * 14, origin.y1 + row * 8,
                      origin.x1 + col * 14 + 11, origin.y1 + row * 8 + 5};
    lv_draw_rect(layer, &dsc, &cell);
  }
}

static void create_tracker_page(tracker_page *page, int index, int kind) {
  static const char *captions[] = {"STREAK", "MAX WKS", "AVG PEAK", "MAX DAYS"};
  static const char *names[] = {"CLAUDE", "CODEX", "GROK"};
  page->kind = kind;
  page->tile = new_tile(index);
  add_provider_icon(page->tile, kind == 0 ? USAGE_PROVIDER_CLAUDE :
                                kind == 2 ? USAGE_PROVIDER_GROK :
                                            USAGE_PROVIDER_CODEX);
  lv_obj_t *title = text(page->tile, &plex_ui_21, COL_WHITE, 56, 8, 150, 24);
  lv_label_set_text(title, names[kind]);
  lv_obj_t *caption = text(page->tile, &plex_ui_12, COL_MUTED, 56, 32, 140, 16);
  lv_label_set_text(caption, "MAX TRACKER");
  page->status = text(page->tile, &plex_ui_12, COL_MUTED, 196, 12, 108, 16);
  lv_obj_set_style_text_align(page->status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(page->status, "NO DATA");
  /* One widget, 140 rects in DRAW_MAIN — 420 lv_obj cells tripped the
   * task watchdog on boot (white ST7789, main stuck in theme_apply). */
  page->grid = bare(page->tile);
  lv_obj_set_pos(page->grid, 16, 50);
  lv_obj_set_size(page->grid, TK_MT_WEEKS * 14, 7 * 8);
  lv_obj_add_event_cb(page->grid, tracker_grid_draw, LV_EVENT_DRAW_MAIN, page);
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
  const tk_mt_provider *src = page->kind == 1 ? &ui.tracker.codex :
                              page->kind == 2 ? &ui.tracker.grok :
                                                &ui.tracker.claude;
  lv_obj_invalidate(page->grid);
  char value[16];
  snprintf(value, sizeof value, "%d",
           ui.tracker.coding_streak_days < 0 ? 0 : ui.tracker.coding_streak_days);
  lv_label_set_text(page->stats[0], value);
  snprintf(value, sizeof value, "%d", src->max_weeks);
  lv_label_set_text(page->stats[1], value);
  if (src->has_avg) snprintf(value, sizeof value, "%.0f%%", src->avg_peak_pct);
  else snprintf(value, sizeof value, "-");
  lv_label_set_text(page->stats[2], value);
  snprintf(value, sizeof value, "%d", src->max_days);
  lv_label_set_text(page->stats[3], value);
  lv_label_set_text(page->status, src->has_plan ? src->plan_label :
                    (src->has_avg ? "MAX TRACKER" : "LIVE ONLY"));
}

static void create_github_page(void) {
  github_page *page = &ui.github;
  page->tile = new_tile(8);
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
  create_pager(page->tile, 8);
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
  page->tile = new_tile(9);
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
  create_pager(page->tile, 9);
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
  create_quota_page(&ui.quotas[2], 2,
                    USAGE_QUOTA_CODEX_WEEK, USAGE_PROVIDER_CODEX);
  create_quota_page(&ui.quotas[3], 3,
                    USAGE_QUOTA_GROK_WEEK, USAGE_PROVIDER_GROK);
  create_burn_rate_page();
  create_tracker_page(&ui.trackers[0], 5, 0);
  create_tracker_page(&ui.trackers[1], 6, 1);
  create_tracker_page(&ui.trackers[2], 7, 2);
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
  for (int i = 0; i < 4; i++) apply_quota(&ui.quotas[i]);
  apply_forecasts();
  apply_value();
}

void usage_screen_apply_agent(const tk_agent_snapshot *snapshot, int64_t now_us) {
  if (!snapshot) return;
  ui.agents = *snapshot;
  ui.have_agents = true;
  ui.last_now_us = now_us;
  for (int i = 0; i < 4; i++) apply_quota(&ui.quotas[i]);
  tk_agent_monitor_apply(snapshot, now_us);
}

void usage_screen_apply_max_tracker(const tk_max_tracker *t) {
  if (!t) return;
  ui.tracker = *t;
  ui.have_tracker = true;
  apply_tracker(&ui.trackers[0]);
  apply_tracker(&ui.trackers[1]);
  apply_tracker(&ui.trackers[2]);
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
