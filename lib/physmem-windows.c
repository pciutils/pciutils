/*
 *      The PCI Library -- Physical memory mapping for Windows systems
 *
 *      Copyright (c) 2023 Pali Rohár <pali@kernel.org>
 *
 *      Can be freely distributed and used under the terms of the GNU GPL v2+
 *
 *      SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "internal.h"

#include <windows.h>
#include <errno.h>
#include <stdlib.h>

#include "physmem.h"
#include "win32-helpers.h"

#ifndef NTSTATUS
#define NTSTATUS LONG
#endif
#ifndef STATUS_INVALID_HANDLE
#define STATUS_INVALID_HANDLE ((NTSTATUS)0xC0000008)
#endif
#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000D)
#endif
#ifndef STATUS_CONFLICTING_ADDRESSES
#define STATUS_CONFLICTING_ADDRESSES ((NTSTATUS)0xC0000018)
#endif
#ifndef STATUS_NOT_MAPPED_VIEW
#define STATUS_NOT_MAPPED_VIEW ((NTSTATUS)0xC0000019)
#endif
#ifndef STATUS_INVALID_VIEW_SIZE
#define STATUS_INVALID_VIEW_SIZE ((NTSTATUS)0xC000001F)
#endif
#ifndef STATUS_ACCESS_DENIED
#define STATUS_ACCESS_DENIED ((NTSTATUS)0xC0000022)
#endif
#ifndef STATUS_OBJECT_NAME_NOT_FOUND
#define STATUS_OBJECT_NAME_NOT_FOUND ((NTSTATUS)0xC0000034)
#endif
#ifndef STATUS_INVALID_PAGE_PROTECTION
#define STATUS_INVALID_PAGE_PROTECTION ((NTSTATUS)0xC0000045)
#endif
#ifndef STATUS_SECTION_PROTECTION
#define STATUS_SECTION_PROTECTION ((NTSTATUS)0xC000004E)
#endif
#ifndef STATUS_INSUFFICIENT_RESOURCES
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009A)
#endif
#ifndef STATUS_INVALID_PARAMETER_3
#define STATUS_INVALID_PARAMETER_3 ((NTSTATUS)0xC00000F1)
#endif
#ifndef STATUS_INVALID_PARAMETER_4
#define STATUS_INVALID_PARAMETER_4 ((NTSTATUS)0xC00000F2)
#endif
#ifndef STATUS_INVALID_PARAMETER_5
#define STATUS_INVALID_PARAMETER_5 ((NTSTATUS)0xC00000F3)
#endif
#ifndef STATUS_INVALID_PARAMETER_8
#define STATUS_INVALID_PARAMETER_8 ((NTSTATUS)0xC00000F6)
#endif
#ifndef STATUS_INVALID_PARAMETER_9
#define STATUS_INVALID_PARAMETER_9 ((NTSTATUS)0xC00000F7)
#endif
#ifndef STATUS_MAPPED_ALIGNMENT
#define STATUS_MAPPED_ALIGNMENT ((NTSTATUS)0xC0000220)
#endif

#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x00000040L
#endif

#ifndef SECTION_INHERIT
#define SECTION_INHERIT ULONG
#endif
#ifndef ViewUnmap
#define ViewUnmap ((SECTION_INHERIT)2)
#endif

#ifndef IMAGE_NT_OPTIONAL_HDR_MAGIC
#ifdef _WIN64
#define IMAGE_NT_OPTIONAL_HDR_MAGIC 0x20b
#else
#define IMAGE_NT_OPTIONAL_HDR_MAGIC 0x10b
#endif
#endif

#ifndef EOVERFLOW
#define EOVERFLOW 132
#endif

#if _WIN32_WINNT < 0x0500
typedef ULONG ULONG_PTR;
typedef ULONG_PTR SIZE_T, *PSIZE_T;
#endif

#ifndef __UNICODE_STRING_DEFINED
#define __UNICODE_STRING_DEFINED
typedef struct _UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} UNICODE_STRING, *PUNICODE_STRING;
#endif

#ifndef __OBJECT_ATTRIBUTES_DEFINED
#define __OBJECT_ATTRIBUTES_DEFINED
typedef struct _OBJECT_ATTRIBUTES {
  ULONG Length;
  HANDLE RootDirectory;
  PUNICODE_STRING ObjectName;
  ULONG Attributes;
  PVOID SecurityDescriptor;
  PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;
#endif

#ifndef InitializeObjectAttributes
#define InitializeObjectAttributes(p, n, a, r, s) \
{ \
  (p)->Length = sizeof(OBJECT_ATTRIBUTES); \
  (p)->RootDirectory = (r); \
  (p)->Attributes = (a); \
  (p)->ObjectName = (n); \
  (p)->SecurityDescriptor = (s); \
  (p)->SecurityQualityOfService = NULL; \
}
#endif

#ifndef RtlInitUnicodeString
#define RtlInitUnicodeString(d, s) \
{ \
  (d)->Length = wcslen(s) * sizeof(WCHAR); \
  (d)->MaximumLength = (d)->Length + sizeof(WCHAR); \
  (d)->Buffer = (PWCHAR)(s); \
}
#endif

#define VWIN32_DEVICE_ID 0x002A /* from vmm.h */
#define WIN32_SERVICE_ID(device, function) (((device) << 16) | (function))
#define VWIN32_Int31Dispatch WIN32_SERVICE_ID(VWIN32_DEVICE_ID, 0x29)
#define DPMI_PHYSICAL_ADDRESS_MAPPING 0x0800

