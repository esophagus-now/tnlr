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
#include <unistd.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "enums.h"
#include "common.h"

void fwd_tunnel_read_cb(struct bufferevent *bev, void *arg) 
#ifdef IMPLEMENT
{
    tunnel_t *tn = (tunnel_t *) arg;

    puts("Got data from local endpoint, forwarding through fwd tunnel");
    
    //I hope this works!!!!!!
    struct evbuffer *in_buf = bufferevent_get_input(bev);
    uint32_t msg_type = htonl(TNLR_MSG_TUNNEL_DATA);
    uint32_t msg_len = htonl(4 + evbuffer_get_length(in_buf));
    uint32_t id = htonl(tn->id);
    bufferevent_write(tn->parent->ev, &msg_type, 4);
    bufferevent_write(tn->parent->ev, &msg_len, 4);
    bufferevent_write(tn->parent->ev, &id, 4);
    bufferevent_write_buffer(tn->parent->ev, in_buf);
}
#else
;
#endif

void fwd_tunnel_event_cb(struct bufferevent *bev, short what, void *arg) 
#ifdef IMPLEMENT
{
    tunnel_t *tn = (tunnel_t *) arg;

    if (what & BEV_EVENT_ERROR) {
        printf(
            "Unknown error on forward tunnel on local port [%d]\n", tn->local_port_native
        );
        //This isn't blocking.... right?
        bufferevent_free(bev);
        tn->local_ev = NULL;
        tn->status = TNLR_ERROR;

        uint32_t msg_type = htonl(TNLR_MSG_CLOSE_TUNNEL);
        uint32_t msg_len = htonl(4);
        uint32_t id = htonl(tn->id);

        bufferevent_write(tn->parent->ev, &msg_type, 4);
        bufferevent_write(tn->parent->ev, &msg_len, 4);
        bufferevent_write(tn->parent->ev, &id, 4);
    } else if (what & BEV_EVENT_EOF) {
        assert("Sorry, need to implement reopening tunnels automatically"); //TODO: implement
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

    //Get rid of this accepting socket and exchange for the socket
    //connection to our local endpoint
    //TODO? Hang onto info about local endpoint? For now we don't need it
    int sfd = accept(tn->fd, NULL, NULL);
    assert((sfd >= 0) && "Something went wrong with accept");
    close(tn->fd);
    tn->fd = sfd;

    tn->local_ev = bufferevent_socket_new(eb, sfd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(
        tn->local_ev, 
        &fwd_tunnel_read_cb, NULL, &fwd_tunnel_event_cb, 
        tn
    );
    
    //We can only enable the local bufferevent that reads data from
    //the local endpoint and forwards it to the remote host once we
    //receive an OPEN_TUNNEL_RESPONSE. So for now, there is nothing
    //to do yet with tn->local_ev
    
    int len = strlen(tn->remote_host);
    tcpconn_t *tc = tn->parent;
    
    //Now that our local endpoint is connected, let's request that the
    //remote endpoint makes a connection
    uint32_t msg_type = htonl(TNLR_MSG_OPEN_TUNNEL);
    uint32_t msg_len = htonl(6 + len);
    uint16_t remote_port = htons(tn->remote_port_native);
    uint32_t id = htonl(tn->id);

    bufferevent_write(tc->ev, &msg_type, 4);
    bufferevent_write(tc->ev, &msg_len, 4);
    bufferevent_write(tc->ev, &id, 4);
    bufferevent_write(tc->ev, &remote_port, 2);
    bufferevent_write(tc->ev, tn->remote_host, len);
}
#else
;
#endif

#endif