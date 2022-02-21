/*
 * Button
 */

function gui_button_set_class(button, klass)
  {
  button.setAttribute("class", button.cl_pfx + klass);
  }

function gui_make_button(icon, cb, arr, pfx)
  {
  var a;
      
  a = document.createElement("div");
  a.setAttribute("style", "display: table-cell; vertical-align: middle; text-align: center; text-decoration: none;");
      
  if(pfx)
    a.cl_pfx = pfx;
  else
    a.cl_pfx = "";
      
  a.cb = cb;
  a.icon = icon;
  gui_button_set_class(a, "button " + a.icon);      
      
  a.onclick   = function(evt)
    {
    if(this.element_disabled)
      {
      stop_propagate(evt);
      return false;
      }
    if(this.cb && this.cb.func)
      this.cb.func();
    return true;
    };

  a.onmousedown   = function(evt)
    {
    if(this.element_disabled)
      {
      stop_propagate(evt);
      return false;
      }

    gui_button_set_class(this, "button_active " + this.icon);
    this.focus();
    stop_propagate(evt);
    return false;
    };
      
  a.onmouseup   = function(evt)
    {
    if(this.element_disabled)
      {
      stop_propagate(evt);
      return false;
      }
    gui_button_set_class(this, "button " + this.icon);
    return true;
    };

  a.onkeydown = function(evt)
    {
    if(this.element_disabled)
      {
      stop_propagate(evt);
      return false;
      }

//    console.log("Code: " + evt.code);
	
    if((evt.code == "ArrowDown") || (evt.code == "ArrowRight"))
      {
      focus_next();
      stop_propagate(evt);
      return false;
      }

    if((evt.code == "ArrowUp") || (evt.code == "ArrowLeft"))
      {
      focus_prev();
      stop_propagate(evt);
      return false;
      }
	
    if(evt.code != "Enter")
      return true;
    gui_button_set_class(this, "button_active " + this.icon);
    if(this.cb && this.cb.func)
      this.cb.func();
    stop_propagate(evt);
    return false;
    };
      
  a.onkeyup = function(evt)
    {
    if(this.element_disabled)
      {
      stop_propagate(evt);
      return false;
      }
    if(evt.code != "Enter")
      return true;
    gui_button_set_class(this, "button " + this.icon);
    stop_propagate(evt);
    return false;
    };

  a.onblur = function(evt)
    {
    gui_button_set_class(this, "button " + this.icon);
    return true;
    };

  /* Additional methods */
      
  a.reset = function()
    {
    gui_button_set_class(this, "button " + this.icon);
    };

  a.enable = function()
    {
    if(!this.element_disabled)
      return;
    gui_button_set_class(this, "button " + this.icon);
    focus_element_enable(this);
    };

  a.disable = function()
    {
    if(this.element_disabled)
      return;
    gui_button_set_class(this, "button_disabled " + this.icon);
    focus_element_disable(this);
    };
      
  if(arr)
    arr.push(a);
  
  return a;
  }

/*
function make_button_box()
  {
      
  }

function gui_make_hbutton_box()
  {

  }

function gui_make_vbutton_box()
  {

  }
*/

/*
 *  Slider
 */


