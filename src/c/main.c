// Copyright (c) 2026 Luigi Picaro <luigi dot picaro at gmail dot com>
// The above copyright notice shall be included in all copies or substantial portions of the program.

// Copyright (c) 2013 Douwe Maan <http://www.douwemaan.com/>
// The above copyright notice shall be included in all copies or substantial portions of the program.

// Envisioned as a watchface by Jean-Noël Mattern
// Based on the display of the Freebox Revolution, which was designed by Philippe Starck.

#include <pebble.h>
#include <pebble_fonts.h>
#include <locale.h>

#define PK_SETTINGS 0
#define TIME_SLOT_ANIMATION_DURATION  500
#define DEBUGLOG                      0
#define ICONS_HEIGHT  20
#define HOURS_SIZE  78
#define MINUTES_SIZE   98
#define MARGIN              1
#define BATTERY_DAYS  30
#define NUMBER_OF_TIME_IMAGES 10
#define EMPTY_SLOT -1
#define NUMBER_OF_TIME_SLOTS 4

static const int TIME_RESOURCE_IDS[NUMBER_OF_TIME_IMAGES] = {
  RESOURCE_ID_TIME_0,
  RESOURCE_ID_TIME_1, RESOURCE_ID_TIME_2, RESOURCE_ID_TIME_3,
  RESOURCE_ID_TIME_4, RESOURCE_ID_TIME_5, RESOURCE_ID_TIME_6,
  RESOURCE_ID_TIME_7, RESOURCE_ID_TIME_8, RESOURCE_ID_TIME_9
};

// Global variables
static Window *window;
typedef struct {
  BitmapLayer *icon;
  TextLayer *text;
  char buffer[11];
  int curr_val;
} IconTextLayer;
typedef struct {
  BitmapLayer *bt;
  IconTextLayer *battery;
  IconTextLayer *steps;
  IconTextLayer *heart;
  IconTextLayer *meridiem;
  IconTextLayer *wday;
  IconTextLayer *day;
  IconTextLayer *month;
  IconTextLayer *year;
} ModelType;
static ModelType data_model;

// Storage statico per gli IconTextLayer: niente malloc/free a runtime (meno frammentazione dell'heap).
// I puntatori in data_model restano NULL quando l'elemento non e' caricato, come prima.
enum { ITEM_BATTERY, ITEM_MERIDIEM, ITEM_WDAY, ITEM_DAY, ITEM_MONTH, ITEM_YEAR, ITEM_STEPS, ITEM_HEART, ITEM_COUNT };
static IconTextLayer s_items[ITEM_COUNT];

static Layer *header_layer, *time_layer, *footer_layer;
typedef struct Slot {
  GBitmap     *image;
  Layer       *image_layer;
  int8_t      number;
  int8_t      state;
  GColor      digit_color;
} Slot;
typedef struct TimeSlot {
  Slot              slot;
  PropertyAnimation *slide_out_animation;
  PropertyAnimation *slide_in_animation;
  int8_t            new_state;
  uint8_t           slot_size;
  bool              updating;
} TimeSlot;
static TimeSlot time_slots[NUMBER_OF_TIME_SLOTS];

// NON modificare layout/tipi: la struct viene salvata con persist_write_data
typedef struct {
  bool hourvibe;
  bool hColors;
  bool tColors;
  bool fColors;
  bool steps;
  int heart_interval;
} Persist;
static Persist settings = {
  .hourvibe = true,
  .hColors = true,
  .tColors = false,
  .fColors = true,
  .steps = true,
  .heart_interval = 600
};

static GFont font_12, font_16, font_18, font_20;

