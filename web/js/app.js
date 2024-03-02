/* Widgets */

var widgets = null;
var current_widget = null;

/* Event hub for key events */
var key_event_hub = null;


/*
 * Application state
 */

var app_state = null;
var last_app_state = null;

var player = null;
var player_uri = null;


/* Current menu (or popup) */
var current_menu = null;

var player_control = null;

var nav_popup = null;
var search_popup = null;
var help_screen = null;

var devices   = null;

var server_connection = null;
var server_uri = null;

var queue_idx = 0;
var queue_len = 0;

/* Currently playing container and track */

var current_track_id  = null;

var has_video = false;

/* Container for the current playlist. It is inserted on message level into
   the browser so we don't need special handling by the frontend */

var playqueue          = null;

var playqueue_children   = null;

var local_items = 0;

/* Array */
var root_elements = null;
var root_container = null;
var have_root = false;

const BROWSER_MODE_LIST = "list";
const BROWSER_MODE_TILES = "tiles";
  
function playqueue_create()
  {
  var m = new Object();
    
  root_elements[0] = new Object();
  root_elements[0].t = GAVL_TYPE_DICTIONARY;
  root_elements[0].v = new Object();
    
  playqueue = root_elements[0].v;
  playqueue_children = new Array();
    
  dict_set_string(m, GAVL_META_LABEL, "No player configured");
  dict_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_ROOT_PLAYQUEUE);
  dict_set_string(m, GAVL_META_ID, BG_PLAYQUEUE_ID);
  
  dict_set_dictionary(playqueue, GAVL_META_METADATA, m);
   
  }

function playqueue_init()
  {
  /* If this is the current track (after the page reload) initialize browser */
  if((app_state.widget == "browser") &&
     (app_state.id == BG_PLAYQUEUE_ID))
    {
    widgets.browser.set_container(playqueue);
    /* set_container will call BG_FUNC_DB_BROWSE_CHILDREN */
    }

  /* Browse object */
  let msg = msg_create(BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);
  player.handle_command(msg);

  }


function playqueue_cleanup()
  {
//  console.log("playqueue_cleanup");
//  console.trace();
  playqueue = null;  

  if(widgets.browser.container &&
     (obj_get_id(widgets.browser.container) == "/"))
    {
    /* Restore default playqueue */

    //    playqueue_delete();
    }

  if(widgets.browser.container &&
     (obj_get_id(widgets.browser.container) == BG_PLAYQUEUE_ID))
    {
    widgets.browser.change_up();
    }
      
  current_track_id  = null;
  }

function playqueue_is_current()
  {
    if((app_state.widget == "browser") &&
       (app_state.id == BG_PLAYQUEUE_ID))
      return true;
    else
      return false;
  }

/* Show widget. If name is omitted, show browser. */

function show_widget(name)
  {
  let push = false;
  if(!name)
    name = "browser";

  if(app_state.widget != name)
    push = true;
    
  app_state.widget = name;
  app_state_apply();
  update_hash();

  if(push)
    push_state();
   
  }

function make_device_array(arr)
  {
  var i;
    
  for(i = 0; i < arr.length; i++)
    {
//    console.log("make_device_array " + i + " " + JSON.stringify(arr[i]));

    if(dict_get_int(arr[i].v, "Type") != 2)
      continue;

    if(!arr[i].v[GAVL_META_ICON_URL])
      {
      dict_set_string(arr[i].v, GAVL_META_ICON_URL, "icon-player");
      }
    }
          
  return arr;
  }

function clear_current()
  {
  widgets.browser.set_current(null);
  }

/*
 *  Global navigation shortcuts:
 *
 *  P:          Player
 *  B:          Browser
 *  Esc:        Hide popup, level up
 *  Left:       History back
 *  Right:      History forward
 *  Ctrl+Left:  Previous sibling
 *  Ctrl+Right: Next sibling
 *  S:          Search
 *  I:          Info
 *  N:          Toggle Navigation
 *  F1/?        Show help screen
 *  
 */


function set_fullscreen(fullscreen)
  {
  msg = msg_create(BG_CMD_SET_STATE, BG_MSG_NS_STATE);
  msg_set_arg_int(msg, 0, 1); // Last
  msg_set_arg_string(msg, 1, "ov");
  msg_set_arg_string(msg, 2, "$fullscreen");

  if(fullscreen)
    msg_set_arg_int(msg, 3, 1);
  else
    msg_set_arg_int(msg, 3, 0);
  player.handle_command(msg);
  }

function next_visualization()
  {
  var msg = msg_create(BG_PLAYER_CMD_NEXT_VISUALIZATION, BG_MSG_NS_PLAYER);
  player.handle_command(msg);
  }

function play_by_id(obj)
  {
  var msg;

  msg = msg_create(BG_PLAYER_CMD_SET_CURRENT_TRACK, BG_MSG_NS_PLAYER);
  msg_set_arg_string(msg, 0, obj_make_playqueue_id(obj));
  player.handle_command(msg);

  msg = msg_create(BG_PLAYER_CMD_PLAY, BG_MSG_NS_PLAYER);
  player.handle_command(msg);
  }

function playqueue_add_entry(obj, replace, play)
  {
  var msg;

  if(app_state.id != BG_PLAYQUEUE_ID)
    {
    msg = msg_create(BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);
    dict_set_string(msg.header, GAVL_MSG_CLIENT_ID, client_id);

    if(replace)
      {
      msg_set_arg_int(msg, 0, 0); // idx
      msg_set_arg_int(msg, 1, -1); // delete
      }
    else
      {
      msg_set_arg_int(msg, 0, -1); // idx
      msg_set_arg_int(msg, 1, -1); // delete
      }
    msg_set_arg_dictionary(msg, 2, clone_object(obj, false)); // children
    player.handle_command(msg);

    if(play)
      play_by_id(obj);
    }
  else
    {
    if(play)
      play_by_id(obj);
    }
    
  }

const TRACKS_TO_SEND = 50;

function send_add_album_message(idx, arr, replace, last)
  {
  var msg;

  msg = msg_create(BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
  dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);
  dict_set_string(msg.header, GAVL_MSG_CLIENT_ID, client_id);

  if(!idx)
    {
    if(replace)
      {
      msg_set_arg_int(msg, 0, 0); // idx
      msg_set_arg_int(msg, 1, -1); // delete
      }
    else
      {
      msg_set_arg_int(msg, 0, -1); // idx
      msg_set_arg_int(msg, 1, 0); // delete
      }
    }
  else
    {
    msg_set_arg_int(msg, 0, idx); // idx
    msg_set_arg_int(msg, 1, 0); // delete
    }
    
  msg_set_arg_array(msg, 2, arr); // children
  player.handle_command(msg);
  
  
  }
  

function playqueue_add_album(container, replace, play)
  {
  var msg;
  var children;
  var start;
  var end;
  var added = 0;
  var i;
  var klass;
  var arr = null;

  if((widgets.browser != current_widget) ||
     (app_state.id == BG_PLAYQUEUE_ID))
    return;
    
  var m = dict_get_dictionary(container, GAVL_META_METADATA);
  var num_items = dict_get_int(m, GAVL_META_NUM_ITEM_CHILDREN);
  var num_containers = dict_get_int(m, GAVL_META_NUM_CONTAINER_CHILDREN);

  if(!num_items)
    return 0;

  children = dict_get_array(container, GAVL_META_CHILDREN);

  for(i = 0; i < children.length; i++)
    {
//    console.log("Add album " + JSON.stringify(children[i]));
      
    klass = obj_get_string(children[i].v, GAVL_META_CLASS);
    if(!klass || !klass.startsWith("item"))
      continue;
    
    if(arr && (arr.length >= TRACKS_TO_SEND))
      {
      // Send previous stuff
      send_add_album_message(added, arr, replace, false);
      added += arr.length;
      arr = null;
      }
    
    if(!arr)
      arr = new Array();

    arr.push(children[i]);
    }

  if(arr)
    send_add_album_message(added, arr, replace, true);

  
  if(play && (children.length > 0))
    {
    msg = msg_create(BG_PLAYER_CMD_PLAY, BG_MSG_NS_PLAYER);
//    msg_set_arg_string(msg, 0, obj_make_playqueue_id(children[0].v));
    player.handle_command(msg);
    }
  
  }

function my_get_metadata(id)
  {
  var msg;

  if(id == "/")
    {
    msg = msg_create(BG_RESP_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
    dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, id);
    msg_set_arg_dictionary(msg, 0, root_container);
    widgets.browser.handle_msg(msg);
    return;
    }
  

  msg = msg_create(BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, id);
  dict_set_string(msg.header, GAVL_MSG_CLIENT_ID, client_id);

//  console.log("my_get_metadata " + JSON.stringify(msg));
//  console.trace();
    
  msg_send(msg, server_connection);
  }

function my_get_children(obj, start, num)
  {
  var id = obj_get_id(obj);
  var msg;

  /* Avoid us to be called recursively */    
    
  msg = msg_create(BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
  dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, id);

//  console.log("my_get_children " + id + " " + start + " " + num);
//  console.trace();
    
  if((id == BG_PLAYQUEUE_ID) && (player.ready))
    {
    if(!playqueue.have_children)
      player.handle_command(msg);
    else
      {
      let res = set_browse_children_response(msg, playqueue_children);

      console.log("set_browse_children_response " + JSON.stringify(playqueue_children));
	

      widgets.browser.handle_msg(res);
      }
    }
  else if(id == "/")
    {
    let res = set_browse_children_response(msg, root_elements);
    widgets.browser.handle_msg(res);

//    console.log("Got children: " + JSON.stringify(root_elements));
    }
  else
    {
    dict_set_string(msg.header, GAVL_MSG_CLIENT_ID, client_id);
    msg_send(msg, server_connection);
    }

  }

function server_connection_init()
  {
  server_connection = new WebSocket(server_uri, ['json']);

  server_connection.onclose = function(evt)
    {
//    var currentdate = new Date(); 
    console.log("Server websocket closed (code: " + evt.code + " :(");
/*
    alert("Server websocket closed " +
          currentdate.getHours() + ":" 
          + currentdate.getMinutes() + ":" +
          currentdate.getSeconds());
 */
    my_log("Server websocket closed (code: " + evt.code + " " + get_websocket_close_reason(evt) + ")");
    setTimeout(server_connection_init, 1000);
    };
  
  server_connection.onopen = function()
    {
    var msg;
    my_log("Server websocket open");

    /* Browse for root object */

    msg = msg_create(BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
    dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, "/");
    msg_send(msg, server_connection);

    msg = msg_create(BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
    dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, "/");
    msg_send(msg, server_connection);

    };

  server_connection.onmessage = function(evt)
    {
    var state_ctx;
    var state_var;
    var i;
    var msg = msg_parse(evt.data);
    var old_var;
	
//    console.log("Got server message " + msg.ns + " " + msg.id + " " + evt.data);

    switch(msg.ns)
      {
      case BG_MSG_NS_STATE:
        switch(msg.id)
          {
          case BG_MSG_STATE_CHANGED:
	    state_ctx = msg.args[1].v;
	    state_var = msg.args[2].v;
//	    console.log("state changed " + state_ctx + " " + state_var + " " + JSON.stringify(msg.args[3].v));
	    switch(state_ctx)
	      {
	      case "mdb":
                switch(state_var)
	          {
	          case "mimetypes":
                    old_var = mimetypes;
                    mimetypes = msg.args[3].v;
                    if(!old_var)
                      init_complete();
		    break;
	          case "renderers":
                    old_var = devices;
                    devices = make_device_array(msg.args[3].v);
                    if(!old_var)
                      init_complete();
                    else
                      {
                      var item = cfg_item_get("renderer");
                      var button  = document.getElementById("cfg-widget-renderer");
		      cfg_info_set_devices(item);
//                      button.menu = create_menu(item.menu_data);
                      set_menu_items(button.menu, item.menu_data);
		      }
		    break;
		  }
                break;
              }
            break;
          }
        break;	
      case BG_MSG_NS_DB:
        switch(msg.id)
          {
          case BG_RESP_DB_BROWSE_OBJECT:
            if((dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID) == '/') && !root_container)
	      {
              root_container = msg.args[0].v;
	      m = dict_get_dictionary(root_container, GAVL_META_METADATA);
              dict_set_int(m, GAVL_META_NUM_CHILDREN, dict_get_int(m, GAVL_META_NUM_CHILDREN)+1);
              dict_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, dict_get_int(m, GAVL_META_NUM_CONTAINER_CHILDREN)+1);
              document.title = dict_get_string(m, GAVL_META_LABEL);
              return;
	      }
	    
            if(app_state.widget == "browser")
	      {
              widgets.browser.set_container(msg.args[0].v);
	      }
	    else if(app_state.widget == "imageviewer")
	      {
              widgets.imageviewer.set_image_internal(msg.args[0].v);
	      }
            break;
          case BG_RESP_DB_BROWSE_CHILDREN:
            if((dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID) == '/') && !have_root)
	      {
              let i;

              for(i = 0; i < msg.args[2].v.length; i++)
                root_elements.splice(msg.args[0].v + i + 1, 0, msg.args[2].v[i]);

	      if(msg_is_last(msg))
                {
		have_root = true;
                init_complete();
                }
              return;
	      }
            widgets.browser.handle_msg(msg);
	    break;
          case BG_MSG_DB_OBJECT_CHANGED:
            widgets.browser.handle_msg(msg);
	    break;
          case BG_MSG_DB_SPLICE_CHILDREN:
	    /* Direct to browser */
            if(dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID) == '/')
	      {
		
	      }
            widgets.browser.handle_msg(msg);
	    break;
          }
        break;	
	
      }
      
    };

  server_connection.onerror = function(evt)
    {
    my_log("Server websocket error");
    };

      
  };


/* Track changes so we can upload our stuff to the server */


/* True if config was loaded (or loading failed) */
var have_cfg = false;
var cfg_changed = false;

var cfg = {
   renderer: null,
   style: "darkblue"
};

function hide_current_menu(evt)
  {
//  console.log("current_menu: " + current_menu);
  if(current_menu)
    {
    current_menu.hide();

    if(evt)
      stop_propagate(evt);

    current_menu = null;
    return true;
    }
  return false;
  }


/* Custom history */

var my_history = null;
var my_history_pos = 0;

function my_history_go(delta)
  {
  /* */

      
  my_history_pos += delta;
  if(my_history_pos < 0)
    my_history_pos = 0;
  else if(my_history_pos >= my_history.length)
    my_history_pos = my_history.length-1;

  app_state = app_state_copy(my_history[my_history_pos]);
  app_state_apply();
  }

function my_history_back(keep)
  {
  my_history_go(-1);

  if(!keep)
    my_history.splice(my_history_pos + 1, 1);
  }

/* Remove last entry */
function my_history_pop()
  {
  my_history.splice(my_history.length - 1, 1);
  my_history_pos--;
  }

function update_hash()
  {
  window.history.replaceState( my_history_state(0), null, app_state_to_hash(app_state));
  }

function popstate_cb(evt)
  {
  if(!evt.state)
    return;
//   console.log("popstate_cb " + JSON.stringify(evt.state) + " " + my_history_pos);

  if(evt.state.delta < 0)
    {
    if(hide_current_menu())
      return;
          
    if(my_history_pos == 0) // Let the user escape from this app using the back button
      {
      delete window.onpopstate;
      window.history.go(-2);
      return;
      }
    console.log("History Backward");
    window.history.go(1);
    my_history_go(-1);
    }
  else if(evt.state.delta > 0)
    {
    console.log("History Forward");
    window.history.go(-1);
    my_history_go(1);
    }
  else if(evt.state.delta == 0)
    {
    update_hash();
    }
    
  //  console.trace();
//  stop_propagate(evt);

/*
  else if(evt.state.delta == 0)
    window.history.replaceState(null, null, app_state_to_hash(app_state));
*/
  }

