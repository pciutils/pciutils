/*
 *      The PCI Library -- Win32 helper functions
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <windows.h>
#include <stdio.h> /* for sprintf() */

#include "win32-helpers.h"

const char *
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

BOOL
win32_is_non_nt_system(void)
{
  OSVERSIONINFOA version;
  version.dwOSVersionInfoSize = sizeof(version);
  return GetVersionExA(&version) && version.dwPlatformId < VER_PLATFORM_WIN32_NT;
}

BOOL
win32_is_32bit_on_64bit_system(void)
{
  BOOL (WINAPI *MyIsWow64Process)(HANDLE, PBOOL);
  HMODULE kernel32;
  BOOL is_wow64;

  /*
   * Check for 64-bit system via IsWow64Process() function exported
   * from 32-bit kernel32.dll library available on the 64-bit systems.
   * Resolve pointer to this function at runtime as this code path is
   * primary running on 32-bit systems where are not available 64-bit
   * functions.
   */

  kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
  if (!kernel32)
    return FALSE;

  MyIsWow64Process = (void *)GetProcAddress(kernel32, "IsWow64Process");
  if (!MyIsWow64Process)
    return FALSE;

  if (!MyIsWow64Process(GetCurrentProcess(), &is_wow64))
    return FALSE;

  return is_wow64;
}

BOOL
win32_is_32bit_on_win8_64bit_system(void)
{
#ifdef _WIN64
  return FALSE;
#else
  OSVERSIONINFOA version;

  /* Check for Windows 8 (NT 6.2). */
  version.dwOSVersionInfoSize = sizeof(version);
  if (!GetVersionExA(&version) ||
      version.dwPlatformId != VER_PLATFORM_WIN32_NT ||
      version.dwMajorVersion < 6 ||
      (version.dwMajorVersion == 6 && version.dwMinorVersion < 2))
    return FALSE;

  return win32_is_32bit_on_64bit_system();
#endif
}
