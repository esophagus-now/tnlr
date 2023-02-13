//This is my standard method now of putting code into other files.
//Ever since I learned this trick to get around needing to have both
//header and C files, I never want to go back to the old way
#ifdef IMPLEMENT
	#ifndef TCPCONN_CBS_H_IMPLEMENTED
		#define SHOULD_INCLUDE 1
		#define TCPCONN_CBS_H_IMPLEMENTED 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef TCPCONN_CBS_H
		#define SHOULD_INCLUDE 1
		#define TCPCONN_CBS_H 1
	#else 
		#define SHOULD_INCLUDE 0
	#endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE

#ifdef IMPLEMENT
#undef IMPLEMENT
#include "tcpconn_cbs.h"
#define IMPLEMENT 1
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "enums.h"
#include "common.h"

void tcpconn_connect_cb(struct bufferevent *bev, short what, void *arg) 
#ifdef IMPLEMENT
{
    tcpconn_t *tc = (tcpconn_t *) arg;

    if (what & BEV_EVENT_CONNECTED) {
        puts("Socket opened!!!!!!!!!!!!!");
        bufferevent_enable(bev, EV_READ);
        tc->status = TNLR_CONNECTED;
        tc->state = TCPCONN_READ_HEADER;
        //No need for these timeouts anymore
        bufferevent_set_timeouts(bev, NULL, NULL);
    } else {
        puts("Connnection timed out. Try again?");
        //This isn't blocking.... right?
        bufferevent_free(bev);
        tc->ev = NULL;
        tc->status = TNLR_ERROR;
    }
}
#else
;
#endif

