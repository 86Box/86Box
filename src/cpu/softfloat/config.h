/* config.h.  Generated from config.h.in by configure.  */
//
//  Copyright (C) 2001-2021  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//

//
// config.h.in is distributed in the source TAR file.  When you run
// the configure script, it generates config.h with some changes
// according to your build environment.  For example, in config.h.in,
// SIZEOF_UNSIGNED_CHAR is set to 0.  When configure produces config.h
// it will change "0" to the detected value for your system.
//
// config.h contains ONLY preprocessor #defines and a few typedefs.
// It must be included by both C and C++ files, so it must not
// contain anything language dependent such as a class declaration.
//

#ifndef _BX_CONFIG_H_
#define _BX_CONFIG_H_ 1

///////////////////////////////////////////////////////////////////
// USER CONFIGURABLE OPTIONS : EDIT ONLY OPTIONS IN THIS SECTION //
///////////////////////////////////////////////////////////////////


#if 1
// quit_sim is defined in gui/siminterface.h
#define BX_EXIT(x)  SIM->quit_sim (x)
#else
// provide the real main and the usual exit.
#define BX_EXIT(x)  ::exit(x)
#endif

// if simulating Linux, this provides a few more debugging options
// such as tracing all system calls.
#define BX_DEBUG_LINUX 0

// adds support for the GNU readline library in the debugger command
// prompt.
#define HAVE_LIBREADLINE 0
#define HAVE_READLINE_HISTORY_H 0

// Define to 1 if you have <locale.h>
#define HAVE_LOCALE_H 0

// I rebuilt the code which provides timers to IO devices.
// Setting this to 1 will introduce a little code which
// will panic out if cases which shouldn't happen occur.
// Set this to 0 for optimal performance.
#define BX_TIMER_DEBUG 0

// Settable A20 line.  For efficiency, you can disable
// having a settable A20 line, eliminating conditional
// code for every physical memory access.  You'll have
// to tell your software not to mess with the A20 line,
// and accept it as always being on if you change this.
//   1 = use settable A20 line. (normal)
//   0 = A20 is like the rest of the address lines

#define BX_SUPPORT_A20 1

// Processor Instructions Per Second
// To find out what value to use for the 'ips' directive
// in your '.bochsrc' file, set BX_SHOW_IPS to 1, and
// run the software in bochs you plan to use most.  Bochs
// will print out periodic IPS ratings.  This will change
// based on the processor mode at the time, and various
// other factors.  You'll get a reasonable estimate though.
// When you're done, reset BX_SHOW_IPS to 0, do a
// 'make all-clean', then 'make' again.

#define BX_SHOW_IPS 1


#define MSVC_TARGET 0
#if defined(_MSC_VER) && defined(MSVC_TARGET)
#if defined(_M_X64) && (MSVC_TARGET != 64)
#error Bochs not configured for MSVC WIN64
#elif !defined(_M_X64) && (MSVC_TARGET != 32)
#error Bochs not configured for MSVC WIN32
#endif
#endif

#if (BX_SHOW_IPS) && (defined(__MINGW32__) || defined(_MSC_VER))
#define        SIGALRM         14
#endif

// Compile in support for DMA & FLOPPY IO.  You'll need this
// if you plan to use the floppy drive emulation.  But if
// you're environment doesn't require it, you can change
// it to 0.

#define BX_DMA_FLOPPY_IO 1

// Default number of Megs of memory to emulate.  The
// 'megs:' directive in the '.bochsrc' file overrides this,
// allowing per-run settings.

#define BX_DEFAULT_MEM_MEGS 32

// CPU level emulation. Default level is set in the configure script.
// BX_CPU_LEVEL defines the CPU level to emulate.
#define BX_CPU_LEVEL 6

// emulate x86-64 instruction set?
#define BX_SUPPORT_X86_64 0

// emulate long physical address (>32 bit)
#define BX_PHY_ADDRESS_LONG 1

