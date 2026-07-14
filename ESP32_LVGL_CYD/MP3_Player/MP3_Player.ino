/*
 * ============================================================
 *  ESP32 CYD – MP3 Player with Memory Match Game (SD Fixed)
 *  Target  : ESP32-2432S028R (Cheap Yellow Display)
 * ============================================================
 */

// ── Core libraries ────────────────────────────────────────────
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SD.h>
#include <SPI.h>
#include <Audio.h>

// ── Memory Optimization ───────────────────────────────────────
#define LV_USE_ANIMATIONS 0
#define LV_USE_TRANSITIONS 0
#define LV_USE_LOG 0

// ── Pin Definitions for CYD ──────────────────────────────────
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define SD_CS        5
#define SD_MOSI      23
#define SD_MISO      19
#define SD_SCLK      18
#define I2S_DOUT      26
#define I2S_BCLK      27
#define I2S_LRC       14
#define SCREEN_W      320
#define SCREEN_H      240

// ── Draw buffer ──────────────────────────────────────────────
#define DRAW_BUF_SIZE (SCREEN_W * 20)
static uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// ── Global Objects ───────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI();
SPIClass touchSPI = SPIClass(VSPI);
SPIClass sdSPI = SPIClass(HSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
Audio audio;

// Screens
static lv_obj_t *scr_player = nullptr;
static lv_obj_t *scr_game = nullptr;

// Player widgets
static lv_obj_t *lbl_song_name = nullptr;
static lv_obj_t *lbl_time = nullptr;
static lv_obj_t *slider_progress = nullptr;
static lv_obj_t *slider_volume = nullptr;
static lv_obj_t *btn_play_pause = nullptr;
static lv_obj_t *lbl_play_icon = nullptr;
static lv_obj_t *game_score_label = nullptr;

// Game variables
#define GRID_SIZE 4
static lv_obj_t *card_buttons[GRID_SIZE][GRID_SIZE];
static int card_values[GRID_SIZE][GRID_SIZE];
static bool card_flipped[GRID_SIZE][GRID_SIZE];
static bool card_matched[GRID_SIZE][GRID_SIZE];
static int first_card_x = -1, first_card_y = -1;
static int second_card_x = -1, second_card_y = -1;
static bool waiting = false;
static int game_score = 0;
static int game_moves = 0;
static int high_score = 0;

// Card symbols
const char* card_symbols[] = {"🐶","🐱","🐭","🐹","🐰","🦊","🐻","🐼"};

// Playlist
static String song_list[20];
static int song_count = 0;
static int current_song = 0;
static bool is_playing = false;
static unsigned long last_update = 0;
static bool sd_mounted = false;

// Colors
#define C_BG      lv_color_hex(0x1a1a2e)
#define C_PLAYER  lv_color_hex(0x16213e)
#define C_ACCENT  lv_color_hex(0xe94560)
#define C_CARD_BG lv_color_hex(0x0f3460)
#define C_CARD    lv_color_hex(0x533483)
#define C_MATCH   lv_color_hex(0x4caf50)
#define C_WHITE   lv_color_hex(0xeeeeee)

// ─────────────────────────────────────────────────────────────
//  Display Flush Callback for LVGL
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
//  Touchscreen Callback
// ─────────────────────────────────────────────────────────────
static void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    int16_t x = map(p.x, 200, 3700, 0, SCREEN_W);
    int16_t y = map(p.y, 200, 3800, 0, SCREEN_H);
    x = constrain(x, 0, SCREEN_W - 1);
    y = constrain(y, 0, SCREEN_H - 1);
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ──
//  Audio Functions
// ─────────────────────────────────────────────────────────────
void scan_sd_card() {
  Serial.println("Scanning SD card...");
  
  if (!sd_mounted) {
    Serial.println("SD Card not mounted!");
    return;
  }
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open root directory");
    return;
  }
  
  song_count = 0;
  while (File file = root.openNextFile()) {
    String name = file.name();
    if (name.endsWith(".mp3") || name.endsWith(".MP3")) {
      song_list[song_count++] = name;
      Serial.print("Found: ");
      Serial.println(name);
      if (song_count >= 20) break;
    }
    file.close();
  }
  root.close();
  
  Serial.print("Total songs: ");
  Serial.println(song_count);
}

