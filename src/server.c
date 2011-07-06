/*
    server.c -- Create Http servers.
    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************* Includes ***********************************/

#include    "http.h"

/********************************** Forwards **********************************/

static int manageServer(HttpServer *server, int flags);
static int destroyServerConnections(HttpServer *server);

/************************************ Code ************************************/
/*
    Create a server listening on ip:port. NOTE: ip may be empty which means bind to all addresses.
 */
HttpServer *httpCreateServer(cchar *ip, int port, MprDispatcher *dispatcher, int flags)
{
    HttpServer  *server;
    Http        *http;
    HttpHost    *host;

    if ((server = mprAllocObj(HttpServer, manageServer)) == 0) {
        return 0;
    }
    http = MPR->httpService;
    server->http = http;
    server->clientLoad = mprCreateHash(HTTP_CLIENTS_HASH, MPR_HASH_STATIC_VALUES);
    server->async = 1;
    server->http = MPR->httpService;
    server->port = port;
    server->ip = sclone(ip);
    server->dispatcher = dispatcher;
    server->loc = httpInitLocation(http, 1);
    server->hosts = mprCreateList(-1, 0);
    httpAddServer(http, server);

    if (flags & HTTP_CREATE_HOST) {
        host = httpCreateHost(server->loc);
        httpSetHostName(host, ip, port);
        httpAddHostToServer(server, host);
    }
    return server;
}


void httpDestroyServer(HttpServer *server)
{
    mprLog(4, "Destroy server %s", server->ip);
    if (server->waitHandler) {
        mprRemoveWaitHandler(server->waitHandler);
        server->waitHandler = 0;
    }
    destroyServerConnections(server);
    if (server->sock) {
        mprCloseSocket(server->sock, 0);
        server->sock = 0;
    }
    httpRemoveServer(MPR->httpService, server);
}


static int manageServer(HttpServer *server, int flags)
{
    if (flags & MPR_MANAGE_MARK) {
        mprMark(server->http);
        mprMark(server->loc);
        mprMark(server->limits);
        mprMark(server->waitHandler);
        mprMark(server->clientLoad);
        mprMark(server->hosts);
        mprMark(server->ip);
        mprMark(server->sock);
        mprMark(server->dispatcher);
        mprMark(server->ssl);

    } else if (flags & MPR_MANAGE_FREE) {
        httpDestroyServer(server);
    }
    return 0;
}


/*  
    Convenience function to create and configure a new server without using a config file.
 */
HttpServer *httpCreateConfiguredServer(cchar *docRoot, cchar *ip, int port)
{
    Http            *http;
    HttpHost        *host;
    HttpServer      *server;

    http = MPR->httpService;

    if (ip == 0) {
        /*  
            If no IP:PORT specified, find the first server
         */
        if ((server = mprGetFirstItem(http->servers)) != 0) {
            ip = server->ip;
            port = server->port;
        } else {
            ip = "localhost";
            if (port <= 0) {
                port = HTTP_DEFAULT_PORT;
            }
            server = httpCreateServer(ip, port, NULL, HTTP_CREATE_HOST);
        }
    } else {
        server = httpCreateServer(ip, port, NULL, HTTP_CREATE_HOST);
    }
    host = mprGetFirstItem(server->hosts);
    if ((host->mimeTypes = mprCreateMimeTypes("mime.types")) == 0) {
        host->mimeTypes = MPR->mimeTypes;
    }
    httpAddDir(host, httpCreateBareDir(docRoot));
    httpSetHostDocumentRoot(host, docRoot);
    return server;
}


static int destroyServerConnections(HttpServer *server)
{
    HttpConn    *conn;
    Http        *http;
    int         next;

    http = server->http;
    lock(http);

    for (next = 0; (conn = mprGetNextItem(http->connections, &next)) != 0; ) {
        if (conn->server == server) {
            conn->server = 0;
            httpDestroyConn(conn);
            next--;
        }
    }
    unlock(http);
    return 0;
}


