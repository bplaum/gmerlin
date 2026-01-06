/* Types */

const GAVL_TYPE_INT         = "i";
const GAVL_TYPE_LONG        = "l";
const GAVL_TYPE_FLOAT       = "f";
const GAVL_TYPE_STRING      = "s";
const GAVL_TYPE_AUDIOFORMAT = "af";
const GAVL_TYPE_VIDEOFORMAT = "vf";
const GAVL_TYPE_COLOR_RGB   = "rgb";
const GAVL_TYPE_COLOR_RGBA  = "rgba";
const GAVL_TYPE_POSITION    = "pos";
const GAVL_TYPE_DICTIONARY  = "d";
const GAVL_TYPE_ARRAY       = "a";

const GAVL_TIME_SCALE = 1000000.0;


/* Portable way to create an XMLHttpRequest */
function make_http_request()
  {
  var xmlHttp = null;
  try
    {
      // Mozilla, Opera, Safari sowie Internet Explorer (ab v7)
      xmlHttp = new XMLHttpRequest();
    }
  catch(e)
    {
    try
      {
      // MS Internet Explorer (ab v6)
	xmlHttp  = new ActiveXObject("Microsoft.XMLHTTP");
      }
    catch(e)
      {
      try
	{
        // MS Internet Explorer (ab v5)
        xmlHttp  = new ActiveXObject("Msxml2.XMLHTTP");
        }
      catch(e)
	{
        xmlHttp  = null;
        }
      }
    }

  xmlHttp.my_open = function(method, url)
    {
    this.open(method, url, true);
    };
      
  return xmlHttp;
  }

/* Get the position of an element */
function get_element_position(element)
  {
  var xPosition = 0;
  var yPosition = 0;

  var width = element.offsetWidth;
  var height = element.offsetHeight;
      
  while(element &&
	!isNaN( element.offsetLeft ) &&
	!isNaN( element.offsetTop ))
    {
    xPosition += (element.offsetLeft - element.scrollLeft + element.clientLeft);
    yPosition += (element.offsetTop - element.scrollTop + element.clientTop);
    element = element.offsetParent;
    }
  return { x: xPosition, y: yPosition, w: width, h: height };
  }

/* Resize browser window */
function resize_window(w, h)
  {
  window.resizeTo(
            w + (window.outerWidth - window.innerWidth),
            h + (window.outerHeight - window.innerHeight));
  }

/* Stop propagating an event */
function stop_propagate(e)
  {
  var evt = e ? e:window.event;
  if (evt.stopPropagation)
    evt.stopPropagation();
  if(evt.cancelBubble!=null)
    evt.cancelBubble = true;
  if(evt.preventDefault)
    evt.preventDefault();
  if(evt.cancel != null)
    evt.cancel = true;
  if(window.event)
    evt.returnValue = false;
  }

/**
 * John Resig, explained on Flexible Javascript Events
 */

function add_event_handler(obj, type, fn)
  {
  if(obj.addEventListener)
    {
    obj.addEventListener( type, fn, false );
    }
  else if (obj.attachEvent)
    {
    obj["e"+type+fn] = fn;
    obj[type+fn] = function() { obj["e"+type+fn]( window.event ); };
    obj.attachEvent( "on"+type, obj[type+fn] );
    }
  }

function format_duration_str(dur)
  {
  var idx;
  if((dur == null) || (dur == undefined))
    return "-:--";
  idx = dur.lastIndexOf(".");
  if(idx >= 0)
    dur = dur.substr(0, idx);

  if((dur.charAt(0) == "0") && (dur.charAt(1) != ":"))
    dur = dur.substr(1);
  return dur;
  }

function time_string_to_seconds(dur)
  {
  var i;
  var ret = 0;
  var split;

  if(dur == null)
    return 0.0;
  split = dur.split(":");

  for(i = 0; i < split.length; i++)
    {
    ret *= 60;
    ret += parseFloat(split[i]);
    }
  return ret;
  }

function make_time_string(hours, minutes, seconds)
  {
  var ret;
  if(hours > 0)
    {
    ret = hours.toString() + ':';

    if(minutes < 10)
      ret += "0";
    ret += minutes.toString() + ':';

    if(seconds < 10)
      ret += "0";
    ret += seconds.toString();
    }
  else
    {
    ret = minutes.toString() + ':';
    if(seconds < 10)
      ret += "0";
    ret += seconds.toString();
    }
  return ret;
  }

function seconds_to_time_string(dur)
  {
  var ret;
  var hours;
  var minutes;
  var seconds;

  hours = Math.floor(dur / 3600);
  dur -= hours * 3600;

  minutes = Math.floor(dur / 60);
  dur -= minutes * 60;

  seconds = Math.floor(dur);
  return make_time_string(hours, minutes, seconds);
  }

function time_to_string(time)
  {
  return seconds_to_time_string(time / GAVL_TIME_SCALE );
  }

function time_to_string_local(time)
  {
  var d = new Date(time / 1000);
  return make_time_string(d.getHours(), d.getMinutes(), d.getSeconds());
  }

/* Get file extension */
function get_extension(str)
  {
  var ret = null;
  var idx;
  var idx2;
  var rx;

  idx = str.lastIndexOf(".");
  if(idx < 0)
    return null;

  idx2 = str.lastIndexOf("/");
  if((idx2 < 0) || (idx2 > idx))
    return null;

  ret = str.substr(idx+1);
  if((ret.length < 1) || (ret.length > 4))
    return null;

  rx = new RegExp("^[a-z0-9]+$", "i");
  if(rx.test(ret) != true)
    return null;

  return ret;
  }

/* Append a string of non-null */

function append_string(str1, str2)
  {
  if(!str2)
    return str1;

  if(!str1)
    str1 = str2;
  else
    str1 += ", " + str2;
  return str1;
  }

/* Scroll support */

function rect_is_above(rect, parent_rect)
  {
  if(rect.bottom > parent_rect.bottom)
    return true;
  return false;
  }

function rect_is_below(rect, parent_rect)
  {
  if(rect.top < parent_rect.top)
    return true;
  return false;
  }

function scroll_elements_first_visible(first_child, parent_obj)
  {
  var el = first_child;
  var parent_rect = parent_obj.getBoundingClientRect();

  while(el)
    {
    var rect = el.getBoundingClientRect();
    if(!rect_is_above(rect, parent_rect) &&
       !rect_is_below(rect, parent_rect))
      return el;
    el = el.nextSibling;
    }
  return null;
  }

function scroll_elements_last_visible(last_child, parent_obj)
  {
  var el = last_child;
  var parent_rect = parent_obj.getBoundingClientRect();

  while(el)
    {
    var rect = el.getBoundingClientRect();
    if(!rect_is_above(rect, parent_rect) &&
       !rect_is_below(rect, parent_rect))
      return el;
    el = el.previousSibling;
    }
  return null;
  }

function scroll_element_is_above(el, parent_obj)
  {
  var rect = el.getBoundingClientRect();
  var parent_rect = parent_obj.getBoundingClientRect();
  return rect_is_above(rect, parent_rect);
  }

function scroll_element_is_below(el, parent_obj)
  {
  var rect = el.getBoundingClientRect();
  var parent_rect = parent_obj.getBoundingClientRect();
  return rect_is_below(rect, parent_rect);
  }