static TextLayer* create_text_layer(Layer *root, GPoint point, const char *text, const GFont font, const GColor color){
  GSize dimensione_reale = graphics_text_layout_get_content_size(
    text, 
    font, 
    GRect(0, 0, 58, 22), // max rect
    GTextOverflowModeWordWrap,
    GTextAlignmentCenter
  );
	GRect rect_finale = GRect(point.x, point.y, dimensione_reale.w, dimensione_reale.h);

	TextLayer* textLayer = text_layer_create(rect_finale);
  text_layer_set_font(textLayer, font);
  text_layer_set_text_color(textLayer, color);
  text_layer_set_background_color(textLayer, GColorClear);
  text_layer_set_text_alignment(textLayer, GTextAlignmentCenter);

  layer_add_child(root, text_layer_get_layer(textLayer));

  return textLayer;
}
static IconTextLayer* create_icon_text_layer(int idx, Layer *root, GPoint point, const char *text, const GFont font, const GColor color){
  IconTextLayer *custom_layer = &s_items[idx];
  memset(custom_layer, 0, sizeof(*custom_layer));

  custom_layer->text = create_text_layer(root, point, text, font, color);
	snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%s", text);
	text_layer_set_text(custom_layer->text, custom_layer->buffer);

	return custom_layer;
}
static void update_digit_layer(IconTextLayer *custom_layer, int val, const GColor color){
  snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%02d", val);
  text_layer_set_text_color(custom_layer->text, color);
	custom_layer->curr_val = val;
}
static void update_icon_text_layer(IconTextLayer *custom_layer, int val, const char *suffix) {
  if (custom_layer == NULL || custom_layer->text == NULL || val == custom_layer->curr_val) {
    return;
  }
  if (suffix != NULL) {
		snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%s", suffix);
  } else if (val > 0){
    snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%u", val);
  } else {
    snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "ND");
  }
  custom_layer->curr_val = val;
}
static BitmapLayer* load_image(const int res, GPoint point, const GColor color){
  GBitmap *image = gbitmap_create_with_resource(res);
  if (image == NULL) return NULL;   // RAM esaurita: l'icona manca ma l'app non crasha

  GRect bounds = gbitmap_get_bounds(image);
  GRect rect = GRect(point.x, point.y, bounds.size.w, bounds.size.h);
  BitmapLayer *image_layer = bitmap_layer_create(rect);
  if (image_layer == NULL) {
    gbitmap_destroy(image);
    return NULL;
  }
  bitmap_layer_set_bitmap(image_layer, image);
  GColor *palette = gbitmap_get_palette(image);
  if (palette != NULL) {
    palette[1] = color;
  }
  bitmap_layer_set_background_color(image_layer, GColorClear);
  bitmap_layer_set_compositing_mode(image_layer, GCompOpSet);

  return image_layer;
}
static void attach_icon(Layer *parent, BitmapLayer *icon) {
  if (icon != NULL) {
    layer_add_child(parent, bitmap_layer_get_layer(icon));
  }
}
static void update_icon_color(BitmapLayer *image_layer, GColor color) {
  if (image_layer == NULL) {
    return;
  }

  GBitmap *bmp = (GBitmap *)bitmap_layer_get_bitmap(image_layer);

  if (bmp != NULL) {
    GColor *palette = gbitmap_get_palette(bmp);

    if (palette != NULL) {
      palette[1] = color;
      layer_mark_dirty(bitmap_layer_get_layer(image_layer));
    }
  }
}
static void unload_image(BitmapLayer **image_ptr) {
  if (image_ptr == NULL || *image_ptr == NULL) return;

  BitmapLayer *image = *image_ptr;

  GBitmap *bmp = (GBitmap *)bitmap_layer_get_bitmap(image);
  if (bmp != NULL) gbitmap_destroy(bmp);

  Layer *base_layer = bitmap_layer_get_layer(image);
  if (base_layer != NULL) layer_remove_from_parent(base_layer);

  bitmap_layer_destroy(image);
  *image_ptr = NULL;
}
static void unload_text(TextLayer **text_ptr) {
  if (text_ptr == NULL || *text_ptr == NULL) return;

  TextLayer *text = *text_ptr;
  Layer *base_layer = text_layer_get_layer(text);
  if (base_layer != NULL) layer_remove_from_parent(base_layer);

  text_layer_destroy(text);
  *text_ptr = NULL;
}
static void unload_item(IconTextLayer **layer_ptr) {
  if (layer_ptr == NULL || *layer_ptr == NULL) return;
  IconTextLayer *layer = *layer_ptr;

  if (layer->icon != NULL) unload_image(&(layer->icon));
  if (layer->text != NULL) unload_text(&(layer->text));

  memset(layer, 0, sizeof(*layer));   // storage statico: si azzera, non si fa free
  *layer_ptr = NULL;
}
static GColor calculate_color(bool enable, GColor color){
  #if defined(PBL_COLOR)
    if (enable) return color;
  #endif
  return GColorWhite;
}
static void update_item_color(IconTextLayer *iconText, const GColor color){
  if (iconText != NULL) text_layer_set_text_color(iconText->text, color);
  if (iconText != NULL) update_icon_color(iconText->icon, color);
}
static GColor rnd_color(){
  static const uint8_t INDICI_BRILLANTI[] = {
    12, 13, 14, 15, 18, 19, 23, 25, 26, 27, 28, 29, 30, 31,
    33, 34, 35, 36, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 54, 55, 56, 58, 60, 63
  };
  int numero_elementi = sizeof(INDICI_BRILLANTI) / sizeof(INDICI_BRILLANTI[0]);
  int scelta_casuale = rand() % numero_elementi;
  uint8_t rnd_index = INDICI_BRILLANTI[scelta_casuale];
  uint8_t pbl_byte = 0b11000000 | rnd_index;

  return (GColor){ .argb = pbl_byte };
}
static bool us_locale(){
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Locale: %s", i18n_get_system_locale()); }
	bool us_date = strncmp(i18n_get_system_locale(), "en", 2) == 0;
	return us_date;
}

// BT
static void update_bluetooth(bool connected){
  if (data_model.bt == NULL) return;   // icona non caricata (RAM esaurita)
  if (settings.hColors){
    layer_set_hidden(bitmap_layer_get_layer(data_model.bt), false);
    if (connected) update_icon_color(data_model.bt, GColorBlueMoon);
    else update_icon_color(data_model.bt, GColorLightGray);
  } else {
		update_icon_color(data_model.bt, GColorWhite);
		layer_set_hidden(bitmap_layer_get_layer(data_model.bt), !connected);
	}
}
static void handle_bluetooth(bool connected) {
  vibes_short_pulse();

  update_bluetooth(connected);
}