function app_state_copy(a)
  {
  var ret = new Object();
  ret.widget  = a.widget;
  ret.id      = a.id;
  ret.sel_id  = a.sel_id;
  ret.info_id = a.info_id;
  ret.image_id = a.image_id;
  return ret;
  }

function app_state_to_hash(a)
  {
  var ret = "";
  ret += "#cntid=" + a.id;
  if(a.sel_id)
    ret += ";selid=" + a.sel_id;	  
  if(a.image_id)
    ret += ";imageid=" + a.image_id;	  

  return ret;
  }

function app_state_from_hash(a, str)
  {
  var i;
  var arr = str.substring(1).split(";");

  /* Fallback */     
  a.widget = "browser";
  a.id = "/";

  a.menu = false;
  a.last = null;
  for(i = 0; i < arr.length; i++)
    {
    var arr1 = arr[i].split("=");

    switch(arr1[0])
      {
      case "cntid":
        a.id = decodeURIComponent(arr1[1]);
        break;
      case "selid":
        a.sel_id = decodeURIComponent(arr1[1]);
        break;
      case "imageid":
        a.image_id = decodeURIComponent(arr1[1]);
        break;
      }
    }
  }

function push_state()
  {
//  console.log("push_state " + my_history.length);
  
  if(!my_history)
    return;
      
  if(!my_history.length)
    my_history_pos = 0;
  else
    my_history_pos++;

  /* Remove all entries starting from here */
  my_history.splice(my_history_pos, (my_history.length - my_history_pos));
  my_history.push(app_state_copy(app_state));
  }

function replace_state()
  {
  if(!my_history)
    return;
  my_history[my_history_pos] = app_state_copy(app_state);
  }

function my_history_state(delta)
  {
  var ret = new Object();
  ret.delta = delta;
  ret.id = "Blubber 2";
  return ret;
  }

function app_state_apply()
  {
  var w;
  var div;     
  var obj;

//  console.log("app_state_apply " + JSON.stringify(app_state));
//  console.trace();

  w = widgets[app_state.widget];

  if(current_widget && (current_widget != w))
    {
    if(current_widget.hide)
      current_widget.hide();
    current_widget.div.style.display = "none";
    }

  if(current_widget != w)
    {
    current_widget = w;

    if(current_widget)
      {
      if(current_widget.show)
        current_widget.show();
      }
    }

  if(app_state.widget == "browser")
    {
    if(!widgets.browser.container ||
       (app_state.id != obj_get_id(widgets.browser.container)))
      {
      widgets.browser.set_container(app_state.id);
      }
    else if(current_widget.container && (app_state.id == obj_get_id(current_widget.container)))
      {
//      console.log("Set header");
      set_header(current_widget.container);
      adjust_header_footer(current_widget.div);
      }
      
    if(app_state.sel_id)
      {
      widgets.browser.select_entry_by_id(app_state.sel_id);
	
      if((div = widgets.browser.div_by_id(app_state.sel_id)))
        {
        widgets.browser.scroll(div);
        widgets.browser.update_footer();
        }
      }
    }
  else if(app_state.widget == "iteminfo")
    {
    current_widget.update();
    }
  else if(app_state.widget == "imageviewer")
    {
    current_widget.set_image_internal(app_state.image_id);
    }
    
  update_nav_popup();

/*    
  if((app_state.widget == "browser") &&
     (!last_app_state ||
      (last_app_state.id != app_state.id)))
    push_state();
*/
      
  last_app_state = app_state_copy(app_state);
  }


function my_history_init()
  {
  my_history = new Array();
      
  window.history.pushState(my_history_state(-1), null);
  window.history.pushState(my_history_state(0), null);
  window.history.pushState(my_history_state(1), null);
  window.history.go(-1);
  window.onpopstate = popstate_cb;

  if(window.location.hash.length)
    {
    app_state_from_hash(app_state, window.location.hash);
    }
  else
    {
    var idx;
    var str;
    idx = window.location.href.indexOf("#");
    if(idx >= 0)
      str = window.location.href.substring(0, idx);
    else
      str = window.location.href;

    window.location.replace(str + app_state_to_hash(app_state));
    }
  }


/* Called periodicly */
function timeout_func()
  {
  if(cfg_changed)
    {
    datastore_set("cfg", cfg);
    cfg_changed = false;
    }
  }

/* Next / previous sibling */

function next_sibling()
  {
//  console.log("next_sibling " + app_state.widget + " " + obj_get_next_sibling(widgets.browser.container));
  if((app_state.widget == "browser") && widgets.browser.container &&
     ((id = obj_get_next_sibling(widgets.browser.container))))
    {
    widgets.browser.jump_to(id);
    return;
    }
  else if((app_state.widget == "iteminfo"))
    widgets.iteminfo.next();	  
  }

function prev_sibling()
  {
  if((app_state.widget == "browser") && widgets.browser.container &&
     ((id = obj_get_prev_sibling(widgets.browser.container))))
    {
    widgets.browser.jump_to(id);
    return;
    }
  else if((app_state.widget == "iteminfo"))
    widgets.iteminfo.prev();	  
  }

/* Device name */

function device_by_url(url)
  {
  var i;
  var test_url;

  for(i = 0; i < devices.length; i++)
    {

    test_url = dict_get_string(devices[i].v, GAVL_META_URI);
    if(test_url == url)
      return devices[i].v;
    }
  return null;      
  }

function get_device_name(url)
  {
  var i;
  var test_url;
      
  for(i = 0; i < devices.length; i++)
    {
    test_url = dict_get_string(devices[i].v, GAVL_META_URI);

    if(test_url == url)
      return dict_get_string(devices[i].v, GAVL_META_LABEL);
    }
  return null;      
  }

function goto_current_track()
  {
  if(playqueue_is_current())
    {
    if(current_track_id)
      widgets.browser.select_entry(current_track_id);
    }
  else
    {
    widgets.browser.jump_to(BG_PLAYQUEUE_ID, current_track_id);
    }
  }

