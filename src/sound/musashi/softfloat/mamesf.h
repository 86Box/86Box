/*----------------------------------------------------------------------------
| One of the macros `BIGENDIAN' or `LITTLEENDIAN' must be defined.
*----------------------------------------------------------------------------*/
#ifdef LSB_FIRST
#define LITTLEENDIAN
#else
#define BIGENDIAN
#endif

/*----------------------------------------------------------------------------
| The macro `BITS64' can be defined to indicate that 64-bit integer types are
| supported by the compiler.
*----------------------------------------------------------------------------*/
#define BITS64

/*----------------------------------------------------------------------------
| Each of the following `typedef's defines the most convenient type that holds
| integers of at least as many bits as specified.  For example, `uint8' should
| be the most convenient type that can hold unsigned integers of as many as
| 8 bits.  The `flag' type must be able to hold either a 0 or 1.  For most
| implementations of C, `flag', `uint8', and `int8' should all be `typedef'ed
| to the same as `int'.
*----------------------------------------------------------------------------*/

typedef sint8 flag;
typedef sint8 int8;
typedef sint16 int16;
typedef sint32 int32;
typedef sint64 int64;

/*----------------------------------------------------------------------------
| Each of the following `typedef's defines a type that holds integers
| of _exactly_ the number of bits specified.  For instance, for most
| implementation of C, `bits16' and `sbits16' should be `typedef'ed to
| `unsigned short int' and `signed short int' (or `short int'), respectively.
*----------------------------------------------------------------------------*/
typedef uint8 bits8;
typedef sint8 sbits8;
typedef uint16 bits16;
typedef sint16 sbits16;
typedef uint32 bits32;
typedef sint32 sbits32;
typedef uint64 bits64;
typedef sint64 sbits64;

/*----------------------------------------------------------------------------
| The `LIT64' macro takes as its argument a textual integer literal and
| if necessary ``marks'' the literal as having a 64-bit integer type.
| For example, the GNU C Compiler (`gcc') requires that 64-bit literals be
| appended with the letters `LL' standing for `long long', which is `gcc's
| name for the 64-bit integer type.  Some compilers may allow `LIT64' to be
| defined as the identity macro:  `#define LIT64( a ) a'.
*----------------------------------------------------------------------------*/
#define LIT64( a ) a##ULL

/*----------------------------------------------------------------------------
| The macro `INLINE' can be used before functions that should be inlined.  If
| a compiler does not support explicit inlining, this macro should be defined
| to be `static'.
*----------------------------------------------------------------------------*/
// MAME defines INLINE
