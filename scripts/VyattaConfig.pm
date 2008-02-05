package VyattaConfig;

use strict;

use VyattaConfigDOMTree;

my %fields = (
  _changes_only_dir_base  => $ENV{VYATTA_CHANGES_ONLY_DIR},
  _new_config_dir_base    => $ENV{VYATTA_TEMP_CONFIG_DIR},
  _active_dir_base        => $ENV{VYATTA_ACTIVE_CONFIGURATION_DIR},
  _vyatta_template_dir    => $ENV{VYATTA_CONFIG_TEMPLATE},
  _current_dir_level      => "/",
  _level => undef,
);

sub new {
  my $that = shift;
  my $class = ref ($that) || $that;
  my $self = {
    %fields,
  };

  bless $self, $class;
  return $self;
}

sub _set_current_dir_level {
  my ($self) = @_;
  my $level = $self->{_level};

  $level =~ s/\//%2F/g;
  $level =~ s/\s+/\//g;

  $self->{_current_dir_level} = "/$level";
  return $self->{_current_dir_level};
}

## setLevel("level")
# if "level" is supplied, set the current level of the hierarchy we are working on
# return the current level
sub setLevel {
  my ($self, $level) = @_;

  $self->{_level} = $level if defined($level);
  $self->_set_current_dir_level();

  return $self->{_level};
}

## listNodes("level")
# return array of all nodes at "level"
# level is relative
sub listNodes {
  my ($self, $path) = @_;
  my @nodes = ();

  if (defined $path) { 
    $path =~ s/\//%2F/g;
    $path =~ s/\s+/\//g;
    $path = $self->{_new_config_dir_base} . $self->{_current_dir_level} . "/" . $path;
  }
  else {
    $path = $self->{_new_config_dir_base} . $self->{_current_dir_level};
  }

  #print "DEBUG VyattaConfig->listNodes(): path = $path\n";
  opendir DIR, "$path" or return ();
  @nodes = grep !/^\./, readdir DIR;
  closedir DIR;

  my @nodes_modified = ();
  while (@nodes) {
    my $tmp = pop (@nodes);
    $tmp =~ s/\n//g;
    $tmp =~ s/%2F/\//g;
    #print "DEBUG VyattaConfig->listNodes(): node = $tmp\n";
    push @nodes_modified, $tmp;
  }

  return @nodes_modified;
}

## listOrigNodes("level")
# return array of all original nodes (i.e., before any current change; i.e.,
# in "working") at "level"
# level is relative
sub listOrigNodes {
  my ($self, $path) = @_;
  my @nodes = ();

  if (defined $path) { 
    $path =~ s/\//%2F/g;
    $path =~ s/\s+/\//g;
    $path = $self->{_active_dir_base} . $self->{_current_dir_level} . "/"
            . $path;
  }
  else {
    $path = $self->{_active_dir_base} . $self->{_current_dir_level};
  }

  #print "DEBUG VyattaConfig->listNodes(): path = $path\n";
  opendir DIR, "$path" or return ();
  @nodes = grep !/^\./, readdir DIR;
  closedir DIR;

  my @nodes_modified = ();
  while (@nodes) {
    my $tmp = pop (@nodes);
    $tmp =~ s/\n//g;
    $tmp =~ s/%2F/\//g;
    #print "DEBUG VyattaConfig->listNodes(): node = $tmp\n";
    push @nodes_modified, $tmp;
  }

  return @nodes_modified;
}

## returnParent("level")
# return the name of parent node relative to the current hierarchy
# in this case "level" is set to the parent dir ".. .."
# for example
sub returnParent {
  my ($self, $node) = @_;
  my @x, my $tmp;

  # split our hierarchy into vars on a stack
  my @level = split /\s+/, $self->{_level};

  # count the number of parents we need to lose
  # and then pop 1 less
  @x = split /\s+/, $node;
  for ($tmp = 1; $tmp < @x; $tmp++) {
    pop @level;
  }

  # return the parent
  $tmp = pop @level;
  return $tmp;
}

## returnValue("node")
# returns the value of "node" or undef if the node doesn't exist .
# node is relative
sub returnValue {
  my ( $self, $node ) = @_;
  my $tmp;

  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;

  if ( -f "$self->{_new_config_dir_base}$self->{_current_dir_level}/$node/node.val" ) {
    open FILE, "$self->{_new_config_dir_base}$self->{_current_dir_level}/$node/node.val" || return undef;
    read FILE, $tmp, 16384;
    close FILE;

    $tmp =~ s/\n$//;
    return $tmp;
  }
  else {
    return undef;
  }
}


