
#include <gmerlin/resourcemanager.h>
#include <gmerlin/utils.h>
#include <gmerlin/pluginregistry.h>

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:

      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * id;
          gavl_dictionary_t dict;
          gavl_dictionary_init(&dict);
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          gavl_msg_get_arg_dictionary_c(msg, 0, &dict);
          fprintf(stderr, "** Resource added: %s\n", id);
          gavl_dictionary_dump(&dict, 2);
          fprintf(stderr, "\n");
          gavl_dictionary_free(&dict);
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          const char * id;
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          fprintf(stderr, "** Resource deleted %s\n", id);
          }
          break;
        }

      break;
    }
  return 1;
  }

int main(int argc, char ** argv)
  {
  static gavl_dictionary_t local_res;
  bg_controllable_t * ctrl;

  bg_msg_sink_t * sink = NULL;
  
  gavl_time_t t = GAVL_TIME_SCALE/10;
  gavl_time_t t1 = 5*GAVL_TIME_SCALE;
  
  gavl_dictionary_init(&local_res);
  gavl_dictionary_set_string(&local_res, GAVL_META_LABEL, "Test resource");
  gavl_dictionary_set_string(&local_res, GAVL_META_URI, "gmerlin-renderer://192.168.2.140:5405/some-cool-stuff");
  gavl_dictionary_set_string(&local_res, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER);
  
  bg_handle_sigint();
  
  bg_plugins_init();

  sink = bg_msg_sink_create(handle_msg, NULL, 1);
  
  /* This creates the resource manager */
  ctrl = bg_resourcemanager_get_controllable();

  bg_msg_hub_connect_sink(ctrl->evt_hub, sink);
  
  bg_resourcemanager_publish(gavl_dictionary_get_string(&local_res, GAVL_META_URI), &local_res);
  
  while(1)
    {
    gavl_time_delay(&t);

    if(bg_got_sigint())
      break;
    }

  bg_resourcemanager_unpublish(gavl_dictionary_get_string(&local_res, GAVL_META_URI));

  gavl_time_delay(&t1);
  
  }


