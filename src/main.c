#include <pebble.h>

#define KEY_STOP_NUMBER 1
#define KEY_STOP_LOCATION 2
#define KEY_STOP_NAME 3
#define KEY_SERVICE_INFO 4
#define KEY_SERVICE_DEST 5

GBitmap *window_icon;

// Request service information. If stop_number is zero, this will request the nearest stops.
// If stop_number is not zero, this will request the next few services for the stop.
void request_services(uint32_t stop_number) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter != NULL) {
    dict_write_uint32(iter, KEY_STOP_NUMBER, stop_number);
    app_message_outbox_send();
  }
}

uint32_t get_stop_number(Tuple *t) {
  uint32_t n;
  uint8_t i;
  n = 0;
  if (t != NULL) {
         if ((t->type == TUPLE_INT ) && (t->length == 2)) n = t->value[0].int16 ;
    else if ((t->type == TUPLE_INT ) && (t->length == 4)) n = t->value[0].int32 ;
    else if ((t->type == TUPLE_INT ) && (t->length == 1)) n = t->value[0].int8  ;
    else if ((t->type == TUPLE_UINT) && (t->length == 2)) n = t->value[0].uint16;
    else if ((t->type == TUPLE_UINT) && (t->length == 4)) n = t->value[0].uint32;
    else if ((t->type == TUPLE_UINT) && (t->length == 1)) n = t->value[0].uint8 ;
    else if (t->type == TUPLE_CSTRING) {
      for (i = 1; i < t->length; ++i) {
        n *= 10;
        n += (t->value[0].cstring[i - 1] - '0');
      }
    } else n = 0;
  }
  return n;
}

void get_cstring(char *str, uint8_t str_size, Tuple *t, char *default_str) {
  if ((t == NULL) || (t->type != TUPLE_CSTRING)) {
    strcpy(str, "");
  } else {
    strncpy(str, t->value[0].cstring, str_size);
  }
}

/* * * * * * * * * *
 * Services Window *
 * * * * * * * * * */

#define MAX_SERVICES 5
#define SERVICE_INFO_LEN 20
#define SERVICE_DEST_LEN 30

struct {
  char info[SERVICE_INFO_LEN];
  char dest[SERVICE_DEST_LEN];
} services[MAX_SERVICES];
uint8_t num_services;

Window *services_window;
MenuLayer *services_menu;
uint16_t services_mod; // minute of day for display purposes

#define HALF_DAY 720U /* half a day in minutes */
#define FULL_DAY 1440U /* full day in minutes */

void services_set_mod(struct tm *t) {
  services_mod = t->tm_hour * 60UL + t->tm_min;
}

/** Given a string with a time, eg "12:34", append the difference
 * between that time and the current service_mod. If services_mod
 * is 750 (12:30), then the resulting string will be "12:34 +4".
 * If services_mod is 760 (12:40), then the resulting string will
 * be "12:34 -6".
 * @param time String
 */
void services_append_diff(char *time) {
  char *p;
  uint8_t h, m;
  bool do_m, valid;
  uint16_t mod, diff, factor;
  /* parse out hours and minutes, eg h=12,m=34 */
  p = time;
  h = 0U;
  m = 0U;
  do_m = false;
  valid = true;
  while (*p != 0) {
    if (*p == ':') {
      do_m = true;
    } else if ((*p < '0') || (*p > '9')) {
      valid = false;
    } else {
      if (do_m) {
        m = m * 10U + (*p - '0');
      } else {
        h = h * 10U + (*p - '0');
      }
    }
    ++p;
  }
  if (valid) {
    /* mod = Minute Of Day (0..1439) */
    mod = h * 60UL + m;
    /* check if mod is most likely the previous day */
    if (mod > services_mod) {
      if (mod > (services_mod + HALF_DAY)) {
        services_mod += FULL_DAY;
      }
    }
    /* check if mod is most likely the next day */
    if (services_mod > mod) {
      if (services_mod > (mod + HALF_DAY)) {
        mod += FULL_DAY;
      }
    }
    /* format for display */
    *p++ = ' ';
    /* calculate difference */
    if (mod >= services_mod) {
      *p++ = '+';
      diff = mod - services_mod;
    } else {
      *p++ = '-';
      diff = services_mod - mod;
    }
    /* output difference, supressing leading zeroes */
    valid = false;
    factor = 100U;
    if (diff > 999U) {
      diff = 999U;
    }
    while (factor > 0U) {
      m = diff / factor % 10U;
      if ((m != 0) || (factor == 1U)) {
        valid = true;
      }
      if (valid) {
        *p++ = m + '0';
      }
      factor /= 10U;
    }
    *p = 0;
  }
}

