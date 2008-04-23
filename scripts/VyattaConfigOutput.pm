#!/usr/bin/perl

# Author: An-Cheng Huang <ancheng@vyatta.com>
# Date: 2007
# Description: Perl module for generating output of the configuration.

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


# outputNewConfig()
#   prints the "new" config, i.e., the active config with any un-committed
#   changes. 'diff' notation is also generated to indicate the changes.
#
# outputActiveConfig()
#   prints the "active" config. suitable for "saving", for example.

package VyattaConfigOutput;

use strict;
use lib '/opt/vyatta/share/perl5/';
use VyattaConfig;
use Sort::Versions;

# whether to show default values
my $show_all = 0;
sub set_show_all {
  if (shift) {
    $show_all = 1;
  }
}

my $hide_password = 0;
sub set_hide_password {
  if (shift) {
    $hide_password = 1;
  }
}

sub txt_need_quotes {
  $_ = shift;
  return 1 if (/^$/ || /[\s\*}{;]/);
  return 0;
}

my $config = undef;

# $0: array ref for path
# $1: display prefix
# $2: node name
# $3: simple show (if defined, don't show diff prefix. used for "don't show as
#     deleted" from displayDeletedOrigChildren.)
sub displayValues {
  my @cur_path = @{$_[0]};
  my $prefix = $_[1];
  my $name = $_[2];
  my $simple_show = $_[3];
  my ($is_multi, $is_text, $default) = $config->parseTmpl(\@cur_path);
  if ($is_text) {
    $default =~ /^"(.*)"$/;
    my $txt = $1;
    if (!txt_need_quotes($txt)) {
      $default = $txt;
    }
  }
  my $is_password = ($name =~ /^.*password$/);
  my $HIDE_PASSWORD = '****************';
  $config->setLevel(join ' ', @cur_path);
  if ($is_multi) {
    my @ovals = $config->returnOrigValues('');
    my @nvals = $config->returnValues('');
    if ($is_text) {
      @ovals = map { (txt_need_quotes($_)) ? "\"$_\"" : "$_"; } @ovals;
      @nvals = map { (txt_need_quotes($_)) ? "\"$_\"" : "$_"; } @nvals;
    }
    my $idx = 0;
    my %ohash = map { $_ => ($idx++) } @ovals;
    $idx = 0;
    my %nhash = map { $_ => ($idx++) } @nvals;
    my @dlist = map { if (!defined($nhash{$_})) { $_; } else { undef; } }
                    @ovals;
    if (defined($simple_show)) {
      foreach my $oval (@ovals) {
        if ($is_password && $hide_password) {
          $oval = $HIDE_PASSWORD;
        }
        print "$prefix$name $oval\n";
      }
      return;
    }
    foreach my $del (@dlist) {
      if (defined($del)) {
        if ($is_password && $hide_password) {
          $del = $HIDE_PASSWORD;
        }
        print "-$prefix$name $del\n";
      }
    }
    foreach my $nval (@nvals) {
      my $diff = '+';
      if (defined($ohash{$nval})) {
        if ($ohash{$nval} != $nhash{$nval}) {
          $diff = '>';
        } else {
          $diff = ' ';
        }
      }
      if ($is_password && $hide_password) {
        $nval = $HIDE_PASSWORD;
      }
      print "$diff$prefix$name $nval\n";
    }
  } else {
    my $oval = $config->returnOrigValue('');
    my $nval = $config->returnValue('');
    if ($is_text) {
      if (defined($oval) && txt_need_quotes($oval)) {
        $oval = "\"$oval\"";
      }
      if (defined($nval) && txt_need_quotes($nval)) {
        $nval = "\"$nval\"";
      }
    }
    if (defined($simple_show)) {
      if (!defined($default) || $default ne $oval || $show_all) {
        if ($is_password && $hide_password) {
          $oval = $HIDE_PASSWORD;
        }
        print "$prefix$name $oval\n";
      }
      return;
    }
    my $value = $nval;
    my $diff = ' ';
    if (!defined($oval) && defined($nval)) {
      $diff = '+';
    } elsif (!defined($nval) && defined($oval)) {
      $diff = '-';
      $value = $oval;
    } else {
      # both must be defined
      if ($oval ne $nval) {
        $diff = '>';
      }
    }
    if (!defined($default) || $default ne $value || $show_all) {
      if ($is_password && $hide_password) {
        $value = $HIDE_PASSWORD;
      }
      print "$diff$prefix$name $value\n";
    }
  }
}

