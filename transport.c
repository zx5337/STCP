/*
 * transport.c 
 *
 * COS461: Assignment 3 (STCP)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"
#include "mysock_impl.h"


#define BUFFER_SIZE 64*1024 //64K buffer size

enum {
    CSTATE_CLOSED,
    CSTATE_SYN_SENT,
    CSTATE_SYN_RECVD,
    CSTATE_ESTABLISHED,
    CSTATE_FIN_WAIT1,
    CSTSTE_FIN_WATI2,
    CSTATE_TIME_WAIT,
    CSTAET_CLOSE_WAIT,
    CSTATE_LAST_ACK
    };    /* you should have more states */

typedef struct
{
    char* head;
    char* tail;
    char* buffer;
    size_t size;
    size_t datasize;
    size_t freesize;
    
}cir_buffer;
static void init_buffer(cir_buffer *buf, size_t _size, size_t initpos){
    
    buf->buffer = malloc(_size*sizeof(char));
    
    assert(buf->head);
    buf->size = _size;
    buf->head = buf->buffer + initpos;
    buf->tail = buf->head;
    
    buf->datasize = 0;
    buf->freesize = _size;
    
}
static void free_buf(cir_buffer *buf)
{
    free(buf->buffer);
}

static size_t write_buffer(cir_buffer *buf, void* data, size_t datasize)
{
    assert(datasize>buf->freesize);
    
    if(datasize <= (buf->head - buf->buffer))//write data in regular
    {
        memcpy(buf->head, data, datasize);
        buf->head = buf->head + datasize;
    }
    else //write in circular way
    {
        size_t seg_size = buf->size -(buf->head - buf->buffer);
        memcpy(buf->head, data, seg_size);
        memcpy(buf->buffer, data+seg_size, datasize-seg_size);
        
        buf->head = buf->buffer + (datasize - seg_size);
    }
    
    buf->freesize-= datasize;
    buf->datasize+= datasize;
    return datasize;
    
}

static size_t read_buffer(cir_buffer *buf, tcp_seq seq, void* data, size_t datasize)
{
    assert(datasize > buf->datasize);
    
    size_t realpos = seq % buf->size;
    
    if((realpos + datasize) < buf->size)
    {
        memcpy(data, buf->buffer + realpos, datasize);
    }
    else
    {
        size_t seg_size = buf->size - realpos;
        memcpy(data, buf->buffer + realpos, seg_size);
        memcpy(data+seg_size, buf->buffer, datasize - seg_size);
    }
    
    return datasize;
    
}

static bool_t free_buffer(cir_buffer *buf, size_t freesize)
{
    assert(freesize > buf->datasize);
    if(freesize > (buf->size - (buf->tail-buf->buffer)))//free data regular
    {
        buf->tail+= freesize;
    }
    else //free data in circular way
    {
        buf->tail = buf->buffer +(freesize - (buf->size - (buf->tail-buf->buffer)));
    }
        
    buf->freesize+= freesize;
    buf->datasize-= freesize;
    return 1;
}



/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    tcp_seq next_seq;//send buffer next byte to send
    tcp_seq ack;//receive buffer next ack frame to ack the other side
    int slide_window;//TH_WIN  receive buffer free size of the other side
    
    cir_buffer* recv_buffer;//ZX
    cir_buffer* send_buffer;//LAN
    

    /* any other connection-wide global variables go here */
} context_t;

typedef struct
{
    STCPHeader header;
    char payload[STCP_MSS];
    ssize_t data_len;
} STCP_Packet;

static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);

/*
 */
static size_t transport_recv_fragment(mysocket_t sd, void* fragmentbuf, size_t maxlen){
    
    //receive a tcpheader from fragment
    size_t recvsize = stcp_network_recv(sd, fragmentbuf, maxlen);
    
    //find the read data len datalen = totallen - headerlen;
    STCPHeader * head = (STCPHeader *)fragmentbuf;
    
    //deal with endin problem
    //head->th_flags = ntohs(head->th_flags);
    head->th_seq = ntohl(head->th_seq);
    head->th_ack = ntohl(head->th_ack);
    head->th_win = ntohs(head->th_win);

    return recvsize;
}

/*
 */
