/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * *****************************************************************/


#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xkbcommon/xkbcommon.h>

#include <config.h>
#include <gmerlin/translation.h>


#include <gmerlin/plugin.h>
#include <gmerlin/glvideo.h>
#include <gmerlin/state.h>

#include <gavl/log.h>
#define LOG_DOMAIN "ov_wayland"

#include <gavl/utils.h>
#include <gavl/state.h>
#include <gavl/keycodes.h>
#include <gavl/msg.h>


#define FLAG_MAPPED        (1<<0)
#define FLAG_CURSOR_HIDDEN (1<<1)
#define FLAG_CONFIGURED    (1<<2)
#define FLAG_CLOSED        (1<<3)

typedef struct
  {
  struct wl_display *display;
  struct wl_compositor *compositor;
  //  struct wl_shell *shell;

  /* Event handling */
  struct wl_seat *seat;
  struct wl_pointer *pointer;
  struct wl_keyboard *keyboard;

  /* Cursor configuration */
  struct wl_shm * shm;
  struct wl_cursor_theme *cursor_theme;
  struct wl_surface *cursor_surface;
  /* */
  
  struct wl_surface *surface;
  struct wl_egl_window *egl_window;

  
  
  /* Window management stuff */
  struct xdg_wm_base *xdg_wm_base;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *xdg_toplevel;
  struct zxdg_decoration_manager_v1* decoration_manager;
  struct zxdg_toplevel_decoration_v1 *decoration;
  
  /* xkb stuff */
  struct xkb_context *xkb_context;
  struct xkb_keymap *xkb_keymap;
  struct xkb_state *xkb_state; 
  
  bg_glvideo_t * g;
  gavl_video_sink_t * sink;
  int flags;

  bg_controllable_t ctrl;
  gavl_dictionary_t state;
  
  gavl_time_t last_active_time;
  gavl_timer_t * timer;

  int gavl_state;
  int mouse_x;
  int mouse_y;
  int width;
  int height;

  char * title;
  
  } wayland_t;

// Linux button codes (aus linux/input.h)
#define BTN_LEFT    0x110
#define BTN_RIGHT   0x111
#define BTN_MIDDLE  0x112
#define BTN_SIDE    0x113
#define BTN_EXTRA   0x114


static void handle_events_wayland(void * priv);

static void handle_events_internal(wayland_t * wayland,
                                   int timeout);

static int keysym_to_keycode(int keysym);


static void show_cursor(wayland_t * wayland, int force)
  {
  struct wl_cursor *cursor;
  struct wl_cursor_image *image;
  struct wl_buffer *buffer;
  
  if(!(wayland->flags & FLAG_CURSOR_HIDDEN) && !force)
    return;

  cursor = wl_cursor_theme_get_cursor(wayland->cursor_theme, "left_ptr");

  if(!cursor || cursor->image_count < 1)
    {
    return;
    }

  image = cursor->images[0];
  buffer = wl_cursor_image_get_buffer(image);

  wl_surface_attach(wayland->cursor_surface, buffer, 0, 0);
  wl_surface_damage(wayland->cursor_surface, 0, 0, image->width, image->height);
  wl_surface_commit(wayland->cursor_surface);
  
  wl_pointer_set_cursor(wayland->pointer, 0, wayland->cursor_surface, 
                        image->hotspot_x, image->hotspot_y);
  
  wayland->flags &= ~FLAG_CURSOR_HIDDEN;
  }

static void hide_cursor(wayland_t * wayland)
  {
  if(wayland->flags & FLAG_CURSOR_HIDDEN)
    return;

  if(wayland->pointer)
    wl_pointer_set_cursor(wayland->pointer, 0, NULL, 0, 0);
  wayland->flags |= FLAG_CURSOR_HIDDEN;
  }

