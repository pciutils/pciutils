/*
 *	The PCI Library -- Access to i386 I/O ports on Windows
 *
 *	Copyright (c) 2004 Alexander Stock <stock.alexander@gmx.de>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include <io.h>
#include <conio.h>
#include <windows.h>

#define outb(x,y) _outp(y,x)
#define outw(x,y) _outpw(y,x)
#define outl(x,y) _outpd(y,x)

#define inb(x) _inp(x)
#define inw(x) _inpw(x)
#define inl(x) _inpd(x)

static int intel_iopl_set = -1;

static int
intel_setup_io(void)
{
  if (intel_iopl_set < 0)
    {
      typedef int (*MYPROC)(void);
      MYPROC InitializeWinIo;
      HMODULE lib;

      intel_iopl_set = 0;

      lib = LoadLibrary("WinIo.dll");
      if (!lib)
	{
	  fprintf(stderr, "libpci: Couldn't load WinIo.dll.\n");
	  return 0;
	}
      /* XXX: Is this really needed? --mj */
      GetProcAddress(lib, "InitializeWinIo");

      InitializeWinIo = (MYPROC) GetProcAddress(lib, "InitializeWinIo");
      if (!InitializeWinIo)
	{
	  fprintf(stderr, "libpci: Couldn't find InitializeWinIo function.\n");
	  return 0;
	}

      if (!InitializeWinIo())
	{
	  fprintf(stderr, "libpci: InitializeWinIo() failed.\n");
	  return 0;
	}

      intel_iopl_set = 1;
    }
  return intel_iopl_set;
}

static inline void
intel_cleanup_io(void)
{
  //TODO: DeInitializeWinIo!
  //intel_iopl_set = -1;
}