static bool validateServer(HttpServer *server)
{
    HttpHost    *host;

    if ((host = mprGetFirstItem(server->hosts)) == 0) {
        mprError("Missing host object on server");
        return 0;
    }
    if (mprGetListLength(host->aliases) == 0) {
        httpAddAlias(host, httpCreateAlias("", host->documentRoot, 0));
    }
    return 1;
}


int httpStartServer(HttpServer *server)
{
    cchar   *proto, *ip;

    if (!validateServer(server)) {
        return MPR_ERR_BAD_ARGS;
    }
    if ((server->sock = mprCreateSocket(server->ssl)) == 0) {
        return MPR_ERR_MEMORY;
    }
    if (mprListenOnSocket(server->sock, server->ip, server->port, MPR_SOCKET_NODELAY | MPR_SOCKET_THREAD) < 0) {
        mprError("Can't open a socket on %s, port %d", server->ip, server->port);
        return MPR_ERR_CANT_OPEN;
    }
    if (server->http->listenCallback && (server->http->listenCallback)(server) < 0) {
        return MPR_ERR_CANT_OPEN;
    }
    if (server->async && server->waitHandler ==  0) {
        //  TODO -- this really should be in server->listen->handler
        server->waitHandler = mprCreateWaitHandler(server->sock->fd, MPR_SOCKET_READABLE, server->dispatcher,
            httpAcceptConn, server, (server->dispatcher) ? 0 : MPR_WAIT_NEW_DISPATCHER);
    } else {
        mprSetSocketBlockingMode(server->sock, 1);
    }
    proto = mprIsSocketSecure(server->sock) ? "HTTPS" : "HTTP ";
    ip = *server->ip ? server->ip : "*";
    mprLog(MPR_CONFIG, "Started %s server on \"%s:%d\"", proto, ip, server->port);
    return 0;
}


void httpStopServer(HttpServer *server)
{
    mprCloseSocket(server->sock, 0);
    server->sock = 0;
}


int httpValidateLimits(HttpServer *server, int event, HttpConn *conn)
{
    HttpLimits      *limits;
    int             count;

    limits = server->limits;
    mprAssert(conn->server == server);
    lock(server->http);

    switch (event) {
    case HTTP_VALIDATE_OPEN_CONN:
        if (server->clientCount >= limits->clientCount) {
            unlock(server->http);
            httpError(conn, HTTP_ABORT | HTTP_CODE_SERVICE_UNAVAILABLE, 
                "Too many concurrent clients %d/%d", server->clientCount, limits->clientCount);
            return 0;
        }
        count = (int) PTOL(mprLookupKey(server->clientLoad, conn->ip));
        mprAddKey(server->clientLoad, conn->ip, ITOP(count + 1));
        server->clientCount = (int) mprGetHashLength(server->clientLoad);
        break;

    case HTTP_VALIDATE_CLOSE_CONN:
        count = (int) PTOL(mprLookupKey(server->clientLoad, conn->ip));
        if (count > 1) {
            mprAddKey(server->clientLoad, conn->ip, ITOP(count - 1));
        } else {
            mprRemoveKey(server->clientLoad, conn->ip);
        }
        server->clientCount = (int) mprGetHashLength(server->clientLoad);
        mprLog(4, "Close connection %d. Active requests %d, active clients %d.", conn->seqno, server->requestCount, 
            server->clientCount);
        break;
    
    case HTTP_VALIDATE_OPEN_REQUEST:
        if (server->requestCount >= limits->requestCount) {
            unlock(server->http);
            httpError(conn, HTTP_ABORT | HTTP_CODE_SERVICE_UNAVAILABLE, 
                "Too many concurrent requests %d/%d", server->requestCount, limits->requestCount);
            return 0;
        }
        server->requestCount++;
        break;

    case HTTP_VALIDATE_CLOSE_REQUEST:
        server->requestCount--;
        mprAssert(server->requestCount >= 0);
        mprLog(4, "Close request. Active requests %d, active clients %d.", server->requestCount, server->clientCount);
        break;
    }
    mprLog(6, "Validate request. Counts: requests: %d/%d, clients %d/%d", 
        server->requestCount, limits->requestCount, server->clientCount, limits->clientCount);
    unlock(server->http);
    return 1;
}


