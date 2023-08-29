project(
  'pciutils',
  'c',
  version: '3.10.0',
  meson_version: '>= 0.50.0',
  default_options: [
    'buildtype=debugoptimized',
    'default_library=static',
    'warning_level=2',
    'werror=true',
  ]
)

cc = meson.get_compiler('c')

abi_version = meson.project_version().split('.')[0]

add_project_arguments(
  [
    '-Wno-parentheses',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes'
  ],
  language: 'c'
)

zlib_dep = dependency('zlib', method: 'pkg-config', required: get_option('with-zlib'))
kmod_dep = dependency('libkmod', method: 'pkg-config', required: get_option('with-libkmod'))
hwdb_dep = dependency('libudev', method: 'pkg-config', version: '>=196', required: get_option('with-hwdb'))
dns_dep = cc.find_library('resolv', has_headers: ['resolv.h'], required: get_option('with-dns'))

dependencies = [ zlib_dep, kmod_dep, hwdb_dep, dns_dep ]
sources = [
  'lib/access.c',
  'lib/caps.c',
  'lib/dump.c',
  'lib/filter.c',
  'lib/generic.c',
  'lib/header.h',
  'lib/init.c',
  'lib/internal.h',
  'lib/names.c',
  'lib/names-cache.c',
  'lib/names.h',
  'lib/names-hash.c',
  'lib/names-hwdb.c',
  'lib/names-net.c',
  'lib/names-parse.c',
  'lib/params.c',
  'lib/pci.h',
  'lib/sysdep.h',
  'lib/types.h',
]

i386_ports_srcs = [
  'lib/i386-io-cygwin.h',
  'lib/i386-io-hurd.h',
  'lib/i386-io-linux.h',
  'lib/i386-io-sunos.h',
  'lib/i386-io-windows.h',
  'lib/i386-ports.c',
]
proc_srcs = [
  'lib/pread.h',
  'lib/proc.c',
]

sysfs_srcs = [
  'lib/pread.h',
  'lib/sysfs.c',
]

conf = configuration_data()
conf.set('PCI_ARCH_@0@'.format(host_machine.cpu().to_upper()), 1)
conf.set('PCI_OS_@0@'.format(host_machine.system().to_upper()), 1)
conf.set_quoted('PCILIB_VERSION', meson.project_version())

IDSDIR = get_option('prefix') / get_option('datadir')

if host_machine.system() == 'linux'
  conf.set('PCI_HAVE_PM_LINUX_SYSFS', 1)
  conf.set('PCI_HAVE_PM_LINUX_PROC', 1)
  conf.set('PCI_HAVE_PM_MMIO_CONF', 1)
  conf.set('PCI_HAVE_PM_ECAM', 1)
  conf.set('PCI_HAVE_LINUX_BYTEORDER_H', 1)
  conf.set_quoted('PCI_PATH_PROC_BUS_PCI', '/proc/bus/pci')
  conf.set_quoted('PCI_PATH_SYS_BUS_PCI', '/sys/bus/pci')
  conf.set_quoted('PCI_PATH_DEVMEM_DEVICE', '/dev/mem')
  conf.set_quoted('PCI_PATH_ACPI_MCFG', '/sys/firmware/acpi/tables/MCFG')
  conf.set_quoted('PCI_PATH_EFI_SYSTAB', '/sys/firmware/efi/systab')
  if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
    conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
    sources += i386_ports_srcs
  endif
  conf.set('PCI_HAVE_64BIT_ADDRESS', 1)
  sources += [
    'lib/mmio-ports.c',
    'lib/ecam.c',
  ]
  sources += proc_srcs
  sources += sysfs_srcs
elif host_machine.system() == 'sunos'
  if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
    conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
    sources += i386_ports_srcs
  else
    error('The PCI library does not support Solaris for this architecture:', host_machine.cpu_family())
  endif
  conf.set('PCI_HAVE_STDINT_H', 1)