void tcpconn_read_cb(struct bufferevent *bev, void *arg)
#ifdef IMPLEMENT
{
    puts("Entered read callback");
    
    tcpconn_t *tc = (tcpconn_t *) arg;

    struct evbuffer *buf = bufferevent_get_input(bev);
    
    if (tc->state == TCPCONN_READ_HEADER) {
        puts("Attempting to read header");
        //Pullup, taking care to return if not enough data
        //If zero data, go straight to TCPCONN_EXEC_MSG,
        //otherwise go to TCPCONN_READ_DATA
        unsigned char *mem = evbuffer_pullup(buf, 8);
    
        if (mem == NULL) {
            puts("Not enough header data");
            /* Not enough data in the buffer */
            return;
        } else {
            uint32_t tmp;
            memcpy(&tmp, mem, 4);
            tc->msg_type = ntohl(tmp);
            printf("Message type = %d\n", tc->msg_type);
            memcpy(&tmp, mem + 4, 4);
            tc->data_sz = ntohl(tmp);
            printf("Data size = %d\n", tc->data_sz);
            /* Actually remove the data from the buffer now that we know we
               like it. */
            evbuffer_drain(buf, 8);
            tc->state = TCPCONN_READ_DATA;
            tc->data_read_so_far = 0;
        }
    } 
    
    if (tc->state == TCPCONN_READ_DATA) {
        puts("Attempting to read data");
        //Try to fill the buffer. If buffer gets filled, we
        //set the state to TCPCONN_EXEC_MSG. Otherwise we 
        //return and wait for more data
        int n = evbuffer_remove_buffer(
            buf, 
            tc->msg_data, 
            tc->data_sz - tc->data_read_so_far
        );
        assert(n>=0);
        tc->data_read_so_far += n;
        printf("Read %d bytes. Total = %d, target = %d\n", n, tc->data_read_so_far, tc->data_sz);
        assert(tc->data_read_so_far <= tc->data_sz);
        if (tc->data_read_so_far == tc->data_sz) {
            tc->state = TCPCONN_EXEC_MSG;
        }
    }

    if (tc->state == TCPCONN_EXEC_MSG) {
        //TODO: clean this up so it isn't a hundreds-of-lines-long switch statement
        puts("Executing a message");
        //The tc->msg_type and tc->msg_data have been filled. Now
        //we do what they tell us to do
        msg_type_e tp = (msg_type_e) tc->msg_type;
        switch (tp) {
        case TNLR_MSG_DBG_MSG: {
            char *printme = (char *) malloc(tc->data_sz + 1); //+1 for NUL
            int rc = evbuffer_copyout(tc->msg_data, printme, tc->data_sz);
            printf("Copied out %d bytes\n", rc);
            printme[tc->data_sz] = 0;
            printf("DBG_MSG:\n\e[35m%s\e[39m\n", printme);
            free(printme);
            break;
        }

        case TNLR_MSG_CLOSE:
            puts("Got a CLOSE message");
            //TODO: close all tunnels before freeing the tcpconn
            if (tc->lua_refcount == 0) tcpconn_free(tc);
            else {
                tc->status = TNLR_CLOSED;
                bufferevent_free(tc->ev);
                tc->ev = NULL;
            }
            break;

        case TNLR_MSG_OPEN_TUNNEL: {
            char *data = (char *) malloc(tc->data_sz + 1); //+1 for NUL
            int rc = evbuffer_copyout(tc->msg_data, data, tc->data_sz);
            assert(rc >= 0);
            data[tc->data_sz] = 0;
            uint32_t id = ntohl(*(uint32_t*)data);
            assert(!(id&0x80000000) && "Incoming ID too big");
            uint16_t local_port_native = ntohs(*(uint16_t*)(data+4));
            char *local_host = strdup((char*) data + 6);
            free(data);

            int response_code = 0;
            
            int sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); //Nonblocking TCP socket
            if (sfd < 0) {
                printf("Could not open socket: [%s]\n", strerror(errno));
                response_code = -2;
                goto open_tunnel_error_response;
            }
            if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
                printf("setsockopt(SO_REUSEADDR) failed\n");
                response_code = -3;
                goto open_tunnel_error_response;
            }
            if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0) {
                printf("setsockopt(SO_REUSEPORT) failed\n");
                response_code = -4;
                goto open_tunnel_error_response;
            }

            { //Anonymous braces to prevent goto problem
            struct bufferevent *bev = bufferevent_socket_new(eb, sfd, BEV_OPT_CLOSE_ON_FREE);
            assert(bev && "Oops, out of memory");
            
            tunnel_t *tn = (tunnel_t *) calloc(1, sizeof(tunnel_t));
            assert(tn && "Oops out of memory");

            tn->id = id;
            tn->parent = tc;
            tn->fd = sfd;
            tn->is_forward_tunnel = 1;
            tn->status = TNLR_CONNECTING;
            tn->local_host = local_host;
            tn->local_port_native = local_port_native;
            //FIXME: should try to get remote port so we can detect redundant tunnels
            tn->local_ev = bev;
            tn->next = tc->tunnels.next;
            tn->prev = &tc->tunnels;
            tc->tunnels.next = tn;
            
            //CONTINUE 
            //(this is where I left off)
            //Need to launch the bufferevent connect, which means I need all
            //the proper callbacks...
            bufferevent_setcb(bev, NULL, NULL, NULL, tn); //TODO: implement callbacks
            struct timeval timeout = {30, 0}; //Allow longer timeout for when creating a tunnel
            bufferevent_set_timeouts(bev, &timeout, &timeout);
            
            struct sockaddr_in local_addr;
            local_addr.sin_family = AF_INET;
            int rc = inet_pton(AF_INET, local_host, &local_addr.sin_addr);
            
            if (rc != 1) {
                printf("Could not parse [%s] as an IPv4 address\n", local_host);
                bufferevent_free(bev);
                response_code = -1;
                goto open_tunnel_error_response;
            }
            
            bufferevent_socket_connect(
                bev, 
                (struct sockaddr*) &local_addr, 
                sizeof(struct sockaddr_in)
            );
            } //End anonymous braces
            
            break;
            
            open_tunnel_error_response:;
            if (sfd >= 0) close(sfd);
            free(local_host);
            uint32_t msg[4];
            msg[0] = htonl(TNLR_MSG_OPEN_TUNNEL_RESPONSE);
            msg[1] = htonl(8);
            msg[2] = htonl(id);
            msg[3] = htonl(response_code);
            bufferevent_write(tc->ev, msg, sizeof(msg));
        }
                
        case TNLR_MSG_TUNNEL_DATA: {
            uint32_t *data = (uint32_t *) evbuffer_pullup(tc->msg_data, 4);
            uint32_t id = ntohl(*data);
            //FIXME: figure out this "upper bit" rule... I think we need another
            //field in the tunnel or something
            evbuffer_drain(tc->msg_data, 4);
            struct bufferevent *bev = NULL;
            //TODO: could use hash table, but for now linear search is AOK
            for (tunnel_t *cur = tc->tunnels.next; cur != &tc->tunnels; tc=tc->next) {
                if (cur->id == id) {
                    bev = cur->local_ev;
                    break;
                }
            }
            if(bev) bufferevent_write_buffer(bev, tc->msg_data); //This clears the data, right?
            else puts("Warning, dropping data for unknown tunnel");

            //Because we already cleared the data, just set tc->data_sz to 0
            //so that the common code doesn't do anything bad
            tc->data_sz = 0;
            break; 
        }
            
        default:
            fprintf(stderr, "Unknown message type [%d]\n", tc->msg_type);
            break;
        }
        
        //Clear the data buffer
        printf("Draining %d bytes\n", tc->data_sz);
        evbuffer_drain(tc->msg_data, tc->data_sz);
        tc->state = TCPCONN_READ_HEADER;
    }
}
#else
;
#endif

#endif