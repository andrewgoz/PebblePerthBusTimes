#include <pebble.h>
#include <ctype.h>

#define KEY_IDENTIFIER (1U)
#define KEY_TITLE      (2U)
#define KEY_SUBTITLE   (3U)
#define KEY_ICON       (4U)

#define MAX_MENUS    ( 2U)
#define MAX_STOPS    (12U) /* 2 headings + 10 stops */
#define MAX_SERVICES ( 6U) /* 1 heading + 5 services */

#define HALF_DAY ( 720U) /* half a day in minutes */
#define FULL_DAY (1440U) /* full day in minutes */

GBitmap * icon_bus16;
GBitmap * icon_train16;
GBitmap * icon_ferry16;

#define MAX_IDENTIFIER_LEN (20U)
#define MAX_TITLE_LEN      (32U)
#define MAX_SUBTITLE_LEN   (32U)
#define MAX_ICON_LEN       ( 2U)

typedef struct {
  char identifier[MAX_IDENTIFIER_LEN];
  char title[MAX_TITLE_LEN];
  char subtitle[MAX_SUBTITLE_LEN];
  char icon[MAX_ICON_LEN];
} menu_item_t;

typedef struct {
  uint8_t max_items;
  menu_item_t * items;
  uint8_t num_items;
  MenuLayer * layer;
  Window * window;
  bool sel_changed;
} menu_t;

menu_item_t stops[MAX_STOPS], services[MAX_SERVICES];
menu_t menus[MAX_MENUS];
uint8_t menu_idx;

void menus_init(void) {
  uint8_t i;
  for (i = 0U; i < MAX_MENUS; ++i) {
    menus[i].window = NULL;
    menus[i].sel_changed = false;
  }
  menus[0].max_items = MAX_STOPS;
  menus[0].items = stops;
  menus[1].max_items = MAX_SERVICES;
  menus[1].items = services;
}

/** Pebble time as minute-of-day. */
uint16_t pebble_mod;

void pebble_minute_tick(struct tm * tick_time, TimeUnits units_changed) {
  menu_t * menu = (menu_t *)app_message_get_context();
  pebble_mod = tick_time->tm_hour * 60U + tick_time->tm_min;
  if ((menu != NULL) && (menu->layer != NULL)) {
    menu_layer_reload_data(menu->layer);  
  }
}

/** Given a string, search it for the first sequence of "HH:MM" and parse
 * that as a time. Then output the time difference after it formatted as
 * " +HH:MM".
 * @param string String with embedded time to be updated with a time delta.
 */
void update_time_delta(char * string) {
  uint8_t i, h, m;
  uint16_t mod, pmod, diff;
  for (i = 0U; string[i] != '\0'; ++i) {
    if (isdigit((int)string[i + 0U]) && isdigit((int)string[i + 1U]) &&
        (string[i + 2U] == ':') &&
        isdigit((int)string[i + 3U]) && isdigit((int)string[i + 4U])) {
      
      /* Get the in-string time as minute-of-day */
      h = (string[i + 0U] - '0') * 10U + (string[i + 1U] - '0');
      m = (string[i + 3U] - '0') * 10U + (string[i + 4U] - '0');
      mod = h * 60U + m;
      
      /* check if mod is most likely the previous day */
      pmod = pebble_mod;
      if (mod > pmod) {
        if (mod > (pmod + HALF_DAY)) {
          pmod += FULL_DAY;
        }
      }
      /* check if mod is most likely the next day */
      if (pmod > mod) {
        if (pmod > (mod + HALF_DAY)) {
          mod += FULL_DAY;
        }
      }
      
      /* calculate difference */
      if (mod == pmod) {
        m = '=';
        diff = 0U;
      } else if (mod > pmod) {
        m = '+';
        diff = mod - pmod;
      } else {
        m = '-';
        diff = pmod - mod;
      }
      
      /* format for display */
      string[i + 5U] = ' ';
      string[i + 6U] = m;
      string[i + 8U] = ':';      
      h = diff / 60U;
      m = diff % 60U;
      if (h > 9U) {
        string[i +  7U] = '*';
        string[i +  9U] = '*';
        string[i + 10U] = '*';
      } else {
        string[i +  7U] = (h % 10U) + '0';
        string[i +  9U] = (m / 10U) + '0';
        string[i + 10U] = (m % 10U) + '0';
      }
      break;
    }
  }
}

