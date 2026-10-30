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
#define DEBUGLOG					  0
#define ICONS_HEIGHT  20
#define BT_WIDTH     12	
#define BATTERY_WIDTH     43
#define HOURS_SIZE  78
#define MINUTES_SIZE   98
#define MARGIN              1
#define BATTERY_DAYS  30
#define NUMBER_OF_TIME_IMAGES 10
#define EMPTY_SLOT -1
#define NUMBER_OF_TIME_SLOTS 4

const int TIME_RESOURCE_IDS[NUMBER_OF_TIME_IMAGES] = {
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
	char buffer[8];
	int curr_val;
} IconTextLayer;
typedef struct {
  BitmapLayer *bt;
	TextLayer *meridiem;  
	IconTextLayer *battery;
  IconTextLayer *wday;
	IconTextLayer *day;
  IconTextLayer *month;
  IconTextLayer *year;
  IconTextLayer *steps;
  IconTextLayer *heart;
} dataModelType;
static dataModelType data_model;

static Layer *header_layer, *time_layer, *footer_layer;
typedef struct Slot {
  int         number;
  GBitmap     *image;
  Layer 			*image_layer;
  int         state;
	GColor			digit_color;
} Slot;
typedef struct TimeSlot {
  Slot              slot;
  int               new_state;
  PropertyAnimation *slide_out_animation;
  PropertyAnimation *slide_in_animation;
  bool              updating;
  int 				slot_size;
} TimeSlot;
static TimeSlot time_slots[NUMBER_OF_TIME_SLOTS];

typedef struct {
  bool usdate;            // DD Month/Month DD
	bool hourvibe;
  bool hColors;
	bool tColors;           
	bool fColors;	
	bool steps;            
	int heart_interval;    
} Persist;
static Persist settings = {
  .usdate = false,
	.hourvibe = false,
	.hColors = true,
	.tColors = false,
	.fColors = true,
	.steps = true,
	.heart_interval = 600
};