struct physmem {
  HANDLE section_handle;
  NTSTATUS (NTAPI *NtOpenSection)(PHANDLE SectionHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes);
  NTSTATUS (NTAPI *NtMapViewOfSection)(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress, ULONG_PTR ZeroBits, SIZE_T CommitSize, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, SECTION_INHERIT InheritDisposition, ULONG AllocationType, ULONG Win32Protect);
  NTSTATUS (NTAPI *NtUnmapViewOfSection)(HANDLE ProcessHandle, PVOID BaseAddress);
  ULONG (NTAPI *RtlNtStatusToDosError)(NTSTATUS Status);
#if defined(__i386__) || defined(_M_IX86)
  DWORD (WINAPI *VxDCall2)(DWORD Service, DWORD Arg1, DWORD Arg2);
  LPVOID w32skrnl_dpmi_lcall_ptr;
  DWORD base_addr_offset;
#endif
};

#if defined(__i386__) || defined(_M_IX86)

static BOOL
w32skrnl_physical_address_mapping(struct physmem *physmem, DWORD phys_addr, DWORD size, DWORD *virt_addr)
{
  DWORD address_hi = phys_addr >> 16;
  DWORD address_lo = phys_addr & 0xffff;
  DWORD size_hi = size >> 16;
  DWORD size_lo = size & 0xffff;
  BYTE failed;

  /*
   * Physical address mapping via w32skrnl.dll on Windows maps physical memory
   * and translates it to the virtual space of the current process memory.
   * Works only for aligned address / length and first 1 MB cannot be mapped
   * by this method. Expect that first 1 MB is already 1:1 mapped by the OS.
   * So accept request for physical memory range which is whole below 1 MB
   * without error and return virtual address same as the physical one.
   */
  if (phys_addr < 1*1024*1024UL)
    {
      if ((u64)phys_addr + size > 1*1024*1024UL)
        {
          errno = ENXIO;
          return FALSE;
        }

      *virt_addr = phys_addr;
      return TRUE;
    }

  /*
   * Unfortunately w32skrnl.dll provides only 48-bit fword pointer to physical
   * address mapping function and such pointer type is not supported by GCC,
   * nor by MSVC and therefore it is not possible call this function in C code.
   * So call this function with all parameters passed via inline assembly.
   */
#if defined(__GNUC__)
  asm volatile (
    "stc\n\t"
    "lcall *(%3)\n\t"
    "setc %0\n\t"
      : "=qm" (failed), "+b" (address_hi), "+c" (address_lo)
      : "r" (physmem->w32skrnl_dpmi_lcall_ptr), "a" (DPMI_PHYSICAL_ADDRESS_MAPPING), "S" (size_hi), "D" (size_lo)
      : "cc", "memory"
  );
#elif defined(_MSC_VER)
  __asm {
    mov esi, size_hi
    mov edi, size_lo
    mov ebx, address_hi
    mov ecx, address_lo
    mov eax, DPMI_PHYSICAL_ADDRESS_MAPPING
    stc
    mov edx, physmem
    mov edx, [edx]physmem.w32skrnl_dpmi_lcall_ptr
    call fword ptr [edx]
    setc failed
    mov address_hi, ebx
    mov address_lo, ecx
  }
#else
#error "Unsupported compiler"
#endif

  /*
   * Windows does not provide any error code when this function call fails.
   * So set errno just to the generic EACCES value.
   */
  if (failed)
    {
      errno = EACCES;
      return FALSE;
    }

  *virt_addr = ((address_hi & 0xffff) << 16) | (address_lo & 0xffff);
  return TRUE;
}

