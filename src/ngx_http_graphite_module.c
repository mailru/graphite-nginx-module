#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {

    ngx_uint_t enable;

    ngx_str_t host;
    ngx_str_t protocol;
    ngx_shm_zone_t *shared;
    char *buffer;

    ngx_str_t prefix;

    struct in_addr server;
    int port;
    ngx_uint_t frequency;

    ngx_array_t *params;
    ngx_array_t *intervals;
    ngx_array_t *splits;
    ngx_array_t *default_params;

    ngx_uint_t max_interval;

    ngx_array_t *metrics;
    ngx_array_t *statistics;

    ngx_array_t *custom_params;
    ngx_hash_t custom_params_hash;
    ngx_uint_t custom_params_hash_max_size;
    ngx_uint_t custom_params_hash_bucket_size;

    size_t shared_size;
    size_t buffer_size;
    size_t package_size;

    ngx_array_t *template;

    ngx_array_t *default_data_template;
    ngx_array_t *default_data_params;
    ngx_http_complex_value_t *default_data_filter;

    ngx_array_t *datas;
    ngx_array_t *custom_datas;
} ngx_http_graphite_main_conf_t;

typedef struct {
    ngx_array_t *default_data_template;
    ngx_array_t *default_data_params;
    ngx_http_complex_value_t *default_data_filter;

    ngx_array_t *datas;
} ngx_http_graphite_srv_conf_t;

typedef struct {
    ngx_array_t *datas;
} ngx_http_graphite_loc_conf_t;

#define SPLIT_EMPTY (ngx_uint_t)-1
#define SPLIT_CUSTOM (ngx_uint_t)-2

static ngx_int_t ngx_http_graphite_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_process_init(ngx_cycle_t *cycle);

static void *ngx_http_graphite_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_srv_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_loc_conf(ngx_conf_t *cf);

static ngx_str_t *ngx_http_graphite_location(ngx_pool_t *pool, ngx_str_t *uri);

#if (NGX_SSL)
static ngx_int_t ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);
#endif

static char *ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_default_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_graphite_custom(ngx_http_request_t *r, ngx_str_t *name, double value);

