# Copyright (c) 2016 VyOS maintainers and contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software
# is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included 
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
# OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
#  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import subprocess
import re

__cli_shell_api = '/bin/cli-shell-api'

class VyOSError(Exception):
    pass


def _make_command(op, path):
    args = path.split()
    cmd = [__cli_shell_api, op] + args
    return cmd

def _run(cmd):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    out = p.stdout.read()
    p.wait()
    if p.returncode != 0:
        raise VyOSError()
    else:
        return out
    

def exists(path):
    try:
        _run(_make_command('exists', path))
        return True
    except VyOSError:
        return False

def session_changed():
    try:
        _run(_make_command('sessionChanged', ''))
        return True
    except VyOSError:
        return False

def in_session():
    try:
        _run(_make_command('inSession', ''))
        return True
    except VyOSError:
        return False

def is_multi(path):
    try:
        _run(_make_command('isMulti', path))
        return True
    except VyOSError:
        return False

def is_tag(path):
    try:
        _run(_make_command('isTag', path))
        return True
    except VyOSError:
        return False

def is_leaf(path):
    try:
        _run(_make_command('isLeaf', path))
        return True
    except VyOSError:
        return False

def return_value(path):
    if is_multi(path):
        raise VyOSError("Cannot use return_value on multi node: {0}".format(path))
    elif not is_leaf(path):
        raise VyOSError("Cannot use return_value on non-leaf node: {0}".format(path))
    else:
        try:
            out = _run(_make_command('returnValue', path))
            return out
        except VyOSError:
            raise VyOSError("Path doesn't exist: {0}".format(path))

def return_values(path):
    if not is_multi(path):
        raise VyOSError("Cannot use return_values on non-multi node: {0}".format(path))
    elif not is_leaf(path):
        raise VyOSError("Cannot use return_values on non-leaf node: {0}".format(path))
    else:
        try:
            out = _run(_make_command('returnValues', path))
            return out
        except VyOSError:
            raise VyOSError("Path doesn't exist: {0}".format(path))

def list_nodes(path):
    if is_tag(path):
        try:
            out = _run(_make_command('listNodes', path))
            values = out.split()
            return list(map(lambda x: re.sub(r'^\'(.*)\'$', r'\1',x), values))
        except VyOSError:
            raise VyOSError("Path doesn't exist: {0}".format(path)) 
    else:
        raise VyOSError("Cannot use list_nodes on a non-tag node: {0}".format(path))
