# Module: File.pm
# File manipulation functions

# **** License ****
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2010 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

package Vyatta::File;
use strict;
use warnings;

our @EXPORT = qw(touch mkdir_p rm_rf);
our @EXPORT_OK = qw(show_error);
use base qw(Exporter);

use Fcntl;
use File::Path qw(make_path remove_tree);

# Change file time stamps
# if file does not exist, it is created empty
sub touch {
    my $file = shift;
    my $t = time;

    sysopen (my $f, $file, O_RDWR|O_CREAT)
	or die "Can't touch $file: $!";
    close $f;
    utime $t, $t, $file;
}

# like mkdir -p
# Wrapper of File::Path:make_tree
sub mkdir_p {
    my $path = shift;
    my $err;

    make_path($path, { error => \$err } );
    
    return @$err;
}

# like rm -rf
# returns an array of errors if any (see File::Path)
sub rm_rf {
    my $path = shift;
    my $err;

    remove_tree($path,  { error => \$err } );

    return @$err;
}

sub show_error {
    for my $diag (@_) {
	my ($f, $msg) = %$diag;
	warn "$f: $msg\n";
    }
}

1;