void play_song(int index) {
  if (!sd_mounted) {
    Serial.println("No SD card!");
    return;
  }
  
  if (index >= 0 && index < song_count) {
    String path = "/" + song_list[index];
    Serial.print("Playing: ");
    Serial.println(path);
    
    audio.stopSong();
    delay(50);
    audio.connecttoFS(SD, path.c_str());
    is_playing = true;
    
    if (lbl_song_name) {
      String short_name = song_list[index];
      if (short_name.length() > 25) short_name = short_name.substring(0, 22) + "...";
      lv_label_set_text(lbl_song_name, short_name.c_str());
    }
    if (lbl_play_icon) lv_label_set_text(lbl_play_icon, LV_SYMBOL_PAUSE);
  }
}

void next_song() {
  if (song_count > 0) {
    current_song = (current_song + 1) % song_count;
    play_song(current_song);
  }
}

void prev_song() {
  if (song_count > 0) {
    current_song = (current_song - 1 + song_count) % song_count;
    play_song(current_song);
  }
}

void pause_play() {
  if (is_playing) {
    audio.stopSong();
    is_playing = false;
    if (lbl_play_icon) lv_label_set_text(lbl_play_icon, LV_SYMBOL_PLAY);
  } else if (song_count > 0) {
    play_song(current_song);
  }
}

void set_volume(int vol) {
  vol = constrain(vol, 0, 21);
  audio.setVolume(vol);
}

// ─────────────────────────────────────────────────────────────
//  Game Functions
// ─────────────────────────────────────────────────────────────
void shuffle_cards() {
  int total_cards = GRID_SIZE * GRID_SIZE;
  int num_pairs = total_cards / 2;
  int symbols_used[16] = {0};
  
  for (int i = 0; i < num_pairs; i++) {
    int symbol_index = i % 8;
    for (int j = 0; j < 2; j++) {
      int pos;
      do {
        pos = random(total_cards);
      } while (symbols_used[pos] != 0);
      symbols_used[pos] = symbol_index + 1;
    }
  }
  
  int idx = 0;
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      card_values[i][j] = symbols_used[idx++] - 1;
      card_flipped[i][j] = false;
      card_matched[i][j] = false;
    }
  }
}

void update_card_display(int x, int y) {
  if (!card_buttons[x][y]) return;
  
  lv_obj_t *btn = card_buttons[x][y];
  lv_obj_t *lbl = (lv_obj_t *)lv_obj_get_child(btn, 0);
  
  if (card_matched[x][y]) {
    lv_obj_set_style_bg_color(btn, C_MATCH, 0);
    lv_label_set_text(lbl, LV_SYMBOL_OK);
  } else if (card_flipped[x][y]) {
    lv_obj_set_style_bg_color(btn, C_ACCENT, 0);
    lv_label_set_text(lbl, card_symbols[card_values[x][y]]);
  } else {
    lv_obj_set_style_bg_color(btn, C_CARD, 0);
    lv_label_set_text(lbl, "?");
  }
}

void check_match() {
  game_moves++;
  
  if (card_values[first_card_x][first_card_y] == card_values[second_card_x][second_card_y]) {
    card_matched[first_card_x][first_card_y] = true;
    card_matched[second_card_x][second_card_y] = true;
    update_card_display(first_card_x, first_card_y);
    update_card_display(second_card_x, second_card_y);
    game_score += 10;
    
    bool all_matched = true;
    for (int i = 0; i < GRID_SIZE; i++) {
      for (int j = 0; j < GRID_SIZE; j++) {
        if (!card_matched[i][j]) {
          all_matched = false;
          break;
        }
      }
    }
    
    if (all_matched) {
      if (game_score > high_score) high_score = game_score;
      lv_label_set_text_fmt(game_score_label, "Score:%d Moves:%d Best:%d", 
                            game_score, game_moves, high_score);
    }
  } else {
    waiting = true;
    lv_timer_create([](lv_timer_t *t) {
      if (first_card_x >= 0 && second_card_x >= 0) {
        card_flipped[first_card_x][first_card_y] = false;
        card_flipped[second_card_x][second_card_y] = false;
        update_card_display(first_card_x, first_card_y);
        update_card_display(second_card_x, second_card_y);
      }
      first_card_x = -1;
      second_card_x = -1;
      waiting = false;
      lv_timer_delete(t);
    }, 800, nullptr);
  }
  
  first_card_x = -1;
  second_card_x = -1;
  lv_label_set_text_fmt(game_score_label, "Score:%d Moves:%d Best:%d", 
                        game_score, game_moves, high_score);
}

