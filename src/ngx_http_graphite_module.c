#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_graphite_module.h"
#include "ngx_http_graphite_net.h"

typedef struct {
    ngx_array_t *default_data_template;
    ngx_array_t *default_data_params;
    ngx_http_complex_value_t *default_data_filter;

    ngx_array_t *datas;
} ngx_http_graphite_srv_conf_t;

typedef struct {
    ngx_array_t *datas;
} ngx_http_graphite_loc_conf_t;

#define PHASE_CONFIG 0
#define PHASE_REQUEST 1

#define SPLIT_EMPTY (ngx_uint_t)-1
#define SPLIT_INTERNAL (ngx_uint_t)-2

#define SOURCE_INTERNAL (ngx_uint_t)-1

static ngx_int_t ngx_http_graphite_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_process_init(ngx_cycle_t *cycle);

static void *ngx_http_graphite_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_srv_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_graphite_merge_loc_conf(ngx_conf_t* cf, void* parent, void* child);

static ngx_str_t *ngx_http_graphite_location(ngx_pool_t *pool, const ngx_str_t *uri);
static ngx_str_t *ngx_http_graphite_server_name(ngx_pool_t *pool, ngx_conf_t* cf);

#if (NGX_SSL)
static ngx_int_t ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);
#endif

static char *ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_default_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_http_graphite_add_default_data(ngx_conf_t *cf, ngx_array_t *datas, const ngx_str_t *location, const ngx_str_t *server_name, const ngx_array_t *template, const ngx_array_t *params, const ngx_http_complex_value_t *filter);
static char *ngx_http_graphite_add_data(ngx_conf_t *cf, ngx_array_t *datas, const ngx_str_t *split, const ngx_array_t *params, const ngx_http_complex_value_t *filter);

typedef struct ngx_http_graphite_context_s {
    ngx_int_t phase;
    ngx_http_graphite_storage_t *storage;
    ngx_pool_t *pool;
    ngx_log_t *log;
    ngx_http_graphite_main_conf_t *gmcf;
} ngx_http_graphite_context_t;

static char *ngx_http_graphite_config_arg_prefix(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_host(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_server(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_port(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_frequency(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_intervals(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_params(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_shared(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_buffer(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_package(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_template(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_protocol(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_timeout(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
#ifdef NGX_LOG_LIMIT_ENABLED
static char *ngx_http_graphite_config_arg_error_log(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
#endif

static char *ngx_http_graphite_param_arg_name(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_aggregate(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_interval(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_percentile(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value);

static char *ngx_http_graphite_parse_params(ngx_http_graphite_context_t *context, const ngx_str_t *value, ngx_array_t *params);
static char *ngx_http_graphite_parse_string(ngx_http_graphite_context_t *context, ngx_str_t *value, ngx_str_t *result);
static char *ngx_http_graphite_parse_size(ngx_http_graphite_context_t *context, ngx_str_t *value, size_t *result);
static char *ngx_http_graphite_parse_time(ngx_http_graphite_context_t *context, ngx_str_t *value, ngx_uint_t *result);

static ngx_command_t ngx_http_graphite_commands[] = {

    { ngx_string("graphite_config"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_ANY,
      ngx_http_graphite_config,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("graphite_param"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE2 | NGX_CONF_TAKE3 | NGX_CONF_TAKE4,
      ngx_http_graphite_param,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("graphite_default_data"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE12,
      ngx_http_graphite_default_data,
      0,
      0,
      NULL },

    { ngx_string("graphite_data"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE123,
      ngx_http_graphite_data,
      0,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t ngx_http_graphite_module_ctx = {
    ngx_http_graphite_add_variables,       /* preconfiguration */
    ngx_http_graphite_init,                /* postconfiguration */

    ngx_http_graphite_create_main_conf,    /* create main configuration */
    NULL,                                  /* init main configuration */

    ngx_http_graphite_create_srv_conf,     /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_graphite_create_loc_conf,     /* create location configuration */
    ngx_http_graphite_merge_loc_conf,      /* merge location configuration */
};

ngx_module_t ngx_http_graphite_module = {
    NGX_MODULE_V1,
    &ngx_http_graphite_module_ctx,         /* module context */
    ngx_http_graphite_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_graphite_process_init,        /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_str_t graphite_shared_name = ngx_string("graphite_shared");

static ngx_http_variable_t ngx_http_graphite_vars[] = {

#if (NGX_SSL)
    { ngx_string("ssl_session_reused"), NULL, ngx_http_graphite_ssl_session_reused, 0, NGX_HTTP_VAR_CHANGEABLE, 0 },
#endif

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};

typedef char *(*ngx_http_graphite_arg_handler_pt)(ngx_http_graphite_context_t *, void *, ngx_str_t *);

typedef struct ngx_http_graphite_arg_s {
    ngx_str_t name;
    ngx_http_graphite_arg_handler_pt handler;
    ngx_str_t deflt;
} ngx_http_graphite_arg_t;

#ifdef NGX_GRAPHITE_PATCH
#define DEFAULT_PARAMS "request_time|bytes_sent|body_bytes_sent|request_length|ssl_handshake_time|ssl_cache_usage|content_time|gzip_time|upstream_time|upstream_connect_time|upstream_header_time|rps|keepalive_rps|response_2xx_rps|response_3xx_rps|response_4xx_rps|response_5xx_rps"
#else
#define DEFAULT_PARAMS "request_time|bytes_sent|body_bytes_sent|request_length|ssl_cache_usage|rps|keepalive_rps|response_2xx_rps|response_3xx_rps|response_4xx_rps|response_5xx_rps"
#endif

#define CONFIG_ARGS_COUNT (sizeof(ngx_http_graphite_config_args) / sizeof(ngx_http_graphite_config_args[0]))

static const ngx_http_graphite_arg_t ngx_http_graphite_config_args[] = {
    { ngx_string("prefix"), ngx_http_graphite_config_arg_prefix, ngx_null_string },
    { ngx_string("host"), ngx_http_graphite_config_arg_host, ngx_null_string },
    { ngx_string("server"), ngx_http_graphite_config_arg_server, ngx_null_string },
    { ngx_string("port"), ngx_http_graphite_config_arg_port, ngx_string("2003") },
    { ngx_string("frequency"), ngx_http_graphite_config_arg_frequency, ngx_string("60") },
    { ngx_string("intervals"), ngx_http_graphite_config_arg_intervals, ngx_string("1m") },
    { ngx_string("params"), ngx_http_graphite_config_arg_params, ngx_string(DEFAULT_PARAMS)},
    { ngx_string("shared"), ngx_http_graphite_config_arg_shared, ngx_string("1m") },
    { ngx_string("buffer"), ngx_http_graphite_config_arg_buffer, ngx_string("64k") },
    { ngx_string("package"), ngx_http_graphite_config_arg_package, ngx_string("1400") },
    { ngx_string("template"), ngx_http_graphite_config_arg_template, ngx_null_string },
    { ngx_string("protocol"), ngx_http_graphite_config_arg_protocol, ngx_string("udp") },
    { ngx_string("timeout"), ngx_http_graphite_config_arg_timeout, ngx_string("100") },
#ifdef NGX_LOG_LIMIT_ENABLED
    { ngx_string("error_log"), ngx_http_graphite_config_arg_error_log, ngx_null_string },
#endif
};

#define PARAM_ARGS_COUNT 4

static const ngx_http_graphite_arg_t ngx_http_graphite_param_args[PARAM_ARGS_COUNT] = {
    { ngx_string("name"), ngx_http_graphite_param_arg_name, ngx_null_string },
    { ngx_string("aggregate"), ngx_http_graphite_param_arg_aggregate, ngx_null_string },
    { ngx_string("interval"), ngx_http_graphite_param_arg_interval, ngx_null_string },
    { ngx_string("percentile"), ngx_http_graphite_param_arg_percentile, ngx_null_string },
};

static ngx_event_t timer;

typedef struct ngx_http_graphite_interval_s {
    ngx_str_t name;
    ngx_uint_t value;
} ngx_http_graphite_interval_t;

typedef struct ngx_http_graphite_metric_data_s {
    double value;
    ngx_uint_t count;
} ngx_http_graphite_metric_data_t;

typedef struct ngx_http_graphite_gauge_data_s {
    double value;
} ngx_http_graphite_gauge_data_t;

#define P2_METRIC_COUNT 5

typedef struct ngx_http_graphite_statistic_data_s {
    double q[P2_METRIC_COUNT];
    double dn[P2_METRIC_COUNT];
    double np[P2_METRIC_COUNT];
    ngx_int_t n[P2_METRIC_COUNT];
    ngx_uint_t count;
} ngx_http_graphite_statistic_data_t;

typedef double (*ngx_http_graphite_aggregate_pt)(const ngx_http_graphite_interval_t*, const void*);

static double ngx_http_graphite_aggregate_avg(const ngx_http_graphite_interval_t *interval, const void *data);
static double ngx_http_graphite_aggregate_persec(const ngx_http_graphite_interval_t *interval, const void *data);
static double ngx_http_graphite_aggregate_sum(const ngx_http_graphite_interval_t *interval, const void *data);
static double ngx_http_graphite_aggregate_gauge(const ngx_http_graphite_interval_t *interval, const void *data);

typedef struct ngx_http_graphite_aggregate_s {
    ngx_str_t name;
    ngx_http_graphite_aggregate_pt get;
} ngx_http_graphite_aggregate_t;

#define AGGREGATE_COUNT 4

static const ngx_http_graphite_aggregate_t ngx_http_graphite_aggregates[AGGREGATE_COUNT] = {
    { ngx_string("avg"), ngx_http_graphite_aggregate_avg },
    { ngx_string("persec"), ngx_http_graphite_aggregate_persec },
    { ngx_string("sum"), ngx_http_graphite_aggregate_sum },
    { ngx_string("gauge"), ngx_http_graphite_aggregate_gauge },
};

struct ngx_http_graphite_source_s;
typedef double (*ngx_http_graphite_source_handler_pt)(const struct ngx_http_graphite_source_s *source, ngx_http_request_t*);

typedef struct ngx_http_graphite_source_s {
    ngx_str_t name;
    ngx_flag_t re;
    ngx_http_graphite_source_handler_pt get;
    ngx_http_graphite_aggregate_pt aggregate;
    ngx_uint_t type;
} ngx_http_graphite_source_t;

static double ngx_http_graphite_source_request_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_bytes_sent(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_body_bytes_sent(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_request_length(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_ssl_cache_usage(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_keepalive_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_response_2xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_response_3xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_response_4xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_response_5xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_response_xxx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_upstream_cache_status_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
#ifdef NGX_GRAPHITE_PATCH
static double ngx_http_graphite_source_ssl_handshake_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_content_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_gzip_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_upstream_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_upstream_connect_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_upstream_header_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
static double ngx_http_graphite_source_lua_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r);
#endif

static const ngx_http_graphite_source_t ngx_http_graphite_sources[] = {
    { .name = ngx_string("request_time"), .get = ngx_http_graphite_source_request_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("bytes_sent"), .get = ngx_http_graphite_source_bytes_sent, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("body_bytes_sent"), .get = ngx_http_graphite_source_body_bytes_sent, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("request_length"), .get = ngx_http_graphite_source_request_length, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("ssl_cache_usage"), .get = ngx_http_graphite_source_ssl_cache_usage, .aggregate = ngx_http_graphite_aggregate_avg, .type = NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF },
    { .name = ngx_string("rps"), .get = ngx_http_graphite_source_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("keepalive_rps"), .get = ngx_http_graphite_source_keepalive_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_2xx_rps"), .get = ngx_http_graphite_source_response_2xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_3xx_rps"), .get = ngx_http_graphite_source_response_3xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_4xx_rps"), .get = ngx_http_graphite_source_response_4xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_5xx_rps"), .get = ngx_http_graphite_source_response_5xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_\\d\\d\\d_rps"), .re = 1, .get = ngx_http_graphite_source_response_xxx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("upstream_cache_(miss|bypass|expired|stale|updating|revalidated|hit)_rps"), .re = 1, .get = ngx_http_graphite_source_upstream_cache_status_rps, .aggregate = ngx_http_graphite_aggregate_persec },
#ifdef NGX_GRAPHITE_PATCH
    { .name = ngx_string("ssl_handshake_time"), .get = ngx_http_graphite_source_ssl_handshake_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("content_time"), .get = ngx_http_graphite_source_content_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("gzip_time"), .get = ngx_http_graphite_source_gzip_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("upstream_time"), .get = ngx_http_graphite_source_upstream_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("upstream_connect_time"), .get = ngx_http_graphite_source_upstream_connect_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("upstream_header_time"), .get = ngx_http_graphite_source_upstream_header_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("lua_time"), .get = ngx_http_graphite_source_lua_time, .aggregate = ngx_http_graphite_aggregate_avg },
#endif
};

const size_t SOURCE_COUNT = sizeof(ngx_http_graphite_sources) / sizeof(ngx_http_graphite_source_t);

typedef struct ngx_http_graphite_param_s {
    ngx_str_t name;
    ngx_uint_t source;
    ngx_http_graphite_interval_t interval;
    ngx_http_graphite_aggregate_pt aggregate;
    ngx_uint_t percentile;
    ngx_http_graphite_array_t *percentiles;
} ngx_http_graphite_param_t;

typedef struct ngx_http_graphite_metric_s {
    ngx_uint_t split;
    ngx_uint_t param;
    ngx_http_graphite_metric_data_t *data;
} ngx_http_graphite_metric_t;

typedef struct ngx_http_graphite_gauge_s {
    ngx_uint_t split;
    ngx_uint_t param;
    ngx_http_graphite_gauge_data_t *data;
} ngx_http_graphite_gauge_t;

typedef struct ngx_http_graphite_statistic_s {
    ngx_uint_t split;
    ngx_uint_t param;
    ngx_http_graphite_statistic_data_t *data;
} ngx_http_graphite_statistic_t;

typedef struct ngx_http_graphite_data_s {
    ngx_http_graphite_array_t *metrics;
    ngx_http_graphite_array_t *gauges;
    ngx_http_graphite_array_t *statistics;
    ngx_http_complex_value_t *filter;
} ngx_http_graphite_data_t;

typedef struct ngx_http_graphite_internal_s {
    ngx_str_t name;
    ngx_http_graphite_data_t data;
} ngx_http_graphite_internal_t;

typedef struct ngx_http_graphite_internal_value_s {
    time_t ts;
    ngx_uint_t internal;
    double value;
} ngx_http_graphite_internal_value_t;

#ifdef NGX_LOG_LIMIT_ENABLED
typedef struct ngx_http_graphite_log_s {
    ngx_str_t name;
    ngx_array_t *error_logs; /* of (ngx_log_t *) */
} ngx_http_graphite_log_t;
#endif

typedef struct ngx_http_graphite_reqctx_s {
    ngx_array_t *internal_values;
} ngx_http_graphite_reqctx_t;

typedef struct ngx_http_graphite_template_arg_s {
    ngx_str_t name;
    int variable;
} ngx_http_graphite_template_arg_t;

typedef struct ngx_http_graphite_template_s {
    ngx_str_t data;
    int variable;
} ngx_http_graphite_template_t;

static char *ngx_http_graphite_template_compile(ngx_http_graphite_context_t *context, ngx_array_t *template, const ngx_http_graphite_template_arg_t *args, size_t nargs, const ngx_str_t *value);
static u_char *ngx_http_graphite_template_execute(u_char* buffer, size_t buffer_size, const ngx_array_t *template, const ngx_str_t *variables[]);
static size_t ngx_http_graphite_template_len(const ngx_array_t *template, const ngx_str_t *variables[]);

typedef enum {
    TEMPLATE_VARIABLE_PREFIX,
    TEMPLATE_VARIABLE_HOST,
    TEMPLATE_VARIABLE_SPLIT,
    TEMPLATE_VARIABLE_PARAM,
    TEMPLATE_VARIABLE_INTERVAL,
} ngx_http_graphite_template_variable_t;

#define TEMPLATE_ARG_COUNT 5

static const ngx_http_graphite_template_arg_t ngx_http_graphite_template_args[TEMPLATE_ARG_COUNT] = {
    { ngx_string("prefix"), TEMPLATE_VARIABLE_PREFIX },
    { ngx_string("host"), TEMPLATE_VARIABLE_HOST },
    { ngx_string("split"), TEMPLATE_VARIABLE_SPLIT },
    { ngx_string("param"), TEMPLATE_VARIABLE_PARAM },
    { ngx_string("interval"), TEMPLATE_VARIABLE_INTERVAL },
};

#define TEMPLATE_VARIABLES(prefix, host, split, param, interval) {prefix, host, split, param, interval}

typedef enum {
    TEMPLATE_VARIABLE_LOCATION,
    TEMPLATE_VARIABLE_SERVER,
} ngx_http_graphite_default_data_variable_t;

#define DEFAULT_DATA_ARG_COUNT 2

static const ngx_http_graphite_template_arg_t ngx_http_graphite_default_data_args[DEFAULT_DATA_ARG_COUNT] = {
    { ngx_string("location"), TEMPLATE_VARIABLE_LOCATION },
    { ngx_string("server"), TEMPLATE_VARIABLE_SERVER },
};

#define DEFAULT_DATA_VARIABLES(location, server_name) {location, server_name}

static ngx_http_complex_value_t *ngx_http_graphite_complex_compile(ngx_conf_t *cf, ngx_str_t *value);

static ngx_int_t ngx_http_graphite_handler(ngx_http_request_t *r);
static void ngx_http_graphite_timer_handler(ngx_event_t *ev);
static ngx_int_t ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data);
static void ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *gmcf, time_t ts);

static ngx_int_t
ngx_http_graphite_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t *var, *v;

    for (v = ngx_http_graphite_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}

#ifdef NGX_LOG_LIMIT_ENABLED
static ngx_http_graphite_log_t *
ngx_http_graphite_search_log(
    ngx_http_graphite_main_conf_t *gmcf, const ngx_str_t *name)
{
    ngx_array_t *logs = gmcf->logs;

    for (ngx_uint_t i = 0; i != logs->nelts; i++) {
        ngx_http_graphite_log_t *gl = &((ngx_http_graphite_log_t*)logs->elts)[i];

        if (gl->name.len == name->len
            && !ngx_strncmp(gl->name.data, name->data, name->len))
            return gl;
    }
    return NULL;
}

static ngx_int_t
ngx_http_graphite_init_error_log(ngx_conf_t *cf) {
    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    if (!gmcf->error_log.len)
        return NGX_OK;

    ngx_log_conf_t *lcf;
    lcf = (ngx_log_conf_t*)ngx_get_conf(cf->cycle->conf_ctx, ngx_errlog_module);

    for (ngx_log_t *log = lcf->global_logs; log != NULL;
         log = log->global_next) {

        ngx_str_t *file_name;

        if (log->writer != NULL || log->file == NULL
            || !(file_name = &log->file->name)->len) {

            /* skip non-files */
            continue;
        }

        u_char name_data[file_name->len];
        ngx_str_t log_name = {.len = sizeof(name_data), .data = name_data};

        for (size_t i = 0; i != log_name.len; i++) {
            u_char c = file_name->data[i];

            if (!isalnum(c))
                c = '_';
            log_name.data[i] = c;
        }

        ngx_http_graphite_log_t *gl;

        gl = ngx_http_graphite_search_log(gmcf, &log_name);
        if (gl == NULL) {
            if ((gl = ngx_array_push(gmcf->logs)) == NULL) {
                ngx_conf_log_error(
                    NGX_LOG_ERR, cf, 0, "graphite can't allocate memory "
                    "for a new log object");
                return NGX_ERROR;
            }
            gl->name.len = log_name.len;
            gl->name.data = ngx_pstrdup(cf->pool, &log_name);
            gl->error_logs = ngx_array_create(cf->pool, 1, sizeof(ngx_log_t *));
            if (gl->name.data == NULL || gl->error_logs == NULL) {
                ngx_conf_log_error(
                    NGX_LOG_ERR, cf, 0, "graphite can't allocate memory "
                    "for internals of a new log object");
                return NGX_ERROR;
            }
        }

        ngx_log_t **plog;

        if ((plog = ngx_array_push(gl->error_logs)) == NULL) {
            ngx_conf_log_error(
                NGX_LOG_ERR, cf, 0, "graphite can't allocate memory "
                "to store an nginx log object");
            return NGX_ERROR;
        }
        *plog = log;
    }
    return NGX_OK;
}
#endif

static ngx_int_t
ngx_http_graphite_init(ngx_conf_t *cf) {

    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;

#ifdef NGX_LOG_LIMIT_ENABLED
    if (ngx_http_graphite_init_error_log(cf) == NGX_ERROR)
        return NGX_ERROR;
#endif

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL)
        return NGX_ERROR;

    *h = ngx_http_graphite_handler;

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_process_init(ngx_cycle_t *cycle) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_graphite_module);

    if (gmcf && gmcf->enable) {

        ngx_memzero(&timer, sizeof(timer));
        timer.handler = ngx_http_graphite_timer_handler;
        timer.data = gmcf;
        timer.log = cycle->log;
        ngx_add_timer(&timer, gmcf->frequency);
    }

    return NGX_OK;
}

static void *
ngx_http_graphite_create_main_conf(ngx_conf_t *cf) {

    ngx_http_graphite_main_conf_t *gmcf;
    gmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_main_conf_t));

    if (gmcf == NULL)
        return NULL;

    gmcf->sources = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_source_t));
    gmcf->splits = ngx_array_create(cf->pool, 1, sizeof(ngx_str_t));
    gmcf->intervals = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_interval_t));
    gmcf->default_params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    gmcf->template = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gmcf->default_data_template = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gmcf->default_data_params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    gmcf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));
    gmcf->internal_values = ngx_array_create(cf->pool, 16, sizeof(ngx_http_graphite_internal_value_t));
#ifdef NGX_LOG_LIMIT_ENABLED
    gmcf->logs = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_log_t));
#endif

    if (gmcf->sources == NULL || gmcf->splits == NULL || gmcf->intervals == NULL || gmcf->default_params == NULL || gmcf->template == NULL || gmcf->default_data_template == NULL || gmcf->default_data_params == NULL || gmcf->datas == NULL || gmcf->internal_values == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    gmcf->storage = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_storage_t));
    if (gmcf->storage == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    ngx_http_graphite_allocator_t *allocator = ngx_palloc(cf->pool, sizeof(ngx_http_graphite_allocator_t));
    if (allocator == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }
    ngx_http_graphite_allocator_init(allocator, cf->pool, ngx_http_graphite_allocator_pool_alloc, ngx_http_graphite_allocator_pool_free);

    gmcf->storage->allocator = allocator;
    gmcf->storage->params = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_http_graphite_param_t));
    gmcf->storage->internals = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_http_graphite_internal_t));
    gmcf->storage->metrics = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_http_graphite_metric_t));
    gmcf->storage->gauges = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_http_graphite_gauge_t));
    gmcf->storage->statistics = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_http_graphite_statistic_t));

    if (gmcf->storage->params == NULL || gmcf->storage->internals == NULL || gmcf->storage->metrics == NULL || gmcf->storage->gauges == NULL || gmcf->storage->statistics == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    return gmcf;
}

static void *
ngx_http_graphite_create_srv_conf(ngx_conf_t *cf) {

    ngx_http_graphite_srv_conf_t *gscf;
    gscf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_srv_conf_t));

    if (gscf == NULL)
        return NULL;

    gscf->default_data_template = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gscf->default_data_params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    gscf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));

    if (gscf->default_data_template == NULL || gscf->default_data_params == NULL || gscf->datas == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    return gscf;
}