uint16_t services_get_num_rows(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
  return (num_services == 0) ? 1 : num_services;
}

void services_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context) {
  char buf [30];
  char *title;
  char *subtitle;
  title = services[cell_index->row].info;
  subtitle = services[cell_index->row].dest;
  if (subtitle[0] == '\0') {
    subtitle = NULL;
  }
  strcpy(buf, title);
  buf[5] = 0;
  services_append_diff(buf);
  strcat(buf, &title[5]);
  title = buf;
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

void services_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
  services_set_mod(tick_time);
  menu_layer_reload_data(services_menu);
}

struct MenuLayerCallbacks services_menu_callbacks;

void services_window_load(Window *window) {
  time_t t;
  // Initialise globals
  num_services = 0;
  strncpy(services[0].info, "Finding services", SERVICE_INFO_LEN);
  services[0].dest[0] = '\0';
  time(&t);
  services_set_mod(localtime(&t));
  
  // Create the menu
  services_menu = menu_layer_create(GRect(0, 0, 144, 154));
  services_menu_callbacks.draw_header = NULL;
  services_menu_callbacks.draw_row = services_draw_row;
  services_menu_callbacks.get_cell_height = NULL;
  services_menu_callbacks.get_header_height = NULL;
  services_menu_callbacks.get_num_rows = services_get_num_rows;
  services_menu_callbacks.get_num_sections = NULL;
  services_menu_callbacks.select_click = NULL;
  services_menu_callbacks.select_long_click = NULL;
  services_menu_callbacks.selection_changed = NULL;
  menu_layer_set_callbacks(services_menu, NULL, services_menu_callbacks);
  menu_layer_set_click_config_onto_window(services_menu, services_window);
	
	// Add the menu to the window
	layer_add_child(window_get_root_layer(services_window), menu_layer_get_layer(services_menu));
  
  // Tick to update display every minute
  tick_timer_service_subscribe(MINUTE_UNIT, services_minute_tick);
  
  // Get the menu data
  request_services(*(uint32_t *)window_get_user_data(services_window));
}

void services_window_appear(Window *window) {
}

void services_window_disappear(Window *window) {
}

void services_window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  menu_layer_destroy(services_menu);
  num_services = 0;
}

void services_add(DictionaryIterator *received) {
  if (num_services < MAX_SERVICES) {
    get_cstring(services[num_services].info, SERVICE_INFO_LEN, dict_find(received, KEY_SERVICE_INFO), "Error");
    get_cstring(services[num_services].dest, SERVICE_DEST_LEN, dict_find(received, KEY_SERVICE_DEST), "Error");
    ++num_services;
    menu_layer_reload_data(services_menu);
  }
}

/* * * * * * * * *
 * Stops Window  *
 * * * * * * * * */

#define MAX_STOPS 15
#define STOP_LOCATION_LEN 20
#define STOP_NAME_LEN 30

struct {
  uint32_t stop_number;
  char location[STOP_LOCATION_LEN]; // eg "2.5km WNW"
  char name[STOP_NAME_LEN];
} stops[MAX_STOPS];
// special value UINT8_MAX used for time Searching
uint8_t num_stops;

Window *stops_window;
MenuLayer *stops_menu;

uint16_t stops_get_num_rows(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
  return (num_stops == 0) ? 1 : num_stops;
}

void stops_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context) {
  char buf[16];
  char *title;
  char *subtitle;
  uint16_t idx;
  idx = cell_index->row;
  if (num_stops == 0) {
    title = stops[0].location;
  } else {
    snprintf(buf, sizeof(buf), "%lu %s", stops[idx].stop_number, stops[idx].location);
    title = buf;
  }
  subtitle = stops[idx].name;
  if (subtitle[0] == '\0') {
    subtitle = NULL;
  }
  menu_cell_basic_draw(ctx, cell_layer, title, subtitle, NULL);
}

void stops_select_click(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  if (num_stops > 0) {
  	// Create services window and menu layer
	  services_window = window_create();
    
    window_set_status_bar_icon(services_window, window_icon);
    window_set_window_handlers(services_window, (WindowHandlers) {
      .load = services_window_load,
      .appear = services_window_appear,
      .disappear = services_window_disappear,
      .unload = services_window_unload
    });
    window_set_user_data(services_window, &stops[cell_index->row].stop_number);
	  window_stack_push(services_window, true);
  }
}

struct MenuLayerCallbacks stops_menu_callbacks;