function copy_to_favorites_internal(obj)
  {
  var msg;
  var m = dict_get_dictionary(obj, GAVL_META_METADATA);
  var klass = dict_get_string(m, GAVL_META_CLASS);

  if(!klass.startsWith("item"))
    return;

//  console.log("Copy to favorites " + JSON.stringify(obj));

  msg = msg_create(BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
  dict_set_string(msg.header, GAVL_MSG_CONTEXT_ID, "/favorites");

  msg_set_arg_int(msg, 0, -1); // idx
  msg_set_arg_int(msg, 1, 0);
  msg_set_arg_dictionary(msg, 2, obj);

//  console.log("Copy to favorites " + JSON.stringify(obj));

      
  msg_send(msg, server_connection);
      
  }

function copy_to_favorites()
  {
  if((app_state.widget == "browser") &&
     (app_state.id != "/favorites") &&
      app_state.sel_id &&
      (app_state.sel_id != ""))
    {
    var obj = widgets.browser.obj_by_id(app_state.sel_id);
    if(obj)
       copy_to_favorites_internal(obj);
    }
  }

function show_info()
  {
  if((app_state.widget == "browser") && app_state.sel_id)
    {
    app_state.info_id = app_state.sel_id;
    show_widget("iteminfo");
    }
  }

function show_log()
  {
  show_widget("iteminfo");
  }

function handle_global_keys(evt)
  {
  var done = false;
      
  switch(evt.key)
    {
    case "Backspace":
      player.stop();
      done = true;
      break;
    case " ":
      player.pause();
      done = true;
      break;
    case "Escape":
      if(hide_current_menu())
        done = true;

      else if(nav_popup.visible)
        {
        nav_popup.hide();
        done = true;
        }
      else if(app_state.widget != "browser")
        {
        widgets.browser.jump_to();
	done = true;
        }
      break;
    case "AudioVolumeUp":
    case "+":
      player_control.volume_up();
      done = true;
      break;
    case "AudioVolumeDown":
    case "-":
      player_control.volume_down();
      done = true;
      break;
    case "AudioVolumeMute":
    case "m":
      player.mute();
      done = true;
      break;
    case "MediaStop":
      player.stop();
      done = true;
      break;
    case "MediaTrackNext":
      player.next();
      done = true;
      break;
    case "MediaTrackPrevious":
      player.prev();
      done = true;
      break;
    /* Endless loop... */
    case "ContextMenu":  // Menu Key
    case "OS":           // Firefox
    case "Meta":         // Chromium
      done = true;
      break;
    case "h":
      widgets.browser.jump_to("/");
      done = true;
      break;
    case "f":
      console.log("Copy to favorites " + JSON.stringify(app_state));
      copy_to_favorites();
      break;
    case "b":
      if(app_state.widget != "browser")
        {
        widgets.browser.jump_to();
        done = true;
	}
      break;
    case "p":
      goto_current_track();
      break;
    case "n":
      if(nav_popup.visible)
        nav_popup.hide();
      else
        nav_popup.show();
      done = true;
      break;
    case "c":
      goto_current_track();
      break;
    case "s":
      if((app_state.widget == "browser"))
        {
        search_popup.show();
        done = true;
        }
      break;
    case "i":
      show_info();
      break;
    case "l":
      show_widget("logviewer");
      break;
    case "v":
      {
      next_visualization();
      }
      break;
    case "?":
    case "F1":
      show_widget("help");
      done = true;
      break;

    }
  if(done)
    stop_propagate(evt);
  return done;
  }



function wallpaper_ok()
  {
  var ar = window.innerWidth/window.innerHeight;
  if((ar > 16.0 / 11.0) && (ar < 16.0 / 7.0))
    return true;
  else
    return false;
  }

function do_set_wallapper()
  {
  var wp = "url('" + document.body.dataset.wallpaper_file + "')";
//  console.log("Set wallpaper " + wp);
  document.getElementById("bg").style.backgroundImage = wp;
  document.body.dataset.has_wallpaper = "1";
  }

function do_clear_wallapper()
  {
  document.getElementById("bg").style.backgroundImage = "";
  document.body.dataset.has_wallpaper = "0";
  }

function resize_callback()
  {
//  console.log("Resize callback " + window.innerWidth + " " + window.innerHeight );

//  div.style.minHeight = window.innerHeight + "px";
      
  if(wallpaper_ok() && document.body.dataset.wallpaper_file)
    do_set_wallapper();
  else
    do_clear_wallapper();


  if(current_widget && current_widget.div)
    adjust_header_footer(current_widget.div);

  }

function set_wallpaper(obj)
  {
  var arr;
  var m;
  var image_uri;
  var uri;

  if(obj && (m = dict_get_dictionary(obj, GAVL_META_METADATA)) && 
     (arr = dict_get_array(m, GAVL_META_WALLPAPER_URL)) &&
     (image_uri = get_image_uri_min(arr)) &&
     (uri = dict_get_string(image_uri.v, GAVL_META_URI)))
    {
    document.body.dataset.wallpaper_file = encodeURI(uri);
//    document.body.dataset.wallpaper_file = uri;
//    console.log("Setting wallpaper " + document.body.dataset.wallpaper_file);  
    if(wallpaper_ok())
      {
      do_set_wallapper();
      return;
      }
	    
    }
    
  /* Clear wallpaper */
  delete document.body.dataset.wallpaper_file;
  do_clear_wallapper();
  }

function image_onload()
  {
  adjust_header_footer(this.div);
  }

function image_onerror()
  {
  }

function make_image(div, p, obj)
  {
  var css_size;
  var arr = null;
  var image_uri = null;
  var img = null;
      
  obj = dict_get_dictionary(obj, GAVL_META_METADATA); 

  if((image_uri = dict_get_dictionary(obj, GAVL_META_COVER_URL)) ||
     (image_uri = dict_get_dictionary(obj, GAVL_META_POSTER_URL)))
    {
    p.dataset.type = "image";

    img = append_dom_element(p, "img");
    img.src = dict_get_string(image_uri, GAVL_META_URI);
//    console.log("Got image " + img.src);
    img.div = div;
    img.onload = image_onload;
    img.onerror = image_onerror;
    }
  else if(((arr = dict_get_array(obj, GAVL_META_COVER_URL)) ||
           (arr = dict_get_array(obj, GAVL_META_POSTER_URL))) &&
	  (image_uri = get_image_uri_max(arr, -1, -1)))
    {
    p.dataset.type = "image";
    
    img = append_dom_element(p, "img");
    img.src = dict_get_string(image_uri.v, GAVL_META_URI);
//    console.log("Got image " + img.src);
    img.div = div;
    img.onload = image_onload;
    img.onerror = image_onerror;
    }
  else if((image_uri = dict_get_string(obj, GAVL_META_LOGO_URL)))
    {
    p.dataset.type = "image";

    img = append_dom_element(p, "img");
    img.src = image_uri;
//    console.log("Got image " + img.src);
    img.div = div;
    img.onload = image_onload;
    img.onerror = image_onerror;
    }
  else
    {
//    console.log("No image");
    return false;
    }

  return true;
  };


function set_text_field(id, text)
  {
  var el = document.getElementById(id);
  if(!el)
    return;
 clear_element(el);
  append_dom_text(el, text);
  }


function append_meta_info_internal(parent, obj, tag, parent_container)
  {
  var str;
  var span;
  var m = dict_get_dictionary(obj, GAVL_META_METADATA);

  if(parent.childNodes.length == 1)
    append_dom_element(parent, "br");

  if(tag == GAVL_META_APPROX_DURATION)
    {
    var duration = dict_get_long(m, tag);
    if(duration <= 0)
      return;
	
    str = time_to_string(duration);
    }
  else if(tag == GAVL_META_YEAR)
    {
    var year = dict_get_string(m, GAVL_META_YEAR);
    if(!year)
      {
      year = dict_get_string(m, GAVL_META_DATE);
      if(year)
        year = year.substring(0, 4);  
      }
    if(!year)
      return;
    str = year;
    if(str == "9999")
      str = null;
    }
  else if(tag == GAVL_META_DATE)
    {
    var date = dict_get_string(m, GAVL_META_DATE);
    if(!date)
      date = dict_get_string(m, GAVL_META_YEAR);
    if(!date)
      return;
    if(date.endsWith("-99-99"))
      date = date.substring(0, 4);  

    if(date == "9999")
      str = null;
    else
      str = date;
    }
  else
    {
    str = dict_get_string(m, tag);

    if(str && parent_container &&
       (dict_get_string(dict_get_dictionary(parent_container, GAVL_META_METADATA), tag) == str))
      str = null;
    }
     
  if(str)
    {
    span = append_dom_element(parent, "span");
    span.setAttribute("class", "info-icon " + get_metatag_icon(tag));

    append_dom_text(parent, "\u00A0");
	
    span = append_dom_element(parent, "span");
    span.setAttribute("class", "info-text");
    append_dom_text(span, str + " ");
    }
  }

function append_meta_info(parent, obj, parent_container)
  {
  var v; 
  var span;
  var klass;
  var child_klass;
  var child_icon;
  var num_children; 
  var m = dict_get_dictionary(obj, GAVL_META_METADATA);
  num_children = dict_get_int(m, GAVL_META_NUM_CHILDREN);
  klass = dict_get_string(m, GAVL_META_CLASS);

  if((num_children >= 0) && klass && klass.startsWith("container"))
    {
    if((child_klass = dict_get_string(m, GAVL_META_CHILD_CLASS)) &&
       (child_icon = media_class_get_icon(child_klass)))
      {
      span = append_dom_element(parent, "span");
      span.setAttribute("class", "info-icon " + child_icon);

      span = append_dom_element(parent, "span");
      span.setAttribute("class", "info-text");
      append_dom_text(span, " " + num_children.toString());
      }
    else
      {
      var num_items = dict_get_int(m, GAVL_META_NUM_ITEM_CHILDREN);
      var num_containers = dict_get_int(m, GAVL_META_NUM_CONTAINER_CHILDREN);

      if(num_containers > 0)	  
     	{
        span = append_dom_element(parent, "span");
        span.setAttribute("class", "info-icon icon-folder");

        span = append_dom_element(parent, "span");
        span.setAttribute("class", "info-text");
        append_dom_text(span, " " + num_containers.toString() + " ");
	}
      if(num_items > 0)	  
     	{
        span = append_dom_element(parent, "span");
        span.setAttribute("class", "info-icon icon-file");
	
        span = append_dom_element(parent, "span");
        span.setAttribute("class", "info-text");
        append_dom_text(span, " " + num_items.toString() + " ");
	}
      }
	
    }

  if((v = dict_get_string(m, GAVL_META_STATION)))
    {
    if(v != dict_get_string(m, GAVL_META_LABEL))
      append_meta_info_internal(parent, obj, GAVL_META_STATION,  parent_container);
    }

  if(klass == GAVL_META_CLASS_SONG)
    {
    append_meta_info_internal(parent, obj, GAVL_META_ARTIST,   parent_container);
    append_meta_info_internal(parent, obj, GAVL_META_ALBUM,    parent_container);
    }

  append_meta_info_internal(parent, obj, GAVL_META_AUTHOR,  parent_container);

  append_meta_info_internal(parent, obj, GAVL_META_COUNTRY,  parent_container);
  append_meta_info_internal(parent, obj, GAVL_META_LANGUAGE, parent_container);
  append_meta_info_internal(parent, obj, GAVL_META_GENRE,    parent_container);
  append_meta_info_internal(parent, obj, GAVL_META_TAG,      parent_container);
  append_meta_info_internal(parent, obj, GAVL_META_DATE,     parent_container);
  append_meta_info_internal(parent, obj, GAVL_META_APPROX_DURATION, parent_container);

  if((klass == GAVL_META_CLASS_MOVIE) ||
     (klass == GAVL_META_CLASS_MOVIE_PART))
    {
    append_dom_element(parent, "br");
    append_meta_info_internal(parent, obj, GAVL_META_DIRECTOR, parent_container);
    append_meta_info_internal(parent, obj, GAVL_META_AUDIO_LANGUAGES, parent_container);
    append_meta_info_internal(parent, obj, GAVL_META_SUBTITLE_LANGUAGES, parent_container);
    }
      
  }

function set_header(obj, no_image, no_metadata)
  {
  var icon;
  var span;
  var icon;
  var title; 
  var klass; 
  var header_text = document.getElementById("header_text");
  var m = dict_get_dictionary(obj, GAVL_META_METADATA);
  clear_element(header_text);

//  console.log("set_header " + JSON.stringify(m));
      
  span = append_dom_element(header_text, "span");
  span.setAttribute("class", "header-label");

  if(!(title = dict_get_string(m, GAVL_META_TITLE)))
    title = dict_get_string(m, GAVL_META_LABEL);
    
  append_dom_text(span, title);

  icon = document.getElementById("header_icon");

  if(no_image)
    icon = make_icon(icon, obj, false, 256);
  else
    icon = make_icon(icon, obj, true, 256);

  if((icon.nodeName == "IMG") && (app_state.widget == "browser") && widgets.browser)
    {
    icon.div = widgets.browser.div;
    icon.onload = image_onload;
    }
    
  if(!no_metadata)
    {
    append_dom_element(header_text, "br");
    append_meta_info(header_text, obj);
    }
  }

/*
 *  Player
 */

function player_create_websocket(ret, url)
  {
  console.log("Opening renderer websocket " + url);

  ret.ws = new WebSocket(url, ["json"]);
      
  ret.ws.onclose = function(evt)
    {
    var div;
    my_log("Renderer websocket closed (code: " + evt.code + " " + get_websocket_close_reason(evt) + ")");
    playqueue_create();
    if((div = widgets.browser.div_by_id(BG_PLAYQUEUE_ID)))
      {
      widgets.browser.update_row(div, playqueue);
      if(widgets.browser == current_widget)
	adjust_header_footer(widgets.browser.div);
      }
    document.getElementById("player-status").setAttribute("class", "icon-x");

    //  TODO: Auto reconnect
    //    setTimeout(player_create, 1000);
    
    };

  ret.ws.player = ret;
      
  ret.ws.onopen = function()
    {
    my_log("Renderer websocket open");
    ret.ready = true;
    playqueue_init();
    };

  ret.ws.onmessage = function(evt)
    {
    var msg = msg_parse(evt.data);
//    console.log("Got renderer message " + evt.data);
    fire_event(this.player, msg);
    };

  ret.ws.onerror = function(evt)
    {
    my_log("renderer websocket error");
    };
    
  }


function player_create()
  {
  var icon;
  var icons;
  var dev;
  var ret = new Object();
  var url;
  var idx;
  ret.ready = false;
   
  if((idx = player_uri.indexOf("://")) > 0)
    url = "ws" + player_uri.slice(idx);
      
  if(player)
    {
    if(player.close)
      player.close();
    delete_my_event_handler(player, player_control);
    player = null;
    }

  player = ret;

  add_my_event_handler(ret, player_control);

  dev = device_by_url(player_uri);
  if(!dev)
    return;

  ret.name = dict_get_string(dev, GAVL_META_LABEL);
      
  if((icon = dict_get_dictionary(dev, GAVL_META_ICON_URL)) ||
     ((icons = dict_get_array(dev, GAVL_META_ICON_URL)) &&
      (icon = get_image_uri_max(icons, 248, -1))) &&
      (icon = icon.v))
    {
//    console.log("icon " + JSON.stringify(icon));
    ret.icon = dict_get_string(icon, GAVL_META_URI);
    }
  else	
    ret.icon = "icon-player";

  player_create_websocket(ret, url);

  /* Functions */

  ret.close = function()
    {
    if(this.ws)
      {
      delete this.ws.onclose;
      this.ws.close();
      delete this.ws;
      }
    playqueue_cleanup();
    };

  ret.handle_command = function(msg)
    {
    if(this.ws)
      msg_send(msg, this.ws);
    };

  ret.play = function()
    {
    this.handle_command(msg_create(BG_PLAYER_CMD_PLAY, BG_MSG_NS_PLAYER));
    };

  ret.stop = function()
    {
    this.handle_command(msg_create(BG_PLAYER_CMD_STOP, BG_MSG_NS_PLAYER));
    };

  ret.pause = function()
    {
    this.handle_command(msg_create(BG_PLAYER_CMD_PAUSE, BG_MSG_NS_PLAYER));
    };

  ret.next = function()
    {
    this.handle_command(msg_create(BG_PLAYER_CMD_NEXT, BG_MSG_NS_PLAYER));
    };

  ret.prev = function()
    {
    this.handle_command(msg_create(BG_PLAYER_CMD_PREV, BG_MSG_NS_PLAYER));
    };

  ret.next_mode = function()
    {
    var msg = msg_create(BG_CMD_SET_STATE_REL, BG_MSG_NS_STATE);

    msg_set_arg_int(msg, 0, 1); // Last
    msg_set_arg_string(msg, 1, BG_PLAYER_STATE_CTX);
    msg_set_arg_string(msg, 2, BG_PLAYER_STATE_MODE);
    msg_set_arg_int(msg, 3, 1);
	
    this.handle_command(msg);
    };

  ret.set_volume = function(vol)
    {
    var msg = msg_create(BG_CMD_SET_STATE, BG_MSG_NS_STATE);

    msg_set_arg_int(msg, 0, 1); // Last
    msg_set_arg_string(msg, 1, BG_PLAYER_STATE_CTX);
    msg_set_arg_string(msg, 2, BG_PLAYER_STATE_VOLUME);
    msg_set_arg_float(msg, 3, vol);
    this.handle_command(msg);
    };

  ret.set_volume_rel = function(vol)
    {
    var msg = msg_create(BG_CMD_SET_STATE_REL, BG_MSG_NS_STATE);

    msg_set_arg_int(msg, 0, 1); // Last
    msg_set_arg_string(msg, 1, BG_PLAYER_STATE_CTX);
    msg_set_arg_string(msg, 2, BG_PLAYER_STATE_VOLUME);
    msg_set_arg_float(msg, 3, vol);
    this.handle_command(msg);
    };

  ret.seek_perc = function(perc)
    {
    var msg;

    msg = msg_create(BG_CMD_SET_STATE, BG_MSG_NS_STATE);
    msg_set_arg_int(msg, 0, 1); // Last
    msg_set_arg_string(msg, 1, BG_PLAYER_STATE_CTX);
    msg_set_arg_string(msg, 2, BG_PLAYER_STATE_TIME_PERC);
    msg_set_arg_float(msg, 3, perc);
    this.handle_command(msg);
    };
      
  ret.mute = function()
    {
    var msg = msg_create(BG_CMD_SET_STATE_REL, BG_MSG_NS_STATE);

    msg_set_arg_int(msg, 0, 1); // Last
    msg_set_arg_string(msg, 1, BG_PLAYER_STATE_CTX);
    msg_set_arg_string(msg, 2, BG_PLAYER_STATE_MUTE);
    msg_set_arg_int(msg, 3, 1);

//    console.log("handle_command: " + this + " " + this.handle_command);
	
    this.handle_command(msg);
    };

  player_control.init_volume = true;
  player_control.init_mute   = true;
      
  player = ret;

  if(url == "local:")
    {
    playqueue_init();
    }


    //  console.log("handle_command: " + this + " " + this.handle_command);
  }

/* */

function _key_event(evt)
  {
  fire_event(key_event_hub, evt);
  }

function init_key_events()
  {
  var key_hdlr;
  console.log("init_key_events");

  key_event_hub = new Object();
  key_event_hub.connect = function(cb)
    {
    prepend_my_event_handler(this, cb);
    };

  key_event_hub.disconnect = function(cb)
    {
    delete_my_event_handler(this, cb);
    };

  key_hdlr = new Object();
      
  /* Create handler for global keys */
  key_hdlr.handle_msg = function(evt)
    {
    if(handle_global_keys(evt))
      {
      stop_propagate(evt);
      return 0;
      }
    else
      return 1;
    };
  
  add_event_handler(document, "keydown", _key_event);

  key_event_hub.connect(key_hdlr);
  }

function update_nav_popup()
  {
  var enabled;
  var obj;
  var klass;
  var m;
  // MENU_ID_PLAYER
  enabled = true;

  nav_popup.player_button.disabled = !enabled;

  // BROWSER
  if(!app_state || (app_state.widget != "browser"))
    enabled = true;
  else
    enabled = false;

  nav_popup.browser_button.disabled = !enabled;

//  nav_popup.current_button.disabled = !enabled;

  // SETTINGS
  if(!app_state || (app_state.widget != "settings"))
    enabled = true;
  else
    enabled = false;

  nav_popup.settings_button.disabled = !enabled;
      
  // HOME
  if(!app_state || (app_state.widget != "browser") || (app_state.id != "/"))
    enabled = true;
  else
    enabled = false;

  nav_popup.home_button.disabled = !enabled;
      
  // SEARCH
  if(app_state && (app_state.widget == "browser"))
    enabled = true;
  else
    enabled = false;

  nav_popup.search_button.disabled = !enabled;
      
  // INFO

  if(app_state && (app_state.widget == "browser"))
    {
    if(!(obj = widgets.browser.get_selected_obj()))
      enabled = false;
    else if((klass = obj_get_string(obj, GAVL_META_CLASS)) &&
	    ((klass == GAVL_META_CLASS_MUSICALBUM) ||
	     (klass == GAVL_META_CLASS_PLAYLIST) ||
	     (klass == GAVL_META_CLASS_TV_SEASON) ||
	     (klass == GAVL_META_CLASS_TV_SHOW) ||
	     klass.startsWith("item.")))
      enabled = true;
    else
      enabled = false;
    }
  else if(app_state && (app_state.widget == "player"))
    enabled = true;
  else
    enabled = false;

  nav_popup.info_button.disabled = !enabled;

  // NEXT_SIBLING
  enabled = false;
  if(app_state)
    {
    if((app_state.widget == "browser") &&
       widgets.browser.container &&
       obj_get_next_sibling(widgets.browser.container))
      enabled = true;
      else if((app_state.widget == "iteminfo") && (app_state.id != BG_PLAYQUEUE_ID) &&
               widgets.iteminfo.obj && obj_get_next_sibling(widgets.iteminfo.obj))
      enabled = true;
    }


  nav_popup.next_button.disabled = !enabled;

  // PREV_SIBLING
  enabled = false;
  if(app_state)
    {
    if((app_state.widget == "browser") &&
       widgets.browser.container &&
       obj_get_prev_sibling(widgets.browser.container))
      enabled = true;
    else if((app_state.widget == "iteminfo") && (app_state.id != BG_PLAYQUEUE_ID) && 
            widgets.iteminfo.obj && obj_get_prev_sibling(widgets.iteminfo.obj))
      enabled = true;
    }

  nav_popup.prev_button.disabled = !enabled;

  // UP
  if(app_state && (app_state.widget == "browser") && (app_state.id != "/"))
    enabled = true;
  else 
    enabled = false;

  nav_popup.levelup_button.disabled = !enabled;


  // Fav
  if(app_state)
    {
    if((app_state.widget == "browser") &&
       (app_state.id != "/favorites") &&
       app_state.sel_id &&
       (app_state.sel_id != "") &&
       (obj = widgets.browser.obj_by_id(app_state.sel_id)) &&
       (m = dict_get_dictionary(obj, GAVL_META_METADATA)) &&
       (klass = dict_get_string(m, GAVL_META_CLASS)) &&
       klass.startsWith("item"))
      enabled = true;
    else if((app_state.widget == "player") && player_control.current_track)
      enabled = true;
    else
      enabled = false;
    }
  else
    enabled = false;
  
  nav_popup.fav_button.disabled = !enabled;

  // Visualize
      
  }

function adjust_header_footer(div)
  {
  var have_add_play = false;
  var div_height;
  var td;
  var header_height = document.getElementsByTagName("header")[0].getBoundingClientRect().height;
  var footer_height = document.getElementsByTagName("footer")[0].getBoundingClientRect().height;
  var window_height = window.innerHeight;

  if(current_widget == widgets.imageviewer)
    return;
    
  div.style.paddingTop    = parseInt(header_height) + "px";
  div.style.paddingBottom = parseInt(footer_height) + "px";

  /* If the size of the div is smaller than the window, we center vertically between header and footer */

  if(div.dataset.vcenter == "true")
    {
    div_height = get_element_position(div).h;

//    console.log("adjust_header_footer " + div_height + " " + window_height);
      
    if(div_height < window_height)
      {
      var delta = 0.5 * (window_height - div_height);
      div.style.paddingTop    = parseInt(header_height + delta) + "px";
      div.style.paddingBottom = parseInt(footer_height + delta) + "px";
      }
    }

  /* Show or hide the add and play buttons */
  
  if((app_state.widget == "browser") && (app_state.id != BG_PLAYQUEUE_ID) &&
     widgets.browser.container &&
     (obj_get_int(widgets.browser.container, GAVL_META_NUM_ITEM_CHILDREN) > 0))
    {
    have_add_play = true;
    } 
  td = document.getElementById("header_buttons");
  td.dataset.has_play = have_add_play;
  td.dataset.has_add = have_add_play;
  }

function menu_add_icon(v, parent)
  {
  if(!v)
    return;      
  if(v.t == "s")
    {
    if(v.v.startsWith("icon-"))
      {
      el = append_dom_element(parent, "span");
      el.setAttribute("class", v.v); 
      } 
//    else
//      console.log("No icon: " + JSON.stringify(v.v));
    }
  else if(v.t == "d")
    {
    el = append_dom_element(parent, "img");
    el.setAttribute("src", dict_get_string(v.v, GAVL_META_URI)); 
//    console.log("Got icon  " + JSON.stringify(v.v));
    }
  else if(v.t == "a")
    {
    var image_uri = get_image_uri_max(v.v, 48, 48);

    if(!image_uri)
      return;	    
    el = append_dom_element(parent, "img");
    el.setAttribute("src", dict_get_string(image_uri.v, GAVL_META_URI)); 
    }
/* 
  else
    {
    console.log("Got no icon for " + JSON.stringify(v));
    }
*/
  }

function set_menu_items(menu, cfg)
  {
  var i;
  var children = dict_get_array(cfg, GAVL_META_CHILDREN);
  clear_element(menu.div);

  for(i = 0; i < children.length; i++)
    {
    div = append_dom_element(menu.div, "div");

    div.dataset.disabled = "false";

//    console.log("BLA " + children[i] + " " + children[i].v);
    div.dataset.id       = dict_get_string(children[i].v, GAVL_META_ID);

    div.menu = menu;

    div.onmousedown = function()
      {
      this.menu.select_entry(this);
      };
	
    div.onclick = function(evt)
      {
      this.menu.fire();
      stop_propagate(evt);
      };
	
    table = append_dom_element(div, "table");
    tr = append_dom_element(table, "tr");

    td = append_dom_element(tr, "td");

    menu_add_icon(children[i].v[GAVL_META_ICON_URL], td);

    td = append_dom_element(tr, "td");
    append_dom_text(td, dict_get_string(children[i].v, GAVL_META_LABEL));
    }
      
  }

function create_menu(menu_data)
  {
  var div;
  var i;
  var menu;
  var table;
  var tr;
  var td;
  var el;
  var icon;

  var children = dict_get_array(menu_data, GAVL_META_CHILDREN);

  menu = new Object();
      
  menu.div = append_dom_element(document.body, "div");
  menu.div.setAttribute("class", "menu");
  menu.div.style.display = "none";      
  
  menu.div.dataset.id = dict_get_string(menu_data.v, GAVL_META_ID);
  menu.data = menu_data;
      
  clear_element(menu);

  if(!children || !children.length)
    return null;

  menu.key_hdlr = new Object();
  menu.key_hdlr.menu = menu;
  menu.key_hdlr.handle_msg = function(evt)
    {
//    console.log("Key pressed " + evt.key);
    switch(evt.key)
      {
      case "Escape":
        this.menu.hide();
        break;
      case "ArrowLeft":
        break;
      case "Enter":
        this.menu.fire();
        break;
      case "ArrowDown":
        while(true)
          {
          this.menu.selected_idx++;
	  if(this.menu.selected_idx >= this.menu.div.childNodes.length)
	    this.menu.selected_idx = 0;

	  if(this.menu.div.childNodes[this.menu.selected_idx].dataset.disabled == "false")
	    break;
          }
        menu.select_entry(this.menu.div.childNodes[this.menu.selected_idx]);
        break;
      case "ArrowUp":
        while(true)
          {
          this.menu.selected_idx--;
	  if(this.menu.selected_idx < 0)
	    this.menu.selected_idx = this.menu.div.childNodes.length-1;

	  if(this.menu.div.childNodes[this.menu.selected_idx].dataset.disabled == "false")
	    break;
          }
        menu.select_entry(this.menu.div.childNodes[this.menu.selected_idx]);
        break;
      case "PageDown":
        break;
      case "PageUp":
        break;
      case "End":
        break;
      case "Home":
        break;
      default:
        return 0;
	break;
      }
    stop_propagate(evt);
    return 0;
    };

  set_menu_items(menu, menu_data);
      
  /* Functions */
  
  menu.select_entry = function(div)
    {
    var i;
    for(i = 0; i < this.div.childNodes.length; i++)
      {
      if(this.div.childNodes[i] == div)
	{
        this.selected_idx = i;
        this.div.childNodes[i].dataset.selected = "true";
	}
      else
        this.div.childNodes[i].dataset.selected = "false";
      }
    };
      
  menu.enable_entry = function(id, enable)
    {
    var i = 0;

    for(i = 0; i < this.div.childNodes.length; i++)
      {
      if(this.div.childNodes[i].dataset.id == id)
        {
        if(enable)
          this.div.childNodes[i].dataset.disabled = "false";
        else
          this.div.childNodes[i].dataset.disabled = "true";
        break;
        }
      }
    };
      
  menu.selected_idx = -1;

  /* Show menu, send messages to cb.handle_msg */
  menu.show = function(parent, cb)
    {
    var parent_rect;
    var menu_rect;

    this.cb = cb;
    this.visible = true;
	
    hide_current_menu();
    current_menu = this;
	
    parent_rect = parent.getBoundingClientRect();

    // document.body.getBoundingClientRect();
    	

    /* Reset properties */

    this.div.style.top = "";
    this.div.style.bottom = "";
	
    /* Vertical pos */
    if(parent_rect.top < window.innerHeight - parent_rect.bottom)
      this.div.style.top = parseInt(parent_rect.bottom) + "px";
    else
      this.div.style.bottom = parseInt(parent_rect.top) + "px";
	
    /* Horizontal pos */
    if(parent_rect.left < window.innerWidth / 2)
      this.div.style.left = parseInt(parent_rect.left) + "px";
    else
      this.div.style.right = parseInt(window.innerWidth - parent_rect.right) + "px";
  
    this.div.style.display = "block";
    key_event_hub.connect(this.key_hdlr);

    menu_rect = this.div.getBoundingClientRect();

    if(menu_rect.bottom > window.innerHeight)
      this.div.style.top = "0px";

    if(menu_rect.bottom > window.innerHeight)
      this.div.style.bottom = "0px";

    };

  menu.hide = function()
    {
//    console.log("menu.hide");
    this.div.style.display = "none";
    this.visible = false;
    key_event_hub.disconnect(this.key_hdlr);
    current_menu = null;
    };

  menu.fire = function()
    {
    var msg;
    this.hide();
    msg = msg_create(BG_MSG_MENU_ITEM_FIRED, BG_MSG_NS_MENU);
    msg_set_arg_string(msg, 0, this.div.dataset.id);
    msg_set_arg_string(msg, 1, this.div.childNodes[this.selected_idx].dataset.id);
    msg_set_arg_int(msg, 2, this.selected_idx);
    this.cb.handle_msg(msg);
    };
//  console.log("create_menu returned " + menu);     
  return menu;
  }

/*
 *  Configuration stuff
 */

var CFG_TYPE_ENUM   = "enum";
var CFG_TYPE_BUTTON = "button";

var cfg_info = [
];

function cfg_item_get(id)
  {
  var i;
  for(i = 0; i < cfg_info.length; i++)
    {
    if(id == dict_get_string(cfg_info[i], GAVL_META_ID))
      return cfg_info[i];
    }
  return null;
  }

function cfg_set_parameter(name, val)
  {
//  console.log("cfg_set_parameter " + name + " " + val);
  switch(name)
    {
    case "renderer":
      /* Create player */
      player_uri = val;	

      clear_current();
      player_create();
      menu_button_set(name, val);
      break;
    case "skin":
      {
      var css = document.getElementById("skin-css");
      css.setAttribute("href", "/static/css/skin-" + val + ".css");
      menu_button_set(name, val);
      }
      break;
    case "playercontrol_visible":
      {
      if(val)
        player_control.show();
      else
        player_control.hide();
      }
      break;
    case "rescan_devs":
      {
      msg_send(msg_create(BG_MSG_BACKENDS_RESCAN, BG_MSG_NS_BACKEND), server_connection);
      }
      break;
    }
  }

function cfg_item_create(id, type, label)
  {
  var ret = new Object();
  dict_set_string(ret, GAVL_META_ID, id);
  dict_set_string(ret, "type", type);
  dict_set_string(ret, GAVL_META_LABEL, label);
  return ret;
  }

function create_menu_data_item(parent_menu, id, icon, label)
  {
  var ret = new Object();
  ret.t = GAVL_TYPE_DICTIONARY;
  ret.v = new Object();

// console.log("create_menu_data_item " + id + " " + label);
//  console.trace();
      
  if(id)
    dict_set_string(ret.v, GAVL_META_ID, id);
  if(icon)
    {
    if(typeof icon == "string")
      {
      var obj = new Object();
      obj.t = "s";
      obj.v = icon;
      dict_set(ret.v, GAVL_META_ICON_URL, obj);
      }
    else
      dict_set(ret.v, GAVL_META_ICON_URL, icon);
    }
          
  if(label)
    dict_set_string(ret.v, GAVL_META_LABEL, label);

  if(parent_menu)
    {
    var children;

    if(!(children = dict_get_array(parent_menu, GAVL_META_CHILDREN)))
      children = dict_set_array(parent_menu, GAVL_META_CHILDREN);

    children.push(ret);
    }
  return ret;
  }

function cfg_info_set_devices(info)
  {
  var i;
  var children;
  var uri;

  if(!(children = dict_get_array(info.menu_data, GAVL_META_CHILDREN)))
    children = dict_set_array(info.menu_data, GAVL_META_CHILDREN);
  else
    children.splice(0, children.length);

  for(i = 0; i < devices.length; i++)
    {
//    console.log("device type " + dict_get_string(devices[i].v, "type"));
	
    if(dict_get_string(devices[i].v, GAVL_META_CLASS) != GAVL_META_CLASS_BACKEND_RENDERER)
      continue;

    if(!(uri = dict_get_string(devices[i].v, "proxy")))
      uri = dict_get_string(devices[i].v, GAVL_META_URI);

    // create_menu_data_item(parent_menu, id, icon, label)
	
    create_menu_data_item(info.menu_data,
			  uri,
			  devices[i].v[GAVL_META_ICON_URL],
			  dict_get_string(devices[i].v, GAVL_META_LABEL));
    }
      
  }

function cfg_info_init()
  {
  var info = null;

  /* Devices */
  info = cfg_item_create("renderer", CFG_TYPE_ENUM, "Renderer");

  info.menu_data = create_menu_data_item(undefined, "renderer");
  cfg_info_set_devices(info);
      
  cfg_info.push(info);

  /* Rescan */
    
  info = cfg_item_create("rescan_devs", CFG_TYPE_BUTTON, "Search devices");
  cfg_info.push(info);
  
  /* Skins */

  info = cfg_item_create("skin", CFG_TYPE_ENUM, "Skin");
  info.menu_data = create_menu_data_item(undefined, "skin");

  create_menu_data_item(info.menu_data, "darkblue", null, "Dark Blue");
  create_menu_data_item(info.menu_data, "lightblue", null, "Light Blue");
    
  cfg_info.push(info);

  }

function cfg_menu_handle_msg(msg)
  {
  switch(msg.ns)
    {
    case BG_MSG_NS_MENU:
      switch(msg.id)
        {
	case BG_MSG_MENU_ITEM_FIRED:

          //    msg_set_arg_string(msg, 0, this.div.dataset.id);
          //    msg_set_arg_string(msg, 1, this.div.childNodes[this.selected_idx].dataset.id);
          //    msg_set_arg_int(msg, 2, this.selected_idx);
//          console.log("cfg_menu_handle_msg " + msg.args[0].v + " " +
//		      msg.args[1].v + " " + msg.args[2].v);

          if(cfg[msg.args[0].v] != msg.args[1].v)
	    {
	    cfg[msg.args[0].v] = msg.args[1].v;
	    cfg_changed = true;
            }
          cfg_set_parameter(msg.args[0].v, msg.args[1].v);
          break;
        }
      break;
    }
  return 1;
  }

var cfg_menu_cb = null;

function cfg_menu_button_callback(evt)
  {
  if(this.menu.visible)
    {
    hide_current_menu();
    return;
    }

  hide_current_menu();

  if(!cfg_menu_cb)
    {
    cfg_menu_cb = new Object();
    cfg_menu_cb.handle_msg = cfg_menu_handle_msg;
    }

//  console.log("cfg_menu_button_callback " + this + " " + );
        
  this.menu.show(this, cfg_menu_cb);
  }

function cfg_button_callback(evt)
  {
//  console.log("cfg_button_callback " + this.id.substring(11));
  cfg_set_parameter(this.id.substring(11), true);
  }

function menu_button_set(id, val)
  {
  var table;
  var tr;
  var td;
  var icon;      
  var i;
  var button = document.getElementById("cfg-widget-" + id);
  var children;

  if(!button.menu)
      return;
  children = dict_get_array(button.menu.data, GAVL_META_CHILDREN);

  clear_element(button);

  table = append_dom_element(button, "table");
  table.setAttribute("style", "width: 100%;");

  tr = append_dom_element(table, "tr");
      
  for(i = 0; i < children.length; i++)
    {
    if(val == dict_get_string(children[i].v, GAVL_META_ID))
      {
      if((icon = children[i].v[GAVL_META_ICON_URL]))
        {
        td = append_dom_element(tr, "td");
        td.setAttribute("style", "text-align: center; width: 1.5em;");
        menu_add_icon(icon, td);
        }

      td = append_dom_element(tr, "td");
      td.setAttribute("style", "text-align: left;");
      append_dom_text(td, dict_get_string(children[i].v, GAVL_META_LABEL));
      break;
      }
    }
      
  td = append_dom_element(tr, "td");
  td.setAttribute("class", "icon-chevron-down");
  td.setAttribute("style", "text-align: right;");
  }

function append_cfg_row(table)
  {
  var tr = append_dom_element(table, "tr");
  return tr;
  }

function cfg_init()
  {
  var i;
  var tr;
  var td;
  var button;
      
  var table = document.getElementById("settings-table");
      
  for(i = 0; i < cfg_info.length; i++)
    {
    switch(dict_get_string(cfg_info[i], "type"))
      {
      case CFG_TYPE_ENUM:
        tr = append_cfg_row(table, "tr");
        td = append_dom_element(tr, "td");
        td.setAttribute("style", "white-space: nowrap;");
	  
        append_dom_text(td, dict_get_string(cfg_info[i], GAVL_META_LABEL));

        td = append_dom_element(tr, "td");
        td.setAttribute("style", "width: 99%;");

        button = append_dom_element(td, "button");
	  
        button.menu = create_menu(cfg_info[i].menu_data);
//        console.log("cfg_init " + button.menu);
	
        button.setAttribute("id", "cfg-widget-" +
			    dict_get_string(cfg_info[i], GAVL_META_ID));
        button.setAttribute("class", "menubutton");

	button.onclick = cfg_menu_button_callback;
        	  
        break;
      case CFG_TYPE_BUTTON:
        tr = append_cfg_row(table, "tr");
        td = append_dom_element(tr, "td");
        td.setAttribute("colspan", "2");
	
        button = append_dom_element(td, "button");

	append_dom_text(button, dict_get_string(cfg_info[i], GAVL_META_LABEL));
        button.setAttribute("id", "cfg-widget-" +
			    dict_get_string(cfg_info[i], GAVL_META_ID));
	button.onclick = cfg_button_callback;
        break;
      }

    }
      
  }

/*
 *  Browser
 */

function get_parent_id(id)
  {
  var idx;
  idx = id.lastIndexOf("/");
  if((idx < 0) || (id == "/"))
    return null;
  else if(idx == 0)
    return "/";
  else 
    return id.substring(0, idx);
  }


/* Load progress: We keep this global. p is between 0 and 1, negative means to hide the widget */

function load_progress(p)
  {
  var popup = document.getElementById("progress-popup");
  var prog = document.getElementById("load-progress");

  if(p < 0.0)
    {
    popup.style.display = "none";
    return;
    }
  if(popup.style.display != "block")
    popup.style.display = "block";

  prog.value = p * 100.0;
      
  }

var tile_modes =
  [
    {
      [GAVL_META_CLASS]: GAVL_META_CLASS_PHOTOALBUM
    },
    {
      [GAVL_META_CHILD_CLASS]: GAVL_META_CLASS_MUSICALBUM
    },
    {
      [GAVL_META_CHILD_CLASS]: GAVL_META_CLASS_MOVIE
    },

  ];

function make_label(m, klass)
  {
  /* Label */
  if((klass == GAVL_META_CLASS_MOVIE) ||
     (klass == GAVL_META_CLASS_SONG) ||
     (klass == GAVL_META_CLASS_MUSICALBUM))
    return dict_get_string(m, GAVL_META_TITLE);
  else
    return dict_get_string(m, GAVL_META_LABEL);
    
  }


function create_browser()
  {
  var ret;
  ret = new Object();

  ret.container = null;
  ret.visible = false;

  ret.div = document.getElementById("browser");
  ret.div.browser = ret;
  ret.div.dataset.id = "browser";
  ret.cur_id = null;

  ret.num_columns = 1;
    
  ret.mode = BROWSER_MODE_LIST;

  ret.mode_button = document.getElementById("browse-mode-button");
  ret.mode_button.b = ret;
  ret.mode_button.onclick = function()
    {
    this.b.toggle_mode();
    }
    
  ret.get_browse_mode = function()
    {
    var val;
    var i;
    var m = dict_get_dictionary(this.container, GAVL_META_METADATA);
      
    for(i = 0; i < tile_modes.length; i++)
      {
	if(tile_modes[i][GAVL_META_CLASS] &&
	   (val = dict_get_string(m, GAVL_META_CLASS)) &&
	   (tile_modes[i][GAVL_META_CLASS] == val))
	  return BROWSER_MODE_TILES;
	else if(tile_modes[i][GAVL_META_CHILD_CLASS] &&
	   (val = dict_get_string(m, GAVL_META_CHILD_CLASS)) &&
           (tile_modes[i][GAVL_META_CHILD_CLASS] == val))
        return BROWSER_MODE_TILES;
      }
    return BROWSER_MODE_LIST;
    }
    
  ret.set_mode_internal = function(mode)
    {
    var child_klass;
    var column_template;
//    console.log("set_mode_internal " + mode);
    this.div.dataset.mode = mode;

    switch(mode)
      {
      case BROWSER_MODE_LIST:
        this.div.style.display = "block";
        this.render = this.render_list;
	this.num_columns = 1;
        this.mode_button.setAttribute("class", "icon-view-icons");
	break;
      case BROWSER_MODE_TILES:
        this.div.style.display = "grid";
        this.render = this.render_tiles;
        this.num_columns = Math.floor(window.innerWidth / 256.0);

//        this.div.style.setProperty("grid-template-columns",
//				   "repeat(" + this.num_columns + ", 1fr)");

        this.div.style.setProperty("grid-template-columns",
				   "repeat(" + this.num_columns + ", " + 99 / this.num_columns + "%)");

	this.mode_button.setAttribute("class", "icon-view-list");

	this.div.dataset.imgaspect = "square";

        child_class = obj_get_string(this.container, GAVL_META_CHILD_CLASS);
        if((child_class == GAVL_META_CLASS_MOVIE)  ||
	   (child_class == GAVL_META_CLASS_TV_SEASON) ||
	   (child_class == GAVL_META_CLASS_TV_SHOW))
 	  this.div.dataset.imgaspect = "poster";
        break;
      }
      
    }

  ret.set_mode = function(mode)
    {
    if(this.div.dataset.mode == mode)
      return;

    clear_element(this.div);
      
    this.set_mode_internal(mode);

    this.splice(1, 0, 0, obj_get_children(this.container));

    }

  ret.toggle_mode = function()
    {
    if(this.div.dataset.mode == BROWSER_MODE_LIST)
      this.set_mode(BROWSER_MODE_TILES);
    else
      this.set_mode(BROWSER_MODE_LIST);
    }
    
  ret.jump_to = function(id, sel_id)
    {
      
    if(id)
      {
      if(app_state.id != id)
	{
        push_state();
	}
      app_state.id = id;

      if(sel_id)
        app_state.sel_id = sel_id;
      else
        delete app_state.sel_id;
      }
    delete app_state.info_id;
    app_state.widget = "browser";
    app_state_apply();
    update_hash();
    }
      
  ret.set_current = function(id)
    {
    this.set_current_by_hash(id);
    };
      
  ret.change_up = function()
    {
    var parent_id;
    var id;
      
    if((this.container) &&
       (id = obj_get_id(this.container)) &&
       (parent_id = get_parent_id(id)))
      {
      /* Check if the new ID is the last entry in the history */
      console.log("Change up " + my_history_pos + " " + parent_id);
      if((my_history_pos > 0) && (my_history[my_history_pos-1].id == parent_id))
        {
        my_history_go(-1);
	}
      else
	this.jump_to(parent_id, id);
      }
    };
      
  /*  */
  ret.div.onmousedown = function(evt)
    {
    hide_current_menu(evt);
    };
      
  ret.key_hdlr = new Object();
  ret.key_hdlr.b = ret;

  ret.key_hdlr.handle_msg = function(evt)
    {
    var i;
    var str;
    var children;
    var obj;
//    console.log("Key pressed " + evt.key);

    switch(evt.key)
      {
      case "Escape":
        this.b.change_up();
        break;
      case "ArrowLeft":
        if(evt.ctrlKey)
          {
          prev_sibling();
          break;
	  }
        else if(evt.altKey)
          {
          my_history_go(-1);
          break;
          }
	else if(this.b.div.dataset.mode == BROWSER_MODE_TILES)	
	  {
	  this.b.move_selected(-1);
          break;
          }
	else
	  return 1;
        break;
      case "ArrowRight":
        if(evt.ctrlKey)
          {
          next_sibling();
          break;
	  }
        else if(evt.altKey)
          {
          my_history_go(1);
          break;
          }
	else if(this.b.div.dataset.mode == BROWSER_MODE_TILES)	
	  {
	  this.b.move_selected(1);
          break;
          }
	else
	  return 1;
        break;
      case "ArrowDown":
        this.b.move_selected(this.b.num_columns);
        break;
      case "ArrowUp":
        this.b.move_selected(-this.b.num_columns);
        break;
      case "PageDown":
        this.b.move_selected(10);
        break;
      case "PageUp":
        this.b.move_selected(-10);
        break;
      case "End":
        this.b.select_entry(this.b.div.childNodes[this.b.div.childNodes.length-1].dataset.id);
        break;
      case "Home":
        this.b.select_entry(this.b.div.childNodes[0].dataset.id);
        break;
      case "Enter":
        if(app_state.sel_id)
          this.b.entry_fire(app_state.sel_id);
        break;
      case "A":
        playqueue_add_album(this.b.container, true, false);
        break;
      case "a":
        obj = this.b.obj_by_id(app_state.sel_id);
	playqueue_add_entry(obj, false, false);
        break;
      case "MediaPlay":
        this.b.play_selected();
        break;
      default:
	return 1;
	break;
      }
    stop_propagate(evt);
    return 0;
    };

  ret.render_common  = function(div, obj)
    {
    div.onmousedown = function(evt)
      {
      if(hide_current_menu(evt))
        return;
      // Can be that we aren't in the DOM tree anymore
      if(!this.parentNode) 
	return;
      this.parentNode.browser.select_entry(this.dataset.id);
      };

    div.ondblclick = function(evt)
      {
      this.parentNode.browser.entry_fire(this.dataset.id);
      };

    }

  ret.use_image = function(klass)
    {
    if(this.have_container_image &&
       ((klass == GAVL_META_CLASS_SONG) ||
	(klass == GAVL_META_CLASS_TV_EPISODE)))
      return false;
    else
      return true;
     
    }
    
  ret.render_tiles = function(div, obj)
    {
    var table;
    var tr;
    var td;
    var child_div;

    var klass;
    var m = dict_get_dictionary(obj, GAVL_META_METADATA);

    this.render_common(div, obj);

    klass = obj_get_string(obj, GAVL_META_CLASS);

    /* Icon */
    child_div = append_dom_element(div, "div");
    child_div.setAttribute("class", "tile-image");

    make_icon(child_div, obj, this.use_image(klass), 600);
    
    /* Label */
    table = append_dom_element(div, "table");
    table.setAttribute("style", "width: 100%;");

    tr = append_dom_element(table, "tr");
    td = append_dom_element(tr, "td");
    td.setAttribute("class", "browser-label");
    append_dom_text(td, make_label(m, klass));
     
    }
    
  ret.render_list = function(div, obj)
    {
    var span;
    var table;
    var tr;
    var td;
    var duration;
    var str;
    var m;
    var button;
    var icon;
    var klass = obj_get_string(obj, GAVL_META_CLASS);

    this.render_common(div, obj);
      
    m = dict_get_dictionary(obj, GAVL_META_METADATA);
      
    div.setAttribute("class", "browser");
    div.setAttribute("style", "width: 100%;");
	
    table = append_dom_element(div, "table");
    table.setAttribute("style", "width: 100%;");
	
    tr = append_dom_element(table, "tr");

    /* Icon */

    td = append_dom_element(tr, "td");
    td.setAttribute("class", "listicon");
  
      
//    console.log("have image " + klass + " " + this.have_container_image);
    make_icon(td, obj, this.use_image(klass), 256);
    
    td = append_dom_element(tr, "td");
//    td.setAttribute("style", "width: 100%;");

    span = append_dom_element(td, "span");
    span.setAttribute("class", "browser-label");
    append_dom_text(span, make_label(m, klass));

    /* Further info */
    append_dom_element(td, "br");
    append_meta_info(td, obj, browser.container);

    td = append_dom_element(tr, "td");
    td.setAttribute("style", "width: 1.2em; padding: 0px;");
	
    /* Arrow */
    if(dict_get_string(m, GAVL_META_CLASS).startsWith("container"))
      {
      if(dict_get_int(m, GAVL_META_LOCKED))	    
        {
        td.setAttribute("class", "lock-icon");
        icon = append_dom_element(td, "span");
        icon.setAttribute("class", "icon-lock");

	}
      else
        {
        button = append_dom_element(td, "button");
        button.setAttribute("type", "button");
        button.setAttribute("class", "icon-chevron-right");
        button.div = div;
	  
        button.onclick = function(evt)
          {
          if(hide_current_menu(evt))
            return;
	    
          this.div.parentNode.browser.select_entry(this.div.dataset.id);
          this.div.parentNode.browser.entry_fire(this.div.dataset.id);
          };

	}
      }
    /* Play icon */
    else
      {
      td.setAttribute("class", "play-icon");
      icon = append_dom_element(td, "span");
      icon.setAttribute("class", "icon-play");
      icon.setAttribute("style", "font-size: 200%;");

      if(div.dataset.id == current_track_id)
        div.dataset.current = "true";
      else
        div.dataset.current = "false";
      }
    append_dom_element(div, "hr");
    };

      
  ret.create_row = function(obj)
    {
    var r = document.createElement("div");
    r.dataset.id = obj_get_id(obj);
    r.dataset.hash = dict_get_string(dict_get_dictionary(obj, GAVL_META_METADATA), GAVL_META_HASH);
    r.dataset.selected = "false";
    r.dataset.current  = "false";
    this.render(r, obj);
    return r;
    };

  ret.update_row = function(div, obj)
    {
    clear_element(div);
    this.render(div, obj);
    };
      
  ret.get_selected_obj = function()
    {
    if(app_state.sel_id)
      return this.obj_by_id(app_state.sel_id);
    return null;
    };
      
  ret.scroll = function(el)
    {
    var header = document.getElementsByTagName("header")[0];
    var footer = document.getElementsByTagName("footer")[0];

    var header_rect = header.getBoundingClientRect();
    var footer_rect = footer.getBoundingClientRect();
    var el_rect = el.getBoundingClientRect();

//    console.log("Scroll " + el_rect.top + " " + el_rect.bottom);
	
    if(el_rect.bottom > footer_rect.top)
      {
//      console.log("Scroll down " + (el_rect.bottom - footer_rect.top));
      window.scrollBy(0, el_rect.bottom - footer_rect.top);
      }

    if(el_rect.top < header_rect.bottom)
      {
//      console.log("Scroll up " + (el_rect.top - header_rect.bottom));
      window.scrollBy(0, el_rect.top - header_rect.bottom);
      }
    };

  ret.splice = function(last, idx, del, add)
    {
    var i;
    var el;
    var sel_idx;

      
//    console.log("splice 1 " + idx);
    
    if(app_state.id == "/")
      idx++;

//    console.log("splice2 " + idx + " " + playqueue + " " + playqueue.visible);

    if(del > 0)
      {
	 
      sel_idx = -1;

      if(app_state.sel_id)
        sel_idx = this.idx_by_id(app_state.sel_id);
	  
      for(i = 0; i < del; i++)
         this.div.removeChild(this.div.childNodes[idx]);

      if((this.idx_by_id(app_state.sel_id) < 0) &&
	 (this.div.childNodes.length > 0))
	{
        if(sel_idx >= this.div.childNodes.length)
          sel_idx = this.div.childNodes.length - 1;
        this.select_entry(this.div.childNodes[sel_idx].dataset.id);
        }
      }
    if(is_array(add))
      {
//      console.log("Adding %d entries", add.length);
      for(i = 0; i < add.length; i++)
	{
        el = this.create_row(add[i].v);
//        console.trace();
	  
	this.div.insertBefore(el, this.div.childNodes[idx]);
        idx++;

        // Select first entry
        if(!app_state.sel_id && this.div.childNodes.length)
          this.select_entry(this.div.childNodes[0].dataset.id);

	}
      }
    else if(is_object(add))
      {
      el = this.create_row(add);

      ret.div.insertBefore(el, ret.div.childNodes[idx]);
		  
      // Select first entry
      if(!app_state.sel_id && this.div.childNodes.length)
        this.select_entry(this.div.childNodes[0].dataset.id);

      }

    if(last) // Last
      {
      
      if(app_state.sel_id)
        {
        this.select_entry_by_id(app_state.sel_id);
	  
        if((div = this.div_by_id(app_state.sel_id)))
	  {
          if(current_widget == this)
            this.scroll(div);
          }
        else
          {
	  delete app_state.sel_id;
	  }
	}
      else
        console.log("Selection not set");
      }
    if(current_widget == this)
      this.update_footer();

	
    };
      
  ret.handle_msg = function(msg)
    {
    var div;
    var foot;

    switch(msg.ns)
      {
      case BG_MSG_NS_DB:
        {
        switch(msg.id)
          {
          case BG_MSG_DB_SPLICE_CHILDREN:
          case BG_RESP_DB_BROWSE_CHILDREN:
	    {
	    var add = null;
	    var el;
            var i;
	    var ctx_id;
	    var cnt_id;
            var last = msg_is_last(msg);
            if(!this.container)
	      return 1;

//            console.log("Browse children response 1: " + JSON.stringify(msg));
            
		
            ctx_id = dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID);
            cnt_id = obj_get_id(this.container);
		    
            /* Check if entries in the directoy above us were deleted */
            if((msg.id == BG_MSG_DB_SPLICE_CHILDREN) &&
	       (is_ancestor(ctx_id, cnt_id)) &&
	       (msg.args[1].v > 0))
	      {
              var msg1;
              /* Need to check if our ancestor was deleted */

              this.ancestor = new Object();
	      this.ancestor.id = ctx_id;
	      this.ancestor.cnt_id = cnt_id;

//              alert("this.ancestor.id " + this.ancestor.id + " this.ancestor.cnt_id " + this.ancestor.cnt_id);

              msg1 = msg_create(BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
              dict_set_string(msg1.header, GAVL_MSG_CONTEXT_ID, ctx_id);
              dict_set_string(msg1.header, GAVL_MSG_CLIENT_ID, client_id);
              msg_send(msg1, server_connection);

	      }

            if((msg.id == BG_RESP_DB_BROWSE_CHILDREN) &&
               (this.ancestor) && (this.ancestor.id == ctx_id) &&
               (msg.args[2] && msg.args[2].v))
	      {
              var obj_id;
              add = msg.args[2].v;
		
//              console.log("BG_RESP_DB_BROWSE_CHILDREN " + ctx_id + " " + this.ancestor + " " + this.ancestor.id);
		
              cnt_id = obj_get_id(this.container);
              if(is_array(add))
                {
                for(i = 0; i < add.length; i++)
	          {
                  obj_id = obj_get_id(add[i].v);

                  if((obj_id == this.ancestor.cnt_id) ||
                     is_ancestor(obj_id, this.ancestor.cnt_id))
                    {
                    this.ancestor.found = true;
                    break;
                    }
                      
                  }
                }
              else if(is_object(add))
                {
                obj_id = obj_get_id(add);

                if((obj_id == this.ancestor.cnt_id) ||
                   is_ancestor(obj_id, this.ancestor.cnt_id))
                  {
                  this.ancestor.found = true;
                  }
                   
                }
                      

              if(last)
                {

                if(!this.ancestor.found)
                  {
                  /* Change to parent */
                  console.log("Ancestor deleted " + this.ancestor.id);
                  widgets.browser.jump_to(this.ancestor.id);
		  }
                    
                delete this.ancestor;
                }

 	      return 1;
	      }
 
		
            if(cnt_id != ctx_id)
 	      return 1;

            if(msg.args[2])
              add = msg.args[2].v;

//            console.log("Browse children response 2: " + JSON.stringify(add));

            this.splice(last, msg.args[0].v, msg.args[1].v, add);

            obj_splice_children(this.container, msg.args[0].v, msg.args[1].v, msg.args[2]);

            if(msg.id == BG_RESP_DB_BROWSE_CHILDREN)
              {

	      if(last)
                { 
                this.container.have_children = true;
                load_progress(-1.0);
		}
              else if(this.num_children > 0)
	        {
                load_progress( this.div.childNodes.length / this.num_children );
	        }

	      }
            update_nav_popup();
            }
            break;
          case BG_RESP_DB_BROWSE_OBJECT:
            this.set_container(msg.args[0].v);
            break;
          case BG_MSG_DB_OBJECT_CHANGED:
            {
            var cur_id = null;
            var parent_id;
            var new_obj;
            var id = dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID);

            new_obj = msg.args[0].v;

            if(this.container)
	      cur_id = obj_get_id(this.container);

//            console.log("BG_MSG_DB_OBJECT_CHANGED: id: " + id + " cur_id: " + cur_id);

            // .update_row

            if(get_parent_id(id) == cur_id)
              {
              var div;
              var obj;
              if((div = this.div_by_id(id)) &&
	         (obj = this.obj_by_id(id)))
                {
                merge_objects(msg.args[0].v, obj);
                this.update_row(div, obj);
                if(this == current_widget)
                  adjust_header_footer(this.div);
                }
              }
            else if(id == cur_id)
	      {
              merge_objects(msg.args[0].v, this.container);
	      if(this == current_widget)
		{
                set_header(this.container);
                adjust_header_footer(this.div);
		}
              }
            break;
	    }
	  }
	}
      	  
        break;
    case BG_MSG_NS_PLAYER:
      switch(msg.id)
        {
        case BG_PLAYER_MSG_CLEAR_QUEUE:
          break;
	}
      break;

    case GAVL_MSG_NS_GUI:
      switch(msg.id)
        {
        case GAVL_MSG_GUI_SWIPE:
          switch(msg.args[0].v)
            {
            case GAVL_MSG_GUI_SWIPE_RIGHT:
              prev_sibling();
              break;
            case GAVL_MSG_GUI_SWIPE_LEFT:
	      next_sibling();
              break;
            case GAVL_MSG_GUI_SWIPE_DOWN:
	      next_sibling();
              break;
	    }
          break;
	}
      break;
      }
    return 1;
    };

  ret.select_entry_by_id = function(id)
    {
    for(i = 0; i < this.div.childNodes.length; i++)
      {
      if(id == this.div.childNodes[i].dataset.id)
        this.div.childNodes[i].dataset.selected = true;
      else
        this.div.childNodes[i].dataset.selected = false;
      }
    }

  ret.set_current_by_hash = function(hash)
    {
    for(i = 0; i < this.div.childNodes.length; i++)
      {
      if(hash == this.div.childNodes[i].dataset.hash)
	{
        this.div.childNodes[i].dataset.current = true;
	this.cur_id = this.div.childNodes[i].dataset.id;
	}
      else
        this.div.childNodes[i].dataset.current = false;
      }
    }
    
  ret.select_entry = function(id)
    {
    let div = null;
    let obj = null;

//  console.log("Select entry " + id + " '" + app_state.sel_id + "'");
//  console.trace();
	
    if(app_state.sel_id && (div = this.div_by_id(app_state.sel_id)))
      {
      if(id == div.dataset.id)
	return;
      }
	
    if(id)
      app_state.sel_id = id;
    else
      app_state.sel_id = "";
	
//    console.log("Select entry 1 " + id + " '" + app_state.sel_id + "'");
    replace_state();
    app_state_apply();
    update_hash();
    update_nav_popup();
    };

  ret.move_selected = function(delta)
    {
    var idx;
    var children;
      
//    console.log("move_selected " + delta + " " + this.container + " " + app_state.sel_id);
	
    if(!this.container || !app_state.sel_id)
      return;

    children = obj_get_children(this.container);
	
    idx = this.idx_by_id(app_state.sel_id);
    if(idx < 0)
      idx = 0;
	
    idx += delta;

    if(idx < 0)
      idx = 0;
    if(idx >= children.length)
      idx = children.length - 1;
	
    this.select_entry(this.div.childNodes[idx].dataset.id);
    };
      
  /* Executed by doubleclick or enter or similar */      
  ret.entry_fire = function(id)
    {
    var obj = this.obj_by_id(id);
    var klass = obj_get_string(obj, GAVL_META_CLASS);

    if(obj_get_int(obj, GAVL_META_LOCKED))
      return;	

//    console.log("entry_fire " + id + " " + klass);
	
    if(klass && klass.startsWith("container"))
      {
      this.jump_to(id);
      }
    else
      {
      if(app_state.id == BG_PLAYQUEUE_ID)
        {
        play_by_id(obj);
	}
      else if(obj_get_string(obj, GAVL_META_CLASS).startsWith(GAVL_META_CLASS_IMAGE))
	{
        console.log("Showing image " + obj_get_id(obj));
        app_state.image_id =  obj_get_id(obj);
        app_state.widget = "imageviewer";
        app_state_apply();
	push_state();
        }
      else
	{
        playqueue_add_album(this.container, true, false);
        play_by_id(obj);
	}
      }
	
    };

  ret.update_footer = function()
    {
    var foot;
    var num_children;
    var idx = -1;

    if(!this.container)
      return;
	
    num_children = this.div.childNodes.length;

    if(app_state.sel_id)
      idx = this.idx_by_id(app_state.sel_id);

    foot = document.getElementById("footer_left");
    clear_element(foot);
    append_dom_text(foot, (idx+1) + "/" + num_children);
    };
    
  ret.set_container = function(obj)
    {
    this.have_container_image = false;

//    console.log("Set container " + obj + " " + this.have_container_image);
      
    if(this.container)
      {
      delete this.container[GAVL_META_CHILDREN];
      clear_element(this.div);
      delete_my_event_handler(this.container, this);
      this.container = null;
      set_wallpaper();
      }
    if(!obj)
      return;
    else if(is_object(obj))
      {
      var klass;
      var icon;
      var span;
      var m;

      this.container = obj;
      m = dict_get_dictionary(this.container, GAVL_META_METADATA);

//      console.log("set_container " + JSON.stringify(this.container));
//      console.trace();
	
      this.set_mode_internal(this.get_browse_mode());
  	
      add_my_event_handler(this.container, this);

      set_header(this.container);

      icon = document.getElementById("header_icon");
      if(icon.firstElementChild &&
         (icon.firstElementChild.tagName.toLowerCase() == "img"))
	this.have_container_image = true;
      else
	this.have_container_image = false;

      set_wallpaper(this.container);
	
      my_get_children(this.container);
      update_nav_popup();
      adjust_header_footer(this.div);
      }
    else // String
      {
//      console.log("set_container, id: " + obj);
      if(obj == BG_PLAYQUEUE_ID)
        {
        this.set_container(playqueue);
        /* Special things for the playqueue:
	   - Select current track if any
	   - Scroll to current track if any
	*/
        }
      else if(widgets.browser.container && (obj_get_id(widgets.browser.container) == obj))
        {
        this.set_container(widgets.browser.container);
//	console.log("blupp 1");
	}
      else if(widgets.browser.container && (get_parent_id(obj) == obj_get_id(widgets.browser.container)))
        {
        this.set_container(this.obj_by_id(obj));
//	console.log("blupp 2");
	}
      else
        {
        my_get_metadata(obj);
//	console.log("blupp 3");
	}
      }
    };

  ret.show = function()
    {
    /* Load object and children */

    if(this.div.dataset.mode == BROWSER_MODE_LIST)
      this.div.style.display = "block";
    else if(this.div.dataset.mode == BROWSER_MODE_TILES)
      this.div.style.display = "grid";
      
    /* set_container() is called by app_state_apply() */

    if(!this.visible)
      {
      key_event_hub.connect(this.key_hdlr);
//      swipe_event_hub.connect(this.hdlr);
      this.visible = true;
      this.update_footer();
      }
    };

  ret.hide = function()
    {
    key_event_hub.disconnect(this.key_hdlr);
//    swipe_event_hub.connect(this.hdlr);
    set_wallpaper();
//    console.log("browser.hide");
    this.visible = false;
    };
      
  ret.div_by_id = function(id)
    {
    var i;
    for(i = 0; i < this.div.childNodes.length; i++)
      {
      if(id == this.div.childNodes[i].dataset.id)
	return this.div.childNodes[i];
      }
    return null;
    };

  ret.obj_by_id = function(id)
    {
    var i;
    var children;

    if(id == BG_PLAYQUEUE_ID)
      {
      return playqueue;
      }
	
    if(!this.container || !(children = obj_get_children(this.container)))
      return null;
	
    for(i = 0; i < children.length; i++)
      {
      if(id == obj_get_id(children[i].v))
	return children[i].v;
      }
    return null;
    };

  ret.idx_by_id = function(id)
    {
    var i;
    for(i = 0; i < this.div.childNodes.length; i++)
      {
      if(id == this.div.childNodes[i].dataset.id)
	return i;
      }
    return -1;
    };
      
  return ret;
  }

