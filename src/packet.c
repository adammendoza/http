/*
    packet.c -- Queue support routines. Queues are the bi-directional data flow channels for the pipeline.
    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************* Includes ***********************************/

#include    "http.h"

/************************************ Code ************************************/
/*  Create a new packet. If size is -1, then also create a default growable buffer -- used for incoming body content. If 
    size > 0, then create a non-growable buffer of the requested size.
 */
HttpPacket *httpCreatePacket(MprCtx ctx, int size)
{
    HttpPacket  *packet;

    packet = mprAllocObjZeroed(ctx, HttpPacket);
    if (packet == 0) {
        return 0;
    }
    if (size != 0) {
        packet->content = mprCreateBuf(packet, size < 0 ? HTTP_BUFSIZE: size, -1);
        if (packet->content == 0) {
            mprFree(packet);
            return 0;
        }
    }
    return packet;
}


/*
    Create a packet for the connection to read into. This may come from the connection packet free list.
 */
HttpPacket *httpCreateConnPacket(HttpConn *conn, int size)
{
    HttpPacket  *packet;
    HttpRx      *rx;

    if (conn->state >= HTTP_STATE_COMPLETE) {
        return httpCreatePacket((MprCtx) conn, size);
    }
    rx = conn->rx;
    if (rx) {
        if ((packet = rx->freePackets) != NULL && size <= packet->content->buflen) {
            rx->freePackets = packet->next; 
            packet->next = 0;
            mprStealBlock(conn, packet);
            return packet;
        }
    }
    return httpCreatePacket(conn->rx ? (MprCtx) conn->rx: (MprCtx) conn, size);
}


void httpFreePacket(HttpQueue *q, HttpPacket *packet)
{
#if FUTURE
    HttpConn    *conn;
    HttpRx      *rx;

    conn = q->conn;
    rx = conn->rx;

    if (rx == 0 || packet->content == 0 || packet->content->buflen < HTTP_BUFSIZE || mprGetParent(packet) == conn) {
        /* 
            Don't bother recycling non-content, small packets or packets owned by the connection
            We only store packets owned by the request and not by the connection on the free list.
         */
        mprFree(packet);
        return;
    }
    /*  
        Add to the packet free list for recycling
        MOB -- need some thresholds to manage this incase it gets too big
     */
    mprAssert(packet->content);
    mprFlushBuf(packet->content);
    mprFree(packet->prefix);
    packet->prefix = 0;
    mprFree(packet->suffix);
    packet->suffix = 0;
    packet->entityLength = 0;
    packet->flags = 0;
    packet->next = rx->freePackets;
    rx->freePackets = packet;
#else
    mprFree(packet);
#endif
} 


HttpPacket *httpCreateDataPacket(MprCtx ctx, int size)
{
    HttpPacket    *packet;

    packet = httpCreatePacket(ctx, size);
    if (packet == 0) {
        return 0;
    }
    packet->flags = HTTP_PACKET_DATA;
    return packet;
}


HttpPacket *httpCreateEndPacket(MprCtx ctx)
{
    HttpPacket    *packet;

    packet = httpCreatePacket(ctx, 0);
    if (packet == 0) {
        return 0;
    }
    packet->flags = HTTP_PACKET_END;
    return packet;
}


HttpPacket *httpCreateHeaderPacket(MprCtx ctx)
{
    HttpPacket    *packet;

    packet = httpCreatePacket(ctx, HTTP_BUFSIZE);
    if (packet == 0) {
        return 0;
    }
    packet->flags = HTTP_PACKET_HEADER;
    return packet;
}


/* 
   Get the next packet from the queue
 */