#define BX_HAVE_SLEEP 1
#define BX_HAVE_MSLEEP 0
#define BX_HAVE_USLEEP 1
#define BX_HAVE_NANOSLEEP 1
#define BX_HAVE_ABORT 1
#define BX_HAVE_SOCKLEN_T 0
#define BX_HAVE_SOCKADDR_IN_SIN_LEN 0
#define BX_HAVE_GETTIMEOFDAY 1
#if defined(WIN32)
#define BX_HAVE_REALTIME_USEC 1
#else
#define BX_HAVE_REALTIME_USEC (BX_HAVE_GETTIMEOFDAY)
#endif
#define BX_HAVE_MKSTEMP 1
#define BX_HAVE_SYS_MMAN_H 0
#define BX_HAVE_XPM_H 0
#define BX_HAVE_XRANDR_H 0
#define BX_HAVE_TIMELOCAL 0
#define BX_HAVE_GMTIME 1
#define BX_HAVE_MKTIME 1
#define BX_HAVE_TMPFILE64 0
#define BX_HAVE_FSEEK64 0
#define BX_HAVE_FSEEKO64 1
#define BX_HAVE_NET_IF_H 0
#define BX_HAVE___BUILTIN_BSWAP32 1
#define BX_HAVE___BUILTIN_BSWAP64 1
#define BX_HAVE_SSIZE_T 1

// This turns on Roland Mainz's idle hack.  Presently it is specific to the X11
// and term gui. If people try to enable it elsewhere, give a compile error
// after the gui definition so that they don't waste their time trying.
#define BX_USE_IDLE_HACK 0

// Minimum Emulated IPS.
// This is used in the realtime PIT as well as for checking the
// IPS value set in the config file.
#define BX_MIN_IPS 1000000

// Minimum and maximum values for SMP quantum variable. Defines
// how many instructions each CPU could execute in one
// shot (one cpu_loop call)
#define BX_SMP_QUANTUM_MIN  1
#define BX_SMP_QUANTUM_MAX 32

// Use Static Member Funtions to eliminate 'this' pointer passing
// If you want the efficiency of 'C', you can make all the
// members of the C++ CPU class to be static.
// This defaults to 1 since it should improve performance, but when
// SMP mode is enabled, it will be turned off by configure.
#define BX_USE_CPU_SMF 1

#define BX_USE_MEM_SMF 1

// Use static member functions in IO DEVice emulation modules.
// For efficiency, use C like functions for IO handling,
// and declare a device instance at compile time,
// instead of using 'new' and storing the pointer.  This
// eliminates some overhead, especially for high-use IO
// devices like the disk drive.
//   1 = Use static member efficiency (normal)
//   0 = Use nonstatic member functions (use only if you need
//       multiple instances of a device class

#define BX_USE_HD_SMF       1  // Hard drive
#define BX_USE_BIOS_SMF     1  // BIOS
#define BX_USE_CMOS_SMF     1  // CMOS
#define BX_USE_DMA_SMF      1  // DMA
#define BX_USE_FD_SMF       1  // Floppy
#define BX_USE_KEY_SMF      1  // Keyboard
#define BX_USE_PAR_SMF      1  // Parallel
#define BX_USE_PIC_SMF      1  // PIC
#define BX_USE_PIT_SMF      1  // PIT
#define BX_USE_SER_SMF      1  // Serial
#define BX_USE_UM_SMF       1  // Unmapped
#define BX_USE_VGA_SMF      1  // VGA
#define BX_USE_SB16_SMF     1  // SB 16 soundcard
#define BX_USE_ES1370_SMF   1  // ES1370 soundcard
#define BX_USE_DEV_SMF      1  // System Devices (port92)
#define BX_USE_PCI_SMF      1  // PCI
#define BX_USE_P2I_SMF      1  // PCI-to-ISA bridge
#define BX_USE_PIDE_SMF     1  // PCI-IDE
#define BX_USE_PCIDEV_SMF   1  // PCI-DEV
#define BX_USE_USB_UHCI_SMF 1  // USB UHCI hub
#define BX_USE_USB_OHCI_SMF 1  // USB OHCI hub
#define BX_USE_USB_EHCI_SMF 1  // USB EHCI hub
#define BX_USE_USB_XHCI_SMF 1  // USB xHCI hub
#define BX_USE_PCIPNIC_SMF  1  // PCI pseudo NIC
#define BX_USE_EFI_SMF      1  // External FPU IRQ
#define BX_USE_GAMEPORT_SMF 1  // Gameport
#define BX_USE_CIRRUS_SMF   1  // SVGA Cirrus
#define BX_USE_BUSM_SMF     1  // Bus Mouse
#define BX_USE_ACPI_SMF     1  // ACPI

#define BX_PLUGINS 0
#define BX_HAVE_LTDL 0
#define BX_HAVE_DLFCN_H 0