static void registry_handler(void *data, struct wl_registry *registry,
                             uint32_t id, const char *interface, uint32_t version)
  {
  wayland_t * wayland = data;

  //  fprintf(stderr, "registry_handler %s\n", interface);
  
  if(!strcmp(interface, "wl_compositor"))
    {
    wayland->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
  else if(!strcmp(interface, "wl_seat"))
    {
    wayland->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
    }
  else if(!strcmp(interface, "xdg_wm_base"))
    {
    wayland->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
    }
  else if(!strcmp(interface, "zxdg_decoration_manager_v1"))
    {
    wayland->decoration_manager = wl_registry_bind(registry, id, &zxdg_decoration_manager_v1_interface, 1);
    }
  else if(!strcmp(interface, wl_shm_interface.name))
    {
    wayland->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
  
  }

static void registry_remover(void *data, struct wl_registry *registry, uint32_t id)
  {
  fprintf(stderr, "registry_remover\n");
  
  }

static const struct wl_registry_listener registry_listener =
  {
    registry_handler,
    registry_remover
  };

/* xdg_wm_base_listener */

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                             uint32_t serial)
  {
  //  fprintf(stderr, "xdg_wm_base_ping\n");
  xdg_wm_base_pong(xdg_wm_base, serial);
  }

static const struct xdg_wm_base_listener xdg_wm_base_listener =
  {
    xdg_wm_base_ping,
  };

/* xdg_surface */

static void xdg_surface_configure(void *data,
                                  struct xdg_surface *xdg_surface,
                                  uint32_t serial)
  {
  wayland_t * wayland = data;
  //  fprintf(stderr, "xdg_surface_configure\n");

  xdg_surface_ack_configure(xdg_surface, serial);

  wayland->flags |= FLAG_CONFIGURED;
  
  }

static const struct xdg_surface_listener xdg_surface_listener =
  {
    xdg_surface_configure,
  };

/* xdg_toplevel */

static void
xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                       int32_t w, int32_t h, struct wl_array *states)
  {
  wayland_t * wayland = data;
  struct wl_region *region = wl_compositor_create_region(wayland->compositor);
  //  fprintf(stderr, "xdg_toplevel_configure %d %d\n", w, h);

  if(!w && !h)
    return;

  if(!wayland->g)
    return;
  
  bg_glvideo_set_window_size(wayland->g, w, h);

  wl_region_add(region, 0, 0, w, h);
  wl_surface_set_opaque_region(wayland->surface, region);
  
  
  wl_egl_window_resize(wayland->egl_window, w, h, 0, 0);
  wl_surface_commit(wayland->surface);
  wl_region_destroy(region);
  }

static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel)
  {
  fprintf(stderr, "xdg_toplevel_close\n");
  }

static const struct xdg_toplevel_listener xdg_toplevel_listener =
  {
    xdg_toplevel_configure,
    xdg_toplevel_close,
  };

/* Pointer callbacks */

static void pointer_enter(void *data, struct wl_pointer *pointer,
                         uint32_t serial, struct wl_surface *surface,
                         wl_fixed_t surface_x, wl_fixed_t surface_y)
  {
  //  fprintf(stderr, "pointer_enter\n");
  show_cursor(data, 1);
  }

static void pointer_leave(void *data, struct wl_pointer *pointer,
                         uint32_t serial, struct wl_surface *surface)
  {
  //  fprintf(stderr, "pointer_leave\n");
  
  }

static void pointer_motion(void *data, struct wl_pointer *pointer,
                          uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
  {
  wayland_t * wayland = data;
  //  fprintf(stderr, "pointer_motion\n");

  show_cursor(wayland, 0);
  wayland->last_active_time = gavl_timer_get(wayland->timer);
  
  wayland->mouse_x = wl_fixed_to_int(surface_x);
  wayland->mouse_y = wl_fixed_to_int(surface_y);

  

  }



static void pointer_button(void *data, struct wl_pointer *pointer,
                          uint32_t serial, uint32_t time, uint32_t button,
                          uint32_t state)
  {
  int flag = 0;
  wayland_t * wayland = data;

  //  fprintf(stderr, "Button %d\n", button);

  switch(button)
    {
    case BTN_LEFT:
      flag = GAVL_KEY_BUTTON1_MASK;
      break;
    case BTN_MIDDLE:
      flag = GAVL_KEY_BUTTON2_MASK;
      break;
    case BTN_RIGHT:
      flag = GAVL_KEY_BUTTON3_MASK;
      break;
    default:
      return;
    }
  
  if(state == WL_POINTER_BUTTON_STATE_PRESSED)
    wayland->gavl_state |= flag;
  else if(state == WL_POINTER_BUTTON_STATE_RELEASED)
    wayland->gavl_state &= ~flag;
  
  }

/* Scrolling */
static void pointer_axis(void *data, struct wl_pointer *pointer,
                        uint32_t time, uint32_t axis, wl_fixed_t value)
  {
  
  }

static void pointer_frame(void *data, struct wl_pointer *pointer)
  {

  }

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
                               uint32_t axis_source)
  {

  }

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
                             uint32_t time, uint32_t axis)
  {

  }

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
                                 uint32_t axis, int32_t discrete)
  {

  }

static const struct wl_pointer_listener pointer_listener =
  {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
  };

/* Keyboard */

