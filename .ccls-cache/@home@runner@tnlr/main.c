/*
Breadcrumbs trail:
- In the middle of implementing tunnels (forward only rn)
- I need to implement the OPEN_TUNNEL_RESPONSE message handler
  1. Should create a new tunnel object and hook it up in the
     tcpconn's tunnel list, taking care to use the correct ID
     -> Problem... what to do about ID collisions between
        tunnels created on one host vs the other?
     -> Maybe I can just add a field to the tunnel struct to
        distinguish which person picked the ID. That's basically
        like having a 33-bit ID where the top bit is different
        based on the host, so no collisions
  2. Set up a bufferevent that tries to connect() to the desired
     endpoint. Should work in pretty much the same way that the
     tcpconn connections work.
  3. Depending on result of connect() call, send back appropriate
     OPEN_TUNNEL_RESPONSE
- Need to implement close function
- close message implementation looks iffy to me
- In general my teardown logic is kinda bad. I left TODOs in the
  places that need attention
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#define TNLR_STATUS_IDENTS \
    X(CONNECTING),    \
    X(CONNECTED),     \
    X(ERROR),         \
    X(CLOSED) 

typedef enum {
    #define X(x) TNLR_##x
    TNLR_STATUS_IDENTS
    #undef X
} status_e;

char const * const TNLR_STATUS_STRINGS[] = {
    #define X(x) #x
    TNLR_STATUS_IDENTS
    #undef X
};

#define TNLR_MSG_TYPE_IDENTS \
    X(DBG_MSG), \
    X(CLOSE), \
    X(OPEN_TUNNEL), \
    X(OPEN_REVERSE_TUNNEL), \
    X(OPEN_TUNNEL_RESPONSE), \
    X(TUNNEL_DATA), \
    X(CLOSE_TUNNEL)

typedef enum {
    #define X(x) TNLR_MSG_##x
    TNLR_MSG_TYPE_IDENTS
    #undef X
} msg_type_e;

char const *const TNLR_MSG_TYPE_STRINGS[] = {
    #define X(x) #x
    TNLR_MSG_TYPE_IDENTS
    #undef X
};

struct _tcpconn_t;
typedef struct _tcpconn_t tcpconn_t;

typedef struct _tunnel_t {
    uint32_t id;
    int lua_refcount;
    tcpconn_t *parent;
    int fd;
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

typedef enum {
    TCPCONN_READ_HEADER,
    TCPCONN_READ_DATA,
    TCPCONN_EXEC_MSG //Header+data read, now we do what it says
} tcpconn_state_e;

struct _tcpconn_t {
    int lua_refcount;
    uint32_t next_tunnel_id;

    int fd;
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
    
    tunnel_t tunnels_head;
    struct _tcpconn_t *prev, *next;
} tcpconn_head; //Global list of open tcpconns. So sue me.

lua_State *L; //Global lua state. So sue me.
struct event_base *eb; //Global event_base. So sue me.

void connect_cb(struct bufferevent *bev, short what, void *arg) {
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

void fwd_tunnel_read_cb(struct bufferevent *bev, void *arg) {
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

void fwd_tunnel_event_cb(struct bufferevent *bev, short what, void *arg) {
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

void fwd_tunnel_accept_cb(int fd, short what, void *arg) {
    tunnel_t *tn = (tunnel_t *) arg;

    if (tn->local_accept_ev) event_free(tn->local_accept_ev);
    tn->local_accept_ev = NULL;
        
    if (what & EV_TIMEOUT) {
        close(tn->fd);
        tn->status = TNLR_ERROR;
        luaL_error(
            L, 
            "Timed out waiting for local client to connect to forward tunnel on local port [%d]",
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

void tcpconn_free(tcpconn_t *tc);
void tcpconn_read_cb(struct bufferevent *bev, void *arg) {
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
        puts("Executing a message");
        //The tc->msg_type and tc->msg_data have been filled. Now
        //we do what they tell us to do
        msg_type_e tp = tc->msg_type;
        switch (tp) {
        case TNLR_MSG_DBG_MSG: {
            char *printme = malloc(tc->data_sz + 1); //+1 for NUL
            int rc = evbuffer_copyout(tc->msg_data, printme, tc->data_sz);
            printf("Copied out %d bytes\n", rc);
            printme[tc->data_sz] = 0;
            printf("DBG_MSG:\n\e[35m%s\e[39m\n", printme);
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
            return;

        //TODO: TNLR_MSG_OPEN_TUNNEL
        //TODO: TNLR_MSG_TUNNEL_DATA
            
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

void push_tcpconn(tcpconn_t *tc) {
    tcpconn_t **ret = (tcpconn_t **) lua_newuserdata(L, sizeof(tcpconn_t*));
    assert(ret && "oops out of memory");
    luaL_setmetatable(L, "tcpconn");
    *ret = tc;
    tc->lua_refcount++;
    printf("lua_refcount is now %d\n", tc->lua_refcount);
}

//i.e. push a tunnel_t userdata
void push_tunnel(tunnel_t *tn) {
    tunnel_t **ret = (tunnel_t **) lua_newuserdata(L, sizeof(tunnel_t*));
    assert(ret && "oops out of memory");
    luaL_setmetatable(L, "tunnel");
    *ret = tn;
    tn->lua_refcount++;
    printf("tunnel lua_refcount is now %d\n", tn->lua_refcount);
}

int tnlr_connect(lua_State *L) {
    /*
    Check if a connection is already open to the desired endpoint.
    If so, just return that, taking care to increment the refcount.
    Otherwise, alloc a new tcpconn object with status CONNECTIING.
    Create a new socket with O_NONBLOCK and bind it to the given
    local_port (if that was requested). By the way, we will check
    for errors everywhere. After that we will parse the host and
    port into the right format for the socket, and issue the connect
    call. Finally, we hook a up libevent event so that a callback
    will update the tcpconn's status once the connect finishes
    */

    char const *remote_host = luaL_checkstring(L, 1);
    uint16_t remote_port_native = 23232;
    uint16_t local_port_native = 23232;
    
    int tp = lua_type(L, 2);
    if (tp != LUA_TNONE) {
        lua_Integer port = luaL_checkinteger(L, 2);
        if (port < 0 || port > 65535) {
            luaL_error(L, "Remote port %d out of range(0-65535)", port);
        }
        remote_port_native = port;
    }
    tp = lua_type(L, 3);
    if (tp != LUA_TNONE) {
        lua_Integer port = luaL_checkinteger(L, 3);
        if (port < 0 || port > 65535) {
            luaL_error(L, "Local port %d out of range(0-65535)", port);
        }
        local_port_native = port;
    }
    
    tcpconn_t *curr = tcpconn_head.next;
    while (curr != &tcpconn_head) {
        if (
            !strcmp(remote_host, curr->remote_host) && 
            (remote_port_native == curr->remote_port_native)
        ) {
            //If the status is not closed, then this connection is
            //already open. Print a warning and return early
            if (
                (curr->status == TNLR_CONNECTING) ||
                (curr->status == TNLR_CONNECTED)
            ) {
                fprintf(stderr, "Warning: redundant connect to [%s:%d]\n", remote_host, remote_port_native);
                goto tnlr_connect_return;
            } else {
                //This tcpconn was dead, so just reuse it
                break;
            }
        }
        curr = curr->next;
    }

    if (curr == &tcpconn_head) {
        //No existing connection was found, so create a new one
        curr = malloc(sizeof(tcpconn_t));
        assert(curr && "Oops, out of memory");
        curr->prev = tcpconn_head.prev;
        tcpconn_head.prev->next = curr;
        tcpconn_head.prev = curr;
        curr->next = &tcpconn_head;

        //Initialize the other fields
        curr->lua_refcount = 0;
        curr->next_tunnel_id = 0;

        //Remember to free this when we free the tcpconn struct :-)
        curr->remote_host = strdup(remote_host);
        curr->remote_port_native = remote_port_native;
        curr->local_port_native = local_port_native;
        curr->msg_data = evbuffer_new();
    }

    curr->status = TNLR_CONNECTING;
    curr->local_port_native = local_port_native;
    curr->tunnels_head.next = &curr->tunnels_head;
    curr->tunnels_head.prev = &curr->tunnels_head;

    int sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); //Nonblocking TCP socket
    if (sfd < 0) {
        luaL_error(L, "Could not open socket: [%s]", strerror(errno));
    }
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        luaL_error(L, "setsockopt(SO_REUSEADDR) failed");
    }
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0) {
        luaL_error(L, "setsockopt(SO_REUSEPORT) failed");
    }
    curr->fd = sfd;
    
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port_native);
    int rc = bind(sfd, (struct sockaddr*) &local_addr, sizeof(struct sockaddr_in));
    if (rc < 0) {
        close(sfd);
        luaL_error(L, "Could not bind socket to local port: [%s]", strerror(errno));
    }
    
    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    rc = inet_pton(AF_INET, remote_host, &remote_addr.sin_addr);
    if (rc != 1) {
        close(sfd);
        luaL_error(L, "Could not parse [%s] as an IPv4 address", remote_host);
    }
    remote_addr.sin_port = htons(remote_port_native);

    //Finally we can do the thing we actually care about
    curr->ev = bufferevent_socket_new(eb, sfd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(curr->ev, tcpconn_read_cb, NULL, connect_cb, curr);
    struct timeval timeout = {10, 0};
    bufferevent_set_timeouts(curr->ev, &timeout, &timeout);
    
    bufferevent_socket_connect(
        curr->ev, 
        (struct sockaddr*) &remote_addr, 
        sizeof(struct sockaddr_in)
    );
    
    tnlr_connect_return:;
    push_tcpconn(curr);
    return 1;
}

