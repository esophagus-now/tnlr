//This is my standard method now of putting code into other files.
//Ever since I learned this trick to get around needing to have both
//header and C files, I never want to go back to the old way
#ifdef IMPLEMENT
	#ifndef ENUMS_H_IMPLEMENTED
		#define SHOULD_INCLUDE 1
		#define ENUMS_H_IMPLEMENTED 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef ENUMS_H
		#define SHOULD_INCLUDE 1
		#define ENUMS_H 1
	#else 
		#define SHOULD_INCLUDE 0
	#endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE

#ifdef IMPLEMENT
#undef IMPLEMENT
#include "enums.h"
#define IMPLEMENT 1
#endif


//////////////////////////
//Connection status enum//
//////////////////////////
#ifndef IMPLEMENT
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
#endif

#ifndef IMPLEMENT
extern
#endif
char const * const TNLR_STATUS_STRINGS[]
#ifdef IMPLEMENT
= {
    #define X(x) #x
    TNLR_STATUS_IDENTS
    #undef X
}
#endif
;

//////////////////////
//Message types enum//
//////////////////////
#ifndef IMPLEMENT
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
#endif

#ifndef IMPLEMENT
extern
#endif
char const *const TNLR_MSG_TYPE_STRINGS[] 
#ifdef IMPLEMENT
= {
    #define X(x) #x
    TNLR_MSG_TYPE_IDENTS
    #undef X
}
#endif
;


//////////////////////////////////////
//tcpconn message parsing state enum//
//////////////////////////////////////
#ifndef IMPLEMENT
typedef enum {
    TCPCONN_READ_HEADER,
    TCPCONN_READ_DATA,
    TCPCONN_EXEC_MSG //Header+data done reading, now we do what it says
} tcpconn_state_e;
#endif

#else
#undef SHOULD_INCLUDE
#endif