static void *
ngx_http_graphite_create_loc_conf(ngx_conf_t *cf) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_loc_conf_t *glcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);
    glcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_loc_conf_t));

    if (glcf == NULL)
        return NULL;

    glcf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));
    if (glcf->datas == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    if (!cf->args)
        return glcf;

    ngx_str_t *directive = &((ngx_str_t*)cf->args->elts)[0];
    ngx_str_t loc = ngx_string("location");

    if (cf->args->nelts >= 2 && directive->len == loc.len && ngx_strncmp(directive->data, loc.data, loc.len) == 0) {
        ngx_http_graphite_srv_conf_t *gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);

        ngx_str_t *uri = &((ngx_str_t*)cf->args->elts)[cf->args->nelts - 1];
        ngx_str_t *location = ngx_http_graphite_location(cf->pool, uri);
        ngx_str_t *server_name = ngx_http_graphite_server_name(cf->pool, cf);

        if (server_name == NULL) {
            return NULL;
        }

        if (server_name->len == 0) {
            return glcf;
        }

        if (location == NULL)
            return NULL;

        if (location->len == 0)
            return glcf;

        if (ngx_http_graphite_add_default_data(cf, glcf->datas, location, server_name, gmcf->default_data_template, gmcf->default_data_params, gmcf->default_data_filter) != NGX_CONF_OK)
            return NULL;

        if (ngx_http_graphite_add_default_data(cf, glcf->datas, location, server_name, gscf->default_data_template, gscf->default_data_params, gscf->default_data_filter) != NGX_CONF_OK)
            return NULL;
    }

    return glcf;
}

