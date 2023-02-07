#ifndef BG_UPNPUTILS_H_INCLUDED
#define BG_UPNPUTILS_H_INCLUDED

#include <gmerlin/httpserver.h>

char * bg_upnp_id_from_upnp(const char * id);
char * bg_upnp_id_to_upnp(const char * id);
char * bg_upnp_parent_id_to_upnp(const char * id);

void bg_upnp_init_server_string();
const char * bg_upnp_get_server_string();
void bg_upnp_free_server_string();

/* Send a description */
void bg_upnp_send_description(bg_http_connection_t * conn,
                              const char * desc1);

/* Finalize a SOAP request on the server side */

void bg_upnp_finish_soap_request(gavl_dictionary_t * soap,
                                 bg_http_connection_t * conn,
                                 bg_http_server_t *srv);

char * bg_upnp_create_icon_list(const gavl_array_t * arr);

/* Client understands multiple <res> elements */
#define BG_UPNP_CLIENT_MULTIPLE_RES   (1<<0)

/* Client wants the (non-standard) original location */
#define BG_UPNP_CLIENT_ORIG_LOCATION  (1<<1)  

typedef struct
  {
  const char * gmerlin;
  const char * client;
  } bg_upnp_mimetype_translation_t;

typedef struct
  {
  int (*check)(const gavl_dictionary_t * m);
  const char ** mimetypes;

  bg_upnp_mimetype_translation_t * mt;
  
  int flags;
  
  int album_thumbnail_width;
  int movie_thumbnail_width;
  } bg_upnp_client_t;

const bg_upnp_client_t * bg_upnp_detect_client(const gavl_dictionary_t * m);
int bg_upnp_client_supports_mimetype(const bg_upnp_client_t * cl, const char * mimetype);

const char *
bg_upnp_client_translate_mimetype(const bg_upnp_client_t * cl, const char * mimetype);

int bg_upnp_parse_bool(const char * str);

/* DLNA specific stuff */

const char * bg_get_dlna_image_profile(const char * mimetype, int width, int height);

char * bg_get_dlna_content_features(const gavl_dictionary_t * track, const gavl_dictionary_t * uri,
                                    int can_seek_http, int can_seek_dlna);


#endif // BG_UPNPUTILS_H_INCLUDED