#if BX_PLUGINS && \
  (   !BX_USE_HD_SMF || !BX_USE_BIOS_SMF || !BX_USE_CMOS_SMF \
   || !BX_USE_DMA_SMF || !BX_USE_FD_SMF || !BX_USE_KEY_SMF \
   || !BX_USE_PAR_SMF || !BX_USE_PIC_SMF || !BX_USE_PIT_SMF \
   || !BX_USE_SER_SMF || !BX_USE_UM_SMF || !BX_USE_VGA_SMF \
   || !BX_USE_SB16_SMF || !BX_USE_ES1370_SMF || !BX_USE_DEV_SMF \
   || !BX_USE_PCI_SMF || !BX_USE_P2I_SMF || !BX_USE_USB_UHCI_SMF \
   || !BX_USE_USB_OHCI_SMF || !BX_USE_USB_EHCI_SMF || !BX_USE_USB_XHCI_SMF \
   || !BX_USE_PCIPNIC_SMF || !BX_USE_PIDE_SMF || !BX_USE_ACPI_SMF \
   || !BX_USE_EFI_SMF || !BX_USE_GAMEPORT_SMF || !BX_USE_PCIDEV_SMF \
   || !BX_USE_CIRRUS_SMF)
#error You must use SMF to have plugins
#endif

#define USE_RAW_SERIAL 0

// This option enables RAM file backing for large guest memory with a smaller
// amount host memory, without causing a panic when host memory is exhausted.
#define BX_LARGE_RAMFILE 1

// This option defines the number of supported ATA channels.
// There are up to two drives per ATA channel.
#define BX_MAX_ATA_CHANNEL 4

#if (BX_MAX_ATA_CHANNEL>4 || BX_MAX_ATA_CHANNEL<1)
  #error "BX_MAX_ATA_CHANNEL should be between 1 and 4"
#endif

// =================================================================
// BEGIN: OPTIONAL DEBUGGER SECTION
//
// These options are only used if you compile in support for the
// native command line debugging environment.  Typically, the debugger
// is not used, and this section can be ignored.
// =================================================================

// Compile in support for virtual/linear/physical breakpoints.
// Enable only those you need. Recommend using only linear
// breakpoints, unless you need others. Less supported means
// slightly faster execution time.
#define BX_DBG_MAX_VIR_BPOINTS 16
#define BX_DBG_MAX_LIN_BPOINTS 16
#define BX_DBG_MAX_PHY_BPOINTS 16

#define BX_DBG_MAX_WATCHPONTS  16

// max file pathname size for debugger commands
#define BX_MAX_PATH     256
// max nesting level for debug scripts including other scripts
#define BX_INFILE_DEPTH  10
// use this command to include (nest) debug scripts
#define BX_INCLUDE_CMD   "source"

// Make a call to command line debugger extensions.  If set to 1,
// a call is made.  An external routine has a chance to process
// the command.  If it does, than the debugger ignores the command.
#define BX_DBG_EXTENSIONS 0

// =================================================================
// END: OPTIONAL DEBUGGER SECTION
// =================================================================

//////////////////////////////////////////////////////////////////////
// END OF USER CONFIGURABLE OPTIONS : DON'T EDIT ANYTHING BELOW !!! //
// THIS IS GENERATED BY THE ./configure SCRIPT                      //
//////////////////////////////////////////////////////////////////////


#define BX_WITH_X11 0
#define BX_WITH_WIN32 1
#define BX_WITH_MACOS 0
#define BX_WITH_CARBON 0
#define BX_WITH_NOGUI 0
#define BX_WITH_TERM 0
#define BX_WITH_RFB 0
#define BX_WITH_VNCSRV 0
#define BX_WITH_AMIGAOS 0
#define BX_WITH_SDL 0
#define BX_WITH_SDL2 0
#define BX_WITH_WX 0

// BX_USE_TEXTCONFIG should be set to 1 unless Bochs is compiled
// for wxWidgets only.
#define BX_USE_TEXTCONFIG 1

// BX_USE_GUI should be set to 1 for all guis with VGA console support
// for 'textconfig'.
#define BX_USE_GUI_CONSOLE 0

// BX_USE_WIN32CONFIG should be set to 1 on WIN32 for the guis
// "win32", "sdl" and "sdl2" only.
#if BX_USE_TEXTCONFIG && defined(WIN32) && (BX_WITH_WIN32 || BX_WITH_SDL || BX_WITH_SDL2)
  #define BX_USE_WIN32CONFIG 1
#else
  #define BX_USE_WIN32CONFIG 0