/* Settings */

function create_settings() 
  {
  var ret = new Object();
  ret.div = document.getElementById("settings");
  ret.div.settings = ret;

  ret.show = function()
    {
    var obj = new Object();
    var m = new Object();

    this.div.style.display = "block";
    dict_set_string(m, GAVL_META_ICON_URL, "icon-config");
    dict_set_string(m, GAVL_META_TITLE, "Settings");
    dict_set_dictionary(obj, GAVL_META_METADATA, m);
    set_header(obj);
    adjust_header_footer(this.div);
    };

/*
  ret.key_hdlr = new Object();
  ret.key_hdlr.handle_msg = function(evt)
    {
    switch(evt.key)
      {
      case "Escape":
        my_history_back(0);
        break;
      default:
        return 1;
      }
    return 0;
    };
*/
    
  return ret;
  }

/* Player control */

function create_player_control()
  {
  var el;
  var ret = new Object();

  ret.div = document.getElementById("player-controls");
  ret.visible = true;
  ret.idle_counter = 0;
  ret.volume = 0.0;
  ret.init_volume = true;
  ret.init_mute   = true;

  ret.duration = -1.0;
  ret.seeking = false;
      
  ret.controls_button = document.getElementById("controls-button");
  ret.controls_button.onclick = function()
    {
    if(player_control.visible)
      player_control.hide();
    else
      player_control.show();

    if(current_widget && current_widget.div)
      adjust_header_footer(current_widget.div);
    };

  el = document.getElementById("volume-up-button");
  el.onclick = function()
    {
    player_control.volume_up();
    };
      
  el = document.getElementById("volume-down-button");
  el.onclick = function()
    {
    player_control.volume_down();
    };

  el = document.getElementById("volume-mute-button");
  el.onclick = function()
    {
    player.mute();
    };

  el = document.getElementById("volume-slider");
  el.oninput = function()
    {
    if(player)
      player.set_volume(parseFloat(this.value));
    };
/*
  el.onchange = function()
    {
    console.log("onchange " + this.value);
    };
*/
 

  ret.volume_up = function()
    {
    player.set_volume_rel(0.01);
    };

  ret.volume_down = function()
    {
    player.set_volume_rel(-0.01);
    };


  el = document.getElementById("player-slider");
  el.oninput = function()
    {
    player_control.seeking = true;
    if(player_control.duration > 0)
      document.getElementById("player-display").innerHTML = time_to_string(this.value * player_control.duration);
    };

  el.onchange = function()
    {
    if(player)
      player.seek_perc(this.value);
    player_control.seeking = false;
    };
      
  ret.key_hdlr = new Object();
  ret.key_hdlr.handle_msg = function(evt)
    {
    var cmd;
//    console.log("Key pressed " + evt.key);
    switch(evt.key)
      {
      case "Escape":
        history.back();
        break;
      case "MediaPlay":
	player.play();
        break;
/* TODO: Search */
      case "0":
	player.seek_perc(0.0);
        break;
      case "1":
	player.seek_perc(0.1);
        break;
      case "2":
	player.seek_perc(0.2);
        break;
      case "3":
	player.seek_perc(0.3);
        break;
      case "4":
	player.seek_perc(0.4);
        break;
      case "5":
	player.seek_perc(0.5);
        break;
      case "6":
	player.seek_perc(0.6);
        break;
      case "7":
	player.seek_perc(0.7);
        break;
      case "8":
	player.seek_perc(0.8);
        break;
      case "9":
	player.seek_perc(0.9);
        break;
      default:
	return 1;
      }
    stop_propagate(evt);
    return 0;
    };

  ret.set_track = function(obj)
    {
    let el;
    let span;
    let m;
    let klass;
      /* Set current track */
    el = document.getElementById("player-image");

    make_icon(el, obj, true, 256);

    if(!(m = dict_get_dictionary(obj, GAVL_META_METADATA)))
      return;
      
//  console.log("have image " + klass + " " + this.have_container_image);
    el = document.getElementById("player-info");
    clear_element(el);

    if(!(klass = dict_get_string(m, GAVL_META_CLASS)))
      return;

    span = append_dom_element(el, "span");
    span.setAttribute("class", "player-label");
    append_dom_text(span, make_label(m, klass));

    /* Further info */
    append_dom_element(el, "br");
    append_meta_info(el, obj);
    
    };
    
  /* Handle player message */
      
  ret.handle_msg = function(msg)
    {
    var t;
    var val;
//    console.log("Handle msg 1 " + JSON.stringify(msg));
    switch(msg.ns)
      {
      case BG_MSG_NS_STATE:
	switch(msg.id)
          {
	  
          case BG_MSG_STATE_CHANGED:
//            console.log("Handle msg 1 " + JSON.stringify(msg));
            switch(msg.args[1].v)
	      {
	      case BG_PLAYER_STATE_CTX:
                switch(msg.args[2].v)
                  {
                  case BG_PLAYER_STATE_TIME:
//                  console.log("Handle msg 2");
                    if(!this.seeking)
                      {
                      if(msg.args[3].t != "l")
                        break;      
//                      console.log("Got time " + JSON.stringify(msg.args[3].v));
                      document.getElementById("player-display").innerHTML =
		        time_to_string(msg.args[3].v);

                      }
             	    break;
                  case BG_PLAYER_STATE_TIME_PERC:
                    if(msg.args[3].t != "f")
                      break;      
                    if(msg.args[3].v >= 0.0)
                      document.getElementById("player-slider").value = msg.args[3].v;
                    else
                      document.getElementById("player-slider").value = 0.0;
		    
		    break;
	          case BG_PLAYER_STATE_STATUS:
		    {
	            var status_icon = null;
                    switch(msg.args[3].v)
		      {
		      case BG_PLAYER_STATUS_INIT:
		      case BG_PLAYER_STATUS_STOPPED:
			status_icon = "icon-stop";
                        break;
		      case BG_PLAYER_STATUS_PLAYING:
			status_icon = "icon-play";
                        set_fullscreen(1);
                        break;
		      case BG_PLAYER_STATUS_PAUSED:
			status_icon = "icon-pause";
                        break;
		      case BG_PLAYER_STATUS_SEEKING:
		      case BG_PLAYER_STATUS_CHANGING:
		      case BG_PLAYER_STATUS_STARTING:
			status_icon = "icon-transition";
                        break;
		      case BG_PLAYER_STATUS_ERROR:
			status_icon = "icon-warning";
                        break;
                      }
                    if(status_icon)
                      document.getElementById("player-status").setAttribute("class", status_icon);
                    }
                    break;
	          case BG_PLAYER_STATE_MODE:
		    {
	            var mode_icon = null;
                    switch(msg.args[3].v)
		      {
                      case BG_PLAYER_MODE_NORMAL: //!< Normal playback
                        mode_icon = "icon-arrow-right";
                        break;
	              case BG_PLAYER_MODE_REPEAT:           //!< Repeat current album
                        mode_icon = "icon-repeat";
                        break;
		      case BG_PLAYER_MODE_SHUFFLE:       //!< Shuffle (implies repeat)
                        mode_icon = "icon-shuffle";
                        break;
		      case BG_PLAYER_MODE_ONE:           //!< Play one track and stop
                        mode_icon = "icon-no-advance";
                        break;
		      case BG_PLAYER_MODE_LOOP:          //!< Loop current track
                        mode_icon = "icon-repeat-1";
                        break;
                      }
                    if(mode_icon)
		      document.getElementById("mode-button").setAttribute("class", mode_icon);
                    }
                    break;
                  case BG_PLAYER_STATE_CURRENT_TRACK:
		    {
                    var klass;
                    
                    var child;
                    var m;
//                    console.log("Got current track " + JSON.stringify(msg.args[3].v));

                    this.current_track = msg.args[3].v;

//                    widgets.player.set(this.current_track);
			
                    if(!(m = dict_get_dictionary(this.current_track, GAVL_META_METADATA)))
                      break;

                    if((klass = dict_get_string(m, GAVL_META_CLASS)) &&
		       klass.startsWith("item.video"))
                      has_video = true;
                    else
                      has_video = false;

                    set_fullscreen(1);
                    
                    this.duration = dict_get_long(m, GAVL_META_APPROX_DURATION);
		      
                    current_track_id = dict_get_string(m, GAVL_META_ID);
                    widgets.browser.set_current(dict_get_string(m, GAVL_META_HASH));

                    if(playqueue_is_current())
		      {
                      widgets.browser.select_entry(current_track_id);
		      }
                    else if((app_state.widget == "iteminfo") &&
			    (app_state.id == BG_PLAYQUEUE_ID))
		      {
                      widgets.iteminfo.update();
		      }
                    this.set_track(this.current_track);
		    }
                    break;
	          case BG_PLAYER_STATE_VOLUME:
		    player_control.set_volume(msg.args[3].v);
		    break;
                  case BG_PLAYER_STATE_QUEUE_IDX:
                    queue_idx = msg.args[3].v;
                    break;
                  case BG_PLAYER_STATE_QUEUE_LEN:
                    queue_len = msg.args[3].v;
                    break;
                  case BG_PLAYER_STATE_MUTE:
		    player_control.set_mute(msg.args[3].v);
		    break;
		  
                  }
                break;
              }
	    break;
          }
        break;

      case BG_MSG_NS_DB:
	switch(msg.id)
          {
          /* Playqueue events */
          case BG_MSG_DB_SPLICE_CHILDREN:
          case BG_RESP_DB_BROWSE_CHILDREN:
	    {
            let idx = msg.args[0].v;
            let del = msg.args[1].v;
            let add = msg.args[2].v;

            if(del)
              playqueue_children.splice(idx, del);

            if(is_array(msg.args[2].v))
              {
	      let i;
	      for(i = 0; i < msg.args[2].v.length; i++)
		{
                playqueue_children.splice(idx+i, 0, msg.args[2].v[i]);
		}
              }
            else
              playqueue_children.splice(idx, 0, msg.args[2]);
	      
            if((current_widget == widgets.browser) &&
               (widgets.browser.container == playqueue))
	      {
              widgets.browser.handle_msg(msg);
	      }
            break;
	    }
          case BG_MSG_DB_OBJECT_CHANGED:
          case BG_RESP_DB_BROWSE_OBJECT:
	    {
            var cur_id = null;
            var parent_id;
            var obj;
            var div;

            var id = dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID);

//            console.log("BG_MSG_DB_OBJECT_CHANGED " + " " + JSON.stringify(msg.args[0].v));

	      
            if((current_widget == widgets.browser) && (widgets.browser.container))
              cur_id = obj_get_id(widgets.browser.container);

            if(id == BG_PLAYQUEUE_ID)
              obj = playqueue;
            else
	      obj = obj_get_child_by_id(playqueue, id);

            if(!obj)
	      return;
	      
            merge_objects(msg.args[0].v, obj);

            // .update_row
            if((div = widgets.browser.div_by_id(id)))
              {
              widgets.browser.update_row(div, obj);
              if(widgets.browser == current_widget)
                 adjust_header_footer(widgets.browser.div);
              }
            else if(id == cur_id)
	      {
              console.log("BG_MSG_DB_OBJECT_CHANGED " + " " + JSON.stringify(msg.args[0].v));
              set_header(obj);
              if(widgets.browser == current_widget)
                adjust_header_footer(widgets.browser.div);
              }
            break;
	    }
	  }
          break;	  

      }
    };
      
      
      
  ret.show = function()
    {
    this.div.style.display = "block";
    this.visible = true;
    key_event_hub.connect(this.key_hdlr);

    if(cfg.playercontrol_visible != true)
      {
      cfg.playercontrol_visible = true;
      cfg_changed = true;
      }

    };
      
  ret.hide = function()
    {
    this.div.style.display = "none";
    this.visible = false;
    key_event_hub.disconnect(this.key_hdlr);

    if(cfg.playercontrol_visible != false)
      {
      cfg.playercontrol_visible = false;
      cfg_changed = true;
      }

    };

  ret.set_volume_internal = function(vol, show)
    {
    var icon;

    if(this.init_volume)
      this.init_volume = false;
    else if(!this.visible && show)
      this.show();

    document.getElementById("volume-slider").value = vol;
    this.volume = vol;
    if(this.volume < 1.0/3.0)
      icon = "icon-volume-min";
    else if(this.volume < 2.0/3.0)
      icon = "icon-volume-mid";
    else
      icon = "icon-volume-max";
        
    this.idle_counter = 0;
    };

  document.getElementById("play-button").onclick = function()  { player.play(); };
  document.getElementById("next-button").onclick = function()  { player.next(); };
  document.getElementById("prev-button").onclick = function()  { player.prev(); };
  document.getElementById("stop-button").onclick = function()  { player.stop(); };
  document.getElementById("pause-button").onclick = function() { player.pause(); };
  document.getElementById("mode-button").onclick = function()  { player.next_mode(); };
      
  ret.set_volume = function(vol) {
  this.set_volume_internal(vol, true);
  };
      

      
  ret.set_mute = function(mute)
    { 
    var el;	

    if(this.init_mute)
      this.init_mute = false;
    else if(!this.visible)
      this.show();

    el = document.getElementById("volume-mute-button");

    if(mute)
      {
      el.dataset.mute = "true";
      }
    else
      {
      this.set_volume_internal(this.volume, false);
      el.dataset.mute = "false";
      }
    this.idle_counter = 0;
    };

      
  player_control = ret;
  }

