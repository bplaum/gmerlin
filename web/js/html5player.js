
function html5player_set_time(player, time, duration)
  {
  var time_dict = new Object();

  if(duration > 0)
    time_dict[BG_PLAYER_TIME_PERC] = value_create(GAVL_TYPE_FLOAT, time/duration);
  else
    time_dict[BG_PLAYER_TIME_PERC] = value_create(GAVL_TYPE_FLOAT, -1.0);
      
  time_dict[BG_PLAYER_TIME] = value_create(GAVL_TYPE_LONG, time);

  html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME,
                              value_create(GAVL_TYPE_DICTIONARY, time_dict));
      
  }

function html5player_stop(player)
  {
  var time_dict = new Object();
  var obj = new Object();
  var m = new Object();

  var status = html5player_get_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;

      
  if(!player.el)
    return;

  if(status == BG_PLAYER_STATUS_PLAYING)
    player.el.pause();
      
//  console.log("Stop " + player.el.src);

  player.el.removeAttribute("src");
      
//  player.el.src = "";
  delete player.el;
      
  dict_set_dictionary(obj, GAVL_META_METADATA, m);

  time_dict[BG_PLAYER_TIME_PERC] = value_create(GAVL_TYPE_FLOAT, 0);
  time_dict[BG_PLAYER_TIME] = value_create(GAVL_TYPE_LONG, 0);

  html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME,
                              value_create(GAVL_TYPE_DICTIONARY, time_dict));
      
  html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                              value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_STOPPED));
  html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                              value_create(GAVL_TYPE_DICTIONARY, obj));

  }

function html5player_ended_callback()
  {
  var next_idx;
  
//  console.log("html5player ended");

  next_idx = html5player_next_track(this.player, false);

  if(next_idx < 0)
    {
//    console.log("Nothing more to play");
    html5player_stop(this.player);

    }
  else
    {
//    console.log("Next track " + next_idx);

    html5player_set_current_track(this.player, next_idx);
    html5player_play(this.player);
    }
      
  }

function html5player_error_callback()
  {
  console.log("html5player error");

  }

function html5player_pause_callback()
  {

  }

function html5player_play_callback()
  {
//  console.log("html5player_play_callback");


      
  }

function html5player_seeking_callback()
  {
  
  }

function html5player_seeked_callback()
  {
  
  
  }

