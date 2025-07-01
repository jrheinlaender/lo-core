#! /usr/bin/python3

import re, sys, fileinput

def splitlines():
    fields = ('Build-Depends', 'Build-Conflicts', 'Build-Depends-Indep', 'Build-Depends-Arch',
              'Depends', 'Replaces', 'Provides', 'Conflicts', 'Recommends', 'Suggests', 'Breaks')
    for line in fileinput.input():
        line = line[:-1]
        field = None
        for f in fields:
            if line.startswith(f+':'):
                field = f
                break
        if not field:
            print(line)
            continue
        values = [f.strip() for f in line.split(':',1)[1].strip().split(',')]
        if len(values) > 2:
            print('%s: %s' % (field, ',\n '.join(values)))
        else:
            print('%s: %s' % (field, ', '.join(values)))

def sort_list(unsorted_list):
    packages = [x for x in unsorted_list if re.match("[a-z0-9]", x)]
    special = [x for x in unsorted_list if not re.match("[a-z0-9]", x)]
    return sorted(packages) + sorted(special)

def joinlines():
    fields = ('Build-Depends', 'Build-Conflicts', 'Build-Depends-Indep', 'Build-Depends-Arch',
              'Depends', 'Replaces', 'Provides', 'Conflicts', 'Recommends', 'Suggests', 'Breaks')
    buffer = None
    for line in fileinput.input():
        line = line[:-1]
        if buffer:
            if line.startswith(' '):
                buffer = buffer + ' ' + line.strip()
                continue
            else:
                packages = sort_list(set([x.strip() for x in buffer[len(field)+1:].split(",")]))
                if "" in packages:
                    packages.remove("")
                length = len(field) + sum([2 + len(package) for package in packages])
                if length > 80:
                    indentation = " " * (len(field) + 2)
                    packages_with_indention = [indentation + x for x in packages]
                    packages_with_indention = ",\n".join(packages_with_indention)
                    print(field + ": " + packages_with_indention.strip())
                else:
                    print(field + ": " + ", ".join(packages).strip())
                buffer = None
        field = None
        for f in fields:
            if line.startswith(f+':'):
                field = f
                break
        if field:
            buffer = line.strip()
            continue
        print(line)

def main():
    #splitlines()
    joinlines()

main()
