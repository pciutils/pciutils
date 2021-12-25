/*
 *	The PCI Library -- Access to i386 I/O ports on Windows
 *
 *	Copyright (c) 2004 Alexander Stock <stock.alexander@gmx.de>
 *	Copyright (c) 2006 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <io.h>
#include <windows.h>

#ifdef _MSC_VER
/* MSVC compiler provides I/O port intrinsics for both 32 and 64-bit modes. */
#pragma intrinsic(_outp)
#pragma intrinsic(_outpw)
#pragma intrinsic(_outpd)
#pragma intrinsic(_inp)
#pragma intrinsic(_inpw)
#pragma intrinsic(_inpd)
#elif defined(_WIN64) || defined(_UCRT)
/*
 * For other compilers I/O port intrinsics are available in <intrin.h> header
 * file either as inline/external functions or macros. Beware that <intrin.h>
 * names are different than MSVC intrinsics names and glibc function names.
 * Usage of <intrin.h> is also the prefered way for 64-bit mode or when using
 * new UCRT library.
 */
#include <intrin.h>
#define _outp(x,y) __outbyte(x,y)
#define _outpw(x,y) __outword(x,y)
#define _outpd(x,y) __outdword(x,y)
#define _inp(x) __inbyte(x)
#define _inpw(x) __inword(x)
#define _inpd(x) __indword(x)
#elif defined(__CRTDLL__)
/*
 * Old CRTDLL library does not provide I/O port functions. Even it is the oldest
 * CRT library it exists also in 64-bit variant. Implement I/O port functions
 * via inline assembly just for 32-bit mode as 64-bit mode uses above <intrin.h>
 * header.
 */
static inline int _outp(unsigned short port, int databyte)
{
  asm volatile ("outb %b0, %w1" : : "a" (databyte), "Nd" (port));
  return databyte;
}
static inline unsigned short _outpw(unsigned short port, unsigned short dataword)
{
  asm volatile ("outw %w0, %w1" : : "a" (dataword), "Nd" (port));
  return dataword;
}
static inline unsigned long _outpd(unsigned short port, unsigned long dataword)
{
  asm volatile ("outl %0, %w1" : : "a" (dataword), "Nd" (port));
  return dataword;
}
static inline int _inp(unsigned short port)
{
  unsigned char ret;
  asm volatile ("inb %w1, %0" : "=a" (ret) : "Nd" (port));
  return ret;
}
static inline unsigned short _inpw(unsigned short port)
{
  unsigned short ret;
  asm volatile ("inw %w1, %0" : "=a" (ret) : "Nd" (port));
  return ret;
}
static inline unsigned long _inpd(unsigned short port)
{
  unsigned long ret;
  asm volatile ("inl %w1, %0" : "=a" (ret) : "Nd" (port));
  return ret;
}
#elif !defined(__GNUC__)
/*
 * Old 32-bit MSVCRT (non-UCRT) library provides I/O port functions. Function
 * prototypes are defined in <conio.h> header file but they are missing in
 * some MinGW toolchains. So for GCC compiler define them manually.
 */
#include <conio.h>
#else
int _outp(unsigned short port, int databyte);
unsigned short _outpw(unsigned short port, unsigned short dataword);
unsigned long _outpd(unsigned short port, unsigned long dataword);
int _inp(unsigned short port);
unsigned short _inpw(unsigned short port);
unsigned long _inpd(unsigned short port);
#endif

#define outb(x,y) _outp(y,x)
#define outw(x,y) _outpw(y,x)
#define outl(x,y) _outpd(y,x)

#define inb(x) _inp(x)
#define inw(x) _inpw(x)
#define inl(x) _inpd(x)

static int
intel_setup_io(struct pci_access *a)
{
  typedef int (*MYPROC)(void);
  MYPROC InitializeWinIo;
  HMODULE lib;

#ifndef _WIN64
  /* 16/32-bit non-NT systems allow applications to access PCI I/O ports without any special setup. */
  OSVERSIONINFOA version;
  version.dwOSVersionInfoSize = sizeof(version);
  if (GetVersionExA(&version) && version.dwPlatformId < VER_PLATFORM_WIN32_NT)
    {
      a->debug("Detected 16/32-bit non-NT system, skipping NT setup...");
      return 1;
    }
#endif

  lib = LoadLibrary("WinIo.dll");
  if (!lib)
    {
      a->warning("i386-io-windows: Couldn't load WinIo.dll.");
      return 0;
    }
  /* XXX: Is this really needed? --mj */
  GetProcAddress(lib, "InitializeWinIo");

  InitializeWinIo = (MYPROC) GetProcAddress(lib, "InitializeWinIo");
  if (!InitializeWinIo)
    {
      a->warning("i386-io-windows: Couldn't find InitializeWinIo function.");
      return 0;
    }

  if (!InitializeWinIo())
    {
      a->warning("i386-io-windows: InitializeWinIo() failed.");
      return 0;
    }

  return 1;
}

static inline int
intel_cleanup_io(struct pci_access *a UNUSED)
{
  //TODO: DeInitializeWinIo!
  return 1;
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