static TextLayer* createText(Layer *root, GRect rect, char *text, char *font, const GColor color){
	TextLayer* textLayer = text_layer_create(rect);
  text_layer_set_text(textLayer, text);
  
  text_layer_set_text_color(textLayer, color);
	text_layer_set_background_color(textLayer, GColorClear);
  text_layer_set_font(textLayer, fonts_get_system_font(font));
  text_layer_set_text_alignment(textLayer, GTextAlignmentCenter);    

  layer_add_child(root, text_layer_get_layer(textLayer));

	return textLayer;
}
static IconTextLayer* createIconText(Layer *root, GRect rect, char *text, char *font, const GColor color){
	IconTextLayer *custom_layer = calloc(1, sizeof(IconTextLayer));
  custom_layer->icon = NULL;

  custom_layer->text = createText(root, rect, text, font, color);
  
  return custom_layer;
}
static void updateTextLayer(IconTextLayer *custom_layer, int val){
	snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%02d", val);
	text_layer_set_text(custom_layer->text, custom_layer->buffer);
}
static void updateIconTextLayer(IconTextLayer *custom_layer, int val, char *suffix) {
  if (custom_layer == NULL || custom_layer->text == NULL || val == custom_layer->curr_val) {
    return;
  }

	if(val > 0){
		if (suffix != NULL) {
		  snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%d%s", val, suffix);
		} else {
		  snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%02d", val);
		}
	}else if(suffix != NULL) {
		snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%s", suffix);
	}else {
		snprintf(custom_layer->buffer, sizeof(custom_layer->buffer), "%s", "ND");
	}  

  text_layer_set_text(custom_layer->text, custom_layer->buffer);
  layer_mark_dirty(text_layer_get_layer(custom_layer->text));
	custom_layer->curr_val = val;
}
static BitmapLayer* loadImage(const int res, int x, int y, const GColor color){
  GBitmap *image = gbitmap_create_with_resource(res);  
	GRect bounds = gbitmap_get_bounds(image);
  GRect rect = GRect(x, y, bounds.size.w, bounds.size.h);  
	BitmapLayer *image_layer = bitmap_layer_create(rect);
  bitmap_layer_set_bitmap(image_layer, image);
	GColor *palette = gbitmap_get_palette(image);
  if (palette != NULL) {
    palette[1] = color;
  }
	bitmap_layer_set_background_color(image_layer, GColorClear);
	bitmap_layer_set_compositing_mode(image_layer, GCompOpSet);
  
  return image_layer;
}
static void updateIconColor(BitmapLayer *image_layer, GColor color) {
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
static void unloadImage(BitmapLayer **image_ptr) {
  if (image_ptr == NULL || *image_ptr == NULL) return;

	BitmapLayer *image = *image_ptr;
  
  GBitmap *bmp = (GBitmap *)bitmap_layer_get_bitmap(image);
  if (bmp != NULL) gbitmap_destroy(bmp);

  Layer *base_layer = bitmap_layer_get_layer(image);
  if (base_layer != NULL) layer_remove_from_parent(base_layer);

  bitmap_layer_destroy(image);
}
static void unloadText(TextLayer **text_ptr) {
  if (text_ptr == NULL || *text_ptr == NULL) return;

	TextLayer *text = *text_ptr;
  Layer *base_layer = text_layer_get_layer(text);
  if (base_layer != NULL) layer_remove_from_parent(base_layer);

  text_layer_destroy(text);
}
static void unloadItem(IconTextLayer **layer_ptr) {
  if (layer_ptr == NULL || *layer_ptr == NULL) return;
	IconTextLayer *layer = *layer_ptr;

  if (layer->icon != NULL) unloadImage(&(layer->icon));
  if (layer->text != NULL) unloadText(&(layer->text));
	layer->curr_val = 0;

  free(layer);
	*layer_ptr = NULL;
}
static GColor calculateColor(bool enable, GColor color){
		#if defined(PBL_COLOR)
			if(enable) return color;
		#endif
		return GColorWhite;
}
static void updateItemColor(IconTextLayer *iconText, bool colored, const GColor color){
	if(iconText != NULL){
		text_layer_set_text_color(iconText->text, calculateColor(colored, color));
		updateIconColor(iconText->icon, calculateColor(colored, color));
	} 
}
static GColor rndColor(){
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

// Animations
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
  slot->state = digit_value;
  slot->image = gbitmap_create_with_resource(digit_resource_ids[digit_value]);

  slot->image_layer = layer_create_with_data(frame, sizeof(Slot *));
  slot->digit_color = rndColor();
  
	Slot **layer_data = (Slot **)layer_get_data(slot->image_layer);
  *layer_data = slot;

  layer_set_update_proc(slot->image_layer, slot_image_update_proc);

  layer_add_child(parent_layer, slot->image_layer);
 	
	return slot->image_layer;
}
static void unload_digit_from_slot(Slot *slot) {
  if (slot->state == EMPTY_SLOT)
    return;

  layer_remove_from_parent(slot->image_layer);
	layer_destroy(slot->image_layer);

  gbitmap_destroy(slot->image);

  slot->state = EMPTY_SLOT;
}

static GRect frame_for_time_slot(TimeSlot *time_slot) {
  int time_size = time_slot->slot_size;
  
  int extra_left = 20;
  int extra_top = 23;
	if(time_slot->slot_size > 78) { // BIG M
    extra_left = 0;
	  extra_top = 3;
  }else if(time_slot->slot_size < 70){ // SMALL H
		extra_left = 22;
	}else if(time_slot->slot_size == 70){ // SMALL M
	  extra_left = 0;
		extra_top = 0;
	}
	
  int x = extra_left + MARGIN + (time_slot->slot.number % 2) * (time_size + 2);
  int y = extra_top + MARGIN + (time_slot->slot.number / 2) * (time_size + 2);

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

  time_slot->slide_in_animation = property_animation_create_layer_frame(image_layer, &from_frame, &to_frame);

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
  }
  else {
    unload_digit_from_slot(&time_slot->slot);

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

// Time
static void display_time_value(int value, int row_number) {
  value = value % 100; // Maximum of two digits per row.

  for (int column_number = 1; column_number >= 0; column_number--) {
    int time_slot_number = (row_number * 2) + column_number;
    TimeSlot *time_slot = &time_slots[time_slot_number];
    update_time_slot(time_slot, value % 10);
    value = value / 10;
  }
}
int get_ampm_hour(struct tm *tick_time){
  int hour = tick_time->tm_hour;
  
	if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) {
      hour = 12;
    }
  }
  return hour;
}
static void set_ampm(int hour){
	//if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Hour: %d", hour); }
	if(!clock_is_24h_style()){
		layer_set_hidden(text_layer_get_layer(data_model.meridiem), false);		
    if(hour < 12) {
			text_layer_set_text(data_model.meridiem, "AM");
			text_layer_set_text_color(data_model.meridiem, calculateColor(settings.tColors, GColorPastelYellow));
		}else {
			text_layer_set_text(data_model.meridiem, "PM");
			text_layer_set_text_color(data_model.meridiem, calculateColor(settings.tColors, GColorRajah));
		}	
		layer_mark_dirty(text_layer_get_layer(data_model.meridiem));
	}else{
	  layer_set_hidden(text_layer_get_layer(data_model.meridiem), true);
	}
}
static void display_time(struct tm *tick_time) {
  int hour = get_ampm_hour(tick_time);

	static int last_hour = -1;
  static int last_min = -1;

  if (hour != last_hour) {
    last_hour = hour;
    display_time_value(hour, 0);
  }
	if (tick_time->tm_min != last_min) {
    last_min = tick_time->tm_min;
    display_time_value(tick_time->tm_min, 1);
  }
}

