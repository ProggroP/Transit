// Transit -- Watchface im Stil einer Kachel-Anzeige.
//
// Zwei Ziffernpaare untereinander: oben die Stunden, darunter die Minuten. Die
// dritte Zeile zeigt je nach Einstellung Datum, Schritte, Herzfrequenz oder
// Tiefst-/Hoechsttemperatur, oder sie ist aus und die Zeit fuellt die Hoehe.
//
// Jedes Zeichen sitzt auf einer farbigen Platte und besteht aus 3 x 5
// quadratischen Kacheln. Eine Kachel ist leer, voll, an einer Ecke gerundet
// oder eine Dreiecksform (Zeichensatz siehe GLYPHS). Zwischen den Kacheln
// scheint die Platte als feine Rasterlinie durch.

#include <pebble.h>

// ---- Farben ----------------------------------------------------------------
// Die Wunschfarben sind auf die 64-Farben-Palette gemappt. Der Bildschirm um
// die Platten bleibt schwarz wie in der Vorlage. Wo die naechste Palettenfarbe
// der Platte Schwarz waere, die Vorgabe aber eine sichtbare Platte meint
// (Schwarzgrau, Anthrazit), steht DarkGray, sonst waeren die Platten unsichtbar.
//   Transit             Gelb auf Rot, die Farben der Vorlage
//   Classic Amber       #FF9900 -> ChromeYellow, Platte #1C1C1C -> DarkGray
//   Neon Green          #00FF41 -> Malachite,    Platte #000000 -> Black
//   High-Contrast Cyan  #00E5FF -> Cyan,         Platte #0A192F -> OxfordBlue
//   Bi-Color Flip Dot   #E6E65C -> Icterine,     Platte #2B2B2B -> DarkGray
//   Monochrome White    #FFFFFF -> White,        Platte #222222 -> DarkGray
//   Monochrome Inverse  #202020 -> Black,        Platte #D0D0D0 -> LightGray
//   Custom              alle drei Farben frei waehlbar, auch der Bildschirm
#define THEME_PRESETS  7
#define THEME_CUSTOM   7        // Index hinter den festen Schemata
#define THEME_COUNT    8
#define THEME_DEFAULT  6
#define COLOR_SCREEN   GColorBlack
#define CUSTOM_SCREEN_DEFAULT  GColorBlack
#define CUSTOM_PLATE_DEFAULT   GColorRed
#define CUSTOM_FG_DEFAULT      GColorYellow

typedef struct {
  GColor fg;
  GColor plate;
} Theme;

static const Theme THEMES[THEME_PRESETS] = {
  { {GColorChromeYellowARGB8}, {GColorDarkGrayARGB8} },
  { {GColorMalachiteARGB8},    {GColorBlackARGB8} },
  { {GColorCyanARGB8},         {GColorOxfordBlueARGB8} },
  { {GColorIcterineARGB8},     {GColorDarkGrayARGB8} },
  { {GColorWhiteARGB8},        {GColorDarkGrayARGB8} },
  { {GColorBlackARGB8},        {GColorLightGrayARGB8} },
  { {GColorYellowARGB8},       {GColorRedARGB8} },
};

// ---- Layout ----------------------------------------------------------------
// Die Kacheln bleiben quadratisch, die Groesse ergibt sich also aus der Hoehe.
// Auf Gabbro muessen die Plattenecken zusaetzlich im Kreis bleiben.
#ifdef PBL_PLATFORM_EMERY
#define CELL_BIG        16      // Zeit, wenn die dritte Zeile an ist
#define CELL_FULL       21      // Zeit, wenn die dritte Zeile aus ist
#define INFO_MAX_W     190
#else
#define CELL_BIG        16
#define CELL_FULL       20
#define INFO_MAX_W     146      // Kreissehne auf Hoehe der dritten Zeile
#endif

#define CELL_INFO        6      // Kachelgroesse der dritten Zeile
#define CELL_INFO_MIN    4      // Untergrenze, falls der Text sonst nicht passt
#define CELL_AMPM        4

#define GRID             1      // Rasterlinie zwischen den Kacheln und zum Rand
#define PLATE_W(c)      (3 * (c) + 4 * GRID)
#define PLATE_H(c)      (5 * (c) + 6 * GRID)

#define GAP_DIGIT        6      // zwischen den grossen Platten, waagerecht und senkrecht
#define GAP_INFO         5      // zwischen Minutenzeile und dritter Zeile
#define GAP_INFO_CHAR    2      // zwischen den Platten der dritten Zeile
#define GAP_AMPM         4
#define INFO_SPACE_W     8      // Leerzeichen in der dritten Zeile

