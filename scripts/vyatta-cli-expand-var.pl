#!/usr/bin/perl

use strict;
use lib "/opt/vyatta/share/perl5/";
use VyattaConfig;

# expand a variable reference
if ($#ARGV != 0) {
  print STDERR "usage: vyatta-cli-expand-var.pl '<var-ref>'\n";
  exit 1;
}

$_ = $ARGV[0];

# basic format check:
# '(' ')' not allowed in reference.
# only allow absolute path for now.
if (!/^\$\(\/([^()]+)\)$/) {
  print STDERR "invalid variable reference (invalid format)\n";
  exit 1;
}
$_ = $1;

my $multi_val = 1;
if (s/^(.*)\/\@\@$/$1/) {
  # return list of multi-node values
  $multi_val = 1;
} elsif (s/^(.*)\/\@$/$1/) {
  # return single value
  $multi_val = 0;
} else {
  # only allow the above 2 forms for now.
  print STDERR "invalid variable reference (invalid value specification)\n";
  exit 1;
}

if (/\@/) {
  # '@' not allowed anywhere else in the reference for now.
  print STDERR "invalid variable reference (extra value specification)\n";
  exit 1;
}

my $config = new VyattaConfig;
my $path_str = join ' ', (split /\//);
my $val_str = "";
if ($multi_val) {
  my @tmp = $config->returnOrigValues($path_str);
  if (scalar(@tmp) > 0) {
    # we got multiple values back
    $val_str = join ' ', @tmp;
  } else {
    # this node may be a 'tag' node. try listing children.
    $config->setLevel($path_str);
    @tmp = $config->listOrigNodes();
    $val_str = join ' ', @tmp;
  }
} else {
  $val_str = $config->returnOrigValue($path_str);
}

# expanded string is printed on stdout (multiple values separated by ' ').
print "$val_str";
exit 0;