// BT
static void update_bluetooth(bool connected){
	if(settings.hColors){
		layer_set_hidden(bitmap_layer_get_layer(data_model.bt), false);
		if(connected) updateIconColor(data_model.bt, GColorBlueMoon);
		else updateIconColor(data_model.bt, GColorLightGray);
	}else layer_set_hidden(bitmap_layer_get_layer(data_model.bt), !connected);
}
static void handle_bluetooth(bool connected) {	
  vibes_short_pulse();
	
	update_bluetooth(connected);
}

// Battery
static void handle_battery(BatteryChargeState charge_state) {
	int totalDays = BATTERY_DAYS;
	#if !defined(PBL_PLATFORM_EMERY)
		totalDays = 7;
	#endif
	int percent = charge_state.charge_percent;
	if(percent > 0 && percent <=10){
		int hoursLeft = (int)totalDays * 24 * percent / 100; 
		snprintf(data_model.battery->buffer, sizeof(data_model.battery->buffer), "%dH", hoursLeft);
	}else{		
		int daysLeft = (int)totalDays*percent/100; 
		const char* sys_locale = i18n_get_system_locale();
		if (strncmp(sys_locale, "it", 2) == 0) {
			snprintf(data_model.battery->buffer, sizeof(data_model.battery->buffer), "%dG", daysLeft);
		}else{
			snprintf(data_model.battery->buffer, sizeof(data_model.battery->buffer), "%dD", daysLeft);
		}
	}
	text_layer_set_text(data_model.battery->text, data_model.battery->buffer);
	updateItemColor(data_model.battery, settings.hColors, GColorGreen);
}

