/*
 *	Linux PCI Utilities -- Declarations
 *
 *	Copyright (c) 1997--2003 Martin Mares <mj@ucw.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#include "lib/pci.h"

#define PCIUTILS_VERSION PCILIB_VERSION

#if !defined(__GNUC__)
#define NONRET
#define inline
#else
#define NONRET __attribute__((noreturn))
#endif

void die(char *msg, ...) NONRET;
void *xmalloc(unsigned int howmuch);
int parse_generic_option(int i, struct pci_access *pacc, char *optarg);

#ifdef HAVE_PM_LINUX_PROC
#define GENOPT_PROC "P:"
#define GENHELP_PROC "-P <dir>\tUse specified directory instead of " PATH_PROC_BUS_PCI "\n"
#else
#define GENOPT_PROC
#define GENHELP_PROC
#endif
#ifdef HAVE_PM_INTEL_CONF
#define GENOPT_INTEL "H:"
#define GENHELP_INTEL "-H <mode>\tUse direct hardware access (<mode> = 1 or 2)\n"
#else
#define GENOPT_INTEL
#define GENHELP_INTEL
#endif
#ifdef HAVE_PM_DUMP
#define GENOPT_DUMP "F:"
#define GENHELP_DUMP "-F <file>\tRead configuration data from given file\n"
#else
#define GENOPT_DUMP
#define GENHELP_DUMP
#endif

#define GENERIC_OPTIONS "G" GENOPT_PROC GENOPT_INTEL GENOPT_DUMP
#define GENERIC_HELP GENHELP_PROC GENHELP_INTEL GENHELP_DUMP \
	"-G\t\tEnable PCI access debugging\n"
