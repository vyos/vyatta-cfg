# Author: Vyatta <eng@vyatta.com>
# Date: 2007
# Description: vyatta configuration parser

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

package Vyatta::Config;

use strict;

use File::Find;

use lib '/opt/vyatta/share/perl5';
use Cstore;

my %fields = (
  _level => undef,
  _cstore => undef,
);

sub new {
  my ($that, $level) = @_;
  my $class = ref ($that) || $that;
  my $self = {
    %fields,
  };
  bless $self, $class;
  $self->{_level} = $level if defined($level);
  $self->{_cstore} = new Cstore();
  return $self;
}

sub get_path_comps {
  my ($self, $pstr) = @_;
  $pstr = '' if (!defined($pstr));
  $pstr = "$self->{_level} $pstr" if (defined($self->{_level}));
  $pstr =~ s/^\s+//;
  $pstr =~ s/\s+$//;
  my @path_comps = split /\s+/, $pstr;
  return \@path_comps;
}

############################################################
# low-level API functions that use the cstore library directly.
# they are either new functions or old ones that have been
# converted to use cstore.
############################################################

######
# observers of current working config or active config during a commit.
# * MOST users of this API should use these functions.
# * these functions MUST NOT worry about the "deactivated" state, i.e.,
#   deactivated nodes are equivalent to having been deleted for these
#   functions. in other words, these functions are NOT "deactivate-aware".
# * functions that can be used to observe "active config" can be used
#   outside a commit as well (only when observing active config, of course).
#
# note: these functions accept a third argument "$include_deactivated", but
#       it is for error checking purposes to ensure that all legacy
#       invocations have been fixed. the functions MUST NOT be called
#       with this argument.
my $DIE_DEACT_MSG = 'This function is NOT deactivate-aware';

## exists("path to node")
# Returns true if specified node exists in working config.
sub exists {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return 1
    if ($self->{_cstore}->cfgPathExists($self->get_path_comps($path), undef));
  return; # note: this return is needed. can't just return the return value
          #       of the above function since some callers expect "undef"
          #       as false.
}

## existsOrig("path to node")
# Returns true if specified node exists in active config.
sub existsOrig {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return 1
    if ($self->{_cstore}->cfgPathExists($self->get_path_comps($path), 1));
  return; # note: this return is needed.
}

## isDefault("path to node")
# Returns true if specified node is "default" in working config.
sub isDefault {
  my ($self, $path) = @_;
  return 1
    if ($self->{_cstore}->cfgPathDefault($self->get_path_comps($path), undef));
  return; # note: this return is needed.
}

## isDefaultOrig("path to node")
# Returns true if specified node is "default" in active config.
sub isDefaultOrig {
  my ($self, $path) = @_;
  return 1
    if ($self->{_cstore}->cfgPathDefault($self->get_path_comps($path), 1));
  return; # note: this return is needed.
}

## listNodes("level")
# return array of all child nodes at "level" in working config.
sub listNodes {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  my $ref = $self->{_cstore}->cfgPathGetChildNodes(
                                $self->get_path_comps($path), undef);
  return @{$ref};
}

## listOrigNodes("level")
# return array of all child nodes at "level" in active config.
sub listOrigNodes {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  my $ref = $self->{_cstore}->cfgPathGetChildNodes(
                                $self->get_path_comps($path), 1);
  return @{$ref};
}

## returnValue("node")
# return value of specified single-value node in working config.
# return undef if fail to get value (invalid node, node doesn't exist,
# not a single-value node, etc.).
sub returnValue {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return $self->{_cstore}->cfgPathGetValue($self->get_path_comps($path),
                                           undef);
}

## returnOrigValue("node")
# return value of specified single-value node in active config.
# return undef if fail to get value (invalid node, node doesn't exist,
# not a single-value node, etc.).
sub returnOrigValue {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return $self->{_cstore}->cfgPathGetValue($self->get_path_comps($path), 1);
}

## returnValues("node")
# return array of values of specified multi-value node in working config.
# return empty array if fail to get value (invalid node, node doesn't exist,
# not a multi-value node, etc.).
sub returnValues {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  my $ref = $self->{_cstore}->cfgPathGetValues($self->get_path_comps($path),
                                               undef);
  return @{$ref};
}