#if defined(__GNUC__) && ((__GNUC__ == 4 && __GNUC_MINOR__ >= 8) || (__GNUC__ > 4)) && (__GNUC__ <= 11)
/*
 * GCC versions 4.8 - 11 are buggy and throw error "'asm' operand has impossible
 * constraints" for inline assembly when optimizations (O1+) are enabled. So for
 * these GCC versions disable buggy optimizations by enforcing O0 optimize flag
 * affecting just this one function.
 */
__attribute__((optimize("O0")))
#endif
static BOOL
vdxcall_physical_address_mapping(struct physmem *physmem, DWORD phys_addr, DWORD size, DWORD *virt_addr)
{
  DWORD address_hi = phys_addr >> 16;
  DWORD address_lo = phys_addr & 0xffff;
  DWORD size_hi = size >> 16;
  DWORD size_lo = size & 0xffff;
  BYTE failed;

  /*
   * Physical address mapping via VxDCall2() on Windows maps physical memory
   * and translates it to the virtual space of the current process memory.
   * There are no restrictions for aligning or physical address ranges.
   * Works with any (unaligned) address or length, including low 1MB range.
   */

  /*
   * Function VxDCall2() has strange calling convention. First 3 arguments are
   * passed on stack, which callee pops (same as stdcall convention) but rest
   * of the function arguments are passed in ESI, EDI and EBX registers. And
   * return value is in carry flag (CF) and AX, BX and CX registers. GCC and
   * neither MSVC do not support this strange calling convention, so call this
   * function via inline assembly.
   *
   * Pseudocode with stdcall calling convention of that function looks like:
   * ESI = size_hi
   * EDI = size_lo
   * EBX = address_hi
   * VxDCall2(VWIN32_Int31Dispatch, DPMI_PHYSICAL_ADDRESS_MAPPING, address_lo)
   * failed = CF
   * address_hi = BX (if not failed)
   * address_lo = CX (if not failed)
   */

#if defined(__GNUC__)
  asm volatile (
    "pushl %6\n\t"
    "pushl %5\n\t"
    "pushl %4\n\t"
    "stc\n\t"
    "call *%P3\n\t"
    "setc %0\n\t"
      : "=qm" (failed), "+b" (address_hi), "=c" (address_lo)
      : "rmi" (physmem->VxDCall2), "rmi" (VWIN32_Int31Dispatch), "rmi" (DPMI_PHYSICAL_ADDRESS_MAPPING),
        "rmi" (address_lo), "S" (size_hi), "D" (size_lo)
/* Specify all call clobbered scratch registers for stdcall calling convention. */
      : "eax", "edx",
/*
 * Since GCC version 4.9.0 it is possible to specify x87 registers as clobbering
 * if they are not disabled by -mno-80387 switch (which defines _SOFT_FLOAT).
 */
#if ((__GNUC__ == 4 && __GNUC_MINOR__ >= 9) || (__GNUC__ > 4)) && !defined(_SOFT_FLOAT)
        "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)",
#endif
#ifdef __MMX__
        "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm6",
#endif
#ifdef __SSE__
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
#endif
        "cc", "memory"
  );
#elif defined(_MSC_VER)
  __asm {
    mov esi, size_hi
    mov edi, size_lo
    mov ebx, address_hi
    push address_lo
    push DPMI_PHYSICAL_ADDRESS_MAPPING
    push VWIN32_Int31Dispatch
    stc
    mov eax, physmem
    call [eax]physmem.VxDCall2
    setc failed
    mov address_hi, ebx
    mov address_lo, ecx
  }
#else
#error "Unsupported compiler"
#endif

  /*
   * Windows does not provide any error code when this function call fails.
   * So set errno just to the generic EACCES value.
   */
  if (failed)
    {
      errno = EACCES;
      return FALSE;
    }

  *virt_addr = ((address_hi & 0xffff) << 16) | (address_lo & 0xffff);
  return TRUE;
}

static BOOL
win32_get_physmem_offset(DWORD *offset)
{
  WORD DSsel;
  LDT_ENTRY DSentry;

  /*
   * Read DS selector. For this purpose there is WinAPI function and when called
   * as GetThreadContext(GetCurrentThread(), ...) with CONTEXT_SEGMENTS param,
   * it fills SegDs value. But on some Windows versions, GetThreadContext() can
   * be called only for threads attached to debugger. Hence we cannot use it for
   * our current thread. So instead read DS selector directly from ds register
   * via inline assembly code.
   */
#if defined(__GNUC__)
  asm ("movw %%ds, %w0" : "=rm" (DSsel));
#elif defined(_MSC_VER)
  __asm { mov DSsel, ds }
#else
#error "Unsupported compiler"
#endif

  if (!GetThreadSelectorEntry(GetCurrentThread(), DSsel, &DSentry))
    return FALSE;

  *offset = DSentry.BaseLow | (DSentry.HighWord.Bytes.BaseMid << 0x10) | (DSentry.HighWord.Bytes.BaseHi << 0x18);
  return TRUE;
}

