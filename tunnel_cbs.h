//This is my standard method now of putting code into other files.
//Ever since I learned this trick to get around needing to have both
//header and C files, I never want to go back to the old way
#ifdef IMPLEMENT
	#ifndef TUNNEL_CBS_H_IMPLEMENTED
		#define SHOULD_INCLUDE 1
		#define TUNNEL_CBS_H_IMPLEMENTED 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef TUNNEL_CBS_H
		#define SHOULD_INCLUDE 1
		#define TUNNEL_CBS_H 1
	#else 
		#define SHOULD_INCLUDE 0
	#endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE

#ifdef IMPLEMENT
#undef IMPLEMENT
#include "tunnel_cbs.h"
#define IMPLEMENT 1
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "enums.h"
#include "common.h"

void tunnel_read_cb(struct bufferevent *bev, void *arg) 
#ifdef IMPLEMENT
{
    tunnel_t *tn = (tunnel_t *) arg;

    puts("Got data from local endpoint, forwarding through tunnel");
    
    //I hope this works!!!!!!
    struct evbuffer *in_buf = bufferevent_get_input(bev);
    uint32_t msg_type = htonl(TNLR_MSG_TUNNEL_DATA);
    uint32_t msg_len = htonl(4 + evbuffer_get_length(in_buf));
    uint32_t id = htonl(tn->id ^ 0x80000000); //Tunnels always have opposite MSB on other host);
    bufferevent_write(tn->parent->ev, &msg_type, 4);
    bufferevent_write(tn->parent->ev, &msg_len, 4);
    bufferevent_write(tn->parent->ev, &id, 4);
    bufferevent_write_buffer(tn->parent->ev, in_buf); //I hope this clears the buffer. The docs are a bit vague on that point
}
#else
;
#endif

void tunnel_event_cb(struct bufferevent *bev, short what, void *arg) 
#ifdef IMPLEMENT
{
    tunnel_t *tn = (tunnel_t *) arg;

    if (what & BEV_EVENT_ERROR) {
        printf(
            "Error on forward tunnel on local port [%d]: [%s]\n", 
            tn->local_port_native,
            evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())
        );
        //This isn't blocking.... right?
        bufferevent_free(bev);
        tn->local_ev = NULL;
        tn->status = TNLR_ERROR;

        uint32_t msg_type = htonl(TNLR_MSG_CLOSE_TUNNEL);
        uint32_t msg_len = htonl(4);
        uint32_t id = htonl(tn->id ^ 0x80000000); //Tunnels always have opposite MSB on other host

        bufferevent_write(tn->parent->ev, &msg_type, 4);
        bufferevent_write(tn->parent->ev, &msg_len, 4);
        bufferevent_write(tn->parent->ev, &id, 4);
    } else if (what & BEV_EVENT_EOF) {
        assert("Sorry, need to implement reopening tunnels automatically"); //TODO: implement
    } else {
        assert("???");
    }
}
#else
;
#endif

void fwd_tunnel_accept_cb(int fd, short what, void *arg) 
#ifdef IMPLEMENT
{
    tunnel_t *tn = (tunnel_t *) arg;

    if (tn->local_accept_ev) event_free(tn->local_accept_ev);
    tn->local_accept_ev = NULL;
        
    if (what & EV_TIMEOUT) {
        close(tn->fd);
        tn->status = TNLR_ERROR;
        printf(
            "Timed out waiting for local client to connect to forward "
            "tunnel on local port [%d]\n",
            tn->local_port_native
        );
        return;
    }

    puts("Accepted on our new fwd tunnel.");

    //Get rid of this accepting socket and exchange for the socket
    //connection to our local endpoint
    //TODO? Hang onto info about local endpoint? For now we don't need it
    printf("Debug: tn->fd = [%d]\n", tn->fd);
    int sfd = accept(tn->fd, NULL, NULL);
    if (sfd < 0) {
        printf("Could not accept connection for forward tunnel: [%s]\n", strerror(errno));
        close(tn->fd);
        tn->fd = -1;
        puts("Giving up on this tunnel");
        tn->status = TNLR_ERROR;
    }
    close(tn->fd);
    tn->fd = sfd;
    
    //We can only enable the local bufferevent that reads data from
    //the local endpoint and forwards it to the remote host once we
    //receive an OPEN_TUNNEL_RESPONSE. So for now, there is nothing
    //to do yet with tn->local_ev
    tn->local_ev = NULL;
    
    int len = strlen(tn->remote_host);
    tcpconn_t *tc = tn->parent;
    
    //Now that our local endpoint is connected, let's request that the
    //remote endpoint makes a connection
    uint32_t msg_type = htonl(TNLR_MSG_OPEN_TUNNEL);
    uint32_t msg_len = htonl(6 + len);
    uint16_t remote_port = htons(tn->remote_port_native);
    uint32_t id = htonl(tn->id ^ 0x80000000); //Tunnels always have opposite MSB on other host

    bufferevent_write(tc->ev, &msg_type, 4);
    bufferevent_write(tc->ev, &msg_len, 4);
    bufferevent_write(tc->ev, &id, 4);
    bufferevent_write(tc->ev, &remote_port, 2);
    bufferevent_write(tc->ev, tn->remote_host, len);
}
#else
;
#endif


void fwd_tunnel_connect_cb(struct bufferevent *bev, short what, void *arg) 
#ifdef IMPLEMENT
{
    tunnel_t *tn = (tunnel_t *) arg;
    uint32_t response = 0;

    if (what & BEV_EVENT_CONNECTED) {
        puts("Fordward tunnel opened!!!!!!!!!!!!!");
        bufferevent_enable(bev, EV_READ | EV_WRITE);
        tn->status = TNLR_CONNECTED;
        //No need for these timeouts anymore
        bufferevent_set_timeouts(bev, NULL, NULL);

        //Send successful response
        response = 0; //0 = success, I guess
    } else if (what & BEV_EVENT_ERROR) {
        printf(
            "Error on forward tunnel connection to local port [%d]: [%s]\n", 
            tn->local_port_native,
            evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR())
        );
        //This isn't blocking.... right?
        bufferevent_free(bev);
        tn->local_ev = NULL;
        tn->status = TNLR_ERROR;

        response = -10; //TODO: need some kind of enum for this?
    } else {
        assert("Impossible");
    }
    
    uint32_t msg[4];
    msg[0] = htonl(TNLR_MSG_OPEN_TUNNEL_RESPONSE);
    msg[1] = htonl(8);
    msg[2] = htonl(tn->id ^ 0x80000000); //Tunnels always have opposite MSB on other host
    msg[3] = htonl(response); 
    bufferevent_write(tn->parent->ev, msg, sizeof(msg));
}
#else
;
#endif

#endif