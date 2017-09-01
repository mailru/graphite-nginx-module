#ifndef _NGX_HTTP_GRAPHITE_MODULE_H_INCLUDED_
#define _NGX_HTTP_GRAPHITE_MODULE_H_INCLUDED_

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

ngx_int_t ngx_http_graphite(ngx_http_request_t *r, const ngx_str_t *name, double value, const char *config);

#endif