static char *ngx_http_graphite_config_arg_prefix(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_host(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_port(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_frequency(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_intervals(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_params(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_shared(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_buffer(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_package(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_template(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_protocol(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);

static char *ngx_http_graphite_param_arg_name(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_aggregate(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_interval(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_percentile(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);

static char *ngx_http_graphite_add_default_data(ngx_conf_t *cf, ngx_array_t *datas, ngx_str_t *split, ngx_array_t *template, ngx_array_t *params, ngx_http_complex_value_t *filter);
static char *ngx_http_graphite_add_data(ngx_conf_t *cf, ngx_array_t *datas, ngx_str_t *split, ngx_array_t *params, ngx_http_complex_value_t *filter);

static char *ngx_http_graphite_parse_params(ngx_conf_t *cf, ngx_str_t *value, ngx_array_t *params);
static char *ngx_http_graphite_parse_string(ngx_conf_t *cf, ngx_str_t *value, ngx_str_t *result);
static char *ngx_http_graphite_parse_size(ngx_str_t *value, size_t *result);
static char *ngx_http_graphite_parse_time(ngx_str_t *value, ngx_uint_t *result);

static double ngx_http_graphite_source_request_time(ngx_http_request_t *r);
static double ngx_http_graphite_source_bytes_sent(ngx_http_request_t *r);
static double ngx_http_graphite_source_body_bytes_sent(ngx_http_request_t *r);
static double ngx_http_graphite_source_request_length(ngx_http_request_t *r);
static double ngx_http_graphite_source_ssl_handshake_time(ngx_http_request_t *r);
static double ngx_http_graphite_source_ssl_cache_usage(ngx_http_request_t *r);
static double ngx_http_graphite_source_content_time(ngx_http_request_t *r);
static double ngx_http_graphite_source_gzip_time(ngx_http_request_t *r);
static double ngx_http_graphite_source_upstream_time(ngx_http_request_t *r);
#if nginx_version >= 1009001
static double ngx_http_graphite_source_upstream_connect_time(ngx_http_request_t *r);
static double ngx_http_graphite_source_upstream_header_time(ngx_http_request_t *r);
#endif
static double ngx_http_graphite_source_rps(ngx_http_request_t *r);
static double ngx_http_graphite_source_keepalive_rps(ngx_http_request_t *r);
static double ngx_http_graphite_source_response_2xx_rps(ngx_http_request_t *r);
static double ngx_http_graphite_source_response_3xx_rps(ngx_http_request_t *r);
static double ngx_http_graphite_source_response_4xx_rps(ngx_http_request_t *r);
static double ngx_http_graphite_source_response_5xx_rps(ngx_http_request_t *r);

static ngx_command_t ngx_http_graphite_commands[] = {

    { ngx_string("graphite_config"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_ANY,
      ngx_http_graphite_config,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("graphite_param_hash_max_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_graphite_main_conf_t, custom_params_hash_max_size),
      NULL },

    { ngx_string("graphite_param_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_graphite_main_conf_t, custom_params_hash_bucket_size),
      NULL },

    { ngx_string("graphite_param"),
      NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE23,
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

   { ngx_string("graphite_custom"),
      0,
      NULL,
      0,
      0,
      ngx_http_graphite_custom },

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
    NULL                                   /* merge location configuration */
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

typedef char *(*ngx_http_graphite_arg_handler_pt)(ngx_conf_t*, ngx_command_t*, void *, ngx_str_t *);

typedef struct ngx_http_graphite_arg_s {
    ngx_str_t name;
    ngx_http_graphite_arg_handler_pt handler;
    ngx_str_t deflt;
} ngx_http_graphite_arg_t;

#define CONFIG_ARGS_COUNT 12

static const ngx_http_graphite_arg_t ngx_http_graphite_config_args[CONFIG_ARGS_COUNT] = {
    { ngx_string("prefix"), ngx_http_graphite_config_arg_prefix, ngx_null_string },
    { ngx_string("host"), ngx_http_graphite_config_arg_host, ngx_null_string },
    { ngx_string("server"), ngx_http_graphite_config_arg_server, ngx_null_string },
    { ngx_string("port"), ngx_http_graphite_config_arg_port, ngx_string("2003") },
    { ngx_string("frequency"), ngx_http_graphite_config_arg_frequency, ngx_string("60") },
    { ngx_string("intervals"), ngx_http_graphite_config_arg_intervals, ngx_string("1m") },
#if nginx_version >= 1009001
    { ngx_string("params"), ngx_http_graphite_config_arg_params, ngx_string("request_time|bytes_sent|body_bytes_sent|request_length|ssl_handshake_time|ssl_cache_usage|content_time|gzip_time|upstream_time|upstream_connect_time|upstream_header_time|rps|keepalive_rps|response_2xx_rps|response_3xx_rps|response_4xx_rps|response_5xx_rps") },
#else
    { ngx_string("params"), ngx_http_graphite_config_arg_params, ngx_string("request_time|bytes_sent|body_bytes_sent|request_length|ssl_handshake_time|ssl_cache_usage|content_time|gzip_time|upstream_time|rps|keepalive_rps|response_2xx_rps|response_3xx_rps|response_4xx_rps|response_5xx_rps") },
#endif
    { ngx_string("shared"), ngx_http_graphite_config_arg_shared, ngx_string("1m") },
    { ngx_string("buffer"), ngx_http_graphite_config_arg_buffer, ngx_string("64k") },
    { ngx_string("package"), ngx_http_graphite_config_arg_package, ngx_string("1400") },
    { ngx_string("template"), ngx_http_graphite_config_arg_template, ngx_null_string },
    { ngx_string("protocol"), ngx_http_graphite_config_arg_protocol, ngx_string("udp") },
};

#define PARAM_ARGS_COUNT 4

static const ngx_http_graphite_arg_t ngx_http_graphite_param_args[PARAM_ARGS_COUNT] = {
    { ngx_string("name"), ngx_http_graphite_param_arg_name, ngx_null_string },
    { ngx_string("aggregate"), ngx_http_graphite_param_arg_aggregate, ngx_null_string },
    { ngx_string("interval"), ngx_http_graphite_param_arg_interval, ngx_null_string },
    { ngx_string("percentile"), ngx_http_graphite_param_arg_percentile, ngx_null_string },
};

static ngx_event_t timer;

typedef struct ngx_http_graphite_acc_s {
    double value;
    ngx_uint_t count;
} ngx_http_graphite_acc_t;

#define P2_METRIC_COUNT 5

typedef struct ngx_http_graphite_stt_s {
    double q[P2_METRIC_COUNT];
    double dn[P2_METRIC_COUNT];
    double np[P2_METRIC_COUNT];
    ngx_int_t n[P2_METRIC_COUNT];
    ngx_uint_t count;
} ngx_http_graphite_stt_t;

typedef struct ngx_http_graphite_storage_s {
    ngx_http_graphite_acc_t *accs;
    ngx_http_graphite_acc_t *custom_accs;
    ngx_http_graphite_stt_t *stts;

    time_t start_time;
    time_t last_time;
    time_t event_time;

} ngx_http_graphite_storage_t;

typedef struct ngx_http_graphite_interval_s {
    ngx_str_t name;
    ngx_uint_t value;
} ngx_http_graphite_interval_t;

typedef double (*ngx_http_graphite_aggregate_pt)(const ngx_http_graphite_interval_t*, const ngx_http_graphite_acc_t*);

static double ngx_http_graphite_aggregate_avg(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc);
static double ngx_http_graphite_aggregate_persec(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc);
static double ngx_http_graphite_aggregate_sum(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc);

typedef struct ngx_http_graphite_aggregate_s {
    ngx_str_t name;
    ngx_http_graphite_aggregate_pt get;
} ngx_http_graphite_aggregate_t;

#define AGGREGATE_COUNT 3

static const ngx_http_graphite_aggregate_t ngx_http_graphite_aggregates[AGGREGATE_COUNT] = {
    { ngx_string("avg"), ngx_http_graphite_aggregate_avg },
    { ngx_string("persec"), ngx_http_graphite_aggregate_persec },
    { ngx_string("sum"), ngx_http_graphite_aggregate_sum },
};

typedef double (*ngx_http_graphite_source_handler_pt)(ngx_http_request_t*);

typedef struct ngx_http_graphite_source_s {
    ngx_str_t name;
    ngx_http_graphite_source_handler_pt get;
    ngx_http_graphite_aggregate_pt aggregate;
    ngx_uint_t type;
} ngx_http_graphite_source_t;

typedef struct ngx_http_graphite_param_s {
    ngx_str_t name;
    ngx_uint_t source;
    ngx_http_graphite_aggregate_pt aggregate;
    ngx_uint_t type;
    ngx_http_graphite_interval_t interval;
    ngx_uint_t percentile;
} ngx_http_graphite_param_t;

#if nginx_version >= 1009001
#define SOURCE_COUNT 17
#else
#define SOURCE_COUNT 15
#endif

static const ngx_http_graphite_source_t ngx_http_graphite_sources[SOURCE_COUNT] = {
    { .name = ngx_string("request_time"), .get = ngx_http_graphite_source_request_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("bytes_sent"), .get = ngx_http_graphite_source_bytes_sent, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("body_bytes_sent"), .get = ngx_http_graphite_source_body_bytes_sent, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("request_length"), .get = ngx_http_graphite_source_request_length, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("ssl_handshake_time"), .get = ngx_http_graphite_source_ssl_handshake_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("ssl_cache_usage"), .get = ngx_http_graphite_source_ssl_cache_usage, .aggregate = ngx_http_graphite_aggregate_avg, .type = NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF },
    { .name = ngx_string("content_time"), .get = ngx_http_graphite_source_content_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("gzip_time"), .get = ngx_http_graphite_source_gzip_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("upstream_time"), .get = ngx_http_graphite_source_upstream_time, .aggregate = ngx_http_graphite_aggregate_avg },
#if nginx_version >= 1009001
    { .name = ngx_string("upstream_connect_time"), .get = ngx_http_graphite_source_upstream_connect_time, .aggregate = ngx_http_graphite_aggregate_avg },
    { .name = ngx_string("upstream_header_time"), .get = ngx_http_graphite_source_upstream_header_time, .aggregate = ngx_http_graphite_aggregate_avg },
#endif
    { .name = ngx_string("rps"), .get = ngx_http_graphite_source_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("keepalive_rps"), .get = ngx_http_graphite_source_keepalive_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_2xx_rps"), .get = ngx_http_graphite_source_response_2xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_3xx_rps"), .get = ngx_http_graphite_source_response_3xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_4xx_rps"), .get = ngx_http_graphite_source_response_4xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
    { .name = ngx_string("response_5xx_rps"), .get = ngx_http_graphite_source_response_5xx_rps, .aggregate = ngx_http_graphite_aggregate_persec },
};

typedef struct ngx_http_graphite_metric_s {
    ngx_uint_t split;
    ngx_uint_t param;
} ngx_http_graphite_metric_t;

typedef struct ngx_http_graphite_statistic_s {
    ngx_uint_t split;
    ngx_uint_t param;
} ngx_http_graphite_statistic_t;

typedef struct ngx_http_graphite_data_t {
    ngx_array_t *metrics;
    ngx_array_t *statistics;
    ngx_http_complex_value_t *filter;
} ngx_http_graphite_data_t;

typedef struct ngx_http_graphite_template_arg_s {
    ngx_str_t name;
    int variable;
} ngx_http_graphite_template_arg_t;

typedef struct ngx_http_graphite_template_s {
    ngx_str_t data;
    int variable;
} ngx_http_graphite_template_t;

static char *ngx_http_graphite_template_compile(ngx_conf_t *cf, ngx_array_t *template, const ngx_http_graphite_template_arg_t *args, size_t nargs, const ngx_str_t *value);
static char *ngx_http_graphite_template_execute(char* buffer, size_t buffer_size, const ngx_array_t *template, const ngx_str_t *variables[]);
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
} ngx_http_graphite_default_data_variable_t;

#define DEFAULT_DATA_ARG_COUNT 1

static const ngx_http_graphite_template_arg_t ngx_http_graphite_default_data_args[DEFAULT_DATA_ARG_COUNT] = {
    { ngx_string("location"), TEMPLATE_VARIABLE_LOCATION },
};

#define DEFAULT_DATA_VARIABLES(location) {location}

static ngx_http_complex_value_t *ngx_http_graphite_complex_compile(ngx_conf_t *cf, ngx_str_t *value);

static ngx_int_t ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data);
static void ngx_http_graphite_statistic_init(ngx_http_graphite_stt_t *stt, ngx_uint_t percentile);
ngx_int_t ngx_http_graphite_handler(ngx_http_request_t *r);
static void ngx_http_graphite_timer_handler(ngx_event_t *ev);
void ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *gmcf, time_t ts);
static double *ngx_http_graphite_get_sources_values(ngx_http_request_t *r);

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

static ngx_int_t
ngx_http_graphite_init(ngx_conf_t *cf) {

    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_graphite_main_conf_t *gmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_hash_init_t hash;
    hash.hash = &gmcf->custom_params_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = gmcf->custom_params_hash_max_size != (ngx_uint_t)NGX_CONF_UNSET ? gmcf->custom_params_hash_max_size : 512;
    hash.bucket_size = gmcf->custom_params_hash_bucket_size != (ngx_uint_t)NGX_CONF_UNSET ? gmcf->custom_params_hash_bucket_size : ngx_align(64, ngx_cacheline_size);
    hash.name = "graphite_param_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    ngx_array_t *names = ngx_array_create(cf->pool, gmcf->custom_params->nelts, sizeof(ngx_hash_key_t));

    ngx_uint_t p;
    for (p = 0; p < gmcf->custom_params->nelts; p++) {
        ngx_str_t *name = &((ngx_str_t*)gmcf->custom_params->elts)[p];

        ngx_hash_key_t *hk = ngx_array_push(names);
        if (hk == NULL) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_ERROR;
        }

        hk->key = *name;
        hk->key_hash = ngx_hash_key_lc(name->data, name->len);
        hk->value = (void*)(p + 1);
    }

    if (ngx_hash_init(&hash, names->elts, names->nelts) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't init custom params hash");
        return NGX_ERROR;
    }

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL)
        return NGX_ERROR;

    *h = ngx_http_graphite_handler;

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_process_init(ngx_cycle_t *cycle) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_graphite_module);

    if (gmcf->enable) {

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

    gmcf->params = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_param_t));
    gmcf->splits = ngx_array_create(cf->pool, 1, sizeof(ngx_str_t));
    gmcf->intervals = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_interval_t));
    gmcf->default_params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    gmcf->custom_params = ngx_array_create(cf->pool, 1, sizeof(ngx_str_t));
    gmcf->metrics = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_metric_t));
    gmcf->statistics = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_statistic_t));
    gmcf->template = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gmcf->default_data_template = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gmcf->default_data_params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    gmcf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));
    gmcf->custom_datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));

    if (gmcf->params == NULL || gmcf->splits == NULL || gmcf->intervals == NULL || gmcf->default_params == NULL || gmcf->custom_params == NULL || gmcf->metrics == NULL || gmcf->statistics == NULL || gmcf->template == NULL || gmcf->default_data_template == NULL || gmcf->default_data_params == NULL || gmcf->datas == NULL || gmcf->custom_datas == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    gmcf->custom_params_hash_max_size = NGX_CONF_UNSET;
    gmcf->custom_params_hash_bucket_size = NGX_CONF_UNSET;

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
    ngx_http_graphite_srv_conf_t *gscf;
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
        gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);

        ngx_str_t *uri = &((ngx_str_t*)cf->args->elts)[cf->args->nelts - 1];
        ngx_str_t *location = ngx_http_graphite_location(cf->pool, uri);
        if (location == NULL)
            return NULL;

        if (location->len == 0)
            return glcf;

        if (ngx_http_graphite_add_default_data(cf, glcf->datas, location, gmcf->default_data_template, gmcf->default_data_params, gmcf->default_data_filter) != NGX_CONF_OK)
            return NULL;

        if (ngx_http_graphite_add_default_data(cf, glcf->datas, location, gscf->default_data_template, gscf->default_data_params, gscf->default_data_filter) != NGX_CONF_OK)
            return NULL;
    }

    return glcf;
}

