/*
 *      The PCI Library -- List PCI devices on Win32 via Configuration Manager
 *
 *      Copyright (c) 2021 Pali Rohár <pali@kernel.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+.
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <windows.h>
#include <cfgmgr32.h>

#include <ctype.h> /* for isxdigit() */
#include <stdio.h> /* for sprintf() */
#include <string.h> /* for strlen(), strchr(), strncmp() */
#include <wchar.h> /* for wcslen(), wcscpy() */

#include "internal.h"

/* Unfortunately MinGW32 toolchain does not provide these cfgmgr32 constants. */

#ifndef RegDisposition_OpenExisting
#define RegDisposition_OpenExisting 0x00000001
#endif

#ifndef CM_REGISTRY_SOFTWARE
#define CM_REGISTRY_SOFTWARE 0x00000001
#endif

#ifndef CM_DRP_HARDWAREID
#define CM_DRP_HARDWAREID 0x00000002
#endif
#ifndef CM_DRP_SERVICE
#define CM_DRP_SERVICE 0x00000005
#endif
#ifndef CM_DRP_BUSNUMBER
#define CM_DRP_BUSNUMBER 0x00000016
#endif
#ifndef CM_DRP_ADDRESS
#define CM_DRP_ADDRESS 0x0000001D
#endif

#ifndef CR_INVALID_CONFLICT_LIST
#define CR_INVALID_CONFLICT_LIST 0x00000039
#endif
#ifndef CR_INVALID_INDEX
#define CR_INVALID_INDEX 0x0000003A
#endif
#ifndef CR_INVALID_STRUCTURE_SIZE
#define CR_INVALID_STRUCTURE_SIZE 0x0000003B
#endif

#ifndef fIOD_10_BIT_DECODE
#define fIOD_10_BIT_DECODE 0x0004
#endif
#ifndef fIOD_12_BIT_DECODE
#define fIOD_12_BIT_DECODE 0x0008
#endif
#ifndef fIOD_16_BIT_DECODE
#define fIOD_16_BIT_DECODE 0x0010
#endif
#ifndef fIOD_POSITIVE_DECODE
#define fIOD_POSITIVE_DECODE 0x0020
#endif
#ifndef fIOD_PASSIVE_DECODE
#define fIOD_PASSIVE_DECODE 0x0040
#endif
#ifndef fIOD_WINDOW_DECODE
#define fIOD_WINDOW_DECODE 0x0080
#endif
#ifndef fIOD_PORT_BAR
#define fIOD_PORT_BAR 0x0100
#endif

#ifndef fMD_WINDOW_DECODE
#define fMD_WINDOW_DECODE 0x0040
#endif
#ifndef fMD_MEMORY_BAR
#define fMD_MEMORY_BAR 0x0080
#endif

/*
 * Unfortunately MinGW32 toolchain does not provide import library for these
 * cfgmgr32.dll functions. So resolve pointers to these functions at runtime.
 */

#ifdef CM_Get_DevNode_Registry_PropertyA
#undef CM_Get_DevNode_Registry_PropertyA
#endif
static CONFIGRET (WINAPI *MyCM_Get_DevNode_Registry_PropertyA)(DEVINST dnDevInst, ULONG ulProperty, PULONG pulRegDataType, PVOID Buffer, PULONG pulLength, ULONG ulFlags);
#define CM_Get_DevNode_Registry_PropertyA MyCM_Get_DevNode_Registry_PropertyA

#ifdef CM_Get_DevNode_Registry_PropertyW
#undef CM_Get_DevNode_Registry_PropertyW
#endif
static CONFIGRET (WINAPI *MyCM_Get_DevNode_Registry_PropertyW)(DEVINST dnDevInst, ULONG ulProperty, PULONG pulRegDataType, PVOID Buffer, PULONG pulLength, ULONG ulFlags);
#define CM_Get_DevNode_Registry_PropertyW MyCM_Get_DevNode_Registry_PropertyW

#ifndef CM_Open_DevNode_Key
#undef CM_Open_DevNode_Key
#endif
static CONFIGRET (WINAPI *MyCM_Open_DevNode_Key)(DEVINST dnDevNode, REGSAM samDesired, ULONG ulHardwareProfile, REGDISPOSITION Disposition, PHKEY phkDevice, ULONG ulFlags);
#define CM_Open_DevNode_Key MyCM_Open_DevNode_Key

static BOOL
resolve_cfgmgr32_functions(void)
{
  HMODULE cfgmgr32;

  if (CM_Get_DevNode_Registry_PropertyA && CM_Get_DevNode_Registry_PropertyW && CM_Open_DevNode_Key)
    return TRUE;

  cfgmgr32 = GetModuleHandleA("cfgmgr32.dll");
  if (!cfgmgr32)
    return FALSE;

  CM_Get_DevNode_Registry_PropertyA = (void *)GetProcAddress(cfgmgr32, "CM_Get_DevNode_Registry_PropertyA");
  CM_Get_DevNode_Registry_PropertyW = (void *)GetProcAddress(cfgmgr32, "CM_Get_DevNode_Registry_PropertyW");
  CM_Open_DevNode_Key = (void *)GetProcAddress(cfgmgr32, "CM_Open_DevNode_Key");
  if (!CM_Get_DevNode_Registry_PropertyA || !CM_Get_DevNode_Registry_PropertyW || !CM_Open_DevNode_Key)
    return FALSE;

  return TRUE;
}

/*
 * cfgmgr32.dll uses custom non-Win32 error numbers which are unsupported by
 * Win32 APIs like GetLastError() and FormatMessage() functions.
 *
 * Windows 7 introduced new cfgmgr32.dll function CM_MapCrToWin32Err() for
 * translating mapping CR_* errors to Win32 errors but most error codes are
 * not mapped. So this function is unusable.
 *
 * Error strings for CR_* errors are defined in cmapi.rc file which is
 * statically linked into some system libraries (e.g. filemgmt.dll,
 * acledit.dll, netui0.dll or netui2.dll) but due to static linking it is
 * not possible to access these error strings easily at runtime.
 *
 * So define own function for translating CR_* errors directly to strings.
 */
static const char *
cr_strerror(CONFIGRET cr_error_id)
{
  static char unknown_error[sizeof("Unknown CR error XXXXXXXXXX")];
  static const char *cr_errors[] = {
    "The operation completed successfully",
    "CR_DEFAULT",
    "Not enough memory is available to process this command",
    "A required pointer parameter is invalid",
    "The ulFlags parameter specified is invalid for this operation",
    "The device instance handle parameter is not valid",
    "The supplied resource descriptor parameter is invalid",
    "The supplied logical configuration parameter is invalid",
    "CR_INVALID_ARBITRATOR",
    "CR_INVALID_NODELIST",
    "CR_DEVNODE_HAS_REQS/CR_DEVINST_HAS_REQS",
    "The RESOURCEID parameter does not contain a valid RESOURCEID",
    "CR_DLVXD_NOT_FOUND",
    "The specified device instance handle does not correspond to a present device",
    "There are no more logical configurations available",
    "There are no more resource descriptions available",
    "This device instance already exists",
    "The supplied range list parameter is invalid",
    "CR_INVALID_RANGE",
    "A general internal error occurred",
    "CR_NO_SUCH_LOGICAL_DEV",
    "The device is disabled for this configuration",
    "CR_NOT_SYSTEM_VM",
    "A service or application refused to allow removal of this device",
    "CR_APM_VETOED",
    "CR_INVALID_LOAD_TYPE",
    "An output parameter was too small to hold all the data available",
    "CR_NO_ARBITRATOR",
    "CR_NO_REGISTRY_HANDLE",
    "A required entry in the registry is missing or an attempt to write to the registry failed",
    "The specified Device ID is not a valid Device ID",
    "One or more parameters were invalid",
    "CR_INVALID_API",
    "CR_DEVLOADER_NOT_READY",
    "CR_NEED_RESTART",
    "There are no more hardware profiles available",
    "CR_DEVICE_NOT_THERE",
    "The specified value does not exist in the registry",
    "CR_WRONG_TYPE",
    "The specified priority is invalid for this operation",
    "This device cannot be disabled",
    "CR_FREE_RESOURCES",
    "CR_QUERY_VETOED",
    "CR_CANT_SHARE_IRQ",
    "CR_NO_DEPENDENT",
    "CR_SAME_RESOURCES",
    "The specified key does not exist in the registry",
    "The specified machine name does not meet the UNC naming conventions",
    "A general remote communication error occurred",
    "The machine selected for remote communication is not available at this time",
    "The Plug and Play service or another required service is not available",
    "Access denied",
    "This routine is not implemented in this version of the operating system",
    "The specified property type is invalid for this operation",
    "Device interface is active",
    "No such device interface",
    "Invalid reference string",
    "Invalid conflict list",
    "Invalid index",
    "Invalid structure size"
  };
  if (cr_error_id <= 0 || cr_error_id >= sizeof(cr_errors)/sizeof(*cr_errors))
    {
      sprintf(unknown_error, "Unknown CR error %lu", cr_error_id);
      return unknown_error;
    }
  return cr_errors[cr_error_id];
}