/* Navigation popup */

function create_nav_popup()
  {
  var el;
  var ret = new Object();

  ret.div = document.getElementById("nav-popup");
  ret.visible = false;
  ret.idle_counter = 0;
  
  ret.left_button = document.getElementById("nav-left");
  ret.left_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    my_history_go(-1);
    };

  ret.right_button = document.getElementById("nav-right");
  ret.right_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    my_history_go(1);
    };

  ret.up_button = document.getElementById("nav-up");
  ret.up_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    if(current_widget.move_selected)
      current_widget.move_selected(-1);
    };

  ret.down_button = document.getElementById("nav-down");
  ret.down_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    if(current_widget.move_selected)
      current_widget.move_selected(1);
    };

  ret.enter_button = document.getElementById("nav-enter");
  ret.enter_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    if(current_widget.container && app_state.sel_id && current_widget.entry_fire)
      current_widget.entry_fire(app_state.sel_id);
    };

  ret.prev_button = document.getElementById("nav-prev");
  ret.prev_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    prev_sibling();
    };

  ret.next_button = document.getElementById("nav-next");
  ret.next_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    next_sibling();
    };

  ret.levelup_button = document.getElementById("nav-levelup");
  ret.levelup_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    if(current_widget.change_up)
      current_widget.change_up();
    };

  ret.home_button = document.getElementById("nav-home");
  ret.home_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    widgets.browser.jump_to("/");
    };

  ret.player_button = document.getElementById("nav-player");
  ret.player_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    goto_current_track();
    };

  ret.browser_button = document.getElementById("nav-browser");
  ret.browser_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    widgets.browser.jump_to();
    };

  ret.settings_button = document.getElementById("nav-settings");
  ret.settings_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    show_widget("settings");
    };

  ret.log_button = document.getElementById("nav-log");
  ret.log_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    show_widget("logviewer");
    };
    
  ret.search_button = document.getElementById("nav-search");
  ret.search_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    search_popup.show();
    };

  ret.info_button = document.getElementById("nav-info");
  ret.info_button.onclick = function()
    {
    nav_popup.idle_counter = 0;
    show_info();
    };


  ret.fav_button = document.getElementById("nav-fav");
  ret.fav_button.onclick = function()
    {
    copy_to_favorites();
    };

  ret.visualize_button = document.getElementById("nav-visualization");
  ret.visualize_button.onclick = function()
    {
    next_visualization();
    };

      
  ret.show = function()
    {
    hide_current_menu();
	
    this.div.style.display = "block";
    this.visible = true;

    setTimeout(function(){ nav_popup.check_idle(); }, 1000);
    this.idle_counter = 0;

    current_menu = this;
    };
      
  ret.hide = function()
    {
    this.div.style.display = "none";
    this.visible = false;
    current_menu = null;
    };

  ret.check_idle = function()
    {
    if(!this.visible)
      return false;

    this.idle_counter++;

    if(this.idle_counter >= 5)
      {
      this.hide();
      return false;
      }
	
    setTimeout(function(){ nav_popup.check_idle(); }, 1000);
    return true;
    };

  ret.nav_button = document.getElementById("nav-button");
  ret.nav_button.onclick = function()
    {
    if(nav_popup.visible)
      nav_popup.hide();
    else
      nav_popup.show();
    };
      
  nav_popup = ret;
  }

