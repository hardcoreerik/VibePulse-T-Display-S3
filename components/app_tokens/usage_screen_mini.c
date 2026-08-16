#include "usage_screen.h"

#include <stdio.h>
#include <string.h>

#include "torget.h"
#include "agent_assets.h"
#include "max_tracker.h"

extern const lv_font_t plex_num_50;
extern const lv_font_t plex_stat_35;
extern const lv_font_t plex_text_32;
extern const lv_font_t plex_ui_21;
extern const lv_font_t plex_ui_16;
extern const lv_font_t plex_ui_14;
extern const lv_font_t plex_ui_12;

#define SCREEN_W 320
#define SCREEN_H 170
#define PAGE_COUNT 6
#define TRACKER_FIRST_PAGE 4

typedef struct {
  lv_obj_t *tile;
  lv_obj_t *icon;
  lv_obj_t *icon_mark;
  lv_obj_t *title;
  lv_obj_t *caption;
  lv_obj_t *percent;
  lv_obj_t *percent_suffix;
  lv_obj_t *track;
  lv_obj_t *fill;
  lv_obj_t *detail;
  lv_obj_t *status;
} mini_page;

static struct {
  lv_obj_t *tiles;
  mini_page pages[PAGE_COUNT];
  int current;
  bool stale;
  tk_tokens tokens;
  tk_agent_snapshot agents;
  bool have_tokens;
  bool have_agents;
  lv_obj_t *detail_screen;
  lv_obj_t *detail_title;
  lv_obj_t *detail_left_label;
  lv_obj_t *detail_left_value;
  lv_obj_t *detail_left_reset;
  lv_obj_t *detail_right_label;
  lv_obj_t *detail_right_value;
  lv_obj_t *detail_right_reset;
  lv_obj_t *detail_hint;
  int detail_index;
  tk_max_tracker tracker;
  bool have_tracker;
  lv_obj_t *tracker_cells[2][TK_MT_DAYS];
  lv_obj_t *tracker_stats[2][4];
} ui;

static void format_tokens(double tokens, char *out, size_t cap) {
  if (tokens >= 1000000.0) snprintf(out, cap, "%.2fM", tokens / 1000000.0);
  else if (tokens >= 1000.0) snprintf(out, cap, "%.1fK", tokens / 1000.0);
  else snprintf(out, cap, "%.0f", tokens);
}

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

static void format_reset(const tk_limit *limit, char *out, size_t cap) {
  if (!limit->has_reset) {
    snprintf(out, cap, "RESET  -");
    return;
  }
  snprintf(out, cap, "RESET  %dd %02dh", limit->reset_min / 1440,
           (limit->reset_min / 60) % 24);
}

static void format_reset_time(const tk_limit *limit, char *out, size_t cap) {
  if (!limit->has_reset) {
    snprintf(out, cap, "-");
    return;
  }
  snprintf(out, cap, "%dd %02dh", limit->reset_min / 1440,
           (limit->reset_min / 60) % 24);
}