static const char *
win32_strerror(DWORD win32_error_id)
{
  /*
   * Use static buffer which is large enough.
   * Hopefully no Win32 API error message string is longer than 4 kB.
   */
  static char buffer[4096];
  DWORD len;

  len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, win32_error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), NULL);

  /* FormatMessage() automatically appends ".\r\n" to the error message. */
  if (len && buffer[len-1] == '\n')
    buffer[--len] = '\0';
  if (len && buffer[len-1] == '\r')
    buffer[--len] = '\0';
  if (len && buffer[len-1] == '.')
    buffer[--len] = '\0';

  if (!len)
    sprintf(buffer, "Unknown Win32 error %lu", win32_error_id);

  return buffer;
}

static int
fmt_validate(const char *s, int len, const char *fmt)
{
  int i;

  for (i = 0; i < len; i++)
    if (!fmt[i] || (fmt[i] == '#' ? !isxdigit(s[i]) : fmt[i] != s[i]))
      return 0;

  return 1;
}

static int
seq_xdigit_validate(const char *s, int mult, int min)
{
  int i, len;

  len = strlen(s);
  if (len < min*mult || len % mult)
    return 0;

  for (i = 0; i < len; i++)
    if (!isxdigit(s[i]))
      return 0;

  return 1;
}

static BOOL
is_non_nt_system(void)
{
  OSVERSIONINFOA version;
  version.dwOSVersionInfoSize = sizeof(version);
  return GetVersionExA(&version) && version.dwPlatformId < VER_PLATFORM_WIN32_NT;
}

static BOOL
is_32bit_on_win8_64bit_system(void)
{
#ifdef _WIN64
  return FALSE;
#else
  BOOL (WINAPI *MyIsWow64Process)(HANDLE, PBOOL);
  OSVERSIONINFOA version;
  HMODULE kernel32;
  BOOL is_wow64;

  /* Check for Windows 8 (NT 6.2). */
  version.dwOSVersionInfoSize = sizeof(version);
  if (!GetVersionExA(&version) ||
      version.dwPlatformId != VER_PLATFORM_WIN32_NT ||
      version.dwMajorVersion < 6 ||
      (version.dwMajorVersion == 6 && version.dwMinorVersion < 2))
    return FALSE;

  /*
   * Check for 64-bit system via IsWow64Process() function exported
   * from 32-bit kernel32.dll library available on the 64-bit systems.
   * Resolve pointer to this function at runtime as this code path is
   * primary running on 32-bit systems where are not available 64-bit
   * functions.
   */

  kernel32 = GetModuleHandleA("kernel32.dll");
  if (!kernel32)
    return FALSE;

  MyIsWow64Process = (void *)GetProcAddress(kernel32, "IsWow64Process");
  if (!MyIsWow64Process)
    return FALSE;

  if (!MyIsWow64Process(GetCurrentProcess(), &is_wow64))
    return FALSE;

  return is_wow64;
#endif
}

static LPWSTR
get_device_service_name(struct pci_access *a, DEVINST devinst, DEVINSTID_A devinst_id, BOOL *supported)
{
  ULONG reg_type, reg_size, reg_len;
  LPWSTR service_name;
  CONFIGRET cr;

  /*
   * All data are stored as 7-bit ASCII strings in system but service name is
   * exception. It can contain UNICODE. Moreover it is passed to other Win32 API
   * functions and therefore it cannot be converted to 8-bit ANSI string without
   * data loss. So use wide function CM_Get_DevNode_Registry_PropertyW() in this
   * case and deal with all wchar_t problems...
   */

  reg_size = 0;
  cr = CM_Get_DevNode_Registry_PropertyW(devinst, CM_DRP_SERVICE, &reg_type, NULL, &reg_size, 0);
  if (cr == CR_CALL_NOT_IMPLEMENTED)
    {
      *supported = FALSE;
      return NULL;
    }
  else if (cr == CR_NO_SUCH_VALUE)
    {
      *supported = TRUE;
      return NULL;
    }
  else if (cr != CR_SUCCESS &&
           cr != CR_BUFFER_SMALL)
    {
      a->warning("Cannot retrieve service name for PCI device %s: %s.", devinst_id, cr_strerror(cr));
      *supported = TRUE;
      return NULL;
    }
  else if (reg_type != REG_SZ)
    {
      a->warning("Cannot retrieve service name for PCI device %s: Service name is stored as unknown type 0x%lx.", devinst_id, reg_type);
      *supported = TRUE;
      return NULL;
    }

retry:
  /*
   * Returned size is on older Windows versions without nul-term char.
   * So explicitly increase size and fill nul-term byte.
   */
  reg_size += sizeof(service_name[0]);
  service_name = pci_malloc(a, reg_size);
  reg_len = reg_size;
  cr = CM_Get_DevNode_Registry_PropertyW(devinst, CM_DRP_SERVICE, &reg_type, service_name, &reg_len, 0);
  service_name[reg_size/sizeof(service_name[0]) - 1] = 0;
  if (reg_len > reg_size)
    {
      pci_mfree(service_name);
      reg_size = reg_len;
      goto retry;
    }
  else if (cr != CR_SUCCESS)
    {
      a->warning("Cannot retrieve service name for PCI device %s: %s.", devinst_id, cr_strerror(cr));
      pci_mfree(service_name);
      *supported = TRUE;
      return NULL;
    }
  else if (reg_type != REG_SZ)
    {
      a->warning("Cannot retrieve service name for PCI device %s: Service name is stored as unknown type 0x%lx.", devinst_id, reg_type);
      pci_mfree(service_name);
      *supported = TRUE;
      return NULL;
    }


  return service_name;
}