static ngx_str_t *
ngx_http_graphite_location(ngx_pool_t *pool, ngx_str_t *uri) {

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

#define HOST_LEN 256

static char *
ngx_http_graphite_parse_args(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, const ngx_http_graphite_arg_t *args, ngx_uint_t args_count) {

    ngx_uint_t isset[args_count];
    ngx_memzero(isset, args_count * sizeof(ngx_uint_t));

    ngx_uint_t i;
    for (i = 1; i < cf->args->nelts; i++) {
        ngx_str_t *var = &((ngx_str_t*)cf->args->elts)[i];

        ngx_uint_t find = 0;
        ngx_uint_t j;
        for (j = 0; j < args_count; j++) {
            const ngx_http_graphite_arg_t *arg = &args[j];

            if (!ngx_strncmp(arg->name.data, var->data, arg->name.len) && var->data[arg->name.len] == '=') {
                isset[j] = 1;
                find = 1;
                ngx_str_t value;
                value.data = var->data + arg->name.len + 1;
                value.len = var->len - (arg->name.len + 1);

                if (arg->handler(cf, cmd, conf, &value) == NGX_CONF_ERROR) {
                    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite invalid option value %V", var);
                    return NGX_CONF_ERROR;
                }
                break;
            }
        }

        if (!find) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknown option %V", var);
            return NGX_CONF_ERROR;
        }
    }

    for (i = 0; i < args_count; i++) {
        if (!isset[i]) {

            const ngx_http_graphite_arg_t *arg = &args[i];
            if (arg->deflt.len) {
                if (arg->handler(cf, cmd, conf, (ngx_str_t*)&arg->deflt) == NGX_CONF_ERROR) {
                      ngx_conf_log_error(NGX_LOG_CRIT, cf, 0, "graphite invalid option default value %V", &arg->deflt);
                      return NGX_CONF_ERROR;
                }
            }
        }
    }

    return NGX_CONF_OK;
}

static ngx_http_graphite_param_t
ngx_http_graphite_source_param(ngx_uint_t source) {

    ngx_http_graphite_param_t param;
    ngx_memzero(&param, sizeof(param));
    param.name = ngx_http_graphite_sources[source].name;
    param.source = source;
    param.aggregate = ngx_http_graphite_sources[source].aggregate;
    param.type = ngx_http_graphite_sources[source].type;

    return param;
}

static ngx_int_t
ngx_http_graphite_new_param(ngx_conf_t *cf, ngx_http_graphite_param_t *param) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_uint_t p;
    for (p = 0; p < gmcf->params->nelts; p++) {
        ngx_http_graphite_param_t *old_param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[p];
        if (param->name.len == old_param->name.len && !ngx_strncmp(param->name.data, old_param->name.data, param->name.len)) {
            if (!param->percentile && !old_param->percentile) {
                if (param->aggregate == old_param->aggregate && param->interval.value == old_param->interval.value)
                    return p;
                else {
                    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param with different aggregate or interval");
                    return NGX_ERROR;
                }
            }
            else if (param->percentile == old_param->percentile)
                return p;
        }
    }

    ngx_http_graphite_param_t *new_param = ngx_array_push(gmcf->params);
    if (!new_param) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_ERROR;
    }

    *new_param = *param;

    return gmcf->params->nelts - 1;
}