function set_scroller_coords(parent_obj)
  {
  var height;
  var top;
  var div = parent_obj.scroll_ctx.scroller;
  var scrollbar_height = div.parentElement.clientHeight;
  
  var scroll_offset = parent_obj.scrollTop;
  var scroll_offset_max = parent_obj.scrollHeight - parent_obj.clientHeight;
      
  if(scroll_offset_max <= 0)
    {
    height = scrollbar_height;
    top = 0;
    }
  else
    {
    height = scrollbar_height * parent_obj.clientHeight / parent_obj.scrollHeight;
    if(height < 20)
      height = 20;
    if(height > scrollbar_height)
      height = scrollbar_height;
    top = (scrollbar_height - height) * scroll_offset / scroll_offset_max;

    if(top < 0)
      top = 0;
    if(top > scrollbar_height - height)
      top = scrollbar_height - height;
    }

  parent_obj.scroll_ctx.top = top;
  parent_obj.scroll_ctx.height = height;
      
  div.setAttribute("style", "top: " + parseInt(top) + "px; bottom: " + (scrollbar_height - parseInt(top + height)) + "px;");
  }

function scrollable_scroll(obj, offset)
  {
  obj.scrollTop = offset;
  set_scroller_coords(obj);
  }

function scrollable_scroll_delta(obj, offset)
  {
  scrollable_scroll(obj, obj.scrollTop + offset);
  }


function browser_icon_onerror()
  {
  var span;
  var parent = this.parentElement;
  var icon = this.alt_icon;
  
  clear_element(parent);

  parent.setAttribute("style", "font-size: 200%; width: 2.0em; padding: 0px; text-align: center;");

  span = append_dom_element(parent, "span");
  span.setAttribute("class", icon);
     
  }

function setup_icon(img, alt_icon)
  {
  img.onerror = browser_icon_onerror;
  img.alt_icon = alt_icon;
  }

function make_icon_element(p, name)
  {
  var ret = append_dom_element(p, name);
  p.dataset.contains = name;
  return ret;
  }

function make_icon(p, obj, use_image, width)
  {
  var arr;
  var image_uri;
  var icon;
  var css_size;
  var ret;
  var m = dict_get_dictionary(obj, GAVL_META_METADATA);
      
  clear_element(p);
    
//  console.log("make_icon " + use_image + " " + JSON.stringify(m));
//  console.trace();
      
  if(use_image)
    {
    if((image_uri = dict_get_dictionary(m, GAVL_META_COVER_URL)) ||
       ((arr = dict_get_array(m, GAVL_META_COVER_URL)) &&
	(image_uri = get_image_uri_max(arr, width, -1)) &&
	(image_uri = image_uri.v)))
      {
      ret = make_icon_element(p, "img");
      setup_icon(ret, obj_get_icon(obj));
      ret.src = dict_get_string(image_uri, GAVL_META_URI);
      return ret;
      }

    if((image_uri = dict_get_dictionary(m, GAVL_META_POSTER_URL)) ||
       ((arr = dict_get_array(m, GAVL_META_POSTER_URL)) &&
	(image_uri = get_image_uri_max(arr, width, -1)) &&
	(image_uri = image_uri.v)))
      {
      ret = make_icon_element(p, "img");
      setup_icon(ret, obj_get_icon(obj));
      ret.src = dict_get_string(image_uri, GAVL_META_URI);
      return ret;
      }

    if((icon = dict_get_string(m, GAVL_META_LOGO_URL)))
      {
      ret = make_icon_element(p, "img");
      setup_icon(ret, obj_get_icon(obj));
      ret.src = icon;
      return ret;
      }

    }

  if(m[GAVL_META_ICON_URL] && m[GAVL_META_ICON_URL].v && is_array(m[GAVL_META_ICON_URL].v) && 
     (arr = dict_get_array(m, GAVL_META_ICON_URL)))
    {
    if((image_uri = get_image_uri_max(arr, width, -1)))
      {
      ret = make_icon_element(p, "img");
      setup_icon(ret, obj_get_icon(obj));
      ret.src = dict_get_string(image_uri.v, GAVL_META_URI);
      return ret;
      }
    }
      
  /* Image icon */
  if((icon = dict_get_string(m, GAVL_META_ICON_URL)))
    {
    if(icon.startsWith("http"))
      {
      ret = make_icon_element(p, "img");
      setup_icon(ret, obj_get_icon(obj));
      ret.src = icon;
      }
    else
      {
      ret = make_icon_element(p, "div");
//      ret.setAttribute("style", " width: 2.0em;");
      ret.setAttribute("class", icon);
      }
    }
  else
    {
    ret = make_icon_element(p, "div");
    ret.setAttribute("class", obj_get_icon(obj));
    }
  return ret;
  }

function is_ancestor(ancestor, descendent)
  {
  if((descendent != ancestor) &&
      descendent.startsWith(ancestor) &&
     (ancestor.endsWith("/") || (descendent.charAt(ancestor.length) == "/")))
    return true;
  else
    return false;
  }


/* Focus handling */

var focus_elements = null;
var focus_idx = 0;
var focus_stack = null;
var focus_tab_listener = false;



function focus_next()
  {
  var old_idx = focus_idx;

  if(old_idx < 0)      
    old_idx = 0;
  
//  console.log("focus_next 1: " + focus_idx + " " + focus_elements.length);
      
  if(!focus_elements.length)
    return true;
  focus_idx++;
  while(1)
    {
    if(focus_idx >= focus_elements.length)
      focus_idx = 0;
//    console.log("focus_next 2: " + old_idx + " " + focus_idx + " " +
//		focus_elements[focus_idx].focus_disabled + " " +
//		focus_elements[focus_idx].element_disabled);
    if(!focus_elements[focus_idx].focus_disabled && !focus_elements[focus_idx].element_disabled)
      break;
    if(focus_idx == old_idx)
      break;
    focus_idx++;
    }

//  console.log("focus_next 3: " + old_idx + " " + focus_idx);
      
  focus_elements[focus_idx].focus();

  return true;
  }

function focus_prev()
  {
  var old_idx = focus_idx;

  if(old_idx < 0)      
    old_idx = 0;
  
//  console.log("tab: " + old_idx + " " + focus_idx);
      
  if(!focus_elements.length)
    return true;
  focus_idx--;
  while(1)
    {
    if(focus_idx < 0)
      focus_idx = focus_elements.length - 1;
    if(!focus_elements[focus_idx].focus_disabled && !focus_elements[focus_idx].element_disabled)
      break;
    if(focus_idx == old_idx)
      break;
    focus_idx--;
    }
  focus_elements[focus_idx].focus();
  return true;
  }

function focus_tab_cb(evt)
  {
  if(evt.code == "Tab")
    {
    focus_next();
    stop_propagate(evt);
    return false;
    }
  else
    return true;
      
  }

function focus_element_append(el)
  {
  var first = false;


  //  el.setAttribute("tabindex", "-1");
  el.tabIndex = -1;
  el.focus_disabled = false;

  if(!focus_tab_listener)
    {
    document.addEventListener("keydown", focus_tab_cb, true);
    focus_tab_listener = true;
    }
      
  if(!focus_elements)
    {
    first = true;
    focus_elements = new Array();
    }
  else if(!focus_elements.length)
    first = true;

      
  focus_elements.push(el);

  el.onfocus = function(evt)
    {
    var idx = focus_elements.indexOf(this);
    if(idx >= 0)
      focus_idx = idx;

    return true;
    };
      
  if(first)
    el.focus();

  }

function set_focus_first()
  {
  var i;
  for(i = 0; i < focus_elements.length; i++)
    {
    if(!focus_elements[i].focus_disabled)
      {
      focus_elements[i].focus();
      return;
      }
    }
  }