function html5player_timeupdate_callback()
  {
//  console.log("html5player_timeupdate_callback");
  var status = html5player_get_state_local(this.player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;

  switch(status)
    {
    case BG_PLAYER_STATUS_PLAYING:
    case BG_PLAYER_STATUS_PAUSED:
    case BG_PLAYER_STATUS_SEEKING:
      html5player_set_time(this.player, this.currentTime * GAVL_TIME_SCALE, this.player.duration);
      break;
    }
  
  }

function html5player_set_state_local(player, ctx, name, value)
  {
  var response = msg_create(BG_MSG_STATE_CHANGED, BG_MSG_NS_STATE);
  msg_set_arg_int(response, 0, 1); // Last
  msg_set_arg_string(response, 1, ctx);
  msg_set_arg_string(response, 2, name);
  msg_set_arg_val(response, 3, value);

  if(!player.state[ctx])
    player.state[ctx] = new Object();

  player.state[ctx][name] = value;
  fire_event(player.cb, response);
  }

function html5player_get_state_local(player, ctx, name)
  {
  if(!player.state[ctx])
    return null;

  return player.state[ctx][name];
  }

function html5player_create_shuffle_list(player)
  {
  var i;
  var swp;
  var idx;
      
  if(player.shuffle_list)
    return;
      
  /* Initialize */
  player.shuffle_list = new Array();    
  for(i = 0; i < player.tracks.length; i++)
    player.shuffle_list[i] = i;

  /* Shuffle */

  for(i = 0; i < player.tracks.length; i++)
    {
    idx = Math.floor(Math.random() * player.tracks.length );

    swp = player.shuffle_list[i];

    player.shuffle_list[i] = player.shuffle_list[idx];
	
    player.shuffle_list[idx] = swp;
    }

  player.cur_idx = html5player_unshuffle(player, player.cur_idx_real);

  }

// idx -> real_idx
function html5player_shuffle(player, idx)
  {
  if(player.shuffle_list)
    {
//    console.log("Shuffle " + player.shuffle_list.length);
    return player.shuffle_list[idx];
    }
  else
    return idx;
  }

// real_idx -> idx
function html5player_unshuffle(player, real_idx)
  {
  var i;

  if(player.shuffle_list)
    {
      for(i = 0; i < player.shuffle_list.length; i++)
      {
	if(real_idx == player.shuffle_list[i])
	  return i;
      }
      
      return -1;
    }
  else
    return real_idx;
    
  }

function html5player_delete_shuffle_list(player)
  {
  if(player.shuffle_list)
    delete player.shuffle_list;

  player.idx = player.idx_real;
  }

/* These functions return the cur_idx of the player */

function html5player_next_track(player, force)
  {
  var ret = -1;
  var mode = html5player_get_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE).v;

  if(mode == BG_PLAYER_MODE_SHUFFLE)
    html5player_create_shuffle_list(player);
  else
    html5player_delete_shuffle_list(player);

  if(!force)
    {
    switch(mode)
      {
      case BG_PLAYER_MODE_NORMAL:
        ret = player.cur_idx + 1;
	if(ret >= player.tracks.length)
          {
	  ret = -1;
          }
	break;
      case BG_PLAYER_MODE_SHUFFLE:
      case BG_PLAYER_MODE_REPEAT:
	ret = player.cur_idx + 1;
	if(ret >= player.tracks.length)
	  ret = 0;
	break;
      case BG_PLAYER_MODE_ONE:
        ret = -1;
	break;
      case BG_PLAYER_MODE_LOOP:
	ret = player.cur_idx;
	break;
      }
    return ret;
    }

  ret = player.cur_idx + 1;
  if(ret >= player.tracks.length)
    ret = 0;

  return ret;
  }

function html5player_previous_track(player)
  {
  var ret = -1;
  var mode = html5player_get_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE).v;

  if(mode == BG_PLAYER_MODE_SHUFFLE)
    html5player_create_shuffle_list(player);
  else
    html5player_delete_shuffle_list(player);

  ret = player.cur_idx - 1;
  if(ret < 0)
    ret = player.tracks.length-1;

  return ret;
  }

function html5player_set_current_track(player, idx, id)
  {
  var id;   
  var i;
  var m;
  var real_idx = -1;

  var status = html5player_get_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;
  
  if(status != BG_PLAYER_STATUS_STOPPED)
    html5player_stop(player);
      
  if(player.cur)
    {
    delete player.cur;
    player.cur_idx = -1;
    player.cur_idx_real = -1;
    }

  if(idx < 0)
    {
    for(i = 0; i < player.tracks.length; i++)
      {
      if(id == obj_get_id(player.tracks[i].v))
        {
        real_idx = i;
        break;
	}
      }

    if(real_idx >= 0)
      idx = html5player_unshuffle(player, real_idx);
    else
      {	
	console.log("Didn't find track for ID " + id + " total tracks: " + player.tracks.length);
	console.trace();
      }
    }

  if(idx >= 0)
    {
    player.cur_idx = idx;
    player.cur_idx_real = html5player_shuffle(player, idx);

    html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_QUEUE_IDX,
                                value_create(GAVL_TYPE_INT, player.cur_idx_real));

    player.cur = player.tracks[player.cur_idx_real].v;
    }

  if(!player.cur)
    return;

  m = player.cur[GAVL_META_METADATA].v;

  player.duration = dict_get_long(m, GAVL_META_APPROX_DURATION);
      
  /* TODO: Decide which player to use */
  player.el = player.audio_el; 
      
  }

function html5player_can_play(p, src)
  {
  var supp;

  console.log("Can play " + JSON.stringify(src));
    
  if(!src[GAVL_META_URI].v.startsWith("http://") &&
     !src[GAVL_META_URI].v.startsWith("https://"))
    return false;
    
  supp = p.el.canPlayType(src[GAVL_META_MIMETYPE].v);
	
  if((supp == "maybe") || (supp == "probably"))
    return true;
  else
    return false;
 
  }