// Callback für Keyboard-Keymap (wird vom Compositor gesendet)
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                            uint32_t format, int fd, uint32_t size)
  {
  wayland_t * kb = data;
  char *keymap_string;
  
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
    {
    close(fd);
    return;
    }
  
  // Keymap aus dem vom Compositor gesendeten File Descriptor laden
  keymap_string = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  if (keymap_string == MAP_FAILED)
    {
    close(fd);
    return;
    }
  
  // XKB-Keymap aus dem String erstellen
  kb->xkb_keymap = xkb_keymap_new_from_string(kb->xkb_context, keymap_string,
                                              XKB_KEYMAP_FORMAT_TEXT_V1,
                                              XKB_KEYMAP_COMPILE_NO_FLAGS);
  
  munmap(keymap_string, size);
  close(fd);
  
  if (!kb->xkb_keymap)
    {
    fprintf(stderr, "Failed to compile keymap\n");
    return;
    }
  
  // XKB-State für diese Keymap erstellen
  if (kb->xkb_state)
    xkb_state_unref(kb->xkb_state);
  
  kb->xkb_state = xkb_state_new(kb->xkb_keymap);
  }

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                        uint32_t serial, uint32_t time, uint32_t key,
                        uint32_t state)
  {
  xkb_keysym_t keysym;
  int keycode;
  
  wayland_t *wayland = data;
  
  if(!wayland->xkb_state)
    return;

  if(state != WL_KEYBOARD_KEY_STATE_PRESSED)
    return; // Ignored for now

  keysym = xkb_state_key_get_one_sym(wayland->xkb_state, key + 8);
  keycode = keysym_to_keycode(keysym);

  if(keycode != GAVL_KEY_NONE)
    {
    double pos[2];
    gavl_msg_t * msg = bg_msg_sink_get(wayland->ctrl.evt_sink);
    
    bg_glvideo_window_coords_to_position(wayland->g, wayland->mouse_x, wayland->mouse_y, pos);
    
    gavl_msg_set_gui_key_press(msg, keycode, wayland->gavl_state,
                               wayland->mouse_x, wayland->mouse_y, pos);

    bg_msg_sink_put(wayland->ctrl.evt_sink);
    }
  
  }

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                              uint32_t serial, uint32_t mods_depressed,
                              uint32_t mods_latched, uint32_t mods_locked,
                              uint32_t group)
  {
  wayland_t * wayland = data;

  if(!wayland->xkb_state)
    return;

  xkb_state_update_mask(wayland->xkb_state, mods_depressed, mods_latched,
                        mods_locked, 0, 0, group);

  
  wayland->gavl_state &= ~(GAVL_KEY_SHIFT_MASK |
                           GAVL_KEY_CONTROL_MASK |
                           GAVL_KEY_ALT_MASK |
                           GAVL_KEY_SUPER_MASK);

  if(xkb_state_mod_name_is_active(wayland->xkb_state, "Shift", XKB_STATE_MODS_EFFECTIVE))
    wayland->gavl_state |= GAVL_KEY_SHIFT_MASK;
  
  if(xkb_state_mod_name_is_active(wayland->xkb_state, "Control", XKB_STATE_MODS_EFFECTIVE))
    wayland->gavl_state |= GAVL_KEY_CONTROL_MASK;
  
  if(xkb_state_mod_name_is_active(wayland->xkb_state, "Mod1", XKB_STATE_MODS_EFFECTIVE) ||
     xkb_state_mod_name_is_active(wayland->xkb_state, "Alt", XKB_STATE_MODS_EFFECTIVE))
    wayland->gavl_state |= GAVL_KEY_ALT_MASK;
  
  if(xkb_state_mod_name_is_active(wayland->xkb_state, "Super", XKB_STATE_MODS_EFFECTIVE) ||
     xkb_state_mod_name_is_active(wayland->xkb_state, "Mod4", XKB_STATE_MODS_EFFECTIVE))
    wayland->gavl_state |= GAVL_KEY_SUPER_MASK;
  
  }

// Dummy-Callbacks
static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, struct wl_surface *surface,
                          struct wl_array *keys)
  {

  }

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, struct wl_surface *surface)
  {

  }

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
                                int32_t rate, int32_t delay)
  {
  
  }

static const struct wl_keyboard_listener keyboard_listener =
  {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
  };

/* Decoration listener */

static void toplevel_decoration_configure(void *data, 
                                          struct zxdg_toplevel_decoration_v1 *decoration,
                                          uint32_t mode)
  {
  switch (mode)
    {
    case ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE:
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "No server side decorations supported");
      //      current_decoration_mode = DECORATION_MODE_CLIENT_SIDE;
      break;
    case ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Server side decorations supported");
      //      current_decoration_mode = DECORATION_MODE_SERVER_SIDE;
      break;
    }
  }

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener =
  {
    .configure = toplevel_decoration_configure,
  };

