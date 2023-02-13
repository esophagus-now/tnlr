/*
Breadcrumbs trail:
- In the middle of implementing tunnels (forward only rn)
- For tunnel IDs: instead of fiddling with marking tunnels we created
  vs those created by the other host, simply keep two linked lists instead
  of one. Duh.
- Need to implement close function
- close message implementation looks iffy to me
- In general my teardown logic is kinda bad. I left TODOs in the
  places that need attention
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>

#define IMPLEMENT
#include "common.h"
#include "lua_cli.h"
#include "tcpconn_cbs.h"
#include "tunnel_cbs.h"

tcpconn_t tcpconns; //Global list of open tcpconns. So sue me.
struct event_base *eb; //Global event_base. So sue me.

void read_stdin_cb(evutil_socket_t sock, short what, void *arg) {
    lua_State *L = (lua_State *) arg;
    
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
    tcpconns.prev = &tcpconns;
    tcpconns.next = &tcpconns;
    
    puts("Say something");

    lua_State *L = luaL_newstate();
    luaopen_base(L);
    luaopen_tnlr(L);

    eb = event_base_new();

    struct event *stdin_ev = event_new(
        eb, 
        STDIN_FILENO, 
        EV_READ | EV_PERSIST, 
        &read_stdin_cb, 
        L
    );

    event_add(stdin_ev, NULL);

    event_base_dispatch(eb);

    //TODO: free all open tcpconns and tunnels
    
    event_free(stdin_ev);
    event_base_free(eb);
    lua_close(L);
    
    return 0;
}