## returnOrigValues("node")
# return array of values of specified multi-value node in active config.
# return empty array if fail to get value (invalid node, node doesn't exist,
# not a multi-value node, etc.).
sub returnOrigValues {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  my $ref = $self->{_cstore}->cfgPathGetValues($self->get_path_comps($path),
                                               1);
  return @{$ref};
}

## sessionChanged()
# return whether the config session has uncommitted changes
sub sessionChanged {
  my ($self) = @_;
  return $self->{_cstore}->sessionChanged();
}

## loadFile()
# "load" the specified file
sub loadFile {
  my ($self, $file) = @_;
  return $self->{_cstore}->loadFile($file);
}

######
# observers of the "effective" config.
# they can be used
#   (1) outside a config session (e.g., op mode, daemons, callbacks, etc.).
#   OR
#   (2) during a config session
#
# HOWEVER, NOTE that the definition of "effective" is different under these
# two scenarios.
#   (1) when used outside a config session, "effective" == "active".
#       in other words, in such cases the effective config is the same
#       as the running config.
#
#   (2) when used during a config session, a config path (leading to either
#       a "node" or a "value") is "effective" if it is "in effect" at the
#       time when these observers are called. more detailed info can be
#       found in the library code.
#
# originally, these functions are exclusively for use during config
# sessions. however, for some usage scenarios, it is useful to have a set
# of API functions that can be used both during and outside config
# sessions. therefore, definition (1) is added above for convenience.
#
# for example, a developer can use these functions in a script that can
# be used both during a commit action and outside config mode, as long as
# the developer is clearly aware of the difference between the above two
# definitions.
#
# note that when used outside a config session (i.e., definition (1)),
# these functions are equivalent to the observers for the "active" config.
#
# to avoid any confusiton, when possible (e.g., in a script that is
# exclusively used in op mode), developers should probably use those
# "active" observers explicitly when outside a config session instead
# of these "effective" observers.
#
# it is also important to note that when used outside a config session,
# due to race conditions, it is possible that the "observed" active config
# becomes out-of-sync with the config that is actually "in effect".
# specifically, this happens when two things occur simultaneously:
#   (a) an observer function is called from outside a config session.
#   AND
#   (b) someone invokes "commit" inside a config session (any session).
#
# this is because "commit" only updates the active config at the end after
# all commit actions have been executed, so before the update happens,
# some config nodes have already become "effective" but are not yet in the
# "active config" and therefore are not observed by these functions.
#
# note that this is only a problem when the caller is outside config mode.
# in such cases, the caller (which could be an op-mode command, a daemon,
# a callback script, etc.) already must be able to handle config changes
# that can happen at any time. if "what's configured" is more important,
# using the "active config" should be fine as long as it is relatively
# up-to-date. if the actual "system state" is more important, then the
# caller should probably just check the system state in the first place
# (instead of using these config observers).

## isEffective("path")
# return whether "path" is in "active" config when used outside config
# session,
# OR
# return whether "path" is "effective" during current commit.
# see above discussion about the two different definitions.
#
# "effective" means the path is in effect, i.e., any of the following is true:
#   (1) active && working
#       path is in both active and working configs, i.e., unchanged.
#   (2) !active && working && committed
#       path is not in active, has been set in working, AND has already
#       been committed, i.e., "commit" has already processed the
#       addition/update of the path.
#   (3) active && !working && !committed
#       path is in active, has been deleted from working, AND
#       has NOT been committed yet, i.e., "commit" (per priority) has not
#       processed the deletion of the path yet (or has processed it but
#       the action failed).
#
# note: during commit, deactivate has the same effect as delete. so as
#       far as this function (and any other commit observer functions) is
#       concerned, deactivated nodes don't exist.
sub isEffective {
  my ($self, $path) = @_;
  return 1
    if ($self->{_cstore}->cfgPathEffective($self->get_path_comps($path)));
  return; # note: this return is needed.
}

## isActive("path")
# XXX this is the original API function. name is confusing ("active" could
#     be confused with "orig") but keep it for compatibility.
#     just call isEffective().
#     also, original function accepts "$disable" flag, which doesn't make
#     sense. for commit purposes, deactivated should be equivalent to
#     deleted.
sub isActive {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return $self->isEffective($path);
}

## listEffectiveNodes("level")
# return array of "effective" child nodes at "level" during current commit.
# see isEffective() for definition of "effective".
sub listEffectiveNodes {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetEffectiveChildNodes(
                                $self->get_path_comps($path));
  return @{$ref};
}

