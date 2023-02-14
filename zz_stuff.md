## Lua CLI

### Data types (userdata)
  - `tcpconn`: This represents the actual TCP connection between
    two machines. For now, this is just the file descriptor and
    a status code. In the future, there may also be some fields
    here for managing encryption.
  - `tunnel`: As much as I dislike the word tunnel (because of how
    abstract and misleading it is) we're kind of stuck with it.
    This acts as a handle to make it easier for the user to close 
    tunnels, and also contains status information. For convenience
    it will hang onto information about the local and remote 
    endpoints in case the user wants to do something with them.

### API functions

  - `connect(remote_host[, remote_port[, local_port]])`: Returns
    a `tcpconn`. Asynchronously starts a TCP connect request to the
    given remote host and port (default = 23232). `remote_host` is
    a string with an IP address (in the future we will support
    DNS lookups). The default `local_port` is 23232.
    
    This function is asynchronous and will never block. You need to
    check the returned `tcpconn`'s status to see what is going on.
    
  - `tunnel(tcpconn, local_port, remote_host, remote_port)`: This is 
    just like SSH's `-L` switch. This will open a tunnel through the 
    specified `tcpconn`. The `remote_host` is a string with an IP address 
    (in the future we we will support DNS lookups).
    
    This function is asynchronous and will never block. You need to
    check the returned `tunnel`'s status to see what is going on.
    
  - `rtunnel(tcpconn, local_host, local_port, remote_port)`: Just like 
    SSH's `-R` switch. Asynchronous, and returns a `tunnel` object.
    
  - `close(obj)`: Closes a `tcpconn` or `tunnel` object. If closing a tunnel,
    this will do `shutdown(fd, SHUT_RDWR)` followed by `close()`. If closing
    a tcpconn, will first close all of its open tunnels then will also
    close the tcpconn
    
  - `quit()`: Gracefully close everything and shut down

  - `list_connections()`: Return a table of open tcpconns
    
  - `list_tunnels(tcpconn)`: Return a table of tunnels under the given
    `tcpconn`
    
  - `parent(tunnel)`: Returns the tcpconn object that is the parent of
    the given `tunnel`
    
  - `status(obj)`: Returns a string describing the status of a 
    tcpconn or tunnel object. This info is also printed when you
    do `print(obj)`.
    
  - `local_host(obj)`, `local_port(obj)`, `remote_host(obj)`, `remote_port(obj)`:
    Returns a string with the desired information. This info is also
    printed when you do `print(obj)`.
    
  - `direction(tunnel)`: Borrowing SSH's nomenclature, "forward" means 
    that when the tunnel was created, the tnlr instance on the remote
    machine issued the TCP connection to remote_host:remote_port, and
    "reverse" means that the tnlr instance on the local machine issued
    the TCP connection to local_host:local_port. I hope that makes sense.

## Line protocol for the tcpconn connection
  - All multi-byte fields are in network endianness
  - All messages have an 8-byte header followed by (arbitrarily long) data.
    The first 4 bytes of the header signify the type of message. The
    second 4 bytes of the header specify the length of the data in bytes.
  - Types of messages we need to support. In these descriptions, L is the
    tnlr instance on the local machine, and all descriptions are from its
    point of view (unless otherwise noted). R is the remote tnlr instance.
    - `OPEN_TUNNEL`. L sends this to R when it creates a forward tunnel. It
      requests that R tries to connect to remote_host:remote_port. The data
      from an `OPEN_TUNNEL` message is a 32-bit ID (this ID is used in the
      response message, see below), a 16-bit port number and a remote_host 
      string. The length of the string is deduced from checking the length 
      field of the header (the length field always encode the entire data
      size, so the length of this string is length - 6). If the connection
      succeeds (or fails), R will notify L with an `OPEN_TUNNEL_RESPONSE`.
    - `OPEN_REVERSE_TUNNEL`. When opening a reverse tunnel, L will first send
      this message to ask R to set up a listening socket. The data will be a
      32-bit request ID and a 16-bit port number. R will tell L what happened
      with an `OPEN_TUNNEL_RESPONSE` message.
    - `OPEN_TUNNEL_RESPONSE`. R uses this type of message to notify L about
      the result of an `OPEN_TUNNEL` or `OPEN_REVERSE_TUNNEL` request. The
      data is the 32-bit ID that was sent with the request, so that L can tell 
      which request this reponse is for. The next 32-bit value is a status 
      code. I won't enumerate them now, but I imagine we'll need a list of 
      possible errors
    - `TUNNEL_DATA`. (Symmetric; L or R can use this in the same way). The
      data is the 32-bit tunnel ID that was given in the `OPEN_TUNNEL` (or
      `OPEN_REVERSE_TUNNEL`) message, followed by whatever data is supposed
      to go on that tunnel. So the header's size field will be `4+data_sz`.
    - `CLOSE_TUNNEL`. Idea: this should use same request ID from the 
      `OPEN_TUNNEL`/`OPEN_REVERSE_TUNNEL` message to simplify the code. Anyway,
      it instructs R to shutdown and close the sockets associate with the 
      given tunnel
    - `CLOSE`. Instructs R to shutdown+close all tunnels that were previosuly
      opened by L, and to also close the connection to L.
    - `DBG_MSG`. Just tells R to print a string (the data from this message) 
      onto the console. Can be used for rudimentary chatting.
    - Note: future features will require their own messages. For example, we'll
      probably need one to start encrypting traffic. I may also put in a helper
      function to help hole punching

## TODO list:
- [x] Rewrite to use bufferevents
- [x] Clean up memory management
    - Implement the tcpconn_gc
    - Try to free tcpconns at the right place (right now I close the
      socket the instant it connects, but this will change once we
      start reading and writing)
- [x] Implement `DBG_MSG` and `CLOSE` as part of bringing up the message-parsing stuff
- [x] Implement list_open_connections and other bookeeping stuff
- [/] Implement tunnel open and close messages
- [x] Implement tunnel data handling (i.e. not just dealing with
      `TUNNEL_DATA` messages, but also polling our local tunnel endpoint
      so we can create the `TUNNEL_DATA` messages in the first place)
- [ ] Properly deal with closed connections
- [ ] Tidy up the loose ends for memory management

## Possible future features
_**(`*` indicates a planned feature)**_
  - Support SOCKS proxying
  - `*` DNS resolution
  - UDP support
  - Built-in chatting would be nice, but it's not really necessary
    given that you can just write a chat program to use a tunnel.
    Even netcat can do a basic version of this
  - Built-in file sharing would be nice too, but once again this 
    could be implemented as a feature on top of a tunnel. (Still, 
    built-in would really be much nicer!!)
  - `*` Encryption
  - Keep stats for amount of data sent/received
  - Set up a matchmaker server and helper functions to simplify 
    hole punching
  - Have a GUI instead of a CLI
  - Have nice forwarding features so that if you are connected to
    a tnlr instance, it becomes easier to connect to tnlr instances
    that that one is connected to already. ("Friend-of-a-friend").
  - Make an easy "what is my ip" function (can maybe use shell escapes
    to run `curl ifconfig.me`)
  - Try to guarantee liveness with timeouts? This is tricky because we might
    want a tunnel to stay open a really long time. So maybe we should use some
    kind of keep-alive messages? Who knows
  - `*` UDP support (can't use UDP for `tcpconn`s, but OK for tunnels)
  - `*` Windows support (ugh)
  - Would be nice to let user give symbolic names to `tcpconn`s and `tunnel`s
  - If I actually wanted to distribute this we would have to think about security.
    We would need rules for deciding when to accept tunnel requests, and probably
    some form of identification via keys. The scope creep is real.