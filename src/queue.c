/*
    queue.c -- Queue support routines. Queues are the bi-directional data flow channels for the pipeline.
    Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************* Includes ***********************************/

#include    "http.h"

/********************************** Forwards **********************************/

static void manageQueue(HttpQueue *q, int flags);

/************************************ Code ************************************/

PUBLIC HttpQueue *httpCreateQueueHead(HttpConn *conn, cchar *name)
{
    HttpQueue   *q;

    if ((q = mprAllocObj(HttpQueue, manageQueue)) == 0) {
        return 0;
    }
    httpInitQueue(conn, q, name);
    httpInitSchedulerQueue(q);
    return q;
}


PUBLIC HttpQueue *httpCreateQueue(HttpConn *conn, HttpStage *stage, int dir, HttpQueue *prev)
{
    HttpQueue   *q;

    if ((q = mprAllocObj(HttpQueue, manageQueue)) == 0) {
        return 0;
    }
    q->conn = conn;
    httpInitQueue(conn, q, stage->name);
    httpInitSchedulerQueue(q);
    httpAssignQueue(q, stage, dir);
    if (prev) {
        httpAppendQueue(prev, q);
    }
    return q;
}


static void manageQueue(HttpQueue *q, int flags)
{
    HttpPacket      *packet;

    if (flags & MPR_MANAGE_MARK) {
        mprMark(q->owner);
        for (packet = q->first; packet; packet = packet->next) {
            mprMark(packet);
        }
        mprMark(q->last);
        mprMark(q->nextQ);
        mprMark(q->prevQ);
        mprMark(q->stage);
        mprMark(q->conn);
        mprMark(q->scheduleNext);
        mprMark(q->schedulePrev);
        mprMark(q->pair);
        mprMark(q->queueData);
        mprMark(q->queueData);
        if (q->nextQ && q->nextQ->stage) {
            /* Not a queue head */
            mprMark(q->nextQ);
        }
    }
}


PUBLIC void httpAssignQueue(HttpQueue *q, HttpStage *stage, int dir)
{
    q->stage = stage;
    q->close = stage->close;
    q->open = stage->open;
    q->start = stage->start;
    if (dir == HTTP_QUEUE_TX) {
        q->put = stage->outgoing;
        q->service = stage->outgoingService;
    } else {
        q->put = stage->incoming;
        q->service = stage->incomingService;
    }
    q->owner = stage->name;
}


PUBLIC void httpInitQueue(HttpConn *conn, HttpQueue *q, cchar *name)
{
    HttpTx      *tx;

    tx = conn->tx;
    q->conn = conn;
    q->nextQ = q;
    q->prevQ = q;
    q->owner = sclone(name);
    q->max = conn->limits->stageBufferSize;
    q->low = q->max / 100 *  5;    
    if (tx && tx->chunkSize > 0) {
        q->packetSize = tx->chunkSize;
    } else {
        q->packetSize = q->max;
    }
}


PUBLIC void httpSetQueueLimits(HttpQueue *q, ssize low, ssize max)
{
    q->low = low;
    q->max = max;
}


#if UNUSED && KEEP
/*  
    Insert a queue after the previous element
 */
PUBLIC void httpAppendQueueToHead(HttpQueue *head, HttpQueue *q)
{
    q->nextQ = head;
    q->prevQ = head->prevQ;
    head->prevQ->nextQ = q;
    head->prevQ = q;
}
#endif


PUBLIC void httpSuspendQueue(HttpQueue *q)
{
    mprLog(7, "Suspend q %s", q->owner);
    q->flags |= HTTP_QUEUE_SUSPENDED;
}


/*  
    Remove all data in the queue. If removePackets is true, actually remove the packet too.
    This preserves the header and EOT packets.
 */