static char*
get_driver_path_for_service(struct pci_access *a, LPCWSTR service_name, SC_HANDLE manager)
{
  UINT (WINAPI *get_system_root_path)(LPWSTR buffer, UINT size) = NULL;
  DWORD service_config_size, service_config_len;
  LPQUERY_SERVICE_CONFIGW service_config = NULL;
  LPWSTR service_image_path = NULL;
  SERVICE_STATUS service_status;
  SC_HANDLE service = NULL;
  char *driver_path = NULL;
  UINT systemroot_len;
  int driver_path_len;
  HMODULE kernel32;
  DWORD error;

  service = OpenServiceW(manager, service_name, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS);
  if (!service)
    {
      error = GetLastError();
      if (error != ERROR_SERVICE_DOES_NOT_EXIST)
        a->warning("Cannot open service %ls with query rights: %s.", service_name, win32_strerror(error));
      goto out;
    }

  if (!QueryServiceStatus(service, &service_status))
    {
      error = GetLastError();
      a->warning("Cannot query status of service %ls: %s.", service_name, win32_strerror(error));
      goto out;
    }

  if (service_status.dwCurrentState == SERVICE_STOPPED)
    goto out;

  if (service_status.dwServiceType != SERVICE_KERNEL_DRIVER)
    goto out;

  if (!QueryServiceConfigW(service, NULL, 0, &service_config_size))
    {
      error = GetLastError();
      if (error != ERROR_INSUFFICIENT_BUFFER)
      {
        a->warning("Cannot query config of service %ls: %s.", service_name, win32_strerror(error));
        goto out;
      }
    }

retry_service_config:
  service_config = pci_malloc(a, service_config_size);
  if (!QueryServiceConfigW(service, service_config, service_config_size, &service_config_len))
    {
      error = GetLastError();
      if (error == ERROR_INSUFFICIENT_BUFFER)
        {
          pci_mfree(service_config);
          service_config_size = service_config_len;
          goto retry_service_config;
        }
      a->warning("Cannot query config of service %ls: %s.", service_name, win32_strerror(error));
      goto out;
    }

  if (service_config->dwServiceType != SERVICE_KERNEL_DRIVER)
    goto out;

  /*
   * Despite QueryServiceConfig() is Win32 API, it returns lpBinaryPathName
   * (ImagePath registry) in NT format. Unfortunately there is no Win32
   * function for converting NT paths to Win32 paths. So do it manually and
   * convert this NT format to human-readable Win32 path format.
   */

  /*
   * Old Windows versions return path to NT SystemRoot namespace via
   * GetWindowsDirectoryW() function. New Windows versions via
   * GetSystemWindowsDirectoryW(). GetSystemWindowsDirectoryW() is not
   * provided in old Windows versions, so use GetProcAddress() for
   * compatibility with all Windows versions.
   */
  kernel32 = GetModuleHandleW(L"kernel32.dll");
  if (kernel32)
    get_system_root_path = (void *)GetProcAddress(kernel32, "GetSystemWindowsDirectoryW");
  if (!get_system_root_path)
    get_system_root_path = &GetWindowsDirectoryW;

  systemroot_len = get_system_root_path(NULL, 0);

  if (!service_config->lpBinaryPathName || !service_config->lpBinaryPathName[0])
    {
      /* No ImagePath is specified, NT kernel assumes implicit kernel driver path by service name. */
      service_image_path = pci_malloc(a, sizeof(WCHAR) * (systemroot_len + sizeof("\\System32\\drivers\\")-1 + wcslen(service_name) + sizeof(".sys")-1 + 1));
      systemroot_len = get_system_root_path(service_image_path, systemroot_len+1);
      if (systemroot_len && service_image_path[systemroot_len-1] != L'\\')
        service_image_path[systemroot_len++] = L'\\';
      wcscpy(service_image_path + systemroot_len, L"System32\\drivers\\");
      wcscpy(service_image_path + systemroot_len + sizeof("System32\\drivers\\")-1, service_name);
      wcscpy(service_image_path + systemroot_len + sizeof("System32\\drivers\\")-1 + wcslen(service_name), L".sys");
    }
  else if (wcsncmp(service_config->lpBinaryPathName, L"\\SystemRoot\\", sizeof("\\SystemRoot\\")-1) == 0)
    {
      /* ImagePath is in NT SystemRoot namespace, convert to Win32 path via GetSystemWindowsDirectoryW()/GetWindowsDirectoryW(). */
      service_image_path = pci_malloc(a, sizeof(WCHAR) * (systemroot_len + wcslen(service_config->lpBinaryPathName) - (sizeof("\\SystemRoot")-1)));
      systemroot_len = get_system_root_path(service_image_path, systemroot_len+1);
      if (systemroot_len && service_image_path[systemroot_len-1] != L'\\')
        service_image_path[systemroot_len++] = L'\\';
      wcscpy(service_image_path + systemroot_len, service_config->lpBinaryPathName + sizeof("\\SystemRoot\\")-1);
    }
  else if (wcsncmp(service_config->lpBinaryPathName, L"\\??\\UNC\\", sizeof("\\??\\UNC\\")-1) == 0 ||
           wcsncmp(service_config->lpBinaryPathName, L"\\??\\\\UNC\\", sizeof("\\??\\\\UNC\\")-1) == 0)
    {
      /* ImagePath is in NT UNC namespace, convert to Win32 UNC path via "\\\\" prefix. */
      service_image_path = pci_malloc(a, sizeof(WCHAR) * (sizeof("\\\\") + wcslen(service_config->lpBinaryPathName) - (sizeof("\\??\\UNC\\")-1)));
      /* Namespace separator may be single or double backslash. */
      driver_path_len = sizeof("\\??\\")-1;
      if (service_config->lpBinaryPathName[driver_path_len] == L'\\')
        driver_path_len++;
      driver_path_len += sizeof("UNC\\")-1;
      wcscpy(service_image_path, L"\\\\");
      wcscpy(service_image_path + sizeof("\\\\")-1, service_config->lpBinaryPathName + driver_path_len);
    }
  else if (wcsncmp(service_config->lpBinaryPathName, L"\\??\\", sizeof("\\??\\")-1) == 0)
    {
      /* ImagePath is in NT Global?? namespace, root of the Win32 file namespace, so just remove "\\??\\" prefix to get Win32 path. */
      service_image_path = pci_malloc(a, sizeof(WCHAR) * (wcslen(service_config->lpBinaryPathName) - (sizeof("\\??\\")-1)));
      /* Namespace separator may be single or double backslash. */
      driver_path_len = sizeof("\\??\\")-1;
      if (service_config->lpBinaryPathName[driver_path_len] == L'\\')
        driver_path_len++;
      wcscpy(service_image_path, service_config->lpBinaryPathName + driver_path_len);
    }
  else if (service_config->lpBinaryPathName[0] != L'\\')
    {
      /* ImagePath is relative to the NT SystemRoot namespace, convert to Win32 path via GetSystemWindowsDirectoryW()/GetWindowsDirectoryW(). */
      service_image_path = pci_malloc(a, sizeof(WCHAR) * (systemroot_len + sizeof("\\") + wcslen(service_config->lpBinaryPathName)));
      systemroot_len = get_system_root_path(service_image_path, systemroot_len+1);
      if (systemroot_len && service_image_path[systemroot_len-1] != L'\\')
        service_image_path[systemroot_len++] = L'\\';
      wcscpy(service_image_path + systemroot_len, service_config->lpBinaryPathName);
    }
  else
    {
      /* ImagePath is in some unhandled NT namespace, copy it as is. It cannot be used in Win32 API but libpci user can be still interested in it. */
      service_image_path = pci_malloc(a, sizeof(WCHAR) * wcslen(service_config->lpBinaryPathName));
      wcscpy(service_image_path, service_config->lpBinaryPathName);
    }

  /* Calculate len of buffer needed for conversion from LPWSTR to char*. */
  driver_path_len = WideCharToMultiByte(CP_ACP, 0, service_image_path, -1, NULL, 0, NULL, NULL);
  if (driver_path_len <= 0)
    {
      error = GetLastError();
      a->warning("Cannot convert kernel driver path from wide string to 8-bit string: %s.", win32_strerror(error));
      goto out;
    }

  driver_path = pci_malloc(a, driver_path_len);
  driver_path_len = WideCharToMultiByte(CP_ACP, 0, service_image_path, -1, driver_path, driver_path_len, NULL, NULL);
  if (driver_path_len <= 0)
    {
      error = GetLastError();
      a->warning("Cannot convert kernel driver path from wide string to 8-bit string: %s.", win32_strerror(error));
      pci_mfree(driver_path);
      driver_path = NULL;
      goto out;
    }

out:
  if (service_image_path)
    pci_mfree(service_image_path);
  if (service_config)
    pci_mfree(service_config);
  if (service)
    CloseServiceHandle(service);
  return driver_path;
}

static HKEY
get_device_driver_devreg(struct pci_access *a, DEVINST devinst, DEVINSTID_A devinst_id)
{
  CONFIGRET cr;
  HKEY key;

  cr = CM_Open_DevNode_Key(devinst, KEY_READ, 0, RegDisposition_OpenExisting, &key, CM_REGISTRY_SOFTWARE);
  if (cr != CR_SUCCESS)
    {
      if (cr != CR_NO_SUCH_VALUE)
        a->warning("Cannot retrieve driver key for device %s: %s.", devinst_id, cr_strerror(cr));
      return NULL;
    }

  return key;
}

static char*
read_reg_key_string_value(struct pci_access *a, HKEY key, const char *name, DWORD *unkn_reg_type)
{
  DWORD reg_type, reg_size, reg_len;
  char *value;
  LONG error;

  reg_size = 0;
  error = RegQueryValueExA(key, name, NULL, &reg_type, NULL, &reg_size);
  if (error != ERROR_SUCCESS &&
      error != ERROR_MORE_DATA)
    {
      SetLastError(error);
      return NULL;
    }
  else if (reg_type != REG_SZ)
    {
      SetLastError(0);
      *unkn_reg_type = reg_type;
      return NULL;
    }

retry:
  value = pci_malloc(a, reg_size + 1);
  reg_len = reg_size;
  error = RegQueryValueExA(key, name, NULL, &reg_type, (PBYTE)value, &reg_len);
  if (error != ERROR_SUCCESS)
    {
      pci_mfree(value);
      if (error == ERROR_MORE_DATA)
        {
          reg_size = reg_len;
          goto retry;
        }
      SetLastError(error);
      return NULL;
    }
  else if (reg_type != REG_SZ)
    {
      pci_mfree(value);
      SetLastError(0);
      *unkn_reg_type = reg_type;
      return NULL;
    }
  value[reg_len] = '\0';

  return value;
}