#if defined(PBL_HEALTH)
	static void aggiorna_passi() {
		if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health Steps %d", (int)settings.steps); }
		if (settings.steps == true){
			if(data_model.steps == NULL){
				data_model.steps = createIconText(header_layer, GRect(35, -5, 66, ICONS_HEIGHT+6), "99.9km", FONT_KEY_GOTHIC_24_BOLD, calculateColor(settings.hColors, GColorYellow));  
				text_layer_set_text_alignment(data_model.steps->text, GTextAlignmentLeft);
				data_model.steps->icon = loadImage(RESOURCE_ID_STEPS, 15, 2, calculateColor(settings.hColors, GColorYellow));
				layer_add_child(header_layer, bitmap_layer_get_layer(data_model.steps->icon));
				updateItemColor(data_model.steps, settings.hColors, GColorYellow);
			}
					
			int meters = (int)health_service_sum_today(HealthMetricWalkedDistanceMeters);
			if(meters != data_model.steps->curr_val){
				MeasurementSystem sys_metric = health_service_get_measurement_system_for_display(HealthMetricWalkedDistanceMeters);
				if (sys_metric == MeasurementSystemMetric){
					const int KM = 1000;
					int intKm = meters / KM;
					int decKm = (meters % KM) / 100;
					snprintf(data_model.steps->buffer, sizeof(data_model.steps->buffer), "%d,%dkm", intKm, decKm);
				}else {
					const int STATUTE_MILE = 1609;
					int intMiles = meters / STATUTE_MILE;
					int decMiles = ((meters % STATUTE_MILE) * 10) / STATUTE_MILE;
					snprintf(data_model.steps->buffer, sizeof(data_model.steps->buffer), "%d.%dmi", intMiles, decMiles);
				}
				text_layer_set_text(data_model.steps->text, data_model.steps->buffer);
				data_model.steps->curr_val = meters;
			}
		}else if (data_model.steps != NULL){
			unloadItem(&data_model.steps);			
		}
	}
	#if PBL_API_EXISTS(health_service_peek_current_value)
		static void aggiorna_cardio() {
			if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health HR %d", settings.heart_interval); }
			if (settings.heart_interval >= 0){
				if(data_model.heart == NULL){
					data_model.heart = createIconText(header_layer, GRect(124, -5, 40, ICONS_HEIGHT+4), "80", FONT_KEY_GOTHIC_24_BOLD, calculateColor(settings.hColors, GColorRed));
					text_layer_set_text_alignment(data_model.heart->text, GTextAlignmentLeft);
					data_model.heart->icon = loadImage(RESOURCE_ID_HEART, 101, 2, calculateColor(settings.hColors, GColorRed));
					layer_add_child(header_layer, bitmap_layer_get_layer(data_model.heart->icon));
					updateItemColor(data_model.heart, settings.hColors, GColorRed);
					HealthServiceAccessibilityMask hr_raw_access = health_service_metric_accessible(HealthMetricHeartRateRawBPM, time(NULL), time(NULL));
					if(hr_raw_access & HealthServiceAccessibilityMaskAvailable){
						int heartBeatInit = (int)health_service_peek_current_value(HealthMetricHeartRateRawBPM);
						if (heartBeatInit == 0) {
							heartBeatInit = 10 + (rand() % 7); // Genera un finto Heart Beat per i test
						}
						if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Heart init: %dRPM", heartBeatInit); }
						updateIconTextLayer(data_model.heart, heartBeatInit, NULL);
					}
				}
				HealthServiceAccessibilityMask hr_access = health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
				if(hr_access & HealthServiceAccessibilityMaskAvailable){
					int heartBeat = (int)health_service_peek_current_value(HealthMetricHeartRateBPM); // HealthMetricHeartRateBPM/HealthMetricHeartRateRawBPM for micro changes 
					if(heartBeat != 0 && heartBeat != data_model.heart->curr_val){
						if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Heart update: %dRPM", heartBeat); }
						updateIconTextLayer(data_model.heart, heartBeat, NULL);
					}
				}
			}else if (data_model.heart != NULL){
				unloadItem(&data_model.heart);			
			}
		}
	#endif
	static void health_handler(HealthEventType event, void *context) {
		if (event == HealthEventMovementUpdate) {
			aggiorna_passi();
		}else if (event == HealthEventHeartRateUpdate) {
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
	static void updateHealth(){
			if(settings.steps || settings.heart_interval >= 0){
				if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health start"); }
			
				if (settings.steps) updateItemColor(data_model.steps, settings.hColors, GColorYellow);
				aggiorna_passi();
				#if PBL_API_EXISTS(health_service_peek_current_value)
					if (settings.heart_interval >= 0 && data_model.heart != NULL) updateItemColor(data_model.heart, settings.hColors, GColorRed);
					aggiorna_cardio();
					health_service_set_heart_rate_sample_period(settings.heart_interval);
					int hr_sample_period_exp = health_service_get_heart_rate_sample_period_expiration_sec();
					if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "HR hr_sample_period_exp %d", hr_sample_period_exp); }
				#endif
				health_service_events_subscribe(health_handler, NULL);
			}else{
				if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Health stop"); }
				health_service_events_unsubscribe();

				unloadItem(&data_model.steps);
				unloadItem(&data_model.heart);		
			}
	}
#endif

