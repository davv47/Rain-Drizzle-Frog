#include <pebble.h>

// ── AppMessage keys ───────────────────────────────────────────────────────────
#define KEY_WMO_CODE    0
#define KEY_TEMPERATURE 1
#define KEY_PRECIP_MM10 2
#define KEY_FETCH_TIME  3

// ── Outfit IDs ────────────────────────────────────────────────────────────────
#define OUTFIT_NAKED    0
#define OUTFIT_SWIMSUIT 1
#define OUTFIT_RAINCOAT 2
#define OUTFIT_SCARF    3
#define OUTFIT_SNOWSUIT 4

// ── Water / snow level IDs ────────────────────────────────────────────────────
#define LEVEL_DRY       0
#define LEVEL_ANKLE     1
#define LEVEL_KNEE      2
#define LEVEL_WAIST     3
#define LEVEL_SUBMERGED 4

// ── Sky type IDs ──────────────────────────────────────────────────────────────
#define SKY_CLEAR  0
#define SKY_CLOUDY 1
#define SKY_RAIN   2
#define SKY_SNOW   3
#define SKY_STORM  4
#define SKY_NIGHT  5   // set when hour < 6 or hour >= 21

// ── Frog sprite dimensions on-screen (Gabbro 260x260) ────────────────────────
// Source PNGs are 153x192 (48*3.2 x 60*3.2, nearest-neighbour scaled).
// We blit them at their natural size — 153x192 pixels.
#define FROG_W 153
#define FROG_H 192

// Frog foot Y position (bottom of sprite) per water level.
// Ground starts at y=182. Puddle centre at y=184; frog sinks into it slightly.
static const int FROG_FOOT_Y[5] = {
  182,   // DRY     — feet on grass
  186,   // ANKLE   — slightly in puddle
  190,   // KNEE    — deeper
  194,   // WAIST   — deeper still
  192,   // SUBMERGED — only eyes sprite used; position matches puddle surface
};

// ── Globals ───────────────────────────────────────────────────────────────────
static Window  *s_window;
static Layer   *s_canvas_layer;

static int s_wmo_code    = 0;
static int s_temperature = 20;
static int s_precip_mm10 = 0;
static int s_hour        = 12;   // updated each minute

static char s_time_buf[8];
static char s_date_buf[16];
static char s_precip_buf[16];
static char s_temp_buf[12];

static GFont s_font_time;
static GFont s_font_info;

// ── Cached bitmaps ────────────────────────────────────────────────────────────
// Backgrounds  (6)
static GBitmap *s_bg[6];
// Frog sprites (6: 5 outfits + submerged)
static GBitmap *s_frog[6];
// Ground/puddle (5 levels, rain variant)
static GBitmap *s_ground_puddle[5];
// Ground/snow   (5 levels, snow variant — index 0 = dry, same as puddle[0])
static GBitmap *s_ground_snow[5];

// ── Resource ID tables ────────────────────────────────────────────────────────
static const uint32_t BG_RESOURCES[6] = {
  RESOURCE_ID_IMAGE_BG_CLEAR,
  RESOURCE_ID_IMAGE_BG_CLOUDY,
  RESOURCE_ID_IMAGE_BG_RAIN,
  RESOURCE_ID_IMAGE_BG_SNOW,
  RESOURCE_ID_IMAGE_BG_STORM,
  RESOURCE_ID_IMAGE_BG_NIGHT,
};

static const uint32_t FROG_RESOURCES[6] = {
  RESOURCE_ID_IMAGE_FROG_NAKED,
  RESOURCE_ID_IMAGE_FROG_SWIMSUIT,
  RESOURCE_ID_IMAGE_FROG_RAINCOAT,
  RESOURCE_ID_IMAGE_FROG_SCARF,
  RESOURCE_ID_IMAGE_FROG_SNOWSUIT,
  RESOURCE_ID_IMAGE_FROG_SUBMERGED,
};

static const uint32_t GROUND_PUDDLE_RESOURCES[5] = {
  RESOURCE_ID_IMAGE_GROUND_DRY,
  RESOURCE_ID_IMAGE_GROUND_PUDDLE_ANKLE,
  RESOURCE_ID_IMAGE_GROUND_PUDDLE_KNEE,
  RESOURCE_ID_IMAGE_GROUND_PUDDLE_WAIST,
  RESOURCE_ID_IMAGE_GROUND_PUDDLE_SUBMERGED,
};

