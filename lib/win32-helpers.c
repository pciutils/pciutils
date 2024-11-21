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

/* Unfortunately i586-mingw32msvc toolchain does not provide this constant. */
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

/* Unfortunately some toolchains do not provide this constant. */
#ifndef SE_IMPERSONATE_NAME
#define SE_IMPERSONATE_NAME TEXT("SeImpersonatePrivilege")
#endif

/* Unfortunately some toolchains do not provide these constants. */
#ifndef SE_DACL_AUTO_INHERIT_REQ
#define SE_DACL_AUTO_INHERIT_REQ 0x0100
#endif
#ifndef SE_SACL_AUTO_INHERIT_REQ
#define SE_SACL_AUTO_INHERIT_REQ 0x0200
#endif
#ifndef SE_DACL_AUTO_INHERITED
#define SE_DACL_AUTO_INHERITED 0x0400
#endif
#ifndef SE_SACL_AUTO_INHERITED
#define SE_SACL_AUTO_INHERITED 0x0800
#endif

/* Older SDK versions do not provide NtCurrentTeb symbol for X86, header files have only function declaration. */
#if defined(_MSC_VER) && defined(_M_IX86) && !defined(PcTeb)
#define PcTeb 0x18
#if _MSC_VER >= 1400
#pragma intrinsic(__readfsdword)
__inline struct _TEB *NtCurrentTeb(void) { return (struct _TEB *)__readfsdword(PcTeb); }
#else
__inline struct _TEB *NtCurrentTeb(void) { __asm mov eax, fs:[PcTeb] }
#endif
#endif

/* Offset to ULONG HardErrorMode field in TEB structure, it is architecture specific. */
#if defined(_M_IX86) || defined(__i386__)
#define TEB_HARD_ERROR_MODE_OFFSET 0x0F28
#elif defined(_M_AMD64) || defined(__x86_64__)
#define TEB_HARD_ERROR_MODE_OFFSET 0x16B0
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
 * These aclapi function is available in advapi.dll library on Windows 2000
 * and higher systems.
 */
typedef BOOL (WINAPI *SetSecurityDescriptorControlProt)(PSECURITY_DESCRIPTOR pSecurityDescriptor, SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest, SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet);

/*
 * This errhandlingapi function is available in kernel32.dll library on
 * Windows 7 and higher systems.
 */
typedef BOOL (WINAPI *SetThreadErrorModeProt)(DWORD dwNewMode, LPDWORD lpOldMode);


static DWORD
format_message_from_system(DWORD win32_error_id, DWORD lang_id, LPSTR buffer, DWORD size)
{
  return FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, win32_error_id, lang_id, buffer, size, NULL);
}

const char *
win32_strerror(DWORD win32_error_id)
{
  /*
   * Use static buffer which is large enough.
   * Hopefully no Win32 API error message string is longer than 4 kB.
   */
  static char buffer[4096];
  DWORD len;

  /*
   * If it is possible show error messages in US English language.
   * International Windows editions do not have to provide error
   * messages in English language, so fallback to the language
   * which system provides (neutral).
   */
  len = format_message_from_system(win32_error_id, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), buffer, sizeof(buffer));
  if (!len)
    len = format_message_from_system(win32_error_id, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer));

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

/*
 * Change error mode of the current thread. If it is not possible then change
 * error mode of the whole process. Always returns previous error mode.
 */