static int
driver_cmp(const char *driver, const char *match)
{
  int len = strlen(driver);
  if (driver[0] == '*')
    driver++;
  if (len >= 4 && strcasecmp(driver + len - 4, ".vxd") == 0)
    len -= 4;
  return strncasecmp(driver, match, len);
}

static char*
get_driver_path_for_regkey(struct pci_access *a, DEVINSTID_A devinst_id, HKEY key)
{
  char *driver_list, *driver, *driver_next;
  char *subdriver, *subname;
  char *driver_ptr;
  char *driver_path;
  DWORD unkn_reg_type;
  UINT systemdir_len;
  HKEY subkey;
  LONG error;
  BOOL vmm32;
  BOOL noext;
  int len;

  driver_list = read_reg_key_string_value(a, key, "DevLoader", &unkn_reg_type);
  if (!driver_list)
    {
      error = GetLastError();
      if (error == 0)
        a->warning("Cannot read driver DevLoader key for PCI device %s: DevLoader key is stored as unknown type 0x%lx.", devinst_id, unkn_reg_type);
      else if (error != ERROR_FILE_NOT_FOUND)
        a->warning("Cannot read driver DevLoader key for PCI device %s: %s.", devinst_id, win32_strerror(error));
      return NULL;
    }

  subdriver = NULL;
  driver = driver_list;
  while (*driver)
    {
      driver_next = strchr(driver, ',');
      if (driver_next)
          *(driver_next++) = '\0';

      if (driver_cmp(driver, "ios") == 0 ||
          driver_cmp(driver, "vcomm") == 0)
        subname = "PortDriver";
      else if (driver_cmp(driver, "ntkern") == 0)
        subname = "NTMPDriver";
      else if (driver_cmp(driver, "ndis") == 0)
        subname = "DeviceVxDs";
      else if (driver_cmp(driver, "vdd") == 0)
        subname = "minivdd";
      else
        subname = NULL;

      subkey = key;
      if (subname && strcmp(subname, "minivdd") == 0)
        {
          error = RegOpenKeyA(key, "Default", &subkey);
          if (error != ERROR_SUCCESS)
            {
              a->warning("Cannot open driver subkey Default for PCI device %s: %s.", devinst_id, win32_strerror(error));
              subkey = NULL;
            }
        }

      if (!subname)
        break;

      if (subkey)
        {
retry_subname:
          subdriver = read_reg_key_string_value(a, subkey, subname, &unkn_reg_type);
          if (!subdriver)
            {
              error = GetLastError();
              if (error == 0)
                a->warning("Cannot read driver %s key for PCI device %s: DevLoader key is stored as unknown type 0x%lx.", subname, devinst_id, unkn_reg_type);
              else if (error != ERROR_FILE_NOT_FOUND)
                a->warning("Cannot read driver %s key for PCI device %s: %s.", subname, devinst_id, win32_strerror(error));
              else if (strcmp(subname, "minivdd") == 0)
                {
                  subname = "drv";
                  goto retry_subname;
                }
              else if (strcmp(subname, "drv") == 0)
                {
                  subname = "vdd";
                  goto retry_subname;
                }
            }

          if (subkey != key)
            RegCloseKey(subkey);

          if (subdriver)
            {
              char *endptr = strchr(subdriver, ',');
              if (endptr)
                *endptr = '\0';
              break;
            }
        }

      driver = driver_next;
    }

  if (subdriver && subdriver[0])
    driver_ptr = subdriver;
  else if (driver[0])
    driver_ptr = driver;
  else
    driver_ptr = NULL;

  if (driver_ptr && driver_ptr[0] == '*')
    {
      vmm32 = TRUE;
      driver_ptr++;
    }
  else
    vmm32 = FALSE;

  if (!driver_ptr[0])
    driver_ptr = NULL;

  len = driver_ptr ? strlen(driver_ptr) : 0;
  noext = driver_ptr && (len < 4 || driver_ptr[len-4] != '.');

  if (!driver_ptr)
    driver_path = NULL;
  else
    {
      if (tolower(driver_ptr[0]) >= 'a' && tolower(driver_ptr[0]) <= 'z' && driver_ptr[1] == ':')
        {
          /* Driver is already with absolute path. */
          driver_path = pci_strdup(a, driver_ptr);
        }
      else if (driver_cmp(driver, "ntkern") == 0 && subdriver)
        {
          /* Driver is relative to system32\drivers\ directory which is relative to windows directory. */
          systemdir_len = GetWindowsDirectoryA(NULL, 0);
          driver_path = pci_malloc(a, systemdir_len + 1 + sizeof("system32\\drivers\\")-1 + strlen(driver_ptr) + 4 + 1);
          systemdir_len = GetWindowsDirectoryA(driver_path, systemdir_len + 1);
          if (systemdir_len && driver_path[systemdir_len - 1] != '\\')
            driver_path[systemdir_len++] = '\\';
          sprintf(driver_path + systemdir_len, "system32\\drivers\\%s%s", driver_ptr, noext ? ".sys" : "");
        }
      else if (vmm32)
        {
          /* Driver is packed in vmm32.vxd which is stored in system directory. */
          systemdir_len = GetSystemDirectoryA(NULL, 0);
          driver_path = pci_malloc(a, systemdir_len + 1 + sizeof("vmm32.vxd ()")-1 + strlen(driver_ptr) + 4 + 1);
          systemdir_len = GetSystemDirectoryA(driver_path, systemdir_len + 1);
          if (systemdir_len && driver_path[systemdir_len - 1] != '\\')
            driver_path[systemdir_len++] = '\\';
          sprintf(driver_path + systemdir_len, "vmm32.vxd (%s%s)", driver_ptr, noext ? ".vxd" : "");
        }
      else
        {
          /* Otherwise driver is relative to system directory. */
          systemdir_len = GetSystemDirectoryA(NULL, 0);
          driver_path = pci_malloc(a, systemdir_len + 1 + strlen(driver_ptr) + 4 + 1);
          systemdir_len = GetSystemDirectoryA(driver_path, systemdir_len + 1);
          if (systemdir_len && driver_path[systemdir_len - 1] != '\\')
            driver_path[systemdir_len++] = '\\';
          sprintf(driver_path + systemdir_len, "%s%s", driver_ptr, noext ? ".vxd" : "");
        }
    }

  if (subdriver)
    pci_mfree(subdriver);
  pci_mfree(driver_list);
  return driver_path;
}

static char *
get_device_driver_path(struct pci_dev *d, SC_HANDLE manager, BOOL manager_supported)
{
  struct pci_access *a = d->access;
  BOOL service_supported = TRUE;
  DEVINSTID_A devinst_id = NULL;
  LPWSTR service_name = NULL;
  ULONG devinst_id_len = 0;
  char *driver_path = NULL;
  DEVINST devinst = (DEVINST)d->aux;
  ULONG problem = 0;
  ULONG status = 0;
  HKEY key = NULL;

  if (CM_Get_DevNode_Status(&status, &problem, devinst, 0) != CR_SUCCESS || !(status & DN_DRIVER_LOADED))
    return NULL;

  if (CM_Get_Device_ID_Size(&devinst_id_len, devinst, 0) == CR_SUCCESS)
    {
      devinst_id = pci_malloc(a, devinst_id_len + 1);
      if (CM_Get_Device_IDA(devinst, devinst_id, devinst_id_len + 1, 0) != CR_SUCCESS)
        {
          pci_mfree(devinst_id);
          devinst_id = pci_strdup(a, "UNKNOWN");
        }
    }
  else
    devinst_id = pci_strdup(a, "UNKNOWN");

  service_name = get_device_service_name(d->access, devinst, devinst_id, &service_supported);
  if ((!service_name || !manager) && service_supported && manager_supported)
    goto out;
  else if (service_name && manager)
    {
      driver_path = get_driver_path_for_service(d->access, service_name, manager);
      goto out;
    }

  key = get_device_driver_devreg(d->access, devinst, devinst_id);
  if (key)
    {
      driver_path = get_driver_path_for_regkey(d->access, devinst_id, key);
      goto out;
    }

out:
  if (key)
    RegCloseKey(key);
  if (service_name)
    pci_mfree(service_name);
  pci_mfree(devinst_id);
  return driver_path;
}