static char *
ngx_http_graphite_merge_loc_conf(ngx_conf_t* cf, void* parent, void* child) {
    ngx_http_graphite_loc_conf_t *prev = parent;
    ngx_http_graphite_loc_conf_t *conf = child;

    size_t i;
    for (i = 0; i < prev->datas->nelts; i++) {
        ngx_http_graphite_data_t *prev_data = &((ngx_http_graphite_data_t*)prev->datas->elts)[i];
        ngx_http_graphite_data_t *data = ngx_array_push(conf->datas);
        if (!data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        *data = *prev_data;
    }

    return NGX_CONF_OK;
}

static ngx_str_t *
ngx_http_graphite_server_name(ngx_pool_t *pool, ngx_conf_t* cf) {

    ngx_str_t *split = ngx_palloc(pool, sizeof(ngx_str_t));
    split->len = 0;
    if (!split)
        return NULL;

    ngx_http_core_srv_conf_t *cscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_core_module);
    if (!cscf) {
        return split;
    }
    if (!cscf->server_names.nelts) {
        return split;
    }
    if (!cscf->server_names.elts) {
        return split;
    }
    ngx_http_server_name_t *value;
    value = cscf->server_names.elts;
    ngx_str_t *server_name_old = &value[0].name;


    split->data = ngx_palloc(pool, server_name_old->len);
    ngx_uint_t i;
    for (i = 0; i < server_name_old->len; ++i) {
        if (isalnum(server_name_old->data[i]))
            split->data[split->len++] = server_name_old->data[i];
        else
            split->data[split->len++] = '_';
    }
    while (split->len > 0 && split->data[0] == '_') {
        split->data++;
        split->len--;
    }
    while (split->len > 0 && split->data[split->len - 1] == '_')
        split->len--;

    return split;
}

static ngx_str_t *
ngx_http_graphite_location(ngx_pool_t *pool, const ngx_str_t *uri) {

    ngx_str_t *split = ngx_palloc(pool, sizeof(ngx_str_t));
    if (!split)
        return NULL;

    split->data = ngx_palloc(pool, uri->len);
    split->len = 0;

    if (!split->data)
        return NULL;

    size_t i;
    for (i = 0; i < uri->len; i++) {
        if (isalnum(uri->data[i]))
            split->data[split->len++] = uri->data[i];
        else
            split->data[split->len++] = '_';
    }

    while (split->len > 0 && split->data[0] == '_') {
        split->data++;
        split->len--;
    }

    while (split->len > 0 && split->data[split->len - 1] == '_')
        split->len--;

    if (split->len == 0) {
        split->data[split->len++] = '_';
    }

    return split;
}

#if (NGX_SSL)
static ngx_int_t
ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data) {

    ngx_str_t s;

    if (r->connection->requests == 1) {
        if (r->connection->ssl && SSL_session_reused(r->connection->ssl->connection)) {
            ngx_str_set(&s, "yes");
        }
        else {
            ngx_str_set(&s, "no");
        }
    }
    else {
        ngx_str_set(&s, "none");
    }

    v->len = s.len;
    v->data = s.data;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}
#endif

static ngx_http_graphite_context_t
ngx_http_graphite_context_from_config(ngx_conf_t *cf) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_http_graphite_context_t context;
    context.phase = PHASE_CONFIG;
    context.storage = gmcf->storage;
    context.pool = cf->pool;
    context.log = cf->log;
    context.gmcf = gmcf;

    return context;
}

static ngx_http_graphite_context_t
ngx_http_graphite_context_from_request(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    ngx_http_graphite_storage_t *storage = NULL;

    if (gmcf->enable) {
        ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
        storage = (ngx_http_graphite_storage_t*)shpool->data;
    }

    ngx_http_graphite_context_t context;
    context.phase = PHASE_REQUEST;
    context.storage = storage;
    context.pool = NULL;
    context.log = r->connection->log;
    context.gmcf = gmcf;
    return context;
}

static char *
ngx_http_graphite_parse_args(ngx_http_graphite_context_t *context, const ngx_array_t *vars, void *conf, const ngx_http_graphite_arg_t *args, ngx_uint_t args_count) {

    ngx_uint_t isset[args_count];
    ngx_memzero(isset, args_count * sizeof(ngx_uint_t));

    ngx_uint_t i;
    for (i = 1; i < vars->nelts; i++) {
        ngx_str_t *var = &((ngx_str_t*)vars->elts)[i];

        ngx_uint_t found = 0;
        ngx_uint_t j;
        for (j = 0; j < args_count; j++) {
            const ngx_http_graphite_arg_t *arg = &args[j];

            if (!ngx_strncmp(arg->name.data, var->data, arg->name.len) && var->data[arg->name.len] == '=') {
                isset[j] = 1;
                found = 1;
                ngx_str_t value;
                value.data = var->data + arg->name.len + 1;
                value.len = var->len - (arg->name.len + 1);

                if (arg->handler(context, conf, &value) == NGX_CONF_ERROR)
                    return NGX_CONF_ERROR;
                break;
            }
        }

        if (!found) {
            ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite unknown option %V", var);
            return NGX_CONF_ERROR;
        }
    }

    for (i = 0; i < args_count; i++) {
        if (!isset[i]) {

            const ngx_http_graphite_arg_t *arg = &args[i];
            if (arg->deflt.len) {
                if (arg->handler(context, conf, (ngx_str_t*)&arg->deflt) == NGX_CONF_ERROR) {
                      ngx_log_error(NGX_LOG_CRIT, context->log, 0, "graphite invalid option default value %V", &arg->deflt);
                      return NGX_CONF_ERROR;
                }
            }
        }
    }

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_graphite_init_data(ngx_http_graphite_context_t *context, ngx_http_graphite_data_t *data) {

    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;

    data->metrics = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_uint_t));
    data->gauges = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_uint_t));
    data->statistics = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_uint_t));
    if (data->metrics == NULL || data->gauges == NULL || data->statistics == NULL) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_ERROR;
    }
    data->filter = NULL;

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_add_param_to_config(ngx_http_graphite_context_t *context, const ngx_http_graphite_param_t *param) {

    ngx_http_graphite_array_t *params = context->storage->params;

    ngx_uint_t p;
    for (p = 0; p < params->nelts; p++) {
        ngx_http_graphite_param_t *old_param = &((ngx_http_graphite_param_t*)params->elts)[p];
        if (param->name.len == old_param->name.len && !ngx_strncmp(param->name.data, old_param->name.data, param->name.len)) {
            if (!param->percentile && !old_param->percentile) {
                if (param->aggregate == old_param->aggregate && param->interval.value == old_param->interval.value)
                    return p;
                else {
                    ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param with different aggregate or interval");
                    return NGX_ERROR;
                }
            }
            else if (param->percentile == old_param->percentile)
                return p;
        }
    }

    ngx_http_graphite_param_t *new_param = ngx_http_graphite_array_push(params);
    if (!new_param) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_ERROR;
    }

    *new_param = *param;

    return params->nelts - 1;
}

static char *
ngx_http_graphite_add_param_to_data(ngx_http_graphite_context_t *context, ngx_uint_t split, ngx_uint_t param, ngx_http_graphite_data_t *data) {

    ngx_http_graphite_storage_t *storage = context->storage;

    ngx_http_graphite_param_t *p = &((ngx_http_graphite_param_t*)storage->params->elts)[param];

    if (p->percentile == 0 && p->aggregate != ngx_http_graphite_aggregate_gauge) {
        ngx_uint_t i;
        for (i = 0; i < storage->metrics->nelts; i++) {
            ngx_http_graphite_metric_t *metric = &((ngx_http_graphite_metric_t*)storage->metrics->elts)[i];
            if (metric->split == split && metric->param == param)
                break;
        }

        if (i == storage->metrics->nelts) {
            ngx_http_graphite_metric_t *metric = ngx_http_graphite_array_push(storage->metrics);
            if (!metric) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            metric->split = split;
            metric->param = param;
            if (context->phase == PHASE_REQUEST) {
                metric->data = ngx_http_graphite_allocator_alloc(storage->allocator, sizeof(ngx_http_graphite_metric_data_t) * (storage->max_interval + 1));
                if (metric->data == NULL) {
                    ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                    return NGX_CONF_ERROR;
                }
                ngx_memzero(metric->data, sizeof(ngx_http_graphite_metric_data_t) * (storage->max_interval + 1));
            }
            else
                metric->data = NULL;
        }

        ngx_uint_t *m = ngx_http_graphite_array_push(data->metrics);
        if (!m) {
            ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        *m = i;
    }
    else if (p->aggregate == ngx_http_graphite_aggregate_gauge) {
        ngx_uint_t i;
        for (i = 0; i < storage->gauges->nelts; i++) {
            ngx_http_graphite_gauge_t *gauge = &((ngx_http_graphite_gauge_t*)storage->gauges->elts)[i];
            if (gauge->split == split && gauge->param == param)
                break;
        }

        if (i == storage->gauges->nelts) {
            ngx_http_graphite_gauge_t *gauge = ngx_http_graphite_array_push(storage->gauges);
            if (!gauge) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            gauge->split = split;
            gauge->param = param;
            if (context->phase == PHASE_REQUEST) {
                gauge->data = ngx_http_graphite_allocator_alloc(storage->allocator, sizeof(ngx_http_graphite_gauge_data_t));
                if (gauge->data == NULL) {
                    ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                    return NGX_CONF_ERROR;
                }
                ngx_memzero(gauge->data, sizeof(ngx_http_graphite_gauge_data_t));
            }
            else
                gauge->data = NULL;
        }

        ngx_uint_t *g = ngx_http_graphite_array_push(data->gauges);
        if (!g) {
            ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        *g = i;
    }
    else if (p->percentile != 0) {
        ngx_uint_t i;
        for (i = 0; i < storage->statistics->nelts; i++) {
            ngx_http_graphite_statistic_t *statistic = &((ngx_http_graphite_statistic_t*)storage->statistics->elts)[i];
            if (statistic->split == split && statistic->param == param)
                break;
        }

        if (i == storage->statistics->nelts) {
            ngx_http_graphite_statistic_t *statistic = ngx_http_graphite_array_push(storage->statistics);
            if (!statistic) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            statistic->split = split;
            statistic->param = param;
            if (context->phase == PHASE_REQUEST) {
                statistic->data = ngx_http_graphite_allocator_alloc(storage->allocator, sizeof(ngx_http_graphite_statistic_data_t));
                if (statistic->data == NULL) {
                    ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                    return NGX_CONF_ERROR;
                }
                ngx_memzero(statistic->data, sizeof(ngx_http_graphite_statistic_data_t));
            }
            else
                statistic->data = NULL;
        }

        ngx_uint_t *s = ngx_http_graphite_array_push(data->statistics);
        if (!s) {
            ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        *s = i;
    }
    else {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite unknown error");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_add_param_to_params(ngx_http_graphite_context_t *context, ngx_uint_t param, ngx_array_t *params) {

    ngx_http_graphite_main_conf_t *gmcf = context->gmcf;

    ngx_uint_t i;
    for (i = 0; i < params->nelts; i++) {
        ngx_uint_t p = ((ngx_uint_t*)params->elts)[i];
        if (p == param) {
            ngx_http_graphite_param_t *old_param = &((ngx_http_graphite_param_t*)gmcf->storage->params->elts)[p];
            if (old_param->percentile == 0) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite duplicate param %V", &old_param->name);
            }
            else {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite duplicate param %V/%ui", &old_param->name, old_param->percentile);
            }
            return NGX_CONF_ERROR;
        }
    }

    ngx_uint_t *new_param = ngx_array_push(params);
    if (!new_param) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    *new_param = param;

    return NGX_CONF_OK;
}

#define HOST_LEN 256

static char *
ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_config(cf);

    if (ngx_http_graphite_parse_args(&context, cf->args, conf, ngx_http_graphite_config_args, CONFIG_ARGS_COUNT) == NGX_CONF_ERROR)
        return NGX_CONF_ERROR;

    ngx_http_graphite_main_conf_t *gmcf = conf;

    if (gmcf->host.len == 0) {
        char host[HOST_LEN];
        gethostname(host, HOST_LEN);
        host[HOST_LEN - 1] = '\0';
        char *dot = strchr(host, '.');
        if (dot)
            *dot = '\0';

        size_t host_size = strlen(host);

        gmcf->host.data = ngx_palloc(cf->pool, host_size);
        if (!gmcf->host.data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        ngx_memcpy(gmcf->host.data, host, host_size);
        gmcf->host.len = host_size;
    }

    if (gmcf->server.name.len == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config server not set");
        return NGX_CONF_ERROR;
    }
    if (gmcf->protocol.len == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config protocol is not specified");
        return NGX_CONF_ERROR;
    }
    else if (ngx_strncmp(gmcf->protocol.data, "udp", 3) != 0 && ngx_strncmp(gmcf->protocol.data, "tcp", 3) != 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config invalid protocol");
        return NGX_CONF_ERROR;
    }

    if (gmcf->port < 1 || gmcf->port > 65535) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config port must be in range form 1 to 65535");
        return NGX_CONF_ERROR;
    }

    if (gmcf->frequency < 1 || gmcf->frequency > 65535) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config frequency must be in range form 1 to 65535");
        return NGX_CONF_ERROR;
    }

    if (gmcf->intervals->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config intervals not set");
        return NGX_CONF_ERROR;
    }

    if (gmcf->default_params->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config params not set");
        return NGX_CONF_ERROR;
    }

    if (gmcf->shared_size == 0 || gmcf->buffer_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config shared must be positive value");
        return NGX_CONF_ERROR;
    }

    if (gmcf->buffer_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config buffer must be positive value");
        return NGX_CONF_ERROR;
    }

    if (gmcf->package_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config package must be positive value");
        return NGX_CONF_ERROR;
    }

    if (gmcf->shared_size < sizeof(ngx_slab_pool_t)) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite too small shared memory");
        return NGX_CONF_ERROR;
    }

    ngx_url_t u;
    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = gmcf->server.name;
    u.default_port = gmcf->port;

    if (ngx_parse_url(cf->pool, &u) != NGX_OK) {
        if (u.err)
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "%s in resolver \"%V\"", u.err, &u.url);
        return NGX_CONF_ERROR;
    }

    gmcf->server.sockaddr = u.addrs[0].sockaddr;
    gmcf->server.socklen = u.addrs[0].socklen;

    ngx_str_t graphite_shared_id;
    graphite_shared_id.len = graphite_shared_name.len + 32;
    graphite_shared_id.data = ngx_palloc(cf->pool, graphite_shared_id.len);
    if (!graphite_shared_id.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    ngx_snprintf(graphite_shared_id.data, graphite_shared_id.len, "%V.%T", &graphite_shared_name, ngx_time());

    gmcf->shared = ngx_shared_memory_add(cf, &graphite_shared_id, gmcf->shared_size, &ngx_http_graphite_module);
    if (!gmcf->shared) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc shared memory");
        return NGX_CONF_ERROR;
    }
    if (gmcf->shared->data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite shared memory is used");
        return NGX_CONF_ERROR;
    }
    gmcf->shared->init = ngx_http_graphite_shared_init;
    gmcf->shared->data = gmcf;

    gmcf->buffer.start = ngx_palloc(cf->pool, gmcf->buffer_size);
    if (!gmcf->buffer.start) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    gmcf->buffer.end = gmcf->buffer.start + gmcf->buffer_size;

    gmcf->enable = 1;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_default_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_config(cf);

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);
    ngx_http_graphite_srv_conf_t *gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

    ngx_str_t *args = cf->args->elts;

    ngx_array_t *template = NULL;
    ngx_array_t *params = NULL;
    ngx_http_complex_value_t **filter;

    if (cf->cmd_type == NGX_HTTP_MAIN_CONF) {
        template = gmcf->default_data_template;
        params = gmcf->default_data_params;
        filter = &gmcf->default_data_filter;
    }
    else if (cf->cmd_type == NGX_HTTP_SRV_CONF) {
        template = gscf->default_data_template;
        params = gscf->default_data_params;
        filter = &gscf->default_data_filter;
    }
    else
        return NGX_CONF_ERROR;

    if (ngx_http_graphite_template_compile(&context, template, ngx_http_graphite_default_data_args, DEFAULT_DATA_ARG_COUNT, &args[1]) != NGX_CONF_OK)
        return NGX_CONF_ERROR;

    if (cf->args->nelts >= 3) {
        ngx_uint_t i;
        for (i = 2; i < cf->args->nelts; i++) {
            if (ngx_strncmp(args[i].data, "params=", 7) == 0) {
                ngx_str_t value;
                value.len = args[i].len - 7;
                value.data = args[i].data + 7;
                if (ngx_http_graphite_parse_params(&context, &value, params) != NGX_CONF_OK)
                    return NGX_CONF_ERROR;
            }
            else if (ngx_strncmp(args[i].data, "if=", 3) == 0) {
                ngx_str_t value;
                value.len = args[i].len - 3;
                value.data = args[i].data + 3;

                *filter = ngx_http_graphite_complex_compile(cf, &value);
                if (!*filter)
                    return NGX_CONF_ERROR;
            }
            else {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknown option %V", &args[i]);
                return NGX_CONF_ERROR;
            }
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_config(cf);

    ngx_http_graphite_main_conf_t *gmcf = context.gmcf;
    ngx_http_graphite_srv_conf_t *gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);
    ngx_http_graphite_loc_conf_t *glcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable)
        return NGX_CONF_OK;

    ngx_str_t *args = cf->args->elts;

    ngx_str_t *split = &args[1];
    ngx_array_t *params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    if (!params) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }
    ngx_http_complex_value_t *filter = NULL;

    if (cf->args->nelts >= 3) {
        ngx_uint_t i;
        for (i = 2; i < cf->args->nelts; i++) {
            if (ngx_strncmp(args[i].data, "params=", 7) == 0) {
                ngx_str_t value;
                value.len = args[i].len - 7;
                value.data = args[i].data + 7;
                if (ngx_http_graphite_parse_params(&context, &value, params) != NGX_CONF_OK)
                    return NGX_CONF_ERROR;
            }
            else if (ngx_strncmp(args[i].data, "if=", 3) == 0) {
                ngx_str_t value;
                value.len = args[i].len - 3;
                value.data = args[i].data + 3;

                filter = ngx_http_graphite_complex_compile(cf, &value);
                if (!filter)
                    return NGX_CONF_ERROR;
            }
            else {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknown option %V", &args[i]);
                return NGX_CONF_ERROR;
            }
        }
    }

    ngx_array_t *datas;

    if (cf->cmd_type & NGX_HTTP_MAIN_CONF)
        datas = gmcf->datas;
    else if (cf->cmd_type & NGX_HTTP_SRV_CONF)
        datas = gscf->datas;
    else
        datas = glcf->datas;

    if (ngx_http_graphite_add_data(cf, datas, split, params, filter) != NGX_CONF_OK)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_graphite_search_param(const ngx_http_graphite_array_t *internals, const ngx_str_t *name, ngx_int_t *found) {
    size_t i;
    for (i = 0; i < internals->nelts; i++) {
        ngx_http_graphite_internal_t *internal = &((ngx_http_graphite_internal_t*)internals->elts)[i];
        size_t min_len = internal->name.len < name->len ? internal->name.len : name->len;
        ngx_int_t r = ngx_strncmp(internal->name.data, name->data, min_len);

        if (r == 0 && internal->name.len == name->len) {
            *found = 1;
            return i;
        }

        if (r > 0)
            return i;
    }
    return i;
}

