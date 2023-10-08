/*
 *	The PCI Library -- Access to i386 I/O ports on Windows
 *
 *	Copyright (c) 2004 Alexander Stock <stock.alexander@gmx.de>
 *	Copyright (c) 2006 Martin Mares <mj@ucw.cz>
 *	Copyright (c) 2021 Pali Rohár <pali@kernel.org>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *	SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <windows.h>
#include <aclapi.h>

#include "i386-io-access.h"

/*
 * Define __readeflags() for MSVC and GCC compilers.
 * MSVC since version 14.00 included in WDK 6001 and since version 15.00
 * included in VS 2008 provides __readeflags() intrinsic for both 32 and 64-bit
 * modes. WDK 6001 defines macro __BUILDMACHINE__ to value WinDDK. VS 2008 does
 * not define this macro at all. MSVC throws error if name of user defined
 * function conflicts with some MSVC intrinsic.
 * MSVC supports inline assembly via __asm keyword in 32-bit mode only.
 * GCC version 4.9.0 and higher provides __builtin_ia32_readeflags_uXX()
 * builtin for XX-mode. This builtin is also available as __readeflags()
 * function indirectly via <x86intrin.h> header file.
 */
#if defined(_MSC_VER) && (_MSC_VER >= 1500 || (_MSC_VER >= 1400 && defined(__BUILDMACHINE__)))
#pragma intrinsic(__readeflags)
#elif defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 9) || (__GNUC__ > 4))
#include <x86intrin.h>
#elif defined(_MSC_VER) && defined(_M_IX86)
static inline unsigned int
__readeflags(void)
{
  __asm pushfd;
  __asm pop eax;
}
#elif defined(__GNUC__)
static inline unsigned
#ifdef __x86_64__
long long
#endif
int
__readeflags(void)
{
  unsigned
#ifdef __x86_64__
  long long
#endif
  int eflags;
  asm volatile ("pushf\n\tpop %0\n" : "=r" (eflags));
  return eflags;
}
#else
#error "Unsupported compiler"
#endif

/* Read IOPL of the current process, IOPL is stored in eflag bits [13:12]. */
#define read_iopl() ((__readeflags() >> 12) & 0x3)

/* Unfortunately i586-mingw32msvc toolchain does not provide this constant. */
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

/* Unfortunately some toolchains do not provide this constant. */
#ifndef SE_IMPERSONATE_NAME
#define SE_IMPERSONATE_NAME TEXT("SeImpersonatePrivilege")
#endif

/*
 * These psapi functions are available in kernel32.dll library with K32 prefix
 * on Windows 7 and higher systems. On older Windows systems these functions are
 * available in psapi.dll libary without K32 prefix. So resolve pointers to
 * these functions dynamically at runtime from the available system library.
 * Function GetProcessImageFileNameW() is not available on Windows 2000 and
 * older systems.
 */
typedef BOOL (WINAPI *EnumProcessesProt)(DWORD *lpidProcess, DWORD cb, DWORD *cbNeeded);
typedef DWORD (WINAPI *GetProcessImageFileNameWProt)(HANDLE hProcess, LPWSTR lpImageFileName, DWORD nSize);
typedef DWORD (WINAPI *GetModuleFileNameExWProt)(HANDLE hProcess, HMODULE hModule, LPWSTR lpImageFileName, DWORD nSize);

/*
 * These aclapi functions are available in advapi.dll library on Windows NT 4.0
 * and higher systems.
 */
typedef DWORD (WINAPI *GetSecurityInfoProt)(HANDLE handle, SE_OBJECT_TYPE ObjectType, SECURITY_INFORMATION SecurityInfo, PSID *ppsidOwner, PSID *ppsidGroup, PACL *ppDacl, PACL *ppSacl, PSECURITY_DESCRIPTOR *ppSecurityDescriptor);
typedef DWORD (WINAPI *SetSecurityInfoProt)(HANDLE handle, SE_OBJECT_TYPE ObjectType, SECURITY_INFORMATION SecurityInfo, PSID psidOwner, PSID psidGroup, PACL pDacl, PACL pSacl);
typedef DWORD (WINAPI *SetEntriesInAclProt)(ULONG cCountOfExplicitEntries, PEXPLICIT_ACCESS pListOfExplicitEntries, PACL OldAcl, PACL *NewAcl);

/*
 * This errhandlingapi function is available in kernel32.dll library on
 * Windows 7 and higher systems.
 */
typedef BOOL (WINAPI *SetThreadErrorModeProt)(DWORD dwNewMode, LPDWORD lpOldMode);

/*
 * Unfortunately NtSetInformationProcess() function, ProcessUserModeIOPL
 * constant and all other helpers for its usage are not specified in any
 * standard WinAPI header file. So define all of required constants and types.
 * Function NtSetInformationProcess() is available in ntdll.dll library on all
 * Windows systems but marked as it can be removed in some future version.
 */
#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED (NTSTATUS)0xC0000002
#endif
#ifndef STATUS_PRIVILEGE_NOT_HELD
#define STATUS_PRIVILEGE_NOT_HELD (NTSTATUS)0xC0000061
#endif
#ifndef PROCESSINFOCLASS
#define PROCESSINFOCLASS DWORD
#endif
#ifndef ProcessUserModeIOPL
#define ProcessUserModeIOPL 16
#endif
typedef NTSTATUS (NTAPI *NtSetInformationProcessProt)(HANDLE ProcessHandle, PROCESSINFOCLASS ProcessInformationClass, PVOID ProcessInformation, ULONG ProcessInformationLength);

