# Author: John Southworth <john.southworth@vyatta.com>
# Date: 2012
# Description: vyatta ioctl functions

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
# Portions created by Vyatta are Copyright (C) 2008 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

package Vyatta::ioctl;

use strict;
use warnings;
use Socket;
use Socket6;

require 'sys/ioctl.ph';

our @EXPORT = qw(get_terminal_size get_interface_flags);
use base qw(Exporter);


# returns (rows, columns) for terminal size;
sub get_terminal_size {
    # undefined if not terminal attached
    open(my $TTY, '>', '/dev/tty')
	or return;

    my $winsize = '';
    # undefined if output not going to terminal
    return unless (ioctl($TTY, &TIOCGWINSZ, $winsize));
    close($TTY);

    my ($rows, $cols, undef, undef) = unpack('S4', $winsize);
    return ($rows, $cols);
}

#Do SIOCGIFFLAGS ioctl in perl
sub get_interface_flags {
    my $name  = shift;

    socket (my $sock, AF_INET, SOCK_DGRAM, 0)
	or die "open UDP socket failed: $!";

    my $ifreq = pack('a16', $name);
    ioctl($sock, &SIOCGIFFLAGS, $ifreq)
	or return; #undef

    my (undef, $flags) = unpack('a16s', $ifreq);
    return $flags;

}

1;