// ---- Daten ----------------------------------------------------------------
#define WEATHER_INTERVAL_MIN 30
#define WEATHER_STALE_SEC    (2 * 60 * 60)

#define DEMO_HOUR        23
#define DEMO_MINUTE      46
#define DEMO_WDAY        1      // Montag
#define DEMO_MDAY        14
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
  INFO_OFF,
  INFO_COUNT
} InfoMode;

typedef struct {
  uint8_t version;
  uint8_t theme;
  uint8_t info_mode;
  GColor custom_screen;
  GColor custom_plate;
  GColor custom_fg;
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
// Zeichensatz: je 5 Zeilen zu 3 Kacheln.
//   .  leer             #  voll
//   q w e r   volle Kachel, Ecke oben links/oben rechts/unten links/unten rechts rund
//   a b c d   Dreieck, gefuellt unten rechts/unten links/oben links/oben rechts
//   > < ^ v   volle Kachel mit Kerbe von rechts/links/oben/unten bis zur Mitte
//   L V A     nur das Kerbendreieck: von rechts/oben/unten bis zur Mitte
//   k         volle Kachel, flacher Schnitt oben rechts (Bein des K)
//   n m N M   steile Flanken des V: oben links/oben rechts/unten links/unten rechts
// ---------------------------------------------------------------------------
static const char GLYPHS_DIGIT[10][16] = {
  "q#w" "#.#" "#.#" "#.#" "e#r",  // 0, bewusst wie O: liest sich besser als die Null mit Schraegstrich
  "a#." ".#." ".#." ".#." ".#.",  // 1
  "q#w" "..#" "a#c" "#.." "###",  // 2
  "q#w" "..#" ".#>" "..#" "e#r",  // 3
  "#.#" "#.#" "d##" "..#" "..#",  // 4
  "###" "#.." "d#b" "..#" "e#r",  // 5
  "q#w" "#.." "##b" "#.#" "e#r",  // 6
  "###" ".ac" "ac." "#.." "#..",  // 7
  "q#w" "#.#" "<#>" "#.#" "e#r",  // 8
  "q#w" "#.#" "d##" "..#" "e#r",  // 9
};

static const char GLYPHS_ALPHA[26][16] = {
  "q#w" "#.#" "###" "#.#" "#.#",  // A
  "##w" "#.#" "##>" "#.#" "##r",  // B
  "q#w" "#.." "#.." "#.." "e#r",  // C
  "##w" "#.#" "#.#" "#.#" "##r",  // D
  "###" "#.." "##." "#.." "###",  // E
  "###" "#.." "##." "#.." "#..",  // F
  "q#w" "#.." "#L#" "#.#" "e#r",  // G
  "#.#" "#.#" "###" "#.#" "#.#",  // H
  ".#." ".#." ".#." ".#." ".#.",  // I
  "..#" "..#" "..#" "#.#" "e#r",  // J
  "#.#" "#ac" "##k" "#.#" "#.#",  // K
  "#.." "#.." "#.." "#.." "###",  // L
  "#.#" "#^#" "#V#" "#.#" "#.#",  // M
  "#.#" "#b#" "###" "#d#" "#.#",  // N
  "q#w" "#.#" "#.#" "#.#" "e#r",  // O
  "##w" "#.#" "##c" "#.." "#..",  // P
  "q#w" "#.#" "#.#" "#db" "e#d",  // Q
  "##w" "#.#" "##>" "#.#" "#.#",  // R
  "q#w" "#.." "d#b" "..#" "e#r",  // S
  "###" ".#." ".#." ".#." ".#.",  // T
  "#.#" "#.#" "#.#" "#.#" "e#r",  // U
  "#.#" "#.#" "#.#" "n#m" "N#M",  // V
  "#.#" "#.#" "#A#" "#v#" "e.r",  // W
  "#.#" "#.#" "<#>" "#.#" "#.#",  // X
  "#.#" "#.#" "d^c" ".#." ".#.",  // Y
  "###" ".ac" "ac." "#.." "###",  // Z
};

static const char GLYPH_BLANK[16] = "...............";
static const char GLYPH_DASH[16]  = "..." "..." "###" "..." "...";
static const char GLYPH_SLASH[16] = "..." "..a" ".ac" "ac." "c..";

static const char *glyph_cells(char c) {
  if (c >= '0' && c <= '9') return GLYPHS_DIGIT[c - '0'];
  if (c >= 'A' && c <= 'Z') return GLYPHS_ALPHA[c - 'A'];
  if (c == '-') return GLYPH_DASH;
  if (c == '/') return GLYPH_SLASH;
  return GLYPH_BLANK;
}

static int isqrt_int(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

// Rechnet in halben Pixeln, damit der Bogen um die Pixelmitte symmetrisch bleibt.
// Der Radius betraegt wie in der Vorlage drei Viertel der Kachel.
static int corner_inset(int row, int c) {
  int d = (3 * c) / 2;
  int dy = d - 2 * row - 1;
  if (dy <= 0) return 0;
  return (d - isqrt_int(d * d - dy * dy) + 1) / 2;
}

// Eine Kachel wird zeilenweise als ein oder zwei waagerechte Spannen gefuellt.
static void draw_cell(GContext *ctx, char type, int x, int y, int c) {
  if (type == '.') return;
  if (type == '#') {
    graphics_fill_rect(ctx, GRect(x, y, c, c), 0, GCornerNone);
    return;
  }
  for (int row = 0; row < c; row++) {
    int flip = c - 1 - row;
    int mid = row < flip ? row : flip;
    int a0 = 0, b0 = c, a1 = 0, b1 = 0;
    switch (type) {
      case 'a': a0 = flip; break;
      case 'b': b0 = row + 1; break;
      case 'c': b0 = flip + 1; break;
      case 'd': a0 = row; break;
      case 'q': a0 = corner_inset(row, c); break;
      case 'w': b0 = c - corner_inset(row, c); break;
      case 'e': a0 = corner_inset(flip, c); break;
      case 'r': b0 = c - corner_inset(flip, c); break;
      case '>': b0 = c - mid; break;
      case '<': a0 = mid; break;
      case 'L': a0 = c - mid; break;
      case '^':
        if (row * 2 < c) { b0 = row; a1 = c - row; b1 = c; }
        break;
      case 'v':
        if (flip * 2 < c) { b0 = flip; a1 = c - flip; b1 = c; }
        break;
      case 'V':
        if (row * 2 < c) { a0 = row; b0 = c - row; } else { b0 = 0; }
        break;
      case 'A':
        if (flip * 2 < c) { a0 = flip; b0 = c - flip; } else { b0 = 0; }
        break;
      case 'k': b0 = 2 * row + 1 < c ? 2 * row + 1 : c; break;
      case 'n': a0 = row / 2; break;
      case 'm': b0 = c - row / 2; break;
      case 'N': a0 = (c + row) / 2; break;
      case 'M': b0 = (c - row) / 2; break;
      default: break;
    }
    if (b0 > a0) graphics_fill_rect(ctx, GRect(x + a0, y + row, b0 - a0, 1), 0, GCornerNone);
    if (b1 > a1) graphics_fill_rect(ctx, GRect(x + a1, y + row, b1 - a1, 1), 0, GCornerNone);
  }
}

// Platte und Kacheln eines Zeichens.
static void draw_glyph(GContext *ctx, char ch, int x, int y, int c, const Theme *th) {
  graphics_context_set_fill_color(ctx, th->plate);
  graphics_fill_rect(ctx, GRect(x, y, PLATE_W(c), PLATE_H(c)), 0, GCornerNone);

  const char *cells = glyph_cells(ch);
  graphics_context_set_fill_color(ctx, th->fg);
  for (int r = 0; r < 5; r++) {
    for (int k = 0; k < 3; k++) {
      draw_cell(ctx, cells[r * 3 + k],
                x + GRID + k * (c + GRID), y + GRID + r * (c + GRID), c);
    }
  }
}

// Leerzeichen sind in der dritten Zeile eine schmale Luecke ohne Platte.
static int text_width(const char *s, int c) {
  int w = 0;
  for (int i = 0; s[i]; i++) {
    if (i) w += GAP_INFO_CHAR;
    w += s[i] == ' ' ? INFO_SPACE_W : PLATE_W(c);
  }
  return w;
}

static void draw_text(GContext *ctx, const char *s, int x, int y, int c, const Theme *th) {
  for (int i = 0; s[i]; i++) {
    if (s[i] == ' ') {
      x += INFO_SPACE_W + GAP_INFO_CHAR;
    } else {
      draw_glyph(ctx, s[i], x, y, c, th);
      x += PLATE_W(c) + GAP_INFO_CHAR;
    }
  }
}

// ---------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------
static void settings_defaults(void) {
  s_settings.version = SETTINGS_VERSION;
  s_settings.theme = THEME_DEFAULT;
  s_settings.info_mode = INFO_DATE;
  s_settings.custom_screen = CUSTOM_SCREEN_DEFAULT;
  s_settings.custom_plate = CUSTOM_PLATE_DEFAULT;
  s_settings.custom_fg = CUSTOM_FG_DEFAULT;
}

static void settings_load(void) {
  settings_defaults();
  if (persist_exists(SETTINGS_KEY)) {
    persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
  }
  if (s_settings.theme >= THEME_COUNT) s_settings.theme = THEME_DEFAULT;
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
      snprintf(buf, len, "%d", steps);
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
        snprintf(buf, len, "%d/%dC", DEMO_TEMP_MIN, DEMO_TEMP_MAX);
      } else if (fresh) {
        int lo = s_temp_min;
        int hi = s_temp_max;
        if (lo < -99) lo = -99;
        if (lo > 999) lo = 999;
        if (hi < -99) hi = -99;
        if (hi > 999) hi = 999;
        snprintf(buf, len, "%d/%d%c", lo, hi, unit);
      } else {
        snprintf(buf, len, "--/--%c", unit);
      }
      break;
    }
    case INFO_OFF:
      buf[0] = '\0';
      break;
    case INFO_DATE:
    default:
      snprintf(buf, len, "%s %02d",
               DAYS[s_demo ? DEMO_WDAY : t->tm_wday],
               s_demo ? DEMO_MDAY : t->tm_mday);
      break;
  }
}

