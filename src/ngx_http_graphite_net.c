#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_graphite_module.h"

static ngx_int_t ngx_http_graphite_net_send_tcp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log);
static ngx_int_t ngx_http_graphite_net_send_udp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log);
static ngx_int_t ngx_http_graphite_net_connect_tcp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log);
static ngx_int_t ngx_http_graphite_net_connect_udp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log);
static void ngx_http_graphite_tcp_read(ngx_event_t *rev);
static void ngx_http_graphite_tcp_write(ngx_event_t *wev);

ngx_int_t
ngx_http_graphite_net_send_buffer(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log) {

    ngx_int_t rc = NGX_ERROR;

    if (ngx_strncmp(gmcf->protocol.data, "tcp", 3) == 0)
        rc = ngx_http_graphite_net_send_tcp(gmcf, log);
    else if (ngx_strncmp(gmcf->protocol.data, "udp", 3) == 0)
        rc = ngx_http_graphite_net_send_udp(gmcf, log);

    return rc;
}

static ngx_int_t
ngx_http_graphite_net_send_tcp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log)
{

    if (gmcf->connection)
        return NGX_ERROR;

    ngx_int_t rc = ngx_http_graphite_net_connect_tcp(gmcf, log);
    if (rc == NGX_ERROR)
        return NGX_ERROR;

    gmcf->connection->data = gmcf;
    gmcf->connection->read->handler = ngx_http_graphite_tcp_read;
    gmcf->connection->write->handler = ngx_http_graphite_tcp_write;

    ngx_add_timer(gmcf->connection->write, (ngx_msec_t)(gmcf->timeout));

    if (rc == NGX_OK)
        ngx_http_graphite_tcp_write(gmcf->connection->write);

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_net_send_udp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log)
{

    if (gmcf->connection)
        return NGX_ERROR;

    if (ngx_http_graphite_net_connect_udp(gmcf, log) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, log, 0, "graphite connect to %V failed", &gmcf->server.name);
        goto failed;
    }

    gmcf->connection->data = gmcf;

    ngx_buf_t *b = &gmcf->buffer;

    u_char *part = b->start;
    u_char *next = NULL;
    u_char *nl = NULL;

    while (*part) {
        next = part;
        nl = part;

        while ((next = (u_char*)ngx_strchr(next, '\n')) && ((size_t)(next - part) <= gmcf->package_size)) {
            nl = next;
            next++;
        }
        if (nl > part) {
            ssize_t n = ngx_send(gmcf->connection, part, nl - part + 1);

            if (n == -1) {
                ngx_log_error(NGX_LOG_ERR, log, n, "graphite udp send error");
                goto failed;
            }

            if (n != nl - part + 1) {
                ngx_log_error(NGX_LOG_ERR, log, 0, "graphite udp send incomplete");
                goto failed;
            }
        }
        else {
            ngx_log_error(NGX_LOG_ERR, log, 0, "graphite package size too small, need send %z", (size_t)(next - part));
        }

        part = nl + 1;
    }

    ngx_close_connection(gmcf->connection);
    gmcf->connection = NULL;

    return NGX_OK;

failed:

    ngx_close_connection(gmcf->connection);
    gmcf->connection = NULL;

    return NGX_ERROR;
}