/* Search popup */

function search_match(val, search_str)
  {
  var str;

  if(!(str = obj_get_string(val.v, GAVL_META_SEARCH_TITLE)) &&
     !(str = obj_get_string(val.v, GAVL_META_TITLE)) &&
     !(str = obj_get_string(val.v, GAVL_META_LABEL)))
     return false;

  if(str.toLowerCase().indexOf(search_str.toLowerCase()) >= 0)
    return true;

  return false;
  }

function create_search_popup()
  {
  var ret = new Object();

  ret.div = document.getElementById("search-popup");
  ret.visible = false;
  ret.input = document.getElementById("search-input");
     
  ret.key_hdlr = new Object();
  ret.key_hdlr.input = ret.input;
  ret.key_hdlr.handle_msg = function(evt)
    {
    var children;
    var str;
    var sel_idx;
    var i;
    switch(evt.key)
      {
      case "ArrowDown":
        {
        /* Next match */
        children = obj_get_children(widgets.browser.container);

        sel_idx = -1;

        if(app_state.sel_id)
          sel_idx = widgets.browser.idx_by_id(app_state.sel_id);

        for(i = sel_idx+1; i < children.length; i++)
          {
          if(search_match(children[i], this.input.value))
            {
            widgets.browser.select_entry(obj_get_id(children[i].v));
            return 1;
            }
          }

        for(i = 0; i < sel_idx; i++)
          {
          if(search_match(children[i], this.input.value))
            {
            widgets.browser.select_entry(obj_get_id(children[i].v));
            return 1;
            }
          }
        }
        break;
      case "ArrowUp":
        {
        /* Previous match */
        children = obj_get_children(widgets.browser.container);

        sel_idx = -1;

        if(app_state.sel_id)
          sel_idx = widgets.browser.idx_by_id(app_state.sel_id);

        for(i = sel_idx-1; i >= 0; i--)
          {
          if(search_match(children[i], this.input.value))
            {
            widgets.browser.select_entry(obj_get_id(children[i].v));
            return 1;
            }
          }

        for(i = children.length - 1; i >= sel_idx; i--)
          {
          if(search_match(children[i], this.input.value))
            {
            widgets.browser.select_entry(obj_get_id(children[i].v));
            return 1;
            }
          }
        }
        break;
      case "Escape":
        search_popup.hide();
        break;
      case "Enter":
        search_popup.hide();
        if(!app_state.sel_id)
          widgets.browser.entry_fire(app_state.sel_id);
        break;
      default:
        return 0;
      }
    stop_propagate(evt);
    return 0;
    };
      
  ret.input.oninput = function()
    {
    var str;         
    var children = obj_get_children(widgets.browser.container);

    if(!this.value.length)
      return;

    for(i = 0; i < children.length; i++)
      {
      if(search_match(children[i], this.value))
        {
        widgets.browser.select_entry(obj_get_id(children[i].v));
        break;
        }
      }
    };
      
  ret.show = function()
    {
    this.input.value = "";

    hide_current_menu();
    this.div.style.display = "block";
    this.visible = true;
    current_menu = this;
    this.input.focus();
    key_event_hub.connect(this.key_hdlr);
    key_event_hub.disconnect(widgets.browser.key_hdlr);
    };
      
  ret.hide = function()
    {
    this.div.style.display = "none";
    this.visible = false;
    current_menu = null;

    key_event_hub.disconnect(this.key_hdlr);
    key_event_hub.connect(widgets.browser.key_hdlr);
    };
  search_popup = ret;
  }