static ngx_int_t
ngx_http_graphite_parse_param_args(ngx_http_graphite_context_t *context, const ngx_array_t *args, ngx_http_graphite_param_t *param) {

    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;

    ngx_memzero(param, sizeof(ngx_http_graphite_param_t));

    param->percentiles = ngx_http_graphite_array_create(allocator, 1, sizeof(ngx_uint_t));
    if (!param->percentiles) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_ERROR;
    }

    if (ngx_http_graphite_parse_args(context, args, param, ngx_http_graphite_param_args, PARAM_ARGS_COUNT) == NGX_CONF_ERROR)
        return NGX_ERROR;

    param->source = SOURCE_INTERNAL;

    ngx_int_t r = NGX_OK;

    if (r == NGX_OK && !param->name.data) {
        r = NGX_ERROR;
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param name not set");
    }

    if (r == NGX_OK && (param->aggregate && param->aggregate != ngx_http_graphite_aggregate_gauge && !param->interval.value)) {
        r = NGX_ERROR;
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param must contain interval with aggregate avg, persec or sum");
    }

    if (r == NGX_OK && ((!param->aggregate || param->aggregate == ngx_http_graphite_aggregate_gauge) && param->interval.value)) {
        r = NGX_ERROR;
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param must contain aggregate avg, persec or sum with interval or aggregate gauge");
    }

    if (r == NGX_OK && (param->aggregate == ngx_http_graphite_aggregate_gauge && param->percentiles->nelts != 0)) {
        r = NGX_ERROR;
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param must contain aggregate gauge or percentile only");
    }

    if (r == NGX_OK && (!param->aggregate && !param->interval.value && param->percentiles->nelts == 0)) {
        r = NGX_ERROR;
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param must contain aggregate avg, perse or sum and interval, aggregate gauge or percentile");
    }

    if (r == NGX_OK && (param->interval.value > context->storage->max_interval)) {
        r = NGX_ERROR;
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param interval value is greather than max interval");
    }

    if (r != NGX_OK) {
        ngx_http_graphite_allocator_free(allocator, param->name.data);
        ngx_http_graphite_allocator_free(allocator, param->interval.name.data);
        ngx_http_graphite_array_destroy(param->percentiles);
    }

    return r;
}

static ngx_int_t
ngx_http_graphite_add_internal(ngx_http_graphite_context_t *context, const ngx_http_graphite_param_t *param) {

    ngx_http_graphite_storage_t *storage = context->storage;

    ngx_int_t found = 0;
    ngx_uint_t i = ngx_http_graphite_search_param(storage->internals, &param->name, &found);

    ngx_http_graphite_data_t data;
    if (!found) {
        if (ngx_http_graphite_init_data(context, &data) == NGX_ERROR)
            return NGX_ERROR;
    }
    else
        data = ((ngx_http_graphite_internal_t*)storage->internals->elts)[i].data;

    ngx_http_graphite_param_t new_param = *param;

    size_t n;
    for (n = 0; n < param->percentiles->nelts + 1; n++) {
        if (n == 0) {
            if (!new_param.aggregate)
                continue;
        }
        else
            new_param.percentile = ((ngx_uint_t*)new_param.percentiles->elts)[n - 1];

        ngx_uint_t p = ngx_http_graphite_add_param_to_config(context, &new_param);
        if ((ngx_int_t)p == NGX_ERROR)
            return NGX_ERROR;

        if (ngx_http_graphite_add_param_to_data(context, SPLIT_INTERNAL, p, &data) != NGX_CONF_OK)
            return NGX_ERROR;
    }

    if (!found) {
        ngx_http_graphite_internal_t *internal = ngx_http_graphite_array_push(storage->internals);
        if (internal == NULL) {
            ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
            return NGX_ERROR;
        }

        ngx_memmove(&((ngx_http_graphite_internal_t*)storage->internals->elts)[i + 1], &((ngx_http_graphite_internal_t*)storage->internals->elts)[i], (storage->internals->nelts - i - 1) * sizeof(ngx_http_graphite_internal_t));

        internal = &((ngx_http_graphite_internal_t*)storage->internals->elts)[i];
        internal->name = param->name;
        internal->data = data;
    }

    return i;
}

static char *
ngx_http_graphite_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_config(cf);

    ngx_http_graphite_main_conf_t *gmcf = context.gmcf;

    if (!gmcf->enable)
        return NGX_CONF_OK;

    ngx_http_graphite_param_t param;
    if (ngx_http_graphite_parse_param_args(&context, cf->args, &param) != NGX_OK)
        return NGX_CONF_ERROR;

    if (ngx_http_graphite_add_internal(&context, &param) == NGX_ERROR)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_prefix(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    return ngx_http_graphite_parse_string(context, value, &gmcf->prefix);
}

static char *
ngx_http_graphite_config_arg_host(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    return ngx_http_graphite_parse_string(context, value, &gmcf->host);
}

static char *
ngx_http_graphite_config_arg_protocol(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    return ngx_http_graphite_parse_string(context, value, &gmcf->protocol);
}

static char *
ngx_http_graphite_config_arg_timeout(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    gmcf->timeout = ngx_atoi(value->data, value->len) * 1000;

    return NGX_CONF_OK;
}

#ifdef NGX_LOG_LIMIT_ENABLED
static char *
ngx_http_graphite_config_arg_error_log(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    return ngx_http_graphite_parse_string(context, value, &gmcf->error_log);
}
#endif

static char *
ngx_http_graphite_config_arg_server(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    return ngx_http_graphite_parse_string(context, value, &gmcf->server.name);
}