void stops_window_load(Window *window) {
  // Initialise globals
  num_stops = 0;
  services_window = NULL;
  strncpy(stops[0].location, "Locating", STOP_LOCATION_LEN);
  stops[0].name[0] = '\0';
  
  // Create the menu
  stops_menu = menu_layer_create(GRect(0, 0, 144, 154));
  stops_menu_callbacks.draw_header = NULL;
  stops_menu_callbacks.draw_row = stops_draw_row;
  stops_menu_callbacks.get_cell_height = NULL;
  stops_menu_callbacks.get_header_height = NULL;
  stops_menu_callbacks.get_num_rows = stops_get_num_rows;
  stops_menu_callbacks.get_num_sections = NULL;
  stops_menu_callbacks.select_click = stops_select_click;
  stops_menu_callbacks.select_long_click = NULL;
  stops_menu_callbacks.selection_changed = NULL;
  menu_layer_set_callbacks(stops_menu, NULL, stops_menu_callbacks);
  menu_layer_set_click_config_onto_window(stops_menu, stops_window);
	
	// Add the menu to the window
	layer_add_child(window_get_root_layer(stops_window), menu_layer_get_layer(stops_menu));
}

void stops_window_appear(Window *window) {
  if (services_window != NULL) {
    window_destroy(services_window);
    services_window = NULL;
  }
}

void stops_window_disappear(Window *window) {
}

void stops_window_unload(Window *window) {
  menu_layer_destroy(stops_menu);
  num_stops = 0;
}

void stops_add(DictionaryIterator *received) {
  uint32_t n;
  n = get_stop_number(dict_find(received, KEY_STOP_NUMBER));
  if ((n == 0) && (num_stops == 0)) {
    get_cstring(stops[0].location, STOP_LOCATION_LEN, dict_find(received, KEY_STOP_LOCATION), "Error");
    stops[0].name[0] = '\0';
    menu_layer_reload_data(stops_menu);
  } else {
    if (num_stops < MAX_STOPS) {
      stops[num_stops].stop_number = n;
      get_cstring(stops[num_stops].location, STOP_LOCATION_LEN, dict_find(received, KEY_STOP_LOCATION), "Error");
      get_cstring(stops[num_stops].name, STOP_NAME_LEN, dict_find(received, KEY_STOP_NAME), "Error");
      ++num_stops;
      menu_layer_reload_data(stops_menu);
    }
  }
}

/* * * * * * * *
 * Application *
 * * * * * * * */

void out_sent_handler(DictionaryIterator *sent, void *context) {
}

void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  if (get_stop_number(dict_find(failed, KEY_STOP_NUMBER)) == 0) {
    // stops failed
    num_stops = 0;
    strncpy(stops[0].location, "No comms", STOP_LOCATION_LEN);
    stops[0].name[0] = '\0';
    menu_layer_reload_data(stops_menu);
  } else {
    // services failed
    num_services = 0;
    strncpy(services[0].info, "No comms", SERVICE_INFO_LEN);
    services[0].dest[0] = '\0';
    menu_layer_reload_data(services_menu);
  }
}

void in_received_handler(DictionaryIterator *received, void *context) {
  //app_log(APP_LOG_LEVEL_INFO, "main.c", __LINE__, "msgin");
  Tuple *t;
  t = dict_find(received, KEY_STOP_NUMBER);
  if (services_window == NULL) {
    // stops window
    if (t != NULL) {
      stops_add(received);
    }
  } else {
    // services windows
    if (t != NULL) {
      // error message
      if (num_services == 0) {
        get_cstring(services[0].info, SERVICE_INFO_LEN,  dict_find(received, KEY_STOP_LOCATION), "No data");
        services[0].dest[0] = '\0';
      }
    } else {
      // service record?
      if (dict_find(received, KEY_SERVICE_INFO) != NULL) {
        services_add(received);
      }
    }
    menu_layer_reload_data(services_menu);
  }
}

void in_dropped_handler(AppMessageResult reason, void *context) {
}

void handle_init(void) {
  // Register message handlers
  const uint32_t inbound_size = 64;
  const uint32_t outbound_size = 64;
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_outbox_sent(out_sent_handler);
  app_message_register_outbox_failed(out_failed_handler);
  app_message_open(inbound_size, outbound_size);
  
  window_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WINDOW_ICON);
  
	// Create a window and menu layer
	stops_window = window_create();
  window_set_status_bar_icon(stops_window, window_icon);
  window_set_window_handlers(stops_window, (WindowHandlers) {
    .load = stops_window_load,
    .appear = stops_window_appear,
    .disappear = stops_window_disappear,
    .unload = stops_window_unload
  });
	window_stack_push(stops_window, true);
}

void handle_deinit(void) {
	window_destroy(stops_window);
  gbitmap_destroy(window_icon);
}

int main(void) {
	handle_init();
	app_event_loop();
	handle_deinit();
}
