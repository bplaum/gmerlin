
var ws = null;


function global_init()
  {
  var cb;
  var el;
  var parent_el;
  var ws1;
      
  /* Open Websocket */
  ws1 = new WebSocket("ws://" +  window.location.host + "/ws/renderer", ['json']);

  ws1.onopen = function()
    {
    ws = this;
    console.log("websocket open :)");
    };

  ws1.onclose = function()
    {
    ws = null;
    console.log("websocket closed :(");
    };

  ws1.onerror = function(evt)
    {
    ws = null;
    console.log("websocket error " + evt.data);
    };

  ws1.onmessage = function(evt)
    {
    console.log("Got message " + evt.data);
    };
      
  parent_el = document.getElementById("td_msg_1");

  cb = new Object();
  cb.func = function()
    {

    if(!ws)
      return;

    var str;
    var dict = new Object();

    var msg = msg_create(1, 2);

    dict_set_string(dict, "meta1", "Bla1");
    dict_set_int(dict, "meta2", 123);

    msg_set_arg_int(msg,        0, 123);
    msg_set_arg_float(msg,      1, 1.23);
    msg_set_arg_long(msg,       2, 1234567890);
    msg_set_arg_string(msg,     3, "Hallo" );
    msg_set_arg_dictionary(msg, 4, dict );

    /* TODO: Array, rgb, rgba, position */
	
    str = JSON.stringify(msg);
    console.log("Sending message: " + str);
    ws.send(str);
    };
      
  el = gui_make_button("icon-info", cb, null, "");
  parent_el.appendChild(el);

  parent_el = document.getElementById("td_msg_2");

  cb = new Object();
  cb.func = function() { console.log("Button 2 pressed"); };
      
  el = gui_make_button("icon-info", cb, null, "");
  parent_el.appendChild(el);

  
  }