# $0: array ref for path
# $1: display prefix
# $2: don't show as deleted? (if defined, config is shown as normal instead of
#     deleted.)
sub displayDeletedOrigChildren {
  my @cur_path = @{$_[0]};
  my $prefix = $_[1];
  my $dont_show_as_deleted = $_[2];
  my $dprefix = '-';
  if (defined($dont_show_as_deleted)) {
    $dprefix = '';
  }
  $config->setLevel('');
  my @children = $config->listOrigNodes(join ' ', @cur_path);
  for my $child (sort @children) {
    if ($child eq 'node.val') {
      # should not happen!
      next;
    }
    my $is_tag = $config->isTagNode([ @cur_path, $child ]);
    $config->setLevel(join ' ', (@cur_path, $child));
    my @cnames = sort $config->listOrigNodes();
    if ($#cnames == 0 && $cnames[0] eq 'node.val') {
      displayValues([ @cur_path, $child ], $prefix, $child,
                    $dont_show_as_deleted);
    } elsif ($cnames[0] eq 'def') {
	#ignore
    } elsif (scalar($#cnames) >= 0) {
      if ($is_tag) {
        @cnames = sort versioncmp @cnames;
        foreach my $cname (@cnames) {
          if ($cname eq 'node.val') {
            # should not happen
            next;
          }
          print "$dprefix$prefix$child $cname {\n";
          displayDeletedOrigChildren([ @cur_path, $child, $cname ],
                                     "$prefix    ", $dont_show_as_deleted);
          print "$dprefix$prefix}\n";
        }
      } else {
        print "$dprefix$prefix$child {\n";
        displayDeletedOrigChildren([ @cur_path, $child ], "$prefix    ",
                                   $dont_show_as_deleted);
        print "$dprefix$prefix}\n";
      }
    } else {
      my $has_tmpl_children = $config->hasTmplChildren([ @cur_path, $child ]);
      print "$dprefix$prefix$child"
            . ($has_tmpl_children ? " {\n$dprefix$prefix}\n" : "\n");
    }
  }
}

# $0: hash ref for children status
# $1: array ref for path
# $2: display prefix
sub displayChildren {
  my %child_hash = %{$_[0]};
  my @cur_path = @{$_[1]};
  my $prefix = $_[2];
  for my $child (sort (keys %child_hash)) {
    if ($child eq 'node.val') {
      # should not happen!
      next;
    }
    my ($diff, $vdiff) = (' ', ' ');
    if ($child_hash{$child} eq 'added') {
      $diff = '+';
      $vdiff = '+';
    } elsif ($child_hash{$child} eq 'deleted') {
      $diff = '-';
      $vdiff = '-';
    } elsif ($child_hash{$child} eq 'changed') {
      $vdiff = '>';
    }
    my $is_tag = $config->isTagNode([ @cur_path, $child ]);
    $config->setLevel(join ' ', (@cur_path, $child));
    my %cnodes = $config->listNodeStatus();
    my @cnames = sort keys %cnodes;
    if ($#cnames == 0 && $cnames[0] eq 'node.val') {
      displayValues([ @cur_path, $child ], $prefix, $child);
    } elsif ($cnames[0] eq 'def') {
	#skip
    } elsif (scalar($#cnames) >= 0) {
      if ($is_tag) {
        @cnames = sort versioncmp @cnames;
        foreach my $cname (@cnames) {
          if ($cname eq 'node.val') {
            # should not happen
            next;
          }
          my $tdiff = ' ';
          if ($cnodes{$cname} eq 'deleted') {
            $tdiff = '-';
          } elsif ($cnodes{$cname} eq 'added') {
            $tdiff = '+';
          }
          print "$tdiff$prefix$child $cname {\n";
          if ($cnodes{$cname} eq 'deleted') {
            displayDeletedOrigChildren([ @cur_path, $child, $cname ],
                                       "$prefix    ");
          } else {
            $config->setLevel(join ' ', (@cur_path, $child, $cname));
            my %ccnodes = $config->listNodeStatus();
            displayChildren(\%ccnodes, [ @cur_path, $child, $cname ],
                            "$prefix    ");
          }
          print "$tdiff$prefix}\n";
        }
      } else {
        print "$diff$prefix$child {\n";
        if ($child_hash{$child} eq 'deleted') {
          # this should not happen
          displayDeletedOrigChildren([ @cur_path, $child ], "$prefix    ");
        } else {
          displayChildren(\%cnodes, [ @cur_path, $child ], "$prefix    ");
        }
        print "$diff$prefix}\n";
      }
    } else {
      if ($child_hash{$child} eq 'deleted') {
        $config->setLevel('');
        my @onodes = $config->listOrigNodes(join ' ', (@cur_path, $child));
        if ($#onodes == 0 && $onodes[0] eq 'node.val') {
          displayValues([ @cur_path, $child ], $prefix, $child);
	} elsif ($onodes[0] eq 'def') {
	    #skip this
        } else {
          print "$diff$prefix$child {\n";
          displayDeletedOrigChildren([ @cur_path, $child ], "$prefix    ");
          print "$diff$prefix}\n";
        }
      } else {
        my $has_tmpl_children
          = $config->hasTmplChildren([ @cur_path, $child ]);
        print "$diff$prefix$child"
              . ($has_tmpl_children ? " {\n$diff$prefix}\n" : "\n");
      }
    }
  }
}

# @ARGV: represents the 'root' path. the output starts at this point under
#        the new config.
sub outputNewConfig {
  $config = new VyattaConfig;
  $config->setLevel(join ' ', @_);
  my %rnodes = $config->listNodeStatus();
  if (scalar(keys %rnodes) > 0) {
    my @rn = keys %rnodes;
    if ($#rn == 0 && $rn[0] eq 'node.val') {
      # this is a leaf value-node
      displayValues([ @_ ], '', $_[$#_]);
    } elsif ($rn[0] eq 'def') {
      #skip
    } else {
      displayChildren(\%rnodes, [ @_ ], '');
    }
  } else {
    if (defined($config->existsOrig()) && !defined($config->exists())) {
      # this is a deleted node
      print 'Configuration under "' . (join ' ', @_) . "\" has been deleted\n";
    } elsif (!defined($config->getTmplPath(\@_))) {
      print "Specified configuration path is not valid\n";
    } else {
      print 'Configuration under "' . (join ' ', @_) . "\" is empty\n";
    }
  }
}

# @ARGV: represents the 'root' path. the output starts at this point under
#        the active config.
sub outputActiveConfig {
  $config = new VyattaConfig;
  $config->setLevel(join ' ', @_);
  displayDeletedOrigChildren([ @_ ], '', 1);
}

1;