## returnOrigValue("node")
# returns the original value of "node" (i.e., before the current change; i.e.,
# in "working") or undef if the node doesn't exist.
# node is relative
sub returnOrigValue {
  my ( $self, $node ) = @_;
  my $tmp;

  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;
  my $filepath = "$self->{_active_dir_base}$self->{_current_dir_level}/$node";
  if ( -f "$filepath/node.val") {
    open FILE, "$filepath/node.val" || return undef;
    read FILE, $tmp, 16384;
    close FILE;

    $tmp =~ s/\n$//;
    return $tmp;
  } else {
    return undef;
  }
}

## returnValues("node")
# returns an array of all the values of "node", or an empty array if the values do not exist.
# node is relative
sub returnValues {
  my $val = returnValue(@_);
  my @values = ();
  if (defined($val)) {
    @values = split("\n", $val);
  }
  return @values;
}

## returnOrigValues("node")
# returns an array of all the original values of "node" (i.e., before the
# current change; i.e., in "working"), or an empty array if the values do not
# exist.
# node is relative
sub returnOrigValues {
  my $val = returnOrigValue(@_);
  my @values = split("\n", $val);
  return @values;
}

## exists("node")
# Returns true if the "node" exists. 
sub exists {
  my ( $self, $node ) = @_;
  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;

  if ( -d "$self->{_new_config_dir_base}$self->{_current_dir_level}/$node" ) {
    #print "DEBUG: the dir is there\n";
    return !0;
  } else {
    return undef;
  }
}

## existsOrig("node")
# Returns true if the "original node" exists. 
sub existsOrig {
  my ( $self, $node ) = @_;
  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;

  if ( -d "$self->{_active_dir_base}$self->{_current_dir_level}/$node" ) {
    return 1;
  } else {
    return undef;
  }
}

## isDeleted("node")
# is the "node" deleted. node is relative.  returns true or false
sub isDeleted {
  my ($self, $node) = @_;
  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;

  my $filepathAct
    = "$self->{_active_dir_base}$self->{_current_dir_level}/$node";
  my $filepathNew
    = "$self->{_new_config_dir_base}$self->{_current_dir_level}/$node";

  if ((-e $filepathAct) && !(-e $filepathNew)) {
    return 1;
  }
  return 0;
}

## listDeleted("level")
# return array of deleted nodes in the "level"
# "level" defaults to current
sub listDeleted {
  my ($self, $path) = @_;
  my @new_nodes = $self->listNodes("$path");
  my @orig_nodes = $self->listOrigNodes("$path");
  my %new_hash = map { $_ => 1 } @new_nodes;
  my @deleted = grep { !defined($new_hash{$_}) } @orig_nodes;
  return @deleted;
}

## isChanged("node")
# will check the change_dir to see if the "node" has been changed from a previous
# value.  returns true or false.
sub isChanged {
  my ($self, $node) = @_;

  # let's setup the filepath for the change_dir
  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;
  my $filepath = "$self->{_changes_only_dir_base}$self->{_current_dir_level}/$node";

  # if the node exists in the change dir, it's modified.
  if (-e "$filepath") { return 1; }
  else { return 0; }
}

## isChangedOrDeleted("node")
# is the "node" changed or deleted. node is relative.  returns true or false
sub isChangedOrDeleted {
  my ($self, $node) = @_;

  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;

  my $filepathChg
    = "$self->{_changes_only_dir_base}$self->{_current_dir_level}/$node";
  if (-e $filepathChg) {
    return 1;
  }

  my $filepathAct
    = "$self->{_active_dir_base}$self->{_current_dir_level}/$node";
  my $filepathNew
    = "$self->{_new_config_dir_base}$self->{_current_dir_level}/$node";

  if ((-e $filepathAct) && !(-e $filepathNew)) {
    return 1;
  }

  return 0;
}

## isAdded("node")
# will compare the new_config_dir to the active_dir to see if the "node" has 
# been added.  returns true or false.
sub isAdded {
  my ($self, $node) = @_;

  #print "DEBUG VyattaConfig->isAdded(): node $node\n";
  # let's setup the filepath for the modify dir
  $node =~ s/\//%2F/g;
  $node =~ s/\s+/\//g;
  my $filepathNewConfig = "$self->{_new_config_dir_base}$self->{_current_dir_level}/$node";
  
  #print "DEBUG VyattaConfig->isAdded(): filepath $filepathNewConfig\n";

  # if the node doesn't exist in the modify dir, it's not
  # been added.  so short circuit and return false.
  if (! -e $filepathNewConfig) { return 0; }
 
  # now let's setup the path for the working dir
  my $filepathActive = "$self->{_active_dir_base}$self->{_current_dir_level}/$node";

  # if the node is in the active_dir it's not new
  if (-e $filepathActive) { return 0; }
  else { return 1; }
}