elif host_machine.system().contains('freebsd')
  conf.set('PCI_HAVE_PM_FBSD_DEVICE', 1)
  conf.set('PCI_HAVE_PM_MMIO_CONF', 1)
  conf.set('PCI_HAVE_PM_ECAM', 1)
  conf.set_quoted('PCI_PATH_FBSD_DEVICE', '/dev/pci')
  conf.set_quoted('PCI_PATH_DEVMEM_DEVICE', '/dev/mem')
  conf.set_quoted('PCI_PATH_ACPI_MCFG', '')
  conf.set_quoted('PCI_PATH_EFI_SYSTAB', '')
  sources += [
    'lib/mmio-ports.c',
    'lib/ecam.c',
    'lib/fbsd-device.c',
  ]
elif host_machine.system() == 'openbsd'
  conf.set('PCI_HAVE_PM_OBSD_DEVICE', 1)
  conf.set('PCI_HAVE_PM_MMIO_CONF', 1)
  conf.set('PCI_HAVE_PM_ECAM', 1)
  conf.set_quoted('PCI_PATH_OBSD_DEVICE', '/dev/pci')
  conf.set_quoted('PCI_PATH_DEVMEM_DEVICE', '/dev/mem')
  conf.set_quoted('PCI_PATH_ACPI_MCFG', '/var/db/acpi/MCFG.*')
  conf.set_quoted('PCI_PATH_EFI_SYSTAB', '')
  sources += [
    'lib/mmio-ports.c',
    'lib/ecam.c',
    'lib/obsd-device.c',
  ]
elif host_machine.system() == 'darwin'
  conf.set('PCI_HAVE_PM_DARWIN_DEVICE', 1)
  conf.set('PCI_HAVE_64BIT_ADDRESS', 1)
  dependencies += [
    dependency('appleframeworks', modules: 'CoreFoundation', required: true),
    dependency('appleframeworks', modules: 'IOKit', required: true)
  ]
  sources += ['lib/darwin.c']
elif host_machine.system() == 'aix'
  conf.set('PCI_HAVE_PM_AIX_DEVICE', 1)
  sources += ['lib/aix-device.c']
elif host_machine.system() == 'netbsd'
  conf.set('PCI_HAVE_PM_NBSD_LIBPCI', 1)
  conf.set('PCI_HAVE_PM_MMIO_CONF', 1)
  conf.set('PCI_HAVE_PM_ECAM', 1)
  conf.set('PCI_PATH_NBSD_DEVICE', '/dev/pci0')
  conf.set('PCI_PATH_DEVMEM_DEVICE', '/dev/mem')
  conf.set('PCI_PATH_ACPI_MCFG', '')
  conf.set('PCI_PATH_EFI_SYSTAB', '')
  conf.set('PCI_HAVE_64BIT_ADDRESS', 1)
  sources += [
    'lib/mmio-ports.c',
    'lib/ecam.c',
    'lib/nbsd-libpci.c',
  ]
elif host_machine.system() == 'gnu'
  conf.set('PCI_HAVE_PM_HURD_CONF', 1)
  conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
  sources += [
    'lib/hurd.c',
  ]
  + i386_ports_srcs
elif host_machine.system() == 'djgpp'
  conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
  sources += i386_port_srcs
elif host_machine.system() == 'cygwin'
  conf.set('PCI_HAVE_64BIT_ADDRESS', 1)
  conf.set('PCI_HAVE_PM_WIN32_CFGMGR32', 1)
  conf.set('PCI_HAVE_PM_WIN32_KLDBG', 1)
  conf.set('PCI_HAVE_PM_WIN32_SYSDBG', 1)
  if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
    conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
    sources += [
      'lib/emulated.c',
      'lib/win32-cfgmgr32.c',
      'lib/win32-kldbg.c',
      'lib/win32-sysdbg.c',
    ]
    + i386_port_srcs
  endif
  dependencies += [ cc.find_library('ioperm', required: true) ]
elif host_machine.system() == 'windows'
  conf.set('PCI_HAVE_64BIT_ADDRESS', 1)
  conf.set('PCI_HAVE_PM_WIN32_CFGMGR32', 1)
  conf.set('PCI_HAVE_PM_WIN32_KLDBG', 1)
  conf.set('PCI_HAVE_PM_WIN32_SYSDBG', 1)
  if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
    conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
    sources += [
      'lib/emulated.c',
      'lib/win32-cfgmgr32.c',
      'lib/win32-kldbg.c',
      'lib/win32-sysdbg.c',
    ]
    + i386_ports_srcs
  endif
  dependencies += [ cc.find_library('advapi32', required: true) ]