static int map_window(wayland_t * wayland)
  {
  if(wayland->surface)
    return 1;
  
  if(!(wayland->surface = wl_compositor_create_surface(wayland->compositor)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Surface creation failed");
    return 0;
    }
  
  if(!(wayland->egl_window = wl_egl_window_create(wayland->surface, 800, 600)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "EGL window creation failed");
    return 0;
    }

  wayland->g = bg_glvideo_create(EGL_PLATFORM_WAYLAND_KHR,
                                 wayland->display, wayland->egl_window);
  
  if(!(wayland->xdg_surface =
       xdg_wm_base_get_xdg_surface(wayland->xdg_wm_base, wayland->surface)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "XDG surface creation failed");
    return 0;
    }
  xdg_surface_add_listener(wayland->xdg_surface, &xdg_surface_listener, wayland);

  
  if(!(wayland->xdg_toplevel = xdg_surface_get_toplevel(wayland->xdg_surface)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Toplevel creation failed");
    return 0;
    }
  xdg_toplevel_add_listener(wayland->xdg_toplevel, &xdg_toplevel_listener, wayland);

  if(wayland->title)
    {
    xdg_toplevel_set_title(wayland->xdg_toplevel, wayland->title);
    }

  if(wayland->decoration_manager)
    {
    wayland->decoration =
      zxdg_decoration_manager_v1_get_toplevel_decoration(wayland->decoration_manager, wayland->xdg_toplevel);

    zxdg_toplevel_decoration_v1_add_listener(wayland->decoration, &decoration_listener, NULL);
    
    }
  
  wl_surface_commit(wayland->surface);
  wl_display_roundtrip(wayland->display);

  while(!(wayland->flags & (FLAG_CLOSED|FLAG_CONFIGURED)))
    handle_events_internal(wayland, 100);
  
  return 1;
  }

static void unmap_window(wayland_t * wayland)
  {
  if(wayland->decoration)
    {
    zxdg_toplevel_decoration_v1_destroy(wayland->decoration);
    wayland->decoration = NULL;
    }
  
  if(wayland->xdg_toplevel)
    {
    xdg_toplevel_destroy(wayland->xdg_toplevel);
    wayland->xdg_toplevel = NULL;
    }

  if(wayland->xdg_surface)
    {
    xdg_surface_destroy(wayland->xdg_surface);
    wayland->xdg_surface = NULL;
    }

  if(wayland->surface)
    {
    wl_surface_destroy(wayland->surface);
    wayland->surface = NULL;
    }
  
  if(wayland->g)
    {
    bg_glvideo_destroy(wayland->g);
    wayland->g = NULL;
    }
  
  if(wayland->egl_window)
    {
    wl_egl_window_destroy(wayland->egl_window);
    wayland->egl_window = NULL;
    }
  
  }
  
static int ensure_window(void * priv)
  {
  wayland_t * wayland = priv;
  struct wl_registry *registry;

  if(!wayland->display)
    {
    wayland->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  
    if(!wayland->xkb_context)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Failed to create XKB context");
      return 0;
      }
  
    if(!(wayland->display = wl_display_connect(NULL)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't connect to wayland");
      return 0;
      }

    registry = wl_display_get_registry(wayland->display);
    wl_registry_add_listener(registry, &registry_listener, wayland);

    wl_display_roundtrip(wayland->display);

    if(!wayland->compositor)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No compositor found");
      return 0;
      }

    if(!wayland->seat)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No seat found");
      return 0;
      }

    if(!(wayland->xdg_wm_base))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No XDG window manager found");
      return 0;
      }

    if(!(wayland->shm))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No SHM found");
      return 0;
      }

    if(!(wayland->cursor_surface = wl_compositor_create_surface(wayland->compositor)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cursor surface creation failed");
      return 0;
      }

    wayland->cursor_theme = wl_cursor_theme_load(NULL, 24, wayland->shm);
    
    xdg_wm_base_add_listener(wayland->xdg_wm_base, &xdg_wm_base_listener, wayland);

    wayland->pointer = wl_seat_get_pointer(wayland->seat);
    wl_pointer_add_listener(wayland->pointer, &pointer_listener, wayland);
  
    wayland->keyboard = wl_seat_get_keyboard(wayland->seat);
    wl_keyboard_add_listener(wayland->keyboard, &keyboard_listener, wayland);
    }
  
  map_window(wayland);
    
  
  //  wl_egl_window_resize(wayland->egl_window, 320, 240, 0, 0);
  
  //  xdg_toplevel_set_min_size(wayland->xdg_toplevel, 320, 240);
  //  xdg_toplevel_set_max_size(wayland->xdg_toplevel, 0, 0);
  //  wl_surface_commit(wayland->surface);
  return 1;
  }



#if 0
static void map_sync(wayland_t * wayland)
  {
  fprintf(stderr, "map_sync...\n");
#if 0  
  wl_surface_commit(wayland->surface); 
  
  while(!(wayland->flags & FLAG_MAPPED))
    {
    handle_events_internal(wayland, 100);
    }
#endif
  fprintf(stderr, "map_sync done\n");
  }