static BYTE *
win32_get_baseaddr_from_hmodule(HMODULE module)
{
  WORD (WINAPI *ImteFromHModule)(HMODULE);
  BYTE *(WINAPI *BaseAddrFromImte)(WORD);
  HMODULE w32skrnl;
  WORD imte;

  if ((GetVersion() & 0xC0000000) != 0x80000000)
    return (BYTE *)module;

  w32skrnl = GetModuleHandleA("w32skrnl.dll");
  if (!w32skrnl)
    return NULL;

  ImteFromHModule = (LPVOID)GetProcAddress(w32skrnl, "_ImteFromHModule@4");
  BaseAddrFromImte = (LPVOID)GetProcAddress(w32skrnl, "_BaseAddrFromImte@4");
  if (!ImteFromHModule || !BaseAddrFromImte)
    return NULL;

  imte = ImteFromHModule(module);
  if (imte == 0xffff)
    return NULL;

  return BaseAddrFromImte(imte);
}

static FARPROC
win32_get_proc_address_by_ordinal(HMODULE module, DWORD ordinal, BOOL must_be_without_name)
{
  BYTE *baseaddr;
  IMAGE_DOS_HEADER *dos_header;
  IMAGE_NT_HEADERS *nt_header;
  DWORD export_dir_offset, export_dir_size;
  IMAGE_EXPORT_DIRECTORY *export_dir;
  DWORD base_ordinal, func_count;
  DWORD *func_addrs;
  FARPROC func_ptr;
  DWORD names_count, i;
  USHORT *names_idxs;
  UINT prev_error_mode;
  char module_name[MAX_PATH];
  DWORD module_name_len;
  char *export_name;
  char *endptr;
  long num;

  baseaddr = win32_get_baseaddr_from_hmodule(module);
  if (!baseaddr)
    return NULL;

  dos_header = (IMAGE_DOS_HEADER *)baseaddr;
  if (dos_header->e_magic != IMAGE_DOS_SIGNATURE)
    return NULL;

  nt_header = (IMAGE_NT_HEADERS *)((BYTE *)dos_header + dos_header->e_lfanew);
  if (nt_header->Signature != IMAGE_NT_SIGNATURE)
    return NULL;

  if (nt_header->FileHeader.SizeOfOptionalHeader < offsetof(IMAGE_OPTIONAL_HEADER, DataDirectory))
    return NULL;

  if (nt_header->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
    return NULL;

  if (nt_header->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT)
    return NULL;

  export_dir_offset = nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
  export_dir_size = nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].Size;

  if (!export_dir_offset || !export_dir_size)
    return NULL;

  export_dir = (IMAGE_EXPORT_DIRECTORY *)(baseaddr + export_dir_offset);
  base_ordinal = export_dir->Base;
  func_count = export_dir->NumberOfFunctions;
  func_addrs = (DWORD *)(baseaddr + (DWORD)export_dir->AddressOfFunctions);

  if (ordinal < base_ordinal || ordinal - base_ordinal > func_count)
    return NULL;

  if (must_be_without_name)
    {
      /* Check that function with ordinal number does not have any name. */
      names_count = export_dir->NumberOfNames;
      names_idxs = (USHORT *)(baseaddr + (DWORD)export_dir->AddressOfNameOrdinals);
      for (i = 0; i < names_count; i++)
        {
          if (names_idxs[i] == ordinal - base_ordinal)
            return NULL;
        }
    }

  func_ptr = (FARPROC)(baseaddr + func_addrs[ordinal - base_ordinal]);
  if ((BYTE *)func_ptr >= (BYTE *)export_dir && (BYTE *)func_ptr < (BYTE *)export_dir + export_dir_size)
    {
      /*
       * We need to locate the _last_ dot character (separator of library name
       * and export symbol name) because library name may contain dot character
       * (used when specifying file name with explicit extension). For example
       * wine is using this kind of strange symbol redirection to different
       * library with non-standard file name extension (different than .dll).
       */
      export_name = strrchr((char *)func_ptr, '.');
      if (!export_name)
        return NULL;
      export_name++;

      module_name_len = export_name - 1 - (char *)func_ptr;
      if (module_name_len >= sizeof(module_name))
        return NULL;

      memcpy(module_name, func_ptr, module_name_len);
      module_name[module_name_len] = 0;

      prev_error_mode = win32_change_error_mode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, TRUE);
      module = LoadLibraryA(module_name);
      win32_change_error_mode(prev_error_mode, FALSE);
      if (!module)
        {
          FreeLibrary(module);
          return NULL;
        }

      if (*export_name == '#')
        {
          export_name++;
          errno = 0;
          num = strtol(export_name, &endptr, 10);
          if (*export_name < '0' || *export_name > '9' || errno || *endptr || num < 0 || (unsigned long)num >= ((DWORD)-1)/2)
            {
              FreeLibrary(module);
              return NULL;
            }
          ordinal = num;
          func_ptr = win32_get_proc_address_by_ordinal(module, ordinal, FALSE);
        }
      else
        {
          func_ptr = GetProcAddress(module, export_name);
        }

      if (!func_ptr)
        FreeLibrary(module);
    }

  return func_ptr;
}