static ngx_int_t
ngx_http_graphite_net_connect_tcp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log)
{
    ngx_socket_t s = ngx_socket(gmcf->server.sockaddr->sa_family, SOCK_STREAM, 0);

    if (s == (ngx_socket_t) -1) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, ngx_socket_n " failed");
        return NGX_ERROR;
    }

    ngx_connection_t *c = ngx_get_connection(s, log);

    if (c == NULL) {
        if (ngx_close_socket(s) == -1)
            ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, ngx_close_socket_n "failed");

        return NGX_ERROR;
    }

    if (ngx_nonblocking(s) == -1) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, ngx_nonblocking_n " failed");

        goto failed;
    }

    ngx_event_t *rev = c->read;
    ngx_event_t *wev = c->write;

    rev->log = log;
    wev->log = log;

    gmcf->connection = c;

    c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

    if (ngx_add_conn) {
        if (ngx_add_conn(c) == NGX_ERROR)
            goto failed;
    }

    int rc = connect(s, gmcf->server.sockaddr, gmcf->server.socklen);

    if (rc == -1) {
        ngx_err_t err = ngx_socket_errno;

        if (err != NGX_EINPROGRESS
#if (NGX_WIN32)
            && err != NGX_EAGAIN
#endif
            )
        {
            ngx_log_error(NGX_LOG_ERR, c->log, err, "graphite connect to %V failed", &gmcf->server.name);

            goto failed;
        }
    }

    if (ngx_add_conn) {
        if (rc == -1)
            return NGX_AGAIN;

        wev->ready = 1;
        return NGX_OK;
    }

    if (ngx_event_flags & NGX_USE_IOCP_EVENT) {

        if (ngx_blocking(s) == -1) {
            ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, ngx_blocking_n " failed");
            goto failed;
        }

        rev->ready = 1;
        wev->ready = 1;

        return NGX_OK;
    }

    ngx_int_t event = (ngx_event_flags & NGX_USE_CLEAR_EVENT) ? NGX_CLEAR_EVENT: NGX_LEVEL_EVENT;

    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK) {
        goto failed;
    }

    if (rc == -1) {
        if (ngx_add_event(wev, NGX_WRITE_EVENT, event) != NGX_OK)
            goto failed;
        return NGX_AGAIN;
    }

    wev->ready = 1;

    return NGX_OK;

failed:

    ngx_close_connection(c);
    gmcf->connection = NULL;

    return NGX_ERROR;
}

static ngx_int_t
ngx_http_graphite_net_connect_udp(ngx_http_graphite_main_conf_t *gmcf, ngx_log_t *log)
{

    ngx_socket_t s = ngx_socket(gmcf->server.sockaddr->sa_family, SOCK_DGRAM, 0);

    if (s == (ngx_socket_t) -1) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, ngx_socket_n " failed");
        return NGX_ERROR;
    }

    ngx_connection_t *c = ngx_get_connection(s, log);

    if (c == NULL) {
        if (ngx_close_socket(s) == -1) {
            ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, ngx_close_socket_n " failed");
        }

        return NGX_ERROR;
    }

    ngx_event_t *rev = c->read;
    ngx_event_t *wev = c->write;

    rev->log = log;
    wev->log = log;

    gmcf->connection = c;

    c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

    int rc = connect(s, gmcf->server.sockaddr, gmcf->server.socklen);

    if (rc == -1) {
        ngx_log_error(NGX_LOG_ERR, log, ngx_socket_errno, "connect failed");
        goto failed;
    }

    wev->ready = 1;

    ngx_int_t event = (ngx_event_flags & NGX_USE_CLEAR_EVENT) ? NGX_CLEAR_EVENT: NGX_LEVEL_EVENT;

    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK)
        goto failed;

    return NGX_OK;

failed:

    ngx_close_connection(c);

    return NGX_ERROR;
}

static void
ngx_http_graphite_tcp_read(ngx_event_t *rev)
{
}

static void
ngx_http_graphite_tcp_write(ngx_event_t *wev)
{
    ngx_connection_t *c = wev->data;
    ngx_http_graphite_main_conf_t *gmcf = (ngx_http_graphite_main_conf_t*)(c->data);
    ngx_buf_t *b = &gmcf->buffer;

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, 0, "graphite tcp connection timeout");
        goto failed;
    }

    off_t sent = c->sent;

    while (wev->ready && b->pos < b->last) {
        ssize_t n = ngx_send(c, b->pos, b->last - b->pos);

        if (n == NGX_AGAIN)
            break;

        if (n == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, wev->log, 0, "graphite tcp send error");
            goto failed;
        }

        b->pos += n;
    }

    if (b->pos != b->start) {
        b->last = ngx_movemem(b->start, b->pos, b->last - b->pos);
        b->pos = b->start;
    }

    if (b->last - b->pos == 0) {
        ngx_close_connection(c);
        gmcf->connection = NULL;
        return;
    }

    if (c->sent != sent)
        ngx_add_timer(wev, (ngx_msec_t)(gmcf->timeout));

    if (ngx_handle_write_event(wev, 0) != NGX_OK)
        goto failed;

    return;

failed:
    ngx_close_connection(c);
    gmcf->connection = NULL;
}