PUBLIC void httpDiscardQueueData(HttpQueue *q, bool removePackets)
{
    HttpPacket  *packet, *prev, *next;
    ssize       len;

    if (q == 0) {
        return;
    }
    for (prev = 0, packet = q->first; packet; packet = next) {
        next = packet->next;
        if (packet->flags & (HTTP_PACKET_RANGE | HTTP_PACKET_DATA)) {
            if (removePackets) {
                if (prev) {
                    prev->next = next;
                } else {
                    q->first = next;
                }
                if (packet == q->last) {
                    q->last = prev;
                }
                q->count -= httpGetPacketLength(packet);
                mprAssert(q->count >= 0);
                continue;
            } else {
                len = httpGetPacketLength(packet);
                q->conn->tx->length -= len;
                q->count -= len;
                mprAssert(q->count >= 0);
                if (packet->content) {
                    mprFlushBuf(packet->content);
                }
            }
        }
        prev = packet;
    }
}


/*  
    Flush queue data by scheduling the queue and servicing all scheduled queues. Return true if there is room for more data.
    If blocking is requested, the call will block until the queue count falls below the queue max.
    WARNING: Be very careful when using blocking == true. Should only be used by end applications and not by middleware.
 */
PUBLIC bool httpFlushQueue(HttpQueue *q, bool blocking)
{
    HttpConn    *conn;
    HttpQueue   *next;

    conn = q->conn;
    mprAssert(conn->sock);
    do {
        httpScheduleQueue(q);
        next = q->nextQ;
        if (next->count >= next->max) {
            httpScheduleQueue(next);
        }
        httpServiceQueues(conn);
        if (conn->sock == 0) {
            break;
        }
        if (blocking) {
            httpPumpHandler(conn);
        }
    } while (blocking && q->count > 0);
    return (q->count < q->max) ? 1 : 0;
}


PUBLIC void httpResumeQueue(HttpQueue *q)
{
    mprLog(7, "Enable q %s", q->owner);
    q->flags &= ~HTTP_QUEUE_SUSPENDED;
    httpScheduleQueue(q);
}


PUBLIC HttpQueue *httpFindPreviousQueue(HttpQueue *q)
{
    while (q->prevQ && q->prevQ->stage && q->prevQ != q) {
        q = q->prevQ;
        if (q->service) {
            return q;
        }
    }
    return 0;
}


PUBLIC HttpQueue *httpGetNextQueueForService(HttpQueue *q)
{
    HttpQueue     *next;
    
    if (q->scheduleNext != q) {
        next = q->scheduleNext;
        next->schedulePrev->scheduleNext = next->scheduleNext;
        next->scheduleNext->schedulePrev = next->schedulePrev;
        next->schedulePrev = next->scheduleNext = next;
        return next;
    }
    return 0;
}


/*  
    Return the number of bytes the queue will accept. Always positive.
 */
PUBLIC ssize httpGetQueueRoom(HttpQueue *q)
{
    mprAssert(q->max > 0);
    mprAssert(q->count >= 0);
    
    if (q->count >= q->max) {
        return 0;
    }
    return q->max - q->count;
}


PUBLIC void httpInitSchedulerQueue(HttpQueue *q)
{
    q->scheduleNext = q;
    q->schedulePrev = q;
}


/*  
    Append a queue after the previous element
 */
PUBLIC void httpAppendQueue(HttpQueue *prev, HttpQueue *q)
{
    q->nextQ = prev->nextQ;
    q->prevQ = prev;
    prev->nextQ->prevQ = q;
    prev->nextQ = q;
}


PUBLIC bool httpIsQueueEmpty(HttpQueue *q)
{
    return q->first == 0;
}


/*  
    Read data. If sync mode, this will block. If async, will never block.
    Will return what data is available up to the requested size. Returns a byte count.
 */