UINT
win32_change_error_mode(UINT new_mode)
{
  SetThreadErrorModeProt MySetThreadErrorMode = NULL;
  OSVERSIONINFOA version;
  HMODULE kernel32;
  HMODULE ntdll;
  DWORD old_mode;

  /*
   * Function SetThreadErrorMode() was introduced in Windows 7, so use
   * GetProcAddress() for compatibility with older systems.
   */
  kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
  if (kernel32)
    MySetThreadErrorMode = (SetThreadErrorModeProt)(void(*)(void))GetProcAddress(kernel32, "SetThreadErrorMode");

  /*
   * Function RtlSetThreadErrorMode() was introduced in Windows XP x64
   * and Windows Server 2003. Use GetProcAddress() as it is in ntdll.dll.
   */
  if (!MySetThreadErrorMode)
    {
      ntdll = GetModuleHandle(TEXT("ntdll.dll"));
      if (ntdll)
        MySetThreadErrorMode = (SetThreadErrorModeProt)(void(*)(void))GetProcAddress(ntdll, "RtlSetThreadErrorMode");
    }

  if (MySetThreadErrorMode &&
      MySetThreadErrorMode(new_mode, &old_mode))
    return old_mode;

#ifdef TEB_HARD_ERROR_MODE_OFFSET
  /*
   * On Windows NT 4.0+ systems fallback to thread HardErrorMode API.
   * It depends on architecture specific offset for HardErrorMode field in TEB.
   */
  version.dwOSVersionInfoSize = sizeof(version);
  if (GetVersionExA(&version) &&
      version.dwPlatformId == VER_PLATFORM_WIN32_NT &&
      version.dwMajorVersion >= 4)
    {
      ULONG *hard_error_mode_ptr = (ULONG *)((BYTE *)NtCurrentTeb() + TEB_HARD_ERROR_MODE_OFFSET);
      old_mode = *hard_error_mode_ptr;
      *hard_error_mode_ptr = new_mode;
      return old_mode;
    }
#endif

  /*
   * Fallback to function SetErrorMode() which modifies error mode of the
   * whole process and returns old mode.
   */
  return SetErrorMode(new_mode);
}

/*
 * Check if the current thread has particular privilege in current active access
 * token. Case when it not possible to determinate it (e.g. current thread does
 * not have permission to open its own current active access token) is evaluated
 * as thread does not have that privilege.
 */
BOOL
win32_have_privilege(LUID luid_privilege)
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
BOOL
win32_change_token(HANDLE new_token, HANDLE *old_token)
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
 * Use to revert after win32_change_token() or change_token_to_primary() call.
 */