#endif

// set to 1 if wxMSW is a unicode version
#define WX_MSW_UNICODE 1
// set to GDK major version for wxGTK
#define WX_GDK_VERSION 0

// A certain functions must NOT be fastcall even if compiled with fastcall
// option, and those are callbacks from Windows which are defined either
// as cdecl or stdcall. The entry point main() also has to remain cdecl.
#ifndef CDECL
#if defined(_MSC_VER)
  #define CDECL __cdecl
#else
  #define CDECL
#endif
#endif

// add special export symbols for win32 DLL building.  The main code must
// have __declspec(dllexport) on variables, functions, or classes that the
// plugins can access.  The plugins should #define PLUGGABLE which will
// activate the __declspec(dllimport) instead.
#if (defined(WIN32) || defined(__CYGWIN__)) && !defined(BXIMAGE)
#  if BX_PLUGINS && defined(BX_PLUGGABLE)
//   #warning I will import DLL symbols from Bochs main program.
#    define BOCHSAPI __declspec(dllimport)
#  elif BX_PLUGINS
//   #warning I will export DLL symbols.
#    define BOCHSAPI __declspec(dllexport)
#  endif
#endif
#ifndef BOCHSAPI
#  define BOCHSAPI
#endif

#if defined(__CYGWIN__)
// Make BOCHSAPI_CYGONLY exactly the same as BOCHSAPI.  This symbol
// will be used for any cases where Cygwin requires a special tag
// but VC++ does not.
#define BOCHSAPI_CYGONLY BOCHSAPI
#else
// define the symbol to be empty
#define BOCHSAPI_CYGONLY /*empty*/
#endif

#if defined(_MSC_VER)
// Make BOCHSAPI_MSVCONLY exactly the same as BOCHSAPI.  This symbol
// will be used for any cases where VC++ requires a special tag
// but Cygwin does not.
#define BOCHSAPI_MSVCONLY BOCHSAPI
#else
// define the symbol to be empty
#define BOCHSAPI_MSVCONLY /*empty*/
#endif

#define BX_DEFAULT_CONFIG_INTERFACE "win32config"
#define BX_DEFAULT_DISPLAY_LIBRARY "win32"

// Roland Mainz's idle hack is presently specific to X11. If people try to
// enable it elsewhere, give a compile error so that they don't waste their
// time trying.
#if (BX_USE_IDLE_HACK && !BX_WITH_X11 && !BX_WITH_TERM)
#  error IDLE_HACK will only work with the X11 or term gui. Correct configure args and retry.
#endif

#define WORDS_BIGENDIAN 0

#define SIZEOF_UNSIGNED_CHAR 1
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define SIZEOF_INT_P 4

#define BX_64BIT_CONSTANTS_USE_LL 1
#if BX_64BIT_CONSTANTS_USE_LL
// doesn't work on Microsoft Visual C++, maybe others
#define BX_CONST64(x)  (x##LL)
#elif defined(_MSC_VER)
#define BX_CONST64(x)  (x##I64)
#else
#define BX_CONST64(x)  (x)
#endif

#if defined(WIN32)
  typedef unsigned char      Bit8u;
  typedef   signed char      Bit8s;
  typedef unsigned short     Bit16u;
  typedef   signed short     Bit16s;
  typedef unsigned int       Bit32u;
  typedef   signed int       Bit32s;
  typedef unsigned long long Bit64u;
  typedef   signed long long Bit64s;
#elif BX_WITH_MACOS
  typedef unsigned char      Bit8u;
  typedef   signed char      Bit8s;
  typedef unsigned short     Bit16u;
  typedef   signed short     Bit16s;
  typedef unsigned int       Bit32u;
  typedef   signed int       Bit32s;
  typedef unsigned long long Bit64u;
  typedef   signed long long Bit64s;
#else

// Unix like platforms

#if SIZEOF_UNSIGNED_CHAR != 1
#  error "sizeof (unsigned char) != 1"
#else
  typedef unsigned char Bit8u;
  typedef   signed char Bit8s;
#endif

#if SIZEOF_UNSIGNED_SHORT != 2
#  error "sizeof (unsigned short) != 2"
#else
  typedef unsigned short Bit16u;
  typedef   signed short Bit16s;
#endif

#if SIZEOF_UNSIGNED_INT == 4
  typedef unsigned int Bit32u;
  typedef   signed int Bit32s;
