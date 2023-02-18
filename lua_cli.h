//This is my standard method now of putting code into other files.
//Ever since I learned this trick to get around needing to have both
//header and C files, I never want to go back to the old way
#ifdef IMPLEMENT
	#ifndef LUA_CLI_H_IMPLEMENTED
		#define SHOULD_INCLUDE 1
		#define LUA_CLI_H_IMPLEMENTED 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef LUA_CLI_H
		#define SHOULD_INCLUDE 1
		#define LUA_CLI_H 1
	#else 
		#define SHOULD_INCLUDE 0
	#endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE

#ifdef IMPLEMENT
#undef IMPLEMENT
#include "lua_cli.h"
#define IMPLEMENT 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "common.h"
#include "tcpconn_cbs.h"
#include "tunnel_cbs.h"
#include "os_common.h"

#ifdef IMPLEMENT
static void push_tcpconn(lua_State *L, tcpconn_t *tc) {
    tcpconn_t **ret = (tcpconn_t **) lua_newuserdata(L, sizeof(tcpconn_t*));
    assert(ret && "oops out of memory");
    luaL_setmetatable(L, "tcpconn");
    *ret = tc;
    tc->lua_refcount++;
    printf("lua_refcount is now %d\n", tc->lua_refcount);
}

//i.e. push a tunnel_t userdata
static void push_tunnel(lua_State *L, tunnel_t *tn) {
    tunnel_t **ret = (tunnel_t **) lua_newuserdata(L, sizeof(tunnel_t*));
    assert(ret && "oops out of memory");
    luaL_setmetatable(L, "tunnel");
    *ret = tn;
    tn->lua_refcount++;
    printf("tunnel lua_refcount is now %d\n", tn->lua_refcount);
}
#endif

