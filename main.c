/*
Breadcrumbs trail:
- Basic tunnels now working, but they don't close gracefully
  when the connection is closed by the external program.
- Also need to implement functions to let user forcibly close
  tunnels
- By the way, SSH tunnels will stick around even if the endpoints
  disconnect, so we should probably implement that
- And I need reverse tunnels...
- And we need encryption...
- And we need to get it working on Windows...
- And we need DNS resolution...
- And I should probably support UDP...
- And it would be nice if you could connect to a file instead of
  a network port...

...man this project just keeps getting bigger
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
#include "os_common.h"

#ifdef WINDOWS
#include <pthread.h>

//Just to try and prevent us connecting to some rando.
//Obviously not secure but, uhhh, look over there! (runs away)
#define MAGIC 0xFAFFADAF

static int here_is_the_port; //Global var written by one thread
                             //and read by another. So sue me.

void* stupid_stdin_socket_forward_thread(void *arg) {
    sockfd sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == INVALID_SOCKET) {
        printf(
            "Could not open a socket: [%s]\n",
            sockstrerror(sockerrno)
        );
        puts("You may as well kill the program, it will now hang"); //FIXME
        return NULL;
    }
    
    struct sockaddr_in cxn_addr;
    cxn_addr.sin_family = AF_INET;
    cxn_addr.sin_addr.s_addr = htonl(0x7F000001); //Does this work?
    cxn_addr.sin_port = htons(here_is_the_port);
    int rc = connect(sfd, (struct sockaddr*) &cxn_addr, sizeof(cxn_addr));
    if (rc == SOCKET_ERROR) {
        printf(
            "Could not connect stupid stdin listener: [%s]\n",
            sockstrerror(sockerrno)
        );
        puts("You may as well kill the program, it will now hang"); //FIXME
        closesocket(sfd);
        return NULL;
    }
    
    puts("Stupid stdin forwarder connected!!!!!!");
    
    while (1) {
        char buf[256];
        int rc = read(STDIN_FILENO, buf, sizeof(buf));
        if (rc == 0) {
            closesocket(sfd);
            return NULL;
        }
        
        int bytes_left = rc;
        while (bytes_left > 0) {
            int num_written = sockwrite(sfd, buf, bytes_left);
            assert(num_written > 0);
            bytes_left -= num_written;
        }
    }
    
    return NULL;
}

//Returns file descriptor for socket to read on
sockfd stupid_windows_stdin_to_socket_fix() {
    sockfd sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd == INVALID_SOCKET) {
        printf(
            "Could not open a socket: [%s]\n",
            sockstrerror(sockerrno)
        );
        return INVALID_SOCKET;
    }
    
    //Don't hog the port
    if (evutil_make_listen_socket_reuseable(sfd) < 0) {
        printf("setsockopt(SO_REUSEADDR) failed\n");
        closesocket(sfd);
        return INVALID_SOCKET;
    }
    
    //Bind to a free port
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = 0; //Let OS choose this
    local_addr.sin_addr.s_addr = INADDR_ANY;
    int rc = bind(sfd, (struct sockaddr*) &local_addr, sizeof(local_addr));
    if (rc == SOCKET_ERROR) {
        printf(
            "Could not bind stupid stdin listener: [%s]\n",
            sockstrerror(sockerrno)
        );
        closesocket(sfd);
        return INVALID_SOCKET;
    }
    
    //Figure out what port was used
    int len = sizeof(local_addr);
    rc = getsockname(sfd, (struct sockaddr*) &local_addr, &len);
    assert(len == sizeof(local_addr));
    if (rc == SOCKET_ERROR) {
        printf(
            "Could not call getsockname: [%s]\n",
            sockstrerror(sockerrno)
        );
        closesocket(sfd);
        return INVALID_SOCKET;
    }
    
    int local_port_native = ntohs(local_addr.sin_port);
    here_is_the_port = local_port_native;
    printf("We are listening on port [%d]\n", local_port_native);
    
    rc = listen(sfd, 1);
    if (rc == SOCKET_ERROR) {
        printf(
            "Could not start listening: [%s]\n",
            sockstrerror(sockerrno)
        );
        closesocket(sfd);
        return INVALID_SOCKET;
    }
    
    //Start other thread so it will call connect()
    pthread_t tid;
    rc = pthread_create(
        &tid,
        NULL,
        stupid_stdin_socket_forward_thread,
        NULL
    );
    if (rc < 0) {
        printf("Could not start thread: [%s]\n", strerror(rc));
        closesocket(sfd);
        return INVALID_SOCKET;
    }
    
    //Make blocking call to accept
    //TODO: make call to accept via select/poll so we can set a
    //timeout
    sockfd sfd2 = accept(sfd, NULL, NULL);
    if (sfd2 == INVALID_SOCKET) {
        //Now we're really in trouble, because we need to
        //clean up the other thread
        pthread_cancel(tid);
        closesocket(sfd);
        return INVALID_SOCKET;
    }
    closesocket(sfd);
    
    //Make this a nonblocking TCP socket (thanks libevent!)
    if (evutil_make_socket_nonblocking(sfd2) < 0) {
        printf("Could not make socket nonblocking: [%s]\n", sockstrerror(sockerrno));
        closesocket(sfd2);
        return INVALID_SOCKET;
    }
    
    return sfd2;
}
#endif


tcpconn_t tcpconns; //Global list of open tcpconns. So sue me.
struct event_base *eb; //Global event_base. So sue me.

void read_stdin_cb(evutil_socket_t sock, short what, void *arg) {
    lua_State *L = (lua_State *) arg;
    
    char line[256];
    int rc = sockread(sock, line, sizeof(line) - 1);
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
    
    os_common_startup();
    
    tcpconns.prev = &tcpconns;
    tcpconns.next = &tcpconns;
    
    puts("Say something");

    lua_State *L = luaL_newstate();
    luaopen_base(L);
    luaopen_tnlr(L);
    
    
    evutil_socket_t stdin_fd;
    #ifdef WINDOWS
        //Ugh...
        stdin_fd = stupid_windows_stdin_to_socket_fix();
        if (stdin_fd == INVALID_SOCKET) {
            puts("Could not apply stdin workaround. Oh well.");
            goto i_give_up;
        }
        { //Anonymous braces to prevent goto error
    #else
        stdin_fd = STDIN_FILENO;
    #endif
    
    eb = event_base_new();
    
    struct event *stdin_ev = event_new(
        eb, 
        stdin_fd, 
        EV_READ | EV_PERSIST, 
        &read_stdin_cb, 
        L
    );

    event_add(stdin_ev, NULL);

    int rc = event_base_dispatch(eb);
    printf("event_base_dispatch returned %d\n", rc);

    //TODO: free all open tcpconns and tunnels
    
    event_free(stdin_ev);
    event_base_free(eb);
    
    #ifdef WINDOWS
        } //End anonymous braces
        closesocket(stdin_fd);
        i_give_up:;
    #endif
    
    lua_close(L);

    os_common_cleanup();
    
    return 0;
}