function focus_element_remove(el)
  {
  var idx;
  if(!focus_elements)
    return;
  if((idx = focus_elements.indexOf(el)) >= 0)
    focus_elements.splice(idx, 1);
  }

function focus_element_disable(el)
  {
  el.element_disabled = true;

  /* Give focus to the first element */
  var idx = focus_elements.indexOf(el);
      
  if(idx == focus_idx)
    set_focus_first();
  }

function focus_element_enable(el)
  {
  el.element_disabled = false;
  }

function focus_element_add_arr(arr)
  {
  var i;
  
//  arr[0].focus();
  for(i = 0; i < arr.length; i++)
    focus_element_append(arr[i]);
  }

function focus_element_remove_arr(arr)
  {
  var i;
  var cur = focus_elements[focus_idx];
  for(i = 0; i < arr.length; i++)
    {
    focus_element_remove(arr[i]);
    if(arr[i] == cur)
      focus_idx = -1;
    }
  if(focus_idx < 0)
    set_focus_first();
  }

function focus_element_block_arr(arr)
  {
  var i;
  var cur = focus_elements[focus_idx];
  for(i = 0; i < arr.length; i++)
    {
    arr[i].focus_disabled = true;
    if(arr[i] == cur)
      focus_idx = -1;
    }
  if(focus_idx < 0)
    set_focus_first();
  }

function focus_element_unblock_arr(arr)
  {
  var i;
  for(i = 0; i < arr.length; i++)
    {
    if(focus_idx < 0)
      arr[i].focus();
    arr[i].focus_disabled = false;
    }
  }

function focus_elements_get_enabled()
  {
  var i;
  var ret = null;
  for(i = 0; i < focus_elements.length; i++)
    {
    if(!focus_elements[i].focus_disabled)
      {
      if(!ret)
	ret = new Array();
      ret.push(focus_elements[i]);
      }
    }
  return ret;
  }


/* Create DOM elements and append to parent */

function append_dom_element(p, tag)
  {
  var ret = document.createElement(tag);

      
  p.appendChild(ret);
  return ret;
  }

function append_dom_text(p, str)
  {
  var ret = document.createTextNode(str);
  p.appendChild(ret);
  return ret;
  }

/* Keyboard shortcuts */

function handle_key_event(e, obj)
  {
  var i;
  var msg = "";
  if(e.shiftKey)
    msg += "Shift+";
  if(e.altKey)
    msg += "Alt+";
  if(e.ctrlKey)
    msg += "Ctrl+";
  msg += e.code;
      
//  console.log(msg);
      
  for(i = 0; i < obj.shortcuts.length; i++)
    {
    if(obj.shortcuts[i].code == e.code)
      {
      if((obj.shortcuts[i].shiftKey == e.shiftKey) &&
         (obj.shortcuts[i].altKey == e.altKey) &&
         (obj.shortcuts[i].ctrlKey == e.ctrlKey))
        {
        if(!obj.shortcuts[i].func(e, obj.shortcuts[i].arg))
          {
          stop_propagate(e);
          return false;
          }
        else
          return true;
	}
      }
    }
  return true;
  }

// shiftKey, altKey, ctrlKey

function register_key_event(obj, code, func, arg)
  {
  var sc;
  var idx;
  if(!obj.shortcuts)
    {
    obj.shortcuts = new Array();
    obj.onkeydown = function(e) { handle_key_event(e, this); };
    }
  sc = new Object();

  if(code.indexOf("Shift+") >= 0)
    sc.shiftKey = true;
  else
    sc.shiftKey = false;

  if(code.indexOf("Alt+") >= 0)
    sc.altKey = true;
  else
    sc.altKey = false;

  if(code.indexOf("Ctrl+") >= 0)
    sc.ctrlKey = true;
  else
    sc.ctrlKey = false;

  if((idx = code.lastIndexOf("+")) >= 0)
    sc.code = code.substring(idx + 1, code.length);
  else
    sc.code = code;
      
  sc.func = func;
  sc.arg = arg;

  obj.shortcuts.push(sc);
  }

function clear_element(el, sibling)
  {
  if(!el)
    return;
    
  if(sibling)
    {
    while(sibling.nextSibling)
      el.removeChild(sibling.nextSibling);
    }
  else
    {
    while(el.firstChild)
      el.removeChild(el.firstChild);
    }
  }

/* Download XML */

function get_xml_onreadystatechange()
  {
  if(this.readyState==4 && this.status==200)
    {
    if(this.cb && this.cb.func)
      this.cb.func(this.responseXML);
    }
  }

function get_xml(url, cb, req)
  {
  if(!req)
    req = make_http_request();
      
  req.cb = cb;

  req.onreadystatechange = get_xml_onreadystatechange;
  req.my_open('GET', url);
  req.send();
  return req;
  }

/* Download json */

function get_json_onreadystatechange()
  {
  if(this.readyState==4 && this.status==200)
    {
    if(this.cb && this.cb.func)
      this.cb.func(JSON.parse(this.responseText));
    }
  }

function get_json(url, cb, req)
  {
  if(!req)
    req = make_http_request();
      
  req.cb = cb;

  req.onreadystatechange = get_json_onreadystatechange;
  req.my_open('GET', url);
  req.send();
  return req;
  }

/* Mimetype handling */

var mimetypes = null;

function mimetype_to_format(mimetype)
  {
  var i;
  for(i = 0; i < mimetypes.length; i++)
    {
    if(dict_get_string(mimetypes[i].v, GAVL_META_MIMETYPE) == mimetype)
      return dict_get_string(mimetypes[i].v, GAVL_META_LABEL);
    }
  return null;
  }


// Info table

function append_icon(parent_obj, klass)
  {
  var span = append_dom_element(parent_obj, "span");
  span.setAttribute("class", klass + " info-icon");
  return span;
  }

function info_table_append_title(table, str)
  {
  var tr, td;

  if(!str)
    return false;

  tr = append_dom_element(table, "tr");
  td = append_dom_element(tr, "td");

  td.setAttribute("colspan", "2");
  td.setAttribute("style", "font-size: 120%; font-weight: bold;");

  append_dom_text(td, str);
  return true;
  }

function info_table_append_title_dict(table, obj, key)
  {
  return info_table_append_title(table, obj_get_string(obj, key));
  }


var metatag_icons =
  [
    { key: GAVL_META_ARTIST,             icon: "icon-microphone"  },
    { key: GAVL_META_ALBUM,              icon: "icon-music-album" }, 
    { key: GAVL_META_GENRE,              icon: "icon-masks"       }, 
    { key: GAVL_META_DIRECTOR,           icon: "icon-movie-maker" }, 
    { key: GAVL_META_AUDIO_LANGUAGES,    icon: "icon-talk"        }, 
    { key: GAVL_META_LANGUAGE,           icon: "icon-talk"        }, 
    { key: GAVL_META_SUBTITLE_LANGUAGES, icon: "icon-subtitle"    }, 
    { key: GAVL_META_COUNTRY,            icon: "icon-flag"        },
    { key: GAVL_META_DATE,               icon: "icon-calendar"    },
    { key: GAVL_META_YEAR,               icon: "icon-calendar"    },
    { key: GAVL_META_APPROX_DURATION,    icon: "icon-clock"         },
    { key: GAVL_META_ACTOR,              icon: "icon-persons"       },
    { key: GAVL_META_TAG,                icon: "icon-tag"           },
    { key: GAVL_META_STATION,            icon: "icon-radio" },
    { key: GAVL_META_AUTHOR,             icon: "icon-person" }
  ];

