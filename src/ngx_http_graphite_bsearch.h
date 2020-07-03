#ifndef _NGX_HTTP_GRAPHITE_BSEARCH_H_INCLUDED_
#define _NGX_HTTP_GRAPHITE_BSEARCH_H_INCLUDED_

#include <stddef.h>

static inline void *
ngx_graphite_bsearch(
    const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *), int *found) {

    size_t l = 0, r = nmemb;

    while (l < r) {
        size_t m = l + (r - l) / 2;

        int c = compar(key, base + m * size);
        if (c < 0)
            r = m;
        else if (c > 0)
            l = m + 1;
        else {
            *found = 1;
            return (void*)((char*)base + m * size);
        }
    }

    *found = 0;
    return (void*)((char*)base + l * size);
}

#endif