HttpPacket *httpGetPacket(HttpQueue *q)
{
    HttpConn      *conn;
    HttpQueue     *prev;
    HttpPacket    *packet;

    conn = q->conn;
    while (q->first) {
        if ((packet = q->first) != 0) {
            q->first = packet->next;
            packet->next = 0;
            q->count -= httpGetPacketLength(packet);
            mprAssert(q->count >= 0);
            if (packet == q->last) {
                q->last = 0;
                mprAssert(q->first == 0);
            }
        }
        if (q->flags & HTTP_QUEUE_FULL && q->count < q->low) {
            /*
                This queue was full and now is below the low water mark. Back-enable the previous queue.
             */
            q->flags &= ~HTTP_QUEUE_FULL;
            prev = httpFindPreviousQueue(q);
            if (prev && prev->flags & HTTP_QUEUE_DISABLED) {
                httpEnableQueue(prev);
            }
        }
        return packet;
    }
    return 0;
}


/*  
    Test if the packet is too too large to be accepted by the downstream queue.
 */
bool httpIsPacketTooBig(HttpQueue *q, HttpPacket *packet)
{
    int     size;
    
    size = mprGetBufLength(packet->content);
    return size > q->max || size > q->packetSize;
}


/*  
    Join a packet onto the service queue
 */
void httpJoinPacketForService(HttpQueue *q, HttpPacket *packet, bool serviceQ)
{
    HttpPacket    *old;

    if (q->first == 0) {
        /*  
            Just use the service queue as a holding queue while we aggregate the post data.
         */
        httpPutForService(q, packet, 0);
    } else {
        q->count += httpGetPacketLength(packet);
        if (q->first && httpGetPacketLength(q->first) == 0) {
            old = q->first;
            packet = q->first->next;
            q->first = packet;
            httpFreePacket(q, old);

        } else {
            /*
                Aggregate all data into one packet and free the packet.
             */
            httpJoinPacket(q->first, packet);
            httpFreePacket(q, packet);
        }
    }
    if (serviceQ && !(q->flags & HTTP_QUEUE_DISABLED))  {
        httpScheduleQueue(q);
    }
}


/*  
    Join two packets by pulling the content from the second into the first.
 */
int httpJoinPacket(HttpPacket *packet, HttpPacket *p)
{
    if (mprPutBlockToBuf(packet->content, mprGetBufStart(p->content), httpGetPacketLength(p)) < 0) {
        return MPR_ERR_NO_MEMORY;
    }
    return 0;
}


/*
    Join queue packets up to the maximum of the given size and the downstream queue packet size.
 */
void httpJoinPackets(HttpQueue *q, int size)
{
    HttpPacket  *first, *next;
    int         maxPacketSize;

    if ((first = q->first) != 0 && first->next) {
        maxPacketSize = min(q->nextQ->packetSize, size);
        while ((next = first->next) != 0) {
            if (next->content && (httpGetPacketLength(first) + httpGetPacketLength(next)) < maxPacketSize) {
                httpJoinPacket(first, next);
                first->next = next->next;
                httpFreePacket(q, next);
            } else {
                break;
            }
        }
    }
}


/*  
    Put the packet back at the front of the queue
 */
void httpPutBackPacket(HttpQueue *q, HttpPacket *packet)
{
    mprAssert(packet);
    mprAssert(packet->next == 0);
    
    packet->next = q->first;

    if (q->first == 0) {
        q->last = packet;
    }
    q->first = packet;
    mprAssert(httpGetPacketLength(packet) >= 0);
    q->count += httpGetPacketLength(packet);
    mprAssert(q->count >= 0);
}


/*  
    Put a packet on the service queue.
 */
void httpPutForService(HttpQueue *q, HttpPacket *packet, bool serviceQ)
{
    mprAssert(packet);
   
    q->count += httpGetPacketLength(packet);
    packet->next = 0;
    
    if (q->first) {
        q->last->next = packet;
        q->last = packet;
    } else {
        q->first = packet;
        q->last = packet;
    }
    if (serviceQ && !(q->flags & HTTP_QUEUE_DISABLED))  {
        httpScheduleQueue(q);
    }
}


/*  
    Split a packet if required so it fits in the downstream queue. Put back the 2nd portion of the split packet on the queue.
    Ensure that the packet is not larger than "size" if it is greater than zero.
 */
