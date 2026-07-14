/*
 * ============================================================
 *  ESP32 CYD – Modern Real-Time Clock & Alarm (WiFi Fixed)
 *  Target  : ESP32-2432S028R (Cheap Yellow Display)
 * ============================================================
 */

// ── Core libraries ────────────────────────────────────────────
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include "time.h"

// ── LVGL Size Optimizations ───────────────────────────────────
#define LV_USE_ANIMATIONS 0
#define LV_USE_TRANSITIONS 0
#define LV_USE_LOG 0

// ── Touchscreen pins ──────────────────────────────────────────
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// ── Buzzer ───────────────────────────────────────────────────
#define BUZZER_PIN 26

// ── Display geometry ─────────────────────────────────────────
#define SCREEN_W 320
#define SCREEN_H 240

// ── NTP ──────────────────────────────────────────────────────
#define NTP_SERVER   "pool.ntp.org"
#define GMT_OFFSET   19800
#define DST_OFFSET   0

// ── LVGL draw buffer (increased for better performance) ───────
#define DRAW_BUF_SIZE (SCREEN_W * SCREEN_H / 5 * (LV_COLOR_DEPTH / 8))
static uint32_t draw_buf[DRAW_BUF_SIZE / 4];
static uint32_t draw_buf2[DRAW_BUF_SIZE / 4];

// ── Touchscreen objects ──────────────────────────────────────
SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// ── Alarm state ──────────────────────────────────────────────
static int  alarmHour   = 7;
static int  alarmMin    = 0;
static bool alarmEnabled = false;
static bool alarmAM     = true;
static bool alarmFired  = false;

// ── WiFi credentials ─────────────────────────────────────────
static char wifiSSID[33] = "Airtel_mani_4630";     
static char wifiPASS[65] = "Air@6682566";
static bool wifiConnected = false;
static bool ntpSynced = false;

// ── LVGL screen handles ──────────────────────────────────────
static lv_obj_t *scr_clock  = nullptr;
static lv_obj_t *scr_alarm  = nullptr;
static lv_obj_t *scr_wifi   = nullptr;
static lv_obj_t *modal_alarm = nullptr;

// ── Clock screen widgets ──────────────────────────────────────
static lv_obj_t *lbl_time      = nullptr;
static lv_obj_t *lbl_date      = nullptr;
static lv_obj_t *lbl_day       = nullptr;
static lv_obj_t *lbl_ampm      = nullptr;
static lv_obj_t *arc_sec       = nullptr;
static lv_obj_t *arc_min       = nullptr;
static lv_obj_t *arc_hour      = nullptr;
static lv_obj_t *badge_wifi    = nullptr;
static lv_obj_t *badge_alarm   = nullptr;
static lv_obj_t *lbl_badge_wifi  = nullptr;
static lv_obj_t *lbl_badge_alarm = nullptr;
static lv_obj_t *wifi_status_indicator = nullptr;

// ── Alarm screen widgets ──────────────────────────────────────
static lv_obj_t *roller_alarm_h = nullptr;
static lv_obj_t *roller_alarm_m = nullptr;
static lv_obj_t *btn_alarm_ampm = nullptr;
static lv_obj_t *lbl_ampm_btn   = nullptr;
static lv_obj_t *sw_alarm       = nullptr;

// ── WiFi screen widgets ──────────────────────────────────────
static lv_obj_t *ta_ssid    = nullptr;
static lv_obj_t *ta_pass    = nullptr;
static lv_obj_t *lbl_status = nullptr;
static lv_obj_t *kb_wifi    = nullptr;

// ── Timers ────────────────────────────────────────────────────
static lv_timer_t *timer_clock = nullptr;
static lv_timer_t *wifi_check_timer = nullptr;

// ── TFT_eSPI object ──────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI();

// ─────────────────────────────────────────────────────────────
//  Colour helpers
// ─────────────────────────────────────────────────────────────
#define C_BG      lv_color_hex(0x0D1B2A)
#define C_CARD    lv_color_hex(0x112233)
#define C_ACCENT  lv_color_hex(0x00BFA5)
#define C_WARN    lv_color_hex(0xFF4444)
#define C_WHITE   lv_color_hex(0xFFFFFF)
#define C_MUTED   lv_color_hex(0x8899AA)
#define C_DARK2   lv_color_hex(0x0A1520)
#define C_GREEN   lv_color_hex(0x4CAF50)