PUBLIC ssize httpRead(HttpConn *conn, char *buf, ssize size)
{
    HttpPacket  *packet;
    HttpQueue   *q;
    MprBuf      *content;
    ssize       nbytes, len;

    q = conn->readq;
    mprAssert(q->count >= 0);
    mprAssert(size >= 0);
    VERIFY_QUEUE(q);

    while (q->count <= 0 && !conn->async && !conn->tx->complete && conn->sock && (conn->state <= HTTP_STATE_CONTENT)) {
        httpServiceQueues(conn);
        if (conn->sock) {
            httpWait(conn, 0, MPR_TIMEOUT_NO_BUSY);
        }
    }
    conn->lastActivity = conn->http->now;

    for (nbytes = 0; size > 0 && q->count > 0; ) {
        if ((packet = q->first) == 0) {
            break;
        }
        content = packet->content;
        len = mprGetBufLength(content);
        len = min(len, size);
        mprAssert(len <= q->count);
        if (len > 0) {
            len = mprGetBlockFromBuf(content, buf, len);
            mprAssert(len <= q->count);
        }
        buf += len;
        size -= len;
        q->count -= len;
        mprAssert(q->count >= 0);
        nbytes += len;
        if (mprGetBufLength(content) == 0) {
            httpGetPacket(q);
        }
    }
    mprAssert(q->count >= 0);
    if (nbytes < size) {
        buf[nbytes] = '\0';
    }
    return nbytes;
}


PUBLIC ssize httpGetReadCount(HttpConn *conn)
{
    return conn->readq->count;
}


PUBLIC bool httpIsEof(HttpConn *conn) 
{
    return conn->rx == 0 || conn->rx->eof;
}


/*
    Read data as a string
 */
PUBLIC char *httpReadString(HttpConn *conn)
{
    HttpRx      *rx;
    ssize       sofar, nbytes, remaining;
    char        *content;

    rx = conn->rx;
    remaining = (ssize) min(MAXSSIZE, rx->length);

    if (remaining > 0) {
        if ((content = mprAlloc(remaining + 1)) == 0) {
            return 0;
        }
        sofar = 0;
        while (remaining > 0) {
            nbytes = httpRead(conn, &content[sofar], remaining);
            if (nbytes < 0) {
                return 0;
            }
            sofar += nbytes;
            remaining -= nbytes;
        }
    } else {
        content = mprAlloc(HTTP_BUFSIZE);
        sofar = 0;
        while (1) {
            nbytes = httpRead(conn, &content[sofar], HTTP_BUFSIZE);
            if (nbytes < 0) {
                return 0;
            } else if (nbytes == 0) {
                break;
            }
            sofar += nbytes;
            content = mprRealloc(content, sofar + HTTP_BUFSIZE);
        }
    }
    content[sofar] = '\0';
    return content;
}


PUBLIC void httpRemoveQueue(HttpQueue *q)
{
    q->prevQ->nextQ = q->nextQ;
    q->nextQ->prevQ = q->prevQ;
    q->prevQ = q->nextQ = q;
}


PUBLIC void httpScheduleQueue(HttpQueue *q)
{
    HttpQueue     *head;
    
    mprAssert(q->conn);
    head = q->conn->serviceq;
    
    if (q->scheduleNext == q && !(q->flags & HTTP_QUEUE_SUSPENDED)) {
        q->scheduleNext = head;
        q->schedulePrev = head->schedulePrev;
        head->schedulePrev->scheduleNext = q;
        head->schedulePrev = q;
    }
}


PUBLIC void httpServiceQueue(HttpQueue *q)
{
    q->conn->currentq = q;

    if (q->servicing) {
        q->flags |= HTTP_QUEUE_RESERVICE;
    } else {
        /*  
            Since we are servicing this "q" now, we can remove from the schedule queue if it is already queued.
         */
        if (q->conn->serviceq->scheduleNext == q) {
            httpGetNextQueueForService(q->conn->serviceq);
        }
        if (!(q->flags & HTTP_QUEUE_SUSPENDED)) {
            q->servicing = 1;
            q->service(q);
            if (q->flags & HTTP_QUEUE_RESERVICE) {
                q->flags &= ~HTTP_QUEUE_RESERVICE;
                httpScheduleQueue(q);
            }
            q->flags |= HTTP_QUEUE_SERVICED;
            q->servicing = 0;
        }
    }
}


