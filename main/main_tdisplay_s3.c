/* LilyGO T-Display-S3 host layer.  The app/network contracts stay shared;
 * only the panel, backlight and input wiring differ from the Waveshare host. */
#include <assert.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lv_adapter.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "agent_monitor.h"
#include "app_tokens.h"
#include "app_tokens_config.h"
#include "project_star_popup.h"
#include "secrets.h"
#include "tdisplay_cfg.h"
#include "torget.h"
#include "usage_screen.h"

#define LCD_W 320
#define LCD_H 170
#define LCD_ROWS 20
#define LCD_PWR GPIO_NUM_15
#define LCD_BL GPIO_NUM_38
#define LCD_RST GPIO_NUM_5
#define LCD_CS GPIO_NUM_6
#define LCD_DC GPIO_NUM_7
#define LCD_WR GPIO_NUM_8
#define LCD_RD GPIO_NUM_9
#define KEY_BOOT GPIO_NUM_0
#define KEY_NEXT GPIO_NUM_14
#define PAGE_COUNT TK_USAGE_SCREEN_VIEWS
#define LONG_PRESS_MS 650

static const char *TAG = "vibepulse-s3";
static EventGroupHandle_t net_events;
#define NET_READY BIT0
static int64_t last_activity_us;
static uint8_t brightness_step;

static void set_backlight(uint8_t duty) {
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}

static void cycle_backlight(void) {
  static const uint8_t levels[] = {255, 128, 32};
  brightness_step = (brightness_step + 1) % (sizeof(levels) / sizeof(levels[0]));
  set_backlight(levels[brightness_step]);
}

void torget_ui_lock(void) { ESP_ERROR_CHECK(esp_lv_adapter_lock(-1)); }
void torget_ui_unlock(void) { esp_lv_adapter_unlock(); }
bool torget_ui_try_lock(uint32_t timeout_ms) {
  return esp_lv_adapter_lock((int32_t)timeout_ms) == ESP_OK;
}
int64_t torget_now_us(void) { return esp_timer_get_time(); }
void torget_net_wait(void) {
  xEventGroupWaitBits(net_events, NET_READY, pdFALSE, pdTRUE, portMAX_DELAY);
}
void torget_keep_awake(void) { last_activity_us = esp_timer_get_time(); }
void torget_update_available(const char *version) { (void)version; }
void torget_data_alive(void) { }

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    int reason = data ? ((wifi_event_sta_disconnected_t *)data)->reason : -1;
    ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d); retrying", reason);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Wi-Fi connected");
    xEventGroupSetBits(net_events, NET_READY);
  }
}

static void wifi_start(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL, NULL));
  wifi_config_t station = {0};
  const char *ssid = tk_tdisplay_wifi_ssid();
  const char *pass = tk_tdisplay_wifi_pass();
  if (!ssid[0]) {
    ESP_LOGW(TAG, "Wi-Fi not configured; flash via the installer or secrets.h");
    return;
  }
  strlcpy((char *)station.sta.ssid, ssid, sizeof station.sta.ssid);
  strlcpy((char *)station.sta.password, pass, sizeof station.sta.password);
  station.sta.threshold.authmode =
      pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static void net_task(void *arg) {
  (void)arg;
  xEventGroupWaitBits(net_events, NET_READY, pdFALSE, pdTRUE, portMAX_DELAY);
  esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
  (void)esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
  vTaskDelete(NULL);
}

static void panel_start(void) {
  gpio_config_t output = {.mode = GPIO_MODE_OUTPUT,
                          .pin_bit_mask = (1ULL << LCD_PWR) |
                                          (1ULL << LCD_RD)};
  ESP_ERROR_CHECK(gpio_config(&output));
  gpio_set_level(LCD_PWR, 1);  /* required: powers the LCD rail */
  gpio_set_level(LCD_RD, 1);
  ledc_timer_config_t ledc_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
      .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK};
  ledc_channel_config_t ledc_channel = {
      .gpio_num = LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0, .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_0, .duty = 255, .hpoint = 0};
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

  esp_lcd_i80_bus_handle_t bus = NULL;
  esp_lcd_i80_bus_config_t bus_cfg = {
      .dc_gpio_num = LCD_DC, .wr_gpio_num = LCD_WR, .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_gpio_nums = {39, 40, 41, 42, 45, 46, 47, 48}, .bus_width = 8,
      .max_transfer_bytes = LCD_W * LCD_ROWS * sizeof(uint16_t),
      .psram_trans_align = 64, .sram_trans_align = 4};
  ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &bus));
  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_io_i80_config_t io_cfg = {
      .cs_gpio_num = LCD_CS, .pclk_hz = 10 * 1000 * 1000,
      .trans_queue_depth = 10, .lcd_cmd_bits = 8, .lcd_param_bits = 8,
      .dc_levels = {.dc_idle_level = 0, .dc_cmd_level = 0,
                    .dc_dummy_level = 0, .dc_data_level = 1}};
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(bus, &io_cfg, &io));
  esp_lcd_panel_handle_t panel = NULL;
  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = LCD_RST, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .bits_per_pixel = 16};
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
  ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 0, 35));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

  esp_lv_adapter_config_t adapter = ESP_LV_ADAPTER_DEFAULT_CONFIG();
  adapter.task_stack_size = 12 * 1024;
  ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter));
  esp_lv_adapter_display_config_t display = {
      .panel = panel, .panel_io = io,
      .profile = {.interface = ESP_LV_ADAPTER_PANEL_IF_OTHER,
                  .rotation = ESP_LV_ADAPTER_ROTATE_0, .hor_res = LCD_W,
                  .ver_res = LCD_H, .buffer_height = LCD_ROWS,
                  .use_psram = true, .enable_ppa_accel = false,
                  .require_double_buffer = true},
      .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE};
  assert(esp_lv_adapter_register_display(&display));
  ESP_ERROR_CHECK(esp_lv_adapter_start());
}