## listOrigPlusComNodes("level")
# XXX this is the original API function. name is confusing (it's neither
#     necessarily "orig" nor "plus") but keep it for compatibility.
#     just call listEffectiveNodes().
#     also, original function accepts "$disable" flag, which doesn't make
#     sense. for commit purposes, deactivated should be equivalent to
#     deleted.
sub listOrigPlusComNodes {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return $self->listEffectiveNodes($path);
}

## returnEffectiveValue("node")
# return "effective" value of specified "node" during current commit.
sub returnEffectiveValue {
  my ($self, $path) = @_;
  return $self->{_cstore}->cfgPathGetEffectiveValue(
                                  $self->get_path_comps($path));
}

## returnOrigPlusComValue("node")
# XXX this is the original API function. just call returnEffectiveValue().
#     also, original function accepts "$disable" flag.
sub returnOrigPlusComValue {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return $self->returnEffectiveValue($path);
}

## returnEffectiveValues("node")
# return "effective" values of specified "node" during current commit.
sub returnEffectiveValues {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetEffectiveValues(
                                      $self->get_path_comps($path));
  return @{$ref};
}

## returnOrigPlusComValues("node")
# XXX this is the original API function. just call returnEffectiveValues().
#     also, original function accepts "$disable" flag.
sub returnOrigPlusComValues {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return $self->returnEffectiveValues($path);
}

## isDeleted("node")
# whether specified node has been deleted in working config
sub isDeleted {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return 1 if ($self->{_cstore}->cfgPathDeleted($self->get_path_comps($path)));
  return; # note: this return is needed.
}

## listDeleted("level")
# return array of deleted nodes at specified "level"
sub listDeleted {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  my $ref = $self->{_cstore}->cfgPathGetDeletedChildNodes(
                                $self->get_path_comps($path));
  return @{$ref};
}

## returnDeletedValues("level")
# return array of deleted values of specified "multi node"
sub returnDeletedValues {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetDeletedValues(
                                $self->get_path_comps($path));
  return @{$ref};
}

## isAdded("node")
# whether specified node has been added in working config
sub isAdded {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return 1 if ($self->{_cstore}->cfgPathAdded($self->get_path_comps($path)));
  return; # note: this return is needed.
}

## isChanged("node")
# whether specified node has been changed in working config
# XXX behavior is different from original implementation, which was
#     inconsistent between deleted nodes and deactivated nodes.
#     see cstore library source for details.
#     basically, a node is "changed" if it's "added", "deleted", or
#     "marked changed" (i.e., if any descendant changed).
sub isChanged {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  return 1 if ($self->{_cstore}->cfgPathChanged($self->get_path_comps($path)));
  return; # note: this return is needed.
}

## listNodeStatus("level")
# return a hash of status of child nodes at specified level.
# node name is the hash key. node status is the hash value.
# node status can be one of "deleted", "added", "changed", or "static".
sub listNodeStatus {
  my ($self, $path, $include_deactivated) = @_;
  die $DIE_DEACT_MSG if (defined($include_deactivated));
  my $ref = $self->{_cstore}->cfgPathGetChildNodesStatus(
                                          $self->get_path_comps($path));
  return %{$ref};
}

## getTmplChildren("level")
# return list of child nodes in the template hierarchy at specified level.
sub getTmplChildren {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->tmplGetChildNodes($self->get_path_comps($path));
  return @{$ref};
}

## validateTmplPath("path")
# return whether specified path is a valid template path
sub validateTmplPath {
  my ($self, $path, $validate_vals) = @_;
  return 1 if ($self->{_cstore}->validateTmplPath($self->get_path_comps($path),
                                                  $validate_vals));
  return; # note: this return is needed.
}

## parseTmplAll("path")
# return hash ref of parsed template of specified path, undef if path is
# invalid. note: if !allow_val, path must terminate at a "node", not "value".
sub parseTmplAll {
  my ($self, $path, $allow_val) = @_;
  my $href = $self->{_cstore}->getParsedTmpl($self->get_path_comps($path),
                                             $allow_val);
  if (defined($href)) {
    # some conversions are needed
    if (defined($href->{is_value}) and $href->{is_value} eq '1') {
      $href->{is_value} = 1;
    }
    if (defined($href->{multi}) and $href->{multi} eq '1') {
      $href->{multi} = 1;
    }
    if (defined($href->{tag}) and $href->{tag} eq '1') {
      $href->{tag} = 1;
    }
    if (defined($href->{limit})) {
      $href->{limit} = int($href->{limit});
    }
  }
  return $href;
}