// ─────────────────────────────────────────────────────────────
//  Display flush callback
// ─────────────────────────────────────────────────────────────
static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)px_map, w * h, true);
  tft.endWrite();
  
  lv_display_flush_ready(disp);
}

// ─────────────────────────────────────────────────────────────
//  Touchscreen callback (improved calibration)
// ─────────────────────────────────────────────────────────────
static void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    
    // Better calibration for CYD
    int16_t x = map(p.x, 250, 3700, 0, SCREEN_W);
    int16_t y = map(p.y, 250, 3800, 0, SCREEN_H);
    
    x = constrain(x, 0, SCREEN_W - 1);
    y = constrain(y, 0, SCREEN_H - 1);
    
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ─────────────────────────────────────────────────────────────
//  Buzzer helper
// ─────────────────────────────────────────────────────────────
static void buzz(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(100);
    digitalWrite(BUZZER_PIN, LOW);  delay(100);
  }
}

// ─────────────────────────────────────────────────────────────
//  WiFi + NTP (non-blocking)
// ─────────────────────────────────────────────────────────────
static void connect_wifi() {
  if (strlen(wifiSSID) < 2 || strcmp(wifiSSID, "YOUR_WIFI_SSID") == 0) {
    Serial.println("WiFi not configured");
    wifiConnected = false;
    ntpSynced = false;
    return;
  }
  
  Serial.print("Connecting to WiFi");
  WiFi.begin(wifiSSID, wifiPASS);
  
  // Non-blocking wait - will be checked in timer
}

static void check_wifi_connection(lv_timer_t *t) {
  static int wifi_attempt = 0;
  
  if (!wifiConnected) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.println("\nWiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      
      // Configure NTP
      configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
      
      // Wait for NTP sync
      struct tm ti;
      int ntp_attempts = 0;
      while (!getLocalTime(&ti, 500) && ntp_attempts < 10) {
        delay(100);
        ntp_attempts++;
      }
      
      if (ntp_attempts < 10) {
        ntpSynced = true;
        Serial.println("NTP synchronized!");
      } else {
        Serial.println("NTP sync failed!");
      }
      
      // Force LVGL refresh
      if (lbl_badge_wifi) {
        lv_label_set_text(lbl_badge_wifi, "WiFi ON");
        lv_obj_set_style_bg_color(badge_wifi, C_ACCENT, 0);
      }
      
      buzz(1);
      wifi_attempt = 0;
    } else if (wifi_attempt < 40) { // 20 seconds timeout
      wifi_attempt++;
      if (wifi_attempt % 10 == 0) Serial.print(".");
    } else {
      wifiConnected = false;
      ntpSynced = false;
      Serial.println("\nWiFi connection failed!");
      
      if (lbl_badge_wifi) {
        lv_label_set_text(lbl_badge_wifi, "WiFi OFF");
        lv_obj_set_style_bg_color(badge_wifi, C_MUTED, 0);
      }
      wifi_attempt = 0;
    }
  }
}

static bool get_time(struct tm &ti) {
  if (!wifiConnected || !ntpSynced) return false;
  return getLocalTime(&ti, 500);
}

// ─────────────────────────────────────────────────────────────
//  ALARM MODAL
// ─────────────────────────────────────────────────────────────
static void dismiss_alarm_cb(lv_event_t *e) {
  if (modal_alarm) {
    lv_obj_delete(modal_alarm);
    modal_alarm = nullptr;
  }
  alarmFired = false;
  digitalWrite(BUZZER_PIN, LOW);
}