void card_click_cb(lv_event_t *e) {
  if (waiting) return;
  
  int pos = (int)(long)lv_event_get_user_data(e);
  int x = pos / 10;
  int y = pos % 10;
  
  if (card_matched[x][y] || card_flipped[x][y]) return;
  
  if (first_card_x == -1) {
    first_card_x = x;
    first_card_y = y;
    card_flipped[x][y] = true;
    update_card_display(x, y);
  } else if (second_card_x == -1 && (first_card_x != x || first_card_y != y)) {
    second_card_x = x;
    second_card_y = y;
    card_flipped[x][y] = true;
    update_card_display(x, y);
    check_match();
  }
}

void new_game() {
  game_score = 0;
  game_moves = 0;
  first_card_x = -1;
  second_card_x = -1;
  waiting = false;
  shuffle_cards();
  
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      card_flipped[i][j] = false;
      card_matched[i][j] = false;
      update_card_display(i, j);
    }
  }
  lv_label_set_text_fmt(game_score_label, "Score:0 Moves:0 Best:%d", high_score);
}

// ─────────────────────────────────────────────────────────────
//  UI Navigation
// ─────────────────────────────────────────────────────────────
static void switch_to_player(lv_event_t *e) {
  lv_screen_load(scr_player);
  if (!is_playing && song_count > 0) {
    play_song(current_song);
  }
}

static void switch_to_game(lv_event_t *e) {
  if (is_playing) {
    audio.stopSong();
    is_playing = false;
  }
  lv_screen_load(scr_game);
  new_game();
}

