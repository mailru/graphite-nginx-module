#ifndef _NGX_HTTP_GRAPHITE_NET_H_INCLUDED_
#define _NGX_HTTP_GRAPHITE_NET_H_INCLUDED_

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_graphite_module.h"

ngx_int_t ngx_http_graphite_net_send_buffer(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log);

#endif