/*  
    Return true if the next queue will accept this packet. If not, then disable the queue's service procedure.
    This may split the packet if it exceeds the downstreams maximum packet size.
 */
PUBLIC bool httpWillNextQueueAcceptPacket(HttpQueue *q, HttpPacket *packet)
{
    HttpQueue   *nextQ;
    ssize       size;

    nextQ = q->nextQ;
    size = httpGetPacketLength(packet);
    if (size <= nextQ->packetSize && (size + nextQ->count) <= nextQ->max) {
        return 1;
    }
    if (httpResizePacket(q, packet, 0) < 0) {
        return 0;
    }
    size = httpGetPacketLength(packet);
    mprAssert(size <= nextQ->packetSize);
    if ((size + nextQ->count) <= nextQ->max) {
        return 1;
    }
    /*  
        The downstream queue is full, so disable the queue and mark the downstream queue as full and service 
     */
    httpSuspendQueue(q);
    if (!(nextQ->flags & HTTP_QUEUE_SUSPENDED)) {
        httpScheduleQueue(nextQ);
    }
    return 0;
}


/*  
    Return true if the next queue will accept a certain amount of data.
 */
PUBLIC bool httpWillNextQueueAcceptSize(HttpQueue *q, ssize size)
{
    HttpQueue   *nextQ;

    nextQ = q->nextQ;
    if (size <= nextQ->packetSize && (size + nextQ->count) <= nextQ->max) {
        return 1;
    }
    httpSuspendQueue(q);
    if (!(nextQ->flags & HTTP_QUEUE_SUSPENDED)) {
        httpScheduleQueue(nextQ);
    }
    return 0;
}


/*
    Write a block of data. This is the lowest level write routine for data. This will buffer the data and flush if
    the queue buffer is full. Flushing is done by calling httpFlushQueue which will service queues as required. This
    may call the queue outgoing service routine and disable downstream queues if they are overfull.
    This routine will always accept the data and never return "short". 
 */
PUBLIC ssize httpWriteBlock(HttpQueue *q, cchar *buf, ssize len, int flags)
{
    HttpPacket  *packet;
    HttpConn    *conn;
    HttpTx      *tx;
    ssize       totalWritten, packetSize, thisWrite;

    mprAssert(q == q->conn->writeq);
               
    conn = q->conn;
    tx = conn->tx;
    if (tx == 0 || tx->finalized) {
        return MPR_ERR_CANT_WRITE;
    }
    tx->responded = 1;

    for (totalWritten = 0; len > 0; ) {
        LOG(7, "httpWriteBlock q_count %d, q_max %d", q->count, q->max);
        if (conn->state >= HTTP_STATE_COMPLETE) {
            return MPR_ERR_CANT_WRITE;
        }
        if (q->last && q->last != q->first && q->last->flags & HTTP_PACKET_DATA && mprGetBufSpace(q->last->content) > 0) {
            packet = q->last;
        } else {
            packetSize = (tx->chunkSize > 0) ? tx->chunkSize : q->packetSize;
            if ((packet = httpCreateDataPacket(packetSize)) == 0) {
                return MPR_ERR_MEMORY;
            }
            httpPutForService(q, packet, HTTP_DELAY_SERVICE);
        }
        thisWrite = min(len, mprGetBufSpace(packet->content));
        if (!(flags & HTTP_BUFFER)) {
            thisWrite = min(thisWrite, q->max - q->count);
        }
        if ((thisWrite = mprPutBlockToBuf(packet->content, buf, thisWrite)) == 0) {
            return MPR_ERR_MEMORY;
        }
        buf += thisWrite;
        len -= thisWrite;
        q->count += thisWrite;
        totalWritten += thisWrite;

        if (q->count >= q->max) {
            httpFlushQueue(q, 0);
            if (q->count >= q->max) {
                if (flags & HTTP_NONBLOCK) {
                    break;
                } else if (flags & HTTP_BLOCK) {
                    while (q->count >= q->max) {
                        mprWaitForEvent(conn->dispatcher, conn->limits->inactivityTimeout);
                    }
                }
            }
        }
    }
    if (conn->error) {
        return MPR_ERR_CANT_WRITE;
    }
    return totalWritten;
}