// ─────────────────────────────────────────────────────────────
//  Build Player Screen
// ─────────────────────────────────────────────────────────────
void build_player_screen() {
  scr_player = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_player, C_BG, 0);
  lv_obj_set_style_bg_opa(scr_player, LV_OPA_COVER, 0);
  
  lv_obj_t *title = lv_label_create(scr_player);
  lv_label_set_text(title, "🎵 MP3 Player");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(title, C_ACCENT, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
  
  lv_obj_t *song_card = lv_obj_create(scr_player);
  lv_obj_set_size(song_card, 280, 50);
  lv_obj_align(song_card, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_color(song_card, C_PLAYER, 0);
  lv_obj_set_style_border_width(song_card, 0, 0);
  lv_obj_set_style_radius(song_card, 10, 0);
  
  lbl_song_name = lv_label_create(song_card);
  lv_label_set_text(lbl_song_name, sd_mounted ? "No songs" : "SD Card Error");
  lv_obj_set_style_text_font(lbl_song_name, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_song_name, C_WHITE, 0);
  lv_obj_center(lbl_song_name);
  
  slider_progress = lv_slider_create(scr_player);
  lv_obj_set_size(slider_progress, 260, 6);
  lv_obj_align(slider_progress, LV_ALIGN_CENTER, 0, -30);
  lv_obj_set_style_bg_color(slider_progress, C_PLAYER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider_progress, C_ACCENT, LV_PART_INDICATOR);
  lv_slider_set_range(slider_progress, 0, 100);
  
  lbl_time = lv_label_create(scr_player);
  lv_label_set_text(lbl_time, "0:00/0:00");
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_12, 0);
  lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -5);
  
  // Control buttons
  lv_obj_t *btn_prev = lv_btn_create(scr_player);
  lv_obj_set_size(btn_prev, 50, 50);
  lv_obj_align(btn_prev, LV_ALIGN_CENTER, -90, 50);
  lv_obj_set_style_bg_color(btn_prev, C_PLAYER, 0);
  lv_obj_set_style_radius(btn_prev, 25, 0);
  
  lv_obj_t *lbl_prev = lv_label_create(btn_prev);
  lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
  lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_24, 0);
  lv_obj_center(lbl_prev);
  lv_obj_add_event_cb(btn_prev, [](lv_event_t *e) { prev_song(); }, LV_EVENT_CLICKED, NULL);
  
  btn_play_pause = lv_btn_create(scr_player);
  lv_obj_set_size(btn_play_pause, 60, 60);
  lv_obj_align(btn_play_pause, LV_ALIGN_CENTER, 0, 50);
  lv_obj_set_style_bg_color(btn_play_pause, C_ACCENT, 0);
  lv_obj_set_style_radius(btn_play_pause, 30, 0);
  
  lbl_play_icon = lv_label_create(btn_play_pause);
  lv_label_set_text(lbl_play_icon, LV_SYMBOL_PLAY);
  lv_obj_set_style_text_font(lbl_play_icon, &lv_font_montserrat_28, 0);
  lv_obj_center(lbl_play_icon);
  lv_obj_add_event_cb(btn_play_pause, [](lv_event_t *e) { pause_play(); }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *btn_next = lv_btn_create(scr_player);
  lv_obj_set_size(btn_next, 50, 50);
  lv_obj_align(btn_next, LV_ALIGN_CENTER, 90, 50);
  lv_obj_set_style_bg_color(btn_next, C_PLAYER, 0);
  lv_obj_set_style_radius(btn_next, 25, 0);
  
  lv_obj_t *lbl_next = lv_label_create(btn_next);
  lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
  lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_24, 0);
  lv_obj_center(lbl_next);
  lv_obj_add_event_cb(btn_next, [](lv_event_t *e) { next_song(); }, LV_EVENT_CLICKED, NULL);
  
  // Volume
  slider_volume = lv_slider_create(scr_player);
  lv_obj_set_size(slider_volume, 120, 6);
  lv_obj_align(slider_volume, LV_ALIGN_BOTTOM_LEFT, 50, -15);
  lv_slider_set_range(slider_volume, 0, 21);
  lv_slider_set_value(slider_volume, 15, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider_volume, [](lv_event_t *e) {
    set_volume(lv_slider_get_value(slider_volume));
  }, LV_EVENT_VALUE_CHANGED, NULL);
  
  lv_obj_t *vol_label = lv_label_create(scr_player);
  lv_label_set_text(vol_label, LV_SYMBOL_VOLUME_MAX);
  lv_obj_set_style_text_font(vol_label, &lv_font_montserrat_14, 0);
  lv_obj_align(vol_label, LV_ALIGN_BOTTOM_LEFT, 15, -15);
  
  // Game button
  lv_obj_t *btn_game = lv_btn_create(scr_player);
  lv_obj_set_size(btn_game, 90, 36);
  lv_obj_align(btn_game, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn_game, C_CARD_BG, 0);
  lv_obj_set_style_radius(btn_game, 18, 0);
  
  lv_obj_t *lbl_game = lv_label_create(btn_game);
  lv_label_set_text(lbl_game, "🎮 Game");
  lv_obj_center(lbl_game);
  lv_obj_add_event_cb(btn_game, switch_to_game, LV_EVENT_CLICKED, NULL);
}