#elif SIZEOF_UNSIGNED_LONG == 4
  typedef unsigned long Bit32u;
  typedef   signed long Bit32s;
#else
#  error "can't find sizeof(type) of 4 bytes!"
#endif

#if SIZEOF_UNSIGNED_LONG == 8
  typedef unsigned long Bit64u;
  typedef   signed long Bit64s;
#elif SIZEOF_UNSIGNED_LONG_LONG == 8
  typedef unsigned long long Bit64u;
  typedef   signed long long Bit64s;
#else
#  error "can't find data type of 8 bytes"
#endif

#endif

#define GET32L(val64) ((Bit32u)(((Bit64u)(val64)) & 0xFFFFFFFF))
#define GET32H(val64) ((Bit32u)(((Bit64u)(val64)) >> 32))

// now that Bit32u and Bit64u exist, defined bx_address
#if BX_SUPPORT_X86_64
typedef Bit64u bx_address;
#else
typedef Bit32u bx_address;
#endif

// define physical and linear address types
typedef bx_address bx_lin_address;

#if BX_SUPPORT_X86_64
#define BX_LIN_ADDRESS_WIDTH 48
#else
#define BX_LIN_ADDRESS_WIDTH 32
#endif

#if BX_PHY_ADDRESS_LONG
typedef Bit64u bx_phy_address;
#if BX_CPU_LEVEL == 5
  #define BX_PHY_ADDRESS_WIDTH 36
#else
  #define BX_PHY_ADDRESS_WIDTH 40
#endif
#else
typedef Bit32u bx_phy_address;
#define BX_PHY_ADDRESS_WIDTH 32
#endif

// small sanity check
#if BX_PHY_ADDRESS_LONG
  #if (BX_PHY_ADDRESS_WIDTH <= 32)
    #error "BX_PHY_ADDRESS_LONG implies emulated physical address width > 32 bit"
  #endif
#endif

// technically, in an 8 bit signed the real minimum is -128, not -127.
// But if you decide to negate -128 you tend to get -128 again, so it's
// better not to use the absolute maximum in the signed range.
#define BX_MAX_BIT64U ( (Bit64u) -1           )
#define BX_MIN_BIT64U ( 0                     )
#define BX_MAX_BIT64S ( ((Bit64u) -1) >> 1    )
#define BX_MIN_BIT64S ( (Bit64s)(1ULL << 63)  )
#define BX_MAX_BIT32U ( (Bit32u) -1           )
#define BX_MIN_BIT32U ( 0                     )
#define BX_MAX_BIT32S ( ((Bit32u) -1) >> 1    )
#define BX_MIN_BIT32S ( (Bit32s)(1U << 31)    )
#define BX_MAX_BIT16U ( (Bit16u) -1           )
#define BX_MIN_BIT16U ( 0                     )
#define BX_MAX_BIT16S ( ((Bit16u) -1) >> 1    )
#define BX_MIN_BIT16S ( (Bit16s)-(((Bit16u) -1) >> 1) - 1)
#define BX_MAX_BIT8U  ( (Bit8u) -1            )
#define BX_MIN_BIT8U  ( 0                     )
#define BX_MAX_BIT8S  ( ((Bit8u) -1) >> 1     )
#define BX_MIN_BIT8S  ( (Bit8s)-(((Bit8u) -1) >> 1) - 1)


// create an unsigned integer type that is the same size as a pointer.
// You can typecast a pointer to a bx_pr_equiv_t without losing any
// bits (and without getting the compiler excited).
#if SIZEOF_INT_P == 4
  typedef Bit32u bx_ptr_equiv_t;
#elif SIZEOF_INT_P == 8
  typedef Bit64u bx_ptr_equiv_t;
#else
#  error "could not define bx_ptr_equiv_t to size of int*"
#endif

#if BX_WITH_MACOS
#  define bx_ptr_t char *
#else
#  define bx_ptr_t void *
#endif

#if defined(WIN32)
#  define BX_LITTLE_ENDIAN
#elif BX_WITH_MACOS
#  define BX_BIG_ENDIAN
#else
#if WORDS_BIGENDIAN
#  define BX_BIG_ENDIAN
#else
#  define BX_LITTLE_ENDIAN
#endif
#endif // defined(WIN32)

// for now only term.cc requires a GUI sighandler.
#define BX_GUI_SIGHANDLER (BX_WITH_TERM)

#define HAVE_SIGACTION 1

// Use BX_CPP_INLINE for all C++ inline functions.
#define BX_CPP_INLINE static __inline