static void
fill_drivers(struct pci_access *a)
{
  BOOL manager_supported;
  SC_HANDLE manager;
  struct pci_dev *d;
  char *driver;
  DWORD error;

  /* ERROR_CALL_NOT_IMPLEMENTED is returned on systems without Service Manager support. */
  manager_supported = TRUE;
  manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
  if (!manager)
    {
      error = GetLastError();
      if (error != ERROR_CALL_NOT_IMPLEMENTED)
        a->warning("Cannot open Service Manager with connect right: %s.", win32_strerror(error));
      else
        manager_supported = FALSE;
    }

  for (d = a->devices; d; d = d->next)
    {
      driver = get_device_driver_path(d, manager, manager_supported);
      if (driver)
        {
          pci_set_property(d, PCI_FILL_DRIVER, driver);
          pci_mfree(driver);
        }
      d->known_fields |= PCI_FILL_DRIVER;
    }

  if (manager)
    CloseServiceHandle(manager);
}

static void
fill_resources(struct pci_dev *d, DEVINST devinst, DEVINSTID_A devinst_id)
{
  struct pci_access *a = d->access;

  CONFIGRET cr;

  LOG_CONF config;
  ULONG problem;
  ULONG status;

  RES_DES prev_res_des;
  RES_DES res_des;
  RESOURCEID res_id;
  DWORD bar_res_count;

  BOOL is_bar_res;
  BOOL non_nt_system;

  int last_irq = -1;
  int last_shared_irq = -1;

  cr = CM_Get_DevNode_Status(&status, &problem, devinst, 0);
  if (cr != CR_SUCCESS)
    {
      a->warning("Cannot retrieve status of PCI device %s: %s.", devinst_id, cr_strerror(cr));
      return;
    }

  cr = CR_NO_MORE_LOG_CONF;

  /*
   * If the device is running then retrieve allocated configuration by PnP
   * manager which is currently in use by a device.
   */
  if (!(status & DN_HAS_PROBLEM))
    cr = CM_Get_First_Log_Conf(&config, devinst, ALLOC_LOG_CONF);

  /*
   * If the device is not running or it does not have allocated configuration by
   * PnP manager then retrieve forced configuration which prevents PnP manager
   * from assigning resources.
   */
  if (cr == CR_NO_MORE_LOG_CONF)
    cr = CM_Get_First_Log_Conf(&config, devinst, FORCED_LOG_CONF);

  /*
   * If the device does not have neither allocated configuration by PnP manager
   * nor forced configuration and it is not disabled in the BIOS then retrieve
   * boot configuration supplied by the BIOS.
   */
  if (cr == CR_NO_MORE_LOG_CONF &&
      (!(status & DN_HAS_PROBLEM) || problem != CM_PROB_HARDWARE_DISABLED))
    cr = CM_Get_First_Log_Conf(&config, devinst, BOOT_LOG_CONF);

  if (cr != CR_SUCCESS)
    {
      /*
       * Note: Starting with Windows 8, CM_Get_First_Log_Conf returns
       * CR_CALL_NOT_IMPLEMENTED when used in a Wow64 scenario.
       * To request information about the hardware resources on a local machine
       * it is necessary implement an architecture-native version of the
       * application using the hardware resource APIs. For example: An AMD64
       * application for AMD64 systems.
       */
      if (cr == CR_CALL_NOT_IMPLEMENTED && is_32bit_on_win8_64bit_system())
        {
          static BOOL warn_once = FALSE;
          if (!warn_once)
            {
              warn_once = TRUE;
              a->warning("Cannot retrieve resources of PCI devices from 32-bit application on 64-bit system.");
            }
        }
      else if (cr != CR_NO_MORE_LOG_CONF)
        a->warning("Cannot retrieve resources of PCI device %s: %s.", devinst_id, cr_strerror(cr));
      return;
    }

  bar_res_count = 0;
  non_nt_system = is_non_nt_system();

  is_bar_res = TRUE;
  if (non_nt_system)
    {
      BOOL has_child;
      DEVINST child;
      ULONG child_name_len;
      PSTR child_name;
      BOOL is_bridge;

      if (CM_Get_Child(&child, devinst, 0) != CR_SUCCESS)
        has_child = FALSE;
      else if (CM_Get_Device_ID_Size(&child_name_len, child, 0) != CR_SUCCESS)
        has_child = FALSE;
      else
        {
          child_name_len++;
          child_name = pci_malloc(a, child_name_len);
          if (CM_Get_Device_IDA(child, child_name, child_name_len, 0) != CR_SUCCESS)
            has_child = FALSE;
          else if (strncmp(child_name, "PCI\\", 4) != 0)
            has_child = FALSE;
          else
            has_child = TRUE;
          pci_mfree(child_name);
        }

      if (has_child || d->device_class == PCI_CLASS_BRIDGE_PCI || d->device_class == PCI_CLASS_BRIDGE_CARDBUS)
        is_bridge = TRUE;
      else
        is_bridge = FALSE;

      if (is_bridge)
        is_bar_res = FALSE;
    }

  prev_res_des = (RES_DES)config;
  while ((cr = CM_Get_Next_Res_Des(&res_des, prev_res_des, ResType_All, &res_id, 0)) == CR_SUCCESS)
    {
      pciaddr_t start, end, size, flags;
      ULONG res_des_data_size;
      PBYTE res_des_data;

      if (prev_res_des != config)
        CM_Free_Res_Des_Handle(prev_res_des);

      prev_res_des = res_des;

      cr = CM_Get_Res_Des_Data_Size(&res_des_data_size, res_des, 0);
      if (cr != CR_SUCCESS)
        {
          a->warning("Cannot retrieve resource data of PCI device %s: %s.", devinst_id, cr_strerror(cr));
          continue;
        }

      if (!res_des_data_size)
        {
          a->warning("Cannot retrieve resource data of PCI device %s: %s.", devinst_id, "Empty data");
          continue;
        }

      res_des_data = pci_malloc(a, res_des_data_size);
      cr = CM_Get_Res_Des_Data(res_des, res_des_data, res_des_data_size, 0);
      if (cr != CR_SUCCESS)
        {
          a->warning("Cannot retrieve resource data of PCI device %s: %s.", devinst_id, cr_strerror(cr));
          pci_mfree(res_des_data);
          continue;
        }

      /*
       * There can be more resources with the same id. In this case we are
       * interested in the last one in the list as at the beginning of the list
       * can be some virtual resources (which are not set in PCI config space).
       */

      if (res_id == ResType_IO)
        {
          PIO_RESOURCE io_data = (PIO_RESOURCE)res_des_data;

          start = io_data->IO_Header.IOD_Alloc_Base;
          end = io_data->IO_Header.IOD_Alloc_End;
          size = (end > start) ? (end - start + 1) : 0;
          flags = PCI_IORESOURCE_IO;

          /*
           * If neither 10-bit, 12-bit, nor 16-bit support is presented then
           * expects that this is 32-bit I/O resource. If resource does not fit
           * into 16-bit space then it must be 32-bit. If PCI I/O resource is
           * not 32-bit then it is 16-bit.
           */
          if (end <= 0xffff && (io_data->IO_Header.IOD_DesFlags & (fIOD_10_BIT_DECODE|fIOD_12_BIT_DECODE|fIOD_16_BIT_DECODE)))
            flags |= PCI_IORESOURCE_IO_16BIT_ADDR;

          /*
           * 16/32-bit non-NT systems do not support these two flags.
           * Most NT-based Windows versions support only the fIOD_WINDOW_DECODE
           * flag and put all BAR resources before window resources in this
           * resource list. So use this fIOD_WINDOW_DECODE flag as separator
           * between IO/MEM windows and IO/MEM BARs of PCI Bridges.
           */
          if (io_data->IO_Header.IOD_DesFlags & fIOD_WINDOW_DECODE)
            is_bar_res = FALSE;
          else if (io_data->IO_Header.IOD_DesFlags & fIOD_PORT_BAR)
            is_bar_res = TRUE;

          if (is_bar_res && bar_res_count < 6)
            {
              d->flags[bar_res_count] = flags;
              d->base_addr[bar_res_count] = start;
              d->size[bar_res_count] = size;
              bar_res_count++;
            }
          else if (!is_bar_res)
            {
              d->bridge_flags[0] = flags;
              d->bridge_base_addr[0] = start;
              d->bridge_size[0] = size;
              d->known_fields |= PCI_FILL_BRIDGE_BASES;
            }
        }
      else if (res_id == ResType_Mem)
        {
          PMEM_RESOURCE mem_data = (PMEM_RESOURCE)res_des_data;

          start = mem_data->MEM_Header.MD_Alloc_Base;
          end = mem_data->MEM_Header.MD_Alloc_End;
          size = (end > start) ? (end - start + 1) : 0;
          flags = PCI_IORESOURCE_MEM;

          /*
           * If fMD_PrefetchAllowed flag is set then this is
           * PCI Prefetchable Memory resource.
           */
          if ((mem_data->MEM_Header.MD_Flags & mMD_Prefetchable) == fMD_PrefetchAllowed)
            flags |= PCI_IORESOURCE_PREFETCH;

          /* If resource does not fit into 32-bit space then it must be 64-bit. */
          if (is_bar_res && end > 0xffffffff)
            flags |= PCI_IORESOURCE_MEM_64;

          /*
           * These two flags (fMD_WINDOW_DECODE and fMD_MEMORY_BAR) are
           * unsupported on most Windows versions, so distinguish between
           * window and BAR based on previous resource type.
           */
          if (mem_data->MEM_Header.MD_Flags & fMD_WINDOW_DECODE)
            is_bar_res = FALSE;
          else if (mem_data->MEM_Header.MD_Flags & fMD_MEMORY_BAR)
            is_bar_res = TRUE;

          /* 64-bit BAR resource must be at even position. */
          if (is_bar_res && (flags & PCI_IORESOURCE_MEM_64) && bar_res_count % 2)
            bar_res_count++;

          if (is_bar_res && bar_res_count < 6)
            {
              d->flags[bar_res_count] = flags;
              d->base_addr[bar_res_count] = start;
              d->size[bar_res_count] = size;
              bar_res_count++;
              /* 64-bit BAR resource occupies two slots. */
              if (flags & PCI_IORESOURCE_MEM_64)
                bar_res_count++;
            }
          else if (!is_bar_res && !(flags & PCI_IORESOURCE_PREFETCH))
            {
              d->bridge_flags[1] = flags;
              d->bridge_base_addr[1] = start;
              d->bridge_size[1] = size;
              d->known_fields |= PCI_FILL_BRIDGE_BASES;
            }
          else if (!is_bar_res && (flags & PCI_IORESOURCE_PREFETCH))
            {
              d->bridge_flags[2] = flags;
              d->bridge_base_addr[2] = start;
              d->bridge_size[2] = size;
              d->known_fields |= PCI_FILL_BRIDGE_BASES;
            }
        }
      else if (res_id == ResType_IRQ)
        {
          PIRQ_RESOURCE irq_data = (PIRQ_RESOURCE)res_des_data;

          /*
           * libpci's d->irq should be set to the non-MSI PCI IRQ and therefore
           * it should be level IRQ which may be shared with other PCI devices
           * and drivers in the system. As always we want to retrieve the last
           * IRQ number from the resource list.
           *
           * On 16/32-bit non-NT systems is fIRQD_Level set to 2 but on NT
           * systems to 0. Moreover it looks like that different PCI drivers
           * on both NT and non-NT systems set bits 0 and 1 to wrong values
           * and so reported value in this list may be incorrect.
           *
           * Therefore take the last level-shared IRQ number from the resource
           * list and if there is none of this type then take the last IRQ
           * number from the list.
           */
          last_irq = irq_data->IRQ_Header.IRQD_Alloc_Num;
          if ((irq_data->IRQ_Header.IRQD_Flags & (mIRQD_Share|mIRQD_Edge_Level)) == (fIRQD_Share|fIRQD_Level))
            last_shared_irq = irq_data->IRQ_Header.IRQD_Alloc_Num;

          /*
           * IRQ resource on 16/32-bit non-NT systems is separator between
           * IO/MEM windows and IO/MEM BARs of PCI Bridges. After the IRQ
           * resource are IO/MEM BAR resources.
           */
          if (!is_bar_res && non_nt_system)
            is_bar_res = TRUE;
        }

      pci_mfree(res_des_data);
    }
  if (cr != CR_NO_MORE_RES_DES)
    a->warning("Cannot retrieve resources of PCI device %s: %s.", devinst_id, cr_strerror(cr));

  if (prev_res_des != config)
    CM_Free_Res_Des_Handle(prev_res_des);

  CM_Free_Log_Conf_Handle(config);

  /* Set the last IRQ from the resource list to pci_dev. */
  if (last_shared_irq >= 0)
    d->irq = last_shared_irq;
  else if (last_irq >= 0)
    d->irq = last_irq;
  if (last_shared_irq >= 0 || last_irq >= 0)
    d->known_fields |= PCI_FILL_IRQ;

  if (bar_res_count > 0)
    d->known_fields |= PCI_FILL_BASES | PCI_FILL_SIZES | PCI_FILL_IO_FLAGS;
}