static void updateDateLayout(struct tm *tick_time, bool usdate){
	strftime(data_model.wday->buffer, sizeof(data_model.wday->buffer), "%a", tick_time);
	text_layer_set_text(data_model.wday->text, data_model.wday->buffer);
	text_layer_set_text_color(data_model.wday->text, calculateColor(settings.fColors, rndColor()));

	Layer *text_layerW = text_layer_get_layer(data_model.wday->text);
	GRect frameW = layer_get_frame(text_layerW);
	Layer *text_layerD = text_layer_get_layer(data_model.day->text);
	GRect frameD = layer_get_frame(text_layerD);
	Layer *text_layerM = text_layer_get_layer(data_model.month->text);
	GRect frameM = layer_get_frame(text_layerM);
	Layer *text_layerY = text_layer_get_layer(data_model.year->text);
	GRect frameY = layer_get_frame(text_layerY);

	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	int screen_width = bounds.size.w;

	int wdayX = 19;
	int dayX = screen_width/2-39;
  int monthX = screen_width/2-1;
	int yearX = screen_width-69;
	#if !defined(PBL_PLATFORM_EMERY)
		wdayX = 1;
		yearX = screen_width-52;
	#endif
	if(usdate){
		frameW.origin.x = wdayX-3;
		frameD.origin.x = monthX;
		text_layer_set_text_alignment(data_model.day->text, GTextAlignmentLeft);
		frameM.origin.x = dayX+7;
		text_layer_set_text_alignment(data_model.month->text, GTextAlignmentRight);
		frameY.origin.x = yearX+3;
		text_layer_set_text_alignment(data_model.year->text, GTextAlignmentRight);
	}else{
		frameW.origin.x = wdayX;
		frameD.origin.x = dayX;
		text_layer_set_text_alignment(data_model.day->text, GTextAlignmentRight);
		frameM.origin.x = monthX;
		text_layer_set_text_alignment(data_model.month->text, GTextAlignmentLeft);
		frameY.origin.x = yearX;
	}
	layer_set_frame(text_layerW, frameW);
	layer_set_frame(text_layerD, frameD);
	layer_set_frame(text_layerM, frameM);
	layer_set_frame(text_layerY, frameY);

	updateTextLayer(data_model.day, tick_time->tm_mday);
	text_layer_set_text_color(data_model.day->text, calculateColor(settings.fColors, rndColor()));
	
	updateTextLayer(data_model.month, tick_time->tm_mon+1);
	text_layer_set_text_color(data_model.month->text, calculateColor(settings.fColors, rndColor()));

	int year = tick_time->tm_year+1900;
	#if !defined(PBL_PLATFORM_EMERY)
		year = year - 2000;
	#endif	
	updateTextLayer(data_model.year, year);
	text_layer_set_text_color(data_model.year->text, calculateColor(settings.fColors, rndColor()));
}
static void handle_tick(struct tm *tick_time, TimeUnits units_changed) {
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "handle_tick"); }
	display_time(tick_time);
	
	if ((units_changed & HOUR_UNIT) != 0) {
		if(settings.hourvibe) {
      vibes_double_pulse();
    }
		set_ampm(tick_time->tm_hour);
  }
	if ((units_changed & DAY_UNIT) != 0) {
	  updateDateLayout(tick_time, settings.usdate);
	}
}
static void settings_callback(DictionaryIterator *iterator, void *context) {
	if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "settings_callback"); }
  Tuple *date_tuple = dict_find(iterator, MESSAGE_KEY_usdate);
  if (date_tuple) {
    settings.usdate = date_tuple->value->int8;
  }
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
		updateHealth();
	#endif	
	handle_battery(battery_state_service_peek());
  time_t now = time(NULL);
  tm *tick_time = localtime(&now);
	display_time(tick_time);
	set_ampm(tick_time->tm_hour);
	updateDateLayout(tick_time, settings.usdate);
	
	persist_write_data(PK_SETTINGS, &settings, sizeof(settings));
}