function gui_make_slider(div, vertical, focus_el, cb)
  {
  var style;
  var children = div.getElementsByTagName("div");
      
  if(children.length == 3)
    div.handle = children[1];
  else
    div.handle = children[0];
      
  if(vertical)
    {
    div.vertical = true;
    div.handle.vertical = true;
    }
  else
    {
    div.vertical = false;
    div.handle.vertical = false;
    }
      
  div.mouse_active = false;
  div.mouse_pos = -1;
  div.offset = 0;

  div.slider_size = 0;
  div.handle_size = 0;

  div.setAttribute("style", "display: inline-block;");

  style = "position: relative;";
  
  div.handle.setAttribute("style", style);
     
  div.cb = cb;

  div.send_callback = function(set)
    {
    var	perc;
    
    perc = this.offset * 1.0 / (this.slider_size - this.handle_size);

    if(!this.cb)
      return;

    if(this.cb.change)
      this.cb.change(perc);

    if(set && this.cb.set)
      this.cb.set(perc);

    };
      
  div.get_mouse_pos = function(evt)
    {
    if(this.vertical)
      return evt.clientY;
    else
      return evt.clientX;
    };

  div.get_client_size = function(el)
    {
    if(this.vertical)
      return el.clientHeight;
    else
      return el.clientWidth;
    };

  div.get_offset_size = function(el)
    {
    if(this.vertical)
      return el.offsetHeight;
    else
      return el.offsetWidth;
    };

  div.apply_offset = function()
    {
    var style = "position: relative;";

    this.slider_size = this.get_client_size(this);

    if(this.bar)
      this.handle_size = this.get_offset_size(this.handle) - this.get_client_size(this.handle);
    else
      this.handle_size = this.get_offset_size(this.handle);

    if(this.offset < 0)
      this.offset = 0;

    if(this.offset > this.slider_size - this.handle_size)
      this.offset = this.slider_size - this.handle_size;


    if(this.bar)
      {
      if(this.vertical)
        style += "top: " + this.offset + "px;";
      else
        style += "width: " + this.offset + "px;";
      }
    else
      {
      if(this.vertical)
        style += "top: " + this.offset + "px;";
      else
        style += "left: " + this.offset + "px;";
      }

    this.handle.setAttribute("style", style);
    };

  div.set_value = function(perc)
    {
    this.slider_size = this.get_client_size(this);
    if(this.bar)
      this.handle_size = this.get_offset_size(this.handle) - this.get_client_size(this.handle);
    else
      this.handle_size = this.get_offset_size(this.handle);
    this.offset = parseInt(perc * (this.slider_size - this.handle_size) + 0.5);
    this.apply_offset();
    };
      
  div.onmouseleave = function (evt)
    {
    if(this.mouse_active)
      this.send_callback(true);
    this.mouse_active = false;
    };

  div.enable = function()
    {
    if(!this.element_disabled)
      return;
    focus_element_enable(this);
    };

  div.disable = function()
    {
    if(this.element_disabled)
      return;
    focus_element_disable(this);
    };

      
  if(cb)
    {
    div.onmousedown = function (evt)
      {
      if(this.element_disabled)
	return;
	  
      if(evt.target == this.handle)
        {
        this.mouse_active = true;
        this.mouse_pos = this.get_mouse_pos(evt);
        }
      };
       
    div.onmouseup = function (evt)
      {
      if(this.mouse_active)
        this.send_callback(true);
      this.mouse_active = false;
      };
      
    div.onmousemove = function (evt)
      {
      var delta;
      var slider_size;
      var handle_size;
      if(!this.mouse_active)
        return false;
 
      delta = this.get_mouse_pos(evt) - this.mouse_pos;
      if(!delta)
        return false;
 
      this.offset += delta;
      this.apply_offset();
      this.send_callback(false);
 
      this.mouse_pos = this.get_mouse_pos(evt);

	
      stop_propagate(evt);
      return false;
      };
    }
     
  if(focus_el)
    {
    div.onkeydown = function(evt)
      {
      if(this.element_disabled)
        return false;

      switch(evt.keyCode)
	{
        case 37: // Left
	  if(this.vertical)
	    return true;
          if(evt.shiftKey)
	    this.offset-=10;
          else
	    this.offset--;
	  this.apply_offset();
          this.send_callback(true);
          stop_propagate(evt);
          return false;
	  
          break;
        case 38: // Up	    
	  if(!this.vertical)
	    return true;
          if(evt.shiftKey)
	    this.offset-=10;
          else
	    this.offset--;
	  this.apply_offset();
          this.send_callback(true);
          stop_propagate(evt);
          return false;

          break;
        case 39: // Right	    
	  if(this.vertical)
	    return true;
          if(evt.shiftKey)
	    this.offset+=10;
          else
	    this.offset++;

	  this.apply_offset();
          this.send_callback(true);
          stop_propagate(evt);

          break;
        case 40: // Down
	  if(!this.vertical)
	    return true;
          if(evt.shiftKey)
	    this.offset+=10;
          else
	    this.offset++;

	  this.apply_offset();
          this.send_callback(true);
          stop_propagate(evt);

          break;
	}
      return true;
      };
	
    focus_el.push(div);
    }
  }

/*
 *  Menu
 */

var gui_current_menu = null;

function _gui_append_menu_icon(td, obj)
  {
  var icon;

    if((icon = make_icon(td, obj, true, 256)))
    {
    }
  }