function get_metatag_icon(key)
  {
  var i;
  var icon = null;
 
  for(i = 0; i < metatag_icons.length; i++)
    {
    if(metatag_icons[i].key == key)
      {
      icon = metatag_icons[i].icon;
      break;
      }
    }
  return icon;
  }

function info_table_append_icon(tr, icon)
  {
  var td = append_dom_element(tr, "td");
  td.setAttribute("style", "text-align: center; padding-right: 3px; width: 1.3em; max-width: 1.3em; vertical-align: top; ");
  append_icon(td, icon);
  }

function info_table_append(table, str, icon)
  {
  var tr, td;
  if(!str)
    return;
      
  tr = append_dom_element(table, "tr");

  if(icon)
    info_table_append_icon(tr, icon);

  td = append_dom_element(tr, "td");
  if(!icon)
    td.setAttribute("colspan", "2");

      
  if(is_object(str)) // gavl_value_t
    append_dom_text(td, value_get_string(str));
  else if(is_array(str))
    append_dom_text(td, str.join(", "));
  else
    append_dom_text(td, str);
  }

function info_table_append_dict(table, obj, key)
  {
  var tr;
  var td;
  var str;
  var i;
  var icon = get_metatag_icon(key);

  if(key == GAVL_META_APPROX_DURATION)
    {
    var time_sec;
    time_sec = dict_get_long(obj, GAVL_META_APPROX_DURATION) / GAVL_TIME_SCALE;

    if(time_sec <= 0)
      return;
	
    str = seconds_to_time_string(time_sec);
    info_table_append(table, str, icon);
    }
  else if(key == GAVL_META_YEAR)
    {
    var year = dict_get_string(obj, GAVL_META_YEAR);
    if(!year)
      {
      year = dict_get_string(obj, GAVL_META_DATE);
      if(year)
        year = year.substring(0, 4);  
      }
    if((year == "9999") || !year)
      return;

    info_table_append(table, year, icon);
    }
  else if(key == GAVL_META_DATE)
    {
    var date = dict_get_string(obj, GAVL_META_DATE);
    if(!date)
      date = dict_get_string(obj, GAVL_META_YEAR);
    if(!date)
      return;
    if(date.endsWith("-99-99"))
      date = date.substring(0, 4);  

    if((date == "9999") || !date)
      return;
    info_table_append(table, date, icon);
    }
  else if(key == GAVL_META_SRC)
    {
    var added = 0;
    var arr = dict_get_array(obj, GAVL_META_SRC);
    tr = append_dom_element(table, "tr");
    var uri;
    var mimetype;
    var isav = 0;
    var a;
    var format = null;

    if(!arr || (arr.length <= 0))
      return;
	
    td = append_dom_element(tr, "td");
    td.setAttribute("style", "text-align: center; padding-right: 3px; width: 1.3em; max-width: 1.3em; vertical-align: top; ");
    append_icon(td, "icon-download");
	
    td = append_dom_element(tr, "td");
    for(i = 0; i < arr.length; i++)
      {
      if(!(uri = dict_get_string(arr[i].v, GAVL_META_URI)) ||
	 (!uri.toLowerCase().startsWith("http://") && !uri.toLowerCase().startsWith("https://")) ||
	 !(mimetype = dict_get_string(arr[i].v, GAVL_META_MIMETYPE)) ||
	 !(format = mimetype_to_format(mimetype)))
        continue;

      if((mimetype.indexOf("audio/") == 0) || (mimetype.indexOf("video/") == 0))
        isav = 1;

      if((isav != 0) && (mimetype.indexOf("image/") == 0))
        continue;

      if(added > 0)
	append_dom_text(td, ", ");

      a = append_dom_element(td, "a");
      a.setAttribute("href", dict_get_string(arr[i].v, GAVL_META_URI));
      append_dom_text(a, format);
      added++;
      }
    
    }
  else if(key == GAVL_META_PLOT)
    {
    var plot;
    var plot_arr;

    if(!(plot = dict_get_string(obj, key)))
      return;

    tr = append_dom_element(table, "tr");
    td = append_dom_element(tr, "td");
    td.setAttribute("colspan", "2");
    plot_arr = plot.split("\n");
    append_dom_element(td, "p");

    for(i = 0; i < plot_arr.length; i++)
      {
      if(i)
	append_dom_element(td, "br");
      append_dom_text(td, plot_arr[i]);
      }
   
    }
  else
    {
    /* Fallback */
    str = dict_get_string(obj, key);
    info_table_append(table, str, icon);
    }
  }

function clone_object(obj, full)
  {
  if(is_array(obj))
    return clone_array(obj);
      
  if(full)
    return JSON.parse(JSON.stringify(obj));
  else // Copy just the object but reference all members
    {
    var ret = new Object();
    for (var name in obj)
      {
      if(obj.hasOwnProperty(name) && is_object(obj[name]) && obj[name].t)
	{
        ret[name] = new Object();
        ret[name].t = obj[name].t;
        ret[name].v = obj[name].v;
        }
      }
    return ret;
    }
  }

function clone_array(obj, full)
  {
  var i;
  var ret = new Array();
//  console.log("clone_array");

  for(i = 0; i < obj.length; i++)
    {
    var ch = new Object();
    ch.t = GAVL_TYPE_DICTIONARY;
    ch.v = clone_object(obj[i].v, full);
    ret.push(ch);
    }
  return ret;
  }

function merge_objects(src, dst)
  {
  for(var name in src)
    {
    if(src.hasOwnProperty(name))
      {
      if(is_object(src[name]))
        {
        if(!dst[name])
	  dst[name] = new Object();
        merge_objects(src[name], dst[name])
	}
      else 
        dst[name] = src[name];
      }
 
    }
  }

// http://stackoverflow.com/questions/7918868/how-to-escape-xml-entities-in-javascript

