var menu = null;

function global_init()
  {
  var cb;
  var el;
  var parent_el;
  var focus_el;

  focus_el = new Array();
      
  /* Button */

  parent_el = document.getElementById("td_button");

  cb = new Object();
  cb.func = function() { console.log("Button pressed"); };
      
  el = gui_make_button("icon-info", cb, focus_el, "");
  parent_el.appendChild(el);

  /* vslider */
      
  parent_el = document.getElementById("td_vslider");
  el = parent_el.getElementsByTagName("div")[0];

  cb = new Object();
  cb.change = function(perc) { console.log("Vertical slider change: " + perc);  };
  cb.set = function(perc) { console.log("Vertical slider set: " + perc); };

  gui_make_slider(el, true, focus_el, cb);

  /* hslider */
      
  parent_el = document.getElementById("td_hslider");
  el = parent_el.getElementsByTagName("div")[0];

  cb = new Object();
  cb.change = function(perc) { console.log("Horizontal slider change: " + perc);  };
  cb.set = function(perc) { console.log("Horizontal slider set: " + perc);  };

  gui_make_slider(el, false, focus_el, cb);

  /* menu */

  cb = new Object();
  cb.func = function(obj) { console.log("Menu callback " + obj.msg); };

  menu = gui_make_menu(cb, "main");

  cb = new Object();
  cb.msg = "item1";
  cb.icon = "icon-network";
  cb.Title = "Item 1";     
  menu.append(cb);

  cb = new Object();
  cb.msg = "item2";
  cb.icon = "icon-config";
  cb.Title = "Item 2";
  menu.append(cb);

  cb = new Object();
  cb.msg = "item3";
  cb.icon = "icon-volume_min";
  cb.Title = "Item 3";
  menu.append(cb);
  
  parent_el = document.getElementById("td_menu");

  cb = new Object();
  cb.func = function() { menu.show(); };

  el = gui_make_button("icon-menu", cb, focus_el, "");
  parent_el.appendChild(el);
      
  focus_element_add_arr(focus_el);
      
  }