static char *
ngx_http_graphite_config_arg_port(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    gmcf->port = ngx_atoi(value->data, value->len);

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_frequency(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    gmcf->frequency = ngx_atoi(value->data, value->len) * 1000;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_intervals(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = data;
    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;

    ngx_uint_t i;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; i++) {

        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite config intervals is empty");
                return NGX_CONF_ERROR;
            }

            ngx_http_graphite_interval_t *interval = ngx_array_push(gmcf->intervals);
            if (!interval) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            interval->name.data = ngx_http_graphite_allocator_alloc(allocator, i - s);
            if (!interval->name.data) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            ngx_memcpy(interval->name.data, &value->data[s], i - s);
            interval->name.len = i - s;

            if (ngx_http_graphite_parse_time(context, &interval->name, &interval->value) == NGX_CONF_ERROR) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite config interval is invalid");
                return NGX_CONF_ERROR;
            }

            if (interval->value > gmcf->storage->max_interval)
                gmcf->storage->max_interval = interval->value;

            s = i + 1;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_params(ngx_http_graphite_context_t *context, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_params(context, value, gmcf->default_params);
}

static char *
ngx_http_graphite_config_arg_shared(ngx_http_graphite_context_t *context, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(context, value, &gmcf->shared_size);
}

static char *
ngx_http_graphite_config_arg_buffer(ngx_http_graphite_context_t *context, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(context, value, &gmcf->buffer_size);
}

static char *
ngx_http_graphite_config_arg_package(ngx_http_graphite_context_t *context, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(context, value, &gmcf->package_size);
}

static char *
ngx_http_graphite_config_arg_template(ngx_http_graphite_context_t *context, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_template_compile(context, gmcf->template, ngx_http_graphite_template_args, TEMPLATE_ARG_COUNT, value);
}

static char *
ngx_http_graphite_param_arg_name(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;
    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    param->name.data = ngx_http_graphite_allocator_alloc(allocator, value->len);
    if (!param->name.data) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    ngx_memcpy(param->name.data, value->data, value->len);
    param->name.len = value->len;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_aggregate(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    ngx_uint_t found = 0;
    ngx_uint_t a;
    for (a = 0; a < AGGREGATE_COUNT; a++) {
        if ((ngx_http_graphite_aggregates[a].name.len == value->len) && !ngx_strncmp(ngx_http_graphite_aggregates[a].name.data, value->data, value->len)) {
            found = 1;
            param->aggregate = ngx_http_graphite_aggregates[a].get;
        }
    }

    if (!found) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param unknow aggregate %V", value);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_interval(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;
    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;
    ngx_http_graphite_interval_t *interval = &param->interval;

    interval->name.data = ngx_http_graphite_allocator_alloc(allocator, value->len);
    if (!interval->name.data) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(interval->name.data, value->data, value->len);
    interval->name.len = value->len;

    if (ngx_http_graphite_parse_time(context, value, &interval->value) == NGX_CONF_ERROR) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param interval is invalid");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_percentile(ngx_http_graphite_context_t *context, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    ngx_uint_t i;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; i++) {

        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param percentile is empty");
                return NGX_CONF_ERROR;
            }

            ngx_int_t p = ngx_atofp(&value->data[s], i - s, 3);
            if (p == NGX_ERROR || p <= 0 || p > 100000) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite param percentile is invalid");
                return NGX_CONF_ERROR;
            }

            ngx_uint_t *percentile = ngx_http_graphite_array_push(param->percentiles);
            if (!percentile) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }
            *percentile = p;


            s = i + 1;
        }
    }

    return NGX_CONF_OK;
}

static ngx_uint_t
ngx_http_graphite_add_split(ngx_conf_t *cf, const ngx_str_t *name) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_array_t *splits = gmcf->splits;

    ngx_uint_t s = SPLIT_EMPTY;

    ngx_uint_t i = 0;
    for (i = 0; i < splits->nelts; i++) {
        ngx_str_t *split = &((ngx_str_t*)splits->elts)[i];
        if ((split->len == name->len) && !ngx_strncmp(split->data, name->data, name->len)) {
            s = i;
            break;
        }
    }

    if (s == SPLIT_EMPTY) {

        s = splits->nelts;

        ngx_str_t *split = ngx_array_push(splits);
        if (!split) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return SPLIT_EMPTY;
        }

        split->data = ngx_pstrdup(cf->pool, (ngx_str_t*)name);
        if (!split->data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return SPLIT_EMPTY;
        }

        split->len = name->len;
    }

    return s;
}

static char *
ngx_http_graphite_add_default_data(ngx_conf_t *cf, ngx_array_t *datas, const ngx_str_t *location, const ngx_str_t *server_name, const ngx_array_t *template, const ngx_array_t *params, const ngx_http_complex_value_t *filter) {

    if (template->nelts == 0)
        return NGX_CONF_OK;

    const ngx_str_t *variables[] = DEFAULT_DATA_VARIABLES(location, server_name);

    ngx_str_t split;
    split.len = ngx_http_graphite_template_len(template, variables);
    if (split.len == 0)
        return NGX_CONF_ERROR;

    split.data = ngx_palloc(cf->pool, split.len);
    if (!split.data)
        return NGX_CONF_ERROR;

    ngx_http_graphite_template_execute(split.data, split.len, template, variables);

    return ngx_http_graphite_add_data(cf, datas, &split, params, filter);
}

static char *
ngx_http_graphite_add_data(ngx_conf_t *cf, ngx_array_t *datas, const ngx_str_t *split, const ngx_array_t *params, const ngx_http_complex_value_t *filter) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_config(cf);
    ngx_http_graphite_main_conf_t *gmcf = context.gmcf;

    ngx_uint_t s = ngx_http_graphite_add_split(cf, split);
    if (s == SPLIT_EMPTY)
        return NGX_CONF_ERROR;

    ngx_http_graphite_data_t *data = ngx_array_push(datas);
    if (!data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    if (ngx_http_graphite_init_data(&context, data) == NGX_ERROR)
        return NGX_CONF_ERROR;

    if (params->nelts == 0)
        params = gmcf->default_params;

    ngx_uint_t i;
    for (i = 0; i < params->nelts; i++) {
        ngx_uint_t p = ((ngx_uint_t*)params->elts)[i];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->storage->params->elts)[p];

        if (param->source != SOURCE_INTERNAL) {
            ngx_http_graphite_source_t *source = &((ngx_http_graphite_source_t*)gmcf->sources->elts)[param->source];

            if (source->type != 0) {
                if ((cf->cmd_type & NGX_HTTP_MAIN_CONF) && !(source->type & NGX_HTTP_MAIN_CONF))
                    continue;
                if ((cf->cmd_type & NGX_HTTP_SRV_CONF) && !(source->type & NGX_HTTP_SRV_CONF))
                    continue;
                if ((cf->cmd_type & NGX_HTTP_LOC_CONF) && !(source->type & NGX_HTTP_LOC_CONF))
                    continue;
            }
        }
        ngx_http_graphite_add_param_to_data(&context, s, p, data);
    }

    data->filter = (ngx_http_complex_value_t*)filter;

    return NGX_CONF_OK;
}

static ngx_uint_t
ngx_http_graphite_get_source(ngx_http_graphite_context_t *context, const ngx_str_t *name) {

    if (context->phase != PHASE_CONFIG)
        return NGX_ERROR;

    ngx_http_graphite_main_conf_t *gmcf = context->gmcf;

    ngx_uint_t c;
    for (c = 0; c < gmcf->sources->nelts; c++) {
        ngx_http_graphite_source_t *source = &((ngx_http_graphite_source_t*)gmcf->sources->elts)[c];
        if ((source->name.len == name->len) && !ngx_strncmp(source->name.data, name->data, name->len))
            return c;
    }

    for (c = 0; c < SOURCE_COUNT; c++) {
        const ngx_http_graphite_source_t *source = &ngx_http_graphite_sources[c];
        if (!source->re) {
            if ((source->name.len == name->len) && !ngx_strncmp(source->name.data, name->data, name->len)) {
                break;
            }
        }
        else {
            ngx_regex_compile_t rc;
            ngx_memzero(&rc, sizeof(ngx_regex_compile_t));
            rc.pool = context->pool;
            rc.pattern = source->name;
            if (ngx_regex_compile(&rc) != NGX_OK) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't compile regex");
                return NGX_ERROR;
            }

            if (ngx_regex_exec(rc.regex, name, NULL, 0) >= 0)
                break;
        }
    }

    if (c == SOURCE_COUNT) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite unknow param %*s", name->len, name->data);
        return NGX_ERROR;
    }

    ngx_http_graphite_source_t *new_source = ngx_array_push(gmcf->sources);
    if (!new_source) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_ERROR;
    }
    *new_source = ngx_http_graphite_sources[c];
    new_source->name = *name;

    return gmcf->sources->nelts - 1;
}

static ngx_http_graphite_param_t
ngx_http_graphite_create_param(ngx_http_graphite_context_t *context, ngx_uint_t c) {

    ngx_http_graphite_main_conf_t *gmcf = context->gmcf;

    ngx_http_graphite_source_t *source = &((ngx_http_graphite_source_t*)gmcf->sources->elts)[c];

    ngx_http_graphite_param_t param;
    ngx_memzero(&param, sizeof(ngx_http_graphite_param_t));

    param.name = source->name;
    param.source = c;
    param.aggregate = source->aggregate;

    return param;
}