/*
 * Check if the current thread has particular privilege in current active access
 * token. Case when it not possible to determinate it (e.g. current thread does
 * not have permission to open its own current active access token) is evaluated
 * as thread does not have that privilege.
 */
static BOOL
have_privilege(LUID luid_privilege)
{
  PRIVILEGE_SET priv;
  HANDLE token;
  BOOL ret;

  /*
   * If the current thread does not have active access token then thread
   * uses primary process access token for all permission checks.
   */
  if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) &&
      (GetLastError() != ERROR_NO_TOKEN ||
       !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)))
    return FALSE;

  priv.PrivilegeCount = 1;
  priv.Control = PRIVILEGE_SET_ALL_NECESSARY;
  priv.Privilege[0].Luid = luid_privilege;
  priv.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;

  if (!PrivilegeCheck(token, &priv, &ret))
    return FALSE;

  return ret;
}

/*
 * Enable or disable particular privilege in specified access token.
 *
 * Note that it is not possible to disable privilege in access token with
 * SE_PRIVILEGE_ENABLED_BY_DEFAULT attribute. This function does not check
 * this case and incorrectly returns no error even when disabling failed.
 * Rationale for this decision: Simplification of this function as WinAPI
 * call AdjustTokenPrivileges() does not signal error in this case too.
 */
static BOOL
set_privilege(HANDLE token, LUID luid_privilege, BOOL enable)
{
  TOKEN_PRIVILEGES token_privileges;

  token_privileges.PrivilegeCount = 1;
  token_privileges.Privileges[0].Luid = luid_privilege;
  token_privileges.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;

  /*
   * WinAPI function AdjustTokenPrivileges() success also when not all
   * privileges were enabled. It is always required to check for failure
   * via GetLastError() call. AdjustTokenPrivileges() always sets error
   * also when it success, as opposite to other WinAPI functions.
   */
  if (!AdjustTokenPrivileges(token, FALSE, &token_privileges, sizeof(token_privileges), NULL, NULL) ||
      GetLastError() != ERROR_SUCCESS)
    return FALSE;

  return TRUE;
}

/*
 * Change access token for the current thread to new specified access token.
 * Previously active access token is stored in old_token variable and can be
 * used for reverting to this access token. It is set to NULL if the current
 * thread previously used primary process access token.
 */
static BOOL
change_token(HANDLE new_token, HANDLE *old_token)
{
  HANDLE token;

  if (!OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &token))
    {
      if (GetLastError() != ERROR_NO_TOKEN)
        return FALSE;
      token = NULL;
    }

  if (!ImpersonateLoggedOnUser(new_token))
    {
      if (token)
        CloseHandle(token);
      return FALSE;
    }

  *old_token = token;
  return TRUE;
}

/*
 * Change access token for the current thread to the primary process access
 * token. This function fails also when the current thread already uses primary
 * process access token.
 */
static BOOL
change_token_to_primary(HANDLE *old_token)
{
  HANDLE token;

  if (!OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &token))
    return FALSE;

  RevertToSelf();

  *old_token = token;
  return TRUE;
}

/*
 * Revert to the specified access token for the current thread. When access
 * token is specified as NULL then revert to the primary process access token.
 * Use to revert after change_token() or change_token_to_primary() call.
 */
static VOID
revert_to_token(HANDLE token)
{
  /*
   * If SetThreadToken() call fails then there is no option to revert to
   * the specified previous thread access token. So in this case revert to
   * the primary process access token.
   */
  if (!token || !SetThreadToken(NULL, token))
    RevertToSelf();
  if (token)
    CloseHandle(token);
}

/*
 * Enable particular privilege for the current thread. And set method how to
 * revert this privilege (if to revert whole token or only privilege).
 */
