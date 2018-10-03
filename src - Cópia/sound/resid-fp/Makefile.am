## Process this file with automake to create Makefile.in

AR = @AR@

noinst_LIBRARIES = libresidfp.a

libresidfp_a_SOURCES = sid.cc voice.cc wave.cc envelope.cc filter.cc extfilt.cc pot.cc version.cc convolve.cc $(noinst_DATA:.dat=.cc)

BUILT_SOURCES = $(noinst_DATA:.dat=.cc)

noinst_HEADERS = sid.h voice.h wave.h envelope.h filter.h extfilt.h pot.h

noinst_DATA = wave6581_PST.dat wave6581_PS_.dat wave6581_P_T.dat wave6581__ST.dat wave8580_PST.dat wave8580_PS_.dat wave8580_P_T.dat wave8580__ST.dat

noinst_SCRIPTS = samp2src.pl

EXTRA_DIST = $(noinst_HEADERS) $(noinst_DATA) $(noinst_SCRIPTS) README.VICE convolve-sse.cc

SUFFIXES = .dat

.dat.cc:
	$(PERL) $(srcdir)/samp2src.pl $* $< $(srcdir)/$@

if USE_SSE
convolve-sse.o: convolve-sse.cc
	$(CXXCOMPILE) -msse -c -o $@ `test -f '$<' || echo '$(srcdir)/'`$<

libresidfp_a_LIBADD = convolve-sse.o
endif