sub hasTmplChildren {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->tmplGetChildNodes($self->get_path_comps($path));
  return if (!defined($ref));
  return (scalar(@{$ref}) > 0);
}


######
# "deactivate-aware" observers of current working config or active config.
# * MUST ONLY be used by operations that NEED to distinguish between
#   deactivated nodes and deleted nodes. below is the list of operations
#   that are allowed to use these functions:
#     * configuration output (show, save, load)
#
# operations that are not on the above list MUST NOT use these
# "deactivate-aware" functions.

## deactivated("node")
# return whether specified node is deactivated in working config.
# note that this is different from "marked deactivated". if a node is
# "marked deactivated", then the node itself and any descendants are
# "deactivated".
sub deactivated {
  my ($self, $path) = @_;
  return 1
    if ($self->{_cstore}->cfgPathDeactivated($self->get_path_comps($path),
                                             undef));
  return; # note: this return is needed.
}

## deactivatedOrig("node")
# return whether specified node is deactivated in active config.
sub deactivatedOrig {
  my ($self, $path) = @_;
  return 1
    if ($self->{_cstore}->cfgPathDeactivated($self->get_path_comps($path), 1));
  return; # note: this return is needed.
}

## returnValuesDA("node")
# DA version of returnValues()
sub returnValuesDA {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetValuesDA($self->get_path_comps($path),
                                                 undef);
  return @{$ref};
}

## returnOrigValuesDA("node")
# DA version of returnOrigValues()
sub returnOrigValuesDA {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetValuesDA($self->get_path_comps($path),
                                                 1);
  return @{$ref};
}

## returnValueDA("node")
# DA version of returnValue()
sub returnValueDA {
  my ($self, $path) = @_;
  return $self->{_cstore}->cfgPathGetValueDA($self->get_path_comps($path),
                                             undef);
}

## returnOrigValueDA("node")
# DA version of returnOrigValue()
sub returnOrigValueDA {
  my ($self, $path) = @_;
  return $self->{_cstore}->cfgPathGetValueDA($self->get_path_comps($path), 1);
}

## listOrigNodesDA("level")
# DA version of listOrigNodes()
sub listOrigNodesDA {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetChildNodesDA(
                                $self->get_path_comps($path), 1);
  return @{$ref};
}

## listNodeStatusDA("level")
# DA version of listNodeStatus()
sub listNodeStatusDA {
  my ($self, $path) = @_;
  my $ref = $self->{_cstore}->cfgPathGetChildNodesStatusDA(
                                          $self->get_path_comps($path));
  return %{$ref};
}

## returnComment("node")
# return comment of "node" in working config or undef if comment doesn't exist
sub returnComment {
  my ($self, $path) = @_;
  return $self->{_cstore}->cfgPathGetComment($self->get_path_comps($path),
                                             undef);
}

## returnOrigComment("node")
# return comment of "node" in active config or undef if comment doesn't exist
sub returnOrigComment {
  my ($self, $path) = @_;
  return $self->{_cstore}->cfgPathGetComment($self->get_path_comps($path), 1);
}


############################################################
# high-level API functions (not using the cstore library directly)
############################################################

## setLevel("level")
# set the current level of config hierarchy to specified level (if defined).
# return the current level.
sub setLevel {
  my ($self, $level) = @_;
  $self->{_level} = $level if defined($level);
  return $self->{_level};
}

## returnParent("..( ..)*")
# return the name of ancestor node relative to the current level.
# each level up is represented by a ".." in the argument.
sub returnParent {
  my ($self, $ppath) = @_;
  my @pcomps = @{$self->get_path_comps()};
  # we could call split in scalar context but that generates a warning
  my @dummy = split(/\s+/, $ppath);
  my $num = scalar(@dummy);
  return if ($num > scalar(@pcomps));
  return $pcomps[-$num];
}

## parseTmpl("path")
# parse template of specified path and return ($is_multi, $is_text, $default)
# or undef if specified path is not valid.
sub parseTmpl {
  my ($self, $path) = @_;
  my $href = $self->parseTmplAll($path);
  return if (!defined($href));
  my $is_multi = $href->{multi};
  my $is_text = (defined($href->{type}) and $href->{type} eq 'txt');
  my $default = $href->{default};
  return ($is_multi, $is_text, $default);
}