// ─────────────────────────────────────────────────────────────
//  Build Game Screen
// ─────────────────────────────────────────────────────────────
void build_game_screen() {
  scr_game = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_game, C_BG, 0);
  lv_obj_set_style_bg_opa(scr_game, LV_OPA_COVER, 0);
  
  lv_obj_t *title = lv_label_create(scr_game);
  lv_label_set_text(title, "🎮 Memory Match Game");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(title, C_ACCENT, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
  
  game_score_label = lv_label_create(scr_game);
  lv_label_set_text(game_score_label, "Score:0 Moves:0 Best:0");
  lv_obj_set_style_text_font(game_score_label, &lv_font_montserrat_12, 0);
  lv_obj_align(game_score_label, LV_ALIGN_TOP_MID, 0, 35);
  
  int card_size = 65;
  int start_x = (SCREEN_W - (card_size * 4 + 24)) / 2;
  int start_y = 65;
  
  for (int i = 0; i < GRID_SIZE; i++) {
    for (int j = 0; j < GRID_SIZE; j++) {
      card_buttons[i][j] = lv_btn_create(scr_game);
      lv_obj_set_size(card_buttons[i][j], card_size, card_size);
      lv_obj_set_pos(card_buttons[i][j], start_x + j * (card_size + 8), start_y + i * (card_size + 8));
      lv_obj_set_style_bg_color(card_buttons[i][j], C_CARD, 0);
      lv_obj_set_style_radius(card_buttons[i][j], 8, 0);
      
      lv_obj_t *lbl = lv_label_create(card_buttons[i][j]);
      lv_label_set_text(lbl, "?");
      lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
      lv_obj_set_style_text_color(lbl, C_WHITE, 0);
      lv_obj_center(lbl);
      
      lv_obj_add_event_cb(card_buttons[i][j], card_click_cb, LV_EVENT_CLICKED, 
                          (void*)((long)(i * 10 + j)));
    }
  }
  
  lv_obj_t *btn_new = lv_btn_create(scr_game);
  lv_obj_set_size(btn_new, 90, 36);
  lv_obj_align(btn_new, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_bg_color(btn_new, C_PLAYER, 0);
  lv_obj_set_style_radius(btn_new, 18, 0);
  
  lv_obj_t *lbl_new = lv_label_create(btn_new);
  lv_label_set_text(lbl_new, "🔄 New");
  lv_obj_center(lbl_new);
  lv_obj_add_event_cb(btn_new, [](lv_event_t *e) { new_game(); }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *btn_back = lv_btn_create(scr_game);
  lv_obj_set_size(btn_back, 90, 36);
  lv_obj_align(btn_back, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn_back, C_CARD_BG, 0);
  lv_obj_set_style_radius(btn_back, 18, 0);
  
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, "🎵 Back");
  lv_obj_center(lbl_back);
  lv_obj_add_event_cb(btn_back, switch_to_player, LV_EVENT_CLICKED, NULL);
}

// ─────────────────────────────────────────────────────────────
//  Audio Callbacks
// ─────────────────────────────────────────────────────────────
void audio_showinfo(const char *info) {
  Serial.print("Info: ");
  Serial.println(info);
}

void audio_id3data(const char *info) {
  Serial.print("ID3: ");
  Serial.println(info);
}

void audio_eof_mp3(const char *info) {
  Serial.println("EOF - Next song");
  next_song();
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   ESP32-CYD MP3 Player + Game        ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Initialize display
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.drawString("ESP32-CYD", 100, 40);
  tft.drawString("MP3 Player", 100, 70);
  delay(1000);
  
  // Initialize LVGL
  lv_init();
  
  lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, disp_flush);
  lv_display_set_buffers(disp, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  
  // Initialize touch using VSPI
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(2);
  
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);
  
  // Initialize SD card on separate HSPI bus
  tft.fillRect(0, 100, 320, 40, TFT_BLACK);
  tft.drawString("Init SD Card...", 80, 110);
  
  sdSPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  delay(100);
  
  if (!SD.begin(SD_CS, sdSPI)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("SD Card Error!", 90, 140);
    Serial.println("SD Card Mount Failed!");
    sd_mounted = false;
  } else {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("SD Card OK!", 105, 140);
    Serial.println("SD Card OK");
    sd_mounted = true;
    scan_sd_card();
  }
  
  // Initialize audio
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(15);
  
  // Build screens
  tft.drawString("Building UI...", 100, 170);
  build_player_screen();
  build_game_screen();
  
  // Load player screen
  lv_screen_load(scr_player);
  
  delay(1000);
  tft.fillScreen(TFT_BLACK);
  
  // Start playing if songs available
  if (sd_mounted && song_count > 0) {
    current_song = 0;
    play_song(current_song);
  }
  
  randomSeed(analogRead(34));
  Serial.println("Setup complete!");
}

// ─────────────────────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  audio.loop();
  lv_timer_handler();
  lv_tick_inc(5);
  delay(5);
  
  // Update progress
  if (millis() - last_update > 500 && is_playing && sd_mounted) {
    int pos = audio.getAudioCurrentTime();
    int total = audio.getAudioFileDuration();
    if (total > 0) {
      int percent = (pos * 100) / total;
      lv_slider_set_value(slider_progress, percent, LV_ANIM_OFF);
      lv_label_set_text_fmt(lbl_time, "%d:%02d/%d:%02d", 
                            pos/60, pos%60, total/60, total%60);
    }
    last_update = millis();
  }
}