#ifdef __GNUC__

// Some helpful compiler hints for compilers that allow them; GCC for now.
//
// BX_CPP_AlignN(n):
//   Align a construct on an n-byte boundary.
//
// BX_CPP_AttrPrintf(formatArg, firstArg):
//   This function takes printf-like arguments, so the compiler can check
//   the consistency of the format string and the matching arguments.
//   'formatArg' is the parameter number (starting from 1) of the format
//   string argument.  'firstArg' is the parameter number of the 1st argument
//   to check against the string argument.  NOTE: For non-static member
//   functions, the this-ptr is argument number 1 but is invisible on
//   the function prototype declaration - but you still have to count it.
//
// BX_CPP_AttrNoReturn():
//   This function never returns.  The compiler can optimize-out following
//   code accordingly.

#define BX_CPP_AlignN(n) __attribute__ ((aligned (n)))
#define BX_CPP_AttrPrintf(formatArg, firstArg) \
                          __attribute__ ((format (printf, formatArg, firstArg)))
#define BX_CPP_AttrNoReturn() __attribute__ ((noreturn))

#ifndef likely
#define likely(x)      __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x)    __builtin_expect(!!(x), 0)
#endif

#else

#define BX_CPP_AlignN(n) /* Not supported. */
#define BX_CPP_AttrPrintf(formatArg, firstArg)  /* Not supported. */
#define BX_CPP_AttrNoReturn() /* Not supported. */

#ifndef likely
#define likely(x)      (x)
#endif

#ifndef unlikely
#define unlikely(x)    (x)
#endif

#endif

#define BX_GDBSTUB 0
#define BX_DEBUGGER 0
#define BX_DEBUGGER_GUI 0

#define BX_INSTRUMENTATION 0

// enable BX_DEBUG/BX_ERROR/BX_INFO messages
#define BX_NO_LOGGING 0

// enable BX_ASSERT checks
#define BX_ASSERT_ENABLE 0

// enable statistics collection
#define BX_ENABLE_STATISTICS 1

#define BX_SUPPORT_ALIGNMENT_CHECK 1
#define BX_SUPPORT_FPU 1
#define BX_SUPPORT_3DNOW 0
#define BX_SUPPORT_PKEYS 0
#define BX_SUPPORT_CET 0
#define BX_SUPPORT_MONITOR_MWAIT 1
#define BX_SUPPORT_PERFMON 1
#define BX_SUPPORT_MEMTYPE 0
#define BX_SUPPORT_SVM 0
#define BX_SUPPORT_VMX 0
#define BX_SUPPORT_AVX 0
#define BX_SUPPORT_EVEX 0

#if BX_SUPPORT_SVM && BX_SUPPORT_X86_64 == 0
  #error "SVM require x86-64 support"
#endif

#if BX_SUPPORT_VMX >= 2 && BX_SUPPORT_X86_64 == 0
  #error "VMX=2 require x86-64 support"
#endif

#if BX_SUPPORT_AVX && BX_SUPPORT_X86_64 == 0
  #error "AVX require x86-64 support"
#endif

#if BX_SUPPORT_EVEX && BX_SUPPORT_AVX == 0
  #error "EVEX and AVX-512 support require AVX to be compiled in"
#endif

#define BX_SUPPORT_REPEAT_SPEEDUPS 0
#define BX_SUPPORT_HANDLERS_CHAINING_SPEEDUPS 0
#define BX_ENABLE_TRACE_LINKING 0

#if (BX_DEBUGGER || BX_GDBSTUB) && BX_SUPPORT_HANDLERS_CHAINING_SPEEDUPS
 #error "Handler-chaining-speedups are not supported together with internal debugger or gdb-stub!"
#endif

#if BX_SUPPORT_3DNOW
  #define BX_CPU_VENDOR_INTEL 0
#else
  #define BX_CPU_VENDOR_INTEL 1
#endif

// Maximum CPUID vendor and brand string lengths
#define BX_CPUID_VENDOR_LEN 12
#define BX_CPUID_BRAND_LEN  48

#define BX_CONFIGURE_MSRS 1

#if (BX_SUPPORT_ALIGNMENT_CHECK && BX_CPU_LEVEL < 4)
  #error Alignment exception check is not supported in i386 !
#endif

#if (BX_CONFIGURE_MSRS && BX_CPU_LEVEL < 5)
  #error MSRs are supported only with CPU level >= 5 !