## isTagNode("path")
# whether specified path is a tag node.
sub isTagNode {
  my ($self, $path) = @_;
  my $href = $self->parseTmplAll($path);
  return (defined($href) and $href->{tag});
}

## isLeafNode("path")
# whether specified path is a "leaf node", i.e., single-/multi-value node.
sub isLeafNode {
  my ($self, $path) = @_;
  my $href = $self->parseTmplAll($path, 1);
  return (defined($href) and !$href->{is_value} and $href->{type}
          and !$href->{tag});
}

## isLeafValue("path")
# whether specified path is a "leaf value", i.e., value of a leaf node.
sub isLeafValue {
  my ($self, $path) = @_;
  my $href = $self->parseTmplAll($path, 1);
  return (defined($href) and $href->{is_value} and !$href->{tag});
}

# compare two value lists and return "deleted" and "added" lists.
# since this is for multi-value nodes, there is no "changed" (if a value's
# ordering changed, it is deleted then added).
# $0: \@orig_values
# $1: \@new_values
sub compareValueLists {
  my $self = shift;
  my @ovals = @{$_[0]};
  my @nvals = @{$_[1]};
  my %comp_hash = (
                    'deleted' => [],
                    'added' => [],
                  );
  my $idx = 0;
  my %ohash = map { $_ => ($idx++) } @ovals;
  $idx = 0;
  my %nhash = map { $_ => ($idx++) } @nvals;
  my $min_changed_idx = 2**31;
  my %dhash = ();
  foreach (@ovals) {
    if (!defined($nhash{$_})) {
      push @{$comp_hash{'deleted'}}, $_;
      $dhash{$_} = 1;
      if ($ohash{$_} < $min_changed_idx) {
        $min_changed_idx = $ohash{$_};
      }
    }
  }
  foreach (@nvals) {
    if (defined($ohash{$_})) {
      if ($ohash{$_} != $nhash{$_}) {
        if ($ohash{$_} < $min_changed_idx) {
          $min_changed_idx = $ohash{$_};
        }
      }
    }
  }
  foreach (@nvals) {
    if (defined($ohash{$_})) {
      if ($ohash{$_} != $nhash{$_}) {
        if (!defined($dhash{$_})) {
          push @{$comp_hash{'deleted'}}, $_;
          $dhash{$_} = 1;
        }
        push @{$comp_hash{'added'}}, $_;
      } elsif ($ohash{$_} >= $min_changed_idx) {
        # ordering unchanged, but something before it is changed.
        if (!defined($dhash{$_})) {
          push @{$comp_hash{'deleted'}}, $_;
          $dhash{$_} = 1;
        }
        push @{$comp_hash{'added'}}, $_;
      } else {
        # this is before any changed value. do nothing.
      }
    } else {
      push @{$comp_hash{'added'}}, $_;
    }
  }
  return %comp_hash;
}


sub outputError {
    my ($location,$msg) = @_;
    if (defined($ENV{VYATTA_OUTPUT_ERROR_LOCATION})) {
	print STDERR "_errloc_:[" . join(" ",@{$location}) . "]\n";
    }
    print STDERR $msg . "\n";
}

############################################################
# API functions that have not been converted
############################################################

# XXX the following function should not be needed. the only user is
#     ConfigLoad, which uses this to get all deactivated nodes in active
#     config and then reactivates everything on load.
#
#     this works for "load" but not for "merge", which incorrectly
#     reactivates all deactivated nodes even if they are not in the config
#     file to be merged. see bug 5746.
#
#     how to get rid of this function depends on how bug 5746 is going
#     to be fixed.
## getAllDeactivated()
# returns array of all deactivated nodes.
my @all_deactivated_nodes;
sub getAllDeactivated {
    my ($self, $path) = @_;
    my $start_dir = $ENV{VYATTA_ACTIVE_CONFIGURATION_DIR};
    find ( \&wanted, $start_dir );
    return @all_deactivated_nodes;
}
sub wanted {
    if ( $_ eq '.disable' ) {
	my $f = $File::Find::name;
	#now strip off leading path and trailing file
	$f = substr($f, length($ENV{VYATTA_ACTIVE_CONFIGURATION_DIR}));
	$f = substr($f, 0, length($f)-length("/.disable"));
	$f =~ s/\// /g;
	push @all_deactivated_nodes, $f;
    }
}

1;