elif host_machine.system() == 'haiku'
  if host_machine.cpu_family() == 'x86' or host_machine.cpu_family() == 'x86_64'
    conf.set('PCI_HAVE_PM_INTEL_CONF', 1)
    sources += i386_ports_srcs
  endif
  conf.set('PCI_HAVE_STDINT_H', 1)
elif host_machine.system() == 'sylixos'
  conf.set_quoted('PCI_PATH_SYLIXOS_DEVICE', '/proc/pci')
  conf.set('PCI_HAVE_64BIT_ADDRESS', 1)
  conf.set('PCI_HAVE_PM_SYLIXOS_DEVICE', 1)
  IDSDIR = get_option('prefix') / get_option('sysconfdir') / 'pci'
  sources += ['lib/sylixos-device.c']
else
  error('Unfortunately, your OS is not supported by the PCI library')
endif

conf.set_quoted('PCI_PATH_IDS_DIR', IDSDIR)

if zlib_dep.found()
  conf.set('PCI_COMPRESSED_IDS', true)
  conf.set_quoted('PCI_IDS', 'pci.ids.gz')
  pci_ids = 'pci.ids.gz'
else
  conf.set_quoted('PCI_IDS', 'pci.ids')
  pci_ids = 'pci.ids'
endif

if (get_option('with-dns').enabled() or
  get_option('with-dns').auto() and host_machine.system() != 'windows')

  assert(cc.has_function('res_query', dependencies:dns_dep), 'missing required function: res_query')
  conf.set('PCI_USE_DNS', 1)
  conf.set_quoted('PCI_ID_DOMAIN', 'pci.id.ucw.cz')
endif

if kmod_dep.found()
  conf.set('PCI_USE_LIBKMOD', 1)
endif
if hwdb_dep.found()
  conf.set('PCI_HAVE_HWDB', 1)
endif

conf.set('PCI_HAVE_PM_DUMP', 1)

conf_file = configure_file(configuration: conf, output: 'config.h')

pcilib = library('pci',
                 dependencies: dependencies,
                 install: true,
                 sources: sources,
                 gnu_symbol_visibility: 'hidden',
                 link_args: '-Wl,--version-script=' + meson.current_source_dir() + '/lib/libpci.ver',
                 link_depends: ['lib/libpci.ver'])

pkg = import('pkgconfig')
pkg.generate(pcilib,
             name: 'libpci',
             filebase: 'libpci',
             description:'libpci',
             variables: [
              'idsdir=' + IDSDIR
            ])

common_srcs = [
  'common.c',
  'pciutils.h',
]
if not cc.has_function('getopt')
  message('getopt() not found. Using compat version')
  common_srcs += [
    'compat/getopt.c',
    'compat/getopt.h',
  ]
elif not cc.has_header_symbol('unistd.h', 'getopt')
  message('getopt() found, but not declared in unistd.h. Using compat version')
  common_srcs += [
    'compat/getopt.c',
    'compat/getopt.h',
  ]
endif

lspci_srcs = common_srcs + [
  'ls-caps.c',
  'ls-caps-vendor.c',
  'ls-ecaps.c',
  'ls-kernel.c',
  'ls-map.c',
  'lspci.c',
  'lspci.h',
  'ls-tree.c',
  'ls-vpd.c',
]

executable('lspci',
            sources: lspci_srcs,
            dependencies: [
              kmod_dep,
            ],
            link_with: [
               pcilib,
            ],
            install: true)

setpci_srcs = common_srcs + [
  'setpci.c',
]

executable('setpci',
           sources: setpci_srcs,
           link_with: [
              pcilib,
           ],
           install: true)

example_srcs = [
  conf_file,
  'lib/header.h',
  'lib/pci.h',
  'lib/sysdep.h',
  'lib/types.h',
  'example.c',
]

executable('example',
           sources: example_srcs,
           link_with: [
              pcilib,
           ],
           install: true)

install_headers(
  conf_file,
  'lib/header.h',
  'lib/pci.h',
  'lib/types.h',
  subdir: 'pci',
)