static BOOL
get_device_location(struct pci_access *a, DEVINST devinst, DEVINSTID_A devinst_id, unsigned int *domain, unsigned int *bus, unsigned int *dev, unsigned int *func)
{
  ULONG reg_type, reg_len;
  CONFIGRET cr;
  BOOL have_bus, have_devfunc;
  DWORD drp_bus_num, drp_address;

  *domain = 0;
  have_bus = FALSE;
  have_devfunc = FALSE;

  /*
   * DRP_BUSNUMBER consists of PCI domain number in high 24 bits
   * and PCI bus number in low 8 bits.
   */
  reg_len = sizeof(drp_bus_num);
  cr = CM_Get_DevNode_Registry_PropertyA(devinst, CM_DRP_BUSNUMBER, &reg_type, &drp_bus_num, &reg_len, 0);
  if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(drp_bus_num))
    {
      *domain = drp_bus_num >> 8;
      *bus = drp_bus_num & 0xff;
      have_bus = TRUE;
    }

  /*
   * DRP_ADDRESS consists of PCI device number in high 16 bits
   * and PCI function number in low 16 bits.
   */
  reg_len = sizeof(drp_address);
  cr = CM_Get_DevNode_Registry_PropertyA(devinst, CM_DRP_ADDRESS, &reg_type, &drp_address, &reg_len, 0);
  if (cr == CR_SUCCESS && reg_type == REG_DWORD && reg_len == sizeof(drp_address))
    {
      *dev = drp_address >> 16;
      *func = drp_address & 0xffff;
      have_devfunc = TRUE;
    }

  /*
   * Device Instance Id for PCI devices is of format:
   *  "<enumerator>\\<device_id>\\<instance_id>"
   * where:
   *  "<enumerator>" is "PCI"
   *  "<device_id>" is "VEN_####&DEV_####&SUBSYS_########&REV_##"
   * and "<instance_id>" for PCI devices is at least in one of following format:
   *  "BUS_##&DEV_##&FUNC_##"
   *  "##.." (sequence of devfn hex bytes, where bytes represents tree path to the root)
   *  "#..&#..&#..&#.." (four hex numbers separated by "&"; meaning is unknown yet)
   *
   * First two formats are used only on systems without support for multiple
   * domains. The second format uses intel-conf encoding of device and function
   * number: Low 3 bits is function number and high 5 bits is device number.
   * Bus number is not really encoded in second format!
   *
   * The third format is used on systems with support for multiple domains but
   * format is variable length and currently its meaning is unknown. Apparently
   * it looks like that DRP_BUSNUMBER and DRP_ADDRESS registry properties are
   * supported on these systems.
   *
   * If DRP_BUSNUMBER or DRP_ADDRESS failed then try to parse PCI bus, device
   * and function numbers from Instance Id part.
   */
  if (!have_bus || !have_devfunc)
    {
      const char *device_id0 = strchr(devinst_id, '\\');
      const char *instance_id0 = device_id0 ? strchr(device_id0 + 1, '\\') : NULL;
      const char *instance_id = instance_id0 ? instance_id0 + 1 : NULL;
      unsigned int devfn;

      if (instance_id)
        {
          if (fmt_validate(instance_id, strlen(instance_id), "BUS_##&DEV_##&FUNC_##") &&
              sscanf(instance_id, "BUS_%x&DEV_%x&FUNC_%x", bus, dev, func) == 3)
            {
              have_bus = TRUE;
              have_devfunc = TRUE;
            }
          else if (seq_xdigit_validate(instance_id, 2, 2) &&
                   sscanf(instance_id, "%2x", &devfn) == 1)
            {
              *dev = devfn >> 3;
              *func = devfn & 0x7;
              have_devfunc = TRUE;
            }
        }
    }

  /*
   * Virtual IRQ holder devices do not have assigned any bus/dev/func number and
   * have "IRQHOLDER" in their Device Id part. So skip them.
   */
  if (!have_bus && !have_devfunc && strncmp(devinst_id, "PCI\\IRQHOLDER\\", 14) == 0)
    return FALSE;

  /*
   * When some numbers cannot be retrieved via cfgmgr32 then set them to zeros
   * to have structure initialized. It makes sense to report via libpci also
   * such "incomplete" device as cfgmgr32 can provide additional information
   * like device/vendor ids or assigned resources.
   */
  if (!have_bus && !have_devfunc)
    {
      *bus = *dev = *func = 0;
      a->warning("Cannot retrieve bus, device and function numbers for PCI device %s: %s.", devinst_id, cr_strerror(cr));
    }
  else if (!have_bus)
    {
      *bus = 0;
      a->warning("Cannot retrieve bus number for PCI device %s: %s.", devinst_id, cr_strerror(cr));
    }
  else if (!have_devfunc)
    {
      *dev = *func = 0;
      a->warning("Cannot retrieve device and function numbers for PCI device %s: %s.", devinst_id, cr_strerror(cr));
    }

  return TRUE;
}