static BOOL
enable_privilege(LUID luid_privilege, HANDLE *revert_token, BOOL *revert_only_privilege)
{
  HANDLE thread_token;
  HANDLE new_token;

  if (OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, TRUE, &thread_token))
    {
      if (set_privilege(thread_token, luid_privilege, TRUE))
        {
          /*
           * Indicate that correct revert method is just to
           * disable privilege in access token.
           */
          if (revert_token && revert_only_privilege)
            {
              *revert_token = thread_token;
              *revert_only_privilege = TRUE;
            }
          else
            {
              CloseHandle(thread_token);
            }
          return TRUE;
        }
      CloseHandle(thread_token);
      /*
       * If enabling privilege failed then try to enable it via
       * primary process access token.
       */
    }

  /*
   * If the current thread has already active thread access token then
   * open it with just impersonate right as it would be used only for
   * future revert.
   */
  if (revert_token && revert_only_privilege)
    {
      if (!OpenThreadToken(GetCurrentThread(), TOKEN_IMPERSONATE, TRUE, &thread_token))
        {
          if (GetLastError() != ERROR_NO_TOKEN)
            return FALSE;
          thread_token = NULL;
        }

      /*
       * If current thread has no access token (and uses primary
       * process access token) or it does not have permission to
       * adjust privileges or it does not have specified privilege
       * then create a copy of the primary process access token,
       * assign it for the current thread (= impersonate self)
       * and then try adjusting privilege again.
       */
      if (!ImpersonateSelf(SecurityImpersonation))
        {
          if (thread_token)
            CloseHandle(thread_token);
          return FALSE;
        }
    }

  if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, TRUE, &new_token))
    {
      /* thread_token is set only when we were asked for revert method. */
      if (revert_token && revert_only_privilege)
        revert_to_token(thread_token);
      return FALSE;
    }

  if (!set_privilege(new_token, luid_privilege, TRUE))
    {
      CloseHandle(new_token);
      /* thread_token is set only when we were asked for revert method. */
      if (revert_token && revert_only_privilege)
        revert_to_token(thread_token);
      return FALSE;
    }

  /*
   * Indicate that correct revert method is to change to the previous
   * access token. Either to the primary process access token or to the
   * previous thread access token.
   */
  if (revert_token && revert_only_privilege)
    {
      *revert_token = thread_token;
      *revert_only_privilege = FALSE;
    }
  return TRUE;
}

/*
 * Revert particular privilege for the current thread was previously enabled by
 * enable_privilege() call. Either disable privilege in specified access token
 * or revert to previous access token.
 */
static VOID
revert_privilege(LUID luid_privilege, HANDLE revert_token, BOOL revert_only_privilege)
{
  if (revert_only_privilege)
    {
      set_privilege(revert_token, luid_privilege, FALSE);
      CloseHandle(revert_token);
    }
  else
    {
      revert_to_token(revert_token);
    }
}

/*
 * Return owner of the access token used by the current thread. Buffer for
 * returned owner needs to be released by LocalFree() call.
 */
static TOKEN_OWNER *
get_current_token_owner(VOID)
{
  HANDLE token;
  DWORD length;
  TOKEN_OWNER *owner;

  /*
   * If the current thread does not have active access token then thread
   * uses primary process access token for all permission checks.
   */
  if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token) &&
      (GetLastError() != ERROR_NO_TOKEN ||
       !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)))
    return NULL;

  if (!GetTokenInformation(token, TokenOwner, NULL, 0, &length) &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
      CloseHandle(token);
      return NULL;
    }

retry:
  owner = (TOKEN_OWNER *)LocalAlloc(LPTR, length);
  if (!owner)
    {
      CloseHandle(token);
      return NULL;
    }

  if (!GetTokenInformation(token, TokenOwner, owner, length, &length))
    {
      /*
       * Length of token owner (SID) buffer between two get calls may
       * changes (e.g. by another thread of process), so retry.
       */
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
          LocalFree(owner);
          goto retry;
        }
      LocalFree(owner);
      CloseHandle(token);
      return NULL;
    }

  CloseHandle(token);
  return owner;
}

/*
 * Grant particular permissions in the primary access token of the specified
 * process for the owner of current thread token and set old DACL of the
 * process access token for reverting permissions. Security descriptor is
 * just memory buffer for old DACL.
 */
