#include <pebble.h>

// ── AppMessage keys ───────────────────────────────────────────────────────────
#define KEY_WMO_CODE   0
#define KEY_TEMPERATURE 1
#define KEY_PRECIP_MM10 2
#define KEY_FETCH_TIME  3

// ── Outfit IDs ────────────────────────────────────────────────────────────────
#define OUTFIT_NAKED    0   // mild / cloudy
#define OUTFIT_SWIMSUIT 1   // hot & sunny
#define OUTFIT_RAINCOAT 2   // rain / drizzle / storm
#define OUTFIT_SCARF    3   // cold / windy / overcast
#define OUTFIT_SNOWSUIT 4   // snow

// ── Water levels ──────────────────────────────────────────────────────────────
#define WATER_DRY       0
#define WATER_ANKLE     1
#define WATER_KNEE      2
#define WATER_WAIST     3
#define WATER_SUBMERGED 4

// ── Globals ───────────────────────────────────────────────────────────────────
static Window   *s_window;
static Layer    *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_precip_layer;

static int s_wmo_code    = 0;
static int s_temperature = 20;
static int s_precip_mm10 = 0;   // precipitation * 10 to avoid floats

static char s_time_buf[8];
static char s_date_buf[16];
static char s_precip_buf[20];

// ── Helpers ───────────────────────────────────────────────────────────────────
static int get_outfit(int wmo, int temp_c) {
  // Snow
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) {
    return OUTFIT_SNOWSUIT;
  }
  // Rain / drizzle / storms
  if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 84) || (wmo >= 95 && wmo <= 99)) {
    return OUTFIT_RAINCOAT;
  }
  // Cold / fog / overcast
  if (wmo >= 3 || (wmo >= 45 && wmo <= 48) || temp_c < 10) {
    return OUTFIT_SCARF;
  }
  // Hot & sunny
  if (wmo == 0 && temp_c >= 25) {
    return OUTFIT_SWIMSUIT;
  }
  // Default: mild / partly cloudy
  return OUTFIT_NAKED;
}

static int get_water_level(int precip_mm10) {
  // precip_mm10 is precipitation_mm * 10
  if (precip_mm10 == 0)          return WATER_DRY;
  if (precip_mm10 < 20)          return WATER_ANKLE;   // < 2 mm
  if (precip_mm10 < 100)         return WATER_KNEE;    // 2–10 mm
  if (precip_mm10 < 250)         return WATER_WAIST;   // 10–25 mm
  return WATER_SUBMERGED;                               // 25+ mm
}

// ── Palette helpers ───────────────────────────────────────────────────────────
#ifdef PBL_COLOR
  #define COL_SKY_CLEAR    GColorVividCerulean
  #define COL_SKY_CLOUDY   GColorLightGray
  #define COL_SKY_RAIN     GColorCobaltBlue
  #define COL_SKY_SNOW     GColorWhite
  #define COL_SKY_STORM    GColorImperialPurple
  #define COL_GROUND       GColorIslamicGreen
  #define COL_WATER_1      GColorPictonBlue
  #define COL_WATER_2      GColorCyan
  #define COL_FROG_BODY    GColorIslamicGreen
  #define COL_FROG_BELLY   GColorMintGreen
  #define COL_FROG_EYE_W   GColorWhite
  #define COL_FROG_EYE_P   GColorBlack
  #define COL_FROG_MOUTH   GColorDarkCandyAppleRed
  #define COL_RAINCOAT     GColorYellow
  #define COL_RAINCOAT_BTN GColorOrange
  #define COL_BOOT         GColorOrange
  #define COL_SCARF        GColorRed
  #define COL_UMBRELLA     GColorBlue
  #define COL_SNOWSUIT     GColorIcterine
  #define COL_SWIMSUIT     GColorMagenta
  #define COL_SUN          GColorYellow
  #define COL_CLOUD        GColorWhite
  #define COL_RAINDROP     GColorPictonBlue
  #define COL_SNOWFLAKE    GColorWhite
  #define COL_TEXT         GColorBlack
  #define COL_TEXT_BG      GColorWhite
