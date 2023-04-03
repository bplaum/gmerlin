#ifndef FRONTEND_PRIV_H_INCLUDED
#define FRONTEND_PRIV_H_INCLUDED

#define BG_FRONTEND_FLAG_FINISHED (1<<0)

struct bg_frontend_s
  {
  void * priv;                // Private data

  int (*ping_func)(bg_frontend_t*, gavl_time_t current_time);
  void (*cleanup_func)(void * priv);
  
  bg_controllable_t * controllable;
  
  // Initialized by backend
  bg_control_t ctrl;

  int flags;
  };

bg_frontend_t * bg_frontend_create(bg_controllable_t * controllable);
void bg_frontend_init(bg_frontend_t *);

  
#endif // FRONTEND_PRIV_H_INCLUDED