## listNodeStatus("level")
# return a hash of the status of nodes at the current config level
# node name is the hash key. node status is the hash value.
# node status can be one of deleted, added, changed, or static
sub listNodeStatus {
  my ($self, $path) = @_;
  my @nodes = ();
  my %nodehash = ();

  # find deleted nodes first
  @nodes = $self->listDeleted("$path");
  foreach my $node (@nodes) {
    if ($node =~ /.+/) { $nodehash{$node} = "deleted" };
  }

  @nodes = ();
  @nodes = $self->listNodes("$path");
  foreach my $node (@nodes) {
    if ($node =~ /.+/) {
      #print "DEBUG VyattaConfig->listNodeStatus(): node $node\n";
      # No deleted nodes -- added, changed, ot static only.
      if    ($self->isAdded("$path $node"))   { $nodehash{$node} = "added"; }
      elsif ($self->isChanged("$path $node")) { $nodehash{$node} = "changed"; }
      else { $nodehash{$node} = "static"; }
    }
  }

  return %nodehash;
}

############ DOM Tree ################

#Create active DOM Tree
sub createActiveDOMTree {

    my $self = shift;

    my $tree = new VyattaConfigDOMTree($self->{_active_dir_base} . $self->{_current_dir_level},"active");

    return $tree;
}

#Create changes only DOM Tree
sub createChangesOnlyDOMTree {

    my $self = shift;

    my $tree = new VyattaConfigDOMTree($self->{_changes_only_dir_base} . $self->{_current_dir_level},
				       "changes_only");

    return $tree;
}

#Create new config DOM Tree
sub createNewConfigDOMTree {

    my $self = shift;

    my $tree = new VyattaConfigDOMTree($self->{_new_config_dir_base} . $self->{_current_dir_level},
				       "new_config");

    return $tree;
}


###### functions for templates ######

# $1: array representing the config node path.
# returns the filesystem path to the template of the specified node,
#   or undef if the specified node path is not valid.
sub getTmplPath {
  my $self = shift;
  my @cfg_path = @{$_[0]};
  my $tpath = $self->{_vyatta_template_dir};
  for my $p (@cfg_path) {
    if (-d "$tpath/$p") {
      $tpath .= "/$p";
      next;
    }
    if (-d "$tpath/node.tag") {
      $tpath .= "/node.tag";
      next;
    }
    # the path is not valid!
    return undef;
  }
  return $tpath
}

sub isTagNode {
  my $self = shift;
  my $cfg_path_ref = shift;
  my $tpath = $self->getTmplPath($cfg_path_ref);
  return undef if (!defined($tpath));
  if (-d "$tpath/node.tag") {
    return 1;
  }
  return 0;
}

sub hasTmplChildren {
  my $self = shift;
  my $cfg_path_ref = shift;
  my $tpath = $self->getTmplPath($cfg_path_ref);
  return undef if (!defined($tpath));
  opendir(TDIR, $tpath) or return 0;
  my @tchildren = grep !/^node\.def$/, (grep !/^\./, (readdir TDIR));
  closedir TDIR;
  if (scalar(@tchildren) > 0) {
    return 1;
  }
  return 0;
}

# $cfg_path_ref: ref to array containing the node path.
# returns ($is_multi, $is_text, $default),
#   or undef if specified node is not valid.
sub parseTmpl {
  my $self = shift;
  my $cfg_path_ref = shift;
  my ($is_multi, $is_text, $default) = (0, 0, undef);
  my $tpath = $self->getTmplPath($cfg_path_ref);
  return undef if (!defined($tpath));
  if (! -r "$tpath/node.def") {
    return ($is_multi, $is_text);
  }
  open(TMPL, "<$tpath/node.def") or return ($is_multi, $is_text);
  foreach (<TMPL>) {
    if (/^multi:/) {
      $is_multi = 1;
    }
    if (/^type:\s+txt\s*$/) {
      $is_text = 1;
    }
    if (/^default:\s+(\S+)\s*$/) {
      $default = $1;
    }
  }
  close TMPL;
  return ($is_multi, $is_text, $default);
}

###### misc functions ######

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


