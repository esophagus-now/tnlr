//Copied out from https://gitlab.com/mahkoe/mmlib

//THINGS THAT ARE BROKEN:
//
// - This doesn't compile in cygwin. The winsock header defines its own 
//   select function, which clashes with cygwin's select function (in 
//   cygwin, stdio.h includes sys/select.h)
// - On MinGW, I used to have to manually define a few constants, but even
//   though I'm pretty sure I haven't updated my MinGW, I actually don't
//   need them anymore (they're in an #if 0 block for the moment). What is
//   going on here?

//This header file tries to only make "cosmetic" changes to OS APIs. The end
//goal is a set of portable functions

#ifdef IMPLEMENT
	#ifndef OS_COMMON_H_IMPLEMENTED
		#define OS_COMMON_H_IMPLEMENTED 1
		#define SHOULD_INCLUDE 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#else
	#ifndef OS_COMMON_H
		#define OS_COMMON_H 1
		#define SHOULD_INCLUDE 1
	#else
		#define SHOULD_INCLUDE 0
	#endif
#endif

#if SHOULD_INCLUDE
#undef SHOULD_INCLUDE

#ifdef IMPLEMENT
#undef IMPLEMENT
#include "os_common.h"
#define IMPLEMENT 1
#endif

#if defined(_WIN32) || defined(_WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__CYGWIN__)
/////////////////////////////
// Definitions for Windows //
/////////////////////////////

    #include <winsock2.h>
    #include <Ws2tcpip.h>
    
    //From https://stackoverflow.com/a/20816961/2737696
	int inet_pton(int af, const char *src, void *dst)
    #ifndef IMPLEMENT
    ;
    #else
	{
	  struct sockaddr_storage ss;
	  int size = sizeof(ss);
	  char src_copy[INET6_ADDRSTRLEN+1];

	  ZeroMemory(&ss, sizeof(ss));
	  /* stupid non-const API */
	  strncpy (src_copy, src, INET6_ADDRSTRLEN+1);
	  src_copy[INET6_ADDRSTRLEN] = 0;

	  if (WSAStringToAddress(src_copy, af, NULL, (struct sockaddr *)&ss, &size) == 0) {
		switch(af) {
		  case AF_INET:
		*(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
		return 1;
		  case AF_INET6:
		*(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
		return 1;
		}
	  }
	  return 0;
	}
    #endif
    
    #ifndef IMPLEMENT
    typedef SOCKET sockfd;
    #endif
    
    #define write(x,y,z) send(x,y,z,0)
    #define read(x,y,z) recv(x,y,z,0)
    
    //Good lord this is complicated... from:
    //https://docs.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup
    int os_common_startup() 
    #ifndef IMPLEMENT
    ;
    #else 
    {
        WORD wVersionRequested;
        WSADATA wsaData;
        int err;

        /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
        wVersionRequested = MAKEWORD(2, 2);

        err = WSAStartup(wVersionRequested, &wsaData);
        if (err != 0) {
            /* Tell the user that we could not find a usable */
            /* Winsock DLL.                                  */
            printf("WSAStartup failed with error: %d\n", err);
            return 1;
        }

        /* Confirm that the WinSock DLL supports 2.2.*/
        /* Note that if the DLL supports versions greater    */
        /* than 2.2 in addition to 2.2, it will still return */
        /* 2.2 in wVersion since that is the version we      */
        /* requested.                                        */

        if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
            /* Tell the user that we could not find a usable */
            /* WinSock DLL.                                  */
            printf("Could not find a usable version of Winsock.dll\n");
            WSACleanup();
            return 1;
        }
        
        return 0;
    }
    #endif
    
    
    void os_common_cleanup() 
    #ifndef IMPLEMENT
    ;
    #else 
    {
        WSACleanup();
    }
    #endif
    
    //The things windows and MinGW make me do...
    //https://virtuallyfun.com/wordpress/2017/02/11/wsapoll-mingw/
    //Actually this fix appears to no longer be needed (last time I tried it
    //was in 2020). 
    #if 0
    #if defined(__MINGW32__) || defined(__MINGW64__) || defined(__CYGWIN__)
        typedef struct pollfd {
            SOCKET fd;
            SHORT events;
            SHORT revents;
        } WSAPOLLFD, *PWSAPOLLFD, FAR *LPWSAPOLLFD;
        WINSOCK_API_LINKAGE int WSAAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, INT timeout);
        
        /* Event flag definitions for WSAPoll(). */
        #define POLLRDNORM  0x0100
        #define POLLRDBAND  0x0200
        #define POLLIN      (POLLRDNORM | POLLRDBAND)
        #define POLLPRI     0x0400

        #define POLLWRNORM  0x0010
        #define POLLOUT     (POLLWRNORM)
        #define POLLWRBAND  0x0020

        #define POLLERR     0x0001
        #define POLLHUP     0x0002
        #define POLLNVAL    0x0004
    #endif
    #endif
    
    #define poll(x,y,z) WSAPoll(x,y,z)
    //Windows really makes things complicated...
    #define fix_rc(x) (((x) == SOCKET_ERROR) ? WSAGetLastError() : 0)
    #define sockerrno WSAGetLastError()
    
    char* sockstrerror(int x) 
    #ifndef IMPLEMENT
    ;
    #else
    {
        static char line[80];
        //It's unbelievable how the Window API is so overcomplicated!
        //From https://stackoverflow.com/a/16723307/2737696
        char *s = NULL;
        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
                       NULL, x,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       (LPSTR)&s, 0, NULL);
        strcpy(line, s);
        LocalFree(s);
        
        //sprintf(line, "some inscrutable windows-related problem with code %d", x);
        return line;
    }
    #endif
    
    //From https://stackoverflow.com/a/26085827/2737696
    #include <stdint.h> // portable: uint64_t   MSVC: __int64 

    struct timezone;

    int gettimeofday(struct timeval * tp, struct timezone * tzp)
    #ifndef IMPLEMENT
    ;
    #else
    {
        // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
        // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
        // until 00:00:00 January 1, 1970 
        static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

        SYSTEMTIME  system_time;
        FILETIME    file_time;
        uint64_t    time;
        
        GetSystemTime( &system_time );
        SystemTimeToFileTime( &system_time, &file_time );
        time =  ((uint64_t)file_time.dwLowDateTime )      ;
        time += ((uint64_t)file_time.dwHighDateTime) << 32;

        tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
        tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
        
        return 0;
    }
    #endif
    
