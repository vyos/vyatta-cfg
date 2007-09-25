#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaTypeChecker;

# find the type of a value (from a list of candidates)
if ($#ARGV < 1) {
  print "usage: vyatta-find-type.pl <value> <type> [<type> ...]\n";
  exit 1;
}

if (my $type = VyattaTypeChecker::findType(@ARGV)) {
  # type found
  print "$type";
  exit 0;
}

# value not valid for any of the candidates
exit 1;

