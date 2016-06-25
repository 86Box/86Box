CC=gcc
CFLAGS=-I. -O2 -Wall
DEPS =	bootp.h          config-host.h    config.h         ctl.h	\
	debug.h          icmp_var.h       if.h             ip.h		\
	ip_icmp.h        libslirp.h       main.h           mbuf.h	\
	misc.h           queue.h          sbuf.h           slirp.h	\
	slirp_config.h   socket.h         tcp.h            tcpip.h	\
	tcp_timer.h      tcp_var.h        tftp.h           udp.h

OBJ =	bootp.o        cksum.o        debug.o        if.o           ip_icmp.o		\
	ip_input.o     ip_output.o    mbuf.o         misc.o         queue.o		\
	sbuf.o         slirp.o        socket.o       tcp_input.o    tcp_output.o	\
	tcp_subr.o     tcp_timer.o    tftp.o         udp.o

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@ 

default: libslirp.a

clean:
	rm -f $(OBJ)
	rm -f libslirp.a

libslirp.a: $(OBJ)
	ar rcs $@ $^
	ranlib $@