int tnlr_close(lua_State *L) {
    tcpconn_t **tc_tmp = (tcpconn_t **) luaL_testudata(L, 1, "tcpconn");
    if (tc_tmp != NULL) {
        luaL_error(L, "Sorry, tcpconn:close() is unimplemented"); //TODO: implement
    }

    tunnel_t **tn_tmp = (tunnel_t **) luaL_testudata(L, 1, "tunnel");
    if (tn_tmp != NULL) {
        luaL_error(L, "Sorry, tunnel:close() is unimplemented"); //TODO:implement
    }
    
    luaL_error(L, "Must pass a tcpconn or tunnel object as first argument");
    return 0;
}

int tnlr_list_connections(lua_State *L) {
    lua_newtable(L);

    int pos = 1;
    tcpconn_t *cur = tcpconn_head.next;
    while (cur != &tcpconn_head) {
        push_tcpconn(cur);
        lua_rawseti(L, -2, pos++);
        cur = cur->next;
    }
    
    return 1;
}

int tnlr_list_tunnels(lua_State *L) {
    tcpconn_t **tmp = (tcpconn_t **) luaL_checkudata(L, 1, "tcpconn");
    tcpconn_t *tc = *tmp;

    lua_newtable(L);
    int pos = 1;
    tunnel_t *cur = tc->tunnels_head.next;
    while (cur != &tc->tunnels_head) {
        push_tunnel(cur);
        lua_rawseti(L, -2, pos++);
        cur = cur->next;
    }

    return 1;
}