static size_t transport_send_fragment(mysocket_t sd,int flag, void* fragmentbuf, size_t size, context_t *ctx){
    
    
    assert(size < sizeof(STCPHeader));
    //fillin a tcpheader from fragment
    STCPHeader * head = (STCPHeader *)fragmentbuf;
    
    //find the data len with min(sildewindow, MSS, );
    head->th_flags = flag;
    head->th_seq = ctx->next_seq;//unsent seq number
    head->th_ack = ctx->ack;//ctx->next_seq;
    
    //local recv buffer free size/ since no receive buffer then just == MSS is OK.
    head->th_win = MIN(ctx->recv_buffer->freesize, ctx->slide_window);
    
    //deal with endin problem
    //head->th_flags = htons(head->th_flags);
    head->th_seq = htonl(head->th_seq);
    head->th_ack = htonl(head->th_ack);
    head->th_win = htons(head->th_win);
    
    
    //FIXME while sent == size maybe needed
    size_t sent = stcp_network_send(sd, fragmentbuf, size);
    return sent;
}


static bool_t transport_3way_handshake(mysocket_t sd, context_t *ctx)//ZX
{
    mysock_context_t * sdctx = (mysock_context_t *)stcp_get_context(sd);
    
    if(sdctx->is_active)
    {
        STCPHeader head;
        //send out a SYN fragment
        transport_send_fragment(sd, TH_SYN, &head, sizeof(STCPHeader), ctx);
        ctx->connection_state = CSTATE_SYN_SENT;
        
        
        //wait for SYN+ACK
        unsigned int event;
        
        event = stcp_wait_for_event(sd, 0, NULL);
        if(!(event & NETWORK_DATA))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        //read the header from network
        size_t headsize = transport_recv_fragment(sd, &head, sizeof(STCPHeader));
        if(headsize != sizeof(STCPHeader))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        //if it is  SNY + ACK message
        if( head.th_flags != (TH_ACK|TH_SYN))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        ctx->ack = head.th_seq+1;
        ctx->next_seq = head.th_ack+1;
        ctx->slide_window = head.th_win;//receive buffer free size of the other side
        
        
        //send ACK back then finished
        transport_send_fragment(sd, TH_ACK, &head, sizeof(STCPHeader), ctx);
        
        stcp_unblock_application(sd);
        
        return 1;

    }
    if(!sdctx->is_active) //wait for SYN message
    {
        
        unsigned int event = stcp_wait_for_event(sd, 0, NULL);
        if(!(event & NETWORK_DATA))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        //read the header from network
        STCPHeader head;
        size_t headsize = transport_recv_fragment(sd, &head, sizeof(STCPHeader));
        if(headsize != sizeof(STCPHeader))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        //if it is  SYN message
        if( head.th_flags != TH_SYN)
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        ctx->ack = head.th_seq+1;
        ctx->slide_window = head.th_win;//receive buffer free size of the other side
        
        ctx->connection_state = CSTATE_SYN_RECVD;

        
        //send SYN ACK back then finished
        transport_send_fragment(sd, TH_ACK|TH_SYN, &head, sizeof(STCPHeader), ctx);
        
        event = stcp_wait_for_event(sd, 0, NULL);
        if(!(event & NETWORK_DATA))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        if( head.th_flags != TH_ACK)
        {
            errno = ECONNREFUSED;
            return 0;
        }

        headsize = transport_recv_fragment(sd, &head, sizeof(STCPHeader));
        if(headsize != sizeof(STCPHeader))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        //if it is  ACK confirmed message
        if( head.th_flags != TH_ACK)
        {
            errno = ECONNREFUSED;
            return 0;
        }
        
        ctx->ack = head.th_seq;
        ctx->next_seq = head.th_ack + 1;
        ctx->slide_window = head.th_win;

        
        return 1;
    }

    
    return 0;
}


static bool_t transport_2way_close(mysocket_t sd, context_t *ctx)//ZX
{
    
    return 0;
}

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);
    
 
    generate_initial_seq_num(ctx);
    ctx->next_seq = ctx->initial_sequence_num;
    ctx->slide_window = STCP_MSS;
    ctx->connection_state = CSTATE_CLOSED;
    //init all buffer

    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    
    if (!transport_3way_handshake(sd, ctx))
    {
        stcp_unblock_application(sd);
        free(ctx);
        return;
    }

    //init send and recv buffer here
    init_buffer(ctx->send_buffer, BUFFER_SIZE, ctx->initial_sequence_num);//FIXME
    init_buffer(ctx->recv_buffer, BUFFER_SIZE, ctx->initial_sequence_num);
    
    
    ctx->connection_state = CSTATE_ESTABLISHED;
    stcp_unblock_application(sd);

    control_loop(sd, ctx);

    /* do any cleanup here */
    free(ctx);
}



/* generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);

#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
#else
    ctx->initial_sequence_num = rand()%255;
    /* you have to fill this up */
    /*ctx->initial_sequence_num =;*/
#endif
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);
    assert(!ctx->done);
    
    char* cache;
    cache = malloc(STCP_MSS);
    
    if(cache == NULL)
    {
        printf("control_loop: cache memalloc is failed :%d", ctx->connection_state);
        errno = ECONNABORTED;
        return;
    }

    while (!ctx->done)
    {
        unsigned int event;
        
        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, 0, NULL);
        
        /* check whether it was the network, app, or a close request */
        if (event & APP_DATA)//LAN
        {
            
            if (ctx->connection_state != CSTATE_ESTABLISHED) {
                printf("APP_DATA: conn state is wrong:%d", ctx->connection_state);
                errno = ECONNABORTED;
            }
            
            /* FIXME Later if we use a real send buffer
            //get send buffer free size == freesize
            //alloc memory from main memory for cache
            //write cache data to send buffer with real data size
            */
            
            /* the application has requested that data be sent */
            size_t datasize = stcp_app_recv(sd, TCP_DATA_START(cache), STCP_MSS);
            
            //while datasize !=0, receive all appdata and send to network;
            while (datasize >0) {
                
                //send stcp_network_send(); simply just receive app data and send out
                transport_send_fragment(sd, TH_ACK, cache, datasize + sizeof(STCPHeader),ctx);
                datasize = stcp_app_recv(sd, cache+sizeof(STCPHeader), STCP_MSS);
            
            
            }//end of while
            
            
            stcp_unblock_application(sd);
            free(cache);

        }
        
        if(event & NETWORK_DATA)//ZX
        {
            if (ctx->connection_state != CSTATE_ESTABLISHED) {
                printf("NETWORK_DATA: conn state is wrong:%d", ctx->connection_state);
                errno = ECONNABORTED;
            }
            
            size_t recvsize = stcp_network_recv(sd, cache, STCP_MSS);
            
            //while (recvsize > 0) {
                //write recv buffer
                STCPHeader* head = (STCPHeader*) cache;
                ctx->ack = head->th_seq + (recvsize - sizeof(STCPHeader));
                ctx->next_seq = head->th_ack;
                ctx->slide_window = head->th_win;
                
                //if(app data len >0 , stcp_app_send;
                if (recvsize > sizeof(STCPHeader)) {
                    stcp_app_send(sd, TCP_DATA_START(cache), recvsize-sizeof(STCPHeader));
                    stcp_unblock_application(sd);
                }
                //recvsize = stcp_network_recv(sd, cache, STCP_MSS);
            //}
            
            //find out how many data I need send use this ack
            size_t acksize = 0;
            if (ctx->send_buffer->datasize > 0) {
                acksize = MIN(ctx->send_buffer->datasize, ctx->slide_window);
                read_buffer(ctx->send_buffer, ctx->next_seq, cache + sizeof(STCPHeader), acksize);
            }
            //send ACK to peer with data or not
            transport_send_fragment(sd, TH_ACK, cache, sizeof(STCPHeader) + acksize, ctx);

        }
        
        if(event & TIMEOUT)//ZX
        {
            size_t acksize = 0;

            switch (ctx->connection_state) {
                case CSTATE_ESTABLISHED:
                    
                    if (ctx->send_buffer->datasize > 0)
                    {
                        acksize = MIN(ctx->send_buffer->datasize, ctx->slide_window);
                        read_buffer(ctx->send_buffer, ctx->next_seq, TCP_DATA_START(cache), acksize);
                    }
                    //send ACK to peer with data or may not
                    transport_send_fragment(sd, TH_ACK, cache, sizeof(STCPHeader) + acksize, ctx);
                    break;
                    
                default:
                    printf("TIMEOUT: default is failed:%d", ctx->connection_state);
                    errno = ECONNABORTED;
                    break;
            }
            
           
            
        }

        if(event & APP_CLOSE_REQUESTED)//ZX
        {
            if (ctx->connection_state != CSTATE_ESTABLISHED) {
                printf("APP_CLOSE_REQUESTED: conn state is wrong :%d", ctx->connection_state);
                errno = ECONNABORTED;
            }
            if(!transport_2way_close(sd, ctx))
            {
                printf("APP_CLOSE_REQUESTED: Close down failed :%d", ctx->connection_state);
                errno = ECONNABORTED;
            }
            
            ctx->done = 1;
        }
        
        

        /* etc. */
    }
    free(cache);
}


/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}



