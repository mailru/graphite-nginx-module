#ifndef _NGX_HTTP_GRAPHITE_MODULE_H_INCLUDED_
#define _NGX_HTTP_GRAPHITE_MODULE_H_INCLUDED_

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_graphite_allocator.h"
#include "ngx_http_graphite_array.h"

typedef struct ngx_http_graphite_storage_s {
    time_t start_time;
    time_t last_time;
    time_t event_time;

    ngx_uint_t max_interval;

    ngx_http_graphite_allocator_t *allocator;

    ngx_http_graphite_array_t *metrics;
    ngx_http_graphite_array_t *gauges;
    ngx_http_graphite_array_t *statistics;

    ngx_http_graphite_array_t *params;
    ngx_http_graphite_array_t *internals;

} ngx_http_graphite_storage_t;

typedef struct {
    struct sockaddr *sockaddr;
    socklen_t socklen;
    ngx_str_t name;
} ngx_http_graphite_server_t;

typedef struct {

    ngx_uint_t enable;

    ngx_str_t host;
    ngx_str_t protocol;
#ifdef NGX_LOG_LIMIT_ENABLED
    ngx_str_t error_log;
#endif
    ngx_shm_zone_t *shared;
    ngx_buf_t buffer;

    ngx_str_t prefix;

    ngx_http_graphite_server_t server;
    int port;
    ngx_uint_t frequency;

    ngx_array_t *sources;
    ngx_array_t *intervals;
    ngx_array_t *splits;

    ngx_uint_t timeout;

    ngx_array_t *default_params;

    size_t shared_size;
    size_t buffer_size;
    size_t package_size;

    ngx_array_t *template;

    ngx_array_t *default_data_template;
    ngx_array_t *default_data_params;
    ngx_http_complex_value_t *default_data_filter;

    ngx_array_t *datas;
    ngx_array_t *internal_values;
#ifdef NGX_LOG_LIMIT_ENABLED
    ngx_array_t *logs;
#endif

    ngx_http_graphite_storage_t *storage;

    ngx_connection_t *connection;

} ngx_http_graphite_main_conf_t;

ngx_int_t ngx_http_graphite(ngx_http_request_t *r, const ngx_str_t *name, double value);
double ngx_http_graphite_get(ngx_http_request_t *r, const ngx_str_t *name);
ngx_int_t ngx_http_graphite_set(ngx_http_request_t *r, const ngx_str_t *name, double value);

#endif