function html5player_play(p, do_pause)
  {
  var m;
  var src;
  var i;
  var el;
  var uri = null;
    
  console.log("html5player_play");
  
  if(!p.cur || !p.el)
    {
    if((p.cur_idx < 0) && (p.tracks && (p.tracks.length > 0)))
      {
      let mode = html5player_get_state_local(p, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE).v;
      console.log("html5player_play");
  
      p.cur_idx = 0;
      if(mode == BG_PLAYER_MODE_SHUFFLE)
        html5player_create_shuffle_list(p);

      p.cur_idx = 0;
      
      p.cur_idx_real = html5player_shuffle(p, p.cur_idx);
      }
    if(p.tracks && (p.cur_idx >= 0) && (p.cur_idx < p.tracks.length))
      {
      html5player_set_current_track(p, p.cur_idx);
      }
    else
      {
      console.log("No track selected");
      return;
      }
    }
  /* Get proper source */

  if(!(m = p.cur[GAVL_META_METADATA].v))
    {
    console.log("Invalid track 1");
    return;
    }

  if(!(src = m[GAVL_META_SRC]) ||
     !(src = src.v))
    {
    console.log("Invalid track 2");
    return;
    }

  if(is_array(src))
    {
    for(i = 0; i < src.length; i++)
      {
      if(html5player_can_play(p, src[i].v))
        {
        uri = src[i].v[GAVL_META_URI].v;
        break;
        }
      }
    }
  else if(is_object(src))
    {
 
    if(html5player_can_play(p, src))
      {
      uri = src[GAVL_META_URI].v;
      }
    }

  if(uri)
    p.el.setAttribute("src", uri);
  else
    {
    console.log("No supported uri found");
    return;
    }
    
  p.el.play();

  html5player_set_state_local(p, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                              value_create(GAVL_TYPE_DICTIONARY, p.cur ));

  if(do_pause)
    {
    p.el.pause();
    html5player_set_state_local(p, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_PAUSED));

    }
  else
    html5player_set_state_local(p, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_PLAYING));

  }

function html5player_set_state(player, ctx, name, val)
  {
//  console.log("set_state " + name + " " + ctx + " " + JSON.stringify(val));
  switch(ctx)
    {
    case BG_PLAYER_STATE_CTX:
      switch(name)
        {
        case BG_PLAYER_STATE_MODE:
           if(val.v == BG_PLAYER_MODE_SHUFFLE)
             html5player_create_shuffle_list(player);
	   else
             html5player_delete_shuffle_list(player);
           break;
        case BG_PLAYER_STATE_VOLUME:
//          console.log("Setting volume: " + val.v);
          if(player.audio_el)
            player.audio_el.volume = val.v;
          if(player.video_el)
            player.video_el.volume = val.v;
          break;
        case BG_PLAYER_STATE_MUTE:
          if(player.audio_el)
            player.audio_el.muted = !!val.v;
          if(player.video_el)
            player.video_el.muted = !!val.v;
          break;

        }
    case BG_PLAYER_STATE_CTX + "/" + BG_PLAYER_STATE_CURRENT_TIME:
      switch(name)
	{
        case BG_PLAYER_TIME_PERC:
	  {
          var status = html5player_get_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;
          if((status == BG_PLAYER_STATUS_PLAYING) ||
             (status == BG_PLAYER_STATUS_PAUSED) &&
	     (player.duration > 0))
	    {
//            console.log("Seek perc: " + val.v);

            html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                        value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_SEEKING));
		
            player.el.currentTime = val.v * player.duration / GAVL_TIME_SCALE;
            html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                        value_create(GAVL_TYPE_INT, status));
	    }
          break;
	  }

	}
    }

  html5player_set_state_local(player, ctx, name, val);

  }

function html5player_set_msg_playqueue(player, evt)
  {
  var obj = clone_object(player.playqueue, false);

  delete obj[GAVL_META_CHILDREN];

  dict_set_string(evt.header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);

  msg_set_arg_dictionary(evt, 0, obj);

  }

