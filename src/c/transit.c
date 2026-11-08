// Transit -- Watchface im Stil einer Flip-Dot- bzw. Segmentanzeige.
//
// Zwei grosse Ziffernpaare untereinander: oben die Stunden, darunter die
// Minuten. Die dritte Zeile zeigt je nach Einstellung Datum, Schritte,
// Herzfrequenz oder Tiefst-/Hoechsttemperatur. Alle Zeichen sind als
// 5x7-Punktmatrix im Code hinterlegt; benachbarte Punkte werden zu
// Segmenten verschmolzen, damit die Anzeige wie die Vorlage aussieht.

#include <pebble.h>

// ---- Farben ----------------------------------------------------------------
// Die gewuenschten Farben sind auf die 64-Farben-Palette des Pebble gemappt.
// Ein GColor8 hat je Kanal zwei Bit; die Kommentare nennen den Palettenwert.
//   Classic Amber        #FF9900 -> ChromeYellow #FFAA00, Platte Schwarz
//   Neon Green          #00FF41 -> Malachite    #00FF55, Platte Schwarz
//   High-Contrast Cyan  #00E5FF -> Cyan         #00FFFF, Platte OxfordBlue
//   Bi-Color Flip Dot   #E6E65C -> Icterine     #FFFF55, Platte DarkGray
//   Monochrome White    #FFFFFF -> White,               Platte Schwarz
//   Monochrome Inverse  #202020 -> Schwarz,             Platte LightGray
#define THEME_COUNT 6

typedef struct {
  GColor fg;
  GColor bg;
} Theme;

static const Theme THEMES[THEME_COUNT] = {
  { {GColorChromeYellowARGB8}, {GColorBlackARGB8} },
  { {GColorMalachiteARGB8},    {GColorBlackARGB8} },
  { {GColorCyanARGB8},         {GColorOxfordBlueARGB8} },
  { {GColorIcterineARGB8},     {GColorDarkGrayARGB8} },
  { {GColorWhiteARGB8},        {GColorBlackARGB8} },
  { {GColorBlackARGB8},        {GColorLightGrayARGB8} },
};

// ---- Layout ----------------------------------------------------------------
// Auf Emery ist das Display rechteckig, auf Gabbro rund. Dort sind die
// Ziffern etwas groesser, der Block sitzt aber etwas tiefer, damit die oberen
// Ecken im Kreis bleiben.
#ifdef PBL_PLATFORM_EMERY
#define CELL_BIG        11
#define DIGIT_GAP       20
#define ROW_GAP         18
#define INFO_GAP        12
#define BLOCK_OFFSET_Y   0
#else
#define CELL_BIG        12
#define DIGIT_GAP       22
#define ROW_GAP         16
#define INFO_GAP        10
#define BLOCK_OFFSET_Y   4
#endif

#define GLYPH_COLS       5
#define GLYPH_ROWS       7
#define INFO_CELL        2
#define AMPM_GAP         8

// ---- Daten ----------------------------------------------------------------
#define WEATHER_INTERVAL_MIN 30
#define WEATHER_STALE_SEC    (2 * 60 * 60)

#define DEMO_STEPS       8642
#define DEMO_HEART       72
#define DEMO_TEMP_MIN    4
#define DEMO_TEMP_MAX    12

// ---- AppMessage ------------------------------------------------------------
// Clay schickt alle Keys in einer Nachricht; ist die Inbox zu klein, verwirft
// die Firmware die ganze Nachricht stumm. Faustformel: 1 + 7 je Key + Nutzdaten.
#define INBOX_SIZE       256
#define OUTBOX_SIZE       64

// ---- Einstellungen ---------------------------------------------------------
// Das Struct wird als Byte-Dump gespeichert, die Feldreihenfolge ist also Teil
// des Speicherformats. Neue Felder NUR am Ende anhaengen.
#define SETTINGS_KEY      1
#define PKEY_TEMP_MIN     2
#define PKEY_TEMP_MAX     3
#define PKEY_WX_UNIT      4
#define PKEY_TEMP_TIME    5
#define SETTINGS_VERSION  1

typedef enum {
  INFO_DATE = 0,
  INFO_STEPS,
  INFO_HEART,
  INFO_TEMP,
  INFO_COUNT
} InfoMode;

typedef struct {
  uint8_t version;
  uint8_t theme;
  uint8_t info_mode;
} Settings;