static int
init_physmem_w32skrnl(struct physmem *physmem, struct pci_access *a)
{
  HMODULE w32skrnl;
  LPVOID (WINAPI *GetThunkBuff)(VOID);
  LPVOID buf_ptr;
  DWORD raw_version;
  USHORT build_num;
  BOOL build_num_valid;

  a->debug("resolving DPMI function via GetThunkBuff() function from w32skrnl.dll...");
  w32skrnl = GetModuleHandleA("w32skrnl.dll");
  if (!w32skrnl)
    {
      a->debug("failed: library not present.");
      errno = ENOENT;
      return 0;
    }

  GetThunkBuff = (LPVOID)GetProcAddress(w32skrnl, "_GetThunkBuff@0");
  if (!GetThunkBuff)
    {
      a->debug("failed: symbol not found.");
      errno = ENOENT;
      return 0;
    }

  raw_version = GetVersion();
  build_num = (raw_version >> 16) & 0x3FFF;
  build_num_valid = ((raw_version & 0xC0000000) == 0x80000000 && (raw_version & 0xff) < 4);

  /* Builds older than 88 (match version 1.1) are not supported. */
  if (build_num_valid && build_num < 88)
    {
      a->debug("failed: found old incompatible version.");
      errno = ENOENT;
      return 0;
    }

  if (!win32_get_physmem_offset(&physmem->base_addr_offset))
    {
      a->debug("failed: cannot retrieve physical address offset: %s.", win32_strerror(GetLastError()));
      errno = EINVAL;
      return 0;
    }

  buf_ptr = GetThunkBuff();
  if (!buf_ptr)
    {
      a->debug("failed: cannot retrieve DPMI function pointer.");
      errno = EINVAL;
      return 0;
    }

  /*
   * Builds 88-103 (match versions 1.1-1.15) have DPMI function at offset 0xa0.
   * Builds 111-172 (match versions 1.15a-1.30c) have DPMI function at offset 0xa4.
   * If build number is unavailable then expects the latest version.
   */
  physmem->w32skrnl_dpmi_lcall_ptr = (LPVOID)((BYTE *)buf_ptr + ((build_num_valid && build_num < 111) ? 0xa0 : 0xa4));

  a->debug("success.");
  return 1;
}

static int
init_physmem_vxdcall(struct physmem *physmem, struct pci_access *a)
{
  HMODULE kernel32;
  BOOL success;
  DWORD addr;

  a->debug("resolving VxDCall2() function from kernel32.dll...");
  kernel32 = GetModuleHandleA("kernel32.dll");
  if (!kernel32)
    {
      a->debug("failed: library not present.");
      errno = ENOENT;
      return 0;
    }

  /*
   * New Windows versions do not export VxDCall2 symbol by name anymore,
   * so try also locating this symbol by its ordinal number, which is 3.
   * Old Windows versions prevents using GetProcAddress() for locating
   * kernel32.dll symbol by ordinal number, so use our own custom function.
   * When locating via ordinal number, check that this ordinal number does
   * not have any name assigned (to ensure that it is really VxDCall2).
   */
  physmem->VxDCall2 = (LPVOID)GetProcAddress(kernel32, "VxDCall2");
  if (!physmem->VxDCall2)
    physmem->VxDCall2 = (LPVOID)win32_get_proc_address_by_ordinal(kernel32, 3, TRUE);

  if (!physmem->VxDCall2)
    {
      a->debug("failed: symbol not found.");
      errno = ENOENT;
      return 0;
    }

  /*
   * Wine implementation of VxDCall2() does not support physical address
   * mapping but returns success with virtual address same as passed physical
   * address. Detect this broken wine behavior by trying to map zero address
   * of zero range. Broken wine implementation returns NULL pointer. This
   * prevents accessing unmapped memory or dereferencing NULL pointer.
   */
  success = vdxcall_physical_address_mapping(physmem, 0x0, 0x0, &addr);
  if (success && addr == 0x0)
    {
      a->debug("failed: physical address mapping via VxDCall2() is broken.");
      physmem->VxDCall2 = NULL;
      errno = EINVAL;
      return 0;
    }
  else if (!success)
    {
      a->debug("failed: physical address mapping via VxDCall2() is unsupported.");
      physmem->VxDCall2 = NULL;
      errno = ENOENT;
      return 0;
    }

  /* Retrieve base address - offset for all addresses returned by VxDCall2(). */
  if (!win32_get_physmem_offset(&physmem->base_addr_offset))
    {
      a->debug("failed: cannot retrieve physical address offset: %s.", win32_strerror(GetLastError()));
      physmem->VxDCall2 = NULL;
      errno = EINVAL;
      return 0;
    }

  a->debug("success.");
  return 1;
}