VOID
win32_revert_to_token(HANDLE token)
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
BOOL
win32_enable_privilege(LUID luid_privilege, HANDLE *revert_token, BOOL *revert_only_privilege)
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
        win32_revert_to_token(thread_token);
      return FALSE;
    }

  if (!set_privilege(new_token, luid_privilege, TRUE))
    {
      CloseHandle(new_token);
      /* thread_token is set only when we were asked for revert method. */
      if (revert_token && revert_only_privilege)
        win32_revert_to_token(thread_token);
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
 * win32_enable_privilege() call. Either disable privilege in specified access token
 * or revert to previous access token.
 */
VOID
win32_revert_privilege(LUID luid_privilege, HANDLE revert_token, BOOL revert_only_privilege)
{
  if (revert_only_privilege)
    {
      set_privilege(revert_token, luid_privilege, FALSE);
      CloseHandle(revert_token);
    }
  else
    {
      win32_revert_to_token(revert_token);
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
 * Create a new security descriptor in absolute form from relative form.
 * Newly created security descriptor in absolute form is stored in linear buffer.
 */
static PSECURITY_DESCRIPTOR
create_relsd_from_abssd(PSECURITY_DESCRIPTOR rel_security_descriptor)
{
  PBYTE abs_security_descriptor_buffer;
  DWORD abs_security_descriptor_size=0, abs_dacl_size=0, abs_sacl_size=0, abs_owner_size=0, abs_primary_group_size=0;

  if (!MakeAbsoluteSD(rel_security_descriptor,
        NULL, &abs_security_descriptor_size,
        NULL, &abs_dacl_size,
        NULL, &abs_sacl_size,
        NULL, &abs_owner_size,
        NULL, &abs_primary_group_size) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return NULL;

  abs_security_descriptor_buffer = (PBYTE)LocalAlloc(LPTR, abs_security_descriptor_size+abs_dacl_size+abs_sacl_size+abs_owner_size+abs_primary_group_size);
  if (!abs_security_descriptor_buffer)
    return NULL;

  if (!MakeAbsoluteSD(rel_security_descriptor,
        (PSECURITY_DESCRIPTOR)abs_security_descriptor_buffer, &abs_security_descriptor_size,
        (PACL)(abs_security_descriptor_buffer+abs_security_descriptor_size), &abs_dacl_size,
        (PACL)(abs_security_descriptor_buffer+abs_security_descriptor_size+abs_dacl_size), &abs_sacl_size,
        (PSID)(abs_security_descriptor_buffer+abs_security_descriptor_size+abs_dacl_size+abs_sacl_size), &abs_owner_size,
        (PSID)(abs_security_descriptor_buffer+abs_security_descriptor_size+abs_dacl_size+abs_sacl_size+abs_owner_size), &abs_primary_group_size))
    return NULL;

  return (PSECURITY_DESCRIPTOR)abs_security_descriptor_buffer;
}

/*
 * Prepare security descriptor obtained by GetKernelObjectSecurity() so it can be
 * passed to SetKernelObjectSecurity() as identity operation. It modifies control
 * flags of security descriptor, which is needed for Windows 2000 and new.
 */
static BOOL
prepare_security_descriptor_for_set_operation(PSECURITY_DESCRIPTOR security_descriptor)
{
  SetSecurityDescriptorControlProt MySetSecurityDescriptorControl;
  SECURITY_DESCRIPTOR_CONTROL bits_mask;
  SECURITY_DESCRIPTOR_CONTROL bits_set;
  SECURITY_DESCRIPTOR_CONTROL control;
  OSVERSIONINFO version;
  HMODULE advapi32;
  DWORD revision;

  /*
   * SE_DACL_AUTO_INHERITED and SE_SACL_AUTO_INHERITED are flags introduced in
   * Windows 2000 to control client-side automatic inheritance (client - user
   * process - is responsible for propagating inherited ACEs to subobjects).
   * To prevent applications which do not understand client-side automatic
   * inheritance (applications created prior Windows 2000 or which use low
   * level API like SetKernelObjectSecurity()) to unintentionally set those
   * SE_DACL_AUTO_INHERITED and SE_SACL_AUTO_INHERITED control flags when
   * coping them from other security descriptor.
   *
   * As we are not modifying existing ACEs, we are compatible with Windows 2000
   * client-side automatic inheritance model and therefore prepare security
   * descriptor for SetKernelObjectSecurity() to not clear existing automatic
   * inheritance control flags.
   *
   * Control flags SE_DACL_AUTO_INHERITED and SE_SACL_AUTO_INHERITED are set
   * into security object only when they are set together with set-only flags
   * SE_DACL_AUTO_INHERIT_REQ and SE_SACL_AUTO_INHERIT_REQ. Those flags are
   * never received by GetKernelObjectSecurity() and are just commands for
   * SetKernelObjectSecurity() how to interpret SE_DACL_AUTO_INHERITED and
   * SE_SACL_AUTO_INHERITED flags.
   *
   * Function symbol SetSecurityDescriptorControl is not available in the
   * older versions of advapi32.dll library, so resolve it at runtime.
   */

  version.dwOSVersionInfoSize = sizeof(version);
  if (!GetVersionEx(&version) ||
      version.dwPlatformId != VER_PLATFORM_WIN32_NT ||
      version.dwMajorVersion < 5)
    return TRUE;

  if (!GetSecurityDescriptorControl(security_descriptor, &control, &revision))
    return FALSE;

  bits_mask = 0;
  bits_set = 0;

  if (control & SE_DACL_AUTO_INHERITED)
    {
      bits_mask |= SE_DACL_AUTO_INHERIT_REQ;
      bits_set |= SE_DACL_AUTO_INHERIT_REQ;
    }

  if (control & SE_SACL_AUTO_INHERITED)
    {
      bits_mask |= SE_SACL_AUTO_INHERIT_REQ;
      bits_set |= SE_SACL_AUTO_INHERIT_REQ;
    }

  if (!bits_mask)
    return TRUE;

  advapi32 = GetModuleHandle(TEXT("advapi32.dll"));
  if (!advapi32)
    return FALSE;

  MySetSecurityDescriptorControl = (SetSecurityDescriptorControlProt)(void(*)(void))GetProcAddress(advapi32, "SetSecurityDescriptorControl");
  if (!MySetSecurityDescriptorControl)
    return FALSE;

  if (!MySetSecurityDescriptorControl(security_descriptor, bits_mask, bits_set))
    return FALSE;

  return TRUE;
}

/*
 * Grant particular permissions in the primary access token of the specified
 * process for the owner of current thread token and set old DACL of the
 * process access token for reverting permissions. Security descriptor is
 * just memory buffer for old DACL.
 */
static BOOL
grant_process_token_dacl_permissions(HANDLE process, DWORD permissions, HANDLE *token, PSECURITY_DESCRIPTOR *old_security_descriptor)
{
  TOKEN_OWNER *owner;
  PACL old_dacl;
  BOOL old_dacl_present;
  BOOL old_dacl_defaulted;
  PACL new_dacl;
  WORD new_dacl_size;
  PSECURITY_DESCRIPTOR new_security_descriptor;
  DWORD length;

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

  if (!GetKernelObjectSecurity(*token, DACL_SECURITY_INFORMATION, NULL, 0, &length) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

retry:
  *old_security_descriptor = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, length);
  if (!*old_security_descriptor)
    {
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  if (!GetKernelObjectSecurity(*token, DACL_SECURITY_INFORMATION, *old_security_descriptor, length, &length))
    {
      /*
       * Length of the security descriptor between two get calls
       * may changes (e.g. by another thread of process), so retry.
       */
      if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
          LocalFree(*old_security_descriptor);
          goto retry;
        }
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  if (!prepare_security_descriptor_for_set_operation(*old_security_descriptor))
    {
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  /* Retrieve the current DACL from security descriptor including present and defaulted properties. */
  if (!GetSecurityDescriptorDacl(*old_security_descriptor, &old_dacl_present, &old_dacl, &old_dacl_defaulted))
    {
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  /*
   * If DACL is not present then system grants full access to everyone. It this
   * case do not modify DACL as it just adds one ACL allow rule for us, which
   * automatically disallow access to anybody else which had access before.
   */
  if (!old_dacl_present || !old_dacl)
    {
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      *old_security_descriptor = NULL;
      return TRUE;
    }

  /* Create new DACL which would be copy of the current old one. */
  new_dacl_size = old_dacl->AclSize + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(owner->Owner) - sizeof(DWORD);
  new_dacl = (PACL)LocalAlloc(LPTR, new_dacl_size);
  if (!new_dacl)
    {
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  /*
   * Initialize new DACL structure to the same format as was the old one.
   * Set new explicit access for the owner of the current thread access
   * token with non-inherited granting access to specified permissions.
   * This permission is added in the first ACE, so has the highest priority.
   */
  if (!InitializeAcl(new_dacl, new_dacl_size, old_dacl->AclRevision) ||
      !AddAccessAllowedAce(new_dacl, ACL_REVISION2, permissions, owner->Owner))
    {
      LocalFree(new_dacl);
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  /*
   * Now (after setting our new permissions) append all ACE entries from the
   * old DACL to the new DACL, which preserve all other existing permissions.
   */
  if (old_dacl->AceCount > 0)
    {
      WORD ace_index;
      LPVOID ace;

      for (ace_index = 0; ace_index < old_dacl->AceCount; ace_index++)
        {
          if (!GetAce(old_dacl, ace_index, &ace) ||
              !AddAce(new_dacl, old_dacl->AclRevision, MAXDWORD, ace, ((PACE_HEADER)ace)->AceSize))
            {
              LocalFree(new_dacl);
              LocalFree(*old_security_descriptor);
              LocalFree(owner);
              CloseHandle(*token);
              return FALSE;
            }
        }
    }

  /*
   * Create copy of the old security descriptor, so we can modify its DACL.
   * Function SetSecurityDescriptorDacl() works only with security descriptors
   * in absolute format. So use our helper function create_relsd_from_abssd()
   * for converting security descriptor from relative format (which is returned
   * by GetKernelObjectSecurity() function) to the absolute format.
   */
  new_security_descriptor = create_relsd_from_abssd(*old_security_descriptor);
  if (!new_security_descriptor)
    {
      LocalFree(new_dacl);
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  /*
   * In the new security descriptor replace old DACL by the new DACL (which has
   * new permissions) and then set this new security descriptor to the token,
   * so token would have new access permissions.
   */
  if (!SetSecurityDescriptorDacl(new_security_descriptor, TRUE, new_dacl, FALSE) ||
      !SetKernelObjectSecurity(*token, DACL_SECURITY_INFORMATION, new_security_descriptor))
    {
      LocalFree(new_security_descriptor);
      LocalFree(new_dacl);
      LocalFree(*old_security_descriptor);
      LocalFree(owner);
      CloseHandle(*token);
      return FALSE;
    }

  LocalFree(new_security_descriptor);
  LocalFree(new_dacl);
  LocalFree(owner);
  return TRUE;
}

/*
 * Revert particular granted permissions in specified access token done by
 * grant_process_token_dacl_permissions() call.
 */
static VOID
revert_token_dacl_permissions(HANDLE token, PSECURITY_DESCRIPTOR old_security_descriptor)
{
  SetKernelObjectSecurity(token, DACL_SECURITY_INFORMATION, old_security_descriptor);
  LocalFree(old_security_descriptor);
  CloseHandle(token);
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

  if (!win32_enable_privilege(luid_debug_privilege, &revert_token, &revert_only_privilege))
    return NULL;

  process = OpenProcess(process_right, FALSE, pid);

  win32_revert_privilege(luid_debug_privilege, revert_token, revert_only_privilege);

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
HANDLE
win32_find_and_open_process_for_query(LPCSTR exe_file)
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
  MyGetProcessImageFileNameW = (GetProcessImageFileNameWProt)(void(*)(void))GetProcAddress(kernel32, "K32GetProcessImageFileNameW");
  MyEnumProcesses = (EnumProcessesProt)(void(*)(void))GetProcAddress(kernel32, "K32EnumProcesses");
  if (!MyGetProcessImageFileNameW || !MyEnumProcesses)
    {
      /*
       * On older NT-based systems these functions are available in
       * psapi.dll library without K32 prefix.
       */
      prev_error_mode = win32_change_error_mode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
      psapi = LoadLibrary(TEXT("psapi.dll"));
      win32_change_error_mode(prev_error_mode);

      if (!psapi)
        return NULL;

      /*
       * Function GetProcessImageFileNameW() is available in
       * Windows XP and higher systems. On older versions is
       * available function GetModuleFileNameExW().
       */
      MyGetProcessImageFileNameW = (GetProcessImageFileNameWProt)(void(*)(void))GetProcAddress(psapi, "GetProcessImageFileNameW");
      MyGetModuleFileNameExW = (GetModuleFileNameExWProt)(void(*)(void))GetProcAddress(psapi, "GetModuleFileNameExW");
      MyEnumProcesses = (EnumProcessesProt)(void(*)(void))GetProcAddress(psapi, "EnumProcesses");
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
  PSECURITY_DESCRIPTOR old_security_descriptor;
  HANDLE grant_token;
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
      if (!grant_process_token_dacl_permissions(process, rights, &grant_token, &old_security_descriptor))
        return NULL;
      if (!OpenProcessToken(process, rights, &token))
        {
          token = NULL;
          error = GetLastError();
        }
      if (old_security_descriptor)
        revert_token_dacl_permissions(grant_token, old_security_descriptor);
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
HANDLE
win32_open_process_token_with_rights(HANDLE process, DWORD rights)
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
      win32_revert_to_token(old_token);
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
      win32_revert_to_token(old_token);
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
 * Call supplied function with its argument and if it fails with
 * ERROR_PRIVILEGE_NOT_HELD then try to enable Tcb privilege and
 * call function with its argument again.
 */
BOOL
win32_call_func_with_tcb_privilege(BOOL (*function)(LPVOID), LPVOID argument)
{
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

  DWORD error;
  BOOL ret;

  impersonate_privilege_enabled = FALSE;
  revert_to_old_token = FALSE;
  lsass_token = NULL;
  old_token = NULL;

  /* Call supplied function. */
  ret = function(argument);
  if (ret || GetLastError() != ERROR_PRIVILEGE_NOT_HELD)
    goto ret;

  /*
   * If function call failed with ERROR_PRIVILEGE_NOT_HELD
   * error then it means that the current thread token does not have
   * Tcb privilege enabled. Try to enable it.
   */

  if (!LookupPrivilegeValue(NULL, SE_TCB_NAME, &luid_tcb_privilege))
    goto err_privilege_not_held;

  /*
   * If the current thread has already Tcb privilege enabled then there
   * is some additional unhanded restriction.
   */
  if (win32_have_privilege(luid_tcb_privilege))
    goto err_privilege_not_held;

  /* Try to enable Tcb privilege and try function call again. */
  if (win32_enable_privilege(luid_tcb_privilege, &revert_token_tcb_privilege, &revert_only_tcb_privilege))
    {
      ret = function(argument);
      win32_revert_privilege(luid_tcb_privilege, revert_token_tcb_privilege, revert_only_tcb_privilege);
      goto ret;
    }

  /*
   * If enabling of Tcb privilege failed then it means that current thread
   * does not have this privilege. But current process may have it. So try it
   * again with primary process access token.
   */

  /*
   * If system supports Impersonate privilege (Windows 2000 SP4 or higher) then
   * all future actions in this function require this Impersonate privilege.
   * So try to enable it in case it is currently disabled.
   */
  if (LookupPrivilegeValue(NULL, SE_IMPERSONATE_NAME, &luid_impersonate_privilege) &&
      !win32_have_privilege(luid_impersonate_privilege))
    {
      /*
       * If current thread does not have Impersonate privilege enabled
       * then first try to enable it just for the current thread. If
       * it is not possible to enable it just for the current thread
       * then try it to enable globally for whole process (which
       * affects all process threads). Both actions will be reverted
       * at the end of this function.
       */
      if (win32_enable_privilege(luid_impersonate_privilege, &revert_token_impersonate_privilege, &revert_only_impersonate_privilege))
        {
          impersonate_privilege_enabled = TRUE;
        }
      else if (win32_enable_privilege(luid_impersonate_privilege, NULL, NULL))
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
      if (win32_enable_privilege(luid_tcb_privilege, &revert_token_tcb_privilege, &revert_only_tcb_privilege))
        {
          ret = function(argument);
          win32_revert_privilege(luid_tcb_privilege, revert_token_tcb_privilege, revert_only_tcb_privilege);
          goto ret;
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

  lsass_process = win32_find_and_open_process_for_query("lsass.exe");
  if (!lsass_process)
    goto err_privilege_not_held;

  /*
   * Open primary lsass.exe process access token with query and duplicate
   * rights. Just these two rights are required for impersonating other
   * primary process token (impersonate right is really not required!).
   */
  lsass_token = win32_open_process_token_with_rights(lsass_process, TOKEN_QUERY | TOKEN_DUPLICATE);

  CloseHandle(lsass_process);

  if (!lsass_token)
    goto err_privilege_not_held;

  /*
   * After successful open of the primary lsass.exe process access token,
   * assign its copy for the current thread.
   */
  if (!win32_change_token(lsass_token, &old_token))
    goto err_privilege_not_held;

  revert_to_old_token = TRUE;

  ret = function(argument);
  if (ret || GetLastError() != ERROR_PRIVILEGE_NOT_HELD)
    goto ret;

  /*
   * Now current thread is not using primary process token anymore
   * but is using custom access token. There is no need to revert
   * enabled Tcb privilege as the whole custom access token would
   * be reverted. So there is no need to setup revert method for
   * enabling privilege.
   */
  if (win32_have_privilege(luid_tcb_privilege) ||
      !win32_enable_privilege(luid_tcb_privilege, NULL, NULL))
    goto err_privilege_not_held;

  ret = function(argument);
  goto ret;

err_privilege_not_held:
  SetLastError(ERROR_PRIVILEGE_NOT_HELD);
  ret = FALSE;
  goto ret;

ret:
  error = GetLastError();

  if (revert_to_old_token)
    win32_revert_to_token(old_token);

  if (impersonate_privilege_enabled)
    win32_revert_privilege(luid_impersonate_privilege, revert_token_impersonate_privilege, revert_only_impersonate_privilege);

  if (lsass_token)
    CloseHandle(lsass_token);

  SetLastError(error);

  return ret;
}