static Window *s_window;
static Layer *s_canvas;
static Settings s_settings;
static int s_temp_min;
static int s_temp_max;
static bool s_wx_unit;          // false = Celsius, true = Fahrenheit
static time_t s_temp_time;      // 0 = noch nie empfangen
static bool s_demo;

// ---------------------------------------------------------------------------
// Zeichensatz: 5 Spalten x 7 Zeilen, Bit 4 ist die linke Spalte.
// ---------------------------------------------------------------------------
static const uint8_t GLYPH_DIGITS[10][GLYPH_ROWS] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
  {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 2
  {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, // 3
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
};

static const uint8_t GLYPH_LETTERS[26][GLYPH_ROWS] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
  {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, // W
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
};

static const uint8_t GLYPH_SPACE[GLYPH_ROWS]  = {0,0,0,0,0,0,0};
static const uint8_t GLYPH_SLASH[GLYPH_ROWS]  = {0x00,0x01,0x02,0x04,0x08,0x10,0x00};
static const uint8_t GLYPH_DEGREE[GLYPH_ROWS] = {0x0C,0x12,0x12,0x0C,0x00,0x00,0x00};
static const uint8_t GLYPH_DASH[GLYPH_ROWS]   = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};

static const uint8_t *glyph_rows(char c) {
  if (c >= '0' && c <= '9') return GLYPH_DIGITS[c - '0'];
  if (c >= 'A' && c <= 'Z') return GLYPH_LETTERS[c - 'A'];
  switch (c) {
    case '/': return GLYPH_SLASH;
    case '~': return GLYPH_DEGREE;   // Ersatzzeichen fuer das Grad-Symbol
    case '-': return GLYPH_DASH;
    default:  return GLYPH_SPACE;
  }
}

static bool row_bit(const uint8_t *rows, int r, int c) {
  if (r < 0 || r >= GLYPH_ROWS || c < 0 || c >= GLYPH_COLS) return false;
  return (rows[r] >> (GLYPH_COLS - 1 - c)) & 1;
}

// Ein Zeichen besteht aus waagerechten und senkrechten Laeufen ab zwei
// Punkten (volle Rundung) und einzelnen Punkten (schwache Rundung). So
// verschmelzen die Punkte zu Segmenten wie in der Vorlage.
static void draw_glyph(GContext *ctx, char c, int x, int y, int cell,
                       GColor color) {
  const uint8_t *rows = glyph_rows(c);
  int radius_run = cell / 2;
  int radius_dot = (cell * 3) / 10;
  if (radius_dot < 1) radius_dot = 1;

  graphics_context_set_fill_color(ctx, color);

  for (int r = 0; r < GLYPH_ROWS; r++) {
    int col = 0;
    while (col < GLYPH_COLS) {
      if (row_bit(rows, r, col) && row_bit(rows, r, col + 1)) {
        int start = col;
        while (col < GLYPH_COLS && row_bit(rows, r, col)) col++;
        graphics_fill_rect(ctx,
            GRect(x + start * cell, y + r * cell,
                  (col - start) * cell, cell),
            radius_run, GCornersAll);
      } else {
        col++;
      }
    }
  }

  for (int col = 0; col < GLYPH_COLS; col++) {
    int r = 0;
    while (r < GLYPH_ROWS) {
      if (row_bit(rows, r, col) && row_bit(rows, r + 1, col)) {
        int start = r;
        while (r < GLYPH_ROWS && row_bit(rows, r, col)) r++;
        graphics_fill_rect(ctx,
            GRect(x + col * cell, y + start * cell,
                  cell, (r - start) * cell),
            radius_run, GCornersAll);
      } else {
        r++;
      }
    }
  }

  for (int r = 0; r < GLYPH_ROWS; r++) {
    for (int col = 0; col < GLYPH_COLS; col++) {
      if (!row_bit(rows, r, col)) continue;
      bool left   = row_bit(rows, r, col - 1);
      bool right  = row_bit(rows, r, col + 1);
      bool up     = row_bit(rows, r - 1, col);
      bool down   = row_bit(rows, r + 1, col);
      if (!left && !right && !up && !down) {
        graphics_fill_rect(ctx,
            GRect(x + col * cell, y + r * cell, cell, cell),
            radius_dot, GCornersAll);
      }
    }
  }
}

static int text_width(const char *s, int cell) {
  int n = strlen(s);
  return n ? (n * 6 - 1) * cell : 0;
}

