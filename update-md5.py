#!/usr/bin/env python2
# -*- coding:utf-8 -*-
#  
#  Copyright 2013 buaa.byl@gmail.com
#
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; see the file COPYING.  If not, write to
#  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
#
from __future__ import print_function
import copy
import sys
import os
import re
from hashlib import md5

regex_file = re.compile(r'^[ *]+File: ([0-9a-zA-Z_.-]+)\W*$')
regex_md5  = re.compile(r'^[ *]+(MD5): ([0-9a-fA-F]+)\W*$')
regex_id   = re.compile(r'^[ *]+(ID): ([0-9a-fA-F]+)\W*$')

HEADER = '''\
/*
 * ----------------------------------------------------------
 * File: %(filename)s
 * %(type)s: %(hash)s
 * ----------------------------------------------------------
 */\
'''


def file_get_contents(fn):
    f = open(fn, 'r')
    d = f.read()
    f.close()
    return d

def file_put_contents(fn, d):
    f = open(fn, 'w')
    f.write(d)
    f.close()

def new_info(INFO, lineno, filename, newtype, newhash):
    info = copy.deepcopy(INFO)
    info['lineno']  = lineno + 1
    info['filename']= filename
    info['type']    = newtype
    info['oldhash'] = newhash
    return info

def build_context(info, lines):
    text = '\n'.join(lines)
    hashstr = md5(text).hexdigest()
    info['hash']    = hashstr
    info['range']   = (info['lineno'], info['lineno'] + len(lines) - 1)
    info['text']    = text
    return info

if __name__ == '__main__':
    source_fn   = sys.argv[1]
    v = file_get_contents(source_fn)

    print('parsing...', source_fn)
    print()

    INFO = {
        'lineno'    :0,
        'range'     :(0, ),
        'filename'  :'',
        'type'      :'',
        'oldhash'   :'',
        'hash'      :'',
        'text'      :''
    }

    lines = v.splitlines()
    nr_lines = len(lines)

    lst_unpacked = []

    lineno = 0
    file_context = []

    info = new_info(INFO, lineno, 'amalgamation.h', '', '')

    while lineno < nr_lines:
        line = lines[lineno]

        if line.find('END-OF-IMPLEMENTATION:') >= 0:
            if len(file_context) > 0:
                lst_unpacked.append(build_context(info, file_context))
                info = new_info(INFO, lineno, 'amalgamation.c', newtype, newhash)
            file_context = lines[lineno:]
            break

        res = regex_file.match(line)
        if res:
            fn_new  = res.groups()[0]
            line    = lines[lineno + 1]
            res     = regex_md5.match(line)
            if not res:
                res = regex_id.match(line)
            if not res:
                file_context.append(lines[lineno])
                lineno += 1
                continue

            newtype = res.groups()[0]
            newhash = res.groups()[1]
            lineno += 4

            file_context.pop(-1)
            file_context.pop(-1)

            if len(file_context) > 0:
                lst_unpacked.append(build_context(info, file_context))
                info = new_info(INFO, lineno, fn_new, newtype, newhash)
                file_context = []
            continue

        file_context.append(line)
        lineno += 1

    if len(file_context) > 0:
        lst_unpacked.append(build_context(info, file_context))

    for db in lst_unpacked:
        print('%s:%d %d-%d' % (sys.argv[1], db['lineno'], db['range'][0], db['range'][1]))
        print('File: %s' % db['filename'])
        print('MD5 : %s %s parsed' % (db['type'], db['oldhash']))
        print('MD5 : %s %s calculated' % (db['type'], db['hash']))
        print()

    if len(sys.argv) == 3:
        target_dir  = sys.argv[2]
        for db in lst_unpacked:
            fn = os.path.join(target_dir, db['filename'])
            file_put_contents(fn, db['text'])
            print('wrote %s' % fn)
        print()

    l = []
    for db in lst_unpacked:
        if db['filename'].startswith('amalgamation'):
            l.append(db['text'])
        else:
            l.append(HEADER % db)
            l.append(db['text'])
    l.append('')

    fn = source_fn + '.__regenerate__.c'
    print('wrote %s' % fn)
    file_put_contents(fn, '\n'.join(l))