#endif

static int handle_cmd(void * priv, gavl_msg_t * cmd)
  {
  wayland_t * wayland  = priv;

  if(wayland->g && bg_glvideo_handle_message(wayland->g, cmd))
    return 1;
  
  switch(cmd->NS)
    {
    case GAVL_MSG_NS_SINK:
      switch(cmd->ID)
        {
        case GAVL_MSG_SINK_RESYNC:
          if(wayland->g)
            bg_glvideo_resync(wayland->g);
          break;
        }
      break;
    case GAVL_MSG_NS_STATE:
      {
      switch(cmd->ID)
        {
        case GAVL_CMD_SET_STATE:
          {
          int last = 0;
          const char * ctx = NULL;
          const char * var = NULL;
          gavl_value_t val;
          gavl_value_init(&val);

          gavl_msg_get_state(cmd,
                             &last,
                             &ctx, &var, &val, NULL);

          
          if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            if(!strcmp(var, BG_STATE_OV_FULLSCREEN))
              {
              int fs = 0;
              if(!gavl_value_get_int(&val, &fs) ||
                 !wayland->display  || !wayland->xdg_toplevel)
                return 1;

              /*
               *  val = 1: Set fullscreen
               *  val = 0: Unset fullscreen
               */

              if(val.v.i)
                {
                //                fprintf(stderr, "Set fullscreen\n");
                xdg_toplevel_set_fullscreen(wayland->xdg_toplevel, NULL);
                }
              else
                {
                //                fprintf(stderr, "Unset fullscreen\n");
                xdg_toplevel_unset_fullscreen(wayland->xdg_toplevel);
                }
              }
            else if(!strcmp(var, BG_STATE_OV_TITLE))
              {
              const char * str;

              if(!(str = gavl_value_get_string(&val)))
                return 1;
              
              if(wayland->xdg_toplevel)
                xdg_toplevel_set_title(wayland->xdg_toplevel, str);

              wayland->title = gavl_strrep(wayland->title, str);
              }
            else if(!strcmp(var, BG_STATE_OV_VISIBLE))
              {
              // TODO
                
              int visible = 0;

              if(!wayland->display)
                return 1;

              gavl_value_get_int(&val, &visible);
              
              if((wayland->flags & FLAG_MAPPED))
                {
                if(!visible)
                  {
                  unmap_window(wayland);
                  handle_events_wayland(wayland);
                  wayland->flags &= ~FLAG_MAPPED;
                  }
                }
              else
                {
                if(visible)
                  {
                  map_window(wayland);
                  wayland->flags |= FLAG_MAPPED;
                  }
                }
              }
            else
              {
#if 0
              fprintf(stderr, "X11 set state %s %s\n", ctx, var);
              gavl_value_dump(&val, 2);
              fprintf(stderr, "\n");
#endif
              }
            }
          gavl_value_free(&val);
          }
          break;
        }
      }
    }
  return 1;
  }

static void * create_wayland()
  {
  wayland_t * wayland = calloc(1, sizeof(*wayland));

  bg_controllable_init(&wayland->ctrl,
                       bg_msg_sink_create(handle_cmd, wayland, 1),
                       bg_msg_hub_create(1));

  wayland->timer = gavl_timer_create();
  gavl_timer_start(wayland->timer);
  
  return wayland;
  }

static void destroy_wayland(void * priv)
  {
  wayland_t * wayland = priv;
  
  if(wayland->g)
    bg_glvideo_destroy(wayland->g);

  if(wayland->cursor_theme)
    wl_cursor_theme_destroy(wayland->cursor_theme);

  if(wayland->pointer)
    wl_pointer_destroy(wayland->pointer);
  
  if(wayland->seat)
    wl_seat_destroy(wayland->seat);
  
  // XDG Shell cleanup
  if (wayland->xdg_toplevel)
    {
    xdg_toplevel_destroy(wayland->xdg_toplevel);
    }
    
  if(wayland->xdg_surface)
    {
    xdg_surface_destroy(wayland->xdg_surface);
    }
    
  if(wayland->xdg_wm_base)
    {
    xdg_wm_base_destroy(wayland->xdg_wm_base);
    }

  if(wayland->egl_window)
    {
    wl_egl_window_destroy(wayland->egl_window);
    }

  if(wayland->shm)
    {
    wl_shm_destroy(wayland->shm);
    }

  if(wayland->surface)
    {
    wl_surface_destroy(wayland->surface);
    }
  if(wayland->cursor_surface)
    {
    wl_surface_destroy(wayland->cursor_surface);
    }
    
  if(wayland->compositor)
    {
    wl_compositor_destroy(wayland->compositor);
    }
    
  if(wayland->display)
    {
    wl_display_disconnect(wayland->display);
    }
  
    
  bg_controllable_cleanup(&wayland->ctrl);
  
  free(wayland);
  
  }