static void draw_text(GContext *ctx, const char *s, int x, int y, int cell,
                      GColor color) {
  for (int i = 0; s[i]; i++) {
    draw_glyph(ctx, s[i], x + i * 6 * cell, y, cell, color);
  }
}

// ---------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------
static void settings_defaults(void) {
  s_settings.version = SETTINGS_VERSION;
  s_settings.theme = 0;
  s_settings.info_mode = INFO_DATE;
}

static void settings_load(void) {
  settings_defaults();
  if (persist_exists(SETTINGS_KEY)) {
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  }
  if (s_settings.theme >= THEME_COUNT) s_settings.theme = 0;
  if (s_settings.info_mode >= INFO_COUNT) s_settings.info_mode = INFO_DATE;

  if (persist_exists(PKEY_TEMP_MIN))  s_temp_min  = persist_read_int(PKEY_TEMP_MIN);
  if (persist_exists(PKEY_TEMP_MAX))  s_temp_max  = persist_read_int(PKEY_TEMP_MAX);
  if (persist_exists(PKEY_WX_UNIT))   s_wx_unit   = persist_read_int(PKEY_WX_UNIT) != 0;
  if (persist_exists(PKEY_TEMP_TIME)) s_temp_time = (time_t)persist_read_int(PKEY_TEMP_TIME);
}