/* * * * * * * * * *
 * Window Handling *
 * * * * * * * * * */

struct MenuLayerCallbacks menu_callbacks;

void window_load(Window * window) {
  menu_t * menu = (menu_t *)window_get_user_data(window);
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  menu->layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(menu->layer, menu, menu_callbacks);
  menu_layer_set_click_config_onto_window(menu->layer, menu->window);
  layer_add_child(window_get_root_layer(menu->window), menu_layer_get_layer(menu->layer));
}

void window_appear(Window * window) {
}

void window_disappear(Window * window) {
}

void window_unload(Window * window) {
  menu_t * menu = (menu_t *)window_get_user_data(window);
  menu_layer_destroy(menu->layer);
  menu->layer = NULL;
  menu->num_items = 0U;
  --menu_idx;
}

/* * * * * * * * *
 * Menu Handling *
 * * * * * * * * */

typedef struct {
  menu_t * menu;
  uint8_t idx;
  MenuIndex menu_idx;
  menu_item_t * item;
} menu_iterator_t;

void menu_iterator_init(menu_iterator_t * pi, menu_t * menu) {
  pi->menu = menu;
  pi->idx = UINT8_MAX;
  pi->menu_idx.section = 0U;
  pi->menu_idx.row = UINT16_MAX;
  pi->item = &menu->items[0];
}

bool menu_iterator_next(menu_iterator_t * pi) {
  bool ret = false;
  if (pi->idx == UINT8_MAX) {
    pi->idx = 0U;
    pi->menu_idx.row = 0U;
  }
  else {
    ++pi->idx;
    ++pi->menu_idx.row;
  }
  if (pi->idx < pi->menu->num_items) {
    ret = true;
    pi->item = &pi->menu->items[pi->idx];
    if ((pi->idx > 0U) && (pi->item->identifier[0] == '!')) {
      ++pi->menu_idx.section;
      pi->menu_idx.row = 0U;
    }
  }
  return ret;
}

void create_window(char * identifier) {
  menu_t * menu;
  DictionaryIterator * iter;
  if (menu_idx < MAX_MENUS) {
    menu = &menus[menu_idx];
    ++menu_idx;
    app_message_set_context(menu);
    if (menu->window != NULL) {
      window_destroy(menu->window);
    }
    menu->window = window_create();
    menu->sel_changed = false;
    window_set_user_data(menu->window, menu);
    menu->num_items = 1U;
    strcpy(menu->items[0].identifier, "$");
    strcpy(menu->items[0].title, "Waiting for phone");
    window_set_status_bar_icon(menu->window, icon_bus16);
    window_set_window_handlers(menu->window, (WindowHandlers) {
      .load = window_load,
      .appear = window_appear,
      .disappear = window_disappear,
      .unload = window_unload
    });
  	window_stack_push(menu->window, true);
    if (isalnum((int)identifier[0])) {
      /* send request to JS */
      app_message_outbox_begin(&iter);
      if (iter != NULL) {
        dict_write_cstring(iter, KEY_IDENTIFIER, identifier);
        app_message_outbox_send();
      }
    }
  }
}

void menu_draw_header(GContext * ctx, Layer const * cell_layer, uint16_t section_index, void * callback_context) {
  char * title = "";
  menu_iterator_t i;
  menu_iterator_init(&i, (menu_t *)callback_context);
  while (menu_iterator_next(&i)) {
    if (i.menu_idx.section == section_index) {
      title = i.item->title;
      break;
    }
  }
  menu_cell_basic_header_draw(ctx, cell_layer, title);
}