static BOOL
grant_process_token_dacl_permissions(HANDLE process, DWORD permissions, HANDLE *token, PACL *old_dacl, PSECURITY_DESCRIPTOR *security_descriptor)
{
  GetSecurityInfoProt MyGetSecurityInfo;
  SetSecurityInfoProt MySetSecurityInfo;
  SetEntriesInAclProt MySetEntriesInAcl;
  EXPLICIT_ACCESS explicit_access;
  TOKEN_OWNER *owner;
  HMODULE advapi32;
  PACL new_dacl;

  /*
   * This source file already uses advapi32.dll library, so it is
   * linked to executable and automatically loaded when starting
   * current running process.
   */
  advapi32 = GetModuleHandle(TEXT("advapi32.dll"));
  if (!advapi32)
    return FALSE;

  /*
   * It does not matter if SetEntriesInAclA() or SetEntriesInAclW() is
   * called as no string is passed to SetEntriesInAcl function.
   */
  MyGetSecurityInfo = (GetSecurityInfoProt)(LPVOID)GetProcAddress(advapi32, "GetSecurityInfo");
  MySetSecurityInfo = (SetSecurityInfoProt)(LPVOID)GetProcAddress(advapi32, "SetSecurityInfo");
  MySetEntriesInAcl = (SetEntriesInAclProt)(LPVOID)GetProcAddress(advapi32, "SetEntriesInAclA");
  if (!MyGetSecurityInfo || !MySetSecurityInfo || !MySetEntriesInAcl)
    return FALSE;

  owner = get_current_token_owner();
  if (!owner)
    return FALSE;

  /*
   * READ_CONTROL is required for GetSecurityInfo(DACL_SECURITY_INFORMATION)
   * and WRITE_DAC is required for SetSecurityInfo(DACL_SECURITY_INFORMATION).
   */
  if (!OpenProcessToken(process, READ_CONTROL | WRITE_DAC, token))
    {
      LocalFree(owner);
      return FALSE;
    }

  if (MyGetSecurityInfo(*token, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, old_dacl, NULL, security_descriptor) != ERROR_SUCCESS)
    {
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  /*
   * Set new explicit access for the owner of the current thread access
   * token with non-inherited granting access to specified permissions.
   */
  explicit_access.grfAccessPermissions = permissions;
  explicit_access.grfAccessMode = GRANT_ACCESS;
  explicit_access.grfInheritance = NO_PROPAGATE_INHERIT_ACE;
  explicit_access.Trustee.pMultipleTrustee = NULL;
  explicit_access.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
  explicit_access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  explicit_access.Trustee.TrusteeType = TRUSTEE_IS_USER;
  /*
   * Unfortunately i586-mingw32msvc toolchain does not have pSid pointer
   * member in Trustee union. So assign owner SID to ptstrName pointer
   * member which aliases with pSid pointer member in the same union.
   */
  explicit_access.Trustee.ptstrName = (PVOID)owner->Owner;

  if (MySetEntriesInAcl(1, &explicit_access, *old_dacl, &new_dacl) != ERROR_SUCCESS)
    {
      LocalFree(*security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  if (MySetSecurityInfo(*token, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, new_dacl, NULL) != ERROR_SUCCESS)
    {
      LocalFree(*security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  LocalFree(owner);
  return TRUE;
}

/*
 * Revert particular granted permissions in specified access token done by
 * grant_process_token_dacl_permissions() call.
 */
static VOID
revert_token_dacl_permissions(HANDLE token, PACL old_dacl, PSECURITY_DESCRIPTOR security_descriptor)
{
  SetSecurityInfoProt MySetSecurityInfo;
  HMODULE advapi32;

  /*
   * This source file already uses advapi32.dll library, so it is
   * linked to executable and automatically loaded when starting
   * current running process.
   */
  advapi32 = GetModuleHandle(TEXT("advapi32.dll"));
  if (advapi32)
    {
      MySetSecurityInfo = (SetSecurityInfoProt)(LPVOID)GetProcAddress(advapi32, "SetSecurityInfo");
      MySetSecurityInfo(token, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, old_dacl, NULL);
    }

  LocalFree(security_descriptor);
  CloseHandle(token);
}

/*
 * Change error mode of the current thread. If it is not possible then change
 * error mode of the whole process. Always returns previous error mode.
 */
static UINT
change_error_mode(UINT new_mode)
{
  SetThreadErrorModeProt MySetThreadErrorMode = NULL;
  HMODULE kernel32;
  DWORD old_mode;

  /*
   * Function SetThreadErrorMode() was introduced in Windows 7, so use
   * GetProcAddress() for compatibility with older systems.
   */
  kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
  if (kernel32)
    MySetThreadErrorMode = (SetThreadErrorModeProt)(LPVOID)GetProcAddress(kernel32, "SetThreadErrorMode");

  if (MySetThreadErrorMode &&
      MySetThreadErrorMode(new_mode, &old_mode))
    return old_mode;

  /*
   * Fallback to function SetErrorMode() which modifies error mode of the
   * whole process and returns old mode.
   */
  return SetErrorMode(new_mode);
}

/*
 * Open process handle specified by the process id with the query right and
 * optionally also with vm read right.
 */
static HANDLE
open_process_for_query(DWORD pid, BOOL with_vm_read)
{
  BOOL revert_only_privilege;
  LUID luid_debug_privilege;
  OSVERSIONINFO version;
  DWORD process_right;
  HANDLE revert_token;
  HANDLE process;

  /*
   * Some processes on Windows Vista and higher systems can be opened only
   * with PROCESS_QUERY_LIMITED_INFORMATION right. This right is enough
   * for accessing primary process token. But this right is not supported
   * on older pre-Vista systems. When the current thread on these older
   * systems does not have Debug privilege then OpenProcess() fails with
   * ERROR_ACCESS_DENIED. If the current thread has Debug privilege then
   * OpenProcess() success and returns handle to requested process.
   * Problem is that this handle does not have PROCESS_QUERY_INFORMATION
   * right and so cannot be used for accessing primary process token
   * on those older systems. Moreover it has zero rights and therefore
   * such handle is fully useless. So never try to use open process with
   * PROCESS_QUERY_LIMITED_INFORMATION right on older systems than
   * Windows Vista (NT 6.0).
   */
  version.dwOSVersionInfoSize = sizeof(version);
  if (GetVersionEx(&version) &&
      version.dwPlatformId == VER_PLATFORM_WIN32_NT &&
      version.dwMajorVersion >= 6)
    process_right = PROCESS_QUERY_LIMITED_INFORMATION;
  else
    process_right = PROCESS_QUERY_INFORMATION;

  if (with_vm_read)
    process_right |= PROCESS_VM_READ;

  process = OpenProcess(process_right, FALSE, pid);
  if (process)
    return process;

  /*
   * It is possible to open only processes to which owner of the current
   * thread access token has permissions. For opening other processing it
   * is required to have Debug privilege enabled. By default local
   * administrators have this privilege, but it is disabled. So try to
   * enable it and then try to open process again.
   */

  if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid_debug_privilege))
    return NULL;

  if (!enable_privilege(luid_debug_privilege, &revert_token, &revert_only_privilege))
    return NULL;

  process = OpenProcess(process_right, FALSE, pid);

  revert_privilege(luid_debug_privilege, revert_token, revert_only_privilege);

  return process;
}

/*
 * Check if process image path name (wide string) matches exe file name
 * (7-bit ASCII string). Do case-insensitive string comparison. Process
 * image path name can be in any namespace format (DOS, Win32, UNC, ...).
 */
static BOOL
check_process_name(LPCWSTR path, DWORD path_length, LPCSTR exe_file)
{
  DWORD exe_file_length;
  WCHAR c1;
  UCHAR c2;
  DWORD i;

  exe_file_length = 0;
  while (exe_file[exe_file_length] != '\0')
    exe_file_length++;

  /* Path must have backslash before exe file name. */
  if (exe_file_length >= path_length ||
      path[path_length-exe_file_length-1] != L'\\')
    return FALSE;

  for (i = 0; i < exe_file_length; i++)
    {
      c1 = path[path_length-exe_file_length+i];
      c2 = exe_file[i];
      /*
       * Input string for comparison is 7-bit ASCII and file name part
       * of path must not contain backslash as it is path separator.
       */
      if (c1 >= 0x80 || c2 >= 0x80 || c1 == L'\\')
        return FALSE;
      if (c1 >= L'a' && c1 <= L'z')
        c1 -= L'a' - L'A';
      if (c2 >= 'a' && c2 <= 'z')
        c2 -= 'a' - 'A';
      if (c1 != c2)
        return FALSE;
    }

  return TRUE;
}

/* Open process handle with the query right specified by process exe file. */
static HANDLE
find_and_open_process_for_query(LPCSTR exe_file)
{
  GetProcessImageFileNameWProt MyGetProcessImageFileNameW;
  GetModuleFileNameExWProt MyGetModuleFileNameExW;
  EnumProcessesProt MyEnumProcesses;
  HMODULE kernel32, psapi;
  UINT prev_error_mode;
  DWORD partial_retry;
  BOOL found_process;
  DWORD size, length;
  DWORD *processes;
  HANDLE process;
  LPWSTR path;
  DWORD error;
  DWORD count;
  DWORD i;

  psapi = NULL;
  kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
  if (!kernel32)
    return NULL;

  /*
   * On Windows 7 and higher systems these functions are available in
   * kernel32.dll library with K32 prefix.
   */
  MyGetModuleFileNameExW = NULL;
  MyGetProcessImageFileNameW = (GetProcessImageFileNameWProt)(LPVOID)GetProcAddress(kernel32, "K32GetProcessImageFileNameW");
  MyEnumProcesses = (EnumProcessesProt)(LPVOID)GetProcAddress(kernel32, "K32EnumProcesses");
  if (!MyGetProcessImageFileNameW || !MyEnumProcesses)
    {
      /*
       * On older NT-based systems these functions are available in
       * psapi.dll library without K32 prefix.
       */
      prev_error_mode = change_error_mode(SEM_FAILCRITICALERRORS);
      psapi = LoadLibrary(TEXT("psapi.dll"));
      change_error_mode(prev_error_mode);

      if (!psapi)
        return NULL;

      /*
       * Function GetProcessImageFileNameW() is available in
       * Windows XP and higher systems. On older versions is
       * available function GetModuleFileNameExW().
       */
      MyGetProcessImageFileNameW = (GetProcessImageFileNameWProt)(LPVOID)GetProcAddress(psapi, "GetProcessImageFileNameW");
      MyGetModuleFileNameExW = (GetModuleFileNameExWProt)(LPVOID)GetProcAddress(psapi, "GetModuleFileNameExW");
      MyEnumProcesses = (EnumProcessesProt)(LPVOID)GetProcAddress(psapi, "EnumProcesses");
      if ((!MyGetProcessImageFileNameW && !MyGetModuleFileNameExW) || !MyEnumProcesses)
        {
          FreeLibrary(psapi);
          return NULL;
        }
    }

  /* Make initial buffer size for 1024 processes. */
  size = 1024 * sizeof(*processes);

retry:
  processes = (DWORD *)LocalAlloc(LPTR, size);
  if (!processes)
    {
      if (psapi)
        FreeLibrary(psapi);
      return NULL;
    }

  if (!MyEnumProcesses(processes, size, &length))
    {
      LocalFree(processes);
      if (psapi)
        FreeLibrary(psapi);
      return NULL;
    }
  else if (size == length)
    {
      /*
       * There is no indication given when the buffer is too small to
       * store all process identifiers. Therefore if returned length
       * is same as buffer size there can be more processes. Call
       * again with larger buffer.
       */
      LocalFree(processes);
      size *= 2;
      goto retry;
    }

  process = NULL;
  count = length / sizeof(*processes);

  for (i = 0; i < count; i++)
    {
      /* Skip System Idle Process. */
      if (processes[i] == 0)
        continue;

      /*
       * Function GetModuleFileNameExW() requires additional
       * PROCESS_VM_READ right as opposite to function
       * GetProcessImageFileNameW() which does not need it.
       */
      process = open_process_for_query(processes[i], MyGetProcessImageFileNameW ? FALSE : TRUE);
      if (!process)
        continue;

      /*
       * Set initial buffer size to 256 (wide) characters.
       * Final path length on the modern NT-based systems can be also larger.
       */
      size = 256;
      found_process = FALSE;
      partial_retry = 0;

retry_path:
      path = (LPWSTR)LocalAlloc(LPTR, size * sizeof(*path));
      if (!path)
        goto end_path;

      if (MyGetProcessImageFileNameW)
        length = MyGetProcessImageFileNameW(process, path, size);
      else
        length = MyGetModuleFileNameExW(process, NULL, path, size);

      error = GetLastError();

      /*
       * GetModuleFileNameEx() returns zero and signal error ERROR_PARTIAL_COPY
       * when remote process is in the middle of updating its module table.
       * Sleep 10 ms and try again, max 10 attempts.
       */
      if (!MyGetProcessImageFileNameW)
        {
          if (length == 0 && error == ERROR_PARTIAL_COPY && partial_retry++ < 10)
            {
              Sleep(10);
              goto retry_path;
            }
          partial_retry = 0;
        }

      /*
       * When buffer is too small then function GetModuleFileNameEx() returns
       * its size argument on older systems (Windows XP) or its size minus
       * argument one on new systems (Windows 10) without signalling any error.
       * Function GetProcessImageFileNameW() on the other hand returns zero
       * value and signals error ERROR_INSUFFICIENT_BUFFER. So in all these
       * cases call function again with larger buffer.
       */

      if (MyGetProcessImageFileNameW && length == 0 && error != ERROR_INSUFFICIENT_BUFFER)
        goto end_path;

      if ((MyGetProcessImageFileNameW && length == 0) ||
          (!MyGetProcessImageFileNameW && (length == size || length == size-1)))
        {
          LocalFree(path);
          size *= 2;
          goto retry_path;
        }

      if (length && check_process_name(path, length, exe_file))
        found_process = TRUE;

end_path:
      if (path)
        {
          LocalFree(path);
          path = NULL;
        }

      if (found_process)
        break;

      CloseHandle(process);
      process = NULL;
    }

  LocalFree(processes);

  if (psapi)
    FreeLibrary(psapi);

  return process;
}

/*
 * Try to open primary access token of the particular process with specified
 * rights. Before opening access token try to adjust DACL permissions of the
 * primary process access token, so following open does not fail on error
 * related to no open permissions. Revert DACL permissions after open attempt.
 * As following steps are not atomic, try to execute them more times in case
 * of possible race conditions caused by other threads or processes.
 */
static HANDLE
try_grant_permissions_and_open_process_token(HANDLE process, DWORD rights)
{
  PSECURITY_DESCRIPTOR security_descriptor;
  HANDLE grant_token;
  PACL old_dacl;
  HANDLE token;
  DWORD retry;
  DWORD error;

  /*
   * This code is not atomic. Between grant and open calls can other
   * thread or process change or revert permissions. So try to execute
   * it more times.
   */
  for (retry = 0; retry < 10; retry++)
    {
      if (!grant_process_token_dacl_permissions(process, rights, &grant_token, &old_dacl, &security_descriptor))
        return NULL;
      if (!OpenProcessToken(process, rights, &token))
        {
          token = NULL;
          error = GetLastError();
        }
      revert_token_dacl_permissions(grant_token, old_dacl, security_descriptor);
      if (token)
        return token;
      else if (error != ERROR_ACCESS_DENIED)
        return NULL;
    }

  return NULL;
}

/*
 * Open primary access token of particular process handle with specified rights.
 * If permissions for specified rights are missing then try to grant them.
 */
static HANDLE
open_process_token_with_rights(HANDLE process, DWORD rights)
{
  HANDLE old_token;
  HANDLE token;

  /* First try to open primary access token of process handle directly. */
  if (OpenProcessToken(process, rights, &token))
    return token;

  /*
   * If opening failed then it means that owner of the current thread
   * access token does not have permission for it. Try it again with
   * primary process access token.
   */
  if (change_token_to_primary(&old_token))
    {
      if (!OpenProcessToken(process, rights, &token))
        token = NULL;
      revert_to_token(old_token);
      if (token)
        return token;
    }

  /*
   * If opening is still failing then try to grant specified permissions
   * for the current thread and try to open it again.
   */
  token = try_grant_permissions_and_open_process_token(process, rights);
  if (token)
    return token;

  /*
   * And if it is still failing then try it again with granting
   * permissions for the primary process token of the current process.
   */
  if (change_token_to_primary(&old_token))
    {
      token = try_grant_permissions_and_open_process_token(process, rights);
      revert_to_token(old_token);
      if (token)
        return token;
    }

  /*
   * TODO: Sorry, no other option for now...
   * It could be possible to use Take Ownership Name privilege to
   * temporary change token owner of specified process to the owner of
   * the current thread token, grant permissions for current thread in
   * that process token, change ownership back to original one, open
   * that process token and revert granted permissions. But this is
   * not implemented yet.
   */
  return NULL;
}

/*
 * Set x86 I/O Privilege Level to 3 for the whole current NT process. Do it via
 * NtSetInformationProcess() call with ProcessUserModeIOPL information class,
 * which is supported by 32-bit Windows NT kernel versions and requires Tcb
 * privilege.
 */
static BOOL
SetProcessUserModeIOPL(VOID)
{
  NtSetInformationProcessProt MyNtSetInformationProcess;

  LUID luid_tcb_privilege;
  LUID luid_impersonate_privilege;

  HANDLE revert_token_tcb_privilege;
  BOOL revert_only_tcb_privilege;

  HANDLE revert_token_impersonate_privilege;
  BOOL revert_only_impersonate_privilege;

  BOOL impersonate_privilege_enabled;

  BOOL revert_to_old_token;
  HANDLE old_token;

  HANDLE lsass_process;
  HANDLE lsass_token;

  UINT prev_error_mode;
  NTSTATUS nt_status;
  HMODULE ntdll;
  BOOL ret;

  impersonate_privilege_enabled = FALSE;
  revert_to_old_token = FALSE;
  lsass_token = NULL;
  old_token = NULL;

  /* Fast path when ProcessUserModeIOPL was already called. */
  if (read_iopl() == 3)
    return TRUE;

  /*
   * Load ntdll.dll library with disabled critical-error-handler message box.
   * It means that NT kernel does not show unwanted GUI message box to user
   * when LoadLibrary() function fails.
   */
  prev_error_mode = change_error_mode(SEM_FAILCRITICALERRORS);
  ntdll = LoadLibrary(TEXT("ntdll.dll"));
  change_error_mode(prev_error_mode);
  if (!ntdll)
    goto err_not_implemented;

  /* Retrieve pointer to NtSetInformationProcess() function. */
  MyNtSetInformationProcess = (NtSetInformationProcessProt)(LPVOID)GetProcAddress(ntdll, "NtSetInformationProcess");
  if (!MyNtSetInformationProcess)
    goto err_not_implemented;

  /*
   * ProcessUserModeIOPL is syscall for NT kernel to change x86 IOPL
   * of the current running process to 3.
   *
   * Process handle argument for ProcessUserModeIOPL is ignored and
   * IOPL is always changed for the current running process. So pass
   * GetCurrentProcess() handle for documentation purpose. Process
   * information buffer and length are unused for ProcessUserModeIOPL.
   *
   * ProcessUserModeIOPL may success (return value >= 0) or may fail
   * because it is not implemented or because of missing privilege.
   * Other errors are not defined, so handle them as unknown.
   */
  nt_status = MyNtSetInformationProcess(GetCurrentProcess(), ProcessUserModeIOPL, NULL, 0);
  if (nt_status >= 0)
    goto verify;
  else if (nt_status == STATUS_NOT_IMPLEMENTED)
    goto err_not_implemented;
  else if (nt_status != STATUS_PRIVILEGE_NOT_HELD)
    goto err_unknown;

  /*
   * If ProcessUserModeIOPL call failed with STATUS_PRIVILEGE_NOT_HELD
   * error then it means that the current thread token does not have
   * Tcb privilege enabled. Try to enable it.
   */

  if (!LookupPrivilegeValue(NULL, SE_TCB_NAME, &luid_tcb_privilege))
    goto err_not_implemented;

  /*
   * If the current thread has already Tcb privilege enabled then there
   * is some additional unhanded restriction.
   */
  if (have_privilege(luid_tcb_privilege))
    goto err_privilege_not_held;

  /* Try to enable Tcb privilege and try ProcessUserModeIOPL call again. */
  if (enable_privilege(luid_tcb_privilege, &revert_token_tcb_privilege, &revert_only_tcb_privilege))
    {
      nt_status = MyNtSetInformationProcess(GetCurrentProcess(), ProcessUserModeIOPL, NULL, 0);
      revert_privilege(luid_tcb_privilege, revert_token_tcb_privilege, revert_only_tcb_privilege);
      if (nt_status >= 0)
        goto verify;
      else if (nt_status == STATUS_NOT_IMPLEMENTED)
        goto err_not_implemented;
      else if (nt_status == STATUS_PRIVILEGE_NOT_HELD)
        goto err_privilege_not_held;
      else
        goto err_unknown;
    }

  /*
   * If enabling of Tcb privilege failed then it means that current thread
   * does not this privilege. But current process may have it. So try it
   * again with primary process access token.
   */

  /*
   * If system supports Impersonate privilege (Windows 2000 SP4 or higher) then
   * all future actions in this function require this Impersonate privilege.
   * So try to enable it in case it is currently disabled.
   */
  if (LookupPrivilegeValue(NULL, SE_IMPERSONATE_NAME, &luid_impersonate_privilege) &&
      !have_privilege(luid_impersonate_privilege))
    {
      /*
       * If current thread does not have Impersonate privilege enabled
       * then first try to enable it just for the current thread. If
       * it is not possible to enable it just for the current thread
       * then try it to enable globally for whole process (which
       * affects all process threads). Both actions will be reverted
       * at the end of this function.
       */
      if (enable_privilege(luid_impersonate_privilege, &revert_token_impersonate_privilege, &revert_only_impersonate_privilege))
        {
          impersonate_privilege_enabled = TRUE;
        }
      else if (enable_privilege(luid_impersonate_privilege, NULL, NULL))
        {
          impersonate_privilege_enabled = TRUE;
          revert_token_impersonate_privilege = NULL;
          revert_only_impersonate_privilege = TRUE;
        }
      else
        {
          goto err_privilege_not_held;
        }

      /*
       * Now when Impersonate privilege is enabled, try to enable Tcb
       * privilege again. Enabling other privileges for the current
       * thread requires Impersonate privilege, so enabling Tcb again
       * could now pass.
       */
      if (enable_privilege(luid_tcb_privilege, &revert_token_tcb_privilege, &revert_only_tcb_privilege))
        {
          nt_status = MyNtSetInformationProcess(GetCurrentProcess(), ProcessUserModeIOPL, NULL, 0);
          revert_privilege(luid_tcb_privilege, revert_token_tcb_privilege, revert_only_tcb_privilege);
          if (nt_status >= 0)
            goto verify;
          else if (nt_status == STATUS_NOT_IMPLEMENTED)
            goto err_not_implemented;
          else if (nt_status == STATUS_PRIVILEGE_NOT_HELD)
            goto err_privilege_not_held;
          else
            goto err_unknown;
        }
    }

  /*
   * If enabling Tcb privilege failed then it means that the current
   * thread access token does not have this privilege or does not
   * have permission to adjust privileges.
   *
   * Try to use more privileged token from Local Security Authority
   * Subsystem Service process (lsass.exe) which has Tcb privilege.
   * Retrieving this more privileged token is possible for local
   * administrators (unless it was disabled by local administrators).
   */

  lsass_process = find_and_open_process_for_query("lsass.exe");
  if (!lsass_process)
    goto err_privilege_not_held;

  /*
   * Open primary lsass.exe process access token with query and duplicate
   * rights. Just these two rights are required for impersonating other
   * primary process token (impersonate right is really not required!).
   */
  lsass_token = open_process_token_with_rights(lsass_process, TOKEN_QUERY | TOKEN_DUPLICATE);

  CloseHandle(lsass_process);

  if (!lsass_token)
    goto err_privilege_not_held;

  /*
   * After successful open of the primary lsass.exe process access token,
   * assign its copy for the current thread.
   */
  if (!change_token(lsass_token, &old_token))
    goto err_privilege_not_held;

  revert_to_old_token = TRUE;

  nt_status = MyNtSetInformationProcess(GetCurrentProcess(), ProcessUserModeIOPL, NULL, 0);
  if (nt_status == STATUS_PRIVILEGE_NOT_HELD)
    {
      /*
       * Now current thread is not using primary process token anymore
       * but is using custom access token. There is no need to revert
       * enabled Tcb privilege as the whole custom access token would
       * be reverted. So there is no need to setup revert method for
       * enabling privilege.
       */
      if (have_privilege(luid_tcb_privilege) ||
          !enable_privilege(luid_tcb_privilege, NULL, NULL))
        goto err_privilege_not_held;
      nt_status = MyNtSetInformationProcess(GetCurrentProcess(), ProcessUserModeIOPL, NULL, 0);
    }
  if (nt_status >= 0)
    goto verify;
  else if (nt_status == STATUS_NOT_IMPLEMENTED)
    goto err_not_implemented;
  else if (nt_status == STATUS_PRIVILEGE_NOT_HELD)
    goto err_privilege_not_held;
  else
    goto err_unknown;

verify:
  /*
   * Some Windows NT kernel versions (e.g. Windows 2003 x64) do not
   * implement ProcessUserModeIOPL syscall at all but incorrectly
   * returns success when it is called by user process. So always
   * after this call verify that IOPL is set to 3.
   */
  if (read_iopl() != 3)
    goto err_not_implemented;
  ret = TRUE;
  goto ret;

err_not_implemented:
  SetLastError(ERROR_INVALID_FUNCTION);
  ret = FALSE;
  goto ret;

err_privilege_not_held:
  SetLastError(ERROR_PRIVILEGE_NOT_HELD);
  ret = FALSE;
  goto ret;

err_unknown:
  SetLastError(ERROR_GEN_FAILURE);
  ret = FALSE;
  goto ret;

ret:
  if (revert_to_old_token)
    revert_to_token(old_token);

  if (impersonate_privilege_enabled)
    revert_privilege(luid_impersonate_privilege, revert_token_impersonate_privilege, revert_only_impersonate_privilege);

  if (lsass_token)
    CloseHandle(lsass_token);

  if (ntdll)
    FreeLibrary(ntdll);

  return ret;
}

static int
intel_setup_io(struct pci_access *a)
{
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

  /* On NT-based systems issue ProcessUserModeIOPL syscall which changes IOPL to 3. */
  if (!SetProcessUserModeIOPL())
    {
      DWORD error = GetLastError();
      a->debug("NT ProcessUserModeIOPL call failed: %s.", error == ERROR_INVALID_FUNCTION ? "Not Implemented" : error == ERROR_PRIVILEGE_NOT_HELD ? "Access Denied" : "Operation Failed");
      return 0;
    }

  a->debug("NT ProcessUserModeIOPL call succeeded...");
  return 1;
}

static inline void
intel_cleanup_io(struct pci_access *a UNUSED)
{
  /*
   * 16/32-bit non-NT systems do not use any special setup and on NT-based
   * systems ProcessUserModeIOPL permanently changes IOPL to 3 for the current
   * NT process, no revert for current process is possible.
   */
}

static inline void intel_io_lock(void)
{
}

static inline void intel_io_unlock(void)
{
}
