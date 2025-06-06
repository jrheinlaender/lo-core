#!/bin/sh

set -x
set -e

pwd=`pwd`
tmp=`mktemp -q -d`

debianver=`uscan --report --verbose | grep newversion | cut -d= -f2 | awk '{ print $1 }' | head -n 1`

# the main tarball
t=`uscan --report --verbose | grep newfile | cut -d= -f2 | head -n 1`
sourcever=`echo $t | cut -d- -f2 | sed -e "s/\.tar\.xz$//"`

files=`uscan --report --verbose | grep newfile | cut -d= -f2 | tail -n +2`

for f in $files; do
	if echo $f | grep -q help; then
		c=helpcontent2;
	else
		c=`echo $f | cut -d- -f2`;
	fi

	cd ${tmp}
	echo "Extracting original $f..."
	tar --strip-components 1 --extract --verbose --xz --file ${pwd}/../$f
	echo "Deleting obsolete libreoffice_${debianver}.orig-${c}.tar.xz and it's signatiure...."
	rm -f ${pwd}/../libreoffice_${debianver}.orig-${c}.tar.xz
	rm -f ${pwd}/../libreoffice_${debianver}.orig-${c}.tar.xz.asc
	echo "Creating new libreoffice_${debianver}.orig-${c}.tar.xz..."
	tar --create --verbose --xz --file ${pwd}/../libreoffice_${debianver}.orig-${c}.tar.xz ${c}
	rm -rf ${c}
	cd ${pwd}
done

rm -rf ${tmp}