// Health
#if defined(PBL_HEALTH)
  static void aggiorna_passi() {
    if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health Steps %d", (int)settings.steps); }
    if (settings.steps == true){
      if (data_model.steps == NULL){
				GPoint iconPoint = GPoint(15, 2);
				GPoint labelPoint = GPoint(33, 6);
				#if defined(PBL_PLATFORM_GABBRO)
					iconPoint = GPoint(92, 25);
					labelPoint = GPoint(111, 29);
				#endif	
        data_model.steps = create_icon_text_layer(ITEM_STEPS, header_layer, labelPoint, "20.3km", font_12, calculate_color(settings.hColors, GColorYellow));
        text_layer_set_text_alignment(data_model.steps->text, GTextAlignmentLeft);
        data_model.steps->icon = load_image(RESOURCE_ID_STEPS, iconPoint, calculate_color(settings.hColors, GColorYellow));
        attach_icon(header_layer, data_model.steps->icon);
      }

      int meters = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
      if (meters != data_model.steps->curr_val){
        MeasurementSystem sys_metric = health_service_get_measurement_system_for_display(HealthMetricWalkedDistanceMeters);
				char buffer[8];
        if (sys_metric == MeasurementSystemMetric){
          const int KM = 1000;
          int intKm = meters / KM;
          int decKm = (meters % KM) / 100;
          snprintf(buffer, sizeof(buffer), "%u,%ukm", intKm, decKm);
        } else {
          const int STATUTE_MILE = 1609;
          int intMiles = meters / STATUTE_MILE;
          int decMiles = ((meters % STATUTE_MILE) * 10) / STATUTE_MILE;
          snprintf(buffer, sizeof(buffer), "%u.%umi", intMiles, decMiles);
        }
				update_icon_text_layer(data_model.steps, meters, buffer);
      }
    } else if (data_model.steps != NULL){
      unload_item(&data_model.steps);
    }
  }
  #if PBL_API_EXISTS(health_service_peek_current_value)
    static void aggiorna_cardio() {
      if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health HR %u", settings.heart_interval); }
      if (settings.heart_interval >= 0){
        if (data_model.heart == NULL){
					data_model.heart = create_icon_text_layer(ITEM_HEART, header_layer, GPoint(117, 6), "238", font_12, calculate_color(settings.hColors, GColorRed));
          text_layer_set_text_alignment(data_model.heart->text, GTextAlignmentLeft);
          data_model.heart->icon = load_image(RESOURCE_ID_HEART, GPoint(101, 2), calculate_color(settings.hColors, GColorRed));
          attach_icon(header_layer, data_model.heart->icon);
          HealthServiceAccessibilityMask hr_raw_access = health_service_metric_accessible(HealthMetricHeartRateRawBPM, time(NULL), time(NULL));
          if (hr_raw_access & HealthServiceAccessibilityMaskAvailable){
            int heartBeatInit = (int)health_service_peek_current_value(HealthMetricHeartRateRawBPM);
            if (heartBeatInit == 0) {
              heartBeatInit = 70 + (rand() % 7); // Genera un finto Heart Beat per i test
            }
            if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Heart init: %uHRM", heartBeatInit); }
            //update_icon_text_layer(data_model.heart, heartBeatInit, NULL);
          }
        }
        HealthServiceAccessibilityMask hr_access = health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
        if (hr_access & HealthServiceAccessibilityMaskAvailable){
          int heartBeat = (int)health_service_peek_current_value(HealthMetricHeartRateBPM); // HealthMetricHeartRateRawBPM for micro changes
          if (heartBeat != 0 && heartBeat != data_model.heart->curr_val){
            if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Heart update: %uHRM", heartBeat); }
            update_icon_text_layer(data_model.heart, heartBeat, NULL);
          }
        }
      } else if (data_model.heart != NULL){
        unload_item(&data_model.heart);
      }
    }
  #endif
  static void health_handler(HealthEventType event, void *context) {
    if (event == HealthEventMovementUpdate) {
      aggiorna_passi();
    } else if (event == HealthEventHeartRateUpdate) {
      time_t now = time(NULL);
      static time_t last_update = 0;

      #if PBL_API_EXISTS(health_service_peek_current_value)
        if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "HealthEventHeartRateUpdate Since %d", (int)now - last_update); }
        if (now - last_update >= settings.heart_interval) { // settings.heart_interval >= 0
          last_update = now;
          aggiorna_cardio();
        }
      #endif
    }
  }
  static void update_health(){
		if (settings.steps || settings.heart_interval >= 0){
      if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health start"); }

      if (settings.steps) update_item_color(data_model.steps, calculate_color(settings.hColors, GColorYellow));
      aggiorna_passi();
      #if PBL_API_EXISTS(health_service_peek_current_value)
        if (settings.heart_interval >= 0 && data_model.heart != NULL) update_item_color(data_model.heart, calculate_color(settings.hColors, GColorRed));
        aggiorna_cardio();
        if (settings.heart_interval > 0) {
					health_service_set_heart_rate_sample_period(settings.heart_interval);
	        int hr_sample_period_exp = health_service_get_heart_rate_sample_period_expiration_sec();
        	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "HR hr_sample_period_exp %d", hr_sample_period_exp); }
				}
      #endif
      health_service_events_subscribe(health_handler, NULL);
    } else {
      if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health stop"); }
      health_service_events_unsubscribe();

      unload_item(&data_model.steps);
      unload_item(&data_model.heart);
    }
  }
#endif