#else
  #define COL_SKY_CLEAR    GColorLightGray
  #define COL_SKY_CLOUDY   GColorLightGray
  #define COL_SKY_RAIN     GColorDarkGray
  #define COL_SKY_SNOW     GColorWhite
  #define COL_SKY_STORM    GColorBlack
  #define COL_GROUND       GColorDarkGray
  #define COL_WATER_1      GColorLightGray
  #define COL_WATER_2      GColorWhite
  #define COL_FROG_BODY    GColorDarkGray
  #define COL_FROG_BELLY   GColorLightGray
  #define COL_FROG_EYE_W   GColorWhite
  #define COL_FROG_EYE_P   GColorBlack
  #define COL_FROG_MOUTH   GColorBlack
  #define COL_RAINCOAT     GColorLightGray
  #define COL_RAINCOAT_BTN GColorBlack
  #define COL_BOOT         GColorBlack
  #define COL_SCARF        GColorLightGray
  #define COL_UMBRELLA     GColorDarkGray
  #define COL_SNOWSUIT     GColorWhite
  #define COL_SWIMSUIT     GColorDarkGray
  #define COL_SUN          GColorWhite
  #define COL_CLOUD        GColorWhite
  #define COL_RAINDROP     GColorLightGray
  #define COL_SNOWFLAKE    GColorWhite
  #define COL_TEXT         GColorBlack
  #define COL_TEXT_BG      GColorWhite
#endif

// ── Drawing helpers ───────────────────────────────────────────────────────────
static void fill_rect_c(GContext *ctx, GColor col, int x, int y, int w, int h) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
}

static void draw_line_c(GContext *ctx, GColor col, int x0, int y0, int x1, int y1) {
  graphics_context_set_stroke_color(ctx, col);
  graphics_draw_line(ctx, GPoint(x0, y0), GPoint(x1, y1));
}

// ── Background scene ──────────────────────────────────────────────────────────
static void draw_background(GContext *ctx, GRect bounds, int wmo, int temp_c) {
  // Sky colour
  GColor sky;
  if (wmo >= 95) {
    sky = COL_SKY_STORM;
  } else if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 84)) {
    sky = COL_SKY_RAIN;
  } else if ((wmo >= 71 && wmo <= 86)) {
    sky = COL_SKY_SNOW;
  } else if (wmo >= 3) {
    sky = COL_SKY_CLOUDY;
  } else {
    sky = COL_SKY_CLEAR;
  }
  fill_rect_c(ctx, sky, 0, 0, bounds.size.w, bounds.size.h);

  // Sun (clear / hot)
  if (wmo <= 1 && temp_c >= 15) {
    fill_rect_c(ctx, COL_SUN, bounds.size.w - 30, 8, 20, 20);
    // Rays
    graphics_context_set_stroke_color(ctx, COL_SUN);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 20, 4, bounds.size.w - 20, 6);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 20, 30, bounds.size.w - 20, 32);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 34, 18, bounds.size.w - 32, 18);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 8, 18, bounds.size.w - 6, 18);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 31, 8, bounds.size.w - 29, 10);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 11, 8, bounds.size.w - 9, 10);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 31, 28, bounds.size.w - 29, 26);
    draw_line_c(ctx, COL_SUN, bounds.size.w - 11, 28, bounds.size.w - 9, 26);
  }

  // Cloud (cloudy / overcast)
  if (wmo >= 2 && wmo <= 3) {
    fill_rect_c(ctx, COL_CLOUD, 8,  10, 30, 12);
    fill_rect_c(ctx, COL_CLOUD, 14, 6,  18, 8);
    fill_rect_c(ctx, COL_CLOUD, 40, 18, 26, 10);
    fill_rect_c(ctx, COL_CLOUD, 44, 14, 16, 6);
  }

  // Rain drops
  if ((wmo >= 51 && wmo <= 67) || (wmo >= 80 && wmo <= 84) || (wmo >= 95 && wmo <= 99)) {
    graphics_context_set_stroke_color(ctx, COL_RAINDROP);
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i < 10; i++) {
      int rx = 8 + i * 13;
      int ry = 12 + (i % 3) * 10;
      draw_line_c(ctx, COL_RAINDROP, rx, ry, rx - 2, ry + 6);
    }
  }

  // Snow flakes
  if ((wmo >= 71 && wmo <= 77) || wmo == 85 || wmo == 86) {
    for (int i = 0; i < 8; i++) {
      int sx = 10 + i * 16;
      int sy = 10 + (i % 3) * 12;
      draw_line_c(ctx, COL_SNOWFLAKE, sx - 3, sy, sx + 3, sy);
      draw_line_c(ctx, COL_SNOWFLAKE, sx, sy - 3, sx, sy + 3);
    }
  }

  // Lightning bolt (thunderstorm)
  if (wmo >= 95 && wmo <= 99) {
    graphics_context_set_stroke_color(ctx, COL_SUN);
    draw_line_c(ctx, COL_SUN, 20, 20, 14, 34);
    draw_line_c(ctx, COL_SUN, 14, 34, 20, 34);
    draw_line_c(ctx, COL_SUN, 20, 34, 14, 48);
  }
}

