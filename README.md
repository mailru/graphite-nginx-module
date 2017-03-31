graphite-nginx-module
=====================

An nginx module for collecting location stats into Graphite.

*This module is not distributed with the Nginx source.* See [the installation instructions](#installation).

Features
========

* Aggregation of location, server or http metrics
* Calculation of percentiles
* Sending data to Grahpite over UDP or TCP in non-blocking way
* Sending custom metrics from lua

Version
=======

This document describes graphite-nginx-module [v1.3.0](https://github.com/mailru/graphite-nginx-module/tags) released on 31 March 2017.

Synopsis
========

```nginx

http {
    graphite_config prefix=playground server=127.0.0.1;
    server {
        location /foo/ {
            graphite_data nginx.foo;
        }
    }
}
```

Description
===========

This module use shared memory segment to collect aggregated stats from all workers and send calculated values for last minute to Graphite every 60s (default) over UDP or TCP in non-blocking way.
Stats aggegation made on the fly in fixed size buffer allocated on server start and does't affect server perfomance.

This module is in active use on [Mail.Ru Sites](http://mail.ru/) (one of largest web-services in Russia) for about a year and considered stable and well-tested.

To collect metrics from nginx core modules (ssl, gzip, upstream) little patch must be applied on nginx source tree. See [the installation instructions](#installation).
You can build this module as a dynamic one, but then you won't be able to collect metrics from nginx core modules (ssl, gzip, upstream) and lua functions.


Directives
==========

### graphite_config

**syntax:** *graphite_config key1=&lt;value1&gt; key2=&lt;value2&gt; ... keyN=&lt;valueN&gt;*

**context:** *http*

Specify global settings for a whole server instance.

Param     | Required | Default       | Description
--------- | -------- | ------------- | -----------
prefix    |          |               | path prefix for all graphs
host      |          | gethostname() | host name for all graphs
server    | Yes      |               | carbon-cache server IP address
protocol  |          | udp           | carbon-cache server protocol (udp or tcp)
port      |          | 2003          | carbon-cache server port
frequency |          | 60            | how often send values to Graphite (seconds)
intervals |          | 1m            | aggregation intervals, time interval list, vertical bar separator (`m` - minutes)
params    |          | *             | limit metrics list to track, vertical bar separator
shared    |          | 2m            | shared memory size, increase in case of `too small shared memory` error
buffer    |          | 64k           | network buffer size, increase in case of `too small buffer size` error
package   |          | 1400          | maximum UDP packet size
template  |          |               | template for graph name (default is $prefix.$host.$split.$param_$interval) 

Example (standard):

```nginx
http {
    graphite_config prefix=playground server=127.0.0.1;
}
```

Example (custom):

```nginx
http {
    graphite_config prefix=playground server=127.0.0.1 intervals=1m|5m|15m params=rps|request_time|upstream_time template=$prefix.$host.$split.$param_$interval;
}
```

### graphite_default_data

**syntax:** *graphite_default_data &lt;path prefix&gt; [params=&lt;params&gt;] [if=&lt;condition&gt;]*

**context:** *http, server*

Create measurement point in all nested locations.
You can use "$location" variable which represents the name of the current location with all non-alphanumeric characters replaced with "\_." Leading and trailing "\_" are deleted.

Example:

```nginx

   graphite_default_data nginx.$location;

   location /foo/ {
   }

   location /bar/ {
   }
```

Data for `/foo/` will be sent to `nginx.foo`, data for `/bar/` - to `nginx.bar`.
The `<params>` parameter (1.3.0) specifies list of params to be collected for all nested locations. To add all default params, use \*.
The `<if>` parameter (1.1.0) enables conditional logging. A request will not be logged if the condition evaluates to "0" or an empty string.

### graphite_data

**syntax:** *graphite_data &lt;path prefix&gt; [params=&lt;params&gt;] [if=&lt;condition&gt;]*

**context:** *http, server, location, if*

Create measurement point in specific location.

Example:

```nginx

    location /foo/ {
        graphite_data nginx.foo;
    }
```

The `<params>` parameter (1.3.0) specifies list of params to be collected for this location. To add all default params, use \*.
The `<if>` parameter (1.1.0) enables conditional logging. A request will not be logged if the condition evaluates to "0" or an empty string.

Example:

```nginx

    map $scheme $is_http { http 1; }
    map $scheme $is_https { https 1; }

	...

    location /bar/ {
        graphite_data nginx.all.bar;
        graphite_data nginx.http.bar if=$is_http;
        graphite_data nginx.https.bar if=$is_https;
        graphite_data nginx.arg params=rps|request_time;
        graphite_data nginx.ext params=*|rps|request_time;
    }
```

### graphite_param

**syntax:** *graphite_param name=&lt;path&gt; interval=&lt;time value&gt; aggregate=&lt;func&gt;*

**context:** *location*

Param     | Required | Description
--------- | -------- | -----------
name      | Yes      | path prefix for all graphs
interval  | Yes      | aggregation interval, time intrval value format (`m` - minutes)
aggregate | Yes      | aggregation function on values

#### aggregate functions
func   | Description
------ | -----------
sum    | sum of values per interval
persec | sum of values per second  (`sum` devided on seconds in `interval`)
avg    | average value on interval

Example: see below.

### graphite_param_hash_bucket_size
**syntax:** *graphite_param_hash_max_size size;*

**default:** *graphite_param_hash_max_size 64;*

**context:** *http*

Sets the bucket size for the params hash table.

### graphite_param_hash_max_size
**syntax:** *graphite_param_hash_max_size size;*

**default:** *graphite_param_hash_max_size 512;*

**context:** *http*

Sets the maximum size of the params hash tables.

Nginx API for Lua
=================

**syntax:** *ngx.graphite(&lt;name&gt;,&lt;value&gt;)*

Write stat value into aggregator function. Floating point numbers accepted in `value`.

*Available after applying patch to lua-nginx-module.* See [the installation instructions](#build-nginx-with-lua-and-graphite-modules).

```lua
ngx.graphite(name, value)
```

Example:

```nginx

location /foo/ {
    graphite_param name=lua.foo_sum aggregate=sum interval=1m;
    graphite_param name=lua.foo_rps aggregate=persec interval=1m;
    graphite_param name=lua.foo_avg aggregate=avg interval=1m;

    content_by_lua '
        ngx.graphite("lua.foo_sum", 0.01)
        ngx.graphite("lua.foo_rps", 1)
        ngx.graphite("lua.foo_avg", ngx.var.request_uri:len())
        ngx.say("hello")
    ';
}
```

Params
======

Param                   | Units | Func | Description
----------------------- | ----- | ---- | ------------------------------------------
request\_time           | ms    | avg  | total time spent on serving request
bytes\_sent             | bytes | avg  | http response length
body\_bytes\_sent       | bytes | avg  | http response body length
request\_length         | bytes | avg  | http request length
ssl\_handshake\_time    | ms    | avg  | time spent on ssl handsake
ssl\_cache\_usage       | %     | last | how much SSL cache used
content\_time           | ms    | avg  | time spent generating content inside nginx
gzip\_time              | ms    | avg  | time spent gzipping content ob-the-fly
upstream\_time          | ms    | avg  | time spent tailking with upstream
upstream\_connect\_time | ms    | avg  | time spent on upstream connect (nginx >= 1.9.1)
upstream\_header\_time  | ms    | avg  | time spent on upstream header (nginx >= 1.9.1)
rps                     | rps   | sum  | total requests number per aggregation interval
keepalive\_rps          | rps   | sum  | requests number sent over previously opened keepalive connection
response\_2xx\_rps      | rps   | sum  | total responses number with 2xx code
response\_3xx\_rps      | rps   | sum  | total responses number with 3xx code
response\_4xx\_rps      | rps   | sum  | total responses number with 4xx code
response\_5xx\_rps      | rps   | sum  | total responses number with 5xx code

Percentiles
===========

To calculate percentile value for any parameter, set percentile level via `/`. E.g. `request_time/50|request_time/90|request_time/99`.

Installation
============

#### Requirements
* nginx: 1.2.0 - 1.10.x
* lua-nginx-module: 0.8.6 - 0.10.2 (optional)

#### Build nginx with graphite module
```bash

wget 'http://nginx.org/download/nginx-1.9.2.tar.gz'
tar -xzf nginx-1.9.2.tar.gz
cd nginx-1.9.2/

# patch to collect ssl_cache_usage, ssl_handshake_time content_time, gzip_time, upstream_time, upstream_connect_time, upstream_header_time graphs (optional)
patch -p1 < /path/to/graphite-nginx-module/graphite_module_v1_7_7.patch

./configure --add-module=/path/to/graphite-nginx-module

make
make install
```

#### Build nginx with graphite dynamic module
```bash

wget 'http://nginx.org/download/nginx-1.9.2.tar.gz'
tar -xzf nginx-1.9.2.tar.gz
cd nginx-1.9.2/

./configure --add-dynamic-module=/path/to/graphite-nginx-module

make
make install
```

#### Build nginx with lua and graphite modules
```bash

wget 'https://github.com/chaoslawful/lua-nginx-module/archive/v0.9.16.tar.gz'
tar -xzf v0.9.16.tar.gz
cd lua-nginx-module-0.9.16/
# patch to add api for sending metrics from lua code (optional)
patch -p1 < /path/to/graphite-nginx-module/lua_module_v0_9_11.patch
cd ..

wget 'http://nginx.org/download/nginx-1.9.2.tar.gz'
tar -xzf nginx-1.9.2.tar.gz
cd nginx-1.9.2/

# patch to collect ssl_cache_usage, ssl_handshake_time content_time, gzip_time, upstream_time, upstream_connect_time, upstream_header_time graphs (optional)
patch -p1 < /path/to/graphite-nginx-module/graphite_module_v1_7_7.patch

./configure \
    --add-module=/path/to/ngx_devel_kit \
    --add-module=/path/to/lua-nginx-module \
    --add-module=/path/to/graphite-nginx-module

make
make install
```

Instructions on installing lua-nginx-module can be found in [documentation on lua-nginx-module](https://github.com/chaoslawful/lua-nginx-module#installation).

License
=======

Copyright (c) 2013-2017, Mail.Ru Ltd.

This module is licensed under the terms of the BSD license.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