// Battery
static void handle_battery(BatteryChargeState charge_state) {
  int totalDays = BATTERY_DAYS;
  #if defined(PBL_PLATFORM_EMERY)
    totalDays = 30;
	#elif defined(PBL_PLATFORM_GABBRO)
		totalDays = 15;
	#else 
		totalDays = 7;
  #endif
  int percent = charge_state.charge_percent;
	char buffer[4];
	bool us_locale = strncmp(i18n_get_system_locale(), "en", 2) == 0;
  if (percent > 0 && percent <=10){
    int hoursLeft = (int)totalDays * 24 * percent / 100; 
		if (us_locale) {
			snprintf(buffer, sizeof(buffer), "%dh", hoursLeft);
		} else {
			snprintf(buffer, sizeof(buffer), "%do", hoursLeft);
		}
  } else {
    int daysLeft = (int)totalDays*percent/100;
		if (us_locale) {
			snprintf(buffer, sizeof(buffer), "%dd", daysLeft);
		} else {
			snprintf(buffer, sizeof(buffer), "%dg", daysLeft);
		}
  }
  update_icon_text_layer(data_model.battery, percent, buffer);
}

// Time
static bool is_ampm(){ // For emulator tests
	return !clock_is_24h_style();
}
static void destroy_property_animation(PropertyAnimation **animation) {
  if (animation == NULL || *animation == NULL) {
    return;
  }

  Animation *base_animation = (Animation *)*animation;
  if (animation_is_scheduled(base_animation)) {
    animation_unschedule(base_animation);
  }
  animation_destroy(base_animation);
  *animation = NULL;
}
static void slot_image_update_proc(Layer *layer, GContext *ctx) {
  Slot *slot = *(Slot **)layer_get_data(layer);
  if (!slot || !slot->image) return;

  const GRect b = layer_get_bounds(layer);
  const int dw = b.size.w, dh = b.size.h;      // dw <= MINUTES_SIZE
  const GRect sb = gbitmap_get_bounds(slot->image);
  const uint8_t *src = gbitmap_get_data(slot->image);
  const int stride = gbitmap_get_bytes_per_row(slot->image);

  if (settings.tColors) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
  }
  graphics_context_set_stroke_color(ctx, settings.tColors ? slot->digit_color : GColorWhite);

  uint8_t xbyte[MINUTES_SIZE], xmask[MINUTES_SIZE];   // once per frame, not per pixel
  for (int x = 0; x < dw; x++) {
    int d = (x < dw / 2) ? x : dw - 1 - x;
    int sx = d * sb.size.w / dw;
    if (x >= dw / 2) sx = sb.size.w - 1 - sx;
    xbyte[x] = sx >> 3;
    xmask[x] = 0x80 >> (sx & 7);
  }

  for (int y = 0; y < dh; y++) {
    int d = (y < dh / 2) ? y : dh - 1 - y;
    int sy = d * sb.size.h / dh;
    if (y >= dh / 2) sy = sb.size.h - 1 - sy;
    const uint8_t *row = src + sy * stride;

    int start = -1;                                    // draw runs, not single pixels
    for (int x = 0; x <= dw; x++) {
      bool on = (x < dw) && (row[xbyte[x]] & xmask[x]);
      if (on && start < 0) start = x;
      if (!on && start >= 0) {
        graphics_draw_line(ctx, GPoint(start, y), GPoint(x - 1, y));
        start = -1;
      }
    }
  }
}
static Layer* load_digit_into_slot(Slot *slot, int digit_value, Layer *parent_layer, GRect frame, const int *digit_resource_ids) {
  if (slot->image != NULL) {
    gbitmap_destroy(slot->image);
    slot->image = NULL;
  }
	slot->image = gbitmap_create_with_resource(digit_resource_ids[digit_value]);  // se NULL il draw proc non disegna nulla
  
  if (slot->image_layer == NULL) {
    slot->image_layer = layer_create_with_data(frame, sizeof(Slot *));
    if (slot->image_layer == NULL) {   // RAM esaurita: slot lasciato vuoto, verra' ricaricato
      if (slot->image != NULL) {
        gbitmap_destroy(slot->image);
        slot->image = NULL;
      }
      slot->state = EMPTY_SLOT;
      return NULL;
    }
    Slot **layer_data = (Slot **)layer_get_data(slot->image_layer);
    *layer_data = slot;

    layer_set_update_proc(slot->image_layer, slot_image_update_proc);

    layer_add_child(parent_layer, slot->image_layer);
  } else {
    layer_set_frame(slot->image_layer, frame);
    layer_mark_dirty(slot->image_layer);   // la bitmap e' cambiata anche se il frame fosse identico
  }
  slot->state = digit_value;

  return slot->image_layer;
}
static void release_slot(Slot *slot) {
  if (slot->image_layer != NULL) {
    layer_remove_from_parent(slot->image_layer);
    layer_destroy(slot->image_layer);
    slot->image_layer = NULL;
  }
  if (slot->image != NULL) {
    gbitmap_destroy(slot->image);
    slot->image = NULL;
  }
  slot->state = EMPTY_SLOT;
}
static GRect frame_for_time_slot(TimeSlot *time_slot) {
  int time_size = time_slot->slot_size;

  int extra_left = 0;
  int extra_top = 0;
  if (time_slot->slot_size > 78) { // BIG M
    extra_left = 0;
    extra_top = 2;
		#if defined(PBL_PLATFORM_GABBRO)
			extra_top -= 10;
		#endif	
  } else if (time_slot->slot_size == 78){ // BIG H
		extra_top = 0;
		extra_left = 20;
	} else if (time_slot->slot_size < 70){ // SMALL H
    extra_left = 22;
		#if defined(PBL_PLATFORM_GABBRO)
			extra_left += 8;
		#endif	
  } else if (time_slot->slot_size == 70){ // SMALL M
    extra_left = 0;
    extra_top = -28;
  }

  int x = extra_left + MARGIN + (time_slot->slot.number % 2) * (time_size + 2);
  int y = extra_top + MARGIN + (time_slot->slot.number / 2) * (HOURS_SIZE);
	if (is_ampm() && time_slot->slot.number == 1 && time_slots[0].slot.state == EMPTY_SLOT){
		x -= time_size/2 + 1;
	}

  return GRect(x, y, time_size, time_size);
}
static void time_slot_slide_in_animation_stopped(Animation *slide_in_animation, bool finished, void *context) {
  TimeSlot *time_slot = (TimeSlot *)context;

  destroy_property_animation(&time_slot->slide_in_animation);

  time_slot->updating = false;
}
static void slide_in_digit_into_time_slot(TimeSlot *time_slot, int digit_value) {
  int time_size = time_slot->slot_size;

  destroy_property_animation(&time_slot->slide_in_animation);

  GRect to_frame = frame_for_time_slot(time_slot);

  int from_x = to_frame.origin.x;
  int from_y = to_frame.origin.y;
  switch (time_slot->slot.number) {
    case 0:
      from_x -= time_size + MARGIN;
      break;
    case 1:
      from_y -= time_size + MARGIN;
      break;
    case 2:
      from_y += time_size + MARGIN;
      break;
    case 3:
      from_x += time_size + MARGIN;
      break;
  }
  GRect from_frame = GRect(from_x, from_y, time_size, time_size);

  Layer *image_layer = load_digit_into_slot(&time_slot->slot, digit_value, time_layer, from_frame, TIME_RESOURCE_IDS);
  if (image_layer == NULL) {   // RAM esaurita: lo slot resta vuoto
    time_slot->updating = false;
    return;
  }

  time_slot->slide_in_animation = property_animation_create_layer_frame(image_layer, &from_frame, &to_frame);
  if (time_slot->slide_in_animation == NULL) {   // niente RAM per l'animazione: cifra posizionata subito
    layer_set_frame(image_layer, to_frame);
    time_slot->updating = false;
    return;
  }

  Animation *animation = (Animation *)time_slot->slide_in_animation;
  animation_set_duration( animation,  TIME_SLOT_ANIMATION_DURATION);
  animation_set_curve(    animation,  AnimationCurveLinear);
  animation_set_handlers( animation,  (AnimationHandlers){
    .stopped = (AnimationStoppedHandler)time_slot_slide_in_animation_stopped
  }, (void *)time_slot);

  animation_schedule(animation);
}
static void time_slot_slide_out_animation_stopped(Animation *slide_out_animation, bool finished, void *context) {
  TimeSlot *time_slot = (TimeSlot *)context;

  destroy_property_animation(&time_slot->slide_out_animation);

  if (time_slot->new_state == EMPTY_SLOT) {
    time_slot->updating = false;
  } else {
    slide_in_digit_into_time_slot(time_slot, time_slot->new_state);

    time_slot->new_state = EMPTY_SLOT;
  }
}
static void slide_out_digit_from_time_slot(TimeSlot *time_slot) {
  int time_size = time_slot->slot_size;

  destroy_property_animation(&time_slot->slide_out_animation);

  GRect from_frame = frame_for_time_slot(time_slot);

  int to_x = from_frame.origin.x;
  int to_y = from_frame.origin.y;
  switch (time_slot->slot.number) {
    case 0:
      to_y -= time_size + MARGIN;
      break;
    case 1:
      to_x += time_size + MARGIN;
      break;
    case 2:
      to_x -= time_size + MARGIN;
      break;
    case 3:
      to_y += time_size + MARGIN;
      break;
  }
  GRect to_frame = GRect(to_x, to_y, time_size, time_size);

  Layer *image_layer = time_slot->slot.image_layer;

  time_slot->slide_out_animation = property_animation_create_layer_frame(image_layer, &from_frame, &to_frame);
  if (time_slot->slide_out_animation == NULL) {   // niente RAM per l'animazione: cambio istantaneo
    load_digit_into_slot(&time_slot->slot, time_slot->new_state, time_layer, from_frame, TIME_RESOURCE_IDS);
    time_slot->new_state = EMPTY_SLOT;
    time_slot->updating = false;
    return;
  }

  Animation *animation = (Animation *)time_slot->slide_out_animation;
  animation_set_duration( animation,  TIME_SLOT_ANIMATION_DURATION);
  animation_set_curve(    animation,  AnimationCurveLinear);
  animation_set_handlers(animation, (AnimationHandlers){
    .stopped = (AnimationStoppedHandler)time_slot_slide_out_animation_stopped
  }, (void *)time_slot);

  animation_schedule(animation);
}
static void update_time_slot(TimeSlot *time_slot, int digit_value) {
  if (time_slot->slot.state == digit_value)
    return;

  if (time_slot->updating) {
    return;
  }

  if (time_slot->slot.state == EMPTY_SLOT) {
    GRect frame = frame_for_time_slot(time_slot);
    load_digit_into_slot(&time_slot->slot, digit_value, time_layer, frame, TIME_RESOURCE_IDS);
  } else {
    time_slot->updating = true;
    time_slot->new_state = digit_value;
    slide_out_digit_from_time_slot(time_slot);
  }
}
static void display_time_value(int value, int row_number) {
  value = value % 100; // Maximum of two digits per row.

  for (int column_number = 1; column_number >= 0; column_number--) {
    int time_slot_number = (row_number * 2) + column_number;
		if (is_ampm() && value < 1 && row_number == 0 && column_number == 0) {
			return; 
		}
    TimeSlot *time_slot = &time_slots[time_slot_number];
		if (gcolor_equal(time_slot->slot.digit_color, GColorClear)) {
			GColor color = GColorWhite;
			if (settings.tColors){
				color = rnd_color();
				if (column_number == 0){
					TimeSlot *prev_time_slot = &time_slots[time_slot_number+1];
					color = prev_time_slot->slot.digit_color;
				}
			}
			time_slot->slot.digit_color = color;
			if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "new digit color: %u", color); }
		}

    update_time_slot(time_slot, value % 10);
    value = value / 10;
  }
}
static int get_ampm_hour(struct tm *tick_time){
  int hour = tick_time->tm_hour;

  if (is_ampm()) {
    hour = hour % 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  return hour;
}
static void set_ampm(int hour){
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Set AM/PM, hour: %u", hour); }
  if (is_ampm()){
    layer_set_hidden(text_layer_get_layer(data_model.meridiem->text), false);
		if (hour < 12) {
      snprintf(data_model.meridiem->buffer, sizeof(data_model.meridiem->buffer), "A\nM");
      update_item_color(data_model.meridiem, calculate_color(settings.tColors, GColorPastelYellow));
    } else {
			snprintf(data_model.meridiem->buffer, sizeof(data_model.meridiem->buffer), "P\nM");
      update_item_color(data_model.meridiem, calculate_color(settings.tColors, GColorRajah));
    }
  } else {
    layer_set_hidden(text_layer_get_layer(data_model.meridiem->text), true);
  }
}
static void display_time(struct tm *tick_time, bool force) {
  int hour = get_ampm_hour(tick_time);

  static int last_hour = -1;
  static int last_min = -1;

  if (hour != last_hour || force) {
    last_hour = hour;
    display_time_value(hour, 0);
  }
  if (tick_time->tm_min != last_min || force) {
    last_min = tick_time->tm_min;
    display_time_value(tick_time->tm_min, 1);
  }
}

// Date
static void update_date_layout(struct tm *tick_time){
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "update_date_layout"); }

	set_ampm(tick_time->tm_hour);
  
	Layer *text_layerW = text_layer_get_layer(data_model.wday->text);
  GRect frameW = layer_get_frame(text_layerW);
  Layer *text_layerD = text_layer_get_layer(data_model.day->text);
  GRect frameD = layer_get_frame(text_layerD);
  Layer *text_layerM = text_layer_get_layer(data_model.month->text);
  GRect frameM = layer_get_frame(text_layerM);
  Layer *text_layerY = text_layer_get_layer(data_model.year->text);
  GRect frameY = layer_get_frame(text_layerY);

  int dayX = frameD.origin.x;
  int monthX = frameM.origin.x;
  int yearX = frameY.origin.x;
  #if PBL_DISPLAY_HEIGHT == 168
		dayX -= 28;
    yearX -= 28;
  #endif
	
	if (us_locale()) {
    frameM.origin.x = dayX+8;
    frameD.origin.x = monthX;
    frameY.origin.x = yearX;
		#if defined(PBL_PLATFORM_GABBRO)
			frameW.origin.x -= 3;
			frameM.origin.x -= 4;
			frameD.origin.x -= 8;
		#endif
  } else {
    frameD.origin.x = dayX;
    frameM.origin.x = monthX;
    frameY.origin.x = yearX;
  }

	strftime(data_model.wday->buffer, sizeof(data_model.wday->buffer), "%a", tick_time);
  #if PBL_DISPLAY_HEIGHT == 168
		data_model.wday->buffer[2] = '\0';
	#endif
	text_layer_set_text(data_model.wday->text, data_model.wday->buffer);
  text_layer_set_text_color(data_model.wday->text, calculate_color(settings.fColors, rnd_color()));

	layer_set_frame(text_layerD, frameD);
  update_digit_layer(data_model.day, tick_time->tm_mday, calculate_color(settings.fColors, rnd_color()));

	layer_set_frame(text_layerM, frameM);
  update_digit_layer(data_model.month, tick_time->tm_mon+1, calculate_color(settings.fColors, rnd_color()));

  int year = tick_time->tm_year+1900;
  #if PBL_DISPLAY_HEIGHT == 168
    year -= 2000;
	#endif
	layer_set_frame(text_layerY, frameY);
  update_digit_layer(data_model.year, year, calculate_color(settings.fColors, rnd_color()));
}