// ---------------------------------------------------------------------------
// Zeichnen
// ---------------------------------------------------------------------------
static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  bool custom = s_settings.theme == THEME_CUSTOM;
  Theme custom_theme = { s_settings.custom_fg, s_settings.custom_plate };
  const Theme *th = custom ? &custom_theme : &THEMES[s_settings.theme];

  graphics_context_set_fill_color(ctx, custom ? s_settings.custom_screen : COLOR_SCREEN);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  bool h24 = s_demo || clock_is_24h_style();

  int hour = s_demo ? DEMO_HOUR : t->tm_hour;
  int minute = s_demo ? DEMO_MINUTE : t->tm_min;
  bool pm = hour >= 12;
  if (!h24) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }

  static char hour_buf[4];
  static char min_buf[4];
  snprintf(hour_buf, sizeof(hour_buf), "%02d", hour);
  snprintf(min_buf, sizeof(min_buf), "%02d", minute);

  bool show_info = s_settings.info_mode != INFO_OFF;
  int c = show_info ? CELL_BIG : CELL_FULL;
  int pw = PLATE_W(c);
  int ph = PLATE_H(c);
  int pair_w = 2 * pw + GAP_DIGIT;
  int block_h = 2 * ph + GAP_DIGIT;
  if (show_info) block_h += GAP_INFO + PLATE_H(CELL_INFO);
  int x0 = (b.size.w - pair_w) / 2;
  int top = (b.size.h - block_h) / 2;

  draw_glyph(ctx, hour_buf[0], x0, top, c, th);
  draw_glyph(ctx, hour_buf[1], x0 + pw + GAP_DIGIT, top, c, th);

  int minute_y = top + ph + GAP_DIGIT;
  draw_glyph(ctx, min_buf[0], x0, minute_y, c, th);
  draw_glyph(ctx, min_buf[1], x0 + pw + GAP_DIGIT, minute_y, c, th);

  if (!h24) {
    draw_glyph(ctx, pm ? 'P' : 'A', x0 + pair_w + GAP_AMPM,
               top + (ph - PLATE_H(CELL_AMPM)) / 2, CELL_AMPM, th);
  }

  if (show_info) {
    static char info[24];
    info_text(t, info, sizeof(info));
    int ic = CELL_INFO;
    while (ic > CELL_INFO_MIN && text_width(info, ic) > INFO_MAX_W) ic--;
    int info_y = minute_y + ph + GAP_INFO + (PLATE_H(CELL_INFO) - PLATE_H(ic)) / 2;
    draw_text(ctx, info, (b.size.w - text_width(info, ic)) / 2, info_y, ic, th);
  }
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
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_SCREEN))) {
    s_settings.custom_screen = GColorFromHEX(tuple_int(t));
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_PLATE))) {
    s_settings.custom_plate = GColorFromHEX(tuple_int(t));
    settings_changed = true;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_CUSTOM_FG))) {
    s_settings.custom_fg = GColorFromHEX(tuple_int(t));
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