void menu_draw_row(GContext * ctx, Layer const * cell_layer, MenuIndex * cell_index, void * callback_context) {
  char title[MAX_TITLE_LEN];
  char subtitle[MAX_SUBTITLE_LEN];
  char icon;
  menu_iterator_t i;
  GRect r;
  GRect layer_rect;
  GSize title_size;
  int16_t icon_size;
  GBitmap * icon_bitmap;
  bool set_sel = false;
  menu_t * menu = (menu_t *)callback_context;
  menu_iterator_init(&i, menu);
  strcpy(title, i.item->title);
  subtitle[0] = '\0';
  icon = '\0';
  while (menu_iterator_next(&i)) {
    if ((i.menu_idx.section == cell_index->section) && (i.menu_idx.row == (cell_index->row + 1U))) {
      strcpy(title, i.item->title);
      strcpy(subtitle, i.item->subtitle);
      icon = i.item->icon[0];
      update_time_delta(title);
      update_time_delta(subtitle);
      if (isalnum((int)i.item->identifier[0])) {
        set_sel = true;
      }
      break;
    }
  }
  //layer_rect = layer_get_frame(cell_layer);
  layer_rect = layer_get_bounds(cell_layer);
  // draw icon
  switch (icon) {
  case 'B':
    icon_size = 16;
    icon_bitmap = icon_bus16;
    break;
  case 'T':
    icon_size = 16;
    icon_bitmap = icon_train16;
    break;
  case 'F':
    icon_size = 16;
    icon_bitmap = icon_ferry16;
    break;
  default:
    icon_size = 0;
    icon_bitmap = NULL;
    break;
  }
  r.origin.x = layer_rect.origin.x;
  r.origin.y = layer_rect.origin.y + 1;
  r.size.w = icon_size;
  r.size.h = icon_size;
  if (icon_bitmap != NULL) {  
    graphics_draw_bitmap_in_rect(ctx, icon_bitmap, r);
  }
  // draw first line of text
  layer_rect.origin.y -= 8;
  r.origin.x = layer_rect.origin.x + icon_size;
  r.origin.y = layer_rect.origin.y;
  r.size.w = layer_rect.size.w - icon_size;
  r.size.h = layer_rect.size.h;
  title_size = graphics_text_layout_get_content_size(title, fonts_get_system_font(FONT_KEY_GOTHIC_24), r, GTextOverflowModeFill, GTextAlignmentLeft);
  r.size.h = title_size.h;
  graphics_draw_text(ctx, title, fonts_get_system_font(FONT_KEY_GOTHIC_24), r, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  if (subtitle[0] != '\0') {
    // draw second line of text
    if (title_size.h < icon_size) {
      title_size.h = icon_size;
    }
    r.origin.x = layer_rect.origin.x;
    r.origin.y = layer_rect.origin.y + title_size.h;
    r.size.w = layer_rect.size.w;
    r.size.h = layer_rect.size.h - title_size.h;
    graphics_draw_text(ctx, subtitle, fonts_get_system_font(FONT_KEY_GOTHIC_18), r, GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
  //menu_cell_basic_draw(ctx, cell_layer, title, (subtitle[0] == '\0') ? NULL : subtitle, icon_bus16);
  if (set_sel && !menu->sel_changed) {
    menu->sel_changed = true;
    menu_layer_set_selected_index(menu->layer, *cell_index, MenuRowAlignCenter, true);
  }
}

int16_t menu_header_height(struct MenuLayer * menu_layer, uint16_t section_index, void * callback_context) {
  int16_t ret = 0;
  menu_iterator_t i;
  menu_iterator_init(&i, (menu_t *)callback_context);
  while (menu_iterator_next(&i)) {
    if (i.menu_idx.section == section_index) {
      if (i.item->identifier[0] == '!') {
        ret = MENU_CELL_BASIC_HEADER_HEIGHT;
      }
    }
  }
  return ret;
}

uint16_t menu_num_rows(struct MenuLayer * menu_layer, uint16_t section_index, void * callback_context) {
  uint16_t num = 0U;
  menu_iterator_t i;
  menu_iterator_init(&i, (menu_t *)callback_context);
  while (menu_iterator_next(&i)) {
    if (i.menu_idx.section == section_index) {
      if (i.item->identifier[0] != '!') {
        ++num;
      }
    }
  }
  return num;
}

uint16_t menu_num_sections(struct MenuLayer * menu_layer, void * callback_context) {
  menu_iterator_t i;
  menu_iterator_init(&i, (menu_t *)callback_context);
  while (menu_iterator_next(&i)) {
  }
  return i.menu_idx.section + 1U;
}

void menu_select_click(struct MenuLayer * menu_layer, MenuIndex * cell_index, void * callback_context) {
  menu_iterator_t i;
  menu_iterator_init(&i, (menu_t *)callback_context);
  while (menu_iterator_next(&i)) {
    if ((i.menu_idx.section == cell_index->section) && (i.menu_idx.row == (cell_index->row + 1U))) {
      if (isalnum((int)i.item->identifier[0])) {
        create_window(i.item->identifier);
      }
      break;
    }
  }
}

void menu_selection_changed(struct MenuLayer * menu_layer, MenuIndex new_index, MenuIndex old_index, void * callback_context) {
  menu_t * menu = (menu_t *)callback_context;
  menu->sel_changed = true;
}

void menu_callbacks_init(void) {
  menu_callbacks.draw_header = menu_draw_header;
  menu_callbacks.draw_row = menu_draw_row;
  menu_callbacks.get_cell_height = NULL;
  menu_callbacks.get_header_height = menu_header_height;
  menu_callbacks.get_num_rows = menu_num_rows;
  menu_callbacks.get_num_sections = menu_num_sections;
  menu_callbacks.select_click = menu_select_click;
  menu_callbacks.select_long_click = NULL;
  menu_callbacks.selection_changed = menu_selection_changed;
}

/* * * * * * * *
 * Application *
 * * * * * * * */

void out_sent_handler(DictionaryIterator * sent, void * context) {
}

void out_failed_handler(DictionaryIterator * failed, AppMessageResult reason, void * context) {
}

bool append_to_menu(menu_t * menu, char * identifier, char * title, char * subtitle, char * icon) {
  menu_item_t * i = &menu->items[menu->num_items];
  strcpy(i->identifier, identifier);
  strcpy(i->title, title);
  strcpy(i->subtitle, subtitle);
  strcpy(i->icon, icon);
  ++menu->num_items;
  return (identifier[0] == '!');
}

void in_received_handler(DictionaryIterator * received, void * context) {
  menu_t * menu = (menu_t *)context;
  //app_log(APP_LOG_LEVEL_INFO, "main.c", __LINE__, "in_received_handler");
  if (menu->num_items < menu->max_items) {
    /* If there is a message string, overwrite it */
    if (menu->num_items > 0U) {
      if (menu->items[menu->num_items - 1U].identifier[0] == '$') {
        --menu->num_items;
      }
    }
    /* Append received message to menu */
    if (append_to_menu(menu,
                       dict_find(received, KEY_IDENTIFIER)->value[0].cstring,
                       dict_find(received, KEY_TITLE)->value[0].cstring,
                       dict_find(received, KEY_SUBTITLE)->value[0].cstring,
                       dict_find(received, KEY_ICON)->value[0].cstring)) {
      /* Add "Please wait" under headings */
      append_to_menu(menu, "$", "Please wait", "", "");
    }
    menu_layer_reload_data(menu->layer);
  }
}

void in_dropped_handler(AppMessageResult reason, void * context) {
}

void handle_init(void) {
  menus_init();
  menu_callbacks_init();
  
  /* Register message handlers */
  const uint32_t inbound_size = 64;
  const uint32_t outbound_size = 64;
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_dropped_handler);
  app_message_register_outbox_sent(out_sent_handler);
  app_message_register_outbox_failed(out_failed_handler);
  app_message_open(inbound_size, outbound_size);
  icon_bus16 = gbitmap_create_with_resource(RESOURCE_ID_ICON_BUS_16X16);
  icon_train16 = gbitmap_create_with_resource(RESOURCE_ID_ICON_TRAIN_16X16);
  icon_ferry16 = gbitmap_create_with_resource(RESOURCE_ID_ICON_FERRY_16X16);
  
  create_window("");
  
  tick_timer_service_subscribe(MINUTE_UNIT, pebble_minute_tick);
}

void handle_deinit(void) {
  uint8_t i;
  Window * window;
  tick_timer_service_unsubscribe();
  for (i = 0U; i < MAX_MENUS; ++i) {
    window = menus[i].window;
    if (window != NULL) {
      window_destroy(window);
    }
  }
  gbitmap_destroy(icon_ferry16);
  gbitmap_destroy(icon_train16);
  gbitmap_destroy(icon_bus16);
  app_message_deregister_callbacks();
}

int main(void) {
  //if (bluetooth_connection_service_peek()) {
  	handle_init();
	  app_event_loop();
	  handle_deinit();
  //}
  /* 40B still allocated is a known issue related to tick_timer_service */
}