function escape_xml(str)
  {
  return str.replace(/[<>&'"]/g,
		     function (c)
		       {
		       switch (c)
                         {
                         case '<':
                           return '&lt;';
                         case '>':
                           return '&gt;';
                         case '&':
                           return '&amp;';
                         case '\'':
                           return '&apos;';
                         case '"':
                           return '&quot;';
                         default:
                           return c;
                         }
		       }
		    );
  }

function move_array_element(arr, from, to)
  {
  arr.splice(to, 0, arr.splice(from, 1)[0]);
  }

/* Gmerlin message interface */

/* Message definition for the player */

function msg_create(id, ns)
  {
  var ret = new Object();
      
  ret.id = id;
  ret.ns = ns;
  ret.args = new Array();

  ret.header = new Object();
  dict_set_int(ret.header, GAVL_MSG_ID, id);
  dict_set_int(ret.header, GAVL_MSG_NS, ns);

  return ret;
  }

function msg_parse(data)
  {
//  console.log("msg_parse " + data);
  var ret = JSON.parse(data);
  ret.id = dict_get_int(ret.header, GAVL_MSG_ID);
  ret.ns = dict_get_int(ret.header, GAVL_MSG_NS);
  return ret;
  }

function value_create(type, value)
  {
  var ret = new Object();
  ret.t = type;
  ret.v = value;
  return ret;
  }

function msg_set_arg(msg, arg, type, value)
  {
  if(arg >= 8)
    return;

  msg.args[arg] = value_create(type, value)
  }

function msg_is_last(msg)
  {
  var not_last = dict_get_int(msg.header, GAVL_MSG_NOT_LAST);
  if(not_last)
    return 0;
  else
    return 1;
  }

function msg_set_last(msg, last)
  {
  if(!last)
    dict_set_int(msg.header, GAVL_MSG_NOT_LAST, 1);
  }

function msg_set_arg_val(msg, arg, val)
  {
  if(arg >= 8)
    return;

  if(val)
    msg_set_arg(msg, arg, val.t, val.v);
  }

function msg_set_arg_int(msg, arg, value)
  {
  msg_set_arg(msg, arg, GAVL_TYPE_INT, value);
  }

function msg_set_arg_long(msg, arg, value)
  {
  msg_set_arg(msg, arg, GAVL_TYPE_LONG, value);
  }

function msg_set_arg_float(msg, arg, value)
  {
  msg_set_arg(msg, arg, GAVL_TYPE_FLOAT, value);
  }

function msg_set_arg_string(msg, arg, value)
  {
  msg_set_arg(msg, arg, GAVL_TYPE_STRING, value);
  }

function msg_set_arg_dictionary(msg, arg, value)
  {
  msg_set_arg(msg, arg, GAVL_TYPE_DICTIONARY, value);
  }

function msg_set_arg_array(msg, arg, value)
  {
  msg_set_arg(msg, arg, GAVL_TYPE_ARRAY, value);
  }

function msg_copy_header_field(dst, src, field)
  {
  dict_set_string(dst.header, field, dict_get_string(src.header, field));
  }

function split_url(href)
  {
  var a = document.createElement("a");
  a.href = href;
  return a;
  }

function msg_send(msg, socket)
  {
  var json = JSON.stringify(msg);
//  console.log("msg_send " + json); 
  socket.send(json);
  }

// JSON.parse(text [, reviver])
// JSON.stringify(value [, replacer] [, space])

function is_array(obj)
  {
  if(Object.prototype.toString.call(obj) === '[object Array]' )
    return true;
  else
    return false;
  }

function is_object(obj)
  {
  return !is_array(obj) && (obj === Object(obj));
  }

function get_url_vars()
  {
  var str, arr, arr1, i;
  var ret = new Object();

  var start;
  var end;

  str = window.location.href;
      
  start = str.indexOf("?");
  if(start < 0)
    return ret; // Empty dict
  else
    start++;
      
  end = str.indexOf("#");
  if(end < 0)
    str = str.substring(start, str.length);
  else
    str = str.substring(start, end);

  arr = str.split('&');

  for(i = 0; i < arr.length; i++)
    {
    arr1 = arr[i].split('=');
    if(arr1 && arr1[0] && arr1[1])
      ret[arr1[0]] = arr1[1];
    }
  return ret;
  }

/* Client ID */

var client_id = null;

function init_client_id()
  {
  var vars;

  if(client_id != null)
    return;

  vars = get_url_vars();
  if(vars.cid)
    client_id = vars.cid;

  console.log("Got client ID " + " " + client_id);
  }

function get_datastore_onreadystatechange()
  {
  if(!this.cb || (this.readyState != 4))
    return;
      
  if(this.status==200)
    {
    if(this.cb.func)
      this.cb.func(JSON.parse(this.responseText));
    }
  else if(this.cb.fail)
    this.cb.fail();
  }

function datastore_get(name, cb)
  {
  var req;
  var url;
  if(!client_id)
    {
    if(cb && cb.end)
      cb.end();
    return;
    }

  url = "/storage/" + name + "?cid=" + client_id;
  req = make_http_request();
  req.cb = cb;
  req.onreadystatechange = get_datastore_onreadystatechange;

  req.my_open('GET', url);
  req.send();
  }

function datastore_set(name, val)
  {
  var req;
  var url;
  if(!client_id)
    return;
      
  url = "/storage/" + name + "?cid=" + client_id;
      
  req = make_http_request();
  req.my_open('PUT', url);
  req.setRequestHeader("Content-Type", "application/json");
  req.send(JSON.stringify(val));

  }

/* Custom events */

function add_my_event_handler(obj, cb)
  {
  if(!obj.event_handlers)
    obj.event_handlers = new Array();
      
  obj.event_handlers.push(cb);
  }

function prepend_my_event_handler(obj, cb)
  {
  if(!obj.event_handlers)
    obj.event_handlers = new Array();

//  console.log("prepend_my_event_handler " + obj.event_handlers.length);
  obj.event_handlers.splice(0, 0, cb);
  }

function delete_my_event_handler(obj, cb)
  {
  var idx;

//  console.log("delete_my_event_handler");
//  console.trace();
      
  if(!obj.event_handlers)
    return;
      
  idx = obj.event_handlers.indexOf(cb);

  if(idx < 0)
    return;      
    
//  console.log("delete_my_event_handler " + obj.event_handlers.length + " " + idx);
//  console.trace();
      
  obj.event_handlers.splice(idx, 1);
  if(!obj.event_handlers.length)
    delete obj.event_handlers;
  }

function fire_event(obj, evt)
  {
  var i;

//  console.log("fire_event 0 " + obj + " " + obj.event_handlers);
      
  if(obj.event_handlers)
    {
//    console.log("fire_event 1  " + obj.event_handlers.length);

    for(i = 0; i < obj.event_handlers.length; i++)
      {
//      console.log("fire_event 2 " + i);
      if(!obj.event_handlers[i].handle_msg(evt) ||
         !obj.event_handlers) // The callback can call delete_my_event_handler()
        break;
      }
    }
  
  }

function animate_timeout(ctx)
  {
  ctx.frame++;

  if(ctx.cb && ctx.cb.func)
    {
    var perc = ctx.frame / ctx.frames;
//    console.log("animate " + perc);
    perc = 0.5 * (1.0 - Math.cos(perc * Math.PI));
    ctx.cb.func(perc);
    } 
  if(ctx.frame < ctx.frames)
    {
    var next_time = parseInt(((ctx.frame+1)*ctx.duration) /frames);
    window.setTimeout(animate_timeout, next_time - ctx.time, ctx);
    }
  else
    {
    if(ctx.cb && ctx.cb.end)
      ctx.cb.end();
    }
  }

function animate(cb, frames, duration)
  {
  var next_time;
  var ctx    = new Object();
  ctx.frame  = 0;
  ctx.frames = frames;
  ctx.duration   = duration;
  ctx.time   = 0;
  ctx.cb = cb;
  next_time = parseInt(((ctx.frame+1)*ctx.duration) /frames);
      
  window.setTimeout(animate_timeout, next_time - ctx.time, ctx);
      
  }

/*

    
*/

var obj_icons = [
    { type: GAVL_META_CLASS_AUDIO_FILE,      icon: "icon-volume-max"      },
    { type: GAVL_META_CLASS_VIDEO_FILE,      icon: "icon-film"            },
    { type: GAVL_META_CLASS_AUDIO_PODCAST_EPISODE,      icon: "icon-volume-max"      },
    { type: GAVL_META_CLASS_VIDEO_PODCAST_EPISODE,      icon: "icon-film"            },
    { type: GAVL_META_CLASS_SONG,            icon: "icon-music"           },
    { type: GAVL_META_CLASS_MOVIE,           icon: "icon-film"            },
    { type: GAVL_META_CLASS_MOVIE_PART,      icon: "icon-film"            },
    { type: GAVL_META_CLASS_TV_EPISODE,      icon: "icon-tv"              },
    { type: GAVL_META_CLASS_AUDIO_BROADCAST, icon: "icon-radio"           },
    { type: GAVL_META_CLASS_VIDEO_BROADCAST, icon: "icon-radio-station"   },
    { type: GAVL_META_CLASS_IMAGE,           icon: "icon-image"           },
    { type: GAVL_META_CLASS_FILE,            icon: "icon-file"            },
    { type: GAVL_META_CLASS_LOCATION,        icon: "icon-globe"           },
    { type: GAVL_META_CLASS_MUSICALBUM,      icon: "icon-music-album"     }, 
    { type: GAVL_META_CLASS_PHOTOALBUM,      icon: "icon-photo"           }, 
    { type: GAVL_META_CLASS_PLAYLIST,           icon: "icon-playlist"     },
    { type: GAVL_META_CLASS_CONTAINER_ACTOR,    icon: "icon-person"       },
    { type: GAVL_META_CLASS_CONTAINER_DIRECTOR, icon: "icon-movie-maker"  },
    { type: GAVL_META_CLASS_CONTAINER_ARTIST,   icon: "icon-microphone"   },
    { type: GAVL_META_CLASS_CONTAINER_COUNTRY,  icon: "icon-flag"         },
    { type: GAVL_META_CLASS_CONTAINER_GENRE,    icon: "icon-masks"        },
    { type: GAVL_META_CLASS_CONTAINER_LANGUAGE, icon: "icon-talk"         },
    { type: GAVL_META_CLASS_CONTAINER_TAG,      icon: "icon-tag"          },
    { type: GAVL_META_CLASS_CONTAINER_YEAR,     icon: "icon-calendar"     },
    { type: GAVL_META_CLASS_CONTAINER_RADIO,    icon: "icon-radio"        },
    { type: GAVL_META_CLASS_CONTAINER_TV,       icon: "icon-tv"           },
    { type: GAVL_META_CLASS_PODCAST,            icon: "icon-rss"    },
    { type: GAVL_META_CLASS_TV_SHOW,            icon: "icon-tv"           },
    { type: GAVL_META_CLASS_AUDIO_RECORDER,     icon: "icon-microphone"   },
    { type: GAVL_META_CLASS_VIDEO_RECORDER,     icon: "icon-videocamera"  },

/* Root Containers */
    { type: GAVL_META_CLASS_ROOT_MUSICALBUMS,  icon: "icon-music-album"   },
    { type: GAVL_META_CLASS_ROOT_SONGS,        icon: "icon-music"         },
    { type: GAVL_META_CLASS_ROOT_MOVIES,       icon: "icon-film"          },
    { type: GAVL_META_CLASS_ROOT_TV_SHOWS,     icon: "icon-tv"            },
    { type: GAVL_META_CLASS_ROOT_STREAMS,      icon: "icon-network" },
    { type: GAVL_META_CLASS_ROOT_DIRECTORIES,  icon: "icon-hdd" },
    { type: GAVL_META_CLASS_ROOT_PHOTOS,       icon: "icon-photo" },
    { type: GAVL_META_CLASS_ROOT_PODCASTS,     icon: "icon-rss"    },

    { type: GAVL_META_CLASS_ROOT_FAVORITES,    icon: "icon-heart"       }, 
    { type: GAVL_META_CLASS_ROOT_BOOKMARKS,    icon: "icon-bookmark"    }, 
    { type: GAVL_META_CLASS_ROOT_LIBRARY,      icon: "icon-library"     },
    { type: GAVL_META_CLASS_ROOT_SERVER,       icon: "icon-server"      },
    { type: GAVL_META_CLASS_ROOT_PLAYQUEUE,    icon: "icon-player"      },
    { type: GAVL_META_CLASS_ROOT_RECORDERS,    icon: "icon-videocamera" },

    { type: GAVL_META_CLASS_ROOT_REMOVABLE,    icon: "icon-hdd"          },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_AUDIOCD, icon: "icon-cdrom"  },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_VCD, icon: "icon-cdrom"  },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_SVCD, icon: "icon-cdrom"  },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_VIDEODVD, icon: "icon-cdrom"  },

    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM,            icon: "icon-hdd"     },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM_HDD,        icon: "icon-hdd"     },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM_PENDRIVE,   icon: "icon-pendrive" },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM_MEMORYCARD, icon: "icon-memorycard" },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM_MOBILE,     icon: "icon-mobile"  },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD,         icon: "icon-cdrom"  },
    { type: GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD,        icon: "icon-cdrom"  },
  ];