function image_viewer_timeout(iv)
  {
  d = new Date();
  if(d.getTime() > iv.off_time)
    {
    console.log("Hide controls");
    iv.div.dataset.controls = false;
    clearInterval(iv.timeout_tag);
    delete iv.timeout_tag;
    }

  }

function create_image_viewer()
  {
  var ret = new Object();

  ret.div = document.getElementById("imageviewer");

  ret.div.iv = ret;    
  ret.img = document.getElementById("imageviewer-img");

  ret.key_hdlr = new Object();
  ret.key_hdlr.iv = ret;
  ret.key_hdlr.handle_msg = function(evt)
    {
    switch(evt.key)
      {
      case "ArrowLeft":
        this.iv.prev();
        break;
      case "ArrowRight":
        this.iv.next();
        break;
      case "Escape":
        {
	my_history_back(false);
/*        app_state.widget = "browser";
	delete app_state.image_id;
	app_state_apply();
	*/
	}
        break;
      default:
        return 0;
      }
    stop_propagate(evt);
    return 0;
    };
    
  ret.show = function()
    {
    this.div.style.display = "block";

    key_event_hub.disconnect(widgets.browser.key_hdlr);
    key_event_hub.connect(this.key_hdlr);
    }

  ret.hide = function()
    {
    this.div.style.display = "none";

    key_event_hub.disconnect(this.key_hdlr);
    key_event_hub.connect(widgets.browser.key_hdlr);
    };
    
  ret.set_image_internal = function(obj)
    {
    var m = dict_get_dictionary(obj, GAVL_META_METADATA); 
    var image_uri;
    var arr;
	
    if(!is_object(obj))
      {
      console.log("set_image_internal " + obj);

      if(obj == this.next_id)
        {

	}
      else if(obj == this.prev_id)
        {

	}

      my_get_metadata(obj);      
      this.show_controls();
      }
    else
      {
//      console.log("Got image object 1 " + JSON.stringify(obj));
      if(obj_get_id(obj) == app_state.image_id)
        {
//        console.log("Got image object 2");
        if((image_uri = dict_get_dictionary(m, GAVL_META_SRC)) ||
            ((arr = dict_get_array(m, GAVL_META_SRC))) &&
           (image_uri = get_image_uri_max(arr, -1, -1)) &&
	   (image_uri = image_uri.v))
          {
//          console.log("Got image " + dict_get_string(image_uri, GAVL_META_URI));
          this.img.setAttribute("src", dict_get_string(image_uri, GAVL_META_URI));
          }
        this.obj = obj;
        }
      let label = obj_get_string(obj, GAVL_META_LABEL);
      clear_element(this.label);
      if(label)
        append_dom_text(this.label, label);
      }
    }

  ret.show_controls = function()
    {
    var d;
    this.div.dataset.controls = true;
    if(!this.timeout_tag)
      this.timeout_tag = setInterval(image_viewer_timeout, 100, this);

    d = new Date();
    this.off_time = d.getTime() + 3 * 1000;
    }
    
  ret.div.onclick = function()
    {
    this.iv.show_controls();
    }
    
  ret.prev = function()
    {
    var id;
    if(this.obj && (id = obj_get_prev_sibling(this.obj)))
      {
      app_state.image_id = id;
      app_state_apply();
      }
      
    }

  ret.next = function()
    {
    var id;
    if(this.obj && (id = obj_get_next_sibling(this.obj)))
      {
      app_state.image_id = id;
      app_state_apply();
      }
    }

  let button = document.getElementById("imageviewer-prev");
  button.iv = ret;
  button.onclick = function() { this.iv.prev(); };

  button = document.getElementById("imageviewer-next");
  button.iv = ret;
  button.onclick = function() { this.iv.next(); };

  ret.label = document.getElementById("imageviewer-label");
    
  return ret;
   
  }