static char *
ngx_http_graphite_push_param(ngx_conf_t *cf, ngx_array_t *params, ngx_uint_t param) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_uint_t i;
    for (i = 0; i < params->nelts; i++) {
        ngx_uint_t p = ((ngx_uint_t*)params->elts)[i];
        if (p == param) {
            ngx_http_graphite_param_t *old_param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[p];
            if (old_param->percentile == 0)
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite duplicate param %V", &old_param->name);
            else
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite duplicate param %V/%ui", &old_param->name, old_param->percentile);
            return NGX_CONF_ERROR;
        }
    }

    ngx_uint_t *new_param = ngx_array_push(params);
    if (!new_param) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    *new_param = param;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_add_param(ngx_conf_t *cf, ngx_uint_t split, ngx_uint_t param, ngx_array_t *metrics, ngx_array_t *statistics) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_http_graphite_param_t *p = &((ngx_http_graphite_param_t*)gmcf->params->elts)[param];

    if (p->percentile == 0) {
        ngx_uint_t i;
        for (i = 0; i < gmcf->metrics->nelts; i++) {
            ngx_http_graphite_metric_t *metric = &((ngx_http_graphite_metric_t*)gmcf->metrics->elts)[i];
            if (metric->split == split && metric->param == param)
                return NGX_CONF_OK;
        }

        ngx_uint_t *m = ngx_array_push(metrics);
        if (!m) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        *m = gmcf->metrics->nelts;

        ngx_http_graphite_metric_t *metric = ngx_array_push(gmcf->metrics);
        if (!metric) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        metric->split = split;
        metric->param = param;
    }
    else {
        ngx_uint_t i;
        for (i = 0; i < gmcf->statistics->nelts; i++) {
            ngx_http_graphite_statistic_t *statistic = &((ngx_http_graphite_statistic_t*)gmcf->statistics->elts)[i];
            if (statistic->split == split && statistic->param == param)
                return NGX_CONF_OK;
        }

        ngx_uint_t *s = ngx_array_push(statistics);
        if (!s) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        *s = gmcf->statistics->nelts;

        ngx_http_graphite_statistic_t *statistic = ngx_array_push(gmcf->statistics);
        if (!statistic) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        statistic->split = split;
        statistic->param = param;
    }

    return NGX_CONF_OK;
}

static ngx_http_graphite_data_t *
ngx_http_graphite_new_data(ngx_conf_t *cf, ngx_array_t *datas) {

    ngx_http_graphite_data_t *data = ngx_array_push(datas);
    if (!data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    data->metrics = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    data->statistics = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));
    if (data->metrics == NULL || data->statistics == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }
	data->filter = NULL;

    return data;
}

static char *
ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    if (ngx_http_graphite_parse_args(cf, cmd, conf, ngx_http_graphite_config_args, CONFIG_ARGS_COUNT) == NGX_CONF_ERROR)
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

    if (gmcf->server.s_addr == 0) {
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

    gmcf->buffer = ngx_palloc(cf->pool, gmcf->buffer_size);
    if (!gmcf->buffer) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    gmcf->enable = 1;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_default_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;

    gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);
    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

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

    if (ngx_http_graphite_template_compile(cf, template, ngx_http_graphite_default_data_args, DEFAULT_DATA_ARG_COUNT, &args[1]) != NGX_CONF_OK)
        return NGX_CONF_ERROR;

    if (cf->args->nelts >= 3) {
        ngx_uint_t i;
        for (i = 2; i < cf->args->nelts; i++) {
            if (ngx_strncmp(args[i].data, "params=", 7) == 0) {
                ngx_str_t value;
                value.len = args[i].len - 7;
                value.data = args[i].data + 7;
                if (ngx_http_graphite_parse_params(cf, &value, params) != NGX_CONF_OK)
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

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;
    ngx_http_graphite_loc_conf_t *glcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);
    gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);
    glcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

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
                if (ngx_http_graphite_parse_params(cf, &value, params) != NGX_CONF_OK)
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

static char *
ngx_http_graphite_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

    ngx_http_graphite_param_t param;
    ngx_memzero(&param, sizeof(ngx_http_graphite_param_t));

    if (ngx_http_graphite_parse_args(cf, cmd, (void*)&param, ngx_http_graphite_param_args, PARAM_ARGS_COUNT) == NGX_CONF_ERROR)
        return NGX_CONF_ERROR;

    if (!param.name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param name not set");
        return NGX_CONF_ERROR;
    }

    if (((param.aggregate || param.interval.value) && param.percentile) ||
        ((!param.aggregate || !param.interval.value) && !param.percentile)) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param must contain aggregate and interval or percentile");
        return NGX_CONF_ERROR;
    }

    if (param.interval.value > gmcf->max_interval) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param interval value is greather than max interval");
        return NGX_CONF_ERROR;
    }

    ngx_uint_t p = ngx_http_graphite_new_param(cf, &param);
    if ((ngx_int_t)p == NGX_ERROR)
        return NGX_CONF_ERROR;

    ngx_uint_t c;
    for (c = 0; c < gmcf->custom_params->nelts; c++) {
        ngx_str_t *name = &((ngx_str_t*)gmcf->custom_params->elts)[c];
        if (name->len == param.name.len && !ngx_strncmp(name->data, param.name.data, param.name.len))
            break;
    }

    ngx_http_graphite_data_t *data = NULL;
    if (c < gmcf->custom_params->nelts)
        data = &((ngx_http_graphite_data_t*)gmcf->custom_datas->elts)[c];
    else {
        ngx_str_t *name = ngx_array_push(gmcf->custom_params);
        if (!name) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }
        *name = param.name;

        data = ngx_http_graphite_new_data(cf, gmcf->custom_datas);
        if (!data)
            return NGX_CONF_ERROR;
    }

    if (ngx_http_graphite_add_param(cf, SPLIT_CUSTOM, p, data->metrics, data->statistics) != NGX_CONF_OK)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_prefix(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    return ngx_http_graphite_parse_string(cf, value, &gmcf->prefix);
}

static char *
ngx_http_graphite_config_arg_host(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    return ngx_http_graphite_parse_string(cf, value, &gmcf->host);
}

static char *
ngx_http_graphite_config_arg_protocol(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    return ngx_http_graphite_parse_string(cf, value, &gmcf->protocol);
}

#define SERVER_LEN 255