static char *
ngx_http_graphite_parse_params(ngx_http_graphite_context_t *context, const ngx_str_t *value, ngx_array_t *params) {

    ngx_http_graphite_main_conf_t *gmcf = context->gmcf;

    ngx_uint_t i = 0;
    ngx_uint_t s = 0;
    ngx_uint_t q = NGX_CONF_UNSET_UINT;
    for (i = 0; i <= value->len; i++) {
        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite params is empty");
                return NGX_CONF_ERROR;
            }

            if (i - s == 1 && value->data[s] == '*') {
                if (params == gmcf->default_params) {
                    ngx_str_t default_params = ngx_string(DEFAULT_PARAMS);
                    if (ngx_http_graphite_parse_params(context, &default_params, params) == NGX_CONF_ERROR)
                        return NGX_CONF_ERROR;
                }
                else {
                    ngx_uint_t p;
                    for (p = 0; p < gmcf->default_params->nelts; p++) {
                        ngx_uint_t param = ((ngx_uint_t*)gmcf->default_params->elts)[p];
                        if (ngx_http_graphite_add_param_to_params(context, param, params) != NGX_CONF_OK)
                            return NGX_CONF_ERROR;
                    }
                }
            }
            else {
                if (q == NGX_CONF_UNSET_UINT)
                    q = i;

                ngx_str_t name;
                name.data = &value->data[s];
                name.len = q - s;
                ngx_uint_t c = ngx_http_graphite_get_source(context, &name);
                if ((ngx_int_t)c == NGX_ERROR)
                    return NGX_CONF_ERROR;

                ngx_http_graphite_param_t param = ngx_http_graphite_create_param(context, c);

                if (q != i) {
                    ngx_int_t percentile = ngx_atofp(&value->data[q + 1], i - q - 1, 3);
                    if (percentile == NGX_ERROR || percentile <= 0 || percentile > 100000) {
                        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite bad param %*s", i - s, &value->data[s]);
                        return NGX_CONF_ERROR;
                    }

                    param.percentile = percentile;
                }

                ngx_uint_t p = ngx_http_graphite_add_param_to_config(context, &param);
                if ((ngx_int_t)p == NGX_ERROR)
                    return NGX_CONF_ERROR;

                if (ngx_http_graphite_add_param_to_params(context, p, params) != NGX_CONF_OK)
                    return NGX_CONF_ERROR;
            }

            s = i + 1;
            q = NGX_CONF_UNSET_UINT;
        }
        if (value->data[i] == '/')
            q = i;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_string(ngx_http_graphite_context_t *context, ngx_str_t *value, ngx_str_t *result) {

    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;

    result->data = ngx_http_graphite_allocator_alloc(allocator, value->len);
    if (!result->data) {
        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(result->data, value->data, value->len);
    result->len = value->len;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_size(ngx_http_graphite_context_t *context, ngx_str_t *value, ngx_uint_t *result) {

    if (!result)
        return NGX_CONF_ERROR;

    size_t len = 0;
    while (len < value->len && value->data[len] >= '0' && value->data[len] <= '9')
        len++;

    *result = ngx_atoi(value->data, len);
    if (*result == (ngx_uint_t)-1)
        return NGX_CONF_ERROR;

    if (len + 1 == value->len) {
        if (value->data[len] == 'b')
            *result *= 1;
        else if (value->data[len] == 'k')
            *result *= 1024;
        else if (value->data[len] == 'm')
            *result *= 1024 * 1024;
        else
            return NGX_CONF_ERROR;

        len++;
    }

    if (len != value->len)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_time(ngx_http_graphite_context_t *context, ngx_str_t *value, ngx_uint_t *result) {

    if (!result)
        return NGX_CONF_ERROR;

    size_t len = 0;
    while (len < value->len && value->data[len] >= '0' && value->data[len] <= '9')
        len++;

    *result = ngx_atoi(value->data, len);
    if (*result == (ngx_uint_t)-1)
        return NGX_CONF_ERROR;

    if (len + 1 == value->len) {
        if (value->data[len] == 's')
            *result *= 1;
        else if (value->data[len] == 'm')
            *result *= 60;
        else
            return NGX_CONF_ERROR;

        len++;
    }

    if (len != value->len)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_template_compile(ngx_http_graphite_context_t *context, ngx_array_t *template, const ngx_http_graphite_template_arg_t *args, size_t nargs, const ngx_str_t *value) {

    ngx_http_graphite_allocator_t *allocator = context->storage->allocator;

    ngx_uint_t i = 0;
    ngx_uint_t s = 0;

    typedef enum {
        TEMPLATE_STATE_ERROR = -1,
        TEMPLATE_STATE_NONE,
        TEMPLATE_STATE_VAR,
        TEMPLATE_STATE_BRACKET_VAR,
        TEMPLATE_STATE_VAR_START,
        TEMPLATE_STATE_BRACKET_VAR_START,
        TEMPLATE_STATE_BRACKET_VAR_END,
        TEMPLATE_STATE_NOP,
    } ngx_http_graphite_template_parser_state_t;

    typedef enum {
        TEMPLATE_LEXEM_ERROR = -1,
        TEMPLATE_LEXEM_VAR,
        TEMPLATE_LEXEM_BRACKET_OPEN,
        TEMPLATE_LEXEM_BRACKET_CLOSE,
        TEMPLATE_LEXEM_ALNUM,
        TEMPLATE_LEXEM_OTHER,
        TEMPLATE_LEXEM_END,
    } ngx_http_graphite_template_parser_lexem_t;

    ngx_http_graphite_template_parser_state_t state = TEMPLATE_STATE_NONE;
    ngx_http_graphite_template_parser_lexem_t lexem;

    ngx_http_graphite_template_parser_state_t parser[TEMPLATE_STATE_BRACKET_VAR_END + 1][TEMPLATE_LEXEM_END + 1] = {
        { 1,  6,  6,  6,  6,  0},
        { 0,  2, -1,  3, -1, -1},
        {-1, -1, -1,  4, -1, -1},
        { 0,  0,  0,  6,  0,  0},
        {-1, -1,  5,  6, -1, -1},
        { 0,  0,  0,  0,  0,  0},
    };

    for (i = 0; i <= value->len; i++) {
        if (i == value->len)
            lexem = TEMPLATE_LEXEM_END;
        else if (value->data[i] == '$')
            lexem = TEMPLATE_LEXEM_VAR;
        else if (value->data[i] == '(')
            lexem = TEMPLATE_LEXEM_BRACKET_OPEN;
        else if (value->data[i] == ')')
            lexem = TEMPLATE_LEXEM_BRACKET_CLOSE;
        else if (isalnum(value->data[i]))
            lexem = TEMPLATE_LEXEM_ALNUM;
        else
            lexem = TEMPLATE_LEXEM_OTHER;

        ngx_int_t new_state = parser[state][lexem];

        if (new_state == TEMPLATE_LEXEM_ERROR)
            return NGX_CONF_ERROR;

        if (new_state != TEMPLATE_STATE_NOP) {

            if (i != s && (state == TEMPLATE_STATE_NONE || state == TEMPLATE_STATE_VAR_START || state == TEMPLATE_STATE_BRACKET_VAR_START)) {

                ngx_http_graphite_template_t *arg = ngx_array_push(template);
                if (!arg) {
                    ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                    return NGX_CONF_ERROR;
                }

                if (state == TEMPLATE_STATE_NONE) {
                    arg->data.data = ngx_http_graphite_allocator_alloc(allocator, i - s);
                    if (!arg->data.data) {
                        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite can't alloc memory");
                        return NGX_CONF_ERROR;
                    }
                    ngx_memcpy(arg->data.data, value->data + s, i - s);
                    arg->data.len = i - s;
                    arg->variable = 0;
                }
                else if (state == TEMPLATE_STATE_VAR_START || state == TEMPLATE_STATE_BRACKET_VAR_START) {
                    ngx_uint_t found = 0;
                    size_t a;
                    for (a = 0; a < nargs; a++) {
                        if ((args[a].name.len == i - s) && !ngx_strncmp(args[a].name.data, &value->data[s], i - s)) {
                            found = 1;
                            arg->variable = args[a].variable;
                            arg->data.data = NULL;
                            arg->data.len = 0;
                        }
                    }

                    if (!found) {
                        ngx_log_error(NGX_LOG_ERR, context->log, 0, "graphite unknow template arg %*s", i - s, &value->data[s]);
                        return NGX_CONF_ERROR;
                    }
                }
            }

            s = i;
            state = new_state;
        }
    }

    return NGX_CONF_OK;
}

static u_char *
ngx_http_graphite_template_execute(u_char* buffer, size_t buffer_size, const ngx_array_t *template, const ngx_str_t *variables[]) {

    u_char *b = buffer;

    ngx_uint_t i;
    for (i = 0; i < template->nelts; i++) {
        ngx_http_graphite_template_t *arg = &((ngx_http_graphite_template_t*)template->elts)[i];
        const ngx_str_t *data = NULL;

        if (arg->data.len)
            data = &arg->data;
        else
            data = variables[arg->variable];

        b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V", data);
    }

    return b;
}

static size_t
ngx_http_graphite_template_len(const ngx_array_t *template, const ngx_str_t *variables[]) {

    size_t len = 0;

    ngx_uint_t i;
    for (i = 0; i < template->nelts; i++) {
        ngx_http_graphite_template_t *arg = &((ngx_http_graphite_template_t*)template->elts)[i];

        if (arg->data.len)
            len += arg->data.len;
        else
            len += variables[arg->variable]->len;
    }

    return len;
}

static ngx_http_complex_value_t *
ngx_http_graphite_complex_compile(ngx_conf_t *cf, ngx_str_t *value) {

    ngx_http_compile_complex_value_t ccv;
    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = value;
    ccv.complex_value = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (ccv.complex_value == NULL)
        return NULL;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK)
        return NULL;

    return ccv.complex_value;
}

static void
ngx_http_graphite_statistic_init(ngx_http_graphite_statistic_data_t *data, ngx_uint_t percentile) {

    ngx_memzero(data, sizeof(ngx_http_graphite_statistic_data_t));

    double p = (double)percentile / 100000;

    data->dn[P2_METRIC_COUNT - 1] = 1;
    data->dn[P2_METRIC_COUNT / 2] = p;

    size_t i;
    for (i = 1; i < P2_METRIC_COUNT / 2; i++)
        data->dn[i] = (p / (P2_METRIC_COUNT / 2)) * i;
    for (i = P2_METRIC_COUNT / 2 + 1; i < P2_METRIC_COUNT - 1; i++)
        data->dn[i] = p + ((1 - p) / (P2_METRIC_COUNT - 1 - P2_METRIC_COUNT / 2)) * (i - P2_METRIC_COUNT / 2);

    for (i = 0; i < P2_METRIC_COUNT; i++) {
        data->np[i] = (P2_METRIC_COUNT - 1) * data->dn[i] + 1;
        data->n[i] = i + 1;
    }
}

static ngx_int_t
ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_graphite_main_conf_t *gmcf = shm_zone->data;

    if (data) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory data set");
        return NGX_ERROR;
    }

    size_t shared_required_size =
        2 *
        (sizeof(ngx_slab_pool_t) +
        sizeof(ngx_http_graphite_storage_t) +
        sizeof(ngx_array_t) * 4 +
        sizeof(ngx_http_graphite_metric_t) * (gmcf->storage->metrics->nelts) +
        sizeof(ngx_http_graphite_gauge_t) * (gmcf->storage->gauges->nelts) +
        sizeof(ngx_http_graphite_statistic_t) * (gmcf->storage->statistics->nelts) +
        sizeof(ngx_http_graphite_param_t) * (gmcf->storage->params->nelts) +
        sizeof(ngx_http_graphite_internal_t) * (gmcf->storage->internals->nelts) +
        sizeof(ngx_http_graphite_metric_data_t) * (gmcf->storage->max_interval + 1) * gmcf->storage->metrics->nelts +
        sizeof(ngx_http_graphite_gauge_data_t) * gmcf->storage->gauges->nelts +
        sizeof(ngx_http_graphite_statistic_data_t) * gmcf->storage->statistics->nelts);

    if (shared_required_size > shm_zone->shm.size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small shared memory (minimum size is %uzb)", shared_required_size);
        return NGX_ERROR;
    }

    // 128 is the approximate size of the one record
    size_t buffer_required_size = (gmcf->intervals->nelts * gmcf->storage->metrics->nelts + gmcf->storage->gauges->nelts + gmcf->storage->statistics->nelts) * 128;
    if (buffer_required_size > gmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small buffer size (minimum size is %uzb)", buffer_required_size);
        return NGX_ERROR;
    }

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t *)shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory exists");
        return NGX_ERROR;
    }

    ngx_http_graphite_storage_t *storage = ngx_slab_alloc(shpool, sizeof(ngx_http_graphite_storage_t));
    if (!storage) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    shpool->data = storage;

    ngx_memzero(storage, sizeof(ngx_http_graphite_storage_t));

    storage->max_interval = gmcf->storage->max_interval;

    time_t ts = ngx_time();
    storage->start_time = ts;
    storage->last_time = ts;
    storage->event_time = ts;

    ngx_http_graphite_allocator_t *allocator = ngx_slab_alloc(shpool, sizeof(ngx_http_graphite_allocator_t));
    if (allocator == NULL) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }
    ngx_http_graphite_allocator_init(allocator, shpool, ngx_http_graphite_allocator_slab_alloc, ngx_http_graphite_allocator_slab_free);

    storage->allocator = allocator;
    storage->metrics = ngx_http_graphite_array_copy(storage->allocator, gmcf->storage->metrics);
    storage->gauges = ngx_http_graphite_array_copy(storage->allocator, gmcf->storage->gauges);
    storage->statistics = ngx_http_graphite_array_copy(storage->allocator, gmcf->storage->statistics);
    storage->params = ngx_http_graphite_array_copy(storage->allocator, gmcf->storage->params);
    storage->internals = ngx_http_graphite_array_copy(storage->allocator, gmcf->storage->internals);

    if (storage->metrics == NULL || storage->gauges == NULL || storage->statistics == NULL || storage->params == NULL || storage->internals == NULL) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    u_char *metric_datas = ngx_slab_calloc(shpool, sizeof(ngx_http_graphite_metric_data_t) * (gmcf->storage->max_interval + 1) * gmcf->storage->metrics->nelts);
    if (metric_datas == NULL) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    u_char *gauge_datas = ngx_slab_calloc(shpool, sizeof(ngx_http_graphite_gauge_data_t) * gmcf->storage->gauges->nelts);
    if (gauge_datas == NULL) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    u_char *statistic_datas = ngx_slab_calloc(shpool, sizeof(ngx_http_graphite_statistic_data_t) * gmcf->storage->statistics->nelts);
    if (statistic_datas == NULL) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    ngx_uint_t m;
    for (m = 0; m < storage->metrics->nelts; m++) {
        ngx_http_graphite_metric_t *metric = &(((ngx_http_graphite_metric_t*)storage->metrics->elts)[m]);
        metric->data = (ngx_http_graphite_metric_data_t*)(metric_datas + sizeof(ngx_http_graphite_metric_data_t) * (gmcf->storage->max_interval + 1) * m);
    }

    ngx_uint_t g;
    for (g = 0; g < storage->gauges->nelts; g++) {
        ngx_http_graphite_gauge_t *gauge = &(((ngx_http_graphite_gauge_t*)storage->gauges->elts)[g]);
        gauge->data = (ngx_http_graphite_gauge_data_t*)(gauge_datas + sizeof(ngx_http_graphite_gauge_data_t) * g);
    }

    ngx_uint_t s;
    for (s = 0; s < storage->statistics->nelts; s++) {
        ngx_http_graphite_statistic_t *statistic = &(((ngx_http_graphite_statistic_t*)storage->statistics->elts)[s]);
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->storage->params)[statistic->param];
        statistic->data = (ngx_http_graphite_statistic_data_t*)(statistic_datas + sizeof(ngx_http_graphite_statistic_data_t) * s);
        ngx_http_graphite_statistic_init(statistic->data, param->percentile);
    }

    return NGX_OK;
}

ngx_http_graphite_reqctx_t *
ngx_http_graphite_get_reqctx(ngx_http_request_t *r) {

    ngx_http_graphite_reqctx_t *reqctx;

    reqctx = ngx_http_get_module_ctx(r->main, ngx_http_graphite_module);
    if (reqctx)
        return reqctx;

    reqctx = ngx_pcalloc(r->main->pool, sizeof(ngx_http_graphite_reqctx_t));
    if (reqctx == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite can't alloc memory");
        return NULL;
    }

    reqctx->internal_values = ngx_array_create(r->main->pool, 16, sizeof(ngx_http_graphite_internal_value_t));
    if (reqctx->internal_values == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite can't alloc memory");
        return NULL;
    }

    ngx_http_set_ctx(r->main, reqctx, ngx_http_graphite_module);

    return reqctx;
}

static double *
ngx_http_graphite_get_sources_values(ngx_http_graphite_main_conf_t *gmcf, ngx_http_request_t *r) {

    double *values = ngx_palloc(r->pool, sizeof(double) * gmcf->sources->nelts);
    if (!values)
        return NULL;

    ngx_uint_t c;
    for (c = 0; c < gmcf->sources->nelts; c++) {

        const ngx_http_graphite_source_t *source = &((ngx_http_graphite_source_t*)gmcf->sources->elts)[c];
        if (source->get)
            values[c] = source->get(source, r);
    }

    return values;
}

static void
ngx_http_graphite_add_metric(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, ngx_http_graphite_metric_t *metric, time_t ts, double value) {

    ngx_uint_t a = ((ts - storage->start_time) % (storage->max_interval + 1));
    ngx_http_graphite_metric_data_t *data = &metric->data[a];

    data->count++;
    data->value += value;
}

