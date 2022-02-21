
#include <gavl/gavl.h>


#include <gmerlin/parameter.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/mdb.h>
#include <gmerlin/frontend.h>

typedef struct
  {
  char * label; // Visible in the network
  char * dbpath; // Database path (parent dir of gmerlin-mdb)

  bg_http_server_t * srv;
  bg_mdb_t * mdb;

  bg_frontend_t * fe_upnp;
  bg_frontend_t * fe_gmerlin;

  bg_parameter_info_t * parameters;

  char * vardir;

  bg_server_storage_t * storage;
  int max_client_ids;

  gavl_dictionary_t state;
  char * state_file;
  } server_t;

int server_init(server_t * s);


void server_cleanup(server_t * s);
int server_iteration(server_t * s);

void server_set_parameter(void * priv, const char * name,
                          const gavl_value_t * val);
