#ifndef FRONTEND_PRIV_H_INCLUDED
#define FRONTEND_PRIV_H_INCLUDED

#define BG_FRONTEND_FLAG_FINISHED (1<<0)

struct bg_frontend_s
  {
  bg_plugin_handle_t * handle;

  bg_control_t ctrl;
  
  //  bg_msg_sink_t * evt_sink; // Handle event from backend
  
  //   void * priv;                // Private data

  //  int (*ping_func)(void * priv);
  //  void (*cleanup_func)(void * priv);
  // int (*handle_message)(void * priv, gavl_msg_t * msg);
  
  bg_controllable_t * controllable;
  
  int flags;
  };

bg_frontend_t * bg_frontend_create(bg_controllable_t * controllable, int type_mask,
                                   const char * plugin_name);

// void bg_frontend_init(bg_frontend_t *);

  
#endif // FRONTEND_PRIV_H_INCLUDED