static void
ngx_http_graphite_add_gauge(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, ngx_http_graphite_gauge_t *gauge, time_t ts, double value) {

    ngx_http_graphite_gauge_data_t *data = gauge->data;

    data->value += value;
}

static void
ngx_http_graphite_add_statistic(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, ngx_http_graphite_statistic_t *statistic, time_t ts, double value, ngx_uint_t percentile) {

    ngx_http_graphite_statistic_data_t *data = statistic->data;

    if (data->count >= P2_METRIC_COUNT) {

        size_t k;
        for (k = 0; k < P2_METRIC_COUNT; k++) {
            if (value < data->q[k])
                break;
        }
        if (k == 0) {
            k = 1;
            data->q[0] = value;
        }
        else if (k == P2_METRIC_COUNT) {
            k = 4;
            data->q[P2_METRIC_COUNT - 1] = value;
        }

        size_t i;
        for (i = 0; i < P2_METRIC_COUNT; i++) {
            if (i >= k)
                data->n[i]++;
            data->np[i] += data->dn[i];
        }

        for (i = 1; i < P2_METRIC_COUNT - 1; i++) {
            double d = data->np[i] - data->n[i];
            int s = (d >= 0.0) ? 1 : -1;
            if((d >= 1.0 && data->n[i + 1] - data->n[i] > 1) || (d <= -1.0 && data->n[i - 1] - data->n[i] < -1)) {
                double a = data->q[i] + (double)s * ((data->n[i] - data->n[i - 1] + s) * (data->q[i + 1] - data->q[i]) / (data->n[i + 1] - data->n[i]) + (data->n[i + 1] - data->n[i] - s) * (data->q[i] - data->q[i - 1]) / (data->n[i] - data->n[i - 1])) / (data->n[i + 1] - data->n[i - 1]);
                if (a <= data->q[i - 1] || data->q[i + 1] <= a)
                    a = data->q[i] + (double)s * (data->q[i + s] - data->q[i]) / (data->n[i + s] - data->n[i]);
                data->q[i] = a;
                data->n[i] += s;
            }
        }
    }
    else {
        size_t i = 0;
        for (i = 0; i < data->count; i++) {
            if (value < data->q[i])
                break;
        }
        if (data->count != 0 && i < P2_METRIC_COUNT - 1)
            memmove(&data->q[i + 1], &data->q[i], (data->count - i) * sizeof(*data->q));
        data->q[i] = value;
        data->count++;
    }
}

static void
ngx_http_graphite_add_data_values(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_http_graphite_data_t *data, const double *values) {

    if (data->filter) {
        ngx_str_t result;
        if (ngx_http_complex_value(r, data->filter, &result) != NGX_OK)
            return;

        if (result.len == 0 || (result.len == 1 && result.data[0] == '0'))
            return;
    }

    ngx_uint_t i;

    for (i = 0; i < data->metrics->nelts; i++) {
        ngx_uint_t m = ((ngx_uint_t*)data->metrics->elts)[i];
        ngx_http_graphite_metric_t *metric = &((ngx_http_graphite_metric_t*)storage->metrics->elts)[m];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[metric->param];
        double value = (param->source != SOURCE_INTERNAL) ? values[param->source] : values[0];
        ngx_http_graphite_add_metric(r, storage, metric, ts, value);
    }

    for (i = 0; i < data->gauges->nelts; i++) {
        ngx_uint_t g = ((ngx_uint_t*)data->gauges->elts)[i];
        ngx_http_graphite_gauge_t *gauge = &((ngx_http_graphite_gauge_t*)storage->gauges->elts)[g];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[gauge->param];
        double value = (param->source != SOURCE_INTERNAL) ? values[param->source] : values[0];
        ngx_http_graphite_add_gauge(r, storage, gauge, ts, value);
    }

    for (i = 0; i < data->statistics->nelts; i++) {
        ngx_uint_t s = ((ngx_uint_t*)data->statistics->elts)[i];
        ngx_http_graphite_statistic_t *statistic = &((ngx_http_graphite_statistic_t*)storage->statistics->elts)[s];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[statistic->param];
        double value = (param->source != SOURCE_INTERNAL) ? values[param->source] : values[0];
        ngx_http_graphite_add_statistic(r, storage, statistic, ts, value, param->percentile);
    }
}

static void
ngx_http_graphite_add_datas_values(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_array_t *datas, const double *values) {

    ngx_uint_t i;
    for (i = 0; i < datas->nelts; i++) {
        ngx_http_graphite_data_t *data = &((ngx_http_graphite_data_t*)datas->elts)[i];
        ngx_http_graphite_add_data_values(r, storage, ts, data, values);
    }
}

static ngx_int_t
ngx_http_graphite_handler(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);
    ngx_http_graphite_srv_conf_t *gscf = ngx_http_get_module_srv_conf(r, ngx_http_graphite_module);
    ngx_http_graphite_loc_conf_t *glcf = ngx_http_get_module_loc_conf(r, ngx_http_graphite_module);

    if (!gmcf->enable)
        return NGX_OK;

    ngx_http_graphite_reqctx_t *reqctx = ngx_http_get_module_ctx(r->main, ngx_http_graphite_module);

    if (gmcf->datas->nelts == 0 && gscf->datas->nelts == 0 && glcf->datas->nelts == 0 && (reqctx == NULL || reqctx->internal_values->nelts == 0) && gmcf->internal_values->nelts == 0)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    time_t ts = ngx_time();

    double *values = ngx_http_graphite_get_sources_values(gmcf, r);
    if (values == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite can't get values");
        return NGX_OK;
    }

    ngx_shmtx_lock(&shpool->mutex);

    ngx_http_graphite_del_old_records(gmcf, ts);

    if (r == r->main) {
        ngx_http_graphite_add_datas_values(r, storage, ts, gmcf->datas, values);
        ngx_http_graphite_add_datas_values(r, storage, ts, gscf->datas, values);
    }
    ngx_http_graphite_add_datas_values(r, storage, ts, glcf->datas, values);

    if (reqctx != NULL) {
        size_t i;
        for (i = 0; i < reqctx->internal_values->nelts; i++) {
            const ngx_http_graphite_internal_value_t *internal_value = &(((ngx_http_graphite_internal_value_t*)reqctx->internal_values->elts)[i]);
            ngx_http_graphite_internal_t *internal = &((ngx_http_graphite_internal_t*)storage->internals->elts)[internal_value->internal];
            ngx_http_graphite_add_data_values(r, storage, internal_value->ts, &internal->data, &internal_value->value);
        }
    }

   size_t i;
   for (i = 0; i < gmcf->internal_values->nelts; i++) {
        const ngx_http_graphite_internal_value_t *internal_value = &(((ngx_http_graphite_internal_value_t*)gmcf->internal_values->elts)[i]);
        ngx_http_graphite_internal_t *internal = &((ngx_http_graphite_internal_t*)storage->internals->elts)[internal_value->internal];
        ngx_http_graphite_add_data_values(r, storage, internal_value->ts, &internal->data, &internal_value->value);
    }
    gmcf->internal_values->nelts = 0;

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

ngx_int_t
ngx_http_graphite(ngx_http_request_t *r, const ngx_str_t *name, double value) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_request(r);
    ngx_http_graphite_main_conf_t *gmcf = context.gmcf;

    if (!gmcf->enable)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_int_t found = 0;
    ngx_int_t internal = ngx_http_graphite_search_param(storage->internals, name, &found);

    if (!found)
        return NGX_OK;

    ngx_http_graphite_internal_value_t *internal_value = NULL;
    if (r->connection->fd != (ngx_socket_t)-1) {
        ngx_http_graphite_reqctx_t *reqctx = ngx_http_graphite_get_reqctx(r);
        if (!reqctx)
            return NGX_ERROR;

        internal_value = ngx_array_push(reqctx->internal_values);
    }
    else {
        internal_value = ngx_array_push(gmcf->internal_values);
    }

    if (internal_value == NULL)
        return NGX_ERROR;

    internal_value->ts = ngx_time();
    internal_value->internal = internal;
    internal_value->value = value;

    return NGX_OK;
}

double
ngx_http_graphite_get(ngx_http_request_t *r, const ngx_str_t *name) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_request(r);
    ngx_http_graphite_main_conf_t *gmcf = context.gmcf;

    if (!gmcf->enable)
        return 0;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_int_t found = 0;
    ngx_int_t i = ngx_http_graphite_search_param(storage->internals, name, &found);

    if (!found)
        return 0;

    ngx_shmtx_lock(&shpool->mutex);

    ngx_http_graphite_internal_t *internal = &((ngx_http_graphite_internal_t*)storage->internals->elts)[i];
    ngx_http_graphite_data_t *data = &internal->data;
    if (data->gauges->nelts) {
        ngx_uint_t g = ((ngx_uint_t*)data->gauges->elts)[0];
        ngx_http_graphite_gauge_t *gauge = &((ngx_http_graphite_gauge_t*)storage->gauges->elts)[g];
        ngx_http_graphite_gauge_data_t *gauge_data = gauge->data;
        ngx_shmtx_unlock(&shpool->mutex);
        return gauge_data->value;
    }

    ngx_shmtx_unlock(&shpool->mutex);
    return 0;
}

ngx_int_t
ngx_http_graphite_set(ngx_http_request_t *r, const ngx_str_t *name, double value) {

    ngx_http_graphite_context_t context = ngx_http_graphite_context_from_request(r);
    ngx_http_graphite_main_conf_t *gmcf = context.gmcf;

    if (!gmcf->enable)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_int_t found = 0;
    ngx_int_t i = ngx_http_graphite_search_param(storage->internals, name, &found);

    if (!found)
        return NGX_ERROR;

    ngx_shmtx_lock(&shpool->mutex);

    ngx_http_graphite_internal_t *internal = &((ngx_http_graphite_internal_t*)storage->internals->elts)[i];
    ngx_http_graphite_data_t *data = &internal->data;
    if (data->gauges->nelts) {
        ngx_uint_t g = ((ngx_uint_t*)data->gauges->elts)[0];
        ngx_http_graphite_gauge_t *gauge = &((ngx_http_graphite_gauge_t*)storage->gauges->elts)[g];
        ngx_http_graphite_gauge_data_t *gauge_data = gauge->data;
        gauge_data->value = value;
    }
    else {
        ngx_shmtx_unlock(&shpool->mutex);
        return NGX_ERROR;
    }

    ngx_shmtx_unlock(&shpool->mutex);
    return NGX_OK;
}

static u_char*
ngx_http_graphite_print_metric(ngx_http_graphite_main_conf_t *gmcf, ngx_http_graphite_storage_t *storage, ngx_uint_t m, const ngx_http_graphite_interval_t *interval, time_t ts, u_char *buffer, size_t buffer_size) {

    const ngx_http_graphite_metric_t *metric = &(((ngx_http_graphite_metric_t*)storage->metrics->elts)[m]);
    const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[metric->param];

    if (metric->data == NULL)
        return buffer;

    ngx_http_graphite_metric_data_t aggregate;
    aggregate.value = 0;
    aggregate.count = 0;

    unsigned l;
    for (l = 0; l < interval->value; l++) {
        if ((time_t)(ts - l - 1) >= storage->start_time) {
            ngx_uint_t a = ((ts - l - 1 - storage->start_time) % (storage->max_interval + 1));
            ngx_http_graphite_metric_data_t *data = &metric->data[a];
            aggregate.value += data->value;
            aggregate.count += data->count;
        }
    }

    double value = param->aggregate(interval, &aggregate);

    u_char *b = buffer;

    if (metric->split != SPLIT_INTERNAL) {
        const ngx_str_t *split = &((ngx_str_t*)gmcf->splits->elts)[metric->split];
        if (!gmcf->template->nelts) {
            if (gmcf->prefix.len)
                b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
            b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V.%V_%V", &gmcf->host, split, &param->name, &interval->name);
        }
        else {
            const ngx_str_t *variables[] = TEMPLATE_VARIABLES(&gmcf->prefix, &gmcf->host, split, &param->name, &interval->name);
            b = ngx_http_graphite_template_execute(b, buffer_size - (b - buffer), gmcf->template, variables);
        }
    }
    else {
        if (gmcf->prefix.len)
            b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
        b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V", &gmcf->host, &param->name);
    }

    b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), " %.3f %T\n", value, ts);

    return b;
}

static u_char*
ngx_http_graphite_print_gauge(ngx_http_graphite_main_conf_t *gmcf, ngx_http_graphite_storage_t *storage, ngx_uint_t g, time_t ts, u_char *buffer, size_t buffer_size) {

    const ngx_http_graphite_gauge_t *gauge = &(((ngx_http_graphite_gauge_t*)storage->gauges->elts)[g]);
    const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[gauge->param];

    if (gauge->data == NULL)
        return buffer;

    double value = param->aggregate(NULL, gauge->data);

    u_char *b = buffer;

    if (gmcf->prefix.len)
        b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
    b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V", &gmcf->host, &param->name);

    b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), " %.3f %T\n", value, ts);

    return b;
}