#else
    ///////////////////////////
    // Definitions for Linux //
    ///////////////////////////

    #include <sys/socket.h>
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <sys/time.h>
    #include <poll.h>
    #include <errno.h>
    
    #ifndef IMPLEMENT
    typedef struct pollfd pollfd;
    typedef int sockfd;
    #define INVALID_SOCKET -1
    #define closesocket(x) close(x)
    #define fix_rc(x) (x)
    #define sockerrno errno
    #define sockstrerror(x) strerror(x)
    #endif
    
    int os_common_startup() 
    #ifndef IMPLEMENT
    ;
    #else 
    {
        return 0;
    }
    #endif
    
    
    void os_common_cleanup() 
    #ifndef IMPLEMENT
    ;
    #else 
    {
        return;
    }
    #endif
    
#endif
/* At this point, the following API can be used on either Linux or Windows:
// - poll can be used as normal, except for some subtle bug ms won't fix
//   (see https://curl.haxx.se/mail/lib-2012-10/0038.html). I'm just hoping
//   that I won't run into this
// - save socket file descriptors into sockfd variables
// - Use closesocket() instead of close()
// - Use INVALID_SOCKET instead of -1
// - gettimeofday can be used as normal
// - (ugh) need to wrap all calls with fix_rc() because the winsock versions
//   return INVALID_SOCKET rather than a meaningful error code. By the way,
//	 0 means success and anything else is an error number that can be passed
//	 into sockstrerror()
// - Use sockerrno instead of errno
// - Use sockstrerror(x) instead of strerror(x)
*/

#else
#undef SHOULD_INCLUDE
#endif