function media_class_get_icon(klass)
  {
  var i;
    
  for(i = 0; i < obj_icons.length; i++)
    {
    if(obj_icons[i].type == klass)
      return obj_icons[i].icon;
    }

  if(klass.startsWith("container"))
    return "icon-folder";

  return null;
  }

function obj_get_icon(obj)
  {
  var klass = obj_get_string(obj, GAVL_META_CLASS);
  if(!klass)
    return null;
  return media_class_get_icon(klass);
  };

function obj_get_id(obj)
  {
  return dict_get_string(dict_get_dictionary(obj, GAVL_META_METADATA), GAVL_META_ID);
  }

function obj_make_playqueue_id(obj)
  {
  var hash = dict_get_string(dict_get_dictionary(obj, GAVL_META_METADATA), GAVL_META_HASH);
//  console.log("obj_make_playqueue_id " + obj_get_id(obj));
//  console.trace();
  return BG_PLAYQUEUE_ID + "/" + hash;
  }

function obj_get_children(obj)
  {
  var ret =  dict_get_array(obj, GAVL_META_CHILDREN);
  if(!ret || !ret.length)
    return null;
  return ret;
  }

function obj_get_child_idx_by_id(obj, id)
  {
  var i;
  var children;      

  if(!(children = obj_get_children(obj)))
    return -1;

  for(i = 0; i < children.length; i++)
    {
    if(obj_get_id(children[i].v) == id)
      return i;
    }
  return -1;
  }

function obj_get_child_by_id(obj, id)
  {
  var children;      
  var i = obj_get_child_idx_by_id(obj, id);

  if(i < 0)
    return null;

  if(!(children = obj_get_children(obj)))
    return null;
      
  return children[i].v;
  }

function obj_get_next_sibling(obj)
  {
  return obj_get_string(obj, GAVL_META_NEXT_ID);
  }

function obj_get_prev_sibling(obj)
  {
  return obj_get_string(obj, GAVL_META_PREVIOUS_ID);
  }

function value_get_string(val)
  {
  switch(val.t)
    {
    case GAVL_TYPE_INT:
    case GAVL_TYPE_LONG:
    case GAVL_TYPE_FLOAT:
      return val.v.toString();
      break;
    case GAVL_TYPE_STRING:
      return val.v;
      break;
    case GAVL_TYPE_ARRAY:
      {
      var i;
      var str = "";
      for(i = 0; i < val.v.length; i++)
	{
	if(i > 0)
	  str += ", ";
	
        str += value_get_string(val.v[i]);
        }
      return str;
      break;
      }
      break;
    case GAVL_TYPE_COLOR_RGB:
    case GAVL_TYPE_COLOR_RGBA:
    case GAVL_TYPE_POSITION:
      return val.v.join(", ");
      break;
    default:
      return null;
    }
  }

function value_set_string(val, str)
  {
  if(is_array(str))
    val.t = GAVL_TYPE_ARRAY;
  else
    val.t = GAVL_TYPE_STRING;
  val.v = str;
  }

function dict_get_string(obj, key)
  {
  if(!obj[key])
    return null;
  return value_get_string(obj[key]);
  }

function obj_get_string(obj, key)
  {
  return dict_get_string(dict_get_dictionary(obj, GAVL_META_METADATA), key);
  }

function obj_get_int(obj, key)
  {
  return dict_get_int(dict_get_dictionary(obj, GAVL_META_METADATA), key);
  }


