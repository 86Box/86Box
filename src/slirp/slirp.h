#ifndef __COMMON_H__
#define __COMMON_H__

#define SLIRP_VERSION "Cockatrice special"

#define CONFIG_QEMU

#ifndef CONFIG_QEMU
#include "version.h"
#endif
#include "config.h"
#include "slirp_config.h"

#ifdef _WIN32
#ifdef __GNUC__		//MINGW?
# include <inttypes.h>	
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
typedef char *SLIRPcaddr_t;
typedef int socklen_t;
typedef unsigned long ioctlsockopt_t;
#else
typedef unsigned char 	u_int8_t;
typedef char			int8_t;
typedef unsigned char	uint8_t;
typedef unsigned short 	u_int16_t;
typedef unsigned short 	uint16_t;
typedef short			int16_t;
typedef unsigned int 	u_int32_t;
typedef unsigned int	uint32_t;
typedef	int				int32_t;

typedef unsigned __int64 u_int64_t;
typedef char 		*SLIRPcaddr_t;
typedef int 		socklen_t;
typedef unsigned long 	ioctlsockopt_t;

#endif

# include <winsock2.h>	//needs to be on top otherwise, it'll pull in winsock1
# include <windows.h>

# include <sys/timeb.h>
# include <iphlpapi.h>

# define USE_FIONBIO 1
# define EWOULDBLOCK WSAEWOULDBLOCK
# define EINPROGRESS WSAEINPROGRESS
# define ENOTCONN WSAENOTCONN
# define EHOSTUNREACH WSAEHOSTUNREACH
# define ENETUNREACH WSAENETUNREACH
# define ECONNREFUSED WSAECONNREFUSED

/* Basilisk II Router defines those */
# define udp_read_completion slirp_udp_read_completion
# define write_udp slirp_write_udp
# define init_udp slirp_init_udp
# define final_udp slirp_final_udp
#else
typedef int ioctlsockopt_t;
# define ioctlsocket ioctl
# define closesocket(s) close(s)
# define O_BINARY 0
#endif

#include <sys/types.h>
#ifdef HAVE_SYS_BITYPES_H
# include <sys/bitypes.h>
#endif
#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#ifndef _MSC_VER
#include <sys/time.h>
#else
#include <time.h>
#endif

#ifdef NEED_TYPEDEFS
typedef char int8_t;
typedef unsigned char u_int8_t;

# if SIZEOF_SHORT == 2
    typedef short int16_t;
    typedef unsigned short u_int16_t;
# else
#  if SIZEOF_INT == 2
    typedef int int16_t;
    typedef unsigned int u_int16_t;
#  else
    #error Cannot find a type with sizeof() == 2
#  endif
# endif

# if SIZEOF_SHORT == 4
   typedef short int32_t;
   typedef unsigned short u_int32_t;
# else
#  if SIZEOF_INT == 4
    typedef int int32_t;
    typedef unsigned int u_int32_t;
#  else
    #error Cannot find a type with sizeof() == 4
#  endif
# endif
#endif /* NEED_TYPEDEFS */

/* Basilisk II types glue */
typedef u_int8_t uint8;
typedef u_int16_t uint16;
typedef u_int32_t uint32;

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif

#include <stdio.h>
#include <errno.h>

#ifndef HAVE_MEMMOVE
#define memmove(x, y, z) bcopy(y, x, z)
#endif

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_STRING_H
# include <string.h>
#else
#ifndef _MSC_VER
# include <strings.h>
#else
#include <string.h>
#endif
#endif

#ifndef _WIN32
#include <sys/uio.h>
#endif

#ifndef _P
#ifndef NO_PROTOTYPES
#  define   _P(x)   x
#else
#  define   _P(x)   ()
#endif
#endif

#ifndef _WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef GETTIMEOFDAY_ONE_ARG
#define gettimeofday(x, y) gettimeofday(x)
#endif

/* Systems lacking strdup() definition in <string.h>. */
#if defined(ultrix)
char *strdup _P((const char *));
#endif

/* Systems lacking malloc() definition in <stdlib.h>. */
#if defined(ultrix) || defined(hcx)
void *malloc _P((size_t arg));
void free _P((void *ptr));
#endif

#ifndef HAVE_INET_ATON
int inet_aton _P((const char *cp, struct in_addr *ia));
#endif

#include <fcntl.h>
#ifndef NO_UNIX_SOCKETS
#include <sys/un.h>
#endif
#include <signal.h>
#ifdef HAVE_SYS_SIGNAL_H
# include <sys/signal.h>
#endif
#ifndef _WIN32
#include <sys/socket.h>
#endif

#if defined(HAVE_SYS_IOCTL_H)
# include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

#ifdef USE_PPP
#include <ppp/slirppp.h>
#endif

#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include <sys/stat.h>

/* Avoid conflicting with the libc insque() and remque(), which
   have different prototypes. */
#define insque slirp_insque
#define remque slirp_remque

#ifdef HAVE_SYS_STROPTS_H
#include <sys/stropts.h>
#endif

#include "debug.h"

