#!/usr/bin/perl
#
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
# A copy of the GNU General Public License is available as
# `/usr/share/common-licenses/GPL' in the Debian GNU/Linux distribution
# or on the World Wide Web at `http://www.gnu.org/copyleft/gpl.html'.
# You can also obtain it by writing to the Free Software Foundation,
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
# MA 02110-1301, USA.
#
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2010 Vyatta, Inc.
# All Rights Reserved.
#
# Author: Deepti Kulkarni	 
# Date: Feb 2012
# Description: Script to log active configuration commits to syslog.
#
# **** End License ****
#


use strict;
use warnings;
use lib '/opt/vyatta/share/perl5/';

use Vyatta::Config;
use Sys::Syslog qw(:standard :macros);

#
# main
#
my $commit_status;
my $cur_tty=`ps -o tty= |head -n1`;
my $cur_user=`id -un`;
my $status = $ENV{'COMMIT_STATUS'};

chomp($cur_tty);
chomp($cur_user);

$commit_status = 'Successful' if ($status eq 'SUCCESS');
#open log for logging commit details
if (defined $commit_status) {
  openlog("commit", "", LOG_USER);
  syslog ("alert", "$commit_status change to active configuration by user $cur_user on $cur_tty");
  closelog();
}
#end of script