// ── Ground + water ────────────────────────────────────────────────────────────
static void draw_ground_water(GContext *ctx, GRect bounds, int water_level) {
  int ground_y = bounds.size.h - 38;

  // Grass strip
  fill_rect_c(ctx, COL_GROUND, 0, ground_y, bounds.size.w, bounds.size.h - ground_y);

  if (water_level == WATER_DRY) return;

  // Water surface heights (from ground_y upward, relative to frog bottom)
  // Frog is drawn with feet at ground_y; body ~28px tall
  int water_tops[] = { 0, ground_y + 4, ground_y - 8, ground_y - 18, ground_y - 26 };
  int wy = water_tops[water_level];

  // Animate with two-tone stripes
  for (int y = wy; y < bounds.size.h; y++) {
    GColor wc = ((y - wy) % 4 < 2) ? COL_WATER_1 : COL_WATER_2;
    draw_line_c(ctx, wc, 0, y, bounds.size.w, y);
  }
  // Ripple line at surface
  draw_line_c(ctx, GColorWhite, 0, wy, bounds.size.w, wy);
  draw_line_c(ctx, GColorWhite, 4, wy + 2, 18, wy + 2);
  draw_line_c(ctx, GColorWhite, bounds.size.w - 20, wy + 2, bounds.size.w - 6, wy + 2);
}

