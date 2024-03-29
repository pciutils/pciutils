/*
 *      The PCI Library -- PCI config space access using NT SysDbg interface
 *
 *      Copyright (c) 2022 Pali Rohár <pali@kernel.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <windows.h>

#include "internal.h"
#include "win32-helpers.h"

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL (NTSTATUS)0xC0000001
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED (NTSTATUS)0xC0000002
#endif
#ifndef STATUS_INVALID_INFO_CLASS
#define STATUS_INVALID_INFO_CLASS (NTSTATUS)0xC0000003
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED (NTSTATUS)0xC0000022
#endif
#ifndef STATUS_DEBUGGER_INACTIVE
#define STATUS_DEBUGGER_INACTIVE (NTSTATUS)0xC0000354
#endif

#ifndef BUS_DATA_TYPE
#define BUS_DATA_TYPE LONG
#endif
#ifndef PCIConfiguration
#define PCIConfiguration (BUS_DATA_TYPE)4
#endif

#ifndef SYSDBG_COMMAND
#define SYSDBG_COMMAND ULONG
#endif
#ifndef SysDbgReadBusData
#define SysDbgReadBusData (SYSDBG_COMMAND)18
#endif
#ifndef SysDbgWriteBusData
#define SysDbgWriteBusData (SYSDBG_COMMAND)19
#endif

#ifndef SYSDBG_BUS_DATA
typedef struct _SYSDBG_BUS_DATA {
  ULONG Address;
  PVOID Buffer;
  ULONG Request;
  BUS_DATA_TYPE BusDataType;
  ULONG BusNumber;
  ULONG SlotNumber;
} SYSDBG_BUS_DATA, *PSYSDBG_BUS_DATA;
#define SYSDBG_BUS_DATA SYSDBG_BUS_DATA
#endif

#ifndef PCI_SLOT_NUMBER
typedef struct _PCI_SLOT_NUMBER {
  union {
    struct {
      ULONG DeviceNumber:5;
      ULONG FunctionNumber:3;
      ULONG Reserved:24;
    } bits;
    ULONG AsULONG;
  } u;
} PCI_SLOT_NUMBER, *PPCI_SLOT_NUMBER;
#define PCI_SLOT_NUMBER PCI_SLOT_NUMBER
#endif

#ifdef NtSystemDebugControl
#undef NtSystemDebugControl
#endif
static NTSTATUS (NTAPI *MyNtSystemDebugControl)(SYSDBG_COMMAND Command, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength, PULONG ReturnLength);
#define NtSystemDebugControl MyNtSystemDebugControl

static BOOL debug_privilege_enabled;
static LUID luid_debug_privilege;
static BOOL revert_only_privilege;
static HANDLE revert_token;
static HMODULE ntdll;

static int win32_sysdbg_initialized;

static NTSTATUS
win32_sysdbg_pci_bus_data(BOOL WriteBusData, BYTE BusNumber, BYTE DeviceNumber, BYTE FunctionNumber, BYTE Address, PVOID Buffer, BYTE BufferSize, PULONG Length)
{
  SYSDBG_BUS_DATA sysdbg_cmd;
  PCI_SLOT_NUMBER pci_slot;

  if (!NtSystemDebugControl)
    return STATUS_NOT_IMPLEMENTED;

  memset(&pci_slot, 0, sizeof(pci_slot));
  memset(&sysdbg_cmd, 0, sizeof(sysdbg_cmd));

  sysdbg_cmd.Address = Address;
  sysdbg_cmd.Buffer = Buffer;
  sysdbg_cmd.Request = BufferSize;
  sysdbg_cmd.BusDataType = PCIConfiguration;
  sysdbg_cmd.BusNumber = BusNumber;
  pci_slot.u.bits.DeviceNumber = DeviceNumber;
  pci_slot.u.bits.FunctionNumber = FunctionNumber;
  sysdbg_cmd.SlotNumber = pci_slot.u.AsULONG;

  *Length = 0;
  return NtSystemDebugControl(WriteBusData ? SysDbgWriteBusData : SysDbgReadBusData, &sysdbg_cmd, sizeof(sysdbg_cmd), NULL, 0, Length);
}

static int
win32_sysdbg_setup(struct pci_access *a)
{
  UINT prev_error_mode;
  NTSTATUS status;
  ULONG ret_len;
  DWORD id;

  if (win32_sysdbg_initialized)
    return 1;

  prev_error_mode = win32_change_error_mode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  ntdll = LoadLibrary(TEXT("ntdll.dll"));
  win32_change_error_mode(prev_error_mode);
  if (!ntdll)
    {
      a->debug("Cannot open ntdll.dll library.");
      return 0;
    }

  NtSystemDebugControl = (LPVOID)GetProcAddress(ntdll, "NtSystemDebugControl");
  if (!NtSystemDebugControl)
    {
      a->debug("Function NtSystemDebugControl() is not supported.");
      FreeLibrary(ntdll);
      ntdll = NULL;
      return 0;
    }

  /*
   * Try to read PCI id register from PCI device 00:00.0.
   * If this device does not exist and NT SysDbg API is working then
   * NT SysDbg returns STATUS_UNSUCCESSFUL.
   */
  status = win32_sysdbg_pci_bus_data(FALSE, 0, 0, 0, 0, &id, sizeof(id), &ret_len);
  if ((status >= 0 && ret_len == sizeof(id)) || status == STATUS_UNSUCCESSFUL)
    {
      win32_sysdbg_initialized = 1;
      return 1;
    }
  else if (status != STATUS_ACCESS_DENIED)
    {
      if (status == STATUS_NOT_IMPLEMENTED || status == STATUS_INVALID_INFO_CLASS)
        a->debug("NT SysDbg is not supported.");
      else if (status == STATUS_DEBUGGER_INACTIVE)
        a->debug("NT SysDbg is disabled.");
      else
        a->debug("NT SysDbg returned error 0x%lx.", status);
      FreeLibrary(ntdll);
      ntdll = NULL;
      NtSystemDebugControl = NULL;
      return 0;
    }

  a->debug("NT SysDbg returned Access Denied, trying again with Debug privilege...");

  if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid_debug_privilege))
    {
      a->debug("Debug privilege is not supported.");
      FreeLibrary(ntdll);
      ntdll = NULL;
      NtSystemDebugControl = NULL;
      return 0;
    }

  if (!win32_enable_privilege(luid_debug_privilege, &revert_token, &revert_only_privilege))
    {
      a->debug("Cannot enable Debug privilege.");
      FreeLibrary(ntdll);
      ntdll = NULL;
      NtSystemDebugControl = NULL;
      return 0;
    }

  status = win32_sysdbg_pci_bus_data(FALSE, 0, 0, 0, 0, &id, sizeof(id), &ret_len);
  if ((status >= 0 && ret_len == sizeof(id)) || status == STATUS_UNSUCCESSFUL)
    {
      a->debug("Succeeded.");
      debug_privilege_enabled = TRUE;
      win32_sysdbg_initialized = 1;
      return 1;
    }

  win32_revert_privilege(luid_debug_privilege, revert_token, revert_only_privilege);
  revert_token = NULL;
  revert_only_privilege = FALSE;

  FreeLibrary(ntdll);
  ntdll = NULL;
  NtSystemDebugControl = NULL;

  if (status == STATUS_NOT_IMPLEMENTED || status == STATUS_INVALID_INFO_CLASS)
    a->debug("NT SysDbg is not supported.");
  else if (status == STATUS_DEBUGGER_INACTIVE)
    a->debug("NT SysDbg is disabled.");
  else if (status == STATUS_ACCESS_DENIED)
    a->debug("NT SysDbg returned Access Denied.");
  else
    a->debug("NT SysDbg returned error 0x%lx.", status);

  return 0;
}