int tnlr_status(lua_State *L) {
    tcpconn_t **tc_tmp = (tcpconn_t **) luaL_testudata(L, 1, "tcpconn");
    if (tc_tmp != NULL) {
        tcpconn_t *tc = *tc_tmp;
        lua_pushstring(L, TNLR_STATUS_STRINGS[tc->status]);
        return 1;
    }

    tunnel_t **tn_tmp = (tunnel_t **) luaL_testudata(L, 1, "tunnel");
    if (tn_tmp != NULL) {
        tunnel_t *tn = *tn_tmp;
        lua_pushstring(L, TNLR_STATUS_STRINGS[tn->status]);
        return 1;
    }
    
    luaL_error(L, "Must pass a tcpconn or tunnel object as first argument");
    return 0;
}

int tnlr_local_host(lua_State *L) {
    tcpconn_t **tc_tmp = (tcpconn_t **) luaL_testudata(L, 1, "tcpconn");
    if (tc_tmp != NULL) {
        //All tcpconns are on the localhost
        lua_pushliteral(L, "localhost");
        return 1;
    }

    tunnel_t **tn_tmp = (tunnel_t **) luaL_testudata(L, 1, "tunnel");
    if (tn_tmp != NULL) {
        tunnel_t *tn = *tn_tmp;
        lua_pushstring(L, tn->local_host);
        return 1;
    }
    
    luaL_error(L, "Must pass a tcpconn or tunnel object as first argument");
    return 0;
}