// ── Frog sprite ───────────────────────────────────────────────────────────────
// Frog centre-x, feet at frog_y. ~22px wide, ~28px tall.
static void draw_frog(GContext *ctx, int cx, int frog_y, int outfit, int water_level) {
  int by = frog_y - 28;  // body top

  // ── Legs (back and front) ────────────────────────────────────────────────
  // Back legs wide, front legs tucked
  fill_rect_c(ctx, COL_FROG_BODY, cx - 14, frog_y - 8, 10, 8);  // left back leg
  fill_rect_c(ctx, COL_FROG_BODY, cx + 4,  frog_y - 8, 10, 8);  // right back leg
  fill_rect_c(ctx, COL_FROG_BODY, cx - 10, frog_y - 4, 6,  4);  // left front leg
  fill_rect_c(ctx, COL_FROG_BODY, cx + 4,  frog_y - 4, 6,  4);  // right front leg

  // Webbed feet (3 small toes)
  for (int t = 0; t < 3; t++) {
    fill_rect_c(ctx, COL_FROG_BODY, cx - 16 + t * 3, frog_y,     2, 3);
    fill_rect_c(ctx, COL_FROG_BODY, cx + 4  + t * 3, frog_y,     2, 3);
  }

  // ── Body ─────────────────────────────────────────────────────────────────
  fill_rect_c(ctx, COL_FROG_BODY,  cx - 11, by,     22, 20);
  fill_rect_c(ctx, COL_FROG_BELLY, cx - 7,  by + 6, 14, 12);

  // ── Head ─────────────────────────────────────────────────────────────────
  int hy = by - 14;
  fill_rect_c(ctx, COL_FROG_BODY, cx - 11, hy,     22, 16);

  // Eyes (bulging, 2px above head top)
  fill_rect_c(ctx, COL_FROG_BODY,  cx - 13, hy - 2, 8, 8);
  fill_rect_c(ctx, COL_FROG_BODY,  cx + 5,  hy - 2, 8, 8);
  fill_rect_c(ctx, COL_FROG_EYE_W, cx - 12, hy - 1, 6, 6);
  fill_rect_c(ctx, COL_FROG_EYE_W, cx + 6,  hy - 1, 6, 6);
  fill_rect_c(ctx, COL_FROG_EYE_P, cx - 11, hy,     3, 3);
  fill_rect_c(ctx, COL_FROG_EYE_P, cx + 8,  hy,     3, 3);

  // Nostril dots
  fill_rect_c(ctx, GColorBlack, cx - 2, hy + 8, 2, 2);
  fill_rect_c(ctx, GColorBlack, cx + 2, hy + 8, 2, 2);

  // Mouth (smile baseline)
  draw_line_c(ctx, COL_FROG_MOUTH, cx - 6, hy + 12, cx - 2, hy + 14);
  draw_line_c(ctx, COL_FROG_MOUTH, cx - 2, hy + 14, cx + 2, hy + 14);
  draw_line_c(ctx, COL_FROG_MOUTH, cx + 2, hy + 14, cx + 6, hy + 12);

  // ── Outfit layer ─────────────────────────────────────────────────────────
  switch (outfit) {

    case OUTFIT_SWIMSUIT: {
      // Bright swimsuit top + trunks
      fill_rect_c(ctx, COL_SWIMSUIT, cx - 8,  by + 2, 16, 8);  // top
      fill_rect_c(ctx, COL_SWIMSUIT, cx - 8,  by + 12, 16, 6); // trunks
      // Goggles on head
      fill_rect_c(ctx, COL_UMBRELLA, cx - 9, hy + 1, 6, 4);
      fill_rect_c(ctx, COL_UMBRELLA, cx + 3, hy + 1, 6, 4);
      draw_line_c(ctx, GColorBlack, cx - 3, hy + 3, cx + 3, hy + 3);
      break;
    }

    case OUTFIT_RAINCOAT: {
      // Yellow raincoat body
      fill_rect_c(ctx, COL_RAINCOAT, cx - 12, by - 2, 24, 24);
      // Collar
      fill_rect_c(ctx, COL_RAINCOAT, cx - 6,  by - 4, 12, 4);
      // Buttons
      fill_rect_c(ctx, COL_RAINCOAT_BTN, cx - 1, by + 4,  2, 2);
      fill_rect_c(ctx, COL_RAINCOAT_BTN, cx - 1, by + 10, 2, 2);
      // Boots
      fill_rect_c(ctx, COL_BOOT, cx - 16, frog_y - 8, 10, 12);
      fill_rect_c(ctx, COL_BOOT, cx + 6,  frog_y - 8, 10, 12);
      // Hood
      fill_rect_c(ctx, COL_RAINCOAT, cx - 11, hy - 4, 22, 8);
      break;
    }

    case OUTFIT_SCARF: {
      // Scarf wrapped around neck
      fill_rect_c(ctx, COL_SCARF, cx - 12, by - 2, 24, 5);
      fill_rect_c(ctx, COL_SCARF, cx - 12, by + 3, 8, 14); // scarf tail
      // Stripes on scarf
      draw_line_c(ctx, GColorWhite, cx - 12, by,     cx + 12, by);
      draw_line_c(ctx, GColorWhite, cx - 12, by + 3, cx - 4,  by + 3);
      // Umbrella handle (right arm raised)
      draw_line_c(ctx, COL_UMBRELLA, cx + 12, by - 2,  cx + 20, hy - 10);
      // Umbrella canopy
      fill_rect_c(ctx, COL_UMBRELLA, cx + 8, hy - 14, 20, 4);
      fill_rect_c(ctx, COL_UMBRELLA, cx + 6, hy - 18, 24, 6);
      draw_line_c(ctx, GColorWhite, cx + 6, hy - 18, cx + 30, hy - 18);
      // Hat (winter beanie)
      fill_rect_c(ctx, COL_SCARF, cx - 10, hy - 6, 20, 6);
      fill_rect_c(ctx, COL_SCARF, cx - 6,  hy - 10, 12, 4);
      fill_rect_c(ctx, GColorWhite, cx - 3, hy - 12, 6, 3); // pom pom
      break;
    }

    case OUTFIT_SNOWSUIT: {
      // Puffy snowsuit — white with light outline
      fill_rect_c(ctx, COL_SNOWSUIT, cx - 13, by - 2, 26, 26);
      // Suit texture lines (quilted look)
      draw_line_c(ctx, GColorLightGray, cx - 13, by + 8, cx + 13, by + 8);
      draw_line_c(ctx, GColorLightGray, cx - 13, by + 16, cx + 13, by + 16);
      draw_line_c(ctx, GColorLightGray, cx, by - 2, cx, by + 24);
      // Suit legs
      fill_rect_c(ctx, COL_SNOWSUIT, cx - 16, frog_y - 8, 11, 12);
      fill_rect_c(ctx, COL_SNOWSUIT, cx + 5,  frog_y - 8, 11, 12);
      // Mittens
      fill_rect_c(ctx, COL_SNOWSUIT, cx - 18, by + 12, 8, 6);
      fill_rect_c(ctx, COL_SNOWSUIT, cx + 10, by + 12, 8, 6);
      // Hood
      fill_rect_c(ctx, COL_SNOWSUIT, cx - 12, hy - 6, 24, 10);
      fill_rect_c(ctx, GColorPink, cx - 8, hy - 2, 16, 8); // face opening
      // Snowsuit boots
      fill_rect_c(ctx, GColorDarkGray, cx - 16, frog_y + 2, 11, 4);
      fill_rect_c(ctx, GColorDarkGray, cx + 5,  frog_y + 2, 11, 4);
      break;
    }

    default: // OUTFIT_NAKED — no extra layers
      break;
  }

  // If submerged, draw water covering lower half (over frog)
  if (water_level == WATER_SUBMERGED) {
    // Only top of head + eyes visible
    // The water layer is drawn after, but we add bubbles
    fill_rect_c(ctx, COL_WATER_1, cx - 2, hy + 6, 2, 2); // bubble
    fill_rect_c(ctx, COL_WATER_1, cx + 3, hy + 4, 2, 2);
  }
}