int httpResizePacket(HttpQueue *q, HttpPacket *packet, int size)
{
    HttpPacket  *tail;
    int         len;
    
    if (size <= 0) {
        size = MAXINT;
    }

    /*  
        Calculate the size that will fit
     */
    len = packet->content ? httpGetPacketLength(packet) : packet->entityLength;
    size = min(size, len);
    size = min(size, q->nextQ->max);
    size = min(size, q->nextQ->packetSize);

    if (size == 0) {
        /* Can't fit anything downstream, no point splitting yet */
        return 0;
    }
    if (size == len) {
        return 0;
    }
    tail = httpSplitPacket(q->conn, packet, size);
    if (tail == 0) {
        return MPR_ERR_NO_MEMORY;
    }
    httpPutBackPacket(q, tail);
    return 0;
}


HttpPacket *httpDupPacket(MprCtx ctx, HttpPacket *orig)
{
    HttpPacket  *packet;
    int         count, size;

    count = httpGetPacketLength(orig);
    size = max(count, HTTP_BUFSIZE);
    size = HTTP_PACKET_ALIGN(size);

    if ((packet = httpCreatePacket(ctx, 0)) == 0) {
        return 0;
    }
    if (orig->content) {
        packet->content = mprDupBuf(packet, orig->content);
    }
    if (orig->prefix) {
        packet->prefix = mprDupBuf(packet, orig->prefix);
    }
    if (orig->suffix) {
        packet->suffix = mprDupBuf(packet, orig->suffix);
    }
    packet->flags = orig->flags;
    packet->entityLength = orig->entityLength;
    return packet;
}


/*  
    Pass to a queue
 */
void httpSendPacket(HttpQueue *q, HttpPacket *packet)
{
    mprAssert(packet);
    
    mprAssert(q->put);
    q->put(q, packet);
}


/*  
    Pass to the next queue
 */
void httpSendPacketToNext(HttpQueue *q, HttpPacket *packet)
{
    mprAssert(packet);
    
    mprAssert(q->nextQ->put);
    q->nextQ->put(q->nextQ, packet);
}


void httpSendPackets(HttpQueue *q)
{
    HttpPacket    *packet;

    for (packet = httpGetPacket(q); packet; packet = httpGetPacket(q)) {
        httpSendPacketToNext(q, packet);
    }
}


/*  
    Split a packet at a given offset and return a new packet containing the data after the offset.
    The suffix data migrates to the new packet. 
 */
HttpPacket *httpSplitPacket(MprCtx ctx, HttpPacket *orig, int offset)
{
    HttpPacket  *packet;
    int         count, size;

    if (offset >= httpGetPacketLength(orig)) {
        mprAssert(0);
        return 0;
    }
    count = httpGetPacketLength(orig) - offset;
    size = max(count, HTTP_BUFSIZE);
    size = HTTP_PACKET_ALIGN(size);

    if ((packet = httpCreatePacket(ctx, orig->entityLength ? 0 : size)) == 0) {
        return 0;
    }
    packet->flags = orig->flags;

    if (orig->entityLength) {
        orig->entityLength = offset;
        packet->entityLength = count;
    }

#if FUTURE
    /*
        Suffix migrates to the new packet (Not currently used)
     */
    if (packet->suffix) {
        packet->suffix = orig->suffix;
        mprStealBlock(packet, packet->suffix);
        orig->suffix = 0;
    }
#endif

    if (orig->content && httpGetPacketLength(orig) > 0) {
        mprAdjustBufEnd(orig->content, -count);
        mprPutBlockToBuf(packet->content, mprGetBufEnd(orig->content), count);
#if BLD_DEBUG
        mprAddNullToBuf(orig->content);
#endif
    }
    return packet;
}


/*
    @copy   default
    
    Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
    Copyright (c) Michael O'Brien, 1993-2010. All Rights Reserved.
    
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
    
    @end
 */