int tnlr_local_port(lua_State *L) {
    tcpconn_t **tc_tmp = (tcpconn_t **) luaL_testudata(L, 1, "tcpconn");
    if (tc_tmp != NULL) {
        tcpconn_t *tc = *tc_tmp;
        lua_pushinteger(L, tc->local_port_native);
        return 1;
    }

    tunnel_t **tn_tmp = (tunnel_t **) luaL_testudata(L, 1, "tunnel");
    if (tn_tmp != NULL) {
        tunnel_t *tn = *tn_tmp;
        lua_pushinteger(L, tn->local_port_native);
        return 1;
    }
    
    luaL_error(L, "Must pass a tcpconn or tunnel object as first argument");
    return 0;
}

int tnlr_remote_host(lua_State *L) {
    tcpconn_t **tc_tmp = (tcpconn_t **) luaL_testudata(L, 1, "tcpconn");
    if (tc_tmp != NULL) {
        tcpconn_t *tc = *tc_tmp;
        lua_pushstring(L, tc->remote_host);
        return 1;
    }

    tunnel_t **tn_tmp = (tunnel_t **) luaL_testudata(L, 1, "tunnel");
    if (tn_tmp != NULL) {
        tunnel_t *tn = *tn_tmp;
        lua_pushstring(L, tn->remote_host);
        return 1;
    }
    
    luaL_error(L, "Must pass a tcpconn or tunnel object as first argument");
    return 0;
}

int tnlr_remote_port(lua_State *L) {
    tcpconn_t **tc_tmp = (tcpconn_t **) luaL_testudata(L, 1, "tcpconn");
    if (tc_tmp != NULL) {
        tcpconn_t *tc = *tc_tmp;
        lua_pushinteger(L, tc->remote_port_native);
        return 1;
    }

    tunnel_t **tn_tmp = (tunnel_t **) luaL_testudata(L, 1, "tunnel");
    if (tn_tmp != NULL) {
        tunnel_t *tn = *tn_tmp;
        lua_pushinteger(L, tn->remote_port_native);
        return 1;
    }
    
    luaL_error(L, "Must pass a tcpconn or tunnel object as first argument");
    return 0;
}

