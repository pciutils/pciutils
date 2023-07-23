#!/bin/sh

set -e

SRC="https://pci-ids.ucw.cz/v2.2/pci.ids"
DEST=pci.ids
PCI_COMPRESSED_IDS=
GREP=grep
VERSION=unknown
USER_AGENT=update-pciids/$VERSION
QUIET=

[ "$1" = "-q" ] && quiet=true || quiet=false

# if pci.ids is read-only (because the filesystem is read-only),
# then just skip this whole process.
if ! touch ${DEST} >/dev/null 2>&1 ; then
	${quiet} || echo "${DEST} is read-only, exiting." 1>&2
	exit 1
fi

if command -v xz >/dev/null 2>&1 ; then
	DECOMP="xz -d"
	SRC="$SRC.xz"
elif command -v bzip2 >/dev/null 2>&1 ; then
	DECOMP="bzip2 -d"
	SRC="$SRC.bz2"
elif command -v gzip >/dev/null 2>&1 ; then
	DECOMP="gzip -d"
	SRC="$SRC.gz"
else
	DECOMP="cat"
fi

if command -v curl >/dev/null 2>&1 ; then
	${quiet} && QUIET="-s -S"
	dl ()
	{
		curl -o $DEST.new --user-agent "$USER_AGENT curl" $QUIET $SRC
	}
elif command -v wget >/dev/null 2>&1 ; then
	${quiet} && QUIET="-q"
	dl ()
	{
		wget --no-timestamping -O $DEST.new --user-agent "$USER_AGENT wget" $QUIET $SRC
	}
elif command -v lynx >/dev/null 2>&1 ; then
	dl ()
	{
		lynx -source -useragent="$USER_AGENT lynx" $SRC >$DEST.new
	}
else
	echo >&2 "update-pciids: cannot find curl, wget or lynx"
	exit 1
fi

if ! dl ; then
	echo >&2 "update-pciids: download failed"
	rm -f $DEST.new
	exit 1
fi

if ! $DECOMP <$DEST.new >$DEST.new.plain ; then
	echo >&2 "update-pciids: decompression failed, probably truncated file"
	exit 1
fi

if ! $GREP >/dev/null "^C " $DEST.new.plain ; then
	echo >&2 "update-pciids: missing class info, probably truncated file"
	exit 1
fi

if [ -f $DEST ] ; then
	ln -f $DEST $DEST.old
	# --reference is supported only by chmod from GNU file, so let's ignore any errors
	chmod -f --reference=$DEST.old $DEST.new $DEST.new.plain 2>/dev/null || true
fi

if [ "$PCI_COMPRESSED_IDS" = 1 ] ; then
	if [ "${SRC%.gz}" != .gz ] ; then
		# Recompress to gzip
		gzip <$DEST.new.plain >$DEST.new
	fi
	mv $DEST.new $DEST
	rm -f $DEST.new.plain
else
	mv $DEST.new.plain $DEST
	rm -f $DEST.new
fi

# Older versions did not compress the ids file, so let's make sure we
# clean that up.
if [ ${DEST%.gz} != ${DEST} ] ; then
	rm -f ${DEST%.gz} ${DEST%.gz}.old
fi

${quiet} || echo "Done."
