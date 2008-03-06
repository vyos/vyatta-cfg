#!/usr/bin/perl
# Perl script for loading the startup config file.
# $0: startup config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigLoad;

umask 0002;

if (!open(OLDOUT, ">&STDOUT") || !open(OLDERR, ">&STDERR")
    || !open(STDOUT, "|/usr/bin/logger -t config-loader -p local0.debug")
    || !open(STDERR, ">&STDOUT")) {
  print STDERR "Cannot dup STDOUT/STDERR: $!\n";
  exit 1;
}
    
if (!open(WARN, "|/usr/bin/logger -t config-loader -p local0.warning")) {
  print OLDERR "Cannot open syslog: $!\n";
  exit 1;
}

sub restore_fds {
  open(STDOUT, ">&OLDOUT");
  open(STDERR, ">&OLDERR");
}

# get a list of all config statement in the startup config file
# (sorted by rank).
my @all_nodes = VyattaConfigLoad::getStartupConfigStatements($ARGV[0]);
if (scalar(@all_nodes) == 0) {
  # no config statements
  restore_fds();
  exit 1;
}
my $cur_rank = ${$all_nodes[0]}[1];

# set up the config environment
my $CWRAPPER = '/opt/vyatta/sbin/vyatta-cfg-cmd-wrapper';
system("$CWRAPPER begin");
if ($? >> 8) {
  print OLDOUT "Cannot set up configuration environment\n";
  print WARN "Cannot set up configuration environment\n";
  restore_fds();
  exit 1;
}

my $commit_cmd = "$CWRAPPER commit";
my $cleanup_cmd = "$CWRAPPER cleanup";
my $ret = 0;
# higher-ranked statements committed before lower-ranked.
foreach (@all_nodes) {
  my ($path_ref, $rank) = @$_;
  if ($rank != $cur_rank) {
    # commit all nodes with the same rank together.
    $ret = system("$commit_cmd");
    if ($ret >> 8) {
      print OLDOUT "Commit failed at rank $cur_rank\n";
      print WARN "Commit failed at rank $cur_rank\n";
      system("$cleanup_cmd");
      # continue after cleanup (or should we abort?)
    }
    $cur_rank = $rank;
  }
  my $cmd = "$CWRAPPER set " . (join ' ', @$path_ref);
  $ret = system("$cmd");
  if ($ret >> 8) {
    $cmd =~ s/^.*?set /set /;
    print OLDOUT "[[$cmd]] failed\n";
    print WARN "[[$cmd]] failed\n";
    # continue after set failure (or should we abort?)
  }
}
$ret = system("$commit_cmd");
if ($ret >> 8) {
  print OLDOUT "Commit failed at rank $cur_rank\n";
  print WARN "Commit failed at rank $cur_rank\n";
  system("$cleanup_cmd");
  # exit normally after cleanup (or should we exit with error?)
}

# really clean up
system("$CWRAPPER end");
restore_fds();

exit 0;

