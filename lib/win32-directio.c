/*
 *      The PCI Library -- List PCI devices on Win32 via DirectIO library.
 *
 *      Copyright (c) 2011-2012 Jernej Simoncic <jernej+s-pciutils@eternallybored.org>
 *      Copyright (c) 2023 Aidan Khoury <aidan@revers.engineering>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL.
 */

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>
#include <stdio.h>

#include "internal.h"

#ifndef WIN64
#define DIRECTIO_LIB_NAME "DirectIOLib32.dll"
#else
#define DIRECTIO_LIB_NAME "DirectIOLibx64.dll"
#endif

typedef enum {
	psByte  = 1,
	psWord  = 2,
	psDWord = 3
} DIRECTIO_PORT_SIZE;

typedef BOOL (WINAPI *SETDLLDIRECTORY)(LPCSTR);

#define LIB_NONE 0
#define LIB_DIRECTIO 1
#define LIB_WINIO 2
static int lib_used = LIB_NONE;

/* DirectIO declarations */
typedef BOOL (WINAPI *DIRECTIO_INIT)(void);
typedef void (WINAPI *DIRECTIO_DEINIT)(void);
typedef BOOL (WINAPI *DIRECTIO_WRITEPORT)(ULONG,USHORT,DIRECTIO_PORT_SIZE);
typedef BOOL (WINAPI *DIRECTIO_READPORT)(PULONG,USHORT,DIRECTIO_PORT_SIZE);

static DIRECTIO_INIT DirectIO_Init;
static DIRECTIO_DEINIT DirectIO_DeInit;
static DIRECTIO_WRITEPORT DirectIO_WritePort;
static DIRECTIO_READPORT DirectIO_ReadPort;

/* WinIo declarations */
typedef BYTE (_stdcall* INITIALIZEWINIO)(void);
typedef void (_stdcall* SHUTDOWNWINIO)(void);
typedef BYTE (_stdcall* GETPORTVAL)(WORD,PDWORD,BYTE);
typedef BYTE (_stdcall* SETPORTVAL)(WORD,DWORD,BYTE);

static SHUTDOWNWINIO ShutdownWinIo;
static GETPORTVAL GetPortVal;
static SETPORTVAL SetPortVal;
static INITIALIZEWINIO InitializeWinIo;

static inline int
load_resource(struct pci_access *a, const char *name, BYTE **data, DWORD *length)
{
  const BYTE *res_data;
  DWORD res_length;

  const HRSRC resource = FindResourceA(NULL, name, (const char *)RT_RCDATA);
  if (!resource) 
    {
      a->error("FindResourceW failed, error %lu\n", GetLastError());
      return 0;
    }

  res_data = LockResource(LoadResource(GetModuleHandleW(NULL), resource));
  res_length = SizeofResource(GetModuleHandleW(NULL), resource);

  *data = malloc(res_length);
  if (!*data) 
    {
      a->error("resource allocation failed, error %lu\n", GetLastError());
      return 0;
    }
  *length = res_length;

  memcpy(*data, res_data, *length);
  return 1;
}