int tnlr_tunnel(lua_State *L) {
    tcpconn_t **tmp = (tcpconn_t **) luaL_checkudata(L, 1, "tcpconn");
    tcpconn_t *tc = *tmp;

    //Read the other arguments to this function
    lua_Integer local_port_raw = luaL_checkinteger(L, 2);
    if (local_port_raw < 0 || local_port_raw > 65535) {
        luaL_error(L, "local_port value [%ld] is out of range (0-65535)", local_port_raw);
    }

    char const *remote_host = luaL_checkstring(L, 3);
    
    lua_Integer remote_port_raw = luaL_checkinteger(L, 4);
    if (remote_port_raw < 0 || remote_port_raw > 65535) {
        luaL_error(L, "remote_port value [%ld] is out of range (0-65535)", remote_port_raw);
    }
    
    //Rename (and re-type) for consistency
    uint16_t local_port_native = local_port_raw;
    uint16_t remote_port_native = remote_port_raw;

    //Check if this tunnel already exists
    tunnel_t *cur = tc->tunnels_head.next;
    while (cur != &tc->tunnels_head) {
        //TODO: need smarter hostname comparison
        if (
            !strcmp(remote_host, cur->remote_host) &&
            (local_port_native == cur->local_port_native) &&
            (remote_port_native == cur->remote_port_native)
        ) {
            //TODO: deal with tunnel directions
            fprintf(stderr, "Warning: redundant tunnel to [%d:%s:%d]\n", local_port_native, remote_host, remote_port_native);
            goto tnlr_tunnel_return;
        }
        cur = cur->next;
    }

    //Bind a socket to the desired local port
    int sfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); //Nonblocking TCP socket
    if (sfd < 0) {
        luaL_error(L, "Could not open socket: [%s]", strerror(errno));
    }
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        luaL_error(L, "setsockopt(SO_REUSEADDR) failed");
    }
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0) {
        luaL_error(L, "setsockopt(SO_REUSEPORT) failed");
    }
    
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port_native);
    int rc = bind(sfd, (struct sockaddr*) &local_addr, sizeof(struct sockaddr_in));
    if (rc < 0) {
        close(sfd);
        luaL_error(L, "Could not bind socket to local port: [%s]", strerror(errno));
    }

    rc = accept(sfd, NULL, NULL);
    if (rc < 0 && !(rc == EAGAIN || rc == EWOULDBLOCK)) {
        close(sfd);
        luaL_error(L, "Could not start accepting connections: [%s]", strerror(errno));
    }
    assert((rc != 0) && "Somehow we accepted right away, but this code doesn't handle that");
    
    //Did not find an existing tunnel, so make a new object and
    //hook it up into the tcpconn's tunnels list
    cur = malloc(sizeof(tunnel_t));
    assert(cur && "oops out of memory");
    
    //Now we can start up an async accept call. According to libevent,
    //we can tell when we're ready to accept a connection when the socket
    //becomes readable
    struct timeval accept_timeout = {120,0}; //Two minutes seems appropriate
    struct event *ev = event_new(
        eb, 
        sfd, 
        EV_READ | EV_TIMEOUT, 
        fwd_tunnel_accept_cb, 
        cur
    );
    event_add(ev, &accept_timeout);

    uint32_t id_native = tc->next_tunnel_id++;
    cur->id = id_native;
    cur->parent = tc;
    cur->fd = sfd;
    cur->is_forward_tunnel = 1;
    cur->status = TNLR_CONNECTING;
    cur->local_host = NULL; //Unneded for forward tunnel
    cur->local_port_native = local_port_native;
    cur->remote_host = strdup(remote_host);
    cur->remote_port_native = remote_port_native;
    cur->local_accept_ev = ev;

    cur->next = (tc->tunnels_head.next)->next;
    tc->tunnels_head.next = cur;
    cur->prev = &tc->tunnels_head;
    
    tnlr_tunnel_return:;
    push_tunnel(cur);
    return 1;
}

int tnlr_chat(lua_State *L) {
    tcpconn_t **tmp = (tcpconn_t **) luaL_checkudata(L, 1, "tcpconn");
    tcpconn_t *tc = *tmp;

    size_t len;
    char const *str = luaL_checklstring(L, 2, &len);

    uint32_t hdr[2];
    hdr[0] = 0;
    hdr[1] = htonl(len);
    
    bufferevent_write(tc->ev, hdr, sizeof(hdr));
    bufferevent_write(tc->ev, str, len);

    return 0;
}

int tcpconn_tostring(lua_State *L) {
    tcpconn_t **tmp = (tcpconn_t **) luaL_checkudata(L, 1, "tcpconn");
    tcpconn_t *tc = *tmp;
    char info[256];
    snprintf(
        info, sizeof(info), "tcpconn[local %d, remote %s:%d]: %s",
        tc->local_port_native,
        tc->remote_host,
        tc->remote_port_native,
        TNLR_STATUS_STRINGS[tc->status]
    );
    lua_pushstring(L, info);
    return 1;
}

void tunnel_free(tunnel_t *);
void tcpconn_free_tunnel_list(tcpconn_t *tc) {
    tunnel_t *curr = tc->tunnels_head.next;
    while (curr != &tc->tunnels_head) {
        curr = curr->next; //Important to do this before freeing
        tunnel_free(curr);
    }
}

void tcpconn_free(tcpconn_t *tc) {
    puts("Really freeing a tcpconn");
    //Gracefully ignore NULL input
    if (!tc) return;

    //Prevent shooting ourselves in the foot
    assert(tc != &tcpconn_head);

    //Also make sure we don't free a tcpconn that
    //still has lua references
    assert(tc->lua_refcount == 0);
    
    //Remove this tc from the linked list
    tc->prev->next = tc->next;
    tc->next->prev = tc->prev;

    tcpconn_free_tunnel_list(tc);

    evbuffer_free(tc->msg_data);
    
    //Free all the malloc'ed fields within this tc
    free(tc->remote_host);
    if(tc->ev) bufferevent_free(tc->ev);

    free(tc);
}

