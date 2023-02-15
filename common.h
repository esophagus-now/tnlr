//This is my standard method now of putting code into other files.
//Ever since I learned this trick to get around needing to have both
//header and C files, I never want to go back to the old way
#ifdef IMPLEMENT
	#ifndef COMMON_H_IMPLEMENTED
		#define SHOULD_INCLUDE 1
		#define COMMON_H_IMPLEMENTED 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef COMMON_H
		#define SHOULD_INCLUDE 1
		#define COMMON_H 1
	#else 
		#define SHOULD_INCLUDE 0
	#endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE

#ifdef IMPLEMENT
#undef IMPLEMENT
#include "common.h"
#define IMPLEMENT 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include <lua.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#include "enums.h"
#include "os_common.h"

#ifndef IMPLEMENT
struct _tcpconn_t;
typedef struct _tcpconn_t tcpconn_t;

typedef struct _tunnel_t {
    uint32_t id;
    int lua_refcount;
    tcpconn_t *parent;
    sockfd fd;
    int is_forward_tunnel;
    status_e status;
    char *local_host;
    uint16_t local_port_native;
    char *remote_host;
    uint16_t remote_port_native;
    //For forward tunnels, we first start an async accept, which
    //we wait for using a regular event. Then, once the listener
    //socket accepts a connection, we open a bufferevent on the
    //new socket (and get rid of the old listener event). For
    //reverse tunnels, we just directly create a bufferevent when
    //we connect() to the local endpoint
    struct event *local_accept_ev;
    struct bufferevent *local_ev;
    struct _tunnel_t *prev, *next;
} tunnel_t;

struct _tcpconn_t {
    int lua_refcount;
    uint32_t next_tunnel_id;

    sockfd fd;
    status_e status;

    uint16_t local_port_native;
    char *remote_host;
    uint16_t remote_port_native;

    struct bufferevent *ev;

    tcpconn_state_e state;
    uint32_t msg_type; //Valid when we are in TCPCONN_READ_DATA or TCPCONN_EXEC_MSG
    uint32_t data_sz; //Valid when we are in TCPCONN_READ_DATA or TCPCONN_EXEC_MSG
                      //Tells us how big the data is supposed to be (not how much
                      //we've currently read)

    //For better performance, we'll use this "move-semantics" evbuffer
    struct evbuffer *msg_data; //Is a valid (intialized) object when we are in the
                               //TCPCONN_READ_DATA state, but is potentially incomplete.
                               //We can only use the data once we reach the 
                               //TCPCONN_EXEC_MSG state
    uint32_t data_read_so_far; //Just for convenience

    //Head sentinel for tunnel linked list
    tunnel_t tunnels;

    struct _tcpconn_t *prev, *next;
};


extern tcpconn_t tcpconns; //Global list of open tcpconns. So sue me.
extern struct event_base *eb; //Global event_base. So sue me.
#endif

#ifdef IMPLEMENT
static void tcpconn_free_tunnel_lists(tcpconn_t *tc) {
    tunnel_t *curr = tc->tunnels.next;
    while (curr != &tc->tunnels) {
        curr = curr->next; //Important to do this before freeing
        tunnel_free(curr);
    }
}
#endif

void tcpconn_free(tcpconn_t *tc) 
#ifdef IMPLEMENT
{
    puts("Really freeing a tcpconn");
    //Gracefully ignore NULL input
    if (!tc) return;

    //Prevent shooting ourselves in the foot
    assert(tc != &tcpconns);

    //Also make sure we don't free a tcpconn that
    //still has lua references
    assert(tc->lua_refcount == 0);
    
    //Remove this tc from the linked list
    tc->prev->next = tc->next;
    tc->next->prev = tc->prev;

    tcpconn_free_tunnel_lists(tc);

    evbuffer_free(tc->msg_data);
    
    //Free all the malloc'ed fields within this tc
    free(tc->remote_host);
    if(tc->ev) bufferevent_free(tc->ev);

    free(tc);
}
#else
;
#endif

void tunnel_free(tunnel_t *t) 
#ifdef IMPLEMENT
{
    puts("Freeing a tunnel for real");
    //Gracefully ignore NULL input
    if (!t) return;

    //Assert that the tunnel is in a finished state. If it isn't, it
    //means I screwed something up elsewhere in the code (in other 
    //words, this assertion is to enforce a design assumption)
    assert(t->status == TNLR_CLOSED || t->status == TNLR_ERROR);

    //Assert that no tunnels have lua references. This is enforcing a
    //design accumption that I will prevent freeing a tcpconn until
    //all the tunnels are unreferenced
    assert(t->lua_refcount == 0);

    //Free fields
    if (t->remote_host) free(t->remote_host);
    if (t->local_host) free(t->local_host);
    if (t->local_ev) bufferevent_free(t->local_ev);
    if (t->local_accept_ev) event_free(t->local_accept_ev);
    
    //Remove from linked-list
    t->prev->next = t->next;
    t->next->prev = t->prev;
    
    //Free the tunnel
    free(t);
}
#else
;
#endif

tunnel_t *find_tunnel(tcpconn_t *tc, uint32_t id)
#ifdef IMPLEMENT
{
    assert(tc);
    
    //TODO: could use hash table, but for now linear search is OK
    for (tunnel_t *cur = tc->tunnels.next; cur != &tc->tunnels; cur=cur->next) {
        if (cur->id == id) return cur;
    }

    return NULL;
}
#else
;
#endif

#else
#undef SHOULD_INCLUDE
#endif