static inline int
directio_setup_io(struct pci_access *a)
{
  HMODULE lib;
  SETDLLDIRECTORY fnSetDllDirectory;

  if (lib_used != LIB_NONE)
    return lib_used;

  /* remove current directory from DLL search path */
  fnSetDllDirectory = (SETDLLDIRECTORY)GetProcAddress(GetModuleHandle("kernel32"), "SetDllDirectoryA");
  if (fnSetDllDirectory)
    fnSetDllDirectory("");

#ifndef WIN64
  if ((GetVersion() & 0x80000000) == 0)
    { /* running on NT, try DirectIo first */
#endif
      /* Load the binary from .rsrc and load onto disk */
      int rc = -1;
      BYTE *directio_data;
      DWORD directio_size;
      if (load_resource(a, "DIRECTIO_BINARY", &directio_data, &directio_size))
        {
          FILE *const directio_fp = fopen(DIRECTIO_LIB_NAME, "wb");
          if (directio_fp)
            {
              if (fwrite(directio_data, 1, directio_size, directio_fp) == directio_size)
                rc = 0;
              fclose(directio_fp);
            }
          if (directio_data)
            free(directio_data); 
        }

      if (rc != 0)
        {
          a->debug("Failed to load DirectIO library!\n");
          return LIB_NONE;
        }

      lib = LoadLibrary(DIRECTIO_LIB_NAME);
      if (!lib)
        { /* DirectIO loading failed, try WinIO instead */
        #ifdef WIN64
          lib = LoadLibrary("WinIo64.dll");
        #else
          lib = LoadLibrary("WinIo32.dll");
          if (!lib)
            { /* WinIo 3 loading failed, try loading WinIo 2 */
              lib = LoadLibrary("WinIo.dll");
            }
        #endif
          if (!lib)
            {
              a->warning("i386-io-windows: Neither DirectIO, nor WinIo library could be loaded.\n");
              return 0;
            }
          else
              lib_used = LIB_WINIO;
        }
      else
          lib_used = LIB_DIRECTIO;
#ifndef WIN64
    }
  else
    { /* running on Win9x, only try loading WinIo 2 */
      lib = LoadLibrary("WinIo.dll");
      if (!lib)
        {
          a->warning("i386-io-windows: WinIo library could not be loaded.\n");
          return LIB_NONE;
        }
      else
          lib_used = LIB_WINIO;
    }
#endif

#define GETPROC(n,d)   n = (d) GetProcAddress(lib, #n); \
                               if (!n) \
                                 { \
                                   a->warning("i386-io-windows: Couldn't find " #n " function.\n"); \
                                   return 0; \
                                 }


  if (lib_used == LIB_DIRECTIO)
    {
      GETPROC(DirectIO_Init, DIRECTIO_INIT);
      GETPROC(DirectIO_DeInit, DIRECTIO_DEINIT);
      GETPROC(DirectIO_WritePort, DIRECTIO_WRITEPORT);
      GETPROC(DirectIO_ReadPort, DIRECTIO_READPORT);
    }
  else
    {
      GETPROC(InitializeWinIo, INITIALIZEWINIO);
      GETPROC(ShutdownWinIo, SHUTDOWNWINIO);
      GETPROC(GetPortVal, GETPORTVAL);
      GETPROC(SetPortVal, SETPORTVAL);
    }

  if (!((lib_used == LIB_DIRECTIO) ? DirectIO_Init() : InitializeWinIo()))
    {
      a->warning("i386-io-windows: IO library initialization failed. Try running from an elevated command prompt.\n");
      return LIB_NONE;
    }

  return lib_used;
}

static inline u8
directio_inb (u16 port)
{
  DWORD pv;

  if (lib_used == LIB_DIRECTIO)
    {
      if (DirectIO_ReadPort(&pv, port, psByte))
          return (u8)pv;
    }
  else
    {
      if (GetPortVal(port, &pv, 1))
          return (u8)pv;
    }
  return 0;
}

static inline u16
directio_inw (u16 port)
{
  DWORD pv;

  if (lib_used == LIB_DIRECTIO)
    {
      if (DirectIO_ReadPort(&pv, port, psWord))
          return (u16)pv;
    }
  else
    {
      if (GetPortVal(port, &pv, 2))
          return (u16)pv;
    }
  return 0;
}

static inline u32
directio_inl (u16 port)
{
  DWORD pv;

  if (lib_used == LIB_DIRECTIO)
    {
      if (DirectIO_ReadPort(&pv, port, psDWord))
          return (u32)pv;
    }
  else
    {
      if (GetPortVal(port, &pv, 4))
        return (u32)pv;
    }
  return 0;
}

static inline void
directio_outb (u8 value, u16 port)
{
  if (lib_used == LIB_DIRECTIO)
      DirectIO_WritePort(value, port, psByte);
  else
      SetPortVal(port, value, 1);
}

static inline void
directio_outw (u16 value, u16 port)
{
  if (lib_used == LIB_DIRECTIO)
      DirectIO_WritePort(value, port, psWord);
  else
      SetPortVal(port, value, 2);
}

static inline void
directio_outl (u32 value, u16 port)
{
  if (lib_used == LIB_DIRECTIO)
      DirectIO_WritePort(value, port, psDWord);
  else
      SetPortVal(port, value, 4);
}

/*
 * Before we decide to use direct hardware access mechanisms, we try to do some
 * trivial checks to ensure it at least _seems_ to be working -- we just test
 * whether bus 00 contains a host bridge (this is similar to checking
 * techniques used in XFree86, but ours should be more reliable since we
 * attempt to make use of direct access hints provided by the PCI BIOS).
 *
 * This should be close to trivial, but it isn't, because there are buggy
 * chipsets (yes, you guessed it, by Intel and Compaq) that have no class ID.
 */

static inline int
directio_sanity_check(struct pci_access *a, struct pci_methods *m)
{
  struct pci_dev d;

  memset(&d, 0, sizeof(d));
  a->debug("directio: sanity check ...");
  d.bus = 0;
  d.func = 0;
  for (d.dev = 0; d.dev < 32; d.dev++)
    {
      u16 class, vendor;
      if (m->read(&d, PCI_CLASS_DEVICE, (byte *) &class, sizeof(class)) &&
	  (class == cpu_to_le16(PCI_CLASS_BRIDGE_HOST) || class == cpu_to_le16(PCI_CLASS_DISPLAY_VGA)) ||
	  m->read(&d, PCI_VENDOR_ID, (byte *) &vendor, sizeof(vendor)) &&
	  (vendor == cpu_to_le16(PCI_VENDOR_ID_INTEL) || vendor == cpu_to_le16(PCI_VENDOR_ID_COMPAQ)))
	{
	  a->debug("directio: sane at 0/%02x/0", d.dev);
	  return 1;
	}
    }
  a->error("directio: insane!");
  return 0;
}

static int
win32_directio_detect(struct pci_access *a)
{
  unsigned int tmp;
  int res = 0;

  if (!directio_setup_io(a)) {
    a->error("No permission to access I/O ports (administrator privileges required).");
    return 0;
  }

  directio_outb (0x01, 0xCFB);
  tmp = directio_inl (0xCF8);
  directio_outl (0x80000000, 0xCF8);
  if (directio_inl (0xCF8) == 0x80000000)
    res = 1;
  directio_outl (tmp, 0xCF8);

  if (res)
    res = directio_sanity_check(a, &pm_win32_directio);
  return res;
}

static void
win32_directio_init(struct pci_access *a)
{
  if (!directio_setup_io(a))
    a->error("No permission to access I/O ports (you probably have to be admin).");
}

static void
win32_directio_cleanup(struct pci_access *a UNUSED)
{
  if (lib_used == LIB_DIRECTIO)
    {
      DirectIO_DeInit();

      FILE *const directio_fp = fopen(DIRECTIO_LIB_NAME, "wb");
      if (directio_fp)
        {
          fclose(directio_fp);
          remove(DIRECTIO_LIB_NAME);
        }
    }
  else
      ShutdownWinIo();
}

static void
win32_directio_scan(struct pci_access *a)
{
  pci_generic_scan(a);
}

static void
win32_directio_fill_info(struct pci_dev *d, unsigned int flags)
{
  pci_generic_fill_info(d, flags);
}

static int
win32_directio_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  int res = 1;
  const int addr = 0xcfc + (pos&3);

  if (d->domain || pos >= 256)
    return 0;

  if (len != 1 && len != 2 && len != 4)
    return pci_generic_block_read(d, pos, buf, len);

  directio_outl(0x80000000 | ((d->bus & 0xff) << 16) | (PCI_DEVFN(d->dev, d->func) << 8) | (pos&~3), 0xcf8);

  switch (len)
    {
    case 1:
      buf[0] = directio_inb(addr);
      break;
    case 2:
      ((u16 *) buf)[0] = cpu_to_le16(directio_inw(addr));
      break;
    case 4:
      ((u32 *) buf)[0] = cpu_to_le32(directio_inl(addr));
      break;
    }

  return res;
}

static int
win32_directio_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  int res = 1;
  const int addr = 0xcfc + (pos&3);

  if (d->domain || pos >= 256)
    return 0;

  if (len != 1 && len != 2 && len != 4)
    return pci_generic_block_write(d, pos, buf, len);

  directio_outl(0x80000000 | ((d->bus & 0xff) << 16) | (PCI_DEVFN(d->dev, d->func) << 8) | (pos&~3), 0xcf8);

  switch (len)
    {
    case 1:
      directio_outb(buf[0], addr);
      break;
    case 2:
      directio_outw(le16_to_cpu(((u16 *) buf)[0]), addr);
      break;
    case 4:
      directio_outl(le32_to_cpu(((u32 *) buf)[0]), addr);
      break;
    }

  return res;
}

struct pci_methods pm_win32_directio = {
  "win32-directio",
  "Win32 PCI device listing via DirectIO library",
  NULL,                                 /* config */
  win32_directio_detect,
  win32_directio_init,
  win32_directio_cleanup,
  win32_directio_scan,
  win32_directio_fill_info,
  win32_directio_read,
  win32_directio_write,
  NULL,                                 /* read_vpd */
  NULL,                                 /* init_dev */
  NULL                                  /* cleanup_dev */
};