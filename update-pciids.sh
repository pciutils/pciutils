#!/bin/sh

set -e
SRC="http://pciids.sourceforge.net/pci.ids"
DEST=pci.ids

if which bzip2 >/dev/null ; then
	DECOMP="bzip2 -d"
	SRC="$SRC.bz2"
elif which gzip >/dev/null ; then
	DECOMP="gzip -d"
	SRC="$SRC.gz"
else
	DECOMP="cat"
fi

if which wget >/dev/null ; then
	DL="wget -O $DEST.new $SRC"
elif which lynx >/dev/null ; then
	DL="eval lynx -source $SRC >$DEST.new"
else
	echo >&2 "update-pciids: cannot find wget nor lynx"
	exit 1
fi

if ! $DL ; then
	echo >&2 "update-pciids: download failed"
	rm -f $DEST.new
	exit 1
fi

if ! $DECOMP <$DEST.new >$DEST.neww ; then
	echo >&2 "update-pciids: decompression failed, probably truncated file"
	exit 1
fi

if ! grep >/dev/null "^C " $DEST.neww ; then
	echo >&2 "update-pciids: missing class info, probably truncated file"
	exit 1
fi

if [ -f $DEST ] ; then
	mv $DEST $DEST.old
fi
mv $DEST.neww $DEST
rm $DEST.new

echo "Done."
