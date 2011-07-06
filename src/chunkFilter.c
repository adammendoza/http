/*
    chunkFilter.c - Transfer chunk endociding filter.

    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "http.h"

/********************************** Forwards **********************************/

static void incomingChunkData(HttpQueue *q, HttpPacket *packet);
static bool matchChunk(HttpConn *conn, HttpStage *handler, int dir);
static void openChunk(HttpQueue *q);
static void outgoingChunkService(HttpQueue *q);
static void setChunkPrefix(HttpQueue *q, HttpPacket *packet);

/*********************************** Code *************************************/
/* 
   Loadable module initialization
 */
int httpOpenChunkFilter(Http *http)
{
    HttpStage     *filter;

    mprLog(5, "Open chunk filter");
    if ((filter = httpCreateFilter(http, "chunkFilter", HTTP_STAGE_ALL, NULL)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    http->chunkFilter = filter;
    filter->open = openChunk; 
    filter->match = matchChunk; 
    filter->outgoingService = outgoingChunkService; 
    filter->incomingData = incomingChunkData; 
    return 0;
}


static bool matchChunk(HttpConn *conn, HttpStage *handler, int dir)
{
    HttpTx  *tx;

    if (dir & HTTP_STAGE_TX) {
        /*
            Don't match if chunking is explicitly turned off vi a the X_APPWEB_CHUNK_SIZE header which sets the chunk 
            size to zero. Also remove if the response length is already known.
         */
        tx = conn->tx;
        return (tx->length < 0 && tx->chunkSize != 0) ? 1 : 0;

    } else {
        /* 
            Must always be ready to handle chunked response data. Clients create their incoming pipeline before it is
            know what the response data looks like (chunked or not).
         */
        return 1;
    }
}


static void openChunk(HttpQueue *q)
{
    HttpConn    *conn;
    HttpRx      *rx;

    conn = q->conn;
    rx = conn->rx;

    q->packetSize = min(conn->limits->chunkSize, q->max);
#if UNUSED
    rx->chunkState = HTTP_CHUNK_START;
#endif
}


/*  
    Get the next chunk size. Chunked data format is:
        Chunk spec <CRLF>
        Data <CRLF>
        Chunk spec (size == 0) <CRLF>
        <CRLF>
    Chunk spec is: "HEX_COUNT; chunk length DECIMAL_COUNT\r\n". The "; chunk length DECIMAL_COUNT is optional.
    As an optimization, use "\r\nSIZE ...\r\n" as the delimiter so that the CRLF after data does not special consideration.
    Achive this by parseHeaders reversing the input start by 2.
 */
static void incomingChunkData(HttpQueue *q, HttpPacket *packet)
{
    HttpConn    *conn;
    HttpRx      *rx;
    MprBuf      *buf;
    char        *start, *cp;
    int         bad;

    conn = q->conn;
    rx = conn->rx;

    if (!(rx->flags & HTTP_CHUNKED)) {
        httpSendPacketToNext(q, packet);
        return;
    }
    buf = packet->content;

    if (buf == 0) {
        if (rx->chunkState == HTTP_CHUNK_DATA) {
            httpError(conn, HTTP_ABORT | HTTP_CODE_BAD_REQUEST, "Bad chunk state");
            return;
        }
        rx->chunkState = HTTP_CHUNK_EOF;
    }
    
    /*  
        NOTE: the request head ensures that packets are correctly sized by packet inspection. The packet will never
        have more data than the chunk state expects.
     */
    switch (rx->chunkState) {
    case HTTP_CHUNK_START:
        /*  
            Validate:  "\r\nSIZE.*\r\n"
         */
        if (mprGetBufLength(buf) < 5) {
            break;
        }
        start = mprGetBufStart(buf);
        bad = (start[0] != '\r' || start[1] != '\n');
        for (cp = &start[2]; cp < buf->end && *cp != '\n'; cp++) {}
        if (*cp != '\n' && (cp - start) < 80) {
            break;
        }
        bad += (cp[-1] != '\r' || cp[0] != '\n');
        if (bad) {
            httpError(conn, HTTP_ABORT | HTTP_CODE_BAD_REQUEST, "Bad chunk specification");
            return;
        }
        rx->chunkSize = (int) stoi(&start[2], 16, NULL);
        if (!isxdigit((int) start[2]) || rx->chunkSize < 0) {
            httpError(conn, HTTP_ABORT | HTTP_CODE_BAD_REQUEST, "Bad chunk specification");
            return;
        }
        mprAdjustBufStart(buf, (cp - start + 1));
        rx->remainingContent = rx->chunkSize;
        if (rx->chunkSize == 0) {
            rx->chunkState = HTTP_CHUNK_EOF;
            /*
                We are lenient if the request does not have a trailing "\r\n" after the last chunk
             */
            cp = mprGetBufStart(buf);
            if (mprGetBufLength(buf) == 2 && *cp == '\r' && cp[1] == '\n') {
                mprAdjustBufStart(buf, 2);
            }
        } else {
            rx->chunkState = HTTP_CHUNK_DATA;
        }
        mprAssert(mprGetBufLength(buf) == 0);
        mprLog(5, "chunkFilter: start incoming chunk of %d bytes", rx->chunkSize);
        break;

    case HTTP_CHUNK_DATA:
        mprAssert(httpGetPacketLength(packet) <= rx->chunkSize);
        mprLog(5, "chunkFilter: data %d bytes, rx->remainingContent %d", httpGetPacketLength(packet), 
            rx->remainingContent);
        httpSendPacketToNext(q, packet);
        if (rx->remainingContent == 0) {
            rx->chunkState = HTTP_CHUNK_START;
            rx->remainingContent = HTTP_BUFSIZE;
        }
        break;

    case HTTP_CHUNK_EOF:
        mprAssert(httpGetPacketLength(packet) == 0);
        httpSendPacketToNext(q, packet);
        mprLog(5, "chunkFilter: last chunk");
        break;    

    default:
        mprAssert(0);
    }
}


/*  
    Apply chunks to dynamic outgoing data. 
 */
static void outgoingChunkService(HttpQueue *q)
{
    HttpConn    *conn;
    HttpPacket  *packet;
    HttpTx      *tx;

    conn = q->conn;
    tx = conn->tx;

    if (!(q->flags & HTTP_QUEUE_SERVICED)) {
        /*  
            If the last packet is the end packet, we have all the data. Thus we know the actual content length 
            and can bypass the chunk handler.
         */
        if (q->last->flags & HTTP_PACKET_END) {
            if (tx->chunkSize < 0 && tx->length <= 0) {
                /*  
                    Set the response content length and thus disable chunking -- not needed as we know the entity length.
                 */
                tx->length = (int) q->count;
            }
        } else {
            if (tx->chunkSize < 0) {
                tx->chunkSize = min(conn->limits->chunkSize, q->max);
            }
        }
    }
    if (tx->chunkSize <= 0 || tx->altBody) {
        httpDefaultOutgoingServiceStage(q);
    } else {
        for (packet = httpGetPacket(q); packet; packet = httpGetPacket(q)) {
            if (!(packet->flags & HTTP_PACKET_HEADER)) {
                httpPutBackPacket(q, packet);
                httpJoinPackets(q, tx->chunkSize);
                packet = httpGetPacket(q);
                if (httpGetPacketLength(packet) > tx->chunkSize) {
                    httpResizePacket(q, packet, tx->chunkSize);
                }
            }
            if (!httpWillNextQueueAcceptPacket(q, packet)) {
                httpPutBackPacket(q, packet);
                return;
            }
            if (!(packet->flags & HTTP_PACKET_HEADER)) {
                setChunkPrefix(q, packet);
            }
            httpSendPacketToNext(q, packet);
        }
    }
}


static void setChunkPrefix(HttpQueue *q, HttpPacket *packet)
{
    if (packet->prefix) {
        return;
    }
    packet->prefix = mprCreateBuf(32, 32);
    /*  
        NOTE: prefixes don't count in the queue length. No need to adjust q->count
     */
    if (httpGetPacketLength(packet)) {
        mprPutFmtToBuf(packet->prefix, "\r\n%x\r\n", httpGetPacketLength(packet));
    } else {
        mprPutStringToBuf(packet->prefix, "\r\n0\r\n\r\n");
    }
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
