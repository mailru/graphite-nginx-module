#ifndef _NGX_HTTP_GRAPHITE_ARRAY_H_INCLUDED_
#define _NGX_HTTP_GRAPHITE_ARRAY_H_INCLUDED_

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_graphite_allocator.h"

typedef struct ngx_http_graphite_array_s {
    void *elts;
    ngx_uint_t nelts;
    size_t size;
    ngx_uint_t nalloc;
    ngx_http_graphite_allocator_t *allocator;
} ngx_http_graphite_array_t;

ngx_http_graphite_array_t *ngx_http_graphite_array_create(ngx_http_graphite_allocator_t *allocator, ngx_uint_t n, size_t size);
ngx_int_t ngx_http_graphite_array_init(ngx_http_graphite_array_t *array, ngx_http_graphite_allocator_t *allocator, ngx_uint_t n, size_t size);
void ngx_http_graphite_array_destroy(ngx_http_graphite_array_t *array);
void *ngx_http_graphite_array_push(ngx_http_graphite_array_t *a);
void *ngx_http_graphite_array_push_n(ngx_http_graphite_array_t *a, ngx_uint_t n);
ngx_http_graphite_array_t *ngx_http_graphite_array_copy(ngx_http_graphite_allocator_t *allocator, ngx_http_graphite_array_t *array);

#endif