custom_target('update-pciids',
              build_by_default: true,
              command: [
                'sed',
                '-e',
	        's@^DEST=.*@DEST="' + IDSDIR/pci_ids + '"@',
                '-e',
                's@^PCI_COMPRESSED_IDS=.*@PCI_COMPRESSED_IDS='
                  + zlib_dep.found().to_int().to_string()
                  + '@',
                '-e',
                's@VERSION=.*@VERSION=' + meson.project_version() + '@',
                '@INPUT@',
              ],
              capture: true,
              input: [
                'update-pciids.sh',
              ],
              output: [
                'update-pciids',
              ],
              install: true,
              install_dir: get_option('sbindir'),
              install_mode: ['rwxr-xr-x'])

if pci_ids == 'pci.ids.gz'
  custom_target(pci_ids,
                build_by_default: true,
                command: [
                  'gzip',
                  '-9nc',
                  '@INPUT@'
                ],
                capture: true,
                input: [
                  'pci.ids',
                ],
                output: [
                  pci_ids
                ],
                install: true,
                install_dir: IDSDIR,
                install_mode: ['rw-r--r--'])
endif

build_date = get_option('build-date')
if build_date == ''
  build_date = run_command(['date', '+%d-%B-%Y'],
                           check: false,
                           capture: true).stdout().strip()
endif

custom_target('pci.ids.5',
              build_by_default: true,
              command: [
                'sed',
                '-e',
                's/@TODAY@/' + build_date + '/',
                '-e',
                's/@VERSION@/pciutils-' + meson.project_version() + '/',
                '-e',
                's#@IDSDIR@#' + IDSDIR + '#',
                '-e',
                's#@PCI_IDS@#' + pci_ids + '#',
                '@INPUT@'
              ],
              capture: true,
              input: [ 'pci.ids.man' ],
              output: [ 'pci.ids.5' ],
              install: true,
              install_dir: get_option('mandir') / 'man5')

custom_target('pcilib.7',
              build_by_default: true,
              command: [
                'sed',
                '-e',
                's/@TODAY@/' + build_date + '/',
                '-e',
                's/@VERSION@/pciutils-' + meson.project_version() + '/',
                '-e',
                's#@IDSDIR@#' + IDSDIR + '#',
                '-e',
                's#@PCI_IDS@#' + pci_ids + '#',
                '@INPUT@'
              ],
              capture: true,
              input: [ 'pcilib.man' ],
              output: [ 'pcilib.7' ],
              install: true,
              install_dir: get_option('mandir') / 'man7')


custom_target('lspci.8',
              build_by_default: true,
              command: [
                'sed',
                '-e',
                's/@TODAY@/' + build_date + '/',
                '-e',
                's/@VERSION@/pciutils-' + meson.project_version() + '/',
                '-e',
                's#@IDSDIR@#' + IDSDIR + '#',
                '-e',
                's#@PCI_IDS@#' + pci_ids + '#',
                '@INPUT@'
              ],
              capture: true,
              input: [ 'lspci.man' ],
              output: [ 'lspci.8' ],
              install: true,
              install_dir: get_option('mandir') / 'man8')

custom_target('setpci.8',
              build_by_default: true,
              command: [
                'sed',
                '-e',
                's/@TODAY@/' + build_date + '/',
                '-e',
                's/@VERSION@/pciutils-' + meson.project_version() + '/',
                '-e',
                's#@IDSDIR@#' + IDSDIR + '#',
                '-e',
                's#@PCI_IDS@#' + pci_ids + '#',
                '@INPUT@'
              ],
              capture: true,
              input: [ 'setpci.man' ],
              output: [ 'setpci.8' ],
              install: true,
              install_dir: get_option('mandir') / 'man8')

custom_target('update-pciids.8',
              build_by_default: true,
              command: [
                'sed',
                '-e',
                's/@TODAY@/' + build_date + '/',
                '-e',
                's/@VERSION@/pciutils-' + meson.project_version() + '/',
                '-e',
                's#@IDSDIR@#' + IDSDIR + '#',
                '-e',
                's#@PCI_IDS@#' + pci_ids + '#',
                '@INPUT@'
              ],
              capture: true,
              input: [ 'update-pciids.man' ],
              output: [ 'update-pciids.8' ],
              install: true,
              install_dir: get_option('mandir') / 'man8')