static void
fill_data_from_string(struct pci_dev *d, const char *str)
{
  BOOL have_device_id;
  BOOL have_vendor_id;
  BOOL have_prog_if;
  BOOL have_rev_id;
  const char *endptr, *endptr2;
  unsigned int hex;
  int len;

  have_device_id = have_vendor_id = (d->known_fields & PCI_FILL_IDENT);
  have_prog_if = have_rev_id = (d->known_fields & PCI_FILL_CLASS_EXT);

  while (1)
    {
      endptr = strchr(str, '&');
      endptr2 = strchr(str, '\\');
      if (endptr2 && (!endptr || endptr > endptr2))
        endptr = endptr2;
      len = endptr ? endptr-str : (int)strlen(str);

      if (!have_vendor_id &&
          fmt_validate(str, len, "VEN_####") &&
          sscanf(str, "VEN_%x", &hex) == 1)
        {
          d->vendor_id = hex;
          have_vendor_id = TRUE;
        }
      else if (!have_device_id &&
               fmt_validate(str, len, "DEV_####") &&
               sscanf(str, "DEV_%x", &hex) == 1)
        {
          d->device_id = hex;
          have_device_id = TRUE;
        }
      else if (!(d->known_fields & PCI_FILL_SUBSYS) &&
               fmt_validate(str, len, "SUBSYS_########") &&
               sscanf(str, "SUBSYS_%x", &hex) == 1)
        {
          d->subsys_vendor_id = hex & 0xffff;
          d->subsys_id = hex >> 16;
          d->known_fields |= PCI_FILL_SUBSYS;
        }
      else if (!have_rev_id &&
               fmt_validate(str, len, "REV_##") &&
               sscanf(str, "REV_%x", &hex) == 1)
        {
          d->rev_id = hex;
          have_rev_id = TRUE;
        }
      else if (!((d->known_fields & PCI_FILL_CLASS) && have_prog_if) &&
               (fmt_validate(str, len, "CC_####") || fmt_validate(str, len, "CC_######")) &&
               sscanf(str, "CC_%x", &hex) == 1)
        {
          if (len == 9)
            {
              if (!have_prog_if)
                {
                  d->prog_if = hex & 0xff;
                  have_prog_if = TRUE;
                }
              hex >>= 8;
            }
          if (!(d->known_fields & PCI_FILL_CLASS))
            {
              d->device_class = hex;
              d->known_fields |= PCI_FILL_CLASS;
            }
        }

      if (!endptr || endptr == endptr2)
        break;

      str = endptr + 1;
    }

  if ((have_device_id || d->device_id) && (have_vendor_id || d->vendor_id))
    d->known_fields |= PCI_FILL_IDENT;

  if ((have_prog_if || d->prog_if) && (have_rev_id || d->rev_id))
    d->known_fields |= PCI_FILL_CLASS_EXT;
}

static void
fill_data_from_devinst_id(struct pci_dev *d, DEVINSTID_A devinst_id)
{
  const char *device_id;

  device_id = strchr(devinst_id, '\\');
  if (!device_id)
    return;
  device_id++;

  /*
   * Device Id part of Device Instance Id is in format:
   *  "VEN_####&DEV_####&SUBSYS_########&REV_##"
   */
  fill_data_from_string(d, device_id);
}

static void
fill_data_from_hardware_ids(struct pci_dev *d, DEVINST devinst, DEVINSTID_A devinst_id)
{
  ULONG reg_type, reg_size, reg_len;
  struct pci_access *a = d->access;
  char *hardware_ids = NULL;
  const char *str;
  CONFIGRET cr;

  reg_size = 0;
  cr = CM_Get_DevNode_Registry_PropertyA(devinst, CM_DRP_HARDWAREID, &reg_type, NULL, &reg_size, 0);
  if (cr != CR_SUCCESS && cr != CR_BUFFER_SMALL)
    {
      a->warning("Cannot retrieve hardware ids for PCI device %s: %s.", devinst_id, cr_strerror(cr));
      return;
    }
  else if (reg_type != REG_MULTI_SZ && reg_type != REG_SZ) /* Older Windows versions return REG_SZ and new versions REG_MULTI_SZ. */
    {
      a->warning("Cannot retrieve hardware ids for PCI device %s: Hardware ids are stored as unknown type 0x%lx.", devinst_id, reg_type);
      return;
    }

retry:
  /*
   * Returned size is on older Windows versions without nul-term char.
   * So explicitly increase size and fill nul-term byte.
   */
  reg_size++;
  hardware_ids = pci_malloc(a, reg_size);
  reg_len = reg_size;
  cr = CM_Get_DevNode_Registry_PropertyA(devinst, CM_DRP_HARDWAREID, &reg_type, hardware_ids, &reg_len, 0);
  hardware_ids[reg_size - 1] = 0;
  if (reg_len > reg_size)
    {
      pci_mfree(hardware_ids);
      reg_size = reg_len;
      goto retry;
    }
  else if (cr != CR_SUCCESS)
    {
      a->warning("Cannot retrieve hardware ids for PCI device %s: %s.", devinst_id, cr_strerror(cr));
      pci_mfree(hardware_ids);
      return;
    }
  else if (reg_type != REG_MULTI_SZ && reg_type != REG_SZ) /* Older Windows versions return REG_SZ and new versions REG_MULTI_SZ. */
    {
      a->warning("Cannot retrieve hardware ids for PCI device %s: Hardware ids are stored as unknown type 0x%lx.", devinst_id, reg_type);
      pci_mfree(hardware_ids);
      return;
    }

  /*
   * Hardware ids is nul-separated nul-term string list where each string has
   * one of the following format:
   *  "PCI\\VEN_####&DEV_####&SUBSYS_########&REV_##"
   *  "PCI\\VEN_####&DEV_####&SUBSYS_########"
   *  "PCI\\VEN_####&DEV_####&REV_##&CC_####"
   *  "PCI\\VEN_####&DEV_####&CC_######"
   *  "PCI\\VEN_####&DEV_####&CC_####"
   *  "PCI\\VEN_####&DEV_####&REV_##"
   *  "PCI\\VEN_####&DEV_####"
   */
  for (str = hardware_ids; *str != '\0'; str += strlen(str) + 1)
    {
      if (strncmp(str, "PCI\\", 4) != 0)
        continue;
      str += 4;
      fill_data_from_string(d, str);
    }

  pci_mfree(hardware_ids);
}