static u_char*
ngx_http_graphite_print_statistic(ngx_http_graphite_main_conf_t *gmcf, ngx_http_graphite_storage_t *storage, ngx_uint_t s, time_t ts, u_char *buffer, size_t buffer_size) {

    const ngx_http_graphite_statistic_t *statistic = &(((ngx_http_graphite_statistic_t*)storage->statistics->elts)[s]);
    const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[statistic->param];

    if (statistic->data == NULL)
        return buffer;

    u_char p[8];
    ngx_str_t percentile;
    percentile.data = p;
    percentile.len = ngx_snprintf(p, sizeof(p), "p%ui_%d", param->percentile / 1000, param->percentile % 1000) - p;

    while (percentile.len && p[percentile.len - 1] == '0')
        percentile.len--;
    if (percentile.len && p[percentile.len - 1] == '_')
        percentile.len--;

    ngx_http_graphite_statistic_data_t *data = statistic->data;

    u_char *b = buffer;

    if (statistic->split != SPLIT_INTERNAL) {
        const ngx_str_t *split = &((ngx_str_t*)gmcf->splits->elts)[statistic->split];
        if (!gmcf->template->nelts) {
            if (gmcf->prefix.len)
                b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
            b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V.%V_%V", &gmcf->host, split, &param->name, &percentile);
        }
        else {
            const ngx_str_t *variables[] = TEMPLATE_VARIABLES(&gmcf->prefix, &gmcf->host, split, &param->name, &percentile);
            b = ngx_http_graphite_template_execute(b, buffer_size - (b - buffer), gmcf->template, variables);
        }
    }
    else {
        if (gmcf->prefix.len)
            b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
        b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V_%V", &gmcf->host, &param->name, &percentile);
    }

    b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), " %.3f %T\n", data->q[P2_METRIC_COUNT / 2], ts);

    return b;
}

#ifdef NGX_LOG_LIMIT_ENABLED
static u_char*
ngx_http_graphite_print_log(
    ngx_http_graphite_main_conf_t *gmcf, ngx_http_graphite_log_t *gl,
    time_t ts, u_char *buffer, size_t buffer_size)
{
    size_t total = 0;
    size_t skipped = 0;
    u_char *b = buffer;

    for (ngx_uint_t i = 0; i != gl->error_logs->nelts; i++) {
        ngx_log_t *log = ((ngx_log_t**)gl->error_logs->elts)[i];
        ngx_log_grow_t *grow = &log->shared->grow;

        skipped += grow->total_bytes_skipped;
        total += grow->total_bytes;
    }
    if (gmcf->prefix.len)
        b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
    b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V.%V.total %uz %T\n", &gmcf->host, &gmcf->error_log, &gl->name, total, ts);

    if (gmcf->prefix.len)
        b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
    b = ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V.%V.skipped %uz %T\n", &gmcf->host, &gmcf->error_log, &gl->name, skipped, ts);

    return b;
}
#endif

static void
ngx_http_graphite_timer_handler(ngx_event_t *ev) {

    time_t ts = ngx_time();

    ngx_http_graphite_main_conf_t *gmcf;
    gmcf = ev->data;

    ngx_buf_t *buffer = &gmcf->buffer;
    u_char *b = buffer->start;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_shmtx_lock(&shpool->mutex);

    if ((ts - storage->event_time) * 1000 < (int)gmcf->frequency) {
        ngx_shmtx_unlock(&shpool->mutex);
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, gmcf->frequency);
        return;
    }

    if (gmcf->connection) {
        ngx_log_error(NGX_LOG_ERR, ev->log, 0, "graphite can't send buffer completely");
        ngx_close_connection(gmcf->connection);
        gmcf->connection = NULL;
    }

    if (storage->allocator->nomemory)
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite shared memory is full");

    storage->event_time = ts;

    ngx_http_graphite_del_old_records(gmcf, ts);

    ngx_uint_t m;
    for (m = 0; m < storage->metrics->nelts; m++) {
        const ngx_http_graphite_metric_t *metric = &(((ngx_http_graphite_metric_t*)storage->metrics->elts)[m]);
        const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[metric->param];

        if (metric->split != SPLIT_INTERNAL) {
            ngx_uint_t i;
            for (i = 0; i < gmcf->intervals->nelts; i++) {
                const ngx_http_graphite_interval_t *interval = &((ngx_http_graphite_interval_t*)gmcf->intervals->elts)[i];
                b = ngx_http_graphite_print_metric(gmcf, storage, m, interval, ts, b, gmcf->buffer_size - (b - buffer->start));
            }
        }
        else
            b = ngx_http_graphite_print_metric(gmcf, storage, m, &param->interval, ts, b, gmcf->buffer_size - (b - buffer->start));
    }

    ngx_uint_t g;
    for (g = 0; g < storage->gauges->nelts; g++)
        b = ngx_http_graphite_print_gauge(gmcf, storage, g, ts, b, gmcf->buffer_size - (b - buffer->start));

    ngx_uint_t s;
    for (s = 0; s < storage->statistics->nelts; s++) {
        const ngx_http_graphite_statistic_t *statistic = &(((ngx_http_graphite_statistic_t*)storage->statistics->elts)[s]);
        const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)storage->params->elts)[statistic->param];

        b = ngx_http_graphite_print_statistic(gmcf, storage, s, ts, b, gmcf->buffer_size - (b - buffer->start));

        ngx_http_graphite_statistic_data_t *data = statistic->data;
        ngx_http_graphite_statistic_init(data, param->percentile);
    }

    ngx_shmtx_unlock(&shpool->mutex);

#ifdef NGX_LOG_LIMIT_ENABLED
    for (ngx_uint_t i = 0; i != gmcf->logs->nelts; i++) {
        ngx_http_graphite_log_t *gl = &((ngx_http_graphite_log_t*)gmcf->logs->elts)[i];
        b = ngx_http_graphite_print_log(gmcf, gl, ts, b, gmcf->buffer_size - (b - buffer->start));
    }
#endif

    *b = '\0';

    if (b == buffer->start + gmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite buffer size is too small");
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, gmcf->frequency);
        return;
    }

    if (b != buffer->start) {
        buffer->pos = buffer->start;
        buffer->last = b;
        ngx_http_graphite_net_send_buffer(gmcf, ev->log);
    }

    if (!(ngx_quit || ngx_terminate || ngx_exiting))
        ngx_add_timer(ev, gmcf->frequency);
}

static void
ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *gmcf, time_t ts) {

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    while ((ngx_uint_t)storage->last_time + storage->max_interval < (ngx_uint_t)ts) {

        ngx_uint_t m;
        for (m = 0; m < storage->metrics->nelts; m++) {
            ngx_http_graphite_metric_t *metric = &(((ngx_http_graphite_metric_t*)storage->metrics->elts)[m]);

            ngx_uint_t a = ((storage->last_time - storage->start_time) % (storage->max_interval + 1));
            ngx_http_graphite_metric_data_t *data = &metric->data[a];

            data->value = 0;
            data->count = 0;
        }

        storage->last_time++;
    }
}

static double
ngx_http_graphite_source_request_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    ngx_msec_int_t   ms;

    struct timeval tp;
    ngx_gettimeofday(&tp);
    ms = (ngx_msec_int_t) ((tp.tv_sec - r->start_sec) * 1000 + (tp.tv_usec / 1000 - r->start_msec));

    return (double)ms;
}

static double
ngx_http_graphite_source_bytes_sent(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    return (double)r->connection->sent;
}

static double
ngx_http_graphite_source_body_bytes_sent(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    off_t  length;

    length = r->connection->sent - r->header_size;
    length = ngx_max(length, 0);

    return (double)length;
}

static double
ngx_http_graphite_source_request_length(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    return (double)r->request_length;
}

static double
ngx_http_graphite_source_ssl_cache_usage(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    double usage;

    usage = 0;

#if NGX_SSL
    ngx_ssl_connection_t *ssl = r->connection->ssl;
    if (ssl) {

        SSL_CTX *ssl_ctx = SSL_get_SSL_CTX(ssl->connection);
        ngx_shm_zone_t *shm_zone = SSL_CTX_get_ex_data(ssl_ctx, ngx_ssl_session_cache_index);
        if (shm_zone) {
            ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)shm_zone->shm.addr;

            ngx_shmtx_lock(&shpool->mutex);

            ngx_uint_t all_pages = (shpool->end - shpool->start) / ngx_pagesize;
            ngx_uint_t free_pages = 0;

            ngx_slab_page_t *page;
            for (page = shpool->free.next; page != &shpool->free; page = page->next)
                free_pages += page->slab;

            ngx_shmtx_unlock(&shpool->mutex);

            if (all_pages > 0)
                usage = (100 * (all_pages - free_pages)) / all_pages;
        }
    }
#endif

    return usage;
}

static double
ngx_http_graphite_source_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    return 1;
}

static double
ngx_http_graphite_source_keepalive_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    if (r->connection->requests == 1)
        return 0;
    else
        return 1;
}

static double
ngx_http_graphite_source_response_2xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 2)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_3xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 3)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_4xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 4)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_5xx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 5)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_xxx_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    ngx_uint_t status = ngx_atoi(source->name.data + sizeof("response_") - 1, 3);
    if (r->headers_out.status == status)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_upstream_cache_status_rps(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {
#if NGX_HTTP_CACHE

    if (r->upstream == NULL || r->upstream->cache_status == 0)
        return 0;

    ngx_str_t param_status;
    param_status.data = source->name.data + sizeof("upstream_cache_") - 1;
    param_status.len = source->name.len - (sizeof("upstream_cache_") - 1) - 4;

    ngx_str_t cache_status = ngx_http_cache_status[r->upstream->cache_status - 1];
    if (cache_status.len == param_status.len && ngx_strncasecmp(cache_status.data, param_status.data, param_status.len) == 0)
        return 1;
    else
        return 0;
#else
    return 0;
#endif
}

#ifdef NGX_GRAPHITE_PATCH

static double
ngx_http_graphite_source_ssl_handshake_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

#if NGX_SSL
    ngx_msec_int_t ms;
    ms = 0;

    if (r->connection->requests == 1) {
        ngx_ssl_connection_t *ssl = r->connection->ssl;
        if (ssl)
            ms = (ngx_msec_int_t)((ssl->handshake_end_sec - ssl->handshake_start_sec) * 1000 + (ssl->handshake_end_msec - ssl->handshake_start_msec));
    }

    return (double)ms;
#else
    return 0;
#endif
}

static double
ngx_http_graphite_source_content_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    return (double)r->content_time;
}

static double
ngx_http_graphite_source_gzip_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    return (double)r->gzip_time;
}

static double
ngx_http_graphite_source_upstream_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; i++) {
        if (state[i].status)
#if nginx_version >= 1009001
            ms += (ngx_msec_int_t)(state[i].response_time);
#else
            ms += (ngx_msec_int_t)(state[i].response_sec * 1000 + state[i].response_msec);
#endif
    }
    ms = ngx_max(ms, 0);

    return (double)ms;
}

static double
ngx_http_graphite_source_upstream_connect_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {
#if nginx_version >= 1009001
    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; i++) {
        if (state[i].status)
            ms += (ngx_msec_int_t)(state[i].connect_time);
    }
    ms = ngx_max(ms, 0);

    return (double)ms;
#else
    return 0;
#endif
}

static double
ngx_http_graphite_source_upstream_header_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {
#if nginx_version >= 1009001

    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; i++) {
        if (state[i].status)
            ms += (ngx_msec_int_t)(state[i].header_time);
    }
    ms = ngx_max(ms, 0);

    return (double)ms;
#else
    return 0
#endif
}

static double
ngx_http_graphite_source_lua_time(const ngx_http_graphite_source_t *source, ngx_http_request_t *r) {

    return r->lua_time;
}

#endif

static double
ngx_http_graphite_aggregate_avg(const ngx_http_graphite_interval_t *interval, const void *data) {

    ngx_http_graphite_metric_data_t *metric_data = (ngx_http_graphite_metric_data_t*)data;
    return (metric_data->count != 0) ? metric_data->value / metric_data->count : 0;
}

static double
ngx_http_graphite_aggregate_persec(const ngx_http_graphite_interval_t *interval, const void *data) {

    ngx_http_graphite_metric_data_t *metric_data = (ngx_http_graphite_metric_data_t*)data;
    return metric_data->value / interval->value;
}

static double
ngx_http_graphite_aggregate_sum(const ngx_http_graphite_interval_t *interval, const void *data) {

    ngx_http_graphite_metric_data_t *metric_data = (ngx_http_graphite_metric_data_t*)data;
    return metric_data->value;
}

static double
ngx_http_graphite_aggregate_gauge(const ngx_http_graphite_interval_t *interval, const void *data) {

    ngx_http_graphite_gauge_data_t *gauge_data = (ngx_http_graphite_gauge_data_t*)data;
    return gauge_data->value;
}