#if UNUSED
/*
    Write a block of data. This is the lowest level write routine for data. This will buffer the data and flush if
    the queue buffer is full. Flushing is done by calling httpFlushQueue which will service queues as required. This
    may call the queue outgoing service routine and disable downstream queues if they are overfull.
    This routine will always accept the data and never return "short". 
 */
PUBLIC ssize httpWriteBlock(HttpQueue *q, cchar *buf, ssize size)
{
    HttpPacket  *packet;
    HttpConn    *conn;
    HttpTx      *tx;
    ssize       bytes, written, packetSize;

    mprAssert(q == q->conn->writeq);
               
    conn = q->conn;
    tx = conn->tx;
    if (tx == 0 || tx->finalized) {
        return MPR_ERR_CANT_WRITE;
    }
    tx->responded = 1;

    for (written = 0; size > 0; ) {
        LOG(7, "httpWriteBlock q_count %d, q_max %d", q->count, q->max);
        if (conn->state >= HTTP_STATE_COMPLETE) {
            return MPR_ERR_CANT_WRITE;
        }
        if (q->last != q->first && q->last->flags & HTTP_PACKET_DATA) {
            packet = q->last;
            mprAssert(packet->content);
        } else {
            packet = 0;
        }
        if (packet == 0 || mprGetBufSpace(packet->content) == 0) {
            packetSize = (tx->chunkSize > 0) ? tx->chunkSize : q->packetSize;
            if ((packet = httpCreateDataPacket(packetSize)) == 0) {
                return MPR_ERR_MEMORY;
            }
            httpPutForService(q, packet, HTTP_DELAY_SERVICE);
        }
        if ((bytes = mprPutBlockToBuf(packet->content, buf, size)) == 0) {
            return MPR_ERR_MEMORY;
        }
        buf += bytes;
        size -= bytes;
        q->count += bytes;
        written += bytes;
        if (q->count >= q->max) {
            httpFlushQueue(q, 0);
        }
    }
    if (conn->error) {
        return MPR_ERR_CANT_WRITE;
    }
    return written;
}
#endif


PUBLIC ssize httpWriteString(HttpQueue *q, cchar *s)
{
    return httpWriteBlock(q, s, strlen(s), HTTP_BUFFER);
}


PUBLIC ssize httpWriteSafeString(HttpQueue *q, cchar *s)
{
    return httpWriteString(q, mprEscapeHtml(s));
}


PUBLIC ssize httpWrite(HttpQueue *q, cchar *fmt, ...)
{
    va_list     vargs;
    char        *buf;
    
    va_start(vargs, fmt);
    buf = sfmtv(fmt, vargs);
    va_end(vargs);
    return httpWriteString(q, buf);
}


#if BIT_DEBUG
PUBLIC bool httpVerifyQueue(HttpQueue *q)
{
    HttpPacket  *packet;
    ssize       count;

    count = 0;
    for (packet = q->first; packet; packet = packet->next) {
        if (packet->next == 0) {
            assure(packet == q->last);
        }
        count += httpGetPacketLength(packet);
    }
    assure(count == q->count);
    return count <= q->count;
}
#endif

/*
    @copy   default

    Copyright (c) Embedthis Software LLC, 2003-2012. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis Open Source license or you may acquire a 
    commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.

    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