static void memory(){
	uint32_t free = heap_bytes_free();
	uint32_t used = heap_bytes_used();

	if (DEBUGLOG) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "[RAM] Tot: %lu B | Used: %lu B | Free: %lu B", 
		        (unsigned long)free + used, (unsigned long)used, (unsigned long)free);
	}
}

static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
  if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "handle_tick start"); }

	time_t start_sec;
	uint16_t start_ms;
	if (DEBUGLOG) time_ms(&start_sec, &start_ms);

	if (units_changed == 0) {
		if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "-> System Tick"); }
		return;
	}
	if (DEBUGLOG) {    
    APP_LOG(APP_LOG_LEVEL_DEBUG, "units_changed: %lu", (unsigned long)units_changed);
    if (units_changed & SECOND_UNIT) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "-> SECOND_UNIT");
    }
    if (units_changed & MINUTE_UNIT) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "-> MINUTE_UNIT");
    }
    if (units_changed & HOUR_UNIT) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "-> HOUR_UNIT");
    }
    if (units_changed & DAY_UNIT) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "-> DAY_UNIT");
    }
    if (units_changed & MONTH_UNIT) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "-> MONTH_UNIT");
    }
    if (units_changed & YEAR_UNIT) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "-> YEAR_UNIT");
    }
  }

	if ((units_changed & MINUTE_UNIT) != 0) {
  	display_time(tick_time, false);
	}
  if ((units_changed & HOUR_UNIT) != 0) {
    if (settings.hourvibe) vibes_double_pulse();
    set_ampm(tick_time->tm_hour);
  }
  if ((units_changed & DAY_UNIT) != 0) {
    update_date_layout(tick_time);
  }
	memory();

	if (DEBUGLOG) { 
		time_t end_sec;
		uint16_t end_ms;
		time_ms(&end_sec, &end_ms);
		long diff_ms = ((end_sec - start_sec) * 1000) + ((int16_t)end_ms - (int16_t)start_ms);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "handle_tick duration: %ld ms", diff_ms);
	}
}