int tnlr_connect(lua_State *L) 
#ifdef IMPLEMENT
{
    /*
    Check if a connection is already open to the desired endpoint.
    If so, just return that, taking care to increment the refcount.
    Otherwise, alloc a new tcpconn object with status CONNECTING.
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
    
    tcpconn_t *curr = tcpconns.next;
    while (curr != &tcpconns) {
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
                //TODO: check if I need to free any of the fields before
                //reusing to prevent memory leaks
                break;
            }
        }
        curr = curr->next;
    }

    if (curr == &tcpconns) {
        //No existing connection was found, so create a new one
        curr = (tcpconn_t*) calloc(1, sizeof(tcpconn_t));
        assert(curr && "Oops, out of memory");
        curr->prev = tcpconns.prev;
        tcpconns.prev->next = curr;
        tcpconns.prev = curr;
        curr->next = &tcpconns;

        //Initialize the other fields
        curr->lua_refcount = 0;
        curr->next_tunnel_id = 0;

        //Remember to free this when we free the tcpconn struct :-)
        curr->remote_host = strdup(remote_host);
        curr->remote_port_native = remote_port_native;
        curr->local_port_native = local_port_native;
        curr->msg_data = evbuffer_new();
    }
    
    //Anonymous braces to prevent goto error
    {
    curr->status = TNLR_CONNECTING;
    curr->local_port_native = local_port_native;
    curr->tunnels.next = &curr->tunnels;
    curr->tunnels.prev = &curr->tunnels;

    sockfd sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == INVALID_SOCKET) {
        luaL_error(L, "Could not open socket: [%s]", sockstrerror(sockerrno));
    }
    //Make this a nonblocking TCP socket (thanks libevent!)
    if (evutil_make_socket_nonblocking(sfd) < 0) {
        closesocket(sfd);
        luaL_error(
            L,
            "Could not make socket nonblocking: [%s]\n", 
            sockstrerror(sockerrno)
        );
    }
    if (
        //setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0
        evutil_make_listen_socket_reuseable(sfd) < 0 //Thank heavens for libevent
    ) {
        closesocket(sfd);
        luaL_error(L, "setsockopt(SO_REUSEADDR) failed");
    }
    #ifndef WINDOWS
    if (fix_rc(setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int))) < 0) {
        closesocket(sfd);
        luaL_error(L, "setsockopt(SO_REUSEPORT) failed");
    }
    #endif
    curr->fd = sfd;
    
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port_native);
    int rc = fix_rc(bind(sfd, (struct sockaddr*) &local_addr, sizeof(struct sockaddr_in)));
    if (rc < 0) {
        closesocket(sfd);
        luaL_error(L, "Could not bind socket to local port: [%s]", strerror(errno));
    }
    
    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    rc = inet_pton(AF_INET, remote_host, &remote_addr.sin_addr);
    if (rc != 1) {
        closesocket(sfd);
        luaL_error(L, "Could not parse [%s] as an IPv4 address", remote_host);
    }
    remote_addr.sin_port = htons(remote_port_native);

    //Finally we can do the thing we actually care about
    curr->ev = bufferevent_socket_new(eb, sfd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(curr->ev, tcpconn_read_cb, NULL, tcpconn_connect_cb, curr);
    struct timeval timeout = {10, 0};
    bufferevent_set_timeouts(curr->ev, &timeout, &timeout);
    
    bufferevent_socket_connect(
        curr->ev, 
        (struct sockaddr*) &remote_addr, 
        sizeof(struct sockaddr_in)
    );
    } //End anonymous braces
    
    tnlr_connect_return:;
    push_tcpconn(L, curr);
    return 1;
}
#else
;
#endif

int tnlr_close(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

int tnlr_list_connections(lua_State *L) 
#ifdef IMPLEMENT
{
    lua_newtable(L);

    int pos = 1;
    tcpconn_t *cur = tcpconns.next;
    while (cur != &tcpconns) {
        push_tcpconn(L, cur);
        lua_rawseti(L, -2, pos++);
        cur = cur->next;
    }
    
    return 1;
}
#else
;
#endif

int tnlr_list_tunnels(lua_State *L)  
#ifdef IMPLEMENT
{
    tcpconn_t **tmp = (tcpconn_t **) luaL_checkudata(L, 1, "tcpconn");
    tcpconn_t *tc = *tmp;

    lua_newtable(L);
    int pos = 1;
    tunnel_t *cur = tc->tunnels.next;
    while (cur != &tc->tunnels) {
        push_tunnel(L, cur);
        lua_rawseti(L, -2, pos++);
        cur = cur->next;
    }

    return 1;
}
#else
;
#endif

int tnlr_status(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

int tnlr_local_host(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

int tnlr_local_port(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

int tnlr_remote_host(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

int tnlr_remote_port(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

int tnlr_tunnel(lua_State *L)  
#ifdef IMPLEMENT
{
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
    tunnel_t *cur = tc->tunnels.next;
    while (cur != &tc->tunnels) {
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
    
    { //Anonymous braces to prevent goto error
    //Bind a socket to the desired local port
    sockfd sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == INVALID_SOCKET) {
        luaL_error(L, "Could not open socket: [%s]", strerror(errno));
    }
    //Make this a nonblocking TCP socket (thanks libevent!)
    if (evutil_make_socket_nonblocking(sfd) < 0) {
        closesocket(sfd);
        luaL_error(
            L,
            "Could not make socket nonblocking: [%s]\n", 
            sockstrerror(sockerrno)
        );
    }
    if (
        //setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0
        evutil_make_listen_socket_reuseable(sfd) < 0 //Thank heavens for libevent
    ) {
        closesocket(sfd);
        luaL_error(L, "setsockopt(SO_REUSEADDR) failed");
    }
    #ifndef WINDOWS
    if (setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &(int){1}, sizeof(int)) < 0) {
        closesocket(sfd);
        luaL_error(L, "setsockopt(SO_REUSEPORT) failed");
    }
    #endif
    
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port_native);
    if (fix_rc(bind(sfd, (struct sockaddr*) &local_addr, sizeof(struct sockaddr_in))) < 0) {
        int errno_saved = sockerrno;
        closesocket(sfd);
        luaL_error(L, "Could not bind socket to local port: [%s]", sockstrerror(errno_saved));
    }
    if (fix_rc(listen(sfd, 1)) < 0) {
        int errno_saved = sockerrno;
        closesocket(sfd);
        luaL_error(L, "Could not start listening for connections: [%s]", sockstrerror(errno_saved));
    }

    //Did not find an existing tunnel, so make a new object and
    //hook it up into the tcpconn's tunnels list
    cur = (tunnel_t*) calloc(1, sizeof(tunnel_t));
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

    puts("Awaiting connection to newly listening fwd tunnel...");
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

    cur->next = (tc->tunnels.next)->next;
    tc->tunnels.next = cur;
    cur->prev = &tc->tunnels;
    } //End anonymous braces
    
    tnlr_tunnel_return:;
    push_tunnel(L, cur);
    return 1;
}
#else
;
#endif

int tnlr_chat(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

#ifdef IMPLEMENT
static int tcpconn_tostring(lua_State *L) {
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

//Only frees the tcpconn if it is well and truly unused by both lua and C
static int tcpconn_gc(lua_State *L) {
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
        tunnel_t *curr = tc->tunnels.next;
        while (curr != &tc->tunnels) {
            if (curr->lua_refcount > 0) return 0;
            curr = curr->next;
        }
        
        tcpconn_free(tc);
    }
    
    return 0;
}

static int tunnel_tostring(lua_State *L) {
    tunnel_t **tmp = (tunnel_t **) luaL_checkudata(L, 1, "tunnel");
    tunnel_t *tn = *tmp;
    char info[256];
    assert(tn->is_forward_tunnel && "Reverse tunnels not yet supported"); //TODO: implement
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
static int tunnel_gc(lua_State *L) {
    puts("Called tunnel_gc");
    tunnel_t **tmp = (tunnel_t **) luaL_checkudata(L, 1, "tunnel");
    tunnel_t *tn = *tmp;
    /*
    The udata is just a pointer to a tunnel in that global linked
    list. We then decrement its lua_refcount. If the refcount goes to
    zero, we need to see if it is safe to free this tunnel. There are
    two reasons why it wouldn't be safe, and we check for them:
        1. Check if this connection is still open (i.e. the TCP socket is
           still open)
        <strikethrough>
        2. If the tcpconn parent of this tunnel still has lua_references,
           then we could use list_connections to find this tunnel. Except
           no, because if this tunnel is not open we're going to remove it
           from the tcpconn's list of tunnels anwyay, so list_tunnels 
           wouldn't find it
        </strikethrough>
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
#endif

int luaopen_tnlr(lua_State *L)  
#ifdef IMPLEMENT
{
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
#else
;
#endif

#endif
