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


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    tcp_seq next_seq;//send buffer next byte to send
    tcp_seq ack;//receive buffer next ack frame to ack the other side
    int slide_window;//TH_WIN  receive buffer free size of the other side
    
    char* recv_buffer;//ZX
    char* send_buffer;//LAN
    

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
static ssize_t transport_recv_fragment(mysocket_t sd, void* fragmentbuf, ssize_t msslen){
    
    //receive a tcpheader from fragment
    
    
    //find the read data len datalen = totallen - headerlen;
    
    
    //cache the realdata in to cache, or !!!just send it to app layer!!!!
    
    
    //unblock the application
    return 0;
}

/*
 useless anymore
 */
static ssize_t transport_recv_head(mysocket_t sd, void* headbuf, ssize_t headlen)//LAN
{
    //use ntohl ntohs decode related header
    ssize_t result = stcp_network_recv(sd, headbuf, sizeof(struct tcphdr), NULL);
    headbuf.th_seq = ntohs(head.th_seq);
    headbuf.th_ack = ntohs(head.th_ack);
    headbuf.th_win = ntohs(head.th_win);
    return sizeof(headbuf);
}
/*
 useless anymore
 */
static ssize_t transport_recv_data(mysocket_t sd, void* buf, ssize_t headlen)//LAN
{
    ssize_t result = stcp_network_recv(sd, recv_buffer, sizeof(recv_buffer), NULL);
    return result;//returns the actual amount of data read into recv_buffer
    //send real app data or stub data 
}

/*
 */
static void transport_send_head(mysocket_t sd, int hflag, ssize_t datalen, context_t* ctx)//LAN
{
    struct tcphdr head;
    //fill a ACK header
    head.th_flags = hflag;
    head.th_seq = ctx->next_seq;//unsent seq number
    head.th_ack = ctx->ack;//ctx->next_seq;
    head.th_win = STCP_MSS;//local recv buffer free size/ since no receive buffer then just == MSS is OK.
    head.th_seq = htons(head.th_seq);
    head.th_ack = htons(head.th_ack);
    head.th_win = htons(head.th_win);
    ssize_t result = stcp_network_send(sd, send_buffer, sizeof(struct tcphdr), NULL);
    //use htonl htons to codec
    
}

/*
*/
static void transport_send_data(mysocket_t sd, char* data, ssize_t datalen, context_t* ctx)//LAN
{
    strncpy(send_buffer, data);
    ctx.next_seq += len;
    ssize_t result = stcp_network_send(sd, send_buffer, sizeof(send_buffer), NULL);
    //send real app data or stub data   
    
}


static bool_t transport_3way_handshake(mysocket_t sd, context_t *ctx)//ZX
{
    mysock_context_t * sdctx = (mysock_context_t *)stcp_get_context(sd);
    
    if(sdctx->is_active)
    {
        transport_send(sd, NULL, 0, TH_SYN, ctx);
        ctx->connection_state = CSTATE_SYN_SENT;
        
        
        //wait for SYN+ACK
        unsigned int event;
        
        event= stcp_wait_for_event(sd, 0, NULL);
        if(!(event & NETWORK_DATA))
        {
            errno = ECONNREFUSED;
            return 0;
        }
        //read the header from network
        struct tcphdr head;
        int headsize = transport_recv_fragment(sd, &head, sizeof(struct tcphdr));
        if(headsize != sizeof(struct tcphdr))
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
        
        ctx->ack = head.th_seq;
        ctx->next_seq = head.th_ack;
        ctx->slide_window = head.th_win;//receive buffer free size of the other side
        
        
        //send ACK back then finished
        transport_send_head(sd, &head, TH_ACK, ctx);
        
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
        struct tcphdr head;
        int headsize = transport_recv_head(sd, &head, sizeof(struct tcphdr));
        if(headsize != sizeof(struct tcphdr))
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
        
        ctx->ack = head.th_seq;
        ctx->next_seq = head.th_ack;
        ctx->slide_window = head.th_win;//receive buffer free size of the other side
        
        ctx->connection_state = CSTATE_SYN_RECVD;

        
        //fill a ACK header
        head.th_seq = ctx->next_seq;
        head.th_ack = ctx->ack;
        head.th_win = MSS//local receive buffer free size;
        
        //send ACK back then finished
        transport_send_head(sd, &head, TH_ACK, ctx);
        
        
        headsize = transport_recv_head(sd, &head, sizeof(struct tcphdr));
        if(headsize != sizeof(struct tcphdr))
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
        
        ctx->ack = head.th_ack;
        ctx->next_seq = head.th_seq + 1;
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
                //error FIXME
                errno = 0;
                return;
            }
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            
            //get send buffer free size == freesize
            char[3076] buff= {0};
            size_t datalen = stcp_app_recv(sd, buff, freesize);

            SendBuff.write(buff, datalen);
            
            //write to send buffer( min (send buffer free size, ))
            
            
            //stcp_unblock_application(sd);
            
            //send stcp_network_send();
            
            //transport send head
            
            //transport send data
        }
        
        if(event & NETWORK_DATA)//ZX
        {
            if (ctx->connection_state != CSTATE_ESTABLISHED) {
                //error FIXME
                errno = 0;
                return;
            }
            //stcp_network_recv();
            //write recv buffer
            
            //recv_window = TH_WIN
            
            //generate local free recv buf;
            
            //send ACK to peer
            
            //if(buff is full), stcp_app_send; stcp_unblock_application(sd);
        
        }
        
        if(event & TIMEOUT)//ZX
        {
            //buff is full send stcp_network_send();
            
            switch (ctx->connection_state) {
                case CSTATE_ESTABLISHED:
                    //if send buffer data size is not zero
                    //send a max data out
                    break;
                    
                default:
                    break;
            }
            
        }

        if(event & APP_CLOSE_REQUESTED)//ZX
        {
            if (ctx->connection_state != CSTATE_ESTABLISHED) {
                //error FIXME
                return;
            }
            if(!transport_2way_close(sd, ctx))
            {
                errno = 0;//FIXME
            }
        }
        
        
        
        /* etc. */
    }
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



