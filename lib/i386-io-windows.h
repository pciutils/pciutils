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

#ifndef __GNUC__
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