function gui_make_menu(cb, pfx, parent)
  {
  var div;
  div = document.createElement("table");
  div.items = new Array();
  div.pfx = pfx;
 
  if(parent)
    {
    div.setAttribute("style",
                     "display: none; position: absolute; border-collapse: collapse;");
    div.parent = parent;
    }
  else
    {
    div.setAttribute("style",
                     "display: none; position: absolute; " +
                     "left: 50%; top: 50%;"+
                     "transform: translate(-50%, -50%);");
    }
  div.setAttribute("class", pfx + "menu");
  div.setAttribute("tabindex", "-1");
  div.cb = cb;
  div.cur_idx = -1;
  div.visible = false;
      
  document.body.appendChild(div);

  div.callback = function()
    {
    if(div.cb && div.cb.func)
      div.cb.func(div.items[div.cur_idx].obj);
    };
      
  div.show = function(parent_el)
    {
    this.style.display = "block";

    this.last_focus = document.activeElement;
	
    this.focus();

    if(this.parent)
      {
      var pos = get_element_position(this.parent);
      this.style.top = pos.y + pos.h + "px";
      this.style.left = pos.x + "px";
      }
    this.visible = true;

    gui_current_menu = this;
    };

  div.hide = function()
    {
    this.style.display = "none"; 
    if(this.last_focus)
      {
      this.last_focus.focus();
      delete this.last_focus;
      }
    this.visible = false;
    gui_current_menu = null;
    };

  div.onblur = function()
    {
    this.hide();
    };

  div.idx_from_name = function(name)
    {
    var i;

    for(i = 0; i < this.items.length; i++)
      {
      if(this.items[i].obj.name == name)
	return i;
      }
    return -1;
    };
      
  div.set_current = function(idx)
    {
    if(this.cur_idx < this.items.length && (this.cur_idx >= 0))
      this.items[this.cur_idx].setAttribute("class", this.pfx + "menuitem");
    this.cur_idx = idx;
    this.items[this.cur_idx].setAttribute("class", this.pfx + "menuitem_selected");
    };
      
  div.append = function(obj)
    {
    var icon;
    var td;
    var el;
    var item_el = document.createElement("tr");
    item_el.visible = true;
    this.appendChild(item_el);

    td = append_dom_element(item_el, "td");
    td.setAttribute("style",
                    "width: 2.0em; height: 2.0em; " +
		    "text-align: center; vertical-align: middle;");
    _gui_append_menu_icon(td, obj);

    td = append_dom_element(item_el, "td");
    td.setAttribute("style", "vertical-align: middle;");

    append_dom_text(td, " " + dict_get_string(obj, GAVL_META_TITLE));

    item_el.obj = obj;
    item_el.setAttribute("class", this.pfx + "menuitem");
    item_el.menu = this;
	
    item_el.onclick = function()
      {
      this.menu.hide();
      this.menu.callback();
      };

    item_el.onmousedown = function()
      {
      this.menu.items[this.menu.cur_idx].setAttribute("class", this.menu.pfx + "menuitem");
      this.menu.cur_idx = this.menu.items.indexOf(this);
      this.setAttribute("class", this.menu.pfx + "menuitem_selected");
      };

    item_el.show = function()
      {
      this.style.display = "block"; 
//      console.log("show()");
      this.visible = true;
      };

    item_el.hide = function()
      {
      this.style.display = "none"; 
      this.visible = false;
//      console.log("hide()");
      if(this.menu.cur_idx == this.menu.items.indexOf(this))
	{
        this.setAttribute("class", this.menu.pfx + "menuitem");
        this.menu.cur_idx = 0;
        while(!this.menu.items[this.menu.cur_idx].visible)
	  this.menu.cur_idx++;
        this.menu.items[this.menu.cur_idx].setAttribute("class",
							this.menu.pfx + "menuitem_selected");
        }
      };
	
    this.items.push(item_el);

    if(this.cur_idx < 0)
      this.set_current(0);

    return item_el;
    };

  div.remove  = function(obj)
    {
    var idx;
    var item_el = null;
	
    for(idx = 0; idx < this.items.length; idx++)
      {
      if(this.items[idx].obj == obj)
        {
	item_el = this.items[idx];
        break;
        }
      }
    if(!item_el)
      return;

    this.items.splice(idx, 1);
    this.removeChild(item_el);
    };

  div.clear  = function()
    {
    clear_element(div);
    this.items.splice(0, this.items.length);
    };
      
  div.onkeypress = function(evt)
    {
//    console.log("Key: " + evt.key);	
    switch(evt.key)
      {
      case "Escape":
	this.hide();
        break;
      case "Enter":
	this.hide();
	this.callback();
	break;
      case "ArrowUp":
	this.items[this.cur_idx].setAttribute("class", this.pfx + "menuitem");
	this.cur_idx--;
        if(this.cur_idx < 0)
          this.cur_idx = this.items.length - 1;
        
        while(this.items[this.cur_idx].visible == false)
          {
 	  this.cur_idx--;
          if(this.cur_idx < 0)
            this.cur_idx = this.items.length - 1;
          }
	this.items[this.cur_idx].setAttribute("class", this.pfx + "menuitem_selected");
	break;
      case "ArrowDown":
	this.items[this.cur_idx].setAttribute("class", this.pfx + "menuitem");
        this.cur_idx++;
        if(this.cur_idx == this.items.length)
          this.cur_idx = 0;
         
        while(this.items[this.cur_idx].visible == false)
          {
          this.cur_idx++;
          if(this.cur_idx >= this.items.length)
            this.cur_idx = 0;
          }
	this.items[this.cur_idx].setAttribute("class", this.pfx + "menuitem_selected");
	break;
      
      }
    };

  return div;
  }