function html5player_update_playqueue(player)
  {
  var i;
  var num_children = 0;
  var duration = 0;
  var track_duration = 0;
  var m;

  for(i = 0; i < player.tracks.length; i++)
    {
    if(!(m = dict_get_dictionary(player.tracks[i].v, GAVL_META_METADATA)))
      {
      duration = -1;
      break;
      }
    track_duration = dict_get_long(m, GAVL_META_APPROX_DURATION);

    if(track_duration > 0)
      duration += track_duration;
    else
      {
      duration = -1;
      break;
      }
    }

  m = dict_get_dictionary(player.playqueue, GAVL_META_METADATA);

  dict_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, player.tracks.length);
  dict_set_int(m, GAVL_META_NUM_CHILDREN, player.tracks.length);

  if(duration > 0)  
    dict_set_long(m, GAVL_META_APPROX_DURATION, duration);
  else
    delete m[GAVL_META_APPROX_DURATION];

  var evt = msg_create(BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

  html5player_set_msg_playqueue(player, evt);
    
  fire_event(player.cb, evt);

  }

function html5player_set_playqueue_id(dict)
  {
  var m;
  var id;

//  console.log("html5player_set_playqueue_id " + JSON.stringify(dict));
 

  m = dict_get_dictionary(dict, GAVL_META_METADATA);

  id = obj_make_playqueue_id(dict);

  dict_set_string(m, GAVL_META_ID, id);
  obj_clear_tag(dict, GAVL_META_NEXT_ID);
  obj_clear_tag(dict, GAVL_META_PREVIOUS_ID);
  }