function dict_set_string(obj, key, val)
  {
  if(!obj[key])
    obj[key] = new Object();
  value_set_string(obj[key], val);
  }

function obj_set_string(obj, key, val)
  {
  return dict_set_string(dict_get_dictionary(obj, GAVL_META_METADATA), key, val);
  }

function obj_set_int(obj, key, val)
  {
  return dict_set_int(dict_get_dictionary(obj, GAVL_META_METADATA), key, val);
  }

function obj_clear_tag(obj, key)
  {
  var m = dict_get_dictionary(obj, GAVL_META_METADATA);
  if(m[key])
    delete m[key];
  }

function obj_append_children(obj, arr)
  {
  var i;
  var children;

  if(!(children = dict_get_array(obj, GAVL_META_CHILDREN)))
    {
    dict_set_array(obj, GAVL_META_CHILDREN, new Array());
    children = dict_get_array(obj, GAVL_META_CHILDREN);
    }

  for(i = 0; i < arr.length; i++)
    {
    children.splice(children.length, 0, arr[i]);
    }
  }

function obj_splice_children(obj, idx, del, add)
  {
  var i;
  var children;

  if(!(children = dict_get_array(obj, GAVL_META_CHILDREN)))
    {
    dict_set_array(obj, GAVL_META_CHILDREN, new Array());
    children = dict_get_array(obj, GAVL_META_CHILDREN);
    }

  if(idx < 0)
    idx = children.length;

  if(del)
    children.splice(idx, del);

  if(!add)
    return;
      
  if(is_array(add.v))
    {
    for(i = 0; i < add.v.length; i++)
      {
      children.splice(idx, 0, add.v[i]);
      idx++;
      }
    }
  else if(is_object(add.v))
    {
    children.splice(idx, 0, add);
    }
  }

function dict_set_int(obj, key, val)
  {
  if(!obj[key])
    obj[key] = new Object();
  obj[key].t = GAVL_TYPE_INT;
  obj[key].v = parseInt(val);
  }

function dict_set(obj, key, val)
  {
  obj[key] = val;
  }

function dict_set_long(obj, key, val)
  {
  if(!obj[key])
    obj[key] = new Object();
  obj[key].t = GAVL_TYPE_LONG;
  obj[key].v = parseInt(val);
  }

function dict_set_float(obj, key, val)
  {
  if(!obj[key])
    obj[key] = new Object();
  obj[key].t = GAVL_TYPE_FLOAT;
  obj[key].v = parseInt(val);
  }


function dict_get_int(obj, key, val)
  {
  if(!obj[key])
    return 0;

  if((obj[key].t == GAVL_TYPE_INT) || (obj[key].t == GAVL_TYPE_LONG))
    return obj[key].v;

  else if(obj[key].t == GAVL_TYPE_FLOAT)
    return Math.floor(obj[key].v);
 
  else if(obj[key].t == GAVL_TYPE_STRING)
    return parseInt(obj[key].v);
  return 0;
  }

function dict_get_long(obj, key, val)
  {
  if(!obj[key])
    return 0;

  if((obj[key].t == GAVL_TYPE_INT) || (obj[key].t == GAVL_TYPE_LONG))
    return obj[key].v;

  else if(obj[key].t == GAVL_TYPE_FLOAT)
    return Math.floor(obj[key].v);
 
  else if(obj[key].t == GAVL_TYPE_STRING)
    return parseInt(obj[key].v);
  return 0;
  }

function dict_set_float(obj, key, val)
  {
  if(!obj[key])
    obj[key] = new Object();

  obj[key].t = GAVL_TYPE_FLOAT;
  obj[key].v = parseFloat(val);
  }

function dict_set_array(obj, key, val)
  {
  if(!val)
    val = new Array();

  if(is_array(val))
    {
    if(!obj[key])
      obj[key] = new Object();
    obj[key].t = GAVL_TYPE_ARRAY;
    obj[key].v = val;
    }
  else if(typeof val == "string")
    {
    if(!obj[key])
      obj[key] = new Object();
    obj[key].t = GAVL_TYPE_STRING;
    obj[key].v = val;
    }
  else
    {
//    console.log("Called dict_set_array with non-array type");
    return null;
    }
  return obj[key].v;
  }

function dict_set_dictionary(obj, key, val)
  {
  if(!val)
    val = new Object();

  if(is_object(val))
    {
    if(!obj[key])
      obj[key] = new Object();
    obj[key].t = GAVL_TYPE_DICTIONARY;
    obj[key].v = val;
    }
  else
    {
//    console.log("Called dict_set_dictionary with non-object type");
    return null;
    }
  return obj[key].v;
  }


function dict_get_array(obj, key)
  {
  if(!obj[key])
    return null;

  if(is_array(obj[key].v))
    return obj[key].v;
  else if(obj[key])
    {
    var arr = new Array();
    arr.push(obj[key]);
    return arr;
    }
  }

function dict_get_dictionary(obj, key)
  {
  if(!obj[key])
    return null;

  if(is_object(obj[key].v))
    return obj[key].v;
  else
    {
//    console.log("Called dict_get_dictionary with non-object type");
    return null;
    }
  }

var swipe_event_hub = null;
var swipe_distance_min = 100;
var swipe_angle_tolerance = 15;

function _touchstart_callback(e)
  {
  var t = e.touches[0];
  swipe_event_hub.x_start = t.screenX;
  swipe_event_hub.y_start = t.screenY;
  swipe_event_hub.x_end = t.screenX;
  swipe_event_hub.y_end = t.screenY;

// alert("Touch start " + t.screenX + " " + t.screenY);

  }

function _touchmove_callback(e)
  {
  var t = e.touches[0];
  swipe_event_hub.x_end = t.screenX;
  swipe_event_hub.y_end = t.screenY;

// alert("Touch start " + t.screenX + " " + t.screenY);

  }

function _touchend_callback(e)
  {
  var swipe_type = 0;
  var msg = 0;
  var angle;

//  alert("Touch end");

  var delta_x = swipe_event_hub.x_end - swipe_event_hub.x_start;

  var delta_y = swipe_event_hub.y_start - swipe_event_hub.y_end; // y = 0 is at the top
         
  if(delta_x*delta_x + delta_y*delta_y < swipe_distance_min * swipe_distance_min)
    return;

  angle = 180.0 * Math.atan2(delta_y,
                             delta_x) / Math.PI;


      
  if((angle <= swipe_angle_tolerance) && (angle >= -swipe_angle_tolerance))
    {
    /* Right */
    swipe_type = GAVL_MSG_GUI_SWIPE_RIGHT;
    }

  else if((angle <= -180.0 + swipe_angle_tolerance) || (angle >= 180.0 - swipe_angle_tolerance))
    {
    /* Left */
    swipe_type = GAVL_MSG_GUI_SWIPE_LEFT;
    }

  else if((angle <= 90.0 + swipe_angle_tolerance) && (angle >= 90.0 - swipe_angle_tolerance))
    {
    /* Up */
//    swipe_type = GAVL_MSG_GUI_SWIPE_UP;
    	  
    }
  else if((angle <= -90.0 + swipe_angle_tolerance) && (angle >= -90.0 - swipe_angle_tolerance))
    {
    /* Down */
//    swipe_type = GAVL_MSG_GUI_SWIPE_DOWN;
	  
    }

      //  alert("Touch end " + delta_x + " " + delta_y + " " + angle + " " + swipe_type);

  if(swipe_type)
    {
    msg = msg_create(GAVL_MSG_GUI_SWIPE, GAVL_MSG_NS_GUI);
    msg_set_arg_int(msg, 0, swipe_type);

    fire_event(swipe_event_hub, msg);
    stop_propagate(e);
    }
  
  }