static void settings_save(void) {
  s_settings.version = SETTINGS_VERSION;
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// ---------------------------------------------------------------------------
// Daten fuer die dritte Zeile
// ---------------------------------------------------------------------------
static int health_today(HealthMetric metric) {
  HealthServiceAccessibilityMask m = health_service_metric_accessible(
      metric, time_start_of_today(), time(NULL));
  if (!(m & HealthServiceAccessibilityMaskAvailable)) return 0;
  return (int)health_service_sum_today(metric);
}

static int heart_bpm(void) {
#ifdef PBL_PLATFORM_EMERY
  return (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
#else
  return 0;   // Gabbro hat keinen Pulssensor
#endif
}

static void info_text(struct tm *t, char *buf, size_t len) {
  static const char *DAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

  switch (s_settings.info_mode) {
    case INFO_STEPS: {
      int steps = s_demo ? DEMO_STEPS : health_today(HealthMetricStepCount);
      if (steps < 0) steps = 0;
      if (steps > 99999) steps = 99999;
      snprintf(buf, len, "STEPS %d", steps);
      break;
    }
    case INFO_HEART: {
      int bpm = s_demo ? DEMO_HEART : heart_bpm();
      if (bpm > 0) snprintf(buf, len, "BPM %d", bpm);
      else         snprintf(buf, len, "BPM --");
      break;
    }
    case INFO_TEMP: {
      bool fresh = s_temp_time != 0 &&
                   time(NULL) - s_temp_time < WEATHER_STALE_SEC;
      char unit = s_wx_unit ? 'F' : 'C';
      if (s_demo) {
        snprintf(buf, len, "%d/%d~%c", DEMO_TEMP_MIN, DEMO_TEMP_MAX, 'C');
      } else if (fresh) {
        int lo = s_temp_min;
        int hi = s_temp_max;
        if (lo < -99) lo = -99;
        if (lo > 999) lo = 999;
        if (hi < -99) hi = -99;
        if (hi > 999) hi = 999;
        snprintf(buf, len, "%d/%d~%c", lo, hi, unit);
      } else {
        snprintf(buf, len, "--/--~%c", unit);
      }
      break;
    }
    case INFO_DATE:
    default:
      snprintf(buf, len, "%s %02d", DAYS[t->tm_wday], t->tm_mday);
      break;
  }
}

// ---------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------
static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  const Theme *th = &THEMES[s_settings.theme];

  graphics_context_set_fill_color(ctx, th->bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  bool h24 = clock_is_24h_style();

  int hour = t->tm_hour;
  if (!h24) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }

  static char hour_buf[4];
  static char min_buf[4];
  if (h24) snprintf(hour_buf, sizeof(hour_buf), "%02d", hour);
  else     snprintf(hour_buf, sizeof(hour_buf), "%2d", hour);
  snprintf(min_buf, sizeof(min_buf), "%02d", t->tm_min);

  int gw = GLYPH_COLS * CELL_BIG;
  int gh = GLYPH_ROWS * CELL_BIG;
  int pair_w = 2 * gw + DIGIT_GAP;
  int block_h = 2 * gh + ROW_GAP + INFO_GAP + GLYPH_ROWS * INFO_CELL;
  int x0 = (b.size.w - pair_w) / 2;
  int top = (b.size.h - block_h) / 2 + BLOCK_OFFSET_Y;

  draw_glyph(ctx, hour_buf[0], x0, top, CELL_BIG, th->fg);
  draw_glyph(ctx, hour_buf[1], x0 + gw + DIGIT_GAP, top, CELL_BIG, th->fg);

  int minute_y = top + gh + ROW_GAP;
  draw_glyph(ctx, min_buf[0], x0, minute_y, CELL_BIG, th->fg);
  draw_glyph(ctx, min_buf[1], x0 + gw + DIGIT_GAP, minute_y, CELL_BIG, th->fg);

  if (!h24) {
    char ap = t->tm_hour < 12 ? 'A' : 'P';
    draw_glyph(ctx, ap, x0 + pair_w + AMPM_GAP, top + CELL_BIG / 2,
               INFO_CELL, th->fg);
  }

  static char info[24];
  info_text(t, info, sizeof(info));
  int info_y = top + 2 * gh + ROW_GAP + INFO_GAP;
  draw_text(ctx, info, (b.size.w - text_width(info, INFO_CELL)) / 2, info_y,
            INFO_CELL, th->fg);
}

// ---------------------------------------------------------------------------
// Ereignisse
// ---------------------------------------------------------------------------
static void request_weather(void) {
  if (s_settings.info_mode != INFO_TEMP) return;
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
    app_message_outbox_send();
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  (void)changed;
  if (s_settings.info_mode == INFO_TEMP &&
      tick_time->tm_min % WEATHER_INTERVAL_MIN == 0) {
    request_weather();
  }
  layer_mark_dirty(s_canvas);
}

static void health_handler(HealthEventType event, void *context) {
  (void)event;
  (void)context;
  if (s_settings.info_mode == INFO_STEPS ||
      s_settings.info_mode == INFO_HEART) {
    layer_mark_dirty(s_canvas);
  }
}

static void bt_handler(bool connected) {
  if (connected) request_weather();
}

// Clay schickt Auswahllisten als Zeichenkette, emu_config.py als Zahl
static int tuple_int(const Tuple *t) {
  if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
  switch (t->length) {
    case 1:  return t->type == TUPLE_INT ? t->value->int8  : t->value->uint8;
    case 2:  return t->type == TUPLE_INT ? t->value->int16 : t->value->uint16;
    default: return t->value->int32;
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  (void)context;
  bool settings_changed = false;
  bool mode_changed = false;
  bool weather_changed = false;
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_THEME))) {
    s_settings.theme = (uint8_t)tuple_int(t);
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_INFO_MODE))) {
    uint8_t mode = (uint8_t)tuple_int(t);
    if (mode != s_settings.info_mode) mode_changed = true;
    s_settings.info_mode = mode;
    settings_changed = true;
  }
  bool unit_changed = dict_find(iter, MESSAGE_KEY_TEMP_UNIT) != NULL;
  if ((t = dict_find(iter, MESSAGE_KEY_TEMP_MIN))) {
    s_temp_min = tuple_int(t);
    weather_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TEMP_MAX))) {
    s_temp_max = tuple_int(t);
    weather_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_WX_UNIT))) {
    s_wx_unit = tuple_int(t) != 0;
    weather_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DEMO))) {
    s_demo = tuple_int(t) != 0;
  }

  if (weather_changed) {
    s_temp_time = time(NULL);
    persist_write_int(PKEY_TEMP_MIN, s_temp_min);
    persist_write_int(PKEY_TEMP_MAX, s_temp_max);
    persist_write_int(PKEY_WX_UNIT, s_wx_unit ? 1 : 0);
    persist_write_int(PKEY_TEMP_TIME, (int32_t)s_temp_time);
  }
  if (settings_changed) {
    settings_save();
    settings_load();
  }
  if (mode_changed || unit_changed) {
    request_weather();
  }
  layer_mark_dirty(s_canvas);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  settings_load();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  health_service_events_subscribe(health_handler, NULL);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bt_handler,
  });

  app_message_register_inbox_received(inbox_received);
  AppMessageResult r = app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  if (r != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "app_message_open: %d", (int)r);
  }

  request_weather();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