#endif

#if (!BX_SUPPORT_FPU && BX_CPU_LEVEL > 4)
  #error With CPU level > 4, you must enable FPU support !
#endif

#if (BX_SUPPORT_FPU && BX_CPU_LEVEL < 3)
  #error "FPU cannot be compiled without cpu level >= 3 !"
#endif

#if (BX_CPU_LEVEL<6 && BX_SUPPORT_VMX)
  #error "VMX only supported with CPU_LEVEL >= 6 !"
#endif

#if BX_SUPPORT_X86_64
// Sanity checks to ensure that you cannot accidently use conflicting options.

#if BX_CPU_LEVEL < 6
  #error "X86-64 requires cpu level 6 or greater !"
#endif
#endif

// We have tested the following combinations:
//  * processors=1, bootstrap=0, ioapic_id=1   (uniprocessor system)
//  * processors=2, bootstrap=0, ioapic_id=2
//  * processors=4, bootstrap=0, ioapic_id=4
//  * processors=8, bootstrap=0, ioapic_id=8
#define BX_SUPPORT_SMP 0
#define BX_BOOTSTRAP_PROCESSOR 0

// For P6 and Pentium family processors the local APIC ID feild is 4 bits
// APIC_MAX_ID indicate broadcast so it can't be used as valid APIC ID
#define BX_MAX_SMP_THREADS_SUPPORTED 0xfe /* leave APIC ID for I/O APIC */

// include in APIC models, required for a multiprocessor system.
#if BX_SUPPORT_SMP || BX_CPU_LEVEL >= 5
  #define BX_SUPPORT_APIC 1
#else
  #define BX_SUPPORT_APIC 0
#endif

#define BX_HAVE_GETENV 1
#define BX_HAVE_SETENV 0
#define BX_HAVE_SELECT 1
#define BX_HAVE_SNPRINTF 1
#define BX_HAVE_VSNPRINTF 1
#define BX_HAVE_STRTOULL 1
#define BX_HAVE_STRTOUQ 0
#define BX_HAVE_STRDUP 1
#define BX_HAVE_STRREV 1
#define BX_HAVE_STRICMP 1
#define BX_HAVE_STRCASECMP 1

// used in term gui
#define BX_HAVE_COLOR_SET 0
#define BX_HAVE_MVHLINE 0
#define BX_HAVE_MVVLINE 0


// set if your compiler does not understand __attribute__ after a struct
#define BX_NO_ATTRIBUTES 0
#if BX_NO_ATTRIBUTES
#define GCC_ATTRIBUTE(x) /* attribute not supported */
#else
#define GCC_ATTRIBUTE __attribute__
#endif

// set to use fast function calls
#define BX_FAST_FUNC_CALL 0

// On gcc2.95+ x86 only
#if BX_FAST_FUNC_CALL && defined(__i386__) && defined(__GNUC__) && (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95))
#if BX_USE_CPU_SMF == 1
#  define BX_CPP_AttrRegparmN(X) __attribute__((regparm(X)))
#else
// FIXME: BX_FAST_FUNC_CALL doesn't work with BX_USE_CPU_SMF = 0
#  define BX_CPP_AttrRegparmN(X) /* Not defined */
#endif
#else
#  define BX_CPP_AttrRegparmN(X) /* Not defined */
#endif

// set if you do have <set>, used in bx_debug/dbg_main.c
#define BX_HAVE_SET 1

// set if you do have <set.h>, used in bx_debug/dbg_main.c
#define BX_HAVE_SET_H 0

// set if you do have <map>, used in bx_debug/dbg_main.c
#define BX_HAVE_MAP 1

// set if you do have <map.h>, used in bx_debug/dbg_main.c
#define BX_HAVE_MAP_H 0

// Support x86 hardware debugger registers and facilities.
// These are the debug facilities offered by the x86 architecture,
// not the optional built-in debugger.
#define BX_X86_DEBUGGER 0

// limited i440FX PCI support
#define BX_SUPPORT_PCI 1

// Experimental host PCI device mapping
#define BX_SUPPORT_PCIDEV 0

#if (BX_SUPPORT_PCIDEV && !BX_SUPPORT_PCI)
  #error To enable PCI host device mapping, you must also enable PCI
#endif

// CLGD54XX emulation
#define BX_SUPPORT_CLGD54XX 1

// Experimental 3dfx Voodoo (SST-1/2) emulation
#define BX_SUPPORT_VOODOO 0

