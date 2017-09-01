#ifndef _NGX_HTTP_GRAPHITE_ALLOCATOR_H_INCLUDED_
#define _NGX_HTTP_GRAPHITE_ALLOCATOR_H_INCLUDED_

#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct ngx_http_graphite_allocator_s {
    void *pool;
    void *(*alloc)(void *pool, size_t size);
    void (*free)(void *pool, void *p);
    ngx_int_t nomemory;
} ngx_http_graphite_allocator_t;

void ngx_http_graphite_allocator_init(ngx_http_graphite_allocator_t *allocator, void *pool, void* (*alloc)(void *, size_t), void (*free)(void *, void *));
void *ngx_http_graphite_allocator_pool_alloc(void *pool, size_t size);
void ngx_http_graphite_allocator_pool_free(void *pool, void *p);
void *ngx_http_graphite_allocator_slab_alloc(void *pool, size_t size);
void ngx_http_graphite_allocator_slab_free(void *pool, void *p);

void *ngx_http_graphite_allocator_alloc(ngx_http_graphite_allocator_t *allocator, size_t size);
void ngx_http_graphite_allocator_free(ngx_http_graphite_allocator_t *allocator, void *p);

#endif
