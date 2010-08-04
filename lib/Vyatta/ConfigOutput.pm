# Author: Vyatta <eng@vyatta.com>
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

package Vyatta::ConfigOutput;
use strict;

our @EXPORT = qw(set_show_all set_hide_password outputActiveConfig outputNewConfig);
use base qw(Exporter);

use lib '/opt/vyatta/share/perl5';
use Vyatta::Config;

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
  my $dis = $_[1];
  my $prefix = $_[2];
  my $name = $_[3];
  my $simple_show = $_[4];

  $config->setLevel(join ' ', @cur_path);
  my ($is_multi, $is_text, $default) = $config->parseTmpl();
  if ($is_text) {
    $default =~ /^"(.*)"$/;
    my $txt = $1;
    if (!txt_need_quotes($txt)) {
      $default = $txt;
    }
  }
  my $is_password = ($name =~ /^.*(passphrase|password|pre-shared-secret|key)$/);

  my $HIDE_PASSWORD = '****************';

  if ($is_multi) {
    my @ovals = $config->returnOrigValuesDA();
    my @nvals = $config->returnValuesDA();
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
        print "$dis$prefix$name $oval\n";
      }
      return;
    }
    foreach my $del (@dlist) {
      if (defined($del)) {
        if ($is_password && $hide_password) {
          $del = $HIDE_PASSWORD;
        }
        print "$dis-$prefix$name $del\n";
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
      print "$dis$diff$prefix$name $nval\n";
    }
  } else {
    if ($config->isDefault() and !$show_all) {
      # not going to show anything so just return
      return;
    }

    my $oval = $config->returnOrigValueDA();
    my $nval = $config->returnValueDA();
    if ($is_text) {
      if (defined($oval) && txt_need_quotes($oval)) {
        $oval = "\"$oval\"";
      }
      if (defined($nval) && txt_need_quotes($nval)) {
        $nval = "\"$nval\"";
      }
    }

    if (defined($simple_show)) {
      if ($is_password && $hide_password) {
        $oval = $HIDE_PASSWORD;
      }
      print "$dis$prefix$name $oval\n";
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
    if ($is_password && $hide_password) {
      $value = $HIDE_PASSWORD;
    }
    print "$dis$diff$prefix$name $value\n";
  }
}

# $0: array ref for path
# $1: display prefix
# $2: don't show as deleted? (if defined, config is shown as normal instead of
#     deleted.)
sub displayDeletedOrigChildren {
  my @cur_path = @{$_[0]};
  my $dis = $_[1];
  my $prefix = $_[2];
  my $dont_show_as_deleted = $_[3];
  my $dprefix = '-';
  if (defined($dont_show_as_deleted)) {
    $dprefix = '';
  }

  $config->setLevel('');
  my @children = $config->listOrigNodesDA(join(' ', @cur_path));
  for my $child (sort @children) {
    # reset level
    $config->setLevel('');
    my $is_tag = $config->isTagNode(join(' ', @cur_path, $child));

    if (!$is_tag) {
	my $path = join(' ',( @cur_path, $child ));
	my $comment = $config->returnComment($path);
	if (defined $comment) {
	    print "$prefix /* $comment */\n";
	}

        # check deactivate state
        my $de_working = $config->deactivated($path);
        my $de_active = $config->deactivatedOrig($path);
        if ($de_active) {
          if ($de_working) {
            # deactivated in both
            $dis = '! ';
          } else {
            # deactivated only in active
            $dis = '! ';
          }
        } else {
          if ($de_working) {
            # deactivated only in working
            if (defined($dont_show_as_deleted)) {
              $dis = '  ';
            } else {
              $dis = 'D ';
            }
          } else {
            # deactivated in neither
            $dis = '  ';
          }
        }
    }

    $config->setLevel(join ' ', (@cur_path, $child));
    if ($config->isLeafNode()) {
      displayValues([ @cur_path, $child ], $dis, $prefix, $child,
                    $dont_show_as_deleted);
      next;
    }

    # not a leaf node
    my @cnames = sort versioncmp ($config->listOrigNodesDA());
    if (scalar(@cnames) > 0) {
      if ($is_tag) {
        foreach my $cname (@cnames) {
	  my $path = join(' ',( @cur_path, $child, $cname ));
          $config->setLevel($path);

	  my $comment = $config->returnComment();
	  if (defined $comment) {
	      print "$prefix /* $comment */\n";
	  }

          # check deactivate state
          my $de_working = $config->deactivated();
          my $de_active = $config->deactivatedOrig();
          if ($de_active) {
            if ($de_working) {
              # deactivated in both
              $dis = '! ';
            } else {
              # deactivated only in active
              $dis = '! ';
            }
          } else {
            if ($de_working) {
              # deactivated only in working
              if (defined($dont_show_as_deleted)) {
                $dis = '  ';
              } else {
                $dis = 'D ';
              }
            } else {
              # deactivated in neither
              $dis = '  ';
            }
          }

          print "$dis$dprefix$prefix$child $cname {\n";
          displayDeletedOrigChildren([ @cur_path, $child, $cname ],
                                     $dis,"$prefix    ", $dont_show_as_deleted);
          print "$dis$dprefix$prefix}\n";
        }
      } else {
        print "$dis$dprefix$prefix$child {\n";
        displayDeletedOrigChildren([ @cur_path, $child ],$dis, "$prefix    ",
                                   $dont_show_as_deleted);
        print "$dis$dprefix$prefix}\n";
      }
    } else {
      my $has_tmpl_children = $config->hasTmplChildren();
      print "$dis$dprefix$prefix$child"
            . ($has_tmpl_children ? " {\n$dis$dprefix$prefix}\n" : "\n");
    }
  }
}