static gavl_hw_context_t * get_hw_context_wayland(void * priv)
  {
  wayland_t * wayland = priv;
  if(!ensure_window(wayland))
    return NULL;
  return bg_glvideo_get_hwctx(wayland->g);
  }


static gavl_video_sink_t *
add_overlay_stream_wayland(void * priv, gavl_video_format_t * format)
  {
  wayland_t * wayland = priv;
  return bg_glvideo_add_overlay_stream(wayland->g, format, 0);
  }

static gavl_video_sink_t * get_sink_wayland(void * priv)
  {
  wayland_t * wayland= priv;
  return wayland->sink;
  }

/* Key mappings */

static const struct
  {
  int xkb;
  int gavl;
  }
keysyms[] = 
  {
    { XKB_KEY_0, GAVL_KEY_0 },
    { XKB_KEY_1, GAVL_KEY_1 },
    { XKB_KEY_2, GAVL_KEY_2 },
    { XKB_KEY_3, GAVL_KEY_3 },
    { XKB_KEY_4, GAVL_KEY_4 },
    { XKB_KEY_5, GAVL_KEY_5 },
    { XKB_KEY_6, GAVL_KEY_6 },
    { XKB_KEY_7, GAVL_KEY_7 },
    { XKB_KEY_8, GAVL_KEY_8 },
    { XKB_KEY_9, GAVL_KEY_9 },
    { XKB_KEY_space,  GAVL_KEY_SPACE },
    { XKB_KEY_Return, GAVL_KEY_RETURN },
    { XKB_KEY_Left,   GAVL_KEY_LEFT },
    { XKB_KEY_Right,  GAVL_KEY_RIGHT },
    { XKB_KEY_Up,     GAVL_KEY_UP },
    { XKB_KEY_Down,   GAVL_KEY_DOWN },
    { XKB_KEY_Page_Up,  GAVL_KEY_PAGE_UP },
    { XKB_KEY_Page_Down, GAVL_KEY_PAGE_DOWN },
    { XKB_KEY_Home,  GAVL_KEY_HOME },
    { XKB_KEY_plus,  GAVL_KEY_PLUS },
    { XKB_KEY_minus, GAVL_KEY_MINUS },
    { XKB_KEY_Tab,   GAVL_KEY_TAB },
    { XKB_KEY_Escape,   GAVL_KEY_ESCAPE },
    { XKB_KEY_Menu,     GAVL_KEY_MENU },
    
    { XKB_KEY_question,   GAVL_KEY_QUESTION }, //!< ?
    { XKB_KEY_exclam,     GAVL_KEY_EXCLAM    }, //!< !
    { XKB_KEY_quotedbl,   GAVL_KEY_QUOTEDBL,   }, //!< "
    { XKB_KEY_dollar,     GAVL_KEY_DOLLAR,     }, //!< $
    { XKB_KEY_percent,    GAVL_KEY_PERCENT,    }, //!< %
    { XKB_KEY_ampersand,  GAVL_KEY_APMERSAND,  }, //!< &
    { XKB_KEY_slash,      GAVL_KEY_SLASH,      }, //!< /
    { XKB_KEY_parenleft,  GAVL_KEY_LEFTPAREN,  }, //!< (
    { XKB_KEY_parenright, GAVL_KEY_RIGHTPAREN, }, //!< )
    { XKB_KEY_equal,      GAVL_KEY_EQUAL,      }, //!< =
    { XKB_KEY_backslash,  GAVL_KEY_BACKSLASH,  }, //!< :-)
    { XKB_KEY_BackSpace,  GAVL_KEY_BACKSPACE,  }, //!< :-)
    { XKB_KEY_a,             GAVL_KEY_a },
    { XKB_KEY_b,             GAVL_KEY_b },
    { XKB_KEY_c,             GAVL_KEY_c },
    { XKB_KEY_d,             GAVL_KEY_d },
    { XKB_KEY_e,             GAVL_KEY_e },
    { XKB_KEY_f,             GAVL_KEY_f },
    { XKB_KEY_g,             GAVL_KEY_g },
    { XKB_KEY_h,             GAVL_KEY_h },
    { XKB_KEY_i,             GAVL_KEY_i },
    { XKB_KEY_j,             GAVL_KEY_j },
    { XKB_KEY_k,             GAVL_KEY_k },
    { XKB_KEY_l,             GAVL_KEY_l },
    { XKB_KEY_m,             GAVL_KEY_m },
    { XKB_KEY_n,             GAVL_KEY_n },
    { XKB_KEY_o,             GAVL_KEY_o },
    { XKB_KEY_p,             GAVL_KEY_p },
    { XKB_KEY_q,             GAVL_KEY_q },
    { XKB_KEY_r,             GAVL_KEY_r },
    { XKB_KEY_s,             GAVL_KEY_s },
    { XKB_KEY_t,             GAVL_KEY_t },
    { XKB_KEY_u,             GAVL_KEY_u },
    { XKB_KEY_v,             GAVL_KEY_v },
    { XKB_KEY_w,             GAVL_KEY_w },
    { XKB_KEY_x,             GAVL_KEY_x },
    { XKB_KEY_y,             GAVL_KEY_y },
    { XKB_KEY_z,             GAVL_KEY_z },

    { XKB_KEY_A,             GAVL_KEY_A },
    { XKB_KEY_B,             GAVL_KEY_B },
    { XKB_KEY_C,             GAVL_KEY_C },
    { XKB_KEY_D,             GAVL_KEY_D },
    { XKB_KEY_E,             GAVL_KEY_E },
    { XKB_KEY_F,             GAVL_KEY_F },
    { XKB_KEY_G,             GAVL_KEY_G },
    { XKB_KEY_H,             GAVL_KEY_H },
    { XKB_KEY_I,             GAVL_KEY_I },
    { XKB_KEY_J,             GAVL_KEY_J },
    { XKB_KEY_K,             GAVL_KEY_K },
    { XKB_KEY_L,             GAVL_KEY_L },
    { XKB_KEY_M,             GAVL_KEY_M },
    { XKB_KEY_N,             GAVL_KEY_N },
    { XKB_KEY_O,             GAVL_KEY_O },
    { XKB_KEY_P,             GAVL_KEY_P },
    { XKB_KEY_Q,             GAVL_KEY_Q },
    { XKB_KEY_R,             GAVL_KEY_R },
    { XKB_KEY_S,             GAVL_KEY_S },
    { XKB_KEY_T,             GAVL_KEY_T },
    { XKB_KEY_U,             GAVL_KEY_U },
    { XKB_KEY_V,             GAVL_KEY_V },
    { XKB_KEY_W,             GAVL_KEY_W },
    { XKB_KEY_X,             GAVL_KEY_X },
    { XKB_KEY_Y,             GAVL_KEY_Y },
    { XKB_KEY_Z,             GAVL_KEY_Z },
    { XKB_KEY_F1,            GAVL_KEY_F1 },
    { XKB_KEY_F2,            GAVL_KEY_F2 },
    { XKB_KEY_F3,            GAVL_KEY_F3 },
    { XKB_KEY_F4,            GAVL_KEY_F4 },
    { XKB_KEY_F5,            GAVL_KEY_F5 },
    { XKB_KEY_F6,            GAVL_KEY_F6 },
    { XKB_KEY_F7,            GAVL_KEY_F7 },
    { XKB_KEY_F8,            GAVL_KEY_F8 },
    { XKB_KEY_F9,            GAVL_KEY_F9 },
    { XKB_KEY_F10,           GAVL_KEY_F10 },
    { XKB_KEY_F11,           GAVL_KEY_F11 },
    { XKB_KEY_F12,           GAVL_KEY_F12 },
    { XKB_KEY_XF86AudioMute, GAVL_KEY_MUTE         }, // = XF86AudioMute
    { XKB_KEY_XF86AudioLowerVolume, GAVL_KEY_VOLUME_MINUS }, // = XF86AudioLowerVolume
    { XKB_KEY_XF86AudioRaiseVolume, GAVL_KEY_VOLUME_PLUS  }, // = XF86AudioRaiseVolume
    { XKB_KEY_XF86AudioNext,        GAVL_KEY_NEXT         }, // = XF86AudioNext
    { XKB_KEY_XF86AudioPlay,        GAVL_KEY_PLAY         }, // = XF86AudioPlay
    { XKB_KEY_XF86AudioPrev,        GAVL_KEY_PREV         }, // = XF86AudioPrev
    { XKB_KEY_XF86AudioStop,        GAVL_KEY_STOP         }, // = XF86AudioStop
    { XKB_KEY_XF86AudioPause,       GAVL_KEY_PAUSE        }, // = XF86AudioPause
    { XKB_KEY_XF86AudioRewind,      GAVL_KEY_REWIND       }, // = XF86AudioRewind
    { XKB_KEY_XF86AudioForward,     GAVL_KEY_FORWARD      }, // = F86AudioForward

    
    { }
    
  };