static void settings_callback(DictionaryIterator *iterator, void *context) {
  if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "settings_callback"); }
  Tuple *hourvibe_tuple = dict_find(iterator, MESSAGE_KEY_hourvibe);
  if (hourvibe_tuple) {
    settings.hourvibe = hourvibe_tuple->value->int8;
  }
  Tuple *hcolors_tuple = dict_find(iterator, MESSAGE_KEY_hcolors);
  if (hcolors_tuple) {
    settings.hColors = hcolors_tuple->value->int8;
  }
  Tuple *tcolors_tuple = dict_find(iterator, MESSAGE_KEY_tcolors);
  if (tcolors_tuple) {
    settings.tColors = tcolors_tuple->value->int8;
  }
  Tuple *fcolors_tuple = dict_find(iterator, MESSAGE_KEY_fcolors);
  if (fcolors_tuple) {
    settings.fColors = fcolors_tuple->value->int8;
  }
  Tuple *steps_tuple = dict_find(iterator, MESSAGE_KEY_steps);
  if (steps_tuple) {
    settings.steps = steps_tuple->value->int8;
  }
  Tuple *heart_tuple = dict_find(iterator, MESSAGE_KEY_heart_interval);
  if (heart_tuple) {
    char *interval_string = heart_tuple->value->cstring;
    settings.heart_interval = atoi(interval_string);
  }
  update_bluetooth(bluetooth_connection_service_peek());
  #if defined(PBL_HEALTH)
    update_health();
  #endif
  handle_battery(battery_state_service_peek());
  update_item_color(data_model.battery, calculate_color(settings.hColors, GColorGreen));

  time_t now = time(NULL);
  tm *tick_time = localtime(&now);
  update_date_layout(tick_time);
  display_time(tick_time, true);

  persist_write_data(PK_SETTINGS, &settings, sizeof(settings));
}