/*
 *  Pulldown (TODO)
 */

function gui_pulldown_set_current(pulldown, name)
  {
  var obj;
  var div;
  var idx = pulldown.menu.idx_from_name(name);

  if(idx < 0)
    return false;
      
  obj = pulldown.menu.items[idx].obj;
      
  pulldown.menu.set_current(idx);

  clear_element(pulldown.label);
  clear_element(pulldown.icon);

  _gui_append_menu_icon(pulldown.icon, obj);

      
//  div = append_dom_element(pulldown.label, "div");
//  div.setAttribute("style", "display: inline-block; width: 2.0em; height: 
     
  append_dom_text(pulldown.label, dict_get_string(pulldown.menu.items[idx].obj, GAVL_META_TITLE));
  return true;
  }

function gui_pulldown_set_options(pulldown, options)
  {
  var i;
  pulldown.menu.clear();
  for(i = 0; i < options.length; i++)
    pulldown.menu.append(options[i]);
  }

function gui_make_pulldown(pfx, options, focus_el, cb)
  {
  var a;
  var el;
  var td;
  var cb1;

      
  a = document.createElement("table");
  a.cb = cb;

  a.onclick   = function(evt)
    {
    this.menu.show();
    };

  a.setAttribute("style", "vertical-align: middle; width: 100%;");
  a.setAttribute("class", pfx + "_pulldown");

  el = append_dom_element(a, "tr");
  el.setAttribute("class", pfx + "_pulldown");

  a.icon = append_dom_element(el, "td");
  a.icon.setAttribute("style",
		      "text-align: center; vertical-align: middle; " +
		      "padding: 0px; " +
		      "width: 2.2em; height: 2.0em;");
//  a.label.setAttribute("class", pfx + "_pulldown");
      
  a.label = append_dom_element(el, "td");
  a.label.setAttribute("style", "text-align: left; vertical-align: middle; padding-left: 5px; padding-right: 5px;");
//  a.label.setAttribute("class", pfx + "_pulldown");

  td =append_dom_element(el, "td");
  td.setAttribute("style", "text-align: center; vertical-align: middle; padding-left: 5px; padding-right: 5px; width: 2.0em; height: 2.0em;");
  td.setAttribute("class", pfx + "_pulldown");
  append_icon(td, "icon-chevron-down");
      
  a.options = options;

  cb1 = new Object();
  cb1.p = a;
  cb1.func = function(obj)
    {
    gui_pulldown_set_current(this.p, obj.name);
    if(this.p.cb && this.p.cb.func)
      this.p.cb.func(obj);
    };
  a.menu = gui_make_menu(cb1, pfx, a);
  return a;
  }

/* Popup window */

function _gui_popup_timeout(div)
  {
  div.hide();
  }

function gui_make_popup(div)
  {
  if(!div)
    {
    div = document.createElement("div");
    document.body.appendChild(div);
    }

  div.setAttribute("style",
		   "display: none; position: absolute; " +
		   "left: 50%; top: 50%;"+
		   "transform: translate(-50%, -50%);");

  div.callback = function()
    {
    if(div.cb && div.cb.func)
      div.cb.func(div.items[div.cur_idx].obj);
    };
      
  div.show = function(timeout)
    {
    this.style.display = "block";
    if(this.timeout)
      {
      clearTimeout(this.timeout);
      delete this.timeout;
      }
    if(timeout > 0)
      this.timeout = setTimeout(_gui_popup_timeout, timeout, this);
    };

  div.hide = function()
    {
    this.style.display = "none"; 
    if(this.timeout)
      delete this.timeout;
    };
  return div;
  }