function my_log(str)
  {
  var div;
  var span;
  var message;
  var now = new Date();

  message = "[" + now.toLocaleString() + "] " + str;
    
  div = widgets.logviewer.div;

  if(div.children.length > 0)
    append_dom_element(div, "br");
  span = append_dom_element(div, "span");
  append_dom_text(span, message);
  
  }

function create_log_viewer()
  {
  var ret = new Object();
  ret.div = document.getElementById("logviewer");

  ret.key_hdlr = new Object();
  ret.key_hdlr.lv = ret;
  ret.key_hdlr.handle_msg = function(evt)
    {
    switch(evt.key)
      {
      case "ArrowUp":
//        this.iv.prev();
        break;
      case "ArrowDown":
//        this.iv.next();
        break;
      case "Escape":
        {
	my_history_back(false);
/*        app_state.widget = "browser";
	delete app_state.image_id;
	app_state_apply();
	*/
	}
        break;
      default:
        return 0;
      }
    stop_propagate(evt);
    return 0;
    };

  ret.show = function()
    {
    var obj = new Object();
    var m = new Object();

    this.div.style.display = "block";

    key_event_hub.disconnect(widgets.browser.key_hdlr);
    key_event_hub.connect(this.key_hdlr);
    adjust_header_footer(this.div);

    dict_set_string(m, GAVL_META_ICON_URL, "icon-log");
    dict_set_string(m, GAVL_META_TITLE, "Log messages");
    dict_set_dictionary(obj, GAVL_META_METADATA, m);
    set_header(obj);
    adjust_header_footer(this.div);
    }

  ret.hide = function()
    {
    this.div.style.display = "none";

    key_event_hub.disconnect(this.key_hdlr);
    key_event_hub.connect(widgets.browser.key_hdlr);
    };
   
  return ret;
  }

/* Help screen */

function create_help_screen()
  {
  var ret = new Object();

  ret.div = document.getElementById("help-screen");
  ret.visible = false;

  ret.show = function()
    {
    var obj = new Object();
    var m = new Object();

    hide_current_menu();
    this.div.style.display = "block";
    this.visible = true;
    	
    dict_set_string(m, GAVL_META_ICON_URL, "icon-help");
    dict_set_string(m, GAVL_META_TITLE, "Help");
    dict_set_dictionary(obj, GAVL_META_METADATA, m);
    set_header(obj);

//    console.log("Show help screen");
    adjust_header_footer(this.div);
    clear_element(document.getElementById("footer_left"));
    };
      
  ret.hide = function()
    {
    this.div.style.display = "none";
    this.visible = false;
    current_menu = null;
    console.log("Hide help screen");
    };

      
  help_screen = ret;
  return ret;
  }

/* Iteminfo */

function create_iteminfo() 
  {
  var ret = new Object();
      
  ret.div = document.getElementById("iteminfo");
  ret.div.info = ret;
  ret.div.dataset.vcenter = "true";

  ret.next = function()
    {
    if(app_state.id == BG_PLAYQUEUE_ID)
      return;    

    var v = obj_get_next_sibling(this.obj);
    if(!v)
      return;	    
    my_history_pop();
    app_state.info_id = v;
    app_state_apply();
    update_hash();
    }

  ret.prev = function()
    {
    if(app_state.id == BG_PLAYQUEUE_ID)
      return;    
    var v = obj_get_prev_sibling(this.obj);
    if(!v)
      return;	    
    my_history_pop();
    app_state.info_id = v;
    app_state_apply();
    update_hash();
    }

  /* Metadata */
  ret.metatable = document.getElementById("iteminfo-table");
  ret.image = document.getElementById("iteminfo-image");
    
  ret.key_hdlr = new Object();
  ret.key_hdlr.info = ret;
  ret.key_hdlr.handle_msg = function(evt)
    {
    var v;          
//    console.log("Iteminfo Key pressed " + evt.key);
    switch(evt.key)
      {
      case "ArrowLeft":
        console.log("ArrowLeft " + obj_get_prev_sibling(this.info.obj));

        if(evt.altKey)
          {
          my_history_back(0);
          }

        if(evt.ctrlKey)
          {
          this.info.prev();
	  }
        break;
      case "ArrowRight":

//        console.log("ArrowRight " + obj_get_next_sibling(this.info.obj));

        if(evt.ctrlKey)
	  {
          this.info.next();
	  }
        break;
      case "Enter":

        if((app_state.id != BG_PLAYEUEUE_ID) &&
	   (v = obj_get_string(this.info.obj, GAVL_META_CLASS)) &&
	   v.startsWith("item.") &&
	   (v = obj_get_id(this.info.obj)))
	  widgets.browser.entry_fire(v);
        break;
      default:
	return 1; // Propagate further
      }
    stop_propagate(evt);
    return 1;
    };

  ret.append_meta_tag = function(m, tag)
    {
    if(m[tag + "Container"])
      {
      var tr;
      var td;
      var i;
      var a;
      var icon = get_metatag_icon(tag);
      var str_arr;
      var id_arr;

      if(tag == GAVL_META_YEAR)
        {
        var val = new Object();
        str_arr = new Array();

        var year = dict_get_string(m, GAVL_META_YEAR);
        if(!year)
          {
          year = dict_get_string(m, GAVL_META_DATE);
          if(year)
            year = year.substring(0, 4);  
          }
        if(!year || (year == "9999"))
          return;

        value_set_string(val, year)
	    
        str_arr.push(val);
    
        id_arr = dict_get_array(m, tag + "Container");
	}
      else
        {
        str_arr = dict_get_array(m, tag);
        id_arr = dict_get_array(m, tag + "Container");
        }
	  
      if(str_arr.length == id_arr.length)
        {
        tr = append_dom_element(this.metatable, "tr");

        if(icon)
          info_table_append_icon(tr, icon);

        td = append_dom_element(tr, "td");
        if(!icon)
          td.setAttribute("colspan", "2");
	    
        for(i = 0; i < str_arr.length; i++)
	  {
          if(i)
            {
            append_dom_text(td, ", ");
	    }

          a = append_dom_element(td, "a");
          a.setAttribute("href", "#");
          a.dataset.id = id_arr[i].v;
          a.onclick = function(evt)
	   {
	   console.log("Jump to " + this.dataset.id);
	   widgets.browser.jump_to(this.dataset.id);
	   stop_propagate(evt);
	   };
	  
          append_dom_text(a, str_arr[i].v);
	  }
        return;
	}
	  
//      console.log("Got container links " + tag + "Container");
      }
    info_table_append_dict(this.metatable, m, tag);
    };
      
  ret.set = function(obj)
    {
    var klass;
    var plot;
    var v;
    var header;
    var m;

    clear_element(this.metatable);
    clear_element(this.image);

//    console.log("iteminfo.set " + JSON.stringify(obj));
//    console.trace();
	
    this.obj = obj;

    /* Image */
    make_image(this.div, this.image, obj);

    m = dict_get_dictionary(obj, GAVL_META_METADATA);

    klass = dict_get_string(m, GAVL_META_CLASS);
	
    /* */

    if(!info_table_append_title_dict(this.metatable, obj, GAVL_META_TITLE))
      info_table_append_title_dict(this.metatable, obj, GAVL_META_LABEL);
	
    this.append_meta_tag(m, GAVL_META_AUTHOR);
       	
    this.append_meta_tag(m, GAVL_META_ARTIST);
    this.append_meta_tag(m, GAVL_META_ALBUM);
    this.append_meta_tag(m, GAVL_META_GENRE);
    this.append_meta_tag(m, GAVL_META_COUNTRY);

    if((v = dict_get_string(m, GAVL_META_DATE)))
      {
      this.append_meta_tag(m, GAVL_META_DATE);
      }
    else
      {
      this.append_meta_tag(m, GAVL_META_YEAR);
      }

    this.append_meta_tag(m, GAVL_META_DIRECTOR);
    this.append_meta_tag(m, GAVL_META_ACTOR);
    this.append_meta_tag(m, GAVL_META_SUBTITLE_LANGUAGES);
    this.append_meta_tag(m, GAVL_META_AUDIO_LANGUAGES);
        

    switch(klass)
      {
      case GAVL_META_CLASS_MUSICALBUM:
        info_table_append(this.metatable, dict_get_int(m, GAVL_META_NUM_CHILDREN) +" track(s)");
        break;
      case GAVL_META_CLASS_TV_SEASON:
        info_table_append(this.metatable, dict_get_int(m, GAVL_META_NUM_CHILDREN) +" episodes(s)");
        break;
      case GAVL_META_CLASS_TV_SHOW:
        info_table_append(this.metatable, dict_get_int(m, GAVL_META_NUM_CHILDREN) +" season(s)");
        break;
      case GAVL_META_CLASS_PLAYLIST:
        info_table_append(this.metatable, dict_get_int(m, GAVL_META_NUM_CHILDREN) +" item(s)");
        break;
      case null:
        break;
      default:
        if(klass.startsWith("container."))
          info_table_append(this.metatable, dict_get_int(m, GAVL_META_NUM_CHILDREN) +" objects(s)");
      }

    this.append_meta_tag(m, GAVL_META_APPROX_DURATION);
    this.append_meta_tag(m, GAVL_META_SRC);

    this.append_meta_tag(m, GAVL_META_PLOT);

    set_wallpaper(obj);
    };

      
  ret.update = function()
    {
    var obj;

    if(app_state.id == BG_PLAYQUEUE_ID)
      {
      obj = player_control.current_track;
      }
    else
      {
      obj = widgets.browser.obj_by_id(app_state.info_id);
      }

    if(!obj)
      {
//      console.log("Got no object");
      return;
      }
    this.set(obj);
    }
      
  ret.show = function()
    {
    var obj = null;
    var header;
    var m;

//    console.log("iteminfo.show()");
	
    this.div.style.display = "block";

    key_event_hub.connect(this.key_hdlr);


    /* Header */
	
    header = new Object();
    m = new Object();
    dict_set_dictionary(header, GAVL_META_METADATA, m);
        
    if(app_state.id == BG_PLAYQUEUE_ID)
      {
      if(player)
        {
        dict_set_string(m, GAVL_META_ICON_URL, player.icon);
//      console.log("player icon: " + player.icon);
        dict_set_string(m, GAVL_META_TITLE, player.name);
        }
      else
        dict_set_string(m, GAVL_META_TITLE, "No player");
      }
    else
      {
      dict_set_string(m, GAVL_META_ICON_URL, "icon-info");
      dict_set_string(m, GAVL_META_TITLE, "Media info");
      } 
	
    set_header(header, true, true);
    adjust_header_footer(this.div);

    this.update();        
    };
      
  ret.hide = function()
    {
    key_event_hub.disconnect(this.key_hdlr);
    set_wallpaper();
    this.div.style.display = "none";
    };
      
  return ret;
  }



function init_complete()
  {
  var i;
  var key;

//  console.log("init_complete() " + !!devices + " " + !!mimetypes + " " + !!have_cfg);
  
  if(!devices || !mimetypes || !have_cfg || !have_root)
    return;

  my_history_init();
  cfg_info_init();
  cfg_init();

  init_swipe_events();
      
  /* Set defaults */

  if(!cfg.renderer)
    {
    for(i = 0; i < devices.length; i++)
      {
      var val_i = dict_get_int(devices[i].v, "Type");
	
      if(val_i != 2)
	continue;

      if(!(cfg.renderer = dict_get_string(devices[i].v, "proxy")))
        cfg.renderer = dict_get_string(devices[i].v, GAVL_META_URI);

      if(cfg.renderer)
	break;
      }
    if(!cfg.renderer)
      alert("no Renderer found, playback will be impossible");
    }
      
  /* Apply parameters */

  for(key in cfg)
    {
    cfg_set_parameter(key, cfg[key]);
    }

  /* Load initial state */
  app_state_apply();
  push_state();
  }

function global_mouse_down_handler(evt)
  {
  if(nav_popup.visible)
    {
    var div = document.getElementById("nav-popup");

    var rect = div.getBoundingClientRect();
	
    if((evt.clientX < rect.left) ||
       (evt.clientX >= rect.right) || 
       (evt.clientY < rect.top) ||
       (evt.clientY >= rect.bottom))
      {
      nav_popup.hide();
      stop_propagate(evt);
      }
    }
  }

function global_init()
  {
  var el;
  var cb;
  var browser_funcs = new Object();

    
  app_state = new Object();
  app_state.widget = "browser";
  app_state.id = "/";
  app_state.sel_id = "";
      
  widgets = new Object();
      
  init_key_events();
  create_player_control();
  create_nav_popup();
  create_search_popup();

  widgets.logviewer     = create_log_viewer();

  server_uri = "ws://" +  window.location.host + "/ws/" + GAVL_META_CLASS_BACKEND_MDB;
  server_connection_init();
    
  widgets.browser   = create_browser();

  widgets.settings      = create_settings();
  widgets.iteminfo      = create_iteminfo();
  widgets.help          = create_help_screen();

  widgets.imageviewer   = create_image_viewer();
     
  /* */
      
  init_client_id();

  /* load config */
  cb = new Object();
  cb.func = function(loaded_cfg)
    {
    cfg = loaded_cfg;
    have_cfg = true;
    init_complete();
    console.log("Got config");
    };

  cb.fail = function()
    {
    have_cfg = true;
    console.log("loading cfg failed");
    init_complete();
    };
      
  datastore_get("cfg", cb);

  add_event_handler(window, "resize", resize_callback);

  window.setInterval(timeout_func, 2000);
    
  do_clear_wallapper();

  add_event_handler(document, "mousedown", global_mouse_down_handler);

  el = document.getElementById("header-play");

  el.onclick = function()
    {
    if((current_widget == widgets.browser) &&
       (app_state.id != BG_PLAYQUEUE_ID) && widgets.browser.container)
      {
      playqueue_add_album(widgets.browser.container, true, true);
      }
    };

  el = document.getElementById("header-add");

  el.onclick = function()
    {
    if((current_widget == widgets.browser) &&
       (app_state.id != BG_PLAYQUEUE_ID) && widgets.browser.container)
      {
      playqueue_add_album(widgets.browser.container, false, false);
      }
    };

  root_elements = new Array();
  
  playqueue_create();
  };
