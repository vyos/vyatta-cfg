#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaTypeChecker;

# validate a value of a specific type
if ($#ARGV < 1) {
  print "usage: vyatta-validate-type.pl [-q] <type> <value>\n";
  exit 1;
}

my $quiet = undef;
if ($ARGV[0] eq '-q') {
  shift;
  $quiet = 1;
}

exit 0 if (VyattaTypeChecker::validateType($ARGV[0], $ARGV[1], $quiet));
exit 1;