static void show_alarm_modal() {
  if (modal_alarm) return;
  lv_obj_t *scr = lv_screen_active();

  modal_alarm = lv_obj_create(scr);
  lv_obj_set_size(modal_alarm, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(modal_alarm, 0, 0);
  lv_obj_set_style_bg_color(modal_alarm, C_DARK2, 0);
  lv_obj_set_style_bg_opa(modal_alarm, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(modal_alarm, C_WARN, 0);
  lv_obj_set_style_border_width(modal_alarm, 3, 0);
  lv_obj_set_style_radius(modal_alarm, 0, 0);
  lv_obj_clear_flag(modal_alarm, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *lbl_title = lv_label_create(modal_alarm);
  lv_label_set_text(lbl_title, "ALARM!");
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_title, C_WARN, 0);
  lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, -40);

  char abuf[16];
  snprintf(abuf, sizeof(abuf), "%02d:%02d %s",
           alarmHour, alarmMin, alarmAM ? "AM" : "PM");
  lv_obj_t *lbl_atime = lv_label_create(modal_alarm);
  lv_label_set_text(lbl_atime, abuf);
  lv_obj_set_style_text_font(lbl_atime, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_atime, C_WHITE, 0);
  lv_obj_align(lbl_atime, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *btn = lv_button_create(modal_alarm);
  lv_obj_set_size(btn, 140, 40);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
  lv_obj_set_style_bg_color(btn, C_WARN, 0);
  lv_obj_set_style_radius(btn, 20, 0);
  lv_obj_add_event_cb(btn, dismiss_alarm_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_d = lv_label_create(btn);
  lv_label_set_text(lbl_d, "DISMISS");
  lv_obj_set_style_text_font(lbl_d, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(lbl_d, C_WHITE, 0);
  lv_obj_center(lbl_d);

  buzz(3);
}

// ─────────────────────────────────────────────────────────────
//  CLOCK SCREEN – periodic update timer callback
// ─────────────────────────────────────────────────────────────
static const char *day_names[] = {
  "SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"
};
static const char *month_names[] = {
  "JAN","FEB","MAR","APR","MAY","JUN",
  "JUL","AUG","SEP","OCT","NOV","DEC"
};

static void clock_timer_cb(lv_timer_t *t) {
  struct tm ti;
  int hr = 0, mn = 0, sc = 0, dy = 0, mo = 0, yr = 0, wd = 0;

  if (wifiConnected && ntpSynced && get_time(ti)) {
    hr = ti.tm_hour; mn = ti.tm_min; sc = ti.tm_sec;
    dy = ti.tm_mday; mo = ti.tm_mon; yr = ti.tm_year + 1900;
    wd = ti.tm_wday;
  } else {
    // Software fallback clock
    static unsigned long lastMillis = 0;
    static int sw_hr = 12, sw_mn = 0, sw_sc = 0;
    
    unsigned long now = millis();
    if (now - lastMillis >= 1000) {
      lastMillis = now;
      sw_sc++;
      if (sw_sc >= 60) {
        sw_sc = 0;
        sw_mn++;
        if (sw_mn >= 60) {
          sw_mn = 0;
          sw_hr++;
          if (sw_hr >= 24) sw_hr = 0;
        }
      }
    }
    hr = sw_hr; mn = sw_mn; sc = sw_sc;
    dy = 1; mo = 0; yr = 2025; wd = 0;
  }

  // Update arcs
  if (arc_sec) lv_arc_set_value(arc_sec, sc);
  if (arc_min) lv_arc_set_value(arc_min, mn);
  if (arc_hour) lv_arc_set_value(arc_hour, (hr % 12) * 5 + mn / 12);

  // Update digital time
  bool pm   = hr >= 12;
  int  hr12 = hr % 12; if (hr12 == 0) hr12 = 12;
  char tbuf[12];
  snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", hr12, mn, sc);
  if (lbl_time) lv_label_set_text(lbl_time, tbuf);
  if (lbl_ampm) lv_label_set_text(lbl_ampm, pm ? "PM" : "AM");

  // Update date
  char dbuf[24];
  snprintf(dbuf, sizeof(dbuf), "%02d %s %04d", dy, month_names[mo], yr);
  if (lbl_date) lv_label_set_text(lbl_date, dbuf);
  if (lbl_day) lv_label_set_text(lbl_day, day_names[wd]);

  // Check alarm
  if (alarmEnabled && !alarmFired && ntpSynced) {
    int alarmHr24 = (alarmHour % 12) + (alarmAM ? 0 : 12);
    if (hr == alarmHr24 && mn == alarmMin && sc == 0) {
      alarmFired = true;
      show_alarm_modal();
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  NAV BUTTONS
// ─────────────────────────────────────────────────────────────
static lv_obj_t *make_nav_btn(lv_obj_t *parent, const char *txt,
                               lv_align_t align, int ox, int oy) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, 80, 30);
  lv_obj_align(btn, align, ox, oy);
  lv_obj_set_style_bg_color(btn, C_CARD, 0);
  lv_obj_set_style_border_color(btn, C_ACCENT, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, 15, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl, C_ACCENT, 0);
  lv_obj_center(lbl);
  return btn;
}

// ─────────────────────────────────────────────────────────────
//  BUILD CLOCK SCREEN
// ─────────────────────────────────────────────────────────────
static void nav_to_alarm_cb(lv_event_t *e);
static void nav_to_wifi_cb(lv_event_t *e);

static void build_clock_screen() {
  scr_clock = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_clock, C_BG, 0);
  lv_obj_set_style_bg_opa(scr_clock, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr_clock, LV_OBJ_FLAG_SCROLLABLE);

  // Arc cluster - Outer (seconds)
  arc_sec = lv_arc_create(scr_clock);
  lv_obj_set_size(arc_sec, 200, 200);
  lv_obj_align(arc_sec, LV_ALIGN_LEFT_MID, 10, 0);
  lv_arc_set_range(arc_sec, 0, 60);
  lv_arc_set_rotation(arc_sec, 270);
  lv_obj_set_style_arc_color(arc_sec, C_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc_sec, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_sec, C_DARK2, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_sec, 3, LV_PART_MAIN);
  lv_obj_remove_flag(arc_sec, LV_OBJ_FLAG_CLICKABLE);

  // Middle arc (minutes)
  arc_min = lv_arc_create(scr_clock);
  lv_obj_set_size(arc_min, 164, 164);
  lv_obj_align(arc_min, LV_ALIGN_LEFT_MID, 28, 0);
  lv_arc_set_range(arc_min, 0, 60);
  lv_arc_set_rotation(arc_min, 270);
  lv_obj_set_style_arc_color(arc_min, lv_color_hex(0x00897B), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc_min, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_min, C_DARK2, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_min, 4, LV_PART_MAIN);
  lv_obj_remove_flag(arc_min, LV_OBJ_FLAG_CLICKABLE);

  // Inner arc (hours)
  arc_hour = lv_arc_create(scr_clock);
  lv_obj_set_size(arc_hour, 122, 122);
  lv_obj_align(arc_hour, LV_ALIGN_LEFT_MID, 49, 0);
  lv_arc_set_range(arc_hour, 0, 60);
  lv_arc_set_rotation(arc_hour, 270);
  lv_obj_set_style_arc_color(arc_hour, C_WHITE, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(arc_hour, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc_hour, C_DARK2, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc_hour, 5, LV_PART_MAIN);
  lv_obj_remove_flag(arc_hour, LV_OBJ_FLAG_CLICKABLE);

  // Digital time
  lbl_time = lv_label_create(scr_clock);
  lv_label_set_text(lbl_time, "12:00:00");
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_36, 0);
  lv_obj_set_style_text_color(lbl_time, C_WHITE, 0);
  lv_obj_align(lbl_time, LV_ALIGN_RIGHT_MID, -10, -68);

  lbl_ampm = lv_label_create(scr_clock);
  lv_label_set_text(lbl_ampm, "AM");
  lv_obj_set_style_text_font(lbl_ampm, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(lbl_ampm, C_ACCENT, 0);
  lv_obj_align(lbl_ampm, LV_ALIGN_RIGHT_MID, -10, -40);

  lbl_day = lv_label_create(scr_clock);
  lv_label_set_text(lbl_day, "MONDAY");
  lv_obj_set_style_text_font(lbl_day, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_day, C_ACCENT, 0);
  lv_obj_align(lbl_day, LV_ALIGN_RIGHT_MID, -10, -12);

  lbl_date = lv_label_create(scr_clock);
  lv_label_set_text(lbl_date, "01 JAN 2025");
  lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_date, C_WHITE, 0);
  lv_obj_align(lbl_date, LV_ALIGN_RIGHT_MID, -10, 14);

  // WiFi badge
  badge_wifi = lv_obj_create(scr_clock);
  lv_obj_set_size(badge_wifi, 95, 24);
  lv_obj_set_style_radius(badge_wifi, 12, 0);
  lv_obj_set_style_border_width(badge_wifi, 0, 0);
  lv_obj_set_style_bg_color(badge_wifi, wifiConnected ? C_ACCENT : C_MUTED, 0);
  lv_obj_align(badge_wifi, LV_ALIGN_RIGHT_MID, -10, 55);
  lv_obj_set_style_pad_all(badge_wifi, 0, 0);
  
  lbl_badge_wifi = lv_label_create(badge_wifi);
  lv_label_set_text(lbl_badge_wifi, wifiConnected ? "WiFi ON" : "WiFi OFF");
  lv_obj_set_style_text_font(lbl_badge_wifi, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_badge_wifi, C_WHITE, 0);
  lv_obj_center(lbl_badge_wifi);

  // Alarm badge
  badge_alarm = lv_obj_create(scr_clock);
  lv_obj_set_size(badge_alarm, 95, 24);
  lv_obj_set_style_radius(badge_alarm, 12, 0);
  lv_obj_set_style_border_width(badge_alarm, 0, 0);
  lv_obj_set_style_bg_color(badge_alarm, alarmEnabled ? C_ACCENT : C_MUTED, 0);
  lv_obj_align(badge_alarm, LV_ALIGN_RIGHT_MID, -10, 85);
  lv_obj_set_style_pad_all(badge_alarm, 0, 0);
  
  lbl_badge_alarm = lv_label_create(badge_alarm);
  char alarm_text[32];
  if (alarmEnabled) {
    snprintf(alarm_text, sizeof(alarm_text), "Alarm %02d:%02d", alarmHour, alarmMin);
  } else {
    strcpy(alarm_text, "Alarm OFF");
  }
  lv_label_set_text(lbl_badge_alarm, alarm_text);
  lv_obj_set_style_text_font(lbl_badge_alarm, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_badge_alarm, C_WHITE, 0);
  lv_obj_center(lbl_badge_alarm);

  // Nav buttons
  lv_obj_t *btn_alarm_nav = make_nav_btn(scr_clock, "Alarm",
                                      LV_ALIGN_BOTTOM_RIGHT, -4, -38);
  lv_obj_add_event_cb(btn_alarm_nav, nav_to_alarm_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_wifi_nav = make_nav_btn(scr_clock, "WiFi",
                                         LV_ALIGN_BOTTOM_RIGHT, -4, -2);
  lv_obj_add_event_cb(btn_wifi_nav, nav_to_wifi_cb, LV_EVENT_CLICKED, NULL);
}

// ─────────────────────────────────────────────────────────────
//  BUILD ALARM SCREEN
// ─────────────────────────────────────────────────────────────
static void back_to_clock_cb(lv_event_t *e);
static void alarm_save_cb(lv_event_t *e);
static void ampm_toggle_cb(lv_event_t *e);

static void build_alarm_screen() {
  scr_alarm = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_alarm, C_BG, 0);
  lv_obj_set_style_bg_opa(scr_alarm, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr_alarm, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(scr_alarm);
  lv_label_set_text(title, "Set Alarm");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, C_ACCENT, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // Build hour roller options
  char hrOpts[128] = "";
  for (int i = 1; i <= 12; i++) {
    char tmp[8]; snprintf(tmp, sizeof(tmp), "%02d", i);
    strcat(hrOpts, tmp);
    if (i < 12) strcat(hrOpts, "\n");
  }
  
  // Build minute roller options
  char mnOpts[256] = "";
  for (int i = 0; i <= 59; i++) {
    char tmp[8]; snprintf(tmp, sizeof(tmp), "%02d", i);
    strcat(mnOpts, tmp);
    if (i < 59) strcat(mnOpts, "\n");
  }

  // Hour roller
  roller_alarm_h = lv_roller_create(scr_alarm);
  lv_roller_set_options(roller_alarm_h, hrOpts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_alarm_h, 3);
  lv_obj_set_width(roller_alarm_h, 70);
  lv_obj_align(roller_alarm_h, LV_ALIGN_LEFT_MID, 30, 50);
  lv_obj_set_style_bg_color(roller_alarm_h, C_CARD, 0);
  lv_obj_set_style_border_color(roller_alarm_h, C_ACCENT, 0);
  lv_obj_set_style_border_width(roller_alarm_h, 1, 0);
  lv_obj_set_style_radius(roller_alarm_h, 8, 0);
  lv_roller_set_selected(roller_alarm_h, alarmHour - 1, LV_ANIM_OFF);

  // Colon
  lv_obj_t *colon = lv_label_create(scr_alarm);
  lv_label_set_text(colon, ":");
  lv_obj_set_style_text_font(colon, &lv_font_montserrat_30, 0);
  lv_obj_set_style_text_color(colon, C_WHITE, 0);
  lv_obj_align(colon, LV_ALIGN_LEFT_MID, 108, 46);

  // Minute roller
  roller_alarm_m = lv_roller_create(scr_alarm);
  lv_roller_set_options(roller_alarm_m, mnOpts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_alarm_m, 3);
  lv_obj_set_width(roller_alarm_m, 70);
  lv_obj_align(roller_alarm_m, LV_ALIGN_LEFT_MID, 128, 50);
  lv_obj_set_style_bg_color(roller_alarm_m, C_CARD, 0);
  lv_obj_set_style_border_color(roller_alarm_m, C_ACCENT, 0);
  lv_obj_set_style_border_width(roller_alarm_m, 1, 0);
  lv_obj_set_style_radius(roller_alarm_m, 8, 0);
  lv_roller_set_selected(roller_alarm_m, alarmMin, LV_ANIM_OFF);

  // AM/PM button
  btn_alarm_ampm = lv_button_create(scr_alarm);
  lv_obj_set_size(btn_alarm_ampm, 60, 44);
  lv_obj_align(btn_alarm_ampm, LV_ALIGN_LEFT_MID, 218, 54);
  lv_obj_set_style_bg_color(btn_alarm_ampm, alarmAM ? C_ACCENT : C_MUTED, 0);
  lv_obj_set_style_radius(btn_alarm_ampm, 8, 0);
  lv_obj_add_event_cb(btn_alarm_ampm, ampm_toggle_cb, LV_EVENT_CLICKED, NULL);

  lbl_ampm_btn = lv_label_create(btn_alarm_ampm);
  lv_label_set_text(lbl_ampm_btn, alarmAM ? "AM" : "PM");
  lv_obj_set_style_text_font(lbl_ampm_btn, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_ampm_btn, C_BG, 0);
  lv_obj_center(lbl_ampm_btn);

  // Enable switch
  lv_obj_t *lbl_sw = lv_label_create(scr_alarm);
  lv_label_set_text(lbl_sw, "Enable Alarm");
  lv_obj_set_style_text_font(lbl_sw, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_sw, C_WHITE, 0);
  lv_obj_align(lbl_sw, LV_ALIGN_LEFT_MID, 30, 120);

  sw_alarm = lv_switch_create(scr_alarm);
  lv_obj_align(sw_alarm, LV_ALIGN_LEFT_MID, 140, 120);
  lv_obj_set_style_bg_color(sw_alarm, C_MUTED, 0);
  if (alarmEnabled) lv_obj_add_state(sw_alarm, LV_STATE_CHECKED);

  // Save button
  lv_obj_t *btn_save = lv_button_create(scr_alarm);
  lv_obj_set_size(btn_save, 100, 36);
  lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
  lv_obj_set_style_bg_color(btn_save, C_ACCENT, 0);
  lv_obj_set_style_radius(btn_save, 18, 0);
  lv_obj_add_event_cb(btn_save, alarm_save_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_save = lv_label_create(btn_save);
  lv_label_set_text(lbl_save, "Save");
  lv_obj_set_style_text_font(lbl_save, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_save, C_BG, 0);
  lv_obj_center(lbl_save);

  // Back button
  lv_obj_t *btn_back = lv_button_create(scr_alarm);
  lv_obj_set_size(btn_back, 80, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -8);
  lv_obj_set_style_bg_color(btn_back, C_CARD, 0);
  lv_obj_set_style_border_color(btn_back, C_ACCENT, 0);
  lv_obj_set_style_border_width(btn_back, 1, 0);
  lv_obj_set_style_radius(btn_back, 18, 0);
  lv_obj_add_event_cb(btn_back, back_to_clock_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Back");
  lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_back, C_ACCENT, 0);
  lv_obj_center(lbl_back);
}

// ─────────────────────────────────────────────────────────────
//  BUILD WIFI SCREEN
// ─────────────────────────────────────────────────────────────
static void kb_event_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_target(e);
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

static void ta_focus_cb(lv_event_t *e) {
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
  if (kb_wifi) {
    lv_keyboard_set_textarea(kb_wifi, ta);
    lv_obj_clear_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
  }
}

static void wifi_connect_btn_cb(lv_event_t *e) {
  if (ta_ssid && ta_pass) {
    strncpy(wifiSSID, lv_textarea_get_text(ta_ssid), 32);
    strncpy(wifiPASS, lv_textarea_get_text(ta_pass), 64);
    wifiSSID[32] = '\0';
    wifiPASS[64] = '\0';
  }
  
  lv_label_set_text(lbl_status, "Connecting...");
  lv_obj_set_style_text_color(lbl_status, C_ACCENT, 0);
  
  // Reset WiFi state
  wifiConnected = false;
  ntpSynced = false;
  
  // Start connection
  connect_wifi();
}

static void build_wifi_screen() {
  scr_wifi = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_wifi, C_BG, 0);
  lv_obj_set_style_bg_opa(scr_wifi, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr_wifi, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(scr_wifi);
  lv_label_set_text(title, "WiFi Setup");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, C_ACCENT, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  // SSID
  lv_obj_t *lbl_s = lv_label_create(scr_wifi);
  lv_label_set_text(lbl_s, "SSID");
  lv_obj_set_style_text_font(lbl_s, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_s, C_MUTED, 0);
  lv_obj_align(lbl_s, LV_ALIGN_TOP_LEFT, 16, 44);

  ta_ssid = lv_textarea_create(scr_wifi);
  lv_obj_set_size(ta_ssid, 288, 36);
  lv_obj_align(ta_ssid, LV_ALIGN_TOP_MID, 0, 62);
  lv_textarea_set_one_line(ta_ssid, true);
  lv_textarea_set_text(ta_ssid, wifiSSID);
  lv_obj_set_style_bg_color(ta_ssid, C_CARD, 0);
  lv_obj_set_style_text_color(ta_ssid, C_WHITE, 0);
  lv_obj_set_style_border_color(ta_ssid, C_ACCENT, 0);
  lv_obj_set_style_border_width(ta_ssid, 1, 0);
  lv_obj_set_style_radius(ta_ssid, 6, 0);
  lv_obj_add_event_cb(ta_ssid, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

  // Password
  lv_obj_t *lbl_p = lv_label_create(scr_wifi);
  lv_label_set_text(lbl_p, "Password");
  lv_obj_set_style_text_font(lbl_p, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_p, C_MUTED, 0);
  lv_obj_align(lbl_p, LV_ALIGN_TOP_LEFT, 16, 106);

  ta_pass = lv_textarea_create(scr_wifi);
  lv_obj_set_size(ta_pass, 288, 36);
  lv_obj_align(ta_pass, LV_ALIGN_TOP_MID, 0, 124);
  lv_textarea_set_one_line(ta_pass, true);
  lv_textarea_set_password_mode(ta_pass, true);
  lv_textarea_set_text(ta_pass, wifiPASS);
  lv_obj_set_style_bg_color(ta_pass, C_CARD, 0);
  lv_obj_set_style_text_color(ta_pass, C_WHITE, 0);
  lv_obj_set_style_border_color(ta_pass, C_ACCENT, 0);
  lv_obj_set_style_border_width(ta_pass, 1, 0);
  lv_obj_set_style_radius(ta_pass, 6, 0);
  lv_obj_add_event_cb(ta_pass, ta_focus_cb, LV_EVENT_FOCUSED, NULL);

  // Status
  lbl_status = lv_label_create(scr_wifi);
  lv_label_set_text(lbl_status, wifiConnected ? "Connected" : "Not connected");
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 168);
  lv_obj_set_style_text_color(lbl_status, wifiConnected ? C_GREEN : C_MUTED, 0);

  // Connect button
  lv_obj_t *btn_conn = lv_button_create(scr_wifi);
  lv_obj_set_size(btn_conn, 110, 36);
  lv_obj_align(btn_conn, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
  lv_obj_set_style_bg_color(btn_conn, C_ACCENT, 0);
  lv_obj_set_style_radius(btn_conn, 18, 0);
  lv_obj_add_event_cb(btn_conn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_conn = lv_label_create(btn_conn);
  lv_label_set_text(lbl_conn, "Connect");
  lv_obj_set_style_text_font(lbl_conn, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_conn, C_BG, 0);
  lv_obj_center(lbl_conn);

  // Back button
  lv_obj_t *btn_back = lv_button_create(scr_wifi);
  lv_obj_set_size(btn_back, 80, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_LEFT, 10, -8);
  lv_obj_set_style_bg_color(btn_back, C_CARD, 0);
  lv_obj_set_style_border_color(btn_back, C_ACCENT, 0);
  lv_obj_set_style_border_width(btn_back, 1, 0);
  lv_obj_set_style_radius(btn_back, 18, 0);
  lv_obj_add_event_cb(btn_back, back_to_clock_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "Back");
  lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_back, C_ACCENT, 0);
  lv_obj_center(lbl_back);

  // Keyboard
  kb_wifi = lv_keyboard_create(scr_wifi);
  lv_obj_set_size(kb_wifi, SCREEN_W, SCREEN_H / 2);
  lv_obj_align(kb_wifi, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(kb_wifi, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(kb_wifi, kb_event_cb, LV_EVENT_ALL, NULL);
}

// ─────────────────────────────────────────────────────────────
//  NAV callbacks
// ─────────────────────────────────────────────────────────────
static void nav_to_alarm_cb(lv_event_t *e) {
  if (scr_alarm) { lv_obj_delete(scr_alarm); scr_alarm = NULL; }
  build_alarm_screen();
  lv_screen_load(scr_alarm);
}

static void nav_to_wifi_cb(lv_event_t *e) {
  if (scr_wifi) { lv_obj_delete(scr_wifi); scr_wifi = NULL; }
  build_wifi_screen();
  lv_screen_load(scr_wifi);
}

static void back_to_clock_cb(lv_event_t *e) {
  lv_screen_load(scr_clock);
}

static void alarm_save_cb(lv_event_t *e) {
  alarmHour    = lv_roller_get_selected(roller_alarm_h) + 1;
  alarmMin     = lv_roller_get_selected(roller_alarm_m);
  alarmEnabled = lv_obj_has_state(sw_alarm, LV_STATE_CHECKED);
  alarmFired   = false;

  // Update alarm badge text
  char alarm_text[32];
  if (alarmEnabled) {
    snprintf(alarm_text, sizeof(alarm_text), "Alarm %02d:%02d", alarmHour, alarmMin);
  } else {
    strcpy(alarm_text, "Alarm OFF");
  }
  if (lbl_badge_alarm) lv_label_set_text(lbl_badge_alarm, alarm_text);
  if (badge_alarm) lv_obj_set_style_bg_color(badge_alarm, alarmEnabled ? C_ACCENT : C_MUTED, 0);

  buzz(1);
  lv_screen_load(scr_clock);
}

static void ampm_toggle_cb(lv_event_t *e) {
  alarmAM = !alarmAM;
  lv_label_set_text(lbl_ampm_btn, alarmAM ? "AM" : "PM");
  lv_obj_set_style_bg_color(btn_alarm_ampm, alarmAM ? C_ACCENT : C_MUTED, 0);
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║     ESP32-CYD Clock Starting...       ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // Initialize LVGL
  lv_init();
  
  // Create LVGL display with double buffer
  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, disp_flush);
  lv_display_set_buffers(disp, draw_buf, draw_buf2, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  
  // Initialize touch
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(2);
  
  // Create LVGL input device
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);
  
  // Build and load clock screen
  build_clock_screen();
  lv_screen_load(scr_clock);
  
  // Create timers
  timer_clock = lv_timer_create(clock_timer_cb, 1000, NULL);
  wifi_check_timer = lv_timer_create(check_wifi_connection, 500, NULL);
  
  // Start WiFi connection (non-blocking)
  connect_wifi();
  
  Serial.println("Setup complete - Display should be visible now");
  
  // Test buzzer
  buzz(1);
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  lv_timer_handler();
  lv_tick_inc(5);
  delay(5);
}