static void init() {
	setlocale(LC_ALL, "");
	if (persist_exists(PK_SETTINGS)) {
    persist_read_data(PK_SETTINGS, &settings, sizeof(settings));
    if (DEBUGLOG) { APP_LOG(APP_LOG_LEVEL_DEBUG, "Settings loaded"); }
  }else{
		#if !defined(PBL_COLOR)
			settings.hColors = false;	
		#endif
		#if !defined(PBL_HEALTH)
			settings.steps = false;
			#if !defined(PBL_PLATFORM_EMERY)
				settings.heart_interval = -1;
			#endif
		#endif
	}
	
  window = window_create();
  window_set_background_color(window, GColorBlack);

  Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);
	int screen_width = bounds.size.w;
	int screen_height = bounds.size.h;
  header_layer = layer_create(GRect(0, 0, screen_width, 24));
  
  // BT
  data_model.bt = loadImage(RESOURCE_ID_BT, 2, 2, calculateColor(settings.hColors, GColorBlueMoon));
  layer_add_child(header_layer, bitmap_layer_get_layer(data_model.bt));
	update_bluetooth(bluetooth_connection_service_peek());
  bluetooth_connection_service_subscribe(handle_bluetooth);
	// Battery
  data_model.battery = createIconText(header_layer, GRect(screen_width-BATTERY_WIDTH-3, -5, BATTERY_WIDTH, ICONS_HEIGHT+4), "30", FONT_KEY_GOTHIC_24_BOLD, calculateColor(settings.hColors, GColorWhite));  
  data_model.battery->icon = loadImage(RESOURCE_ID_BATTERY,screen_width-BATTERY_WIDTH-2, 2, calculateColor(settings.hColors, GColorGreen));
	layer_add_child(header_layer, bitmap_layer_get_layer(data_model.battery->icon));
	handle_battery(battery_state_service_peek());
	battery_state_service_subscribe(handle_battery);
	#if defined(PBL_HEALTH)
		updateHealth();
	#endif
	layer_add_child(window_layer, header_layer);
  
  // Time
	int hSize = HOURS_SIZE;
	int mSize = MINUTES_SIZE;
	int meridiemX = screen_width-21;
	#if !defined(PBL_PLATFORM_EMERY)
		hSize = 48;
		mSize = 70;
		meridiemX = screen_width-22;
	#endif
  for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    TimeSlot *time_slot = &time_slots[i];
	  time_slot->slot.number  = i;
    time_slot->slot.state   = EMPTY_SLOT;
    time_slot->new_state    = EMPTY_SLOT;
    time_slot->updating     = false;
  	if(i < 2) {
  	  time_slot->slot_size    = hSize;
  	}else {
  	  time_slot->slot_size    = mSize;
  	}
  }
	time_layer = layer_create(GRect(0, 0, screen_width, screen_height-24));
  layer_set_clips(time_layer, true);
  data_model.meridiem = createText(time_layer, GRect(meridiemX, 23+hSize-17, 22, 20), "AM", FONT_KEY_GOTHIC_18_BOLD, calculateColor(settings.tColors, GColorPastelYellow));
	layer_add_child(window_layer, time_layer);
	// Date
	footer_layer = layer_create(GRect(0, screen_height-24, screen_width, 24));
	layer_set_clips(footer_layer, true);
  data_model.wday = createIconText(footer_layer, GRect(0, -6, 42, 28), "Wed", FONT_KEY_GOTHIC_28_BOLD, calculateColor(settings.fColors, rndColor()));
	text_layer_set_text_alignment(data_model.wday->text, GTextAlignmentLeft);
  data_model.day = createIconText(footer_layer, GRect(0, -10, 40, 48), "1", FONT_KEY_LECO_32_BOLD_NUMBERS, calculateColor(settings.fColors, rndColor()));
  data_model.month = createIconText(footer_layer, GRect(0, -4, 32, 46), "2", FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM, calculateColor(settings.fColors, rndColor()));
  data_model.year = createIconText(footer_layer, GRect(0, 2, 52, 20), "2026", FONT_KEY_LECO_20_BOLD_NUMBERS, calculateColor(settings.fColors, rndColor()));
	text_layer_set_text_alignment(data_model.year->text, GTextAlignmentRight);
	layer_add_child(window_layer, footer_layer);
	
  time_t now = time(NULL);
  tm *tick_time = localtime(&now);
	display_time(tick_time);
	set_ampm(tick_time->tm_hour);
	updateDateLayout(tick_time, settings.usdate);
  tick_timer_service_subscribe(MINUTE_UNIT, handle_tick);

	window_stack_push(window, true /* Animated */);
  
	app_message_register_inbox_received(settings_callback);
  app_message_open(128, 128);  
}

static void deinit() {
	app_message_deregister_callbacks();

  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  health_service_events_unsubscribe();

	unloadImage(&data_model.bt);
  unloadItem(&data_model.steps);       
  unloadItem(&data_model.heart);
  unloadItem(&data_model.battery);
  unloadText(&data_model.meridiem);
	for (int i = 0; i < NUMBER_OF_TIME_SLOTS; i++) {
    if (time_slots[i].slot.image_layer != NULL) {
      layer_remove_from_parent(time_slots[i].slot.image_layer);
      layer_destroy(time_slots[i].slot.image_layer);
    }
    if (time_slots[i].slot.image != NULL) {
      gbitmap_destroy(time_slots[i].slot.image);
    }
  }
  unloadItem(&data_model.wday);
  unloadItem(&data_model.day);
  unloadItem(&data_model.month);
  unloadItem(&data_model.year);

	if (header_layer != NULL) { layer_destroy(header_layer); }
  if (time_layer != NULL)   { layer_destroy(time_layer); }
  if (footer_layer != NULL) { layer_destroy(footer_layer); }

  if (window != NULL) {
    window_destroy(window);
  }
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}