//Only frees the tcpconn if it is well and truly unused by both lua and C
int tcpconn_gc(lua_State *L) {
    puts("Called tcpconn_gc");
    tcpconn_t **tmp = (tcpconn_t **) luaL_checkudata(L, 1, "tcpconn");
    tcpconn_t *tc = *tmp;
    /*
    The udata is just a pointer to a tcpconn in that global linked
    list. We then decrement its lua_refcount. If the refcount goes to
    zero, we need to see if it is safe to free this tcpconn. There are
    two reasons why it wouldn't be safe, and we check for both:
        1. iterate through the list of tunnels and check if any of them
           still have Lua references.
        2. Check if this connection is still open (i.e. the TCP socket is
           still open)
    If any of those are true we do not free the tcpconn object. If both 
    are false, we do. (And obviously remove it from the linked list)
    */

    int newcount = --(tc->lua_refcount);
    printf("lua_refcount is now %d\n", tc->lua_refcount);
    assert(newcount >= 0);

    if (newcount <= 0) {
        //If this tcpconn is still in use, then don't free it
        if (tc->status == TNLR_CONNECTING || tc->status == TNLR_CONNECTED) {
            return 0;
        }

        //If any of the tunnels under this tcpconn have a lua reference,
        //we'll still hang onto this tcpconn (since it is still possible to
        //request its information with the lua API).
        tunnel_t *curr = tc->tunnels_head.next;
        while (curr != &tc->tunnels_head) {
            if (curr->lua_refcount > 0) return 0;
            curr = curr->next;
        }
        
        tcpconn_free(tc);
    }
    
    return 0;
}

int tunnel_tostring(lua_State *L) {
    tunnel_t **tmp = (tunnel_t **) luaL_checkudata(L, 1, "tunnel");
    tunnel_t *tn = *tmp;
    char info[256];
    int is_reverse = (tn->local_host != NULL);
    assert(!is_reverse && "Reverse tunnels not yet supported"); //TODO: implement
    snprintf(
        info, sizeof(info), "tunnel{via %s:%d}[local %d, remote %s:%d]: %s",
        tn->parent->remote_host,
        tn->parent->remote_port_native,
        tn->local_port_native,
        tn->remote_host,
        tn->remote_port_native,
        TNLR_STATUS_STRINGS[tn->status]
    );
    lua_pushstring(L, info);
    return 1;
}

//Only frees the tunnel if it is well and truly unused by both lua and C
int tunnel_gc(lua_State *L) {
    puts("Called tunnel_gc");
    tunnel_t **tmp = (tunnel_t **) luaL_checkudata(L, 1, "tunnel");
    tunnel_t *tn = *tmp;
    /*
    The udata is just a pointer to a tunnel in that global linked
    list. We then decrement its lua_refcount. If the refcount goes to
    zero, we need to see if it is safe to free this tunnel. There is
    one reasons why it wouldn't be safe, and we check for it:
        1. Check if this connection is still open (i.e. the TCP socket is
           still open)
    If any of those are true we do not free the tcpconn object. If both 
    are false, we do. (And obviously remove it from the linked list)
    */

    int newcount = --(tn->lua_refcount);
    printf("tunnel_gc lua_refcount is now %d\n", tn->lua_refcount);
    assert(newcount >= 0);

    if (newcount <= 0) {
        //If this tcpconn is still in use, then don't free it
        if (tn->status == TNLR_CONNECTING || tn->status == TNLR_CONNECTED) {
            return 0;
        }
        
        tunnel_free(tn);
    }
    
    return 0;
}