static void render_quota(int index, const char *name, const tk_limit *limit,
                         lv_color_t accent) {
  mini_page *p = &ui.pages[index];
  char value[12], reset[16], status[24];
  if (limit->has_pct) snprintf(value, sizeof value, "%.0f", limit->pct);
  else snprintf(value, sizeof value, "NO USAGE");
  format_reset_time(limit, reset, sizeof reset);
  lv_label_set_text(p->title, name);
  lv_obj_set_style_text_font(p->percent, limit->has_pct ? &plex_num_50 : &plex_ui_21, 0);
  lv_obj_set_pos(p->percent, 20, limit->has_pct ? 54 : 66);
  lv_obj_set_size(p->percent, 200, limit->has_pct ? 52 : 28);
  lv_obj_set_pos(p->percent_suffix, 130, 76);
  lv_label_set_text(p->caption, limit->has_pct ? "WEEKLY LIMIT" : "CLAUDE USAGE");
  lv_obj_remove_flag(p->caption, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(p->percent, value);
  lv_label_set_text(p->percent_suffix, limit->has_pct ? "%" : "");
  lv_obj_remove_flag(p->track, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(p->detail, reset);
  lv_obj_set_style_bg_color(p->fill, accent, 0);
  lv_obj_set_width(p->fill, limit->has_pct ? (int)(180 * limit->pct / 100.0) : 0);
  lv_obj_set_pos(p->detail, 214, 104);
  lv_obj_set_size(p->detail, 90, 27);
  lv_obj_set_pos(p->status, 214, 132);
  lv_obj_set_size(p->status, 90, 20);
  lv_label_set_text(p->detail, reset);
  if (limit->has_delta) snprintf(status, sizeof status, "%+.0f%% TODAY", limit->delta_pct);
  else snprintf(status, sizeof status, "%s", ui.stale || limit->stale ? "CACHED" : "TO RESET");
  lv_label_set_text(p->status, limit->has_pct ? status : "ADD USAGE DATA");
}

static const char *state_name(tk_agent_state state) {
  switch (state) {
    case TK_AGENT_WAITING: return "NEEDS YOU";
    case TK_AGENT_WORKING: return "WORKING";
    case TK_AGENT_DONE: return "DONE";
    case TK_AGENT_ERROR: return "ERROR";
    default: return "IDLE";
  }
}

static void render_activity(void) {
  mini_page *p = &ui.pages[2];
  const tk_agent_status *job = NULL;
  const char *provider = "NO ACTIVE AGENTS";
  if (ui.have_agents) {
    job = tk_agent_provider_primary(&ui.agents.claude);
    if (job) provider = "CLAUDE";
    const tk_agent_status *codex = tk_agent_provider_primary(&ui.agents.codex);
    if (codex && (!job || codex->state == TK_AGENT_WAITING ||
                  (codex->state == TK_AGENT_WORKING && job->state != TK_AGENT_WAITING))) {
      job = codex;
      provider = "CODEX";
    }
  }
  lv_label_set_text(p->title, provider);
  /* plex_text_32 is a deliberately limited display face.  Agent states and
   * remote project names need the complete UI font or they show replacement
   * boxes on the T-Display-S3. */
  lv_obj_set_style_text_font(p->percent, &plex_ui_21, 0);
  lv_obj_set_pos(p->percent, 20, 57);
  lv_obj_set_size(p->percent, 280, 28);
  lv_label_set_text(p->percent, job ? state_name(job->state) : "-");
  lv_label_set_text(p->percent_suffix, "");
  lv_obj_add_flag(p->caption, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(p->track, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_width(p->fill, 0);
  /* Both fields are intentionally full-width.  The old quota-page positions
   * (x=214, width=90) caused project text to overpaint model/status text. */
  lv_obj_set_style_text_font(p->detail, &plex_ui_14, 0);
  lv_obj_set_pos(p->detail, 20, 98);
  lv_obj_set_size(p->detail, 280, 20);
  lv_obj_set_style_text_align(p->detail, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_pos(p->status, 20, 124);
  lv_obj_set_size(p->status, 280, 18);
  lv_obj_set_style_text_align(p->status, LV_TEXT_ALIGN_LEFT, 0);
  lv_label_set_text(p->detail, job && job->project[0] ? job->project : "NO PROJECT DATA");
  lv_label_set_text(p->status, job && job->has_model ? job->model : "AGENT STATUS");
}

static void render_metrics(void) {
  mini_page *p = &ui.pages[3];
  char month[16], today[16], rate[16], sessions[20], today_label[24], footer[40];
  format_tokens(ui.tokens.month_tokens, month, sizeof month);
  format_tokens(ui.tokens.day_tokens, today, sizeof today);
  format_tokens(ui.tokens.day_tokens_per_hour, rate, sizeof rate);
  snprintf(sessions, sizeof sessions, "%d SESSIONS", ui.tokens.day_sessions);
  lv_label_set_text(p->title, "METRICS / MONTH");
  /* Month totals carry K/M suffixes, which the numeral font intentionally
   * does not contain.  Use the complete UI font rather than missing glyphs. */
  lv_obj_set_style_text_font(p->percent, &plex_ui_21, 0);
  lv_obj_set_pos(p->percent, 20, 69);
  lv_obj_set_size(p->percent, 180, 28);
  lv_label_set_text(p->percent, month);
  lv_label_set_text(p->percent_suffix, "");
  lv_obj_add_flag(p->caption, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(p->track, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(p->detail, 20, 128);
  lv_obj_set_size(p->detail, 130, 20);
  lv_obj_set_pos(p->status, 156, 128);
  lv_obj_set_size(p->status, 144, 20);
  snprintf(today_label, sizeof today_label, "TODAY %s", today);
  lv_label_set_text(p->detail, today_label);
  snprintf(footer, sizeof footer, "%s  %s/H", sessions, rate);
  lv_label_set_text(p->status, footer);
}

static lv_color_t tracker_color(bool codex, const tk_mt_day *day) {
  if (day->pct >= 100) return lv_color_hex(0xFF4E40);
  if (day->pct >= 75) return codex ? lv_color_hex(0x6F78FF) : lv_color_hex(0xD97757);
  if (day->pct >= 50) return codex ? lv_color_hex(0x454B8A) : lv_color_hex(0x8A4F42);
  if (day->pct >= 0) return lv_color_hex(0x2C3038);
  if (day->lvl >= 1) return lv_color_hex(0x30353D);
  return lv_color_hex(0x0C0E13);
}

static void render_tracker(int tracker_index) {
  const tk_mt_provider *src = tracker_index == 0 ? &ui.tracker.claude : &ui.tracker.codex;
  mini_page *p = &ui.pages[TRACKER_FIRST_PAGE + tracker_index];
  char value[16];
  for (int i = 0; i < TK_MT_DAYS; ++i) {
    lv_obj_t *cell = ui.tracker_cells[tracker_index][i];
    const tk_mt_day *day = &src->days[i];
    lv_obj_set_style_bg_color(cell, tracker_color(tracker_index != 0, day), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cell, day->pct < 0 && day->lvl < 0 ? 1 : 0, 0);
    lv_obj_set_style_border_color(cell, lv_color_hex(0x343A43), 0);
  }
  snprintf(value, sizeof value, "%d", ui.tracker.coding_streak_days < 0 ? 0 : ui.tracker.coding_streak_days);
  lv_label_set_text(ui.tracker_stats[tracker_index][0], value);
  snprintf(value, sizeof value, "%d", src->max_weeks);
  lv_label_set_text(ui.tracker_stats[tracker_index][1], value);
  snprintf(value, sizeof value, "%s", src->has_avg ? "" : "-");
  if (src->has_avg) snprintf(value, sizeof value, "%.0f%%", src->avg_peak_pct);
  lv_label_set_text(ui.tracker_stats[tracker_index][2], value);
  snprintf(value, sizeof value, "%d", src->max_days);
  lv_label_set_text(ui.tracker_stats[tracker_index][3], value);
  lv_label_set_text(p->status, src->has_plan ? src->plan_label :
                    (src->has_avg ? "MAX TRACKER" : "NO HISTORY"));
}

static void create_tracker_page(int index) {
  int tracker_index = index - TRACKER_FIRST_PAGE;
  bool codex = tracker_index != 0;
  mini_page *p = &ui.pages[index];
  static const char *captions[] = {"STREAK", "MAX WKS", "AVG PEAK", "MAX DAYS"};
  p->tile = lv_tileview_add_tile(ui.tiles, index, 0, LV_DIR_HOR);
  lv_obj_set_style_bg_color(p->tile, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(p->tile, LV_OPA_COVER, 0);
  p->icon = lv_image_create(p->tile);
  lv_image_set_src(p->icon, codex ? &tk_img_codex_32 : &tk_img_claude_32);
  lv_obj_set_pos(p->icon, 18, 8);
  if (!codex) {
    lv_obj_set_style_image_recolor(p->icon, lv_color_hex(0xD97757), 0);
    lv_obj_set_style_image_recolor_opa(p->icon, LV_OPA_COVER, 0);
  }
  p->title = text(p->tile, &plex_ui_21, lv_color_white(), 58, 12, 160, 28);
  lv_label_set_text(p->title, codex ? "CODEX" : "CLAUDE");
  p->caption = text(p->tile, &plex_ui_21, lv_color_hex(0xB2B7C0), 20, 43, 200, 26);
  lv_label_set_text(p->caption, "MAX TRACKER");
  p->status = text(p->tile, &plex_ui_14, lv_color_hex(0x9298A2), 212, 18, 88, 20);
  lv_obj_set_style_text_align(p->status, LV_TEXT_ALIGN_RIGHT, 0);
  for (int col = 0; col < TK_MT_WEEKS; ++col) {
    for (int row = 0; row < 7; ++row) {
      int cell_index = col * 7 + row;
      lv_obj_t *cell = lv_obj_create(p->tile);
      lv_obj_remove_style_all(cell);
      lv_obj_set_pos(cell, 32 + col * 13, 73 + row * 8);
      lv_obj_set_size(cell, 11, 6);
      lv_obj_set_style_radius(cell, 2, 0);
      ui.tracker_cells[tracker_index][cell_index] = cell;
    }
  }
  for (int i = 0; i < 4; ++i) {
    int x = 20 + i * 75;
    lv_obj_t *caption = text(p->tile, &plex_ui_12, lv_color_hex(0x9298A2), x, 137, 70, 16);
    lv_label_set_text(caption, captions[i]);
    ui.tracker_stats[tracker_index][i] = text(p->tile, &plex_ui_21, lv_color_white(), x, 151, 70, 22);
    lv_label_set_text(ui.tracker_stats[tracker_index][i], "-");
  }
  render_tracker(tracker_index);
}

static void detail_limit(const tk_limit *limit, char *value, size_t value_cap,
                         char *reset, size_t reset_cap) {
  if (limit->has_pct) snprintf(value, value_cap, "%.0f%%", limit->pct);
  else snprintf(value, value_cap, "-");
  format_reset(limit, reset, reset_cap);
}

static void render_detail(void) {
  char left_value[16], right_value[16], left_reset[32], right_reset[32];
  int index = ui.detail_index;
  if (index == 0 || index == 1) {
    const tk_limit *week = index == 0 ? &ui.tokens.claude_week : &ui.tokens.codex_week;
    const tk_limit *session = index == 0 ? &ui.tokens.claude_session : &ui.tokens.codex_session;
    lv_label_set_text(ui.detail_title, index == 0 ? "CLAUDE METRICS" : "CODEX METRICS");
    detail_limit(week, left_value, sizeof left_value, left_reset, sizeof left_reset);
    detail_limit(session, right_value, sizeof right_value, right_reset, sizeof right_reset);
    lv_label_set_text(ui.detail_left_label, "WEEKLY USED");
    lv_label_set_text(ui.detail_right_label, "SESSION USED");
    lv_label_set_text(ui.detail_left_value, left_value);
    lv_label_set_text(ui.detail_right_value, right_value);
    lv_label_set_text(ui.detail_left_reset, left_reset);
    lv_label_set_text(ui.detail_right_reset, right_reset);
    /* Values include a percent sign; the complete UI font owns that glyph. */
    lv_obj_set_style_text_font(ui.detail_left_value, &plex_ui_21, 0);
    lv_obj_set_style_text_font(ui.detail_right_value, &plex_ui_21, 0);
  } else {
    char month[16], today[16], rate[8], sessions[6];
    format_tokens(ui.tokens.month_tokens, month, sizeof month);
    format_tokens(ui.tokens.day_tokens, today, sizeof today);
    format_tokens(ui.tokens.day_tokens_per_hour, rate, sizeof rate);
    snprintf(sessions, sizeof sessions, "%d", ui.tokens.day_sessions);
    lv_label_set_text(ui.detail_title, "MONTH METRICS");
    lv_label_set_text(ui.detail_left_label, "THIS MONTH");
    lv_label_set_text(ui.detail_right_label, "TODAY");
    lv_label_set_text(ui.detail_left_value, month);
    lv_label_set_text(ui.detail_right_value, today);
    lv_label_set_text(ui.detail_left_reset, "TOKENS");
    snprintf(right_reset, sizeof right_reset, "%s/H  %s S", rate, sessions);
    lv_label_set_text(ui.detail_right_reset, right_reset);
    lv_obj_set_style_text_font(ui.detail_left_value, &plex_ui_21, 0);
    lv_obj_set_style_text_font(ui.detail_right_value, &plex_ui_21, 0);
  }
}

void usage_screen_create(lv_obj_t *root) {
  memset(&ui, 0, sizeof ui);
  ui.tiles = lv_tileview_create(root);
  lv_obj_set_size(ui.tiles, SCREEN_W, SCREEN_H);
  lv_obj_set_scrollbar_mode(ui.tiles, LV_SCROLLBAR_MODE_OFF);
  static const char *marks[PAGE_COUNT] = {"", "", ">", "+", "", ""};
  static const uint32_t colors[PAGE_COUNT] = {0xD97757, 0x6F78FF, 0x51545C, 0x4A9D75, 0xD97757, 0x6F78FF};
  for (int i = 0; i < PAGE_COUNT; ++i) {
    mini_page *p = &ui.pages[i];
    if (i >= TRACKER_FIRST_PAGE) {
      create_tracker_page(i);
      continue;
    }
    p->tile = lv_tileview_add_tile(ui.tiles, i, 0,
                                   i == 0 ? LV_DIR_RIGHT :
                                   i == PAGE_COUNT - 1 ? LV_DIR_LEFT : LV_DIR_HOR);
    lv_obj_set_style_bg_color(p->tile, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(p->tile, LV_OPA_COVER, 0);
    if (i < 2) {
      p->icon = lv_image_create(p->tile);
      lv_image_set_src(p->icon, i == 0 ? &tk_img_claude_32 : &tk_img_codex_32);
      lv_obj_set_pos(p->icon, 18, 10);
      if (i == 0) {
        lv_obj_set_style_image_recolor(p->icon, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_image_recolor_opa(p->icon, LV_OPA_COVER, 0);
      }
    } else {
      p->icon = lv_obj_create(p->tile);
      lv_obj_remove_style_all(p->icon);
      lv_obj_set_pos(p->icon, 20, 13);
      lv_obj_set_size(p->icon, 26, 26);
      lv_obj_set_style_bg_color(p->icon, lv_color_hex(colors[i]), 0);
      lv_obj_set_style_bg_opa(p->icon, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(p->icon, LV_RADIUS_CIRCLE, 0);
      p->icon_mark = text(p->icon, &plex_ui_16, lv_color_white(), 0, 4, 26, 18);
      lv_label_set_text(p->icon_mark, marks[i]);
      lv_obj_set_style_text_align(p->icon_mark, LV_TEXT_ALIGN_CENTER, 0);
    }
    p->title = text(p->tile, &plex_ui_21, lv_color_white(), 58, 12, 242, 28);
    p->caption = text(p->tile, &plex_ui_14, lv_color_hex(0x9298A2), 55, 37, 180, 18);
    lv_obj_add_flag(p->caption, LV_OBJ_FLAG_HIDDEN);
    p->percent = text(p->tile, &plex_num_50, lv_color_white(), 20, 54, 170, 52);
    p->percent_suffix = text(p->tile, &plex_ui_21, lv_color_white(), 130, 76, 28, 28);
    p->track = lv_obj_create(p->tile);
    lv_obj_remove_style_all(p->track);
    lv_obj_set_pos(p->track, 20, 116);
    lv_obj_set_size(p->track, 180, 11);
    lv_obj_set_style_bg_color(p->track, lv_color_hex(0x303238), 0);
    lv_obj_set_style_bg_opa(p->track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->track, LV_RADIUS_CIRCLE, 0);
    p->fill = lv_obj_create(p->track);
    lv_obj_remove_style_all(p->fill);
    lv_obj_set_size(p->fill, 0, 11);
    lv_obj_set_style_bg_opa(p->fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p->fill, LV_RADIUS_CIRCLE, 0);
    p->detail = text(p->tile, &plex_ui_21, lv_color_white(), 214, 104, 90, 27);
    p->status = text(p->tile, &plex_ui_14, lv_color_hex(0x9298A2), 214, 132, 90, 20);
    lv_obj_set_style_text_align(p->detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(p->status, LV_TEXT_ALIGN_RIGHT, 0);
  }
  ui.detail_screen = lv_obj_create(root);
  lv_obj_remove_style_all(ui.detail_screen);
  lv_obj_set_size(ui.detail_screen, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(ui.detail_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(ui.detail_screen, LV_OPA_COVER, 0);
  ui.detail_title = text(ui.detail_screen, &plex_ui_21, lv_color_white(), 20, 15, 280, 28);
  ui.detail_left_label = text(ui.detail_screen, &plex_ui_14, lv_color_hex(0x9298A2), 20, 52, 125, 20);
  ui.detail_left_value = text(ui.detail_screen, &plex_num_50, lv_color_white(), 20, 69, 125, 48);
  ui.detail_left_reset = text(ui.detail_screen, &plex_ui_14, lv_color_hex(0xB2B7C0), 20, 112, 135, 20);
  ui.detail_right_label = text(ui.detail_screen, &plex_ui_14, lv_color_hex(0x9298A2), 166, 52, 135, 20);
  ui.detail_right_value = text(ui.detail_screen, &plex_num_50, lv_color_white(), 166, 69, 135, 48);
  ui.detail_right_reset = text(ui.detail_screen, &plex_ui_14, lv_color_hex(0xB2B7C0), 166, 112, 145, 20);
  ui.detail_hint = text(ui.detail_screen, &plex_ui_14, lv_color_hex(0x9298A2), 20, 145, 280, 18);
  lv_label_set_text(ui.detail_hint, "SHORT PRESS: BACK");
  lv_obj_add_flag(ui.detail_screen, LV_OBJ_FLAG_HIDDEN);
  usage_screen_apply_tokens(&ui.tokens);
  render_activity();
  render_metrics();
}

void usage_screen_apply_tokens(const tk_tokens *tokens) {
  if (!tokens) return;
  ui.tokens = *tokens;
  ui.have_tokens = true;
  render_quota(0, "CLAUDE", &tokens->claude_week, lv_color_hex(0xD97757));
  render_quota(1, "CODEX", &tokens->codex_week, lv_color_hex(0x6F78FF));
  render_metrics();
}

void usage_screen_apply_agent(const tk_agent_snapshot *snapshot, int64_t now_us) {
  (void)now_us;
  if (!snapshot) return;
  ui.agents = *snapshot;
  ui.have_agents = true;
  render_activity();
}

void usage_screen_apply_max_tracker(const tk_max_tracker *t) {
  if (!t) return;
  ui.tracker = *t;
  ui.have_tracker = true;
  render_tracker(0);
  render_tracker(1);
}
void usage_screen_apply_github(const tk_github_status *status) { (void)status; }
void usage_screen_tick(int64_t now_us) { (void)now_us; }

void usage_screen_set_stale(bool stale) {
  ui.stale = stale;
  if (ui.have_tokens) usage_screen_apply_tokens(&ui.tokens);
}

void usage_screen_show_view(int index) {
  if (index < 0) index = 0;
  ui.current = index % PAGE_COUNT;
  lv_obj_set_tile_id(ui.tiles, ui.current, 0, LV_ANIM_OFF);
}

int usage_screen_current_view(void) { return ui.current; }

void usage_screen_show_detail(int index) {
  if (index != 0 && index != 1 && index != 3) return;
  ui.detail_index = index;
  render_detail();
  lv_obj_remove_flag(ui.detail_screen, LV_OBJ_FLAG_HIDDEN);
}

void usage_screen_hide_detail(void) { lv_obj_add_flag(ui.detail_screen, LV_OBJ_FLAG_HIDDEN); }