static int keysym_to_keycode(int keysym)
  {
  int i;

  for(i = 0; i < sizeof(keysyms)/sizeof(keysyms[0]); i++)
    {
    if(keysym == keysyms[i].xkb)
      return keysyms[i].gavl;
    }
  return GAVL_KEY_NONE;
  }

#if 0
static void handle_events_x11(void * priv)
  {
  XEvent evt;
  x11_t * x11 = priv;
  gavl_time_t current_time;
  
  /* Check whether to hide the cursor */
  current_time = gavl_timer_get(x11->timer);

  while(XPending(x11->dpy))
    {
    XNextEvent(x11->dpy, &evt);

    switch(evt.type)
      {
      case KeyPress:
        {
        int gavl_code;
        int gavl_mask;

        KeySym keysym;

        evt.xkey.state &= STATE_IGNORE;
        
        keysym = XLookupKeysym(&evt.xkey, 0);
        
        if((gavl_code = keysym_to_key_code(keysym)) == GAVL_KEY_NONE)
          gavl_code = x11_keycode_to_bg(evt.xkey.keycode);

        if(gavl_code != GAVL_KEY_NONE)
          {
          double pos[2];
          gavl_msg_t * msg = bg_msg_sink_get(x11->ctrl.evt_sink);

          gavl_mask = x11_to_key_mask(evt.xkey.state);

          bg_glvideo_window_coords_to_position(x11->g, evt.xkey.x, evt.xkey.y, pos);
          
          gavl_msg_set_gui_key_press(msg, gavl_code, gavl_mask,
                                     evt.xkey.x, evt.xkey.y, pos);
          bg_msg_sink_put(x11->ctrl.evt_sink);
          }
        
        show_cursor(x11);
        x11->last_active_time = current_time;
        }
        break;
      case ConfigureNotify:
        {
        //        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Window size changed %d %d", evt.xconfigure.width, evt.xconfigure.height);
  
        if(x11->g)
          bg_glvideo_set_window_size(x11->g, evt.xconfigure.width, evt.xconfigure.height);
        }
      case MotionNotify:
        show_cursor(x11);
        x11->last_active_time = current_time;
        break;
      }
    
    }

  if(current_time - x11->last_active_time > 3 * GAVL_TIME_SCALE)
    hide_cursor(x11);
  
  }
