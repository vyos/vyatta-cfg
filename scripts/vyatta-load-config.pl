#!/usr/bin/perl

# Author: Vyatta <eng@vyatta.com>
# Date: 2007
# Description: Perl script for loading config file at run time.

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
# Portions created by Vyatta are Copyright (C) 2006, 2007, 2008 Vyatta, Inc.
# All Rights Reserved.
# **** End License ****

# $0: config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use POSIX;
use IO::Prompt;
use Getopt::Long;
use Sys::Syslog qw(:standard :macros);
use Vyatta::Config;
use Vyatta::ConfigLoad;
use Vyatta::Misc qw(get_short_config_path);

$SIG{'INT'} = 'IGNORE';

my $etcdir       = $ENV{vyatta_sysconfdir};
my $sbindir      = $ENV{vyatta_sbindir};
my $bootpath     = $etcdir . "/config";
my $load_file    = $bootpath . "/config.boot";
my $url_tmp_file = $bootpath . "/config.boot.$$";


#
# Note: to get merge to work on arbitrary nodes
# within the configuration multinodes need to be escaped.
# i.e.:
# load --merge='load-balancing/wan/interface-health\ eth0'
#
# will start loading of the configuration node from:
#
# load-balancing/wan/interface-health:eth0
#
# Note current loading is limited to first new
# multinode.
#
sub usage {
    print "Usage: $0 --merge=<root>\n";
    exit 0;
}

my $merge;
GetOptions(
    "merge:s"              => \$merge,
    ) or usage();

my $mode = 'local';
my $proto;

if ( defined( $ARGV[0] ) ) {
    $load_file = $ARGV[0];
}
my $orig_load_file = $load_file;

if ( $load_file =~ /^[^\/]\w+:\// ) {
    if ( $load_file =~ /^(\w+):\/\/\w/ ) {
        $mode  = 'url';
        $proto = lc($1);
        unless( $proto eq 'tftp' ||
		$proto eq 'ftp'  ||
		$proto eq 'http' ||
		$proto eq 'scp' ) {
	    die "Invalid url protocol [$proto]\n";
        }
    } else {
        print "Invalid url [$load_file]\n";
        exit 1;
    }
}

if ( $mode eq 'local' and !( $load_file =~ /^\// ) ) {
    # relative path
    $load_file = "$bootpath/$load_file";
}

my $cfg;
if ( $mode eq 'local' ) {
    die "Cannot open configuration file $load_file: $!\n"
	unless open( $cfg, '<', $load_file);
}
elsif ( $mode eq 'url' ) {
    if ( !-f '/usr/bin/curl' ) {
        print "Package [curl] not installed\n";
        exit 1;
    }
    if ( $proto eq 'http' ) {

        #
        # error codes are send back in html, so 1st try a header
        # and look for "HTTP/1.1 200 OK"
        #
        my $rc = `curl -q -I $load_file 2>&1`;
        if ( $rc =~ /HTTP\/\d+\.?\d\s+(\d+)\s+(.*)$/mi ) {
            my $rc_code   = $1;
            my $rc_string = $2;
            if ( $rc_code == 200 ) {

                # good resonse
            }
            else {
                print "http error: [$rc_code] $rc_string\n";
                exit 1;
            }
        }
        else {
            print "Error: $rc\n";
            exit 1;
        }
    }
    my $rc = system("curl -# -o $url_tmp_file $load_file");
    if ($rc) {
        print "Can not open remote configuration file $load_file\n";
        exit 1;
    }
    die "Cannot open configuration file $load_file: $!\n"
	unless open( $cfg, '<', $url_tmp_file);

    $load_file = $url_tmp_file;
}

my $xorp_cfg  = 0;
my $valid_cfg = 0;
while (<$cfg>) {
    if (/\/\*XORP Configuration File, v1.0\*\//) {
        $xorp_cfg = 1;
        last;
    }
    elsif (/vyatta-config-version/) {
        $valid_cfg = 1;
        last;
    }
}
if ( $xorp_cfg or !$valid_cfg ) {
    if ($xorp_cfg) {
        print "Warning: Loading a pre-Glendale configuration.\n";
    }
    else {
        print "Warning: file does NOT appear to be a valid config file.\n";
    }
    if ( !prompt( "Do you want to continue? ", -tty, -Yes, -default => 'no' ) )
    {
        print "Configuration not loaded\n";
        exit 1;
    }
}
close $cfg;

# log it
openlog( $0, "", LOG_USER );
my $login = getlogin() || getpwuid($<) || "unknown";
syslog( "warning", "Load config [$orig_load_file] by $login" );

# do config migration
system("$sbindir/vyatta_config_migrate.pl $load_file");

# note: "load" is now handled in the backend so only "merge" is actually
# handled in this script. "merge" hasn't been moved into the backend since
# the command itself needs to be revisited after mendocino time frame.

# when presenting to users, show shortened /config path
my $shortened_load_file = get_short_config_path($load_file);
print "Loading configuration from '$shortened_load_file'...\n";

my $cobj = new Vyatta::Config;
if (!defined($merge)) {
  # "load" => use backend through API
  $cobj->loadFile($load_file);
} else {
  # "merge" => handled here
  my %cfg_hier = Vyatta::ConfigLoad::loadConfigHierarchy($load_file,$merge);
  if ( scalar( keys %cfg_hier ) == 0 ) {
    print "The specified file does not contain any configuration.\n";
    print
      "Do you want to remove everything in the running configuration? [no] ";
    my $resp = <STDIN>;
    if ( !( $resp =~ /^yes$/i ) ) {
      print "Configuration not loaded\n";
      exit 1;
    }
  }

  my %cfg_diff = Vyatta::ConfigLoad::getConfigDiff(\%cfg_hier);
  my @set_list    = @{ $cfg_diff{'set'} };
  foreach (@set_list) {
    my ( $cmd_ref, $rank ) = @{$_};
    my @cmd = ( "$sbindir/my_set", @{$cmd_ref} );
    my $cmd_str = join ' ', @cmd;
    system("$cmd_str");
    if ( $? >> 8 ) {
      $cmd_str =~ s/^$sbindir\/my_//;
      print "\"$cmd_str\" failed\n";
    }
  }
}

if (!($cobj->sessionChanged())) {
  print "No configuration changes to commit\n";
  exit 0;
}

print ("\n" . (defined($merge) ? 'Merge' : 'Load')
       . " complete.  Use 'commit' to make changes active.\n");
exit 0;
