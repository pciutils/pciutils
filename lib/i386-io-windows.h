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
#include "win32-helpers.h"

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
 *
 * CAVEAT: Semicolon in MSVC __asm block means start of the comment, and not
 * end of the __asm statement, like it is for all other C statements. Also
 * function which uses MSVC inline assembly cannot be inlined to another function
 * (compiler reports a warning about it, not a fatal error). So we add explicit
 * curly brackets for __asm blocks, remove misleading semicolons and do not
 * declare functions as inline.
 */
#if defined(_MSC_VER) && (_MSC_VER >= 1500 || (_MSC_VER >= 1400 && defined(__BUILDMACHINE__)))
#pragma intrinsic(__readeflags)
#elif defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 9) || (__GNUC__ > 4))
#include <x86intrin.h>
#elif defined(_MSC_VER) && defined(_M_IX86)
static unsigned int
__readeflags(void)
{
  __asm {
    pushfd
    pop eax
  }
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

/* Unfortunately some toolchains do not provide this constant. */
#ifndef SE_IMPERSONATE_NAME
#define SE_IMPERSONATE_NAME TEXT("SeImpersonatePrivilege")
#endif

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
typedef ULONG (NTAPI *RtlNtStatusToDosErrorProt)(NTSTATUS Status);

/*
 * Call supplied function Func with its Arg and if it fails with
 * ERROR_PRIVILEGE_NOT_HELD then try to enable Tcb privilege and
 * call Func with its Arg again.
 */
static BOOL
CallFuncWithTcbPrivilege(BOOL (*Func)(LPVOID), LPVOID Arg)
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

  BOOL ret;

  impersonate_privilege_enabled = FALSE;
  revert_to_old_token = FALSE;
  lsass_token = NULL;
  old_token = NULL;

  /* Call supplied function. */
  ret = Func(Arg);
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
      ret = Func(Arg);
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
          ret = Func(Arg);
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

  ret = Func(Arg);
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

  ret = Func(Arg);
  goto ret;

err_privilege_not_held:
  SetLastError(ERROR_PRIVILEGE_NOT_HELD);
  ret = FALSE;
  goto ret;

ret:
  if (revert_to_old_token)
    win32_revert_to_token(old_token);

  if (impersonate_privilege_enabled)
    win32_revert_privilege(luid_impersonate_privilege, revert_token_impersonate_privilege, revert_only_impersonate_privilege);

  if (lsass_token)
    CloseHandle(lsass_token);

  return ret;
}

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
static BOOL
SetProcessUserModeIOPLFunc(LPVOID Arg)
{
  RtlNtStatusToDosErrorProt RtlNtStatusToDosErrorPtr = (RtlNtStatusToDosErrorProt)(((LPVOID *)Arg)[1]);
  NtSetInformationProcessProt NtSetInformationProcessPtr = (NtSetInformationProcessProt)(((LPVOID *)Arg)[0]);
  NTSTATUS nt_status = NtSetInformationProcessPtr(GetCurrentProcess(), ProcessUserModeIOPL, NULL, 0);
  if (nt_status >= 0)
    return TRUE;

  /*
   * If we have optional RtlNtStatusToDosError() function then use it for
   * translating NT status to Win32 error. If we do not have it then translate
   * two important status codes which we use later STATUS_NOT_IMPLEMENTED and
   * STATUS_PRIVILEGE_NOT_HELD.
   */
  if (RtlNtStatusToDosErrorPtr)
    SetLastError(RtlNtStatusToDosErrorPtr(nt_status));
  else if (nt_status == STATUS_NOT_IMPLEMENTED)
    SetLastError(ERROR_INVALID_FUNCTION);
  else if (nt_status == STATUS_PRIVILEGE_NOT_HELD)
    SetLastError(ERROR_PRIVILEGE_NOT_HELD);
  else
    SetLastError(ERROR_GEN_FAILURE);

  return FALSE;
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
  LPVOID Arg[2];
  UINT prev_error_mode;
  HMODULE ntdll;
  BOOL ret;

  /*
   * Load ntdll.dll library with disabled critical-error-handler message box.
   * It means that NT kernel does not show unwanted GUI message box to user
   * when LoadLibrary() function fails.
   */
  prev_error_mode = win32_change_error_mode(SEM_FAILCRITICALERRORS);
  ntdll = LoadLibrary(TEXT("ntdll.dll"));
  win32_change_error_mode(prev_error_mode);
  if (!ntdll)
    {
      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }

  /* Retrieve pointer to NtSetInformationProcess() function. */
  Arg[0] = (LPVOID)GetProcAddress(ntdll, "NtSetInformationProcess");
  if (!Arg[0])
    {
      FreeLibrary(ntdll);
      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }

  /* Retrieve pointer to optional RtlNtStatusToDosError() function, it may be NULL. */
  Arg[1] = (LPVOID)GetProcAddress(ntdll, "RtlNtStatusToDosError");

  /* Call ProcessUserModeIOPL with Tcb privilege. */
  ret = CallFuncWithTcbPrivilege(SetProcessUserModeIOPLFunc, (LPVOID)&Arg);

  FreeLibrary(ntdll);

  if (!ret)
    return FALSE;

  /*
   * Some Windows NT kernel versions (e.g. Windows 2003 x64) do not
   * implement ProcessUserModeIOPL syscall at all but incorrectly
   * returns success when it is called by user process. So always
   * after this call verify that IOPL is set to 3.
   */
  if (read_iopl() != 3)
    {
      SetLastError(ERROR_INVALID_FUNCTION);
      return FALSE;
    }

  return TRUE;
}

static int
intel_setup_io(struct pci_access *a)
{
#ifndef _WIN64
  /* 16/32-bit non-NT systems allow applications to access PCI I/O ports without any special setup. */
  if (win32_is_non_nt_system())
    {
      a->debug("Detected 16/32-bit non-NT system, skipping NT setup...");
      return 1;
    }
#endif

  /* Check if we have I/O permission */
  if (read_iopl() == 3)
    {
      a->debug("IOPL is already set to 3, skipping NT setup...");
      return 1;
    }

  /* On NT-based systems issue ProcessUserModeIOPL syscall which changes IOPL to 3. */
  if (!SetProcessUserModeIOPL())
    {
      DWORD error = GetLastError();
      a->debug("NT ProcessUserModeIOPL call failed: %s.", error == ERROR_INVALID_FUNCTION ? "Call is not supported" : win32_strerror(error));
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