#if defined __GNUC__
#define PACKED__ __attribute__ ((packed))
#elif defined __sgi
#define PRAGMA_PACK_SUPPORTED 1
#define PACK_END 0
#define PACKED__
#elif _MSC_VER
//#define PRAGMA_PACK_SUPPORTED 1
//#define PACK_END 4
#define PACKED__
#else
#error "Packed attribute or pragma shall be supported"
#endif

#if defined(_MSC_VER)
#pragma pack(push, 1)
#endif

#include "ip.h"
#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_var.h"
#include "tcpip.h"
#include "udp.h"
#include "icmp_var.h"
#include "mbuf.h"
#include "sbuf.h"
#include "socket.h"
#include "if.h"
#include "main.h"
#include "misc.h"
#include "ctl.h"
#ifdef USE_PPP
#include "ppp/pppd.h"
#include "ppp/ppp.h"
#endif

#include "bootp.h"
#include "tftp.h"
#include "libslirp.h"

extern struct ttys *ttys_unit[MAX_INTERFACES];

#ifndef NULL
#define NULL (void *)0
#endif

#ifndef FULL_BOLT
void if_start _P((void));
#else
void if_start _P((struct ttys *));
#endif

#ifdef BAD_SPRINTF
# define vsprintf vsprintf_len
# define sprintf sprintf_len
 extern int vsprintf_len _P((char *, const char *, va_list));
 extern int sprintf_len _P((char *, const char *, ...));
#endif

#ifdef DECLARE_SPRINTF
# ifndef BAD_SPRINTF
 extern int vsprintf _P((char *, const char *, va_list));
# endif
 extern int vfprintf _P((FILE *, const char *, va_list));
#endif

#ifndef HAVE_STRERROR
#ifndef _MSC_VER
 extern char *strerror _P((int error));
 #define HAVE_STRERROR
#endif
#endif

#ifndef HAVE_INDEX
 char *index _P((const char *, int));
#endif

#ifndef HAVE_GETHOSTID
 long gethostid _P((void));
#endif

void lprint _P((const char *, ...));

extern int do_echo;

#ifdef _MSC_VER
#define inline
#endif

#if SIZEOF_CHAR_P == 4
# define insque_32 insque
# define remque_32 remque
#else
 extern inline void insque_32 _P((void *, void *));
 extern inline void remque_32 _P((void *));
#endif

#ifndef _WIN32
#include <netdb.h>
#endif

#define DEFAULT_BAUD 115200

/* cksum.c */
int cksum(struct SLIRPmbuf *m, int len);

/* if.c */
void if_init _P((void));
void if_output _P((struct SLIRPsocket *, struct SLIRPmbuf *));

/* ip_input.c */
void ip_init _P((void));
void ip_input _P((struct SLIRPmbuf *));
struct ip * ip_reass _P((register struct ipasfrag *, register struct ipq *));
void ip_freef _P((struct ipq *));
void ip_enq _P((register struct ipasfrag *, register struct ipasfrag *));
void ip_deq _P((register struct ipasfrag *));
void ip_slowtimo _P((void));
void ip_stripoptions _P((register struct SLIRPmbuf *, struct SLIRPmbuf *));

/* ip_output.c */
int ip_output _P((struct SLIRPsocket *, struct SLIRPmbuf *));

/* tcp_input.c */
int tcp_reass _P((register struct tcpcb *, register struct tcpiphdr *, struct SLIRPmbuf *));
void tcp_input _P((register struct SLIRPmbuf *, int, struct SLIRPsocket *));
void tcp_dooptions _P((struct tcpcb *, u_char *, int, struct tcpiphdr *));
void tcp_xmit_timer _P((register struct tcpcb *, int));
int tcp_mss _P((register struct tcpcb *, u_int));

/* tcp_output.c */
int tcp_output _P((register struct tcpcb *));
void tcp_setpersist _P((register struct tcpcb *));

/* tcp_subr.c */
void tcp_init _P((void));
void tcp_template _P((struct tcpcb *));
void tcp_respond _P((struct tcpcb *, register struct tcpiphdr *, register struct SLIRPmbuf *, tcp_seq, tcp_seq, int));
struct tcpcb * tcp_newtcpcb _P((struct SLIRPsocket *));
struct tcpcb * tcp_close _P((register struct tcpcb *));
void tcp_drain _P((void));
void tcp_sockclosed _P((struct tcpcb *));
int tcp_fconnect _P((struct SLIRPsocket *));
void tcp_connect _P((struct SLIRPsocket *));
int tcp_attach _P((struct SLIRPsocket *));
u_int8_t tcp_tos _P((struct SLIRPsocket *));
int tcp_emu _P((struct SLIRPsocket *, struct SLIRPmbuf *));
int tcp_ctl _P((struct SLIRPsocket *));
struct tcpcb *tcp_drop(struct tcpcb *tp, int err);


#if defined(_MSC_VER)
#pragma pack(pop)
#endif

#ifdef USE_PPP
#define MIN_MRU MINMRU
#define MAX_MRU MAXMRU
#else
#define MIN_MRU 128
#define MAX_MRU 16384
#endif

#ifndef _WIN32
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) > (y) ? (x) : (y))
#endif

#ifdef _WIN32
#undef errno
#define errno (WSAGetLastError())
#endif

#define PROBE_CONN

#endif