// USB host controllers
#define BX_SUPPORT_USB_UHCI 0
#define BX_SUPPORT_USB_OHCI 0
#define BX_SUPPORT_USB_EHCI 0
#define BX_SUPPORT_USB_XHCI 0
#define BX_SUPPORT_PCIUSB \
  (BX_SUPPORT_USB_UHCI || BX_SUPPORT_USB_OHCI || BX_SUPPORT_USB_EHCI || BX_SUPPORT_USB_XHCI)

#if (BX_SUPPORT_PCIUSB && !BX_SUPPORT_PCI)
  #error To enable USB, you must also enable PCI
#endif

#if (BX_SUPPORT_USB_EHCI && !BX_SUPPORT_USB_UHCI)
  #error To enable EHCI, you must also enable UHCI
#endif

// MS bus mouse support
#define BX_SUPPORT_BUSMOUSE 1

#define BX_SUPPORT_CDROM 1

#if BX_SUPPORT_CDROM
   // This is the C++ class name to use if we are supporting
   // low-level CDROM.
#  define LOWLEVEL_CDROM cdrom_win32_c
#endif

// NE2K network emulation
#define BX_SUPPORT_NE2K 1

// Pseudo PCI NIC
#define BX_SUPPORT_PCIPNIC 0

#if (BX_SUPPORT_PCIPNIC && !BX_SUPPORT_PCI)
  #error To enable the PCI pseudo NIC, you must also enable PCI
#endif

// Intel(R) Gigabit Ethernet
#define BX_SUPPORT_E1000 0

#if (BX_SUPPORT_E1000 && !BX_SUPPORT_PCI)
  #error To enable the E1000 NIC, you must also enable PCI
#endif

// this enables the lowlevel stuff below if one of the NICs is present
#define BX_NETWORKING 1

// which networking modules will be enabled
// determined by configure script
#define BX_NETMOD_FBSD    0
#define BX_NETMOD_LINUX   0
#define BX_NETMOD_WIN32 1
#define BX_NETMOD_TAP     0
#define BX_NETMOD_TUNTAP  0
#define BX_NETMOD_VDE     0
#define BX_NETMOD_SLIRP 1
#define BX_NETMOD_SOCKET 1

// Soundcard and gameport support
#define BX_SUPPORT_SB16 1
#define BX_SUPPORT_ES1370 0
#define BX_SUPPORT_GAMEPORT 1
#define BX_SUPPORT_SOUNDLOW 1

// which sound lowlevel modules will be enabled
#define BX_HAVE_SOUND_ALSA 0
#define BX_HAVE_SOUND_OSS  0
#define BX_HAVE_SOUND_OSX  0
#define BX_HAVE_SOUND_SDL  0
#define BX_HAVE_SOUND_WIN 1

#if BX_SUPPORT_SOUNDLOW
// Determines which sound lowlevel driver is to be used as the default.
// Currently the following are available:
//    alsa   Output for Linux with ALSA PCM and sequencer interface
//    oss    Output for Linux, to /dev/dsp and /dev/midi00
//    osx    Output for MacOSX midi and wave device
//    sdl    Wave output with SDL/SDL2
//    win    Output for Windows midi and wave mappers
//    file   Wave and midi output to file
//    dummy  Dummy functions, no output
#define BX_SOUND_LOWLEVEL_NAME "win"
// resampling support
#define BX_HAVE_LIBSAMPLERATE  0
#define BX_HAVE_SOXR_LSR       0
// SDL2 audio capture support (version >= 2.0.5)
#define BX_HAVE_SDL2_AUDIO_CAPTURE 0
#endif

#if (BX_SUPPORT_ES1370 && !BX_SUPPORT_PCI)
  #error To enable the ES1370 soundcard, you must also enable PCI
#endif

// I/O Interface to debugger
#define BX_SUPPORT_IODEBUG 0

#ifdef WIN32
#define BX_FLOPPY0_NAME "Floppy Disk A:"
#define BX_FLOPPY1_NAME "Floppy Disk B:"
#else
#define BX_FLOPPY0_NAME "Floppy Disk 0"
#define BX_FLOPPY1_NAME "Floppy Disk 1"
#endif

#if defined(__GNUC__) && (__GNUC__ == 4 && __GNUC_MINOR__ == 0)
#error "gcc 4.0.0 is known to produce incorrect code which breaks Bochs emulation"
#endif

#endif  // _BX_CONFIG_H