function init_swipe_events()
  {
  swipe_event_hub = new Object();

  swipe_event_hub.x_start = 0;
  swipe_event_hub.y_start = 0;
  swipe_event_hub.x_end = 0;
  swipe_event_hub.y_end = 0;

  swipe_event_hub.connect = function(cb)
    {
    prepend_my_event_handler(this, cb);
    };

  swipe_event_hub.disconnect = function(cb)
    {
    delete_my_event_handler(this, cb);
    };

  add_event_handler(document.body, "touchstart", _touchstart_callback);
  add_event_handler(document.body, "touchmove", _touchmove_callback);
  add_event_handler(document.body, "touchend", _touchend_callback);
  }

/* https://stackoverflow.com/questions/105034/create-guid-uuid-in-javascript
 *
 * And, yes, we are ok with random numbers
 */

function guid()
  {
  function s4() {
    return Math.floor((1 + Math.random()) * 0x10000)
      .toString(16)
      .substring(1);
  }
  return s4() + s4() + '-' + s4() + '-' + s4() + '-' +
    s4() + '-' + s4() + s4() + s4();
  }

function get_image_uri_min(arr, mimetype)
  {
  var img_w;
  var ext, ext_i, i;
  var uri;

//  console.log("get_image_uri_min");
      
  if(!arr || !arr.length)
    return null;      

  ext = -1;
    
  for(i = 0; i < arr.length; i++)
    {
    if(mimetype && (mimetype != dict_get_string(arr[i].v, GAVL_META_MIMETYPE)))
      continue;

    /* Ignore non-http uris */
    uri = dict_get_string(arr[i].v, GAVL_META_URI);
      
    if(!uri.startsWith("http://") && !uri.startsWith("https://"))
      continue;

      
    img_w = dict_get_int(arr[i].v, GAVL_META_WIDTH);

    if((ext < 0) || (img_w < ext))
      {
      ext = img_w;
      ext_i = i;
      }
    }
  return arr[ext_i];
  }

function get_image_uri_max(arr, w, h, mimetype, depth)
  {
  var ext, ext_i = -1, i;
  var img_w;
  var resolution;
  var resolution_arr;
  var uri;

//  console.log("get_image_uri_max " + JSON.stringify(arr));
//  console.trace();
      
  if(!arr || !is_array(arr) || !arr.length || (arr[0].t != "d"))
    return null;      
      
  for(i = 0; i < arr.length; i++)
    {
//    console.log("Array element " + JSON.stringify(arr[i].v));
	  
    if(mimetype && (mimetype != dict_get_string(arr[i].v, GAVL_META_MIMETYPE)))
      {
//      console.log("blupp 1");
      continue;
      }
    /* Ignore non-http uris */
    uri = dict_get_string(arr[i].v, GAVL_META_URI);

    if(!uri.startsWith("http://") && !uri.startsWith("https://"))
      {
//      console.log("blupp 2");
      continue;
      }
      
    if(depth && (depth != dict_get_int(arr[i].v, "depth")))
      {
//      console.log("blupp 3");
      continue;
      }

    img_w = dict_get_int(arr[i].v, GAVL_META_WIDTH);
	
    if((w > 0) && (img_w > w))
      {
//      console.log("blupp 4");
      continue;
      }
	
    if((ext_i < 0) || (img_w > ext))
      {
      ext = img_w;
      ext_i = i;
      }
    }
       
  if(ext_i >= 0)
    return arr[ext_i];

  if(!mimetype && !depth)
    return get_image_uri_min(arr);
  else
    return null;
  }

function obj_dump(obj)
  {
  console.log(JSON.stringify(obj));
  }

/* Not sure if this is needed */

function obj_handle_command(cmd)
  {
  switch(cmd.ns)
    {
    case BG_MSG_NS_DB:
      switch(cmd.id)
        {
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          var idx;
	  var del;
          var add;
          var added = 0;
	  var evt;
	  var last;
          var children;
	  evt = msg_create(BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
	  last = cmd.args[0].v;
	  idx = cmd.args[1].v;
	  del = cmd.args[2].v;

          children = dict_get_array(this, GAVL_META_CHILDREN);

          if(!children)
            {		  
            dict_set_array(this, GAVL_META_CHILDREN, new Array());
            children = dict_get_array(this, GAVL_META_CHILDREN);
	    }

          // console.log("cmd splice " + children);
	      
          if(idx < 0)
	    idx = children.length;

          if(del < 0) /* Delete all until end */
            del = children.length - idx;

          dict_set_string(evt.header, GAVL_MSG_CONTEXT_ID, dict_get_string(cmd.header, GAVL_MSG_CONTEXT_ID));
	      
          /* prepare message but send later */
          msg_set_arg_val(evt, 0, cmd.args[0]); // last
          msg_set_arg_int(evt, 1, idx); // idx
          msg_set_arg_int(evt, 2, del); // del
          msg_set_arg_val(evt, 3, cmd.args[3]); // add

          /* Delete children */	      
          children.splice(idx, del);

          if(cmd.args[3])
            {
            if(cmd.args[3].t == GAVL_TYPE_ARRAY)
              {
              var i;
//            console.log("splice 1 " + JSON.stringify(cmd.args[3].v));
		
              for(i = 0; i < cmd.args[3].v.length; i++)
                {
                obj = cmd.args[3].v[i];

//              console.log("splice 2 " + i + " " + cmd.args[3].v.length + " " + JSON.stringify(obj));
		  
                children.splice(idx, 0, obj);
                idx++;
                added++;
                }
              }
            else if(cmd.args[3].t == GAVL_TYPE_DICTIONARY)
              {
              obj = cmd.args[3];
              children.splice(idx, 0, obj);
              added++;
              }
	    }
	      
          fire_event(this, evt);

          if(last)
	    {
            this.changed = true;
            if(added)
	      this.has_children = true;
	    }
	  }
     	  break;
	}
      break;
	
    }
  return 1;
  }

function set_browse_children_response(req, arr)
  {
  var ret;
  var num = 0;
  var start = 0;

  if(req.args[0])
    start = req.args[0].v;

  if(req.args[1])
    num = req.args[1].v;

  if(num <= 0)
    num = arr.length;
      
  ret = msg_create(BG_RESP_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
  dict_set_string(ret.header, GAVL_MSG_CONTEXT_ID, dict_get_string(req.header, GAVL_MSG_CONTEXT_ID));

  msg_set_arg_int(ret, 0, start);
  msg_set_arg_int(ret, 1, 0); // del
  msg_set_arg_array(ret, 2, arr.slice(start, start + num));
  
  return ret;
    
  }

function get_websocket_close_reason(event)
  {
  switch(event.code)
    {
    case 1000:
      return "Normal closure";
      break;
    case 1001:
      return "Server going down or a browser having navigated away from a page.";
      break;
    case 1002:
      return "Protocol error";
      break;
    case 1003:
      return "Received unacceptable data";
      break;
    case 1004:
      return "Reserved";
      break;
    case 1005:
      return "No code present";
      break;
    case 1006:
      return "Unexpected close";
      break;
    case 1007:
      return "Invalid message for type";
      break;
    case 1008:
      return "Policy violation";
      break;
    case 1009:
      return "Message too large";
      break;
    case 1010:
      return "Expected extension not present";
      break;
    case 1011:
      return "Unexpected condition";
      break;
    case 1015:
      return "TLS handshake failed";
      break;
    }
  return "Unknown reason";
  }
