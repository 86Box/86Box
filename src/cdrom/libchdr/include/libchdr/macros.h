#ifndef LIBCHDR_MACROS_H
#define LIBCHDR_MACROS_H

#undef ARRAY_LENGTH
#define ARRAY_LENGTH(x) (sizeof(x)/sizeof(x[0]))

#undef MAX
#undef MIN
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#ifndef CHDR_INLINE
	#if defined(_WIN32) || defined(__INTEL_COMPILER)
		#define CHDR_INLINE __inline
	#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
		#define CHDR_INLINE inline
	#elif defined(__GNUC__)
		#define CHDR_INLINE __inline__
	#else
		#define CHDR_INLINE
	#endif
#endif

#endif /* LIBCHDR_MACROS_H */
