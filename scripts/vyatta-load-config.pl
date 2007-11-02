#!/usr/bin/perl
# Perl script for loading config file at run time.
# $0: config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigLoad;

my $etcdir = $ENV{vyatta_sysconfdir};
my $sbindir = $ENV{vyatta_sbindir};
my $bootpath = '';
if (-r "$etcdir/bootfile_path") {
  $bootpath = `cat $etcdir/bootfile_path`;
}
$bootpath =~ s/\/[^\/]+$//;

if ($#ARGV != 0) {
  print "Usage: load <config_file_name>\n";
  exit 1;
}

my $load_file = $ARGV[0];
if (!($load_file =~ /^\//)) {
  # relative path
  $load_file = "$bootpath/$load_file";
}

print "Loading config file $load_file...\n";
my %cfg_hier = VyattaConfigLoad::loadConfigHierarchy($load_file);
if (scalar(keys %cfg_hier) == 0) {
  print "Load failed\n";
  exit 1;
}

my %cfg_diff = VyattaConfigLoad::getConfigDiff(\%cfg_hier);

my @delete_list = @{$cfg_diff{'delete'}};
my @set_list = @{$cfg_diff{'set'}};

foreach (@delete_list) {
  my ($cmd_ref, $rank) = @{$_};
  my @cmd = ( "$sbindir/my_delete", @{$cmd_ref} );
  my $cmd_str = join ' ', @cmd;
  system("$cmd_str");
  if ($? >> 8) {
    $cmd_str =~ s/^$sbindir\/my_//;
    print "\"$cmd_str\" failed\n";
  }
}

foreach (@set_list) {
  my ($cmd_ref, $rank) = @{$_};
  my @cmd = ( "$sbindir/my_set", @{$cmd_ref} );
  my $cmd_str = join ' ', @cmd;
  system("$cmd_str");
  if ($? >> 8) {
    $cmd_str =~ s/^$sbindir\/my_//;
    print "\"$cmd_str\" failed\n";
  }
}

system("$sbindir/my_commit");
if ($? >> 8) {
  print "Load failed (commit failed)\n";
  exit 1;
}

print "Done\n";
exit 0;