function html5player_handle_msg(msg)
  {
  var idx;
  var ctx;
  var name;
  var val;
  var delta_val;
  var response;
  var status; 
  var mode;

  var del;
  var add;
  var resp;

//  console.log("html5player_handle_msg " + JSON.stringify(msg));

  switch(msg.ns)
    {
    case BG_MSG_NS_STATE:
      switch(msg.id)
        {
        case BG_CMD_SET_STATE:
          {

          ctx = msg.args[1].v;
          name = msg.args[2].v;
          val = msg.args[3];

          html5player_set_state(this, ctx, name, val)
          return;
	  }
    
          break;
        case BG_CMD_SET_STATE_REL:
          {
          ctx = msg.args[1].v;
          name = msg.args[2].v;
          delta_val = msg.args[3];

          val = html5player_get_state_local(this, ctx, name);
	      
//          console.log("set_state rel " + name + " " + ctx + " " + JSON.stringify(val.v) + " " + JSON.stringify(delta_val.v));

          switch(ctx)
            {
            case BG_PLAYER_STATE_CTX:
              switch(name)
                {
                case BG_PLAYER_STATE_MODE:
                  val.v += delta_val.v;

                  if(val.v >= BG_PLAYER_MODE_MAX)
                    val.v = 0;
                  if(val.v < 0)
                    val.v = BG_PLAYER_MODE_MAX-1;

                  html5player_set_state(this, ctx, name, val);
		    
                  break;
                case BG_PLAYER_STATE_VOLUME:
                  val.v += delta_val.v;
                  if(val.v > 1.0)
                    val.v = 1.0;
                  if(val.v < 0.0)
	            val.v = 0.0;
                  html5player_set_state(this, ctx, name, val);
                  break;
                case BG_PLAYER_STATE_MUTE:
                  val.v += delta_val.v;
                  if(val.v > 1)
                    val.v = 0;
                  if(val.v < 0)
                    val.v = 1;
                  html5player_set_state(this, ctx, name, val);
                  break;
                }
              break;
	    }
          return;
	  }
          break;
        }
      break;
    case BG_MSG_NS_PLAYER:
      switch(msg.id)
        {
        case BG_PLAYER_CMD_PLAY:
          status = html5player_get_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;

          switch(status)
            {
            case BG_PLAYER_STATUS_PAUSED:

                html5player_set_state_local(player, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                            value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_PLAYING));

		this.el.play();
              break;
            case BG_PLAYER_STATUS_STOPPED:
              html5player_play(this)
              break;

	    }

	  
          break;
        case BG_PLAYER_CMD_PLAY_BY_ID:
          {
          html5player_set_current_track(this, -1, msg.args[0].v);
          html5player_play(this)
          }
          break;
        case BG_PLAYER_CMD_STOP:
          status = html5player_get_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;

          switch(status)
            {
            case BG_PLAYER_STATUS_PLAYING:
              this.el.pause();
              html5player_stop(this);
              break;
            case BG_PLAYER_STATUS_PAUSED:
              html5player_stop(this);
              break;
	    }
	      
	  
	    break;
        case BG_PLAYER_CMD_PAUSE:
          
          status = html5player_get_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;

          switch(status)
            {
            case BG_PLAYER_STATUS_PAUSED:
              this.el.play();
                html5player_set_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                            value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_PLAYING));

		break;
            case BG_PLAYER_STATUS_PLAYING:
		this.el.pause();
                html5player_set_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                                            value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_PAUSED));
		break;
	    }
	  
          break;
        case BG_PLAYER_CMD_NEXT:
          status = html5player_get_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;
          html5player_stop(this)
          idx = html5player_next_track(this, true); 
	      
          if(idx < 0)
            return;
	      
          switch(status)
            {
            case BG_PLAYER_STATUS_PLAYING:
              html5player_set_current_track(this, idx);
	      html5player_play(this);
              break;
            case BG_PLAYER_STATUS_PAUSED:
              html5player_set_current_track(this, idx);
	      html5player_play(this, true);
              break;
            case BG_PLAYER_STATUS_STOPPED:
              this.cur_idx = idx;
              this.cur_idx_real = html5player_shuffle(this, idx);
              break;
	    }
    
          break;
        case BG_PLAYER_CMD_PREV:
          status = html5player_get_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS).v;
          html5player_stop(this)
          idx = html5player_previous_track(this);

          if(idx < 0)
            return;

          switch(status)
            {
            case BG_PLAYER_STATUS_PLAYING:
              html5player_set_current_track(this, idx);
	      html5player_play(this);
              break;
            case BG_PLAYER_STATUS_PAUSED:
              html5player_set_current_track(this, idx);
	      html5player_play(this, true);
              break;
            case BG_PLAYER_STATUS_STOPPED:
              this.cur_idx = idx;
              this.cur_idx_real = html5player_shuffle(this, idx);
              break;
	    }

          break;
        case BG_PLAYER_CMD_SET_CURRENT_TRACK:
          html5player_set_current_track(this, -1, msg.args[0].v);

	      
              
          break;
	}
      break;
    case BG_MSG_NS_DB:
      switch(msg.id)
        {
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          let old_length = this.tracks.length;
	    
          if(dict_get_string(msg.header, GAVL_MSG_CONTEXT_ID) != BG_PLAYQUEUE_ID)
            return
//          console.log("html5player Splice children");
          idx = msg.args[0].v;
          del = msg.args[1].v;

          if(msg.args[2])
            add = msg.args[2].v;

          if(idx < 0)
            idx = this.tracks.length;
          if(del < 0)
            del = this.tracks.length - idx;

          if(del)
            this.tracks.splice(idx, del);

          if(msg.args[2])
            {
	    if(is_array(msg.args[2].v))
              {
              var i;
              for(i = 0; i < msg.args[2].v.length; i++)
                {
                let obj = clone_object(msg.args[2].v[i].v, true);
                html5player_set_playqueue_id(obj);
                this.tracks.splice(idx + i, 0, value_create(GAVL_TYPE_DICTIONARY, obj));
                }
              }
            else if(is_object(msg.args[2].v))
	      {
              let obj = clone_object(msg.args[2].v, true);
              html5player_set_playqueue_id(obj);
              this.tracks.splice(idx, 0, value_create(GAVL_TYPE_DICTIONARY, obj));
	      }
            }

          mode = html5player_get_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE).v;
          if(mode == BG_PLAYER_MODE_SHUFFLE)
	    {
            html5player_delete_shuffle_list(this);
	    html5player_create_shuffle_list(this);
	    }

          response = msg_create(BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
	      
          dict_set_string(response.header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);

          msg_set_arg_int(response, 0, idx);
          msg_set_arg_int(response, 1, del);
          msg_set_arg_val(response, 2, msg.args[2]);
	
          fire_event(this.cb, response);

          html5player_set_state_local(this, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_QUEUE_LEN,
                                      value_create(GAVL_TYPE_INT, this.tracks.length));
          html5player_update_playqueue(this);

          if((old_length == 0) && (this.tracks.length > 0))
            {
	    this.cur_idx = -1;
            this.cur_idx_real = -1;
	    }
	    
          break;
	  }
        case BG_FUNC_DB_BROWSE_CHILDREN:
//	  console.log("html5player Browse children");
          resp = set_browse_children_response(msg, this.tracks)
          fire_event(this.cb, resp);
          break;
        case BG_FUNC_DB_BROWSE_OBJECT:

	  
 
	  resp = msg_create(BG_RESP_DB_BROWSE_OBJECT, BG_MSG_NS_DB);

          if(msg.header[GAVL_MSG_CLIENT_ID])
	    resp.header[GAVL_MSG_CLIENT_ID] = msg.header[GAVL_MSG_CLIENT_ID];

          if(msg.header[GAVL_MSG_FUNCTION_TAG])
	    resp.header[GAVL_MSG_FUNCTION_TAG] = msg.header[GAVL_MSG_FUNCTION_TAG];
	  
//  gavl_msg_copy_header_field(dst, src, GAVL_MSG_CLIENT_ID);
//  gavl_msg_copy_header_field(dst, src, GAVL_MSG_CONTEXT_ID);
//  gavl_msg_copy_header_field(dst, src, GAVL_MSG_FUNCTION_TAG);
	  
          html5player_set_msg_playqueue(this, resp);
          fire_event(this.cb, resp);

	  break;

	}
      break;
    }


  }

function html5player_set_callbacks(el)
  {
  /* Set callbacks */
  el.onended = html5player_ended_callback;
  el.onerror = html5player_error_callback;
  el.onpause = html5player_pause_callback;
  el.onplay = html5player_play_callback;
  el.onseeking = html5player_seeking_callback;
  el.onseeked = html5player_seeked_callback;
  el.ontimeupdate = html5player_timeupdate_callback;
  }

function html5player_create(cb, audio_id, video_id, playqueue)
  {
  var ret;
  var vol_init = 0.5;
  
  ret = new Object();
  ret.audio_el = document.getElementById(audio_id);

  ret.audio_el.volume = vol_init;
      
  ret.tracks = new Array();

  ret.playqueue = new Object();
  dict_set_array(ret.playqueue, GAVL_META_CHILDREN, ret.tracks);
  dict_set_dictionary(ret.playqueue, GAVL_META_METADATA, new Object());

  obj_set_string(ret.playqueue, GAVL_META_LABEL, "Web player queue");

  ret.state = new Object();

  ret.cur_idx = 0;
  ret.cur_idx_real = 0;
      
  html5player_set_callbacks(ret.audio_el)

  ret.handle_msg = html5player_handle_msg;
  ret.cb = cb;

  ret.audio_el.player = ret;
      
  html5player_set_state_local(ret, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                              value_create(GAVL_TYPE_INT, BG_PLAYER_STATUS_STOPPED));
  html5player_set_state_local(ret, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE,
                              value_create(GAVL_TYPE_INT, BG_PLAYER_MODE_NORMAL));
  html5player_set_state_local(ret, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_VOLUME,
                              value_create(GAVL_TYPE_FLOAT, vol_init));
  html5player_set_state_local(ret, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MUTE,
                              value_create(GAVL_TYPE_INT, 0));

  html5player_set_state_local(ret, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_QUEUE_IDX,
                              value_create(GAVL_TYPE_INT, 0));
  html5player_set_state_local(ret, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_QUEUE_LEN,
                              value_create(GAVL_TYPE_INT, 0));

  html5player_set_time(ret, 0.0, -1.0)
  
      
  return ret;
  }

