#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfigOutput;

my $etcdir = "/opt/vyatta/etc";
my $bootpath = $etcdir . "/config";
my $save_file = $bootpath . "/config.boot";

if ($#ARGV > 0) {
  print "Usage: save [config_file_name]\n";
  exit 1;
}

if (defined($ARGV[0])) {
  $save_file = $ARGV[0];
  if (!($save_file =~ /^\//)) {
    # relative path
    $save_file = "$bootpath/$save_file";
  }
}

# this overwrites the file if it exists. we could create a backup first.
if (! open(SAVE, ">$save_file")) {
  print "Cannot open file '$save_file': $!\n";
  exit 1;
}

print "Saving configuration to '$save_file'...";
select SAVE;
VyattaConfigOutput::set_show_all(1);
VyattaConfigOutput::outputActiveConfig();
my $version_str = `/opt/vyatta/sbin/vyatta_current_conf_ver.pl`;
print SAVE $version_str;
select STDOUT;
print "\nDone\n";
close SAVE;
exit 0;