// ── Canvas draw ───────────────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int outfit      = get_outfit(s_wmo_code, s_temperature);
  int water_level = get_water_level(s_precip_mm10);

  // 1. Background scene
  draw_background(ctx, bounds, s_wmo_code, s_temperature);

  // 2. Ground + water
  draw_ground_water(ctx, bounds, water_level);

  // 3. Frog — centred horizontally, feet on ground
  int ground_y = bounds.size.h - 38;
  int cx = bounds.size.w / 2;
  draw_frog(ctx, cx, ground_y, outfit, water_level);
}

// ── Time / date / precip updates ──────────────────────────────────────────────
static void update_time(struct tm *tick_time) {
  clock_copy_time_string(s_time_buf, sizeof(s_time_buf));
  text_layer_set_text(s_time_layer, s_time_buf);

  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", tick_time);
  text_layer_set_text(s_date_layer, s_date_buf);
}

static void update_precip_text(void) {
  int mm_whole = s_precip_mm10 / 10;
  int mm_frac  = s_precip_mm10 % 10;
  // Show rain level label
  const char *label;
  int level = get_water_level(s_precip_mm10);
  switch (level) {
    case WATER_ANKLE:     label = "drizzle"; break;
    case WATER_KNEE:      label = "rain";    break;
    case WATER_WAIST:     label = "heavy";   break;
    case WATER_SUBMERGED: label = "flood!";  break;
    default:              label = "dry";     break;
  }
  snprintf(s_precip_buf, sizeof(s_precip_buf), "%d.%dmm %s",
           mm_whole, mm_frac, label);
  text_layer_set_text(s_precip_layer, s_precip_buf);
}

// ── Tick handler ─────────────────────────────────────────────────────────────
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
}

// ── AppMessage ────────────────────────────────────────────────────────────────
static void inbox_received_handler(DictionaryIterator *iter, void *ctx) {
  Tuple *t;

  t = dict_find(iter, KEY_WMO_CODE);
  if (t) s_wmo_code = (int)t->value->int32;

  t = dict_find(iter, KEY_TEMPERATURE);
  if (t) s_temperature = (int)t->value->int32;

  t = dict_find(iter, KEY_PRECIP_MM10);
  if (t) s_precip_mm10 = (int)t->value->int32;

  layer_mark_dirty(s_canvas_layer);
  update_precip_text();
}

// ── Window setup ─────────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  // Canvas fills the screen
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  // Time — large, top centre
  s_time_layer = text_layer_create(GRect(0, 2, bounds.size.w, 40));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, COL_TEXT_BG);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_LECO_32_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  // Date — below time
  s_date_layer = text_layer_create(GRect(0, 42, bounds.size.w, 22));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, COL_TEXT_BG);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  // Precipitation — bottom strip
  s_precip_layer = text_layer_create(GRect(0, bounds.size.h - 18, bounds.size.w, 18));
  text_layer_set_background_color(s_precip_layer, GColorBlack);
  text_layer_set_text_color(s_precip_layer, GColorWhite);
  text_layer_set_font(s_precip_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_precip_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_precip_layer));

  // Seed time
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t);
  update_precip_text();
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_precip_layer);
}

// ── Init / deinit ─────────────────────────────────────────────────────────────
static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(256, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