/*  
    Accept a new client connection on a new socket. If multithreaded, this will come in on a worker thread 
    dedicated to this connection. This is called from the listen wait handler.
 */
HttpConn *httpAcceptConn(HttpServer *server, MprEvent *event)
{
    HttpConn        *conn;
    MprSocket       *sock;
    MprDispatcher   *dispatcher;
    MprEvent        e;
    int             level;

    mprAssert(server);
    mprAssert(event);

    /*
        This will block in sync mode until a connection arrives
     */
    if ((sock = mprAcceptSocket(server->sock)) == 0) {
        return 0;
    }
    if (server->waitHandler) {
        /* Re-enable events on the listen socket */
        mprWaitOn(server->waitHandler, MPR_READABLE);
    }
    dispatcher = event->dispatcher;

    if ((conn = httpCreateConn(server->http, server, dispatcher)) == 0) {
        mprCloseSocket(sock, 0);
        return 0;
    }
    conn->notifier = server->notifier;
    conn->async = server->async;
    conn->server = server;
    conn->sock = sock;
    conn->port = sock->port;
    conn->ip = sclone(sock->ip);
    conn->secure = mprIsSocketSecure(sock);

    if (!httpValidateLimits(server, HTTP_VALIDATE_OPEN_CONN, conn)) {
        /* Prevent validate limits from */
        conn->server = 0;
        httpDestroyConn(conn);
        return 0;
    }
    mprAssert(conn->state == HTTP_STATE_BEGIN);
    httpSetState(conn, HTTP_STATE_CONNECTED);

    if ((level = httpShouldTrace(conn, HTTP_TRACE_RX, HTTP_TRACE_CONN, NULL)) >= 0) {
        mprLog(level, "### Incoming connection from %s:%d to %s:%d %s", 
            conn->ip, conn->port, sock->acceptIp, sock->acceptPort, conn->secure ? "(secure)" : "");
    }
    e.mask = MPR_READABLE;
    e.timestamp = conn->http->now;
    (conn->ioCallback)(conn, &e);
    return conn;
}


void *httpGetServerContext(HttpServer *server)
{
    return server->context;
}


int httpGetServerAsync(HttpServer *server) 
{
    return server->async;
}


//  MOB - rename. This could be a "restart"
void httpSetServerAddress(HttpServer *server, cchar *ip, int port)
{
    if (ip) {
        server->ip = sclone(ip);
    }
    if (port >= 0) {
        server->port = port;
    }
    if (server->sock) {
        httpStopServer(server);
        httpStartServer(server);
    }
}


void httpSetServerAsync(HttpServer *server, int async)
{
    if (server->sock) {
        if (server->async && !async) {
            mprSetSocketBlockingMode(server->sock, 1);
        }
        if (!server->async && async) {
            mprSetSocketBlockingMode(server->sock, 0);
        }
    }
    server->async = async;
}


void httpSetServerContext(HttpServer *server, void *context)
{
    mprAssert(server);
    server->context = context;
}


void httpSetServerLocation(HttpServer *server, HttpLoc *loc)
{
    mprAssert(server);
    mprAssert(loc);
    server->loc = loc;
}


void httpSetServerNotifier(HttpServer *server, HttpNotifier notifier)
{
    mprAssert(server);
    server->notifier = notifier;
}


#if UNUSED
/*
    This returns the first matching server. IP and port can be wild (set to 0)
 */
HttpServer *httpLookupServer(cchar *ip, int port)
{
    HttpServer  *server;
    Http        *http;
    int         next, count;

    http = MPR->httpService;
    if (ip == 0) {
        ip = "";
    }
    for (count = 0, next = 0; (server = mprGetNextItem(http->servers, &next)) != 0; ) {
        if (server->port <= 0 || port <= 0 || server->port == port) {
            mprAssert(server->ip);
            if (*server->ip == '\0' || *ip == '\0' || scmp(server->ip, ip) == 0) {
                return server;
            }
        }
    }
    return 0;
}
#endif


