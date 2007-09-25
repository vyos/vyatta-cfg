#!/usr/bin/perl
# Perl script for loading the startup config file.
# $0: startup config file.

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigLoad;

# get a list of all config statement in the startup config file
# (sorted by rank).
my @all_nodes = VyattaConfigLoad::getStartupConfigStatements($ARGV[0]);
if (scalar(@all_nodes) == 0) {
  # no config statements
  exit 1;
}
my $cur_rank = ${$all_nodes[0]}[1];
my $commit_cmd = '/opt/vyatta/sbin/xorp_tmpl_tool commit';
my $cleanup_cmd = '/opt/vyatta/sbin/xorp_tmpl_tool cleanup';
my $ret = 0;
# higher-ranked statements committed before lower-ranked.
foreach (@all_nodes) {
  my ($path_ref, $rank) = @$_;
  if ($rank != $cur_rank) {
    # commit all nodes with the same rank together.
    $ret = system("$commit_cmd");
    if ($ret >> 8) {
      print STDERR "Commit failed at rank $cur_rank\n";
      system("$cleanup_cmd");
      # continue after cleanup (or should we abort?)
    }
    $cur_rank = $rank;
  }
  my $cmd = '/opt/vyatta/sbin/xorp_tmpl_tool set ' . (join ' ', @$path_ref);
  $ret = system("$cmd");
  if ($ret >> 8) {
    $cmd =~ s/^.*?set /set /;
    print STDERR "[[$cmd]] failed\n";
    # continue after set failure (or should we abort?)
  }
}
$ret = system("$commit_cmd");
if ($ret >> 8) {
  print STDERR "Commit failed at rank $cur_rank\n";
  system("$cleanup_cmd");
  # exit normally after cleanup (or should we exit with error?)
}

# really clean up
system('/opt/vyatta/sbin/xorp_tmpl_tool end_loading');

exit 0;