static void key_task(void *arg) {
  (void)arg;
  bool next_was_down = false;
  bool boot_was_down = false;
  bool next_long = false;
  bool boot_long = false;
  int64_t next_down_ms = 0;
  int64_t boot_down_ms = 0;
  int page = 0;
  bool detail_open = false;
  for (;;) {
    bool next_down = gpio_get_level(KEY_NEXT) == 0;
    bool boot_down = gpio_get_level(KEY_BOOT) == 0;
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (next_down && !next_was_down) {
      next_down_ms = now_ms;
      next_long = false;
    }
    if (boot_down && !boot_was_down) {
      boot_down_ms = now_ms;
      boot_long = false;
    }
    if (next_down && !next_long && now_ms - next_down_ms >= LONG_PRESS_MS) {
      next_long = true;
      torget_ui_lock();
      if (detail_open) {
        tokens_hide_detail();
        detail_open = false;
      } else if (page == VIEW_CLAUDE_FABLE || page == VIEW_CLAUDE_ALL ||
                 page == VIEW_CODEX_WEEKLY) {
        tokens_show_detail(page);
        detail_open = true;
      }
      torget_ui_unlock();
    }
    if (boot_down && !boot_long && now_ms - boot_down_ms >= LONG_PRESS_MS) {
      boot_long = true;
      torget_ui_lock();
      if (detail_open) {
        tokens_hide_detail();
        detail_open = false;
      } else if (page == VIEW_CLAUDE_FABLE || page == VIEW_CLAUDE_ALL ||
                 page == VIEW_CODEX_WEEKLY) {
        tokens_show_detail(page);
        detail_open = true;
      }
      torget_ui_unlock();
    }
    if (!next_down && next_was_down && !next_long) {
      torget_ui_lock();
      if (tk_agent_monitor_visible()) {
        tk_agent_monitor_dismiss_current();
#if TK_GITHUB_NOTIFICATIONS_ENABLED
      } else if (tk_project_star_popup_visible()) {
        tk_project_star_popup_dismiss();
#endif
      } else if (detail_open) {
        tokens_hide_detail();
        detail_open = false;
      } else {
        page = (page + 1) % PAGE_COUNT;
        tokens_show_view(page);
      }
      torget_ui_unlock();
      last_activity_us = esp_timer_get_time();
    }
    if (!boot_down && boot_was_down && !boot_long) {
      torget_ui_lock();
      if (tk_agent_monitor_visible()) {
        tk_agent_monitor_dismiss_current();
#if TK_GITHUB_NOTIFICATIONS_ENABLED
      } else if (tk_project_star_popup_visible()) {
        tk_project_star_popup_dismiss();
#endif
      } else if (detail_open) {
        tokens_hide_detail();
        detail_open = false;
      } else {
        page = (page + PAGE_COUNT - 1) % PAGE_COUNT;
        tokens_show_view(page);
      }
      torget_ui_unlock();
      last_activity_us = esp_timer_get_time();
    }
    next_was_down = next_down;
    boot_was_down = boot_down;
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  net_events = xEventGroupCreate();
  panel_start();
  gpio_config_t key = {.mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
                       .pin_bit_mask = (1ULL << KEY_BOOT) | (1ULL << KEY_NEXT)};
  ESP_ERROR_CHECK(gpio_config(&key));
  torget_ui_lock();
  torget_ui_create();
  torget_ui_unlock();
  wifi_start();
  xTaskCreate(net_task, "vp-net", 4096, NULL, 5, NULL);
  /* The detail renderer runs in this task under the LVGL lock.  4 KB still
   * overflowed on a long press, so reserve the same safe margin as a UI task. */
  xTaskCreate(key_task, "vp-key", 8192, NULL, 4, NULL);
  ESP_LOGI(TAG, "VibePulse Mini ready: short press pages; long press details");
}