static const uint32_t GROUND_SNOW_RESOURCES[5] = {
  RESOURCE_ID_IMAGE_GROUND_DRY,
  RESOURCE_ID_IMAGE_GROUND_SNOW_ANKLE,
  RESOURCE_ID_IMAGE_GROUND_SNOW_KNEE,
  RESOURCE_ID_IMAGE_GROUND_SNOW_WAIST,
  RESOURCE_ID_IMAGE_GROUND_SNOW_SUBMERGED,
};

// ── Logic helpers ─────────────────────────────────────────────────────────────
static int get_sky_type(int wmo, int hour) {
  if (hour < 6 || hour >= 21) return SKY_NIGHT;
  if (wmo >= 95)              return SKY_STORM;
  if ((wmo>=51&&wmo<=67)||(wmo>=80&&wmo<=84)) return SKY_RAIN;
  if (wmo>=71&&wmo<=86)       return SKY_SNOW;
  if (wmo >= 3)               return SKY_CLOUDY;
  return SKY_CLEAR;
}

static int get_outfit(int wmo, int temp_c) {
  if ((wmo>=71&&wmo<=77)||wmo==85||wmo==86)         return OUTFIT_SNOWSUIT;
  if ((wmo>=51&&wmo<=67)||(wmo>=80&&wmo<=84)||(wmo>=95&&wmo<=99)) return OUTFIT_RAINCOAT;
  if (wmo>=3||(wmo>=45&&wmo<=48)||temp_c<10)        return OUTFIT_SCARF;
  if (wmo==0&&temp_c>=25)                           return OUTFIT_SWIMSUIT;
  return OUTFIT_NAKED;
}

static int get_level(int precip_mm10) {
  if (precip_mm10 == 0)  return LEVEL_DRY;
  if (precip_mm10 < 20)  return LEVEL_ANKLE;
  if (precip_mm10 < 100) return LEVEL_KNEE;
  if (precip_mm10 < 250) return LEVEL_WAIST;
  return LEVEL_SUBMERGED;
}

static bool is_snow_mode(int temp_c, int wmo) {
  return temp_c < 0 && ((wmo>=71&&wmo<=77)||wmo==85||wmo==86);
}

// ── Outlined text helper ──────────────────────────────────────────────────────
static void draw_text_outlined(GContext *ctx, const char *text, GFont font,
                                GRect box, GTextAlignment align) {
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, text, font,
    GRect(box.origin.x-1, box.origin.y-1, box.size.w, box.size.h),
    GTextOverflowModeWordWrap, align, NULL);
  graphics_draw_text(ctx, text, font,
    GRect(box.origin.x+1, box.origin.y-1, box.size.w, box.size.h),
    GTextOverflowModeWordWrap, align, NULL);
  graphics_draw_text(ctx, text, font,
    GRect(box.origin.x-1, box.origin.y+1, box.size.w, box.size.h),
    GTextOverflowModeWordWrap, align, NULL);
  graphics_draw_text(ctx, text, font,
    GRect(box.origin.x+1, box.origin.y+1, box.size.w, box.size.h),
    GTextOverflowModeWordWrap, align, NULL);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, box,
    GTextOverflowModeWordWrap, align, NULL);
}

