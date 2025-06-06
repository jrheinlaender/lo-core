#!/bin/sh

set -e

# Quick and dirty script to stat the LO translations/ pos wrt % translated
# FIXME: maybe use pocount from translate-toolkit

# Author: Rene Engelhard <rene@debian.org>
# (C) 2017 Software in the Public Interest, Inc.

cd translations/source

for l in `ls -1`; do
	# 23:36 <@cloph> no - but you can do a brute-force method of just counting msgid and comparing that with »msgstr ""« matches.
	# 23:37 <@cloph> While that ignores multiline strings, there aren't too many and should work as a rough estimate
	msgid_count=$(grep msgid `find $l -name "*.po"` | wc -l)
	msgstr_count=$(grep msgstr `find $l -name "*.po"` | wc -l)
	empty_msgstr_count=$(grep msgstr\ \"\" `find $l -name "*.po"` | wc -l)
	p=$((100*$empty_msgstr_count/$msgid_count))
	echo "$l: $msgid_count strings, $(($msgid_count-$empty_msgstr_count))/$msgid_count translated; $empty_msgstr_count/$msgid_count untranslated"
	if test $msgid_count -lt 50000; then
		echo "$l: no help translations"
	fi
	echo "$l: $((100-$p))% translated, $p% untranslated"

	if test $p -lt 20; then
		langs="$langs $l"
	fi
done

echo "languages over 80% translated:"
echo $langs