#endif

static int
init_physmem_ntdll(struct physmem *physmem, struct pci_access *a, const char *filename, int w)
{
  wchar_t *wide_filename;
  UNICODE_STRING unicode_filename;
  OBJECT_ATTRIBUTES attributes;
  NTSTATUS status;
  HMODULE ntdll;
  int len;

  a->debug("resolving section functions from ntdll.dll...");
  ntdll = GetModuleHandle(TEXT("ntdll.dll"));
  if (!ntdll)
    {
      a->debug("failed: library ntdll.dll is not present.");
      errno = ENOENT;
      return 0;
    }

  physmem->RtlNtStatusToDosError = (LPVOID)GetProcAddress(ntdll, "RtlNtStatusToDosError");

  physmem->NtOpenSection = (LPVOID)GetProcAddress(ntdll, "NtOpenSection");
  if (!physmem->NtOpenSection)
    {
      a->debug("failed: function NtOpenSection() not found.");
      errno = ENOENT;
      return 0;
    }

  physmem->NtMapViewOfSection = (LPVOID)GetProcAddress(ntdll, "NtMapViewOfSection");
  if (!physmem->NtMapViewOfSection)
    {
      a->debug("failed: function NtMapViewOfSection() not found.");
      errno = ENOENT;
      return 0;
    }

  physmem->NtUnmapViewOfSection = (LPVOID)GetProcAddress(ntdll, "NtUnmapViewOfSection");
  if (!physmem->NtUnmapViewOfSection)
    {
      a->debug("failed: function NtUnmapViewOfSection() not found.");
      errno = ENOENT;
      return 0;
    }

  a->debug("success.");

  /*
   * Note: It is not possible to use WinAPI function OpenFileMappingA() because
   * it takes path relative to the NT base path \\Sessions\\X\\BaseNamedObjects\\
   * and so it does not support to open sections outside that NT directory.
   * NtOpenSection() does not have this restriction and supports specifying any
   * path, including path in absolute format. Unfortunately NtOpenSection()
   * takes path in UNICODE_STRING structure, unlike OpenFileMappingA() which
   * takes path in 8-bit char*. So first it is needed to do conversion from
   * char* string to wchar_t* string via function MultiByteToWideChar() and
   * then fill UNICODE_STRING structure from that wchar_t* string via function
   * RtlInitUnicodeString().
   */

  len = MultiByteToWideChar(CP_ACP, 0, filename, -1, NULL, 0);
  if (len <= 0)
    {
      a->debug("Option devmem.path '%s' is invalid multibyte string.", filename);
      errno = EINVAL;
      return 0;
    }

  wide_filename = pci_malloc(a, len * sizeof(wchar_t));
  len = MultiByteToWideChar(CP_ACP, 0, filename, -1, wide_filename, len);
  if (len <= 0)
    {
      a->debug("Option devmem.path '%s' is invalid multibyte string.", filename);
      pci_mfree(wide_filename);
      errno = EINVAL;
      return 0;
    }

  RtlInitUnicodeString(&unicode_filename, wide_filename);
  InitializeObjectAttributes(&attributes, &unicode_filename, OBJ_CASE_INSENSITIVE, NULL, NULL);

  a->debug("trying to open NT Section %s in %s mode...", filename, w ? "read/write" : "read-only");
  physmem->section_handle = INVALID_HANDLE_VALUE;
  status = physmem->NtOpenSection(&physmem->section_handle, SECTION_MAP_READ | (w ? SECTION_MAP_WRITE : 0), &attributes);

  pci_mfree(wide_filename);

  if (status < 0 || physmem->section_handle == INVALID_HANDLE_VALUE)
    {
      physmem->section_handle = INVALID_HANDLE_VALUE;
      if (status == 0)
        a->debug("failed.");
      else if (physmem->RtlNtStatusToDosError)
        a->debug("failed: %s (0x%lx).", win32_strerror(physmem->RtlNtStatusToDosError(status)), status);
      else
        a->debug("failed: 0x%lx.", status);
      switch (status)
        {
        case STATUS_INVALID_PARAMETER: /* SectionHandle or ObjectAttributes parameter is invalid */
          errno = EINVAL;
          break;
        case STATUS_OBJECT_NAME_NOT_FOUND: /* Section name in ObjectAttributes.ObjectName does not exist */
          errno = ENOENT;
          break;
        case STATUS_ACCESS_DENIED: /* No permission to access Section name in ObjectAttributes.ObjectName */
          errno = EACCES;
          break;
        default: /* Other unspecified error */
          errno = EINVAL;
          break;
        }
      return 0;
    }

  a->debug("success.");
  return 1;
}