static void
scan_devinst_id(struct pci_access *a, DEVINSTID_A devinst_id)
{
  unsigned int domain, bus, dev, func;
  struct pci_dev *d;
  DEVINST devinst;
  CONFIGRET cr;

  cr = CM_Locate_DevNodeA(&devinst, devinst_id, CM_LOCATE_DEVNODE_NORMAL);
  if (cr != CR_SUCCESS)
    {
      /* Do not show warning when device is not present (= does not match NORMAL flag). */
      if (cr != CR_NO_SUCH_DEVNODE)
        a->warning("Cannot retrieve handle for device %s: %s.", devinst_id, cr_strerror(cr));
      return;
    }

  /* get_device_location() returns FALSE if devinst is not real PCI device. */
  if (!get_device_location(a, devinst, devinst_id, &domain, &bus, &dev, &func))
    return;

  d = pci_get_dev(a, domain, bus, dev, func);
  pci_link_dev(a, d);
  if (!d->access->aux)
    d->no_config_access = 1;
  d->aux = (void *)devinst;

  /* Parse device id part of devinst id and fill details into pci_dev. */
  if (!a->buscentric)
    fill_data_from_devinst_id(d, devinst_id);

  /* Retrieve hardware ids of devinst, parse them and fill details into pci_dev. */
  if (!a->buscentric)
    fill_data_from_hardware_ids(d, devinst, devinst_id);

  if (!a->buscentric)
    fill_resources(d, devinst, devinst_id);

  /*
   * Set parent field to cfgmgr32 parent devinst handle and aux field to current
   * devinst handle. At later stage in win32_cfgmgr32_scan() when all pci_dev
   * devices are linked, change every devinst handle by pci_dev.
   */
  if (!a->buscentric)
    {
      DEVINST parent_devinst;
      if (CM_Get_Parent(&parent_devinst, devinst, 0) != CR_SUCCESS)
        {
          parent_devinst = 0;
          a->warning("Cannot retrieve parent handle for device %s: %s.", devinst_id, cr_strerror(cr));
        }
      d->parent = (void *)parent_devinst;
    }
}

static void
win32_cfgmgr32_scan(struct pci_access *a)
{
  ULONG devinst_id_list_size;
  PCHAR devinst_id_list;
  DEVINSTID_A devinst_id;
  struct pci_dev *d;
  CONFIGRET cr;

  if (!resolve_cfgmgr32_functions())
    {
      a->warning("Required cfgmgr32.dll functions are unavailable.");
      return;
    }

  /*
   * Explicitly initialize size to zero as wine cfgmgr32 implementation does not
   * support this API but returns CR_SUCCESS without touching size argument.
   */
  devinst_id_list_size = 0;
  cr = CM_Get_Device_ID_List_SizeA(&devinst_id_list_size, "PCI", CM_GETIDLIST_FILTER_ENUMERATOR);
  if (cr != CR_SUCCESS)
    {
      a->warning("Cannot retrieve list of PCI devices: %s.", cr_strerror(cr));
      return;
    }
  else if (devinst_id_list_size <= 1)
    {
      a->warning("Cannot retrieve list of PCI devices: No device was found.");
      return;
    }

  devinst_id_list = pci_malloc(a, devinst_id_list_size);
  cr = CM_Get_Device_ID_ListA("PCI", devinst_id_list, devinst_id_list_size, CM_GETIDLIST_FILTER_ENUMERATOR);
  if (cr != CR_SUCCESS)
    {
      a->warning("Cannot retrieve list of PCI devices: %s.", cr_strerror(cr));
      pci_mfree(devinst_id_list);
      return;
    }

  /* Register pci_dev for each cfgmgr32 devinst handle. */
  for (devinst_id = devinst_id_list; *devinst_id; devinst_id += strlen(devinst_id) + 1)
    scan_devinst_id(a, devinst_id);

  /* Fill all drivers. */
  if (!a->buscentric)
    fill_drivers(a);

  /* Switch parent fields from cfgmgr32 devinst handle to pci_dev. */
  if (!a->buscentric)
    {
      struct pci_dev *d1, *d2;
      for (d1 = a->devices; d1; d1 = d1->next)
        {
          for (d2 = a->devices; d2; d2 = d2->next)
            if ((DEVINST)d1->parent == (DEVINST)d2->aux)
              break;
          d1->parent = d2;
          if (d1->parent)
            d1->known_fields |= PCI_FILL_PARENT;
        }
    }

  /* devinst stored in ->aux is not needed anymore, clear it. */
  for (d = a->devices; d; d = d->next)
    d->aux = NULL;

  pci_mfree(devinst_id_list);
}

static void
win32_cfgmgr32_config(struct pci_access *a)
{
  pci_define_param(a, "win32.cfgmethod", "auto", "PCI config space access method");
}

static int
win32_cfgmgr32_detect(struct pci_access *a)
{
  ULONG devinst_id_list_size;
  CONFIGRET cr;

  if (!resolve_cfgmgr32_functions())
    {
      a->debug("Required cfgmgr32.dll functions are unavailable.");
      return 0;
    }

  /*
   * Explicitly initialize size to zero as wine cfgmgr32 implementation does not
   * support this API but returns CR_SUCCESS without touching size argument.
   */
  devinst_id_list_size = 0;
  cr = CM_Get_Device_ID_List_SizeA(&devinst_id_list_size, "PCI", CM_GETIDLIST_FILTER_ENUMERATOR);
  if (cr != CR_SUCCESS)
    {
      a->debug("CM_Get_Device_ID_List_SizeA(\"PCI\"): %s.", cr_strerror(cr));
      return 0;
    }
  else if (devinst_id_list_size <= 1)
    {
      a->debug("CM_Get_Device_ID_List_SizeA(\"PCI\"): No device was found.");
      return 0;
    }

  return 1;
}

static void
win32_cfgmgr32_fill_info(struct pci_dev *d, unsigned int flags)
{
  /*
   * All available flags were filled by win32_cfgmgr32_scan().
   * Filling more flags is possible only from config space.
   */
  if (!d->access->aux)
    return;

  pci_generic_fill_info(d, flags);
}

static int
win32_cfgmgr32_read(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct pci_access *a = d->access;
  struct pci_access *acfg = a->aux;
  struct pci_dev *dcfg = d->aux;

  if (!acfg)
    return pci_emulated_read(d, pos, buf, len);

  if (!dcfg)
    d->aux = dcfg = pci_get_dev(acfg, d->domain, d->bus, d->dev, d->func);

  return pci_read_block(dcfg, pos, buf, len);
}

static int
win32_cfgmgr32_write(struct pci_dev *d, int pos, byte *buf, int len)
{
  struct pci_access *a = d->access;
  struct pci_access *acfg = a->aux;
  struct pci_dev *dcfg = d->aux;

  if (!acfg)
    return 0;

  if (!dcfg)
    d->aux = dcfg = pci_get_dev(acfg, d->domain, d->bus, d->dev, d->func);

  return pci_write_block(dcfg, pos, buf, len);
}

static void
win32_cfgmgr32_cleanup_dev(struct pci_dev *d)
{
  struct pci_dev *dcfg = d->aux;

  if (dcfg)
    pci_free_dev(dcfg);
}

static void
win32_cfgmgr32_init(struct pci_access *a)
{
  char *cfgmethod = pci_get_param(a, "win32.cfgmethod");
  struct pci_access *acfg;

  if (strcmp(cfgmethod, "") == 0 ||
      strcmp(cfgmethod, "auto") == 0)
    {
      acfg = pci_clone_access(a);
      acfg->method = PCI_ACCESS_AUTO;
    }
  else if (strcmp(cfgmethod, "none") == 0 ||
           strcmp(cfgmethod, "win32-cfgmgr32") == 0)
    {
      if (a->writeable)
        a->error("Write access requested but option win32.cfgmethod was not set.");
      return;
    }
  else
    {
      int m = pci_lookup_method(cfgmethod);
      if (m < 0)
        a->error("Option win32.cfgmethod is set to an unknown access method \"%s\".", cfgmethod);
      acfg = pci_clone_access(a);
      acfg->method = m;
    }

  a->debug("Loading config space access method...\n");
  if (!pci_init_internal(acfg, PCI_ACCESS_WIN32_CFGMGR32))
    {
      pci_cleanup(acfg);
      a->debug("Cannot find any working config space access method.\n");
      if (a->writeable)
        a->error("Write access requested but no usable access method found.");
      return;
    }

  a->aux = acfg;
}

static void
win32_cfgmgr32_cleanup(struct pci_access *a)
{
  struct pci_access *acfg = a->aux;

  if (acfg)
    pci_cleanup(acfg);
}

struct pci_methods pm_win32_cfgmgr32 = {
  "win32-cfgmgr32",
  "Win32 device listing via Configuration Manager",
  win32_cfgmgr32_config,
  win32_cfgmgr32_detect,
  win32_cfgmgr32_init,
  win32_cfgmgr32_cleanup,
  win32_cfgmgr32_scan,
  win32_cfgmgr32_fill_info,
  win32_cfgmgr32_read,
  win32_cfgmgr32_write,
  NULL,                                 /* read_vpd */
  NULL,                                 /* init_dev */
  win32_cfgmgr32_cleanup_dev,
};