static void load_fonts(){
	font_12 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FR_FONT_12));
	font_16 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FR_FONT_16));
	font_18 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FR_FONT_18));
	font_20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FR_FONT_20));
}

static void init() {
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "INIT"); }
	srand(time(NULL));
	time_t start_sec;
	uint16_t start_ms;
	if (DEBUGLOG) time_ms(&start_sec, &start_ms);

	setlocale(LC_ALL, "");
  if (persist_exists(PK_SETTINGS)) {
    persist_read_data(PK_SETTINGS, &settings, sizeof(settings));
    if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Settings loaded"); }
  }
  #if !defined(PBL_COLOR)
    settings.hColors = false;
  #endif
  #if !defined(PBL_HEALTH)
    settings.steps = false;
  #endif
  #if !defined(PBL_PLATFORM_EMERY)
    settings.heart_interval = -1;
  #endif
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "settings.steps=%d, settings.heart_interval=%d", settings.steps, settings.heart_interval); }
  
	window = window_create();
  window_set_background_color(window, GColorBlack);

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  int screen_width = bounds.size.w;
  int screen_height = bounds.size.h;
  
	header_layer = layer_create(GRect(0, 0, screen_width, 24));
	GPoint btPoint = GPoint(1, 2);
	GPoint battPoint = GPoint(161, 6);
	GPoint battIconPoint = GPoint(157, 2);
	#if PBL_DISPLAY_HEIGHT == 168
		battPoint = GPoint(105, 6);
		battIconPoint = GPoint(101, 2);
	#elif defined(PBL_PLATFORM_GABBRO)
		header_layer = layer_create(GRect(0, 0, screen_width, 46));
		btPoint = GPoint(102, 3);
		battPoint = GPoint(118, 7);
		battIconPoint = GPoint(116, 3);
  #endif	
	load_fonts();

  // BT
  data_model.bt = load_image(RESOURCE_ID_BT, btPoint, calculate_color(settings.hColors, GColorBlueMoon));
  attach_icon(header_layer, data_model.bt);
  bluetooth_connection_service_subscribe(handle_bluetooth);
  // Battery
  data_model.battery = create_icon_text_layer(ITEM_BATTERY, header_layer, battPoint, "24g", font_12, calculate_color(settings.hColors, GColorGreen));
  data_model.battery->icon = load_image(RESOURCE_ID_BATTERY, battIconPoint, calculate_color(settings.hColors, GColorGreen));
  attach_icon(header_layer, data_model.battery->icon);
  handle_battery(battery_state_service_peek());
	#if defined(PBL_HEALTH)
    update_health();
  #endif
  layer_add_child(window_layer, header_layer);

  // Time
  int hSize = HOURS_SIZE;
  int mSize = MINUTES_SIZE;
	GPoint meridiemPoint = GPoint(181, 47);
  #if PBL_DISPLAY_HEIGHT == 168
    hSize = 48;
    mSize = 70;
    meridiemPoint = GPoint(meridiemPoint.x-57, meridiemPoint.y-30);
	#elif defined(PBL_PLATFORM_GABBRO)
		hSize = 68;
		meridiemPoint = GPoint(meridiemPoint.x-10, meridiemPoint.y-10);
  #endif
  for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    TimeSlot *time_slot = &time_slots[i];
    time_slot->slot.number  = i;
    time_slot->slot.state   = EMPTY_SLOT;
    time_slot->new_state    = EMPTY_SLOT;
    time_slot->updating     = false;
    if (i < 2) {
      time_slot->slot_size    = hSize;
    } else {
      time_slot->slot_size    = mSize;
    }
  }
  time_layer = layer_create(GRect(0, 24, screen_width, screen_height-46));
	#if PBL_DISPLAY_HEIGHT == 168
		time_layer = layer_create(GRect(0, 23, screen_width, screen_height-46));
	#elif defined(PBL_PLATFORM_GABBRO)
		time_layer = layer_create(GRect(30, 47, screen_width-60, screen_height-91));
	#endif
  //layer_set_clips(time_layer, true);

	if (is_ampm() && time_slots[0].slot.state == EMPTY_SLOT){
		meridiemPoint = GPoint(meridiemPoint.x-hSize/2+1, meridiemPoint.y);
	}
	
  data_model.meridiem = create_icon_text_layer(ITEM_MERIDIEM, time_layer, meridiemPoint, "A\nM", font_16, calculate_color(settings.hColors, GColorPastelYellow));
	layer_add_child(window_layer, time_layer);

  // Date
  footer_layer = layer_create(GRect(0, screen_height-26, screen_width, 26));
	#if defined(PBL_PLATFORM_GABBRO)
		footer_layer = layer_create(GRect(0, screen_height-43, screen_width, 46));
	#endif
  //layer_set_clips(footer_layer, true);

	GPoint wPoint = GPoint(1, 6);
	GPoint dPoint = GPoint(57, 4);
	GPoint mPoint = GPoint(screen_width/2+1, 8);
	GPoint yPoint = GPoint(screen_width/2+48, 12);
	#if defined(PBL_PLATFORM_GABBRO)
		wPoint = GPoint(58, 4);
		dPoint = GPoint(109, 2);
		mPoint = GPoint(168, 6);
		yPoint = GPoint(104, 28);
	#endif

  data_model.wday = create_icon_text_layer(ITEM_WDAY, footer_layer, wPoint, "Wed", font_18, calculate_color(settings.fColors, rnd_color()));
  text_layer_set_text_alignment(data_model.wday->text, GTextAlignmentLeft);
  data_model.day = create_icon_text_layer(ITEM_DAY, footer_layer, dPoint, "33", font_20, calculate_color(settings.fColors, rnd_color()));
  data_model.month = create_icon_text_layer(ITEM_MONTH, footer_layer, mPoint, "88", font_16, calculate_color(settings.fColors, rnd_color()));
  data_model.year = create_icon_text_layer(ITEM_YEAR, footer_layer, yPoint, "2026", font_12, calculate_color(settings.fColors, rnd_color()));
  text_layer_set_text_alignment(data_model.year->text, GTextAlignmentRight);
  layer_add_child(window_layer, footer_layer);

  time_t now = time(NULL);
  tm *tick_time = localtime(&now);
  update_date_layout(tick_time);
  display_time(tick_time, true);

	tick_timer_service_subscribe(MINUTE_UNIT, handle_tick);

  window_stack_push(window, true /* Animated */);

  app_message_register_inbox_received(settings_callback);
  app_message_open(128, 128);

	if (DEBUGLOG) { 
		time_t end_sec;
		uint16_t end_ms;
		time_ms(&end_sec, &end_ms);
		long diff_ms = ((end_sec - start_sec) * 1000) + ((int16_t)end_ms - (int16_t)start_ms);
		APP_LOG(APP_LOG_LEVEL_DEBUG, "init duration: %ld ms", diff_ms);
	}
}

static void de_init() {
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "DE-INIT"); }
  app_message_deregister_callbacks();

  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  #if defined(PBL_HEALTH)
    health_service_events_unsubscribe();
  #endif

  unload_image(&data_model.bt);
  unload_item(&data_model.steps);
  unload_item(&data_model.heart);
  unload_item(&data_model.battery);
  unload_item(&data_model.meridiem);
  for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    release_slot(&time_slots[i].slot);
  }
  unload_item(&data_model.wday);
  unload_item(&data_model.day);
  unload_item(&data_model.month);
  unload_item(&data_model.year);

  if (header_layer != NULL) { layer_destroy(header_layer); }
  if (time_layer != NULL)   { layer_destroy(time_layer); }
  if (footer_layer != NULL) { layer_destroy(footer_layer); }

	fonts_unload_custom_font(font_12);
	fonts_unload_custom_font(font_16);
	fonts_unload_custom_font(font_18);
	fonts_unload_custom_font(font_20);
  
	if (window != NULL) {
    window_destroy(window);
  }
}

int main(void) {
  init();
  app_event_loop();
  de_init();
}