void
physmem_init_config(struct pci_access *a)
{
  pci_define_param(a, "devmem.path", PCI_PATH_DEVMEM_DEVICE, "NT path to the PhysicalMemory NT Section"
#if defined(__i386__) || defined(_M_IX86)
    " or \"vxdcall\" or \"w32skrnl\""
#endif
  );
}

int
physmem_access(struct pci_access *a, int w)
{
  struct physmem *physmem = physmem_open(a, w);
  if (!physmem)
    return -1;
  physmem_close(physmem);
  return 0;
}

struct physmem *
physmem_open(struct pci_access *a, int w)
{
  const char *devmem = pci_get_param(a, "devmem.path");
#if defined(__i386__) || defined(_M_IX86)
  int force_vxdcall = strcmp(devmem, "vxdcall") == 0;
  int force_w32skrnl = strcmp(devmem, "w32skrnl") == 0;
#endif
  struct physmem *physmem = pci_malloc(a, sizeof(*physmem));

  memset(physmem, 0, sizeof(*physmem));
  physmem->section_handle = INVALID_HANDLE_VALUE;

  errno = ENOENT;

  if (
#if defined(__i386__) || defined(_M_IX86)
      !force_vxdcall && !force_w32skrnl &&
#endif
      init_physmem_ntdll(physmem, a, devmem, w))
    return physmem;

#if defined(__i386__) || defined(_M_IX86)
  if (!force_w32skrnl && init_physmem_vxdcall(physmem, a))
    return physmem;

  if (!force_vxdcall && init_physmem_w32skrnl(physmem, a))
    return physmem;
#endif

  a->debug("no windows method for physical memory access.");
  pci_mfree(physmem);
  return NULL;
}

void
physmem_close(struct physmem *physmem)
{
  if (physmem->section_handle != INVALID_HANDLE_VALUE)
    CloseHandle(physmem->section_handle);
  pci_mfree(physmem);
}

long
physmem_get_pagesize(struct physmem *physmem UNUSED)
{
  SYSTEM_INFO system_info;
  system_info.dwPageSize = 0;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
}