int httpSecureServer(HttpServer *server, struct MprSsl *ssl)
{
#if BLD_FEATURE_SSL
    server->ssl = ssl;
    return 0;
#else
    return MPR_ERR_BAD_STATE;
#endif
}


int httpSecureServerByName(cchar *name, struct MprSsl *ssl)
{
    HttpServer  *server;
    Http        *http;
    char        *ip;
    int         port, next, count;

    http = MPR->httpService;
    mprParseIp(name, &ip, &port, -1);
    if (ip == 0) {
        ip = "";
    }
    for (count = 0, next = 0; (server = mprGetNextItem(http->servers, &next)) != 0; ) {
        if (server->port <= 0 || port <= 0 || server->port == port) {
            mprAssert(server->ip);
            if (*server->ip == '\0' || *ip == '\0' || scmp(server->ip, ip) == 0) {
                httpSecureServer(server, ssl);
                count++;
            }
        }
    }
    return (count == 0) ? MPR_ERR_CANT_FIND : 0;
}


void httpAddHostToServer(HttpServer *server, HttpHost *host)
{
    mprAddItem(server->hosts, host);
    if (server->limits == 0) {
        server->limits = host->limits;
    }
}


bool httpIsNamedVirtualServer(HttpServer *server)
{
    return server->flags & HTTP_NAMED_VHOST;
}


void httpSetNamedVirtualServer(HttpServer *server)
{
    server->flags |= HTTP_NAMED_VHOST;
}


HttpHost *httpLookupHost(HttpServer *server, cchar *name)
{
    HttpHost    *host;
    char        *ip;
    int         next, port;

    if (name == 0) {
        return mprGetFirstItem(server->hosts);
    }
    mprParseIp(name, &ip, &port, -1);

    for (next = 0; (host = mprGetNextItem(server->hosts, &next)) != 0; ) {
        if (host->port <= 0 || port <= 0 || host->port == port) {
            if (*host->ip == '\0' || *ip == '\0' || scmp(host->ip, ip) == 0) {
                return host;
            }
        }
    }
    return 0;
}


int httpSetNamedVirtualServers(Http *http, cchar *ip, int port)
{
    HttpServer  *server;
    int         next, count;

    if (ip == 0) {
        ip = "";
    }
    for (count = 0, next = 0; (server = mprGetNextItem(http->servers, &next)) != 0; ) {
        if (server->port <= 0 || port <= 0 || server->port == port) {
            mprAssert(server->ip);
            if (*server->ip == '\0' || *ip == '\0' || scmp(server->ip, ip) == 0) {
                httpSetNamedVirtualServer(server);
                count++;
            }
        }
    }
    return (count == 0) ? MPR_ERR_CANT_FIND : 0;
}


/*
    @copy   default
    
    Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
    Copyright (c) Michael O'Brien, 1993-2011. All Rights Reserved.
    
    This software is distributed under commercial and open source licenses.
    You may use the GPL open source license described below or you may acquire 
    a commercial license from Embedthis Software. You agree to be fully bound 
    by the terms of either license. Consult the LICENSE.TXT distributed with 
    this software for full details.
    
    This software is open source; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License as published by the 
    Free Software Foundation; either version 2 of the License, or (at your 
    option) any later version. See the GNU General Public License for more 
    details at: http://www.embedthis.com/downloads/gplLicense.html
    
    This program is distributed WITHOUT ANY WARRANTY; without even the 
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
    
    This GPL license does NOT permit incorporating this software into 
    proprietary programs. If you are unable to comply with the GPL, you must
    acquire a commercial license to use this software. Commercial licenses 
    for this software and support services are available from Embedthis 
    Software at http://www.embedthis.com 
    
    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