static char *
ngx_http_graphite_config_arg_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;

    char server[SERVER_LEN];
    if (value->len >= SERVER_LEN) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite server name too long");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy((u_char*)server, value->data, value->len);
    server[value->len] = '\0';

    in_addr_t a = inet_addr(server);
    if (a != (in_addr_t)-1 || !a)
        gmcf->server.s_addr = a;
    else
    {
        struct hostent *host = gethostbyname(server);
        if (host != NULL) {
            gmcf->server = *(struct in_addr*)*host->h_addr_list;
        }
        else {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't resolve server name");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_port(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    gmcf->port = ngx_atoi(value->data, value->len);

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_frequency(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    gmcf->frequency = ngx_atoi(value->data, value->len) * 1000;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_intervals(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;

    ngx_uint_t i;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; i++) {

        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config intervals is empty");
                return NGX_CONF_ERROR;
            }

            ngx_http_graphite_interval_t *interval = ngx_array_push(gmcf->intervals);
            if (!interval) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            interval->name.data = ngx_palloc(cf->pool, i - s);
            if (!interval->name.data) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            ngx_memcpy(interval->name.data, &value->data[s], i - s);
            interval->name.len = i - s;

            if (ngx_http_graphite_parse_time(&interval->name, &interval->value) == NGX_CONF_ERROR) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config interval is invalid");
                return NGX_CONF_ERROR;
            }

            if (interval->value > gmcf->max_interval)
                gmcf->max_interval = interval->value;

            s = i + 1;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_params(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_params(cf, value, gmcf->default_params);
}

static char *
ngx_http_graphite_config_arg_shared(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(value, &gmcf->shared_size);
}

static char *
ngx_http_graphite_config_arg_buffer(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(value, &gmcf->buffer_size);
}

static char *
ngx_http_graphite_config_arg_package(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(value, &gmcf->package_size);
}

static char *
ngx_http_graphite_config_arg_template(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_template_compile(cf, gmcf->template, ngx_http_graphite_template_args, TEMPLATE_ARG_COUNT, value);
}

static char *
ngx_http_graphite_param_arg_name(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    param->name.data = ngx_pcalloc(cf->pool, value->len);
    if (!param->name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    ngx_memcpy(param->name.data, value->data, value->len);
    param->name.len = value->len;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_aggregate(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    ngx_uint_t find = 0;
    ngx_uint_t a;
    for (a = 0; a < AGGREGATE_COUNT; a++) {
        if ((ngx_http_graphite_aggregates[a].name.len == value->len) && !ngx_strncmp(ngx_http_graphite_aggregates[a].name.data, value->data, value->len)) {
            find = 1;
            param->aggregate = ngx_http_graphite_aggregates[a].get;
        }
    }

    if (!find) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param unknow aggregate %V", value);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_interval(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;
    ngx_http_graphite_interval_t *interval = &param->interval;

    interval->name.data = ngx_palloc(cf->pool, value->len);
    if (!interval->name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(interval->name.data, value->data, value->len);
    interval->name.len = value->len;

    if (ngx_http_graphite_parse_time(value, &interval->value) == NGX_CONF_ERROR) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param interval is invalid");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_percentile(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    ngx_int_t percentile = ngx_atoi(value->data, value->len);
    if (percentile == NGX_ERROR || percentile <= 0 || percentile > 100) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param percentile is invalid");
        return NGX_CONF_ERROR;
    }

    param->percentile = percentile;

    return NGX_CONF_OK;
}

static ngx_uint_t
ngx_http_graphite_add_split(ngx_conf_t *cf, ngx_str_t *name) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

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

        split->data = ngx_pstrdup(cf->pool, name);
        if (!split->data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return SPLIT_EMPTY;
        }

        split->len = name->len;
    }

    return s;
}

static char *
ngx_http_graphite_add_default_data(ngx_conf_t *cf, ngx_array_t *datas, ngx_str_t *location, ngx_array_t *template, ngx_array_t *params, ngx_http_complex_value_t *filter) {

    if (template->nelts == 0)
        return NGX_CONF_OK;

    const ngx_str_t *variables[] = DEFAULT_DATA_VARIABLES(location);

    ngx_str_t split;
    split.len = ngx_http_graphite_template_len(template, variables);
    if (split.len == 0)
        return NGX_CONF_ERROR;

    split.data = ngx_palloc(cf->pool, split.len);
    if (!split.data)
        return NGX_CONF_ERROR;

    ngx_http_graphite_template_execute((char*)split.data, split.len, template, variables);

    return ngx_http_graphite_add_data(cf, datas, &split, params, filter);
}

static char *
ngx_http_graphite_add_data(ngx_conf_t *cf, ngx_array_t *datas, ngx_str_t *split, ngx_array_t *params, ngx_http_complex_value_t *filter) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_uint_t s = ngx_http_graphite_add_split(cf, split);
    if (s == SPLIT_EMPTY)
        return NGX_CONF_ERROR;

    ngx_http_graphite_data_t *data = ngx_http_graphite_new_data(cf, datas);
    if (!data)
        return NGX_CONF_ERROR;

    if (params->nelts == 0)
        params = gmcf->default_params;

    ngx_uint_t i;
    for (i = 0; i < params->nelts; i++) {
        ngx_uint_t p = ((ngx_uint_t*)params->elts)[i];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[p];

        if (param->type != 0) {
            if ((cf->cmd_type & NGX_HTTP_MAIN_CONF) && !(param->type & NGX_HTTP_MAIN_CONF))
                continue;
            if ((cf->cmd_type & NGX_HTTP_SRV_CONF) && !(param->type & NGX_HTTP_SRV_CONF))
                continue;
            if ((cf->cmd_type & NGX_HTTP_LOC_CONF) && !(param->type & NGX_HTTP_LOC_CONF))
                continue;
        }
        ngx_http_graphite_add_param(cf, s, p, data->metrics, data->statistics);
    }

    data->filter = filter;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_params(ngx_conf_t *cf, ngx_str_t *value, ngx_array_t *params) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_uint_t i = 0;
    ngx_uint_t s = 0;
    ngx_uint_t q = NGX_CONF_UNSET_UINT;
    for (i = 0; i <= value->len; i++) {
        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite params is empty");
                return NGX_CONF_ERROR;
            }

            if (i - s == 1 && value->data[s] == '*') {
                if (params == gmcf->default_params) {
                    ngx_uint_t c;
                    for (c = 0; c < SOURCE_COUNT; c++) {

                        ngx_http_graphite_param_t param = ngx_http_graphite_source_param(c);

                        ngx_uint_t p = ngx_http_graphite_new_param(cf, &param);
                        if ((ngx_int_t)p == NGX_ERROR)
                            return NGX_CONF_ERROR;

                        if (ngx_http_graphite_push_param(cf, params, p) != NGX_CONF_OK)
                            return NGX_CONF_ERROR;
                    }
                }
                else {
                    ngx_uint_t p;
                    for (p = 0; p < gmcf->default_params->nelts; p++) {
                        ngx_uint_t param = ((ngx_uint_t*)gmcf->default_params->elts)[p];
                        if (ngx_http_graphite_push_param(cf, params, param) != NGX_CONF_OK)
                            return NGX_CONF_ERROR;
                    }
                }
            }
            else {
                ngx_uint_t find = 0;
                ngx_uint_t c;
                for (c = 0; c < SOURCE_COUNT; c++) {
                    if (q == NGX_CONF_UNSET_UINT)
                        q = i;
                    if ((ngx_http_graphite_sources[c].name.len == q - s) && !ngx_strncmp(ngx_http_graphite_sources[c].name.data, &value->data[s], q - s)) {
                        find = 1;

                        ngx_http_graphite_param_t param = ngx_http_graphite_source_param(c);

                        if (q != i) {
                            ngx_int_t percentile = ngx_atoi(&value->data[q + 1], i - q - 1);
                            if (percentile == NGX_ERROR || percentile <= 0 || percentile > 100) {
                                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite bad param %*s", i - s, &value->data[s]);
                                return NGX_CONF_ERROR;
                            }

                            param.percentile = percentile;
                        }

                        ngx_uint_t p = ngx_http_graphite_new_param(cf, &param);
                        if ((ngx_int_t)p == NGX_ERROR)
                            return NGX_CONF_ERROR;

                        if (ngx_http_graphite_push_param(cf, params, p) != NGX_CONF_OK)
                            return NGX_CONF_ERROR;
                        break;
                    }
                }

                if (!find) {
                    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknow param %*s", i - s, &value->data[s]);
                    return NGX_CONF_ERROR;
                }
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
ngx_http_graphite_parse_string(ngx_conf_t *cf, ngx_str_t *value, ngx_str_t *result) {

    result->data = ngx_pstrdup(cf->pool, value);
    if (!result->data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    result->len = value->len;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_size(ngx_str_t *value, ngx_uint_t *result) {

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
ngx_http_graphite_parse_time(ngx_str_t *value, ngx_uint_t *result) {

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
ngx_http_graphite_template_compile(ngx_conf_t *cf, ngx_array_t *template, const ngx_http_graphite_template_arg_t *args, size_t nargs, const ngx_str_t *value) {

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
                    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                    return NGX_CONF_ERROR;
                }

                if (state == TEMPLATE_STATE_NONE) {
                    arg->data.data = ngx_palloc(cf->pool, i - s);
                    if (!arg->data.data) {
                        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                        return NGX_CONF_ERROR;
                    }
                    ngx_memcpy(arg->data.data, value->data + s, i - s);
                    arg->data.len = i - s;
                    arg->variable = 0;
                }
                else if (state == TEMPLATE_STATE_VAR_START || state == TEMPLATE_STATE_BRACKET_VAR_START) {
                    ngx_uint_t find = 0;
                    size_t a;
                    for (a = 0; a < nargs; a++) {
                        if ((args[a].name.len == i - s) && !ngx_strncmp(args[a].name.data, &value->data[s], i - s)) {
                            find = 1;
                            arg->variable = args[a].variable;
                            arg->data.data = NULL;
                            arg->data.len = 0;
                        }
                    }

                    if (!find) {
                        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknow template arg %*s", i - s, &value->data[s]);
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

static char *
ngx_http_graphite_template_execute(char* buffer, size_t buffer_size, const ngx_array_t *template, const ngx_str_t *variables[]) {

    char *b = buffer;

    ngx_uint_t i;
    for (i = 0; i < template->nelts; i++) {
        ngx_http_graphite_template_t *arg = &((ngx_http_graphite_template_t*)template->elts)[i];
        const ngx_str_t *data = NULL;

        if (arg->data.len)
            data = &arg->data;
        else
            data = variables[arg->variable];

        b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V", data);
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

static ngx_int_t
ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_graphite_main_conf_t *gmcf = shm_zone->data;

    if (data) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory data set");
        return NGX_ERROR;
    }

    size_t shared_required_size = 0;
    shared_required_size += sizeof(ngx_http_graphite_storage_t);
    shared_required_size += sizeof(ngx_http_graphite_acc_t) * (gmcf->max_interval + 1) * gmcf->metrics->nelts + sizeof(ngx_http_graphite_stt_t) * gmcf->statistics->nelts;

    if (sizeof(ngx_slab_pool_t) + shared_required_size > shm_zone->shm.size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small shared memory (minimum size is %uzb)", sizeof(ngx_slab_pool_t) + shared_required_size);
        return NGX_ERROR;
    }

    // 128 is the approximate size of the one udp record
    size_t buffer_required_size = (gmcf->intervals->nelts * gmcf->metrics->nelts + gmcf->statistics->nelts) * 128;
    if (buffer_required_size > gmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small buffer size (minimum size is %uzb)", buffer_required_size);
        return NGX_ERROR;
    }

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t *)shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory exists");
        return NGX_ERROR;
    }

    char *p = ngx_slab_alloc(shpool, shared_required_size);
    if (!p) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    shpool->data = p;

    ngx_memzero(p, shared_required_size);

    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)p;
    p += sizeof(ngx_http_graphite_storage_t);

    storage->accs = (ngx_http_graphite_acc_t*)p;
    p += sizeof(ngx_http_graphite_acc_t) * (gmcf->max_interval + 1) * gmcf->metrics->nelts;

    storage->stts = (ngx_http_graphite_stt_t*)p;
    p += sizeof(ngx_http_graphite_stt_t) * gmcf->statistics->nelts;

    ngx_uint_t s;
    for (s = 0; s < gmcf->statistics->nelts; s++) {
        const ngx_http_graphite_statistic_t *statistic = &(((ngx_http_graphite_statistic_t*)gmcf->statistics->elts)[s]);
        ngx_http_graphite_stt_t *stt = &storage->stts[s];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params)[statistic->param];
        ngx_http_graphite_statistic_init(stt, param->percentile);
    }

    time_t ts = ngx_time();
    storage->start_time = ts;
    storage->last_time = ts;
    storage->event_time = ts;

    return NGX_OK;
}

static void
ngx_http_graphite_statistic_init(ngx_http_graphite_stt_t *stt, ngx_uint_t percentile) {

    ngx_memzero(stt, sizeof(*stt));

    double p = (double)percentile / 100;

    stt->dn[P2_METRIC_COUNT - 1] = 1;
    stt->dn[P2_METRIC_COUNT / 2] = p;

    size_t i;
    for (i = 1; i < P2_METRIC_COUNT / 2; i++)
        stt->dn[i] = (p / (P2_METRIC_COUNT / 2)) * i;
    for (i = P2_METRIC_COUNT / 2 + 1; i < P2_METRIC_COUNT - 1; i++)
        stt->dn[i] = p + ((1 - p) / (P2_METRIC_COUNT - 1 - P2_METRIC_COUNT / 2)) * (i - P2_METRIC_COUNT / 2);

    for (i = 0; i < P2_METRIC_COUNT; i++) {
        stt->np[i] = (P2_METRIC_COUNT - 1) * stt->dn[i] + 1;
        stt->n[i] = i + 1;
    }
}

static void
ngx_http_graphite_add_metric(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_uint_t metric, double value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    ngx_uint_t k = ((ts - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->metrics->nelts + metric;
    ngx_http_graphite_acc_t *acc = &storage->accs[k];

    acc->count++;
    acc->value += value;
}

static void
ngx_http_graphite_add_statistic(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_uint_t statistic, double value, ngx_uint_t percentile) {

    ngx_uint_t s = statistic;
    ngx_http_graphite_stt_t *stt = &storage->stts[s];

    if (stt->count >= P2_METRIC_COUNT) {

        size_t k;
        for (k = 0; k < P2_METRIC_COUNT; k++) {
            if (value < stt->q[k])
                break;
        }
        if (k == 0) {
            k = 1;
            stt->q[0] = value;
        }
        else if (k == P2_METRIC_COUNT) {
            k = 4;
            stt->q[P2_METRIC_COUNT - 1] = value;
        }

        size_t i;
        for (i = 0; i < P2_METRIC_COUNT; i++) {
            if (i >= k)
                stt->n[i]++;
            stt->np[i] += stt->dn[i];
        }

        for (i = 1; i < P2_METRIC_COUNT - 1; i++) {
            double d = stt->np[i] - stt->n[i];
            int s = (d >= 0.0) ? 1 : -1;
            if((d >= 1.0 && stt->n[i + 1] - stt->n[i] > 1) || (d <= -1.0 && stt->n[i - 1] - stt->n[i] < -1)) {
                double a = stt->q[i] + (double)s * ((stt->n[i] - stt->n[i - 1] + s) * (stt->q[i + 1] - stt->q[i]) / (stt->n[i + 1] - stt->n[i]) + (stt->n[i + 1] - stt->n[i] - s) * (stt->q[i] - stt->q[i - 1]) / (stt->n[i] - stt->n[i - 1])) / (stt->n[i + 1] - stt->n[i - 1]);
                if (a <= stt->q[i - 1] || stt->q[i + 1] <= a)
                    a = stt->q[i] + (double)s * (stt->q[i + s] - stt->q[i]) / (stt->n[i + s] - stt->n[i]);
                stt->q[i] = a;
                stt->n[i] += s;
            }
        }
    }
    else {
        size_t i = 0;
        for (i = 0; i < stt->count; i++) {
            if (value < stt->q[i])
                break;
        }
        if (stt->count != 0 && i < P2_METRIC_COUNT - 1)
            memmove(&stt->q[i + 1], &stt->q[i], (stt->count - i) * sizeof(*stt->q));
        stt->q[i] = value;
        stt->count++;
    }
}

static void
ngx_http_graphite_add_data_values(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_http_graphite_data_t *data, const double *values) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

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
        ngx_http_graphite_metric_t *metric = &((ngx_http_graphite_metric_t*)gmcf->metrics->elts)[m];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[metric->param];
        ngx_http_graphite_add_metric(r, storage, ts, m, values[param->source]);
    }

    for (i = 0; i < data->statistics->nelts; i++) {
        ngx_uint_t s = ((ngx_uint_t*)data->statistics->elts)[i];
        ngx_http_graphite_statistic_t *statistic = &((ngx_http_graphite_statistic_t*)gmcf->statistics->elts)[s];
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[statistic->param];
        ngx_http_graphite_add_statistic(r, storage, ts, s, values[param->source], param->percentile);
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

ngx_int_t
ngx_http_graphite_handler(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;
    ngx_http_graphite_loc_conf_t *glcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);
    gscf = ngx_http_get_module_srv_conf(r, ngx_http_graphite_module);
    glcf = ngx_http_get_module_loc_conf(r, ngx_http_graphite_module);

    if (!gmcf->enable)
        return NGX_OK;

    if (gmcf->datas->nelts == 0 && gscf->datas->nelts == 0 && glcf->datas->nelts == 0)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    time_t ts = ngx_time();

    double *values = ngx_http_graphite_get_sources_values(r);
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

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_custom(ngx_http_request_t *r, ngx_str_t *name, double value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    if (!gmcf->enable)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    time_t ts = ngx_time();

    ngx_shmtx_lock(&shpool->mutex);

    ngx_http_graphite_del_old_records(gmcf, ts);

    ngx_uint_t key = ngx_hash_key_lc(name->data, name->len);
    ngx_uint_t p = (ngx_uint_t)ngx_hash_find(&gmcf->custom_params_hash, key, name->data, name->len);

    if (p) {
        ngx_http_graphite_data_t *data = &((ngx_http_graphite_data_t*)gmcf->custom_datas->elts)[p - 1];
        ngx_http_graphite_add_data_values(r, storage, ts, data, &value);
    }

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

static char*
ngx_http_graphite_print_metric(ngx_http_graphite_main_conf_t *gmcf, ngx_http_graphite_storage_t *storage, ngx_uint_t m, const ngx_http_graphite_interval_t *interval, time_t ts, char *buffer, size_t buffer_size) {

    const ngx_http_graphite_metric_t *metric = &(((ngx_http_graphite_metric_t*)gmcf->metrics->elts)[m]);
    const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[metric->param];

    ngx_http_graphite_acc_t a;
    a.value = 0;
    a.count = 0;

    unsigned l;
    for (l = 0; l < interval->value; l++) {
        if ((time_t)(ts - l - 1) >= storage->start_time) {
            ngx_uint_t k = ((ts - l - 1 - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->metrics->nelts + m;
            ngx_http_graphite_acc_t *acc = &storage->accs[k];
            a.value += acc->value;
            a.count += acc->count;
        }
    }

    double value = param->aggregate(interval, &a);

    char *b = buffer;

    if (metric->split != SPLIT_CUSTOM) {
        const ngx_str_t *split = &((ngx_str_t*)gmcf->splits->elts)[metric->split];
        if (!gmcf->template->nelts) {
            if (gmcf->prefix.len)
                b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
            b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V.%V_%V", &gmcf->host, split, &param->name, &interval->name);
        }
        else {
            const ngx_str_t *variables[] = TEMPLATE_VARIABLES(&gmcf->prefix, &gmcf->host, split, &param->name, &interval->name);
            b = ngx_http_graphite_template_execute(b, buffer_size - (b - buffer), gmcf->template, variables);
        }
    }
    else {
        if (gmcf->prefix.len)
            b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
        b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V", &gmcf->host, &param->name);
    }

    b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), " %.3f %T\n", value, ts);

    return b;
}

static char*
ngx_http_graphite_print_statistic(ngx_http_graphite_main_conf_t *gmcf, ngx_http_graphite_storage_t *storage, ngx_uint_t s, time_t ts, char *buffer, size_t buffer_size) {

    const ngx_http_graphite_statistic_t *statistic = &(((ngx_http_graphite_statistic_t*)gmcf->statistics->elts)[s]);
    const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[statistic->param];

    u_char p[4];
    ngx_str_t percentile;
    percentile.data = p;
    percentile.len = ngx_snprintf(p, sizeof(p), "p%ui", param->percentile) - p;

    ngx_http_graphite_stt_t *stt = &storage->stts[s];

    char *b = buffer;

    if (statistic->split != SPLIT_CUSTOM) {
        const ngx_str_t *split = &((ngx_str_t*)gmcf->splits->elts)[statistic->split];
        if (!gmcf->template->nelts) {
            if (gmcf->prefix.len)
                b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
            b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V.%V_%V", &gmcf->host, split, &param->name, &percentile);
        }
        else {
            const ngx_str_t *variables[] = TEMPLATE_VARIABLES(&gmcf->prefix, &gmcf->host, split, &param->name, &percentile);
            b = ngx_http_graphite_template_execute(b, buffer_size - (b - buffer), gmcf->template, variables);
        }
    }
    else {
        if (gmcf->prefix.len)
            b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.", &gmcf->prefix);
        b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V.%V_%V", &gmcf->host, &param->name, &percentile);
    }

    b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), " %.3f %T\n", stt->q[P2_METRIC_COUNT / 2], ts);

    return b;
}

static ngx_int_t
ngx_http_graphite_send_buffer(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log, char *buffer) {

    struct sockaddr_in sin;
    ngx_memzero((char *) &sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr = gmcf->server;
    sin.sin_port = htons(gmcf->port);

    int fd;
    if (ngx_strncmp(gmcf->protocol.data, "tcp", 3) == 0)
        fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    else
        fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (fd < 0) {
        ngx_log_error(NGX_LOG_ALERT, log, 0, "graphite can't create socket");
        return NGX_ERROR;
    }

    if (ngx_strncmp(gmcf->protocol.data, "tcp", 3) == 0) {

        if (ngx_nonblocking(fd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, log, 0, "graphite can't set tcp socket nonblocking");
            close(fd);
            return NGX_ERROR;
        }

        if (connect(fd, (struct sockaddr*)&sin, sizeof(sin)) == -1) {
            if (ngx_socket_errno != NGX_EINPROGRESS) {
               ngx_log_error(NGX_LOG_ALERT, log, ngx_socket_errno, "graphite tcp connect failed");
               close(fd);
               return NGX_ERROR;
            }
        }
    }

    char *part = buffer;
    char *next = NULL;
    char *nl = NULL;

    while (*part) {
        next = part;
        nl = part;

        while ((next = strchr(next, '\n')) && ((size_t)(next - part) <= gmcf->package_size)) {
            nl = next;
            next++;
        }
        if (nl > part) {
            if (ngx_strncmp(gmcf->protocol.data, "udp", 3) == 0) {
                if (sendto(fd, part, nl - part + 1, 0, (struct sockaddr*)&sin, sizeof(sin)) == -1)
                    ngx_log_error(NGX_LOG_ALERT, log, 0, "graphite can't send udp packet");
            }
            else if (ngx_strncmp(gmcf->protocol.data, "tcp", 3) == 0) {
                if (send(fd, part, nl - part + 1, 0) < 0) {
                    if (ngx_socket_errno == NGX_EAGAIN) {
                        ngx_log_error(NGX_LOG_ALERT, log, 0, "graphite tcp buffer is full");
                    }
                    else {
                        ngx_log_error(NGX_LOG_ALERT, log, 0, "graphite can't send tcp packet");
                    }
                }
            }
        }
        else {
            ngx_log_error(NGX_LOG_ALERT, log, 0, "graphite package size too small, need send %z", (size_t)(next - part));
        }

        part = nl + 1;
    }

    close(fd);

    return NGX_OK;
}

static void
ngx_http_graphite_timer_handler(ngx_event_t *ev) {

    time_t ts = ngx_time();

    ngx_http_graphite_main_conf_t *gmcf;
    gmcf = ev->data;

    char *buffer = gmcf->buffer;
    char *b = buffer;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_shmtx_lock(&shpool->mutex);

    if ((ngx_uint_t)(ts - storage->event_time) * 1000 < gmcf->frequency) {
        ngx_shmtx_unlock(&shpool->mutex);
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, gmcf->frequency);
        return;
    }

    storage->event_time = ts;

    ngx_http_graphite_del_old_records(gmcf, ts);

    ngx_uint_t m;
    for (m = 0; m < gmcf->metrics->nelts; m++) {
        const ngx_http_graphite_metric_t *metric = &(((ngx_http_graphite_metric_t*)gmcf->metrics->elts)[m]);
        const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[metric->param];

        if (metric->split != SPLIT_CUSTOM) {
            ngx_uint_t i;
            for (i = 0; i < gmcf->intervals->nelts; i++) {
                const ngx_http_graphite_interval_t *interval = &((ngx_http_graphite_interval_t*)gmcf->intervals->elts)[i];
                b = ngx_http_graphite_print_metric(gmcf, storage, m, interval, ts, b, gmcf->buffer_size - (b - buffer));
            }
        }
        else
            b = ngx_http_graphite_print_metric(gmcf, storage, m, &param->interval, ts, b, gmcf->buffer_size - (b - buffer));
    }

    ngx_uint_t s;
    for (s = 0; s < gmcf->statistics->nelts; s++) {
        const ngx_http_graphite_statistic_t *statistic = &(((ngx_http_graphite_statistic_t*)gmcf->statistics->elts)[s]);
        const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[statistic->param];

        b = ngx_http_graphite_print_statistic(gmcf, storage, s, ts, b, gmcf->buffer_size - (b - buffer));

        ngx_http_graphite_stt_t *stt = &storage->stts[s];
        ngx_http_graphite_statistic_init(stt, param->percentile);
    }

    ngx_shmtx_unlock(&shpool->mutex);

    if (b == buffer + gmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite buffer size is too small");
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, gmcf->frequency);
        return;
    }
    *b = '\0';

    if (b != buffer)
        ngx_http_graphite_send_buffer(gmcf, ev->log, buffer);

    if (!(ngx_quit || ngx_terminate || ngx_exiting))
        ngx_add_timer(ev, gmcf->frequency);
}

void
ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *gmcf, time_t ts) {

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    while ((ngx_uint_t)storage->last_time + gmcf->max_interval < (ngx_uint_t)ts) {

        ngx_uint_t m;
        for (m = 0; m < gmcf->metrics->nelts; m++) {

            ngx_uint_t a = ((storage->last_time - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->metrics->nelts + m;
            ngx_http_graphite_acc_t *acc = &storage->accs[a];

            acc->value = 0;
            acc->count = 0;
        }

        storage->last_time++;
    }
}

static double *
ngx_http_graphite_get_sources_values(ngx_http_request_t *r) {

    double *values = ngx_palloc(r->pool, sizeof(double) * SOURCE_COUNT);
    if (!values)
        return NULL;

    ngx_uint_t s;
    for (s = 0; s < SOURCE_COUNT; s++) {

        const ngx_http_graphite_source_t *source = &ngx_http_graphite_sources[s];
        if (source->get)
            values[s] = source->get(r);
    }

    return values;
}

static double
ngx_http_graphite_source_request_time(ngx_http_request_t *r) {

    ngx_time_t      *tp;
    ngx_msec_int_t   ms;

    tp = ngx_timeofday();
    ms = (ngx_msec_int_t) ((tp->sec - r->start_sec) * 1000 + (tp->msec - r->start_msec));

    return (double)ms;
}

static double
ngx_http_graphite_source_bytes_sent(ngx_http_request_t *r) {

    return (double)r->connection->sent;
}

static double
ngx_http_graphite_source_body_bytes_sent(ngx_http_request_t *r) {

    off_t  length;

    length = r->connection->sent - r->header_size;
    length = ngx_max(length, 0);

    return (double)length;
}

static double
ngx_http_graphite_source_request_length(ngx_http_request_t *r) {

    return (double)r->request_length;
}

static double
ngx_http_graphite_source_ssl_handshake_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (defined(NGX_GRAPHITE_PATCH) && (NGX_SSL))
    if (r->connection->requests == 1) {
        ngx_ssl_connection_t *ssl = r->connection->ssl;
        if (ssl)
            ms = (ngx_msec_int_t)((ssl->handshake_end_sec - ssl->handshake_start_sec) * 1000 + (ssl->handshake_end_msec - ssl->handshake_start_msec));
    }
#endif

    return (double)ms;
}

static double
ngx_http_graphite_source_ssl_cache_usage(ngx_http_request_t *r) {

    double usage;

    usage = 0;

#if (defined(NGX_GRAPHITE_PATCH) && (NGX_SSL))
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
ngx_http_graphite_source_content_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (defined(NGX_GRAPHITE_PATCH))
    ms = (ngx_msec_int_t)((r->content_end_sec - r->content_start_sec) * 1000 + (r->content_end_msec - r->content_start_msec));
#endif

    ms = ngx_max(ms, 0);

    return (double)ms;
}

static double
ngx_http_graphite_source_gzip_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (defined(NGX_GRAPHITE_PATCH) && (NGX_HTTP_GZIP))
    ms = (ngx_msec_int_t)((r->gzip_end_sec - r->gzip_start_sec) * 1000 + (r->gzip_end_msec - r->gzip_start_msec));
#endif
    ms = ngx_max(ms, 0);

    return (double)ms;
}

static double
ngx_http_graphite_source_upstream_time(ngx_http_request_t *r) {

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

#if nginx_version >= 1009001
static double
ngx_http_graphite_source_upstream_connect_time(ngx_http_request_t *r) {

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
}

static double
ngx_http_graphite_source_upstream_header_time(ngx_http_request_t *r) {

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
}
#endif

static double
ngx_http_graphite_source_rps(ngx_http_request_t *r) {

    return 1;
}

static double
ngx_http_graphite_source_keepalive_rps(ngx_http_request_t *r) {

    if (r->connection->requests == 1)
        return 0;
    else
        return 1;
}

static double
ngx_http_graphite_source_response_2xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 2)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_3xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 3)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_4xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 4)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_source_response_5xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 5)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_aggregate_avg(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc) {

    return (acc->count != 0) ? acc->value / acc->count : 0;
}

static double
ngx_http_graphite_aggregate_persec(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc) {

    return acc->value / interval->value;
}

static double
ngx_http_graphite_aggregate_sum(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc) {

    return acc->value;
}