#endif

static void handle_events_internal(wayland_t * wayland,
                                   int timeout)
  {
  while(wl_display_prepare_read(wayland->display) != 0)
    wl_display_dispatch_pending(wayland->display);

  wl_display_flush(wayland->display);

  if((timeout >= 0) && !gavl_fd_can_read(wl_display_get_fd(wayland->display), timeout))
    {
    wl_display_cancel_read(wayland->display);
    return;
    }

  wl_display_read_events(wayland->display);
  wl_display_dispatch_pending(wayland->display);
  
  }

static void handle_events_wayland(void * priv)
  {
  wayland_t * wayland = priv;
  
  handle_events_internal(wayland, 0);
  
  if(gavl_timer_get(wayland->timer) -
     wayland->last_active_time > 3 * GAVL_TIME_SCALE)
    hide_cursor(wayland);
  
  }



static int open_wayland(void * priv, const char * uri,
                    gavl_video_format_t * format,
                    int src_flags)
  {
  wayland_t * wayland = priv;

  if(!ensure_window(priv))
    return 0;

  //  map_sync(wayland);
  
  wayland->sink = 
    bg_glvideo_open(wayland->g, format, src_flags);

  show_cursor(wayland, 0);
  wayland->last_active_time = gavl_timer_get(wayland->timer);
  
  return 1;
  }


static void close_wayland(void * priv)
  {
  wayland_t * wayland = priv;
  if(wayland->g)
    bg_glvideo_close(wayland->g);
  }

static bg_controllable_t * get_controllable_wayland(void * data)
  {
  wayland_t * priv = data;
  return &priv->ctrl;
  }

static char const * const protocols = "wayland-sink";

static const char * get_protocols_wayland(void * priv)
  {
  return protocols;
  }


const bg_ov_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "ov_wayland",
      .long_name =     TRS("Wayland"),
      .description =   TRS("Wayland display driver supporting OpenGL and direct rendering to DMA buffers"),
      .type =          BG_PLUGIN_OUTPUT_VIDEO,
      .flags =         BG_PLUGIN_OV_STILL,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_wayland,
      .destroy =       destroy_wayland,

      //      .get_parameters   = get_parameters_wayland,
      //      .set_parameter    = set_parameter_wayland,
      .get_controllable = get_controllable_wayland,
      .get_protocols = get_protocols_wayland,
    },

    .get_hw_context     = get_hw_context_wayland,
    .open               = open_wayland,
    .get_sink           = get_sink_wayland,
    
    .add_overlay_stream = add_overlay_stream_wayland,

    .handle_events      = handle_events_wayland,
    .close              = close_wayland,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