void *
physmem_map(struct physmem *physmem, u64 addr, size_t length, int w)
{
  LARGE_INTEGER section_offset;
  NTSTATUS status;
  SIZE_T view_size;
  VOID *ptr;

  if (physmem->section_handle != INVALID_HANDLE_VALUE)
    {
      /*
       * Note: Do not use WinAPI function MapViewOfFile() because it makes memory
       * mapping available also for all child processes that are spawned in the
       * future. NtMapViewOfSection() allows to specify ViewUnmap parameter which
       * creates mapping just for this process and not for future child processes.
       * For security reasons we do not want this physical address mapping to be
       * present also in future spawned processes.
       */
      ptr = NULL;
      section_offset.QuadPart = addr;
      view_size = length;
      status = physmem->NtMapViewOfSection(physmem->section_handle, GetCurrentProcess(), &ptr, 0, 0, &section_offset, &view_size, ViewUnmap, 0, w ? PAGE_READWRITE : PAGE_READONLY);
      if (status < 0)
        {
          switch (status)
            {
            case STATUS_INVALID_HANDLE: /* Invalid SectionHandle (physmem->section_handle) */
            case STATUS_INVALID_PARAMETER_3: /* Invalid BaseAddress parameter (&ptr) */
            case STATUS_CONFLICTING_ADDRESSES: /* Invalid value of BaseAddress pointer (ptr) */
            case STATUS_MAPPED_ALIGNMENT: /* Invalid value of BaseAddress pointer (ptr) or SectionOffset (section_offset) */
            case STATUS_INVALID_PARAMETER_4: /* Invalid ZeroBits parameter (0) */
            case STATUS_INVALID_PARAMETER_5: /* Invalid CommitSize parameter (0) */
            case STATUS_INVALID_PARAMETER_8: /* Invalid InheritDisposition parameter (ViewUnmap) */
            case STATUS_INVALID_PARAMETER_9: /* Invalid AllocationType parameter (0) */
            case STATUS_SECTION_PROTECTION: /* Invalid Protect parameter (based on w) */
            case STATUS_INVALID_PAGE_PROTECTION: /* Invalid Protect parameter (based on w) */
              errno = EINVAL;
              break;
            case STATUS_INVALID_VIEW_SIZE: /* Invalid SectionOffset / ViewSize range (section_offset, view_size) */
              errno = ENXIO;
              break;
            case STATUS_INSUFFICIENT_RESOURCES: /* Quota limit exceeded */
            case STATUS_NO_MEMORY: /* Memory limit exceeded */
              errno = ENOMEM;
              break;
            case STATUS_ACCESS_DENIED: /* No permission to create mapping */
              errno = EPERM;
              break;
            default: /* Other unspecified error */
              errno = EACCES;
              break;
            }
          return (void *)-1;
        }

      return ptr;
    }
#if defined(__i386__) || defined(_M_IX86)
  else if (physmem->VxDCall2 || physmem->w32skrnl_dpmi_lcall_ptr)
    {
      BOOL success;
      DWORD virt;

      /*
       * These two methods support mapping only the first 4 GB of physical memory
       * and mapped memory is always read/write. There is no way to create
       * read-only mapping, so argument "w" is ignored.
       */
      if (addr >= 0xffffffffUL || addr + length > 0xffffffffUL)
        {
          errno = EOVERFLOW;
          return (void *)-1;
        }

      if (physmem->VxDCall2)
        success = vdxcall_physical_address_mapping(physmem, (DWORD)addr, length, &virt);
      else
        success = w32skrnl_physical_address_mapping(physmem, (DWORD)addr, length, &virt);

      /* Both above functions set errno on failure. */
      if (!success)
        return (void *)-1;

      /* Virtual address from our view is calculated from the base offset. */
      ptr = (VOID *)(virt - physmem->base_addr_offset);
      return ptr;
    }
#endif


  /* invalid physmem parameter */
  errno = EBADF;
  return (void *)-1;
}

int
physmem_unmap(struct physmem *physmem, void *ptr, size_t length)
{
  long pagesize = physmem_get_pagesize(physmem);
  MEMORY_BASIC_INFORMATION info;
  NTSTATUS status;

  if (physmem->section_handle != INVALID_HANDLE_VALUE)
    {
      /*
       * NtUnmapViewOfSection() unmaps entire memory range previously mapped by
       * NtMapViewOfSection(). The specified ptr (BaseAddress) does not have to
       * point to the beginning of the mapped memory range.
       *
       * So verify that the ptr argument is the beginning of the mapped range
       * and length argument is the length of mapped range.
       */

      if (VirtualQuery(ptr, &info, sizeof(info)) != sizeof(info))
        {
          errno = EINVAL;
          return -1;
        }

      /* RegionSize is already page aligned, but length does not have to be. */
      if (info.AllocationBase != ptr || info.RegionSize != ((length + pagesize-1) & ~(pagesize-1)))
        {
          errno = EINVAL;
          return -1;
        }

      status = physmem->NtUnmapViewOfSection(GetCurrentProcess(), ptr);
      if (status < 0)
        {
          switch (status)
            {
            case STATUS_NOT_MAPPED_VIEW: /* BaseAddress parameter (ptr) not mapped */
              errno = EINVAL;
              break;
            case STATUS_ACCESS_DENIED: /* No permission to unmap BaseAddress (ptr) */
              errno = EPERM;
              break;
            default: /* Other unspecified error */
              errno = EACCES;
              break;
            }
          return -1;
        }

      return 0;
    }
#if defined(__i386__) || defined(_M_IX86)
  else if (physmem->VxDCall2 || physmem->w32skrnl_dpmi_lcall_ptr)
    {
      /* There is no way to unmap physical memory mapped by these methods. */
      errno = ENOSYS;
      return -1;
    }
#endif

  /* invalid physmem parameter */
  errno = EBADF;
  return -1;
}