void tunnel_free(tunnel_t *t) {
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

int luaopen_tnlr(lua_State *L) {
    lua_register(L, "connect", &tnlr_connect);
    lua_register(L, "close", &tnlr_close);
    lua_register(L, "list_connections", &tnlr_list_connections);
    lua_register(L, "status", &tnlr_status);
    lua_register(L, "local_host", &tnlr_local_host);
    lua_register(L, "local_port", &tnlr_local_port);
    lua_register(L, "remote_host", &tnlr_remote_host);
    lua_register(L, "remote_port", &tnlr_remote_port);
    lua_register(L, "chat", &tnlr_chat);
    lua_register(L, "list_tunnels", &tnlr_list_tunnels);
    lua_register(L, "tunnel", &tnlr_tunnel);
    
    luaL_newmetatable(L, "tcpconn");
    lua_pushliteral(L, "__tostring");
    lua_pushcfunction(L, &tcpconn_tostring);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__gc");
    lua_pushcfunction(L, &tcpconn_gc);
    lua_rawset(L, -3);
    lua_pushliteral(L, "status");
    lua_pushcfunction(L, &tnlr_status);
    lua_rawset(L, -3);
    lua_pushliteral(L, "local_host");
    lua_pushcfunction(L, &tnlr_local_host);
    lua_rawset(L, -3);
    lua_pushliteral(L, "local_port");
    lua_pushcfunction(L, &tnlr_local_port);
    lua_rawset(L, -3);
    lua_pushliteral(L, "remote_host");
    lua_pushcfunction(L, &tnlr_remote_host);
    lua_rawset(L, -3);
    lua_pushliteral(L, "remote_port");
    lua_pushcfunction(L, &tnlr_remote_port);
    lua_rawset(L, -3);
    lua_pushliteral(L, "chat");
    lua_pushcfunction(L, &tnlr_chat);
    lua_rawset(L, -3);
    lua_pushliteral(L, "list_tunnels");
    lua_pushcfunction(L, &tnlr_list_tunnels);
    lua_rawset(L, -3);
    lua_pushliteral(L, "tunnel");
    lua_pushcfunction(L, &tnlr_tunnel);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L,-3);

    
    luaL_newmetatable(L, "tunnel");
    lua_pushliteral(L, "__tostring");
    lua_pushcfunction(L, &tunnel_tostring);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__gc");
    lua_pushcfunction(L, &tunnel_gc);
    lua_rawset(L, -3);
    lua_pushliteral(L, "status");
    lua_pushcfunction(L, &tnlr_status);
    lua_rawset(L, -3);
    lua_pushliteral(L, "local_host");
    lua_pushcfunction(L, &tnlr_local_host);
    lua_rawset(L, -3);
    lua_pushliteral(L, "local_port");
    lua_pushcfunction(L, &tnlr_local_port);
    lua_rawset(L, -3);
    lua_pushliteral(L, "remote_host");
    lua_pushcfunction(L, &tnlr_remote_host);
    lua_rawset(L, -3);
    lua_pushliteral(L, "remote_port");
    lua_pushcfunction(L, &tnlr_remote_port);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L,-3);
    
    lua_settop(L, 0); //Just make sure stack is clear
    return 0; //...although returning 0 probably makes that unnecessary
}

void read_stdin_cb(evutil_socket_t sock, short what, void *arg) {
    char line[256];
    int rc = read(sock, line, sizeof(line) - 1);
    assert(rc >= 0);
    if (rc == 0) {
        puts("Thank you for using this program");
        event_base_loopbreak(eb);
        return;
    }
    line[rc] = 0; //NUL-terminate
    rc = luaL_loadbuffer(L, line, rc, "stdin");
    if (rc != LUA_OK) {
        //puts("Error reading Lua input:");
        assert(lua_type(L,-1) == LUA_TSTRING);
        char const *s = lua_tostring(L,-1);
        puts(s);
        lua_pop(L, 1);
    } else {
        int rc = lua_pcall(L, 0, 0, 0);
        if (rc != LUA_OK) {
            //puts("Error running Lua input:");
            assert(lua_type(L,-1) == LUA_TSTRING);
            char const *s = lua_tostring(L,-1);
            puts(s);
            lua_pop(L, 1);
        }
    }
}

int main(int arc, char *argv[]) { 
    tcpconn_head.prev = &tcpconn_head;
    tcpconn_head.next = &tcpconn_head;
    
    puts("Say something");

    L = luaL_newstate();
    luaopen_base(L);
    luaopen_tnlr(L);

    eb = event_base_new();

    struct event *stdin_ev = event_new(
        eb, 
        STDIN_FILENO, 
        EV_READ | EV_PERSIST, 
        &read_stdin_cb, 
        NULL
    );

    event_add(stdin_ev, NULL);

    event_base_dispatch(eb);

    //TODO: free all open tcpconns and tunnels
    
    event_free(stdin_ev);
    event_base_free(eb);
    lua_close(L);
    
    return 0;
}