# $0: hash ref for children status
# $1: array ref for path
# $2: display prefix
sub displayChildren {
  my %child_hash = %{$_[0]};
  my @cur_path = @{$_[1]};
  my $dis = $_[2];
  my $prefix = $_[3];
  for my $child (sort (keys %child_hash)) {
    my $dis = "";
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

    $config->setLevel('');
    my $is_tag = $config->isTagNode(join(' ', @cur_path, $child));

    if (!$is_tag) {
	my $path = join(' ',( @cur_path, $child ));
	my $comment = $config->returnComment($path);
	if (defined $comment) {
	    print "$prefix /* $comment */\n";
	}

        # check deactivate state
        my $de_working = $config->deactivated($path);
        my $de_active = $config->deactivatedOrig($path);
        if ($de_active) {
          if ($de_working) {
            # deactivated in both
            $dis = '! ';
          } else {
            # deactivated only in active
            if ($child_hash{$child} eq 'deleted') {
              $dis = '! ';
            } else {
              $dis = 'A ';
            }
          }
        } else {
          if ($de_working) {
            # deactivated only in working
            $dis = 'D ';
          } else {
            # deactivated in neither
            $dis = '  ';
          }
        }
    }

    $config->setLevel(join ' ', (@cur_path, $child));
    if ($config->isLeafNode()) {
      displayValues([ @cur_path, $child ], $dis, $prefix, $child);
      next;
    }

    # not a leaf node
    my %cnodes = $config->listNodeStatusDA();
    my @cnames = sort keys %cnodes;
    if (scalar(@cnames) > 0) {
      if ($is_tag) {
        @cnames = sort versioncmp @cnames;
        foreach my $cname (@cnames) {
	  my $path = join(' ',( @cur_path, $child, $cname ));
	  $config->setLevel($path);
	  my $comment = $config->returnComment();
	  if (defined $comment) {
	      print "$prefix /* $comment */\n";
	  }

          # check deactivate state
          my $de_working = $config->deactivated();
          my $de_active = $config->deactivatedOrig();
          if ($de_active) {
            if ($de_working) {
              # deactivated in both
              $dis = '! ';
            } else {
              # deactivated only in active
              if ($cnodes{$cname} eq 'deleted') {
                $dis = '! ';
              } else {
                $dis = 'A ';
              }
            }
          } else {
            if ($de_working) {
              # deactivated only in working
              $dis = 'D ';
            } else {
              # deactivated in neither
              $dis = '  ';
            }
          }

          my $tdiff = ' ';
          if ($cnodes{$cname} eq 'deleted') {
            $tdiff = '-';
          } elsif ($cnodes{$cname} eq 'added') {
            $tdiff = '+';
          }
          print "$dis$tdiff$prefix$child $cname {\n";
          if ($cnodes{$cname} eq 'deleted') {
            displayDeletedOrigChildren([ @cur_path, $child, $cname ],
                                       $dis, "$prefix    ");
          } else {
            $config->setLevel(join ' ', (@cur_path, $child, $cname));
            my %ccnodes = $config->listNodeStatusDA();
            displayChildren(\%ccnodes, [ @cur_path, $child, $cname ],
                            $dis, "$prefix    ");
          }
          print "$dis$tdiff$prefix}\n";
        }
      } else {
        print "$dis$diff$prefix$child {\n";
        if ($child_hash{$child} eq 'deleted') {
          # this should not happen
          displayDeletedOrigChildren([ @cur_path, $child ], $dis,
                                     "$prefix    ");
        } else {
          displayChildren(\%cnodes, [ @cur_path, $child ], $dis,
                          "$prefix    ");
        }
        print "$dis$diff$prefix}\n";
      }
    } else {
      if ($child_hash{$child} eq 'deleted') {
        # XXX weird. already checked for leaf node above.
        $config->setLevel('');
        if ($config->isLeafNode(join ' ', (@cur_path, $child))) {
          displayValues([ @cur_path, $child ], $dis, $prefix, $child);
        } else {
          print "$dis$diff$prefix$child {\n";
          displayDeletedOrigChildren([ @cur_path, $child ], $dis,
                                     "$prefix    ");
          print "$dis$diff$prefix}\n";
        }
      } else {
        my $has_tmpl_children
          = $config->hasTmplChildren();
        print "$dis$diff$prefix$child"
              . ($has_tmpl_children ? " {\n$dis$diff$prefix}\n" : "\n");
      }
    }
  }
}

# @ARGV: represents the 'root' path. the output starts at this point under
#        the new config.
sub outputNewConfig {
  $config = new Vyatta::Config;
  $config->setLevel(join ' ', @_);
  if ($config->isLeafNode()) {
    displayValues([ @_ ], '', '', $_[$#_]);
    return;
  }

  # not a leaf node
  my %rnodes = $config->listNodeStatusDA();
  if (scalar(keys %rnodes) > 0) {
    displayChildren(\%rnodes, [ @_ ], '', '');
  } else {
    if ($config->existsOrig() && ! $config->exists()) {
      # this is a deleted node
      print 'Configuration under "' . (join ' ', @_) . "\" has been deleted\n";
    } elsif (!$config->validateTmplPath('', 1)) {
      # validation of current path (including values) failed
      print "Specified configuration path is not valid\n";
    } else {
      print 'Configuration under "' . (join ' ', @_) . "\" is empty\n";
    }
  }
}

# @ARGV: represents the 'root' path. the output starts at this point under
#        the active config.
sub outputActiveConfig {
  $config = new Vyatta::Config;
  $config->setLevel(join ' ', @_);
  displayDeletedOrigChildren([ @_ ], '','', 1);
}

1;