static int
win32_sysdbg_detect(struct pci_access *a)
{
  if (!win32_sysdbg_setup(a))
    return 0;

  return 1;
}

static void
win32_sysdbg_init(struct pci_access *a)
{
  if (!win32_sysdbg_setup(a))
    {
      a->debug("\n");
      a->error("NT SysDbg PCI Bus Data interface cannot be accessed.");
    }
}

static void
win32_sysdbg_cleanup(struct pci_access *a UNUSED)
{
  if (!win32_sysdbg_initialized)
    return;

  if (debug_privilege_enabled)
    {
      win32_revert_privilege(luid_debug_privilege, revert_token, revert_only_privilege);
      revert_token = NULL;
      revert_only_privilege = FALSE;
      debug_privilege_enabled = FALSE;
    }

  FreeLibrary(ntdll);
  ntdll = NULL;
  NtSystemDebugControl = NULL;

  win32_sysdbg_initialized = 0;
}

static int
win32_sysdbg_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  NTSTATUS status;
  ULONG ret_len;

  if ((unsigned int)d->domain > 0 || (unsigned int)pos > 255 || (unsigned int)(pos+len) > 256)
    return 0;

  status = win32_sysdbg_pci_bus_data(FALSE, d->bus, d->dev, d->func, pos, buf, len, &ret_len);
  if (status < 0 || ret_len != (unsigned int)len)
    return 0;

  return 1;
}

static int
win32_sysdbg_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  NTSTATUS status;
  ULONG ret_len;

  if ((unsigned int)d->domain > 0 || (unsigned int)pos > 255 || (unsigned int)(pos+len) > 256)
    return 0;

  status = win32_sysdbg_pci_bus_data(TRUE, d->bus, d->dev, d->func, pos, buf, len, &ret_len);
  if (status < 0 || ret_len != (unsigned int)len)
    return 0;

  return 1;
}

struct pci_methods pm_win32_sysdbg = {
  .name = "win32-sysdbg",
  .help = "Win32 PCI config space access using NT SysDbg Bus Data interface",
  .detect = win32_sysdbg_detect,
  .init = win32_sysdbg_init,
  .cleanup = win32_sysdbg_cleanup,
  .scan = pci_generic_scan,
  .fill_info = pci_generic_fill_info,
  .read = win32_sysdbg_read,
  .write = win32_sysdbg_write,
};
