#!/bin/sh

libs=`grep libebook.*\.so connectivity/source/drivers/evoab2/EApi.cxx | perl -pe 's/\s+\"(.*)\".*/$1/'`

for l in $libs; do
	if [ -e "/usr/lib/`dpkg-architecture -qDEB_HOST_MULTIARCH`/$l" ]; then
		p=/usr/lib/`dpkg-architecture -qDEB_HOST_MULTIARCH`
	else
		if [ -e /usr/lib/$l ]; then
			p=/usr/lib
		else
			continue
		fi
        fi
	# sanity check: do the libs match with what we would get
	# for our libebook version if we followed the .so symlink?
	l1=`readlink $p/$l`
	l2_tmp=`echo $l | perl -pe 's/(.*)\.\d+$/$1/'`
	l2=`readlink $p/$l2_tmp`
	l3=`readlink $p/$l2`
	if [ "$l1" = "$l2" -o "$l1" = "$l3" ]; then
		dep=`dpkg -S $p/$l | cut -d: -f1`
	fi
done

if [ -n "$dep" ]; then
	echo $dep
else
	echo "Cannot find libebook dependency. None of the following libs found:"
	echo $libs
	exit 1
fi

