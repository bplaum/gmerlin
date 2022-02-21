

struct bg_frontend_s
  {
  void * priv;                // Private data

  int (*ping_func)(bg_frontend_t*, gavl_time_t current_time);
  void (*cleanup_func)(void * priv);
  
  bg_controllable_t * controllable;
  
  // Initialized by backend
  bg_control_t ctrl;
  };

bg_frontend_t * bg_frontend_create(bg_controllable_t * controllable);
void bg_frontend_init(bg_frontend_t *);

  