// ── Canvas update ─────────────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int W = bounds.size.w, H = bounds.size.h;

  int sky    = get_sky_type(s_wmo_code, s_hour);
  int outfit = get_outfit(s_wmo_code, s_temperature);
  int level  = get_level(s_precip_mm10);
  bool snow  = is_snow_mode(s_temperature, s_wmo_code);

  // ── 1. Background ──────────────────────────────────────────────────────────
  if (s_bg[sky]) {
    graphics_draw_bitmap_in_rect(ctx, s_bg[sky], GRect(0, 0, W, H));
  }

  // ── 2. Ground / puddle / snow drift ───────────────────────────────────────
  GBitmap **ground_set = snow ? s_ground_snow : s_ground_puddle;
  if (ground_set[level]) {
    graphics_draw_bitmap_in_rect(ctx, ground_set[level], GRect(0, 0, W, H));
  }

  // ── 3. Frog sprite ────────────────────────────────────────────────────────
  int frog_idx = (level == LEVEL_SUBMERGED) ? 5 : outfit;
  if (s_frog[frog_idx]) {
    int foot_y = FROG_FOOT_Y[level];
    int fx = (W - FROG_W) / 2;          // horizontally centred
    int fy = foot_y - FROG_H;           // top of sprite
    graphics_draw_bitmap_in_rect(ctx, s_frog[frog_idx],
                                  GRect(fx, fy, FROG_W, FROG_H));
  }

  // ── 4. HUD ────────────────────────────────────────────────────────────────
  // Time pill at top
  graphics_context_set_fill_color(ctx, GColorFromRGBA(0, 0, 0, 107)); // ~42% black
  graphics_fill_rect(ctx, GRect(W/2-72, 14, 144, 54), 8, GCornersAll);

  draw_text_outlined(ctx, s_time_buf, s_font_time, GRect(0, 16, W, 44),
                     GTextAlignmentCenter);
  draw_text_outlined(ctx, s_date_buf, s_font_info, GRect(0, 54, W, 22),
                     GTextAlignmentCenter);

  // Info pill at bottom
  graphics_context_set_fill_color(ctx, GColorFromRGBA(0, 0, 0, 148)); // ~58% black
  graphics_fill_rect(ctx, GRect(W/2-74, 208, 148, 36), 10, GCornersAll);

  graphics_context_set_text_color(ctx, GColorFromHEX(0x80c8ff)); // light blue
  graphics_draw_text(ctx, s_temp_buf, s_font_info,
    GRect(0, 210, W, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, GColorFromHEX(0x5ab4ff));
  graphics_draw_text(ctx, s_precip_buf, s_font_info,
    GRect(0, 226, W, 18), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ── Buffer updates ────────────────────────────────────────────────────────────
static void update_time(struct tm *t) {
  clock_copy_time_string(s_time_buf, sizeof(s_time_buf));
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", t);
  s_hour = t->tm_hour;
  layer_mark_dirty(s_canvas_layer);
}

static void update_weather_text(void) {
  int mm_whole = s_precip_mm10 / 10;
  int mm_frac  = s_precip_mm10 % 10;
  const char *label;
  switch (get_level(s_precip_mm10)) {
    case LEVEL_ANKLE:     label = "drizzle"; break;
    case LEVEL_KNEE:      label = "rain";    break;
    case LEVEL_WAIST:     label = "heavy";   break;
    case LEVEL_SUBMERGED: label = "flood!";  break;
    default:              label = "dry";     break;
  }
  snprintf(s_precip_buf, sizeof(s_precip_buf), "%d.%dmm %s", mm_whole, mm_frac, label);
  snprintf(s_temp_buf,   sizeof(s_temp_buf),   "%d\xc2\xb0""C", s_temperature); // degree symbol UTF-8
  layer_mark_dirty(s_canvas_layer);
}

// ── Tick / AppMessage ─────────────────────────────────────────────────────────
static void tick_handler(struct tm *t, TimeUnits units_changed) {
  update_time(t);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  t = dict_find(iter, KEY_WMO_CODE);    if (t) s_wmo_code    = (int)t->value->int32;
  t = dict_find(iter, KEY_TEMPERATURE); if (t) s_temperature = (int)t->value->int32;
  t = dict_find(iter, KEY_PRECIP_MM10); if (t) s_precip_mm10 = (int)t->value->int32;
  update_weather_text();
}

// ── Bitmap loading / unloading ────────────────────────────────────────────────
static void load_bitmaps(void) {
  for (int i = 0; i < 6; i++) {
    s_bg[i]   = gbitmap_create_with_resource(BG_RESOURCES[i]);
    s_frog[i] = gbitmap_create_with_resource(FROG_RESOURCES[i]);
  }
  for (int i = 0; i < 5; i++) {
    s_ground_puddle[i] = gbitmap_create_with_resource(GROUND_PUDDLE_RESOURCES[i]);
    s_ground_snow[i]   = gbitmap_create_with_resource(GROUND_SNOW_RESOURCES[i]);
  }
}

static void unload_bitmaps(void) {
  for (int i = 0; i < 6; i++) {
    if (s_bg[i])   { gbitmap_destroy(s_bg[i]);   s_bg[i]   = NULL; }
    if (s_frog[i]) { gbitmap_destroy(s_frog[i]); s_frog[i] = NULL; }
  }
  for (int i = 0; i < 5; i++) {
    if (s_ground_puddle[i]) { gbitmap_destroy(s_ground_puddle[i]); s_ground_puddle[i] = NULL; }
    if (s_ground_snow[i])   { gbitmap_destroy(s_ground_snow[i]);   s_ground_snow[i]   = NULL; }
  }
}

// ── Window ────────────────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_font_time = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  s_font_info = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  load_bitmaps();

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  // Seed buffers
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  update_time(t);
  update_weather_text();
}

static void window_unload(Window *window) {
  unload_bitmaps();
  layer_destroy(s_canvas_layer);
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