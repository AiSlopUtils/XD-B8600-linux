#!/bin/sh

candidate="$1"

tar=`which $candidate`
if [ ! -x "$tar" ]; then
	tar=`which tar`
	if [ ! -x "$tar" ]; then
		# echo nothing: no suitable tar found
		exit 1
	fi
fi

# Output of 'tar --version' examples:
# tar (GNU tar) 1.15.1
# tar (GNU tar) 1.25
# bsdtar 2.8.3 - libarchive 2.8.3
version=`$tar --version | head -n 1 | sed 's/^.*\s\([0-9]\+\.\S\+\).*$/\1/'`
major=`echo "$version" | cut -d. -f1`
minor=`echo "$version" | cut -d. -f2`
bugfix=`echo "$version" | cut -d. -f3`
version_bsd=`$tar --version | grep 'bsdtar'`

# BSD tar does not have all the command-line options
if [ -n "${version_bsd}" ] ; then
    # echo nothing: no suitable tar found
    exit 1
fi

# Minimal version = 1.27 (previous versions do not correctly unpack archives
# containing hard-links if the --strip-components option is used or create
# different gnu long link headers for path elements > 100 characters).
major_min=1
minor_min=27

# Newer GNU tar releases changed only deterministic archive output here.  This
# project consumes package archives but does not emit a Buildroot rootfs, so
# the container's maintained GNU tar is both safe and preferable to building
# the old host-tar 1.29 on a modern host.
if [ $major -lt $major_min ]; then
	# echo nothing: no suitable tar found
	exit 1
fi

if [ $major -eq $major_min -a $minor -lt $minor_min ]; then
	# echo nothing: no suitable tar found
	exit 1
fi

# valid
echo $tar
