#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {

    ngx_uint_t enable;

    ngx_str_t host;
    ngx_shm_zone_t *shared;
    char *buffer;

    int socket;

    ngx_str_t prefix;

    struct in_addr server;
    int port;
    ngx_uint_t frequency;

    ngx_array_t *intervals;
    ngx_uint_t max_interval;
    ngx_array_t *splits;
    ngx_array_t *params;

    size_t shared_size;
    size_t buffer_size;
    size_t package_size;

} ngx_http_graphite_main_conf_t;

typedef struct {
    ngx_uint_t split;
} ngx_http_graphite_loc_conf_t;

#define SPLIT_EMPTY (ngx_uint_t)-1

static ngx_int_t ngx_http_graphite_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_process_init(ngx_cycle_t *cycle);

static void *ngx_http_graphite_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_loc_conf(ngx_conf_t *cf);

#if (NGX_SSL)
static ngx_int_t ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);
#endif

static char *ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_http_graphite_arg_prefix(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_port(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_frequency(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_intervals(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_params(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_shared(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_buffer(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_arg_package(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);

static char *ngx_http_graphite_parse_size(ngx_str_t *value, size_t *result);

static ngx_uint_t ngx_http_graphite_param_request_time(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_bytes_sent(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_body_bytes_sent(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_request_length(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_ssl_handshake_time(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_content_time(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_gzip_time(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_upstream_time(ngx_http_request_t *r);
static ngx_uint_t ngx_http_graphite_param_rps(ngx_http_request_t *r);

static ngx_command_t ngx_http_graphite_commands[] = {

    { ngx_string("graphite_config"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_ANY,
      ngx_http_graphite_config,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("graphite_data"),
      NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE1,
      ngx_http_graphite_data,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t ngx_http_graphite_module_ctx = {
    ngx_http_graphite_add_variables,       /* preconfiguration */
    ngx_http_graphite_init,                /* postconfiguration */

    ngx_http_graphite_create_main_conf,    /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
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

typedef char *(*ngx_http_graphite_arg_handler_pt)(ngx_conf_t*, ngx_command_t*, void *, ngx_str_t *value);

typedef struct ngx_http_graphite_arg_s {
    ngx_str_t name;
    ngx_http_graphite_arg_handler_pt handler;
} ngx_http_graphite_arg_t;

#define ARGS_COUNT 9

static const ngx_http_graphite_arg_t ngx_http_graphite_args[ARGS_COUNT] = {
    { ngx_string("prefix"), ngx_http_graphite_arg_prefix },
    { ngx_string("server"), ngx_http_graphite_arg_server },
    { ngx_string("port"), ngx_http_graphite_arg_port },
    { ngx_string("frequency"), ngx_http_graphite_arg_frequency },
    { ngx_string("intervals"), ngx_http_graphite_arg_intervals },
    { ngx_string("params"), ngx_http_graphite_arg_params },
    { ngx_string("shared"), ngx_http_graphite_arg_shared },
    { ngx_string("buffer"), ngx_http_graphite_arg_buffer },
    { ngx_string("package"), ngx_http_graphite_arg_package }
};

static ngx_event_t timer_event;

typedef struct ngx_http_graphite_acc_s {
    uint64_t value;
    ngx_uint_t count;
} ngx_http_graphite_acc_t;

typedef struct ngx_http_graphite_record_s {
    ngx_http_graphite_acc_t acc;
} ngx_http_graphite_record_t;

typedef struct ngx_http_graphite_data_s {
    ngx_http_graphite_record_t *records;
    ngx_http_graphite_acc_t *accs;

    time_t start_time;
    time_t *last_time;
    time_t event_time;

} ngx_http_graphite_data_t;

typedef struct ngx_http_graphite_interval_s {
    ngx_str_t name;
    ngx_uint_t value;
} ngx_http_graphite_interval_t;

typedef ngx_uint_t (*ngx_http_graphite_param_handler_pt)(ngx_http_request_t*);
typedef double (*ngx_http_graphite_param_value_pt)(ngx_http_graphite_interval_t*, ngx_http_graphite_acc_t*);

static double ngx_http_graphite_param_value_avg(ngx_http_graphite_interval_t *interval, ngx_http_graphite_acc_t *acc);
static double ngx_http_graphite_param_value_persec(ngx_http_graphite_interval_t *interval, ngx_http_graphite_acc_t *acc);

typedef struct ngx_http_graphite_param_s {
    ngx_str_t name;
    ngx_http_graphite_param_handler_pt get;
    ngx_http_graphite_param_value_pt value;
} ngx_http_graphite_param_t;

#define PARAM_COUNT 9

static const ngx_http_graphite_param_t ngx_http_graphite_params[PARAM_COUNT] = {
    { ngx_string("request_time"), ngx_http_graphite_param_request_time, ngx_http_graphite_param_value_avg },
    { ngx_string("bytes_sent"), ngx_http_graphite_param_bytes_sent, ngx_http_graphite_param_value_avg },
    { ngx_string("body_bytes_sent"), ngx_http_graphite_param_body_bytes_sent, ngx_http_graphite_param_value_avg },
    { ngx_string("request_length"), ngx_http_graphite_param_request_length, ngx_http_graphite_param_value_avg },
    { ngx_string("ssl_handshake_time"), ngx_http_graphite_param_ssl_handshake_time, ngx_http_graphite_param_value_avg },
    { ngx_string("content_time"), ngx_http_graphite_param_content_time, ngx_http_graphite_param_value_avg },
    { ngx_string("gzip_time"), ngx_http_graphite_param_gzip_time, ngx_http_graphite_param_value_avg },
    { ngx_string("upstream_time"), ngx_http_graphite_param_upstream_time, ngx_http_graphite_param_value_avg },
    { ngx_string("rps"), ngx_http_graphite_param_rps, ngx_http_graphite_param_value_persec },
};

static ngx_int_t ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data);
ngx_int_t ngx_http_graphite_handler(ngx_http_request_t *r);
static void ngx_http_graphite_timer_event_handler(ngx_event_t *ev);
void ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *lmcf, time_t ts);
static ngx_uint_t *ngx_http_graphite_get_params(ngx_http_request_t *r);

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

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL)
        return NGX_ERROR;

    *h = ngx_http_graphite_handler;

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_process_init(ngx_cycle_t *cycle) {

    ngx_http_graphite_main_conf_t *lmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_graphite_module);

    if (lmcf->enable) {

        ngx_memzero(&timer_event, sizeof(timer_event));
        timer_event.handler = ngx_http_graphite_timer_event_handler;
        timer_event.data = lmcf;
        timer_event.log = cycle->log;
        ngx_add_timer(&timer_event, lmcf->frequency);
    }

    return NGX_OK;
}

static void *
ngx_http_graphite_create_main_conf(ngx_conf_t *cf) {

    ngx_http_graphite_main_conf_t *lmcf;
    lmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_main_conf_t));

    if (lmcf == NULL)
        return NULL;

    lmcf->splits = ngx_array_create(cf->pool, 1, sizeof(ngx_str_t));
    lmcf->intervals = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_interval_t));
    lmcf->params = ngx_array_create(cf->pool, 1, sizeof(ngx_uint_t));

    return lmcf;
}

static void *
ngx_http_graphite_create_loc_conf(ngx_conf_t *cf) {

    ngx_http_graphite_loc_conf_t *llcf;

    llcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_loc_conf_t));
    if (llcf == NULL)
        return NULL;

    llcf->split = SPLIT_EMPTY;

    return llcf;
}

#if (NGX_SSL)
static ngx_int_t
ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data) {

    ngx_str_t s;

    if (r->connection->ssl && SSL_session_reused(r->connection->ssl->connection)) {
        ngx_str_set(&s, "yes");
    }
    else {
        ngx_str_set(&s, "no");
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
ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_graphite_main_conf_t *lmcf = conf;

    ngx_uint_t i;
    for (i = 1; i < cf->args->nelts; ++i) {
        ngx_str_t *var = &((ngx_str_t*)cf->args->elts)[i];

        ngx_uint_t find = 0;
        ngx_uint_t j;
        for (j = 0; j < ARGS_COUNT; ++j) {
            const ngx_http_graphite_arg_t *arg = &ngx_http_graphite_args[j];

            if (!ngx_strncmp(arg->name.data, var->data, arg->name.len) && var->data[arg->name.len] == '=') {
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

    if (lmcf->prefix.len == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite prefix not set");
        return NGX_CONF_ERROR;
    }

    if (lmcf->port < 1 || lmcf->port > 65535) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite port must be in range form 1 to 65535");
        return NGX_CONF_ERROR;
    }

    if (lmcf->frequency < 1 || lmcf->frequency > 65535) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite frequency must be in range form 1 to 65535");
        return NGX_CONF_ERROR;
    }

    if (lmcf->intervals->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite intervals not set");
        return NGX_CONF_ERROR;
    }

    if (lmcf->params->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite params not set");
        return NGX_CONF_ERROR;
    }

    if (lmcf->shared_size == 0 || lmcf->buffer_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite shared must be positive value");
        return NGX_CONF_ERROR;
    }

    if (lmcf->buffer_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite buffer must be positive value");
        return NGX_CONF_ERROR;
    }

    if (lmcf->package_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite package must be positive value");
        return NGX_CONF_ERROR;
    }

    lmcf->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (lmcf->socket < 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't create socket");
        return NGX_CONF_ERROR;
    }

    if (lmcf->shared_size < sizeof(ngx_slab_pool_t)) {
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

    lmcf->shared = ngx_shared_memory_add(cf, &graphite_shared_id, lmcf->shared_size, &ngx_http_graphite_module);
    if (!lmcf->shared) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc shared memory");
        return NGX_CONF_ERROR;
    }
    if (lmcf->shared->data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite shared memory is used");
        return NGX_CONF_ERROR;
    }
    lmcf->shared->init = ngx_http_graphite_shared_init;
    lmcf->shared->data = lmcf;

    lmcf->buffer = ngx_palloc(cf->pool, lmcf->buffer_size);
    if (!lmcf->buffer) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    char host[HOST_LEN];
    gethostname(host, HOST_LEN);
    host[HOST_LEN - 1] = '\0';
    char *dot = strchr(host, '.');
    if (dot)
        *dot = '\0';

    size_t host_size = strlen(host);

    lmcf->host.data = ngx_palloc(cf->pool, host_size);
    if (!lmcf->host.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(lmcf->host.data, host, host_size);
    lmcf->host.len = host_size;

    lmcf->enable = 1;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_main_conf_t *lmcf;
    ngx_http_graphite_loc_conf_t *llcf;

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);
    llcf = (ngx_http_graphite_loc_conf_t*)conf;;

    if (!lmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

    ngx_str_t *split = &((ngx_str_t*)cf->args->elts)[1];

    ngx_uint_t find = 0;
    ngx_uint_t i = 0;
    for (i = 0; i < lmcf->splits->nelts; ++i) {

        ngx_str_t *variant = &((ngx_str_t*)lmcf->splits->elts)[i];
        if (variant->len == split->len && !ngx_strncmp(variant->data, split->data, variant->len)) {
            find = 1;
            llcf->split = i;
            break;
        }
    }

    if (!find) {
        llcf->split = lmcf->splits->nelts;

        ngx_str_t *n = ngx_array_push(lmcf->splits);
        if (!n) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        n->data = ngx_palloc(cf->pool, split->len);
        if (!n->data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        ngx_memcpy(n->data, split->data, split->len);
        n->len = split->len;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_arg_prefix(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;
    lmcf->prefix.data = ngx_palloc(cf->pool, value->len);

    if (!lmcf->prefix.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(lmcf->prefix.data, value->data, value->len);
    lmcf->prefix.len = value->len;

    return NGX_CONF_OK;
}

#define SERVER_LEN 255

static char *
ngx_http_graphite_arg_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;

    char server[SERVER_LEN];
    if (value->len >= SERVER_LEN) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite server name too long");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy((u_char*)server, value->data, value->len);

    in_addr_t a = inet_addr(server);
    if (a != (in_addr_t)-1 || !a)
        lmcf->server.s_addr = a;
    else
    {
        struct hostent *host = gethostbyname(server);
        if (host != NULL) {
            lmcf->server = *(struct in_addr*)*host->h_addr_list;
        }
        else {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't resolve server name");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_arg_port(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;
    lmcf->port = ngx_atoi(value->data, value->len);

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_arg_frequency(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;
    lmcf->frequency = ngx_atoi(value->data, value->len) * 1000;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_arg_intervals(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;

    ngx_uint_t i;
    ngx_uint_t l = 0;
    ngx_uint_t b = 0;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; ++i) {

        if (i == value->len || value->data[i] == '|') {

            if (l == 0 || !b || i - s == 0) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite intervals is empty");
                return NGX_CONF_ERROR;
            }

            ngx_http_graphite_interval_t *interval = ngx_array_push(lmcf->intervals);
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
            interval->value = l;
            if (l > lmcf->max_interval)
                lmcf->max_interval = l;
            l = 0;
            b = 0;
            s = i + 1;
        }
        else if (!b && value->data[i] >= '0' && value->data[i] <= '9')
            l = l * 10 + (value->data[i] - '0');
        else if (!b && value->data[i] == 'm') {
            l *= 60;
            b = 1;
        }
        else if (!b && value->data[i] == 's')
            b = 1;
        else
            return NGX_CONF_ERROR;
    }

    if (l || b) {
           ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite intervals bad value");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_arg_params(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;

    ngx_uint_t i = 0;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; ++i) {
        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite params is empty");
                return NGX_CONF_ERROR;
            }

            ngx_uint_t *param = ngx_array_push(lmcf->params);
            if (!param) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            ngx_uint_t find = 0;
            ngx_uint_t p;
            for (p = 0; p < PARAM_COUNT; ++p) {
                if (!ngx_strncmp(ngx_http_graphite_params[p].name.data, &value->data[s], i - s)) {
                    find = 1;
                    *param = p;
                }
            }

            if (!find) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknow param %*s", i - s, &value->data[s]);
                return NGX_CONF_ERROR;
            }

            s = i + 1;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_arg_shared(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;
    return ngx_http_graphite_parse_size(value, &lmcf->shared_size);
}

static char *
ngx_http_graphite_arg_buffer(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;
    return ngx_http_graphite_parse_size(value, &lmcf->buffer_size);
}

static char *
ngx_http_graphite_arg_package(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *lmcf = conf;
    return ngx_http_graphite_parse_size(value, &lmcf->package_size);
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

static ngx_int_t
ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_graphite_main_conf_t *lmcf = shm_zone->data;

    if (data) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory data set");
        return NGX_ERROR;
    }

    ngx_uint_t i;

    size_t shared_required_size = 0;
    shared_required_size += sizeof(ngx_http_graphite_data_t);
    shared_required_size += sizeof(ngx_http_graphite_record_t*) * lmcf->intervals->nelts;
    shared_required_size += sizeof(ngx_http_graphite_record_t) * lmcf->max_interval * lmcf->splits->nelts * lmcf->params->nelts;
    shared_required_size += sizeof(ngx_http_graphite_acc_t) * lmcf->intervals->nelts * lmcf->splits->nelts * lmcf->params->nelts;
    shared_required_size += sizeof(time_t*) * lmcf->intervals->nelts;

    if (sizeof(ngx_slab_pool_t) + shared_required_size > shm_zone->shm.size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small shared memory (minimum size is %uzb)", sizeof(ngx_slab_pool_t) + shared_required_size);
        return NGX_ERROR;
    }

    // 128 is the approximate size of the one udp record
    size_t buffer_required_size = lmcf->splits->nelts * (lmcf->intervals->nelts * lmcf->params->nelts + 1) * 128;
    if (buffer_required_size > lmcf->buffer_size) {
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

    memset(p, 0, shared_required_size);

    ngx_http_graphite_data_t *d = (ngx_http_graphite_data_t*)p;
    p += sizeof(ngx_http_graphite_data_t);

    d->records = (ngx_http_graphite_record_t*)p;
    p += sizeof(ngx_http_graphite_record_t) * lmcf->max_interval * lmcf->splits->nelts * lmcf->params->nelts;

    d->accs = (ngx_http_graphite_acc_t*)p;
    p += sizeof(ngx_http_graphite_acc_t) * lmcf->intervals->nelts * lmcf->splits->nelts * lmcf->params->nelts;

    d->last_time = (time_t*)p;
    p += sizeof(time_t*) * lmcf->intervals->nelts;

    time_t ts = ngx_time();
    d->start_time = ts;

    for (i = 0; i < lmcf->intervals->nelts; ++i)
        d->last_time[i] = ts;

    d->event_time = ts;

    return NGX_OK;
}

ngx_int_t
ngx_http_graphite_handler(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *lmcf;
    ngx_http_graphite_loc_conf_t *llcf;

    lmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);
    llcf = ngx_http_get_module_loc_conf(r, ngx_http_graphite_module);

    if (!lmcf->enable)
        return NGX_OK;

    if (llcf->split == SPLIT_EMPTY)
        return NGX_OK;

    ngx_str_t *split = &(((ngx_str_t*)lmcf->splits->elts)[llcf->split]);

    ngx_uint_t *params = ngx_http_graphite_get_params(r);
    if (params == NULL) {

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite can't get params");
        return NGX_OK;
    }

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)lmcf->shared->shm.addr;
    ngx_http_graphite_data_t *d = (ngx_http_graphite_data_t*)shpool->data;

    time_t ts = ngx_time();

    ngx_shmtx_lock(&shpool->mutex);

    ngx_http_graphite_del_old_records(lmcf, ts);

    ngx_uint_t i, p;
    for (p = 0; p < lmcf->params->nelts; ++p) {

        ngx_uint_t m = ((ts - d->start_time) % lmcf->max_interval) * lmcf->splits->nelts * lmcf->params->nelts + llcf->split * lmcf->params->nelts + p;
        ngx_http_graphite_record_t *record = &d->records[m];

        record->acc.value += params[p];
        record->acc.count++;

        if (record->acc.value < params[p]) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite accamulator overfull");
            record->acc.value = 0;
            record->acc.count = 0;
        }

        for (i = 0; i < lmcf->intervals->nelts; ++i) {

            ngx_http_graphite_acc_t *a = &d->accs[i * (lmcf->splits->nelts * lmcf->params->nelts) + llcf->split * lmcf->params->nelts + p];
            a->value += params[p];
            a->count++;

            if (a->value < params[p]) {
                ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite accamulator overfull");
                a->value = 0;
                a->count = 0;
            }
        }
    }

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

static void
ngx_http_graphite_timer_event_handler(ngx_event_t *ev) {

    time_t ts = ngx_time();

    ngx_http_graphite_main_conf_t *lmcf;
    lmcf = ev->data;

    char *buffer = lmcf->buffer;
    char *b = buffer;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)lmcf->shared->shm.addr;
    ngx_http_graphite_data_t *d = (ngx_http_graphite_data_t*)shpool->data;

    ngx_shmtx_lock(&shpool->mutex);

    if ((ngx_uint_t)(ts - d->event_time) * 1000 < lmcf->frequency) {
        ngx_shmtx_unlock(&shpool->mutex);
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, lmcf->frequency);
        return;
    }

    d->event_time = ts;

    ngx_http_graphite_del_old_records(lmcf, ts);

    ngx_uint_t i, s, p;
    for (i = 0; i < lmcf->intervals->nelts; ++i) {
        ngx_http_graphite_interval_t *interval = &((ngx_http_graphite_interval_t*)lmcf->intervals->elts)[i];

         for (s = 0; s < lmcf->splits->nelts; ++s) {
             ngx_str_t *split = &(((ngx_str_t*)lmcf->splits->elts)[s]);

             for (p = 0; p < lmcf->params->nelts; ++p) {
                 const ngx_http_graphite_param_t *param = &(ngx_http_graphite_params[((ngx_uint_t*)lmcf->params->elts)[p]]);

                 ngx_http_graphite_acc_t *a = &d->accs[i * (lmcf->splits->nelts * lmcf->params->nelts) + s * lmcf->params->nelts + p];
                 double value = param->value(interval, a);
                 b = (char*)ngx_snprintf((u_char*)b, lmcf->buffer_size - (b - buffer), "%V.%V.%V_%V_%V %.3f %T\n", &lmcf->prefix, &lmcf->host, split, &interval->name, &param->name, value, ts);
             }
         }
    }

    ngx_shmtx_unlock(&shpool->mutex);

    if (b == buffer + lmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite buffer size is too small");
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, lmcf->frequency);
        return;
    }
    *b = '\0';

    if (b != buffer) {
        struct sockaddr_in sin;
        memset((char *) &sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr = lmcf->server;
        sin.sin_port = htons(lmcf->port);

        char *part = buffer;
        char *next = NULL;
        char *nl = NULL;

        while (*part) {
            next = part;
            nl = part;

            while ((next = strchr(next, '\n')) && ((size_t)(next - part) <= lmcf->package_size)) {
                nl = next;
                next++;
            }

            if (nl > part) {

                if (sendto(lmcf->socket, part, nl - part + 1, 0, &sin, sizeof(sin)) == -1)
                    ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite can't send udp packet");
            }
            else {
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite package size too small, need send %z", (size_t)(next - part));
            }

            part = nl + 1;
        }
    }

    if (!(ngx_quit || ngx_terminate || ngx_exiting))
        ngx_add_timer(ev, lmcf->frequency);
}

void
ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *lmcf, time_t ts) {

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)lmcf->shared->shm.addr;
    ngx_http_graphite_data_t *d = (ngx_http_graphite_data_t*)shpool->data;

    ngx_uint_t i, s, p;
    for (i = 0; i < lmcf->intervals->nelts; ++i) {

        ngx_http_graphite_interval_t *interval = &((ngx_http_graphite_interval_t*)lmcf->intervals->elts)[i];
        while ((ngx_uint_t)d->last_time[i] + interval->value <= (ngx_uint_t)ts) {

            for (s = 0; s < lmcf->splits->nelts; ++s) {

                for (p = 0; p < lmcf->params->nelts; ++p) {
                    ngx_uint_t m = ((d->last_time[i] - d->start_time) % lmcf->max_interval) * lmcf->splits->nelts * lmcf->params->nelts + s * lmcf->params->nelts + p;
                    ngx_http_graphite_record_t *record = &d->records[m];
                    ngx_http_graphite_acc_t *a = &d->accs[i * (lmcf->splits->nelts * lmcf->params->nelts) + s * lmcf->params->nelts + p];

                    a->value -= record->acc.value;
                    a->count -= record->acc.count;

                    if (interval->value == lmcf->max_interval) {
                        record->acc.value = 0;
                        record->acc.count = 0;
                    }
                }
            }
            d->last_time[i]++;
        }
    }
}

static ngx_uint_t *
ngx_http_graphite_get_params(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *lmcf;
    lmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    ngx_uint_t *params = ngx_palloc(r->pool, sizeof(ngx_uint_t) * lmcf->params->nelts);
    if (!params)
        return NULL;

    ngx_uint_t i;
    for (i = 0; i < lmcf->params->nelts; ++i) {

        ngx_uint_t p = ((ngx_uint_t*)lmcf->params->elts)[i];
        params[i] = ngx_http_graphite_params[p].get(r);
    }

    return params;
}

static ngx_uint_t
ngx_http_graphite_param_request_time(ngx_http_request_t *r) {

    ngx_time_t      *tp;
    ngx_msec_int_t   ms;

    tp = ngx_timeofday();
    ms = (ngx_msec_int_t) ((tp->sec - r->start_sec) * 1000 + (tp->msec - r->start_msec));
    ms = ngx_max(ms, 0);

    return (ngx_uint_t)ms;
}

static ngx_uint_t
ngx_http_graphite_param_bytes_sent(ngx_http_request_t *r) {

    return (ngx_uint_t)r->connection->sent;
}

static ngx_uint_t
ngx_http_graphite_param_body_bytes_sent(ngx_http_request_t *r) {

    off_t  length;

    length = r->connection->sent - r->header_size;
    length = ngx_max(length, 0);

    return (ngx_uint_t)length;
}

static ngx_uint_t
ngx_http_graphite_param_request_length(ngx_http_request_t *r) {

    return (ngx_uint_t)r->request_length;
}

static ngx_uint_t
ngx_http_graphite_param_ssl_handshake_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (NGX_SSL)
    ngx_ssl_connection_t *ssl = r->connection->ssl;
    if (ssl)
        ms = (ngx_msec_int_t)((ssl->handshake_end_sec - ssl->handshake_start_sec) * 1000 + (ssl->handshake_end_msec - ssl->handshake_start_msec));
#endif
    ms = ngx_max(ms, 0);

    return (ngx_uint_t)ms;
}

static ngx_uint_t
ngx_http_graphite_param_content_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = (ngx_msec_int_t)((r->content_end_sec - r->content_start_sec) * 1000 + (r->content_end_msec - r->content_start_msec));
    ms = ngx_max(ms, 0);

    return (ngx_uint_t)ms;
}

static ngx_uint_t
ngx_http_graphite_param_gzip_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (NGX_HTTP_GZIP)
    ms = (ngx_msec_int_t)((r->gzip_end_sec - r->gzip_start_sec) * 1000 + (r->gzip_end_msec - r->gzip_start_msec));
#endif
    ms = ngx_max(ms, 0);

    return (ngx_uint_t)ms;
}

static ngx_uint_t
ngx_http_graphite_param_upstream_time(ngx_http_request_t *r) {

    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; ++i) {
        if (state[i].status)
            ms += (ngx_msec_int_t)(state[i].response_sec * 1000 + state[i].response_msec);
    }
    ms = ngx_max(ms, 0);

    return ms;
}

static ngx_uint_t
ngx_http_graphite_param_rps(ngx_http_request_t *r) {

    return 1;
}

static double
ngx_http_graphite_param_value_avg(ngx_http_graphite_interval_t *interval, ngx_http_graphite_acc_t *acc) {

    return (acc->count != 0) ? (double)acc->value / acc->count : 0;
}

static double
ngx_http_graphite_param_value_persec(ngx_http_graphite_interval_t *interval, ngx_http_graphite_acc_t *acc) {

    return (double)acc->count / interval->value;
}
