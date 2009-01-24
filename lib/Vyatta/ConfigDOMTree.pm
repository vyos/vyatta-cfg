#!/usr/bin/perl

#
# Module: vyatta-cfg
#
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
# A copy of the GNU General Public License is available as
# `/usr/share/common-licenses/GPL' in the Debian GNU/Linux distribution
# or on the World Wide Web at `http://www.gnu.org/copyleft/gpl.html'.
# You can also obtain it by writing to the Free Software Foundation,
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
# MA 02110-1301, USA.
#
# This code was originally developed by Vyatta, Inc.
# Portions created by Vyatta are Copyright (C) 2005, 2006, 2007 Vyatta, Inc.
# All Rights Reserved.
#
# Author: Oleg Moskalenko
# Date: 2007
# Description:
#
# **** End License ****
#
#

package Vyatta::ConfigDOMTree;

use strict;

my %fields = (
	      _dir              => undef,
	      _name             => undef,
	      _value            => undef,
	      _subnodes         => undef
	      );

sub new {

  my $that = shift;
  my $dir = shift;
  my $name = shift;

  my $class = ref ($that) || $that;

  my $self = {
    %fields
  };

  bless $self, $class;

  $self->{_dir}  = $dir;
  $self->{_name} = $name;

  return $self->_construct_dom_tree();
}

#Simple DOM Tree iteration and screen output
#$1 - left screen offset (optional)
sub print {

    my $self = shift;
    my $level = shift;

    my $tree = $self;

    if(!(defined $level)) {
	$level="";
    }
    
    if(defined $tree) {

	print("$level name=",$tree->getNodeName(),"\n");

	my $value = $tree->getNodeValue();

	if(defined $value) {

	    print("$level value=$value\n");

	}
	
	my @subnodes = $tree->getSubNodes();
	
	while(@subnodes) {
	    
	    my $subnode = shift @subnodes;
	    $subnode->print($level . "  ");
	}
    }
}

#Return value of the tree node
sub getNodeValue {

    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;
    
    if(defined $tree) {
	
	$ret = $tree->{_value};
    }
    
    return $ret;
}

#Return value of the tree node.
#If the value is nor defined, return empty string.
sub getNodeValueAsString {

    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;
    
    if(defined $tree) {
	
	$ret = $tree->getNodeValue();
    }

    if(!defined $ret) {
	$ret = "";
    }
    
    return $ret;
}

#Return name of the tree node
sub getNodeName {

    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;
    
    if(defined $tree) {
	
	$ret = $tree->{_name};
    }
    
    return $ret;
}

#Return array of subnodes of the tree node
sub getSubNodes {
    
    my $self = shift;
    my $tree = $self;
    
    my @ret = ();
    
    if(defined $tree) {
	
	my $subnodes = $tree->{_subnodes};
	
	if(defined $subnodes) {
	    
	    @ret = values %{$subnodes};
	    
	}
    }
    
    return @ret;
}

sub isLeafNode {
    
    my $self = shift;
    my $tree = $self;

    my $ret=undef;

    if(defined $tree) {

	if(defined $tree->{_value}) {

	    if(! defined $tree->{_subnodes}) {

		$ret="true";
	    }
	}
    }

    return $ret;
}

#Return subtree of the tree according to the path list
#$1, $2, ... - path to the subtree
sub getSubNode {
    
    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;

    while(@_ && $tree) {
	
	my $subnode = shift (@_);
	
	my $subnodes = $tree->{_subnodes};
	
	if(defined $subnodes) {
	    
	    $tree = $subnodes->{$subnode};
	    
	} else {
	    
	    $tree = undef;
	    
	}
    }
    
    $ret=$tree;
    
    return $ret;
}

#Return value of the subnode of the tree according to the path list
#$1, $2, ... - path to the subtree
sub getSubNodeValue {
    
    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;
    
    if(defined $tree) {
	
	my $node = $tree->getSubNode(@_);
	
	if(defined $node) {

	    $ret=$node->getNodeValue();
	}
    }
    
    return $ret;
}

#Return value of the subnode of the tree according to the path list.
#If the value is not defined, return empty string.
#$1, $2, ... - path to the subtree
sub getSubNodeValueAsString {
    
    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;
    
    if(defined $tree) {
	
	my $node = $tree->getSubNode(@_);
	
	if(defined $node) {

	    $ret=$node->getNodeValue();
	}
    }

    if(! defined $ret) {
	$ret = "";
    }
    
    return $ret;
}

#Check if there is a subnode with the specified path.
#$1, $2, ... - path to the subtree
sub subNodeExist {
    
    my $self = shift;
    my $tree = $self;
    
    my $ret = undef;
    
    if(defined $tree) {
	
	my $node = $tree->getSubNode(@_);
	
	if(defined $node) {

	    $ret="true";
	}
    }
    
    return $ret;
}

#Return of the children of the node
#$1, $2, ... - path to the subtree
sub getSubNodesNumber {
    
    my $self = shift;
    my $tree = $self;
    
    my $ret = 0;
    
    if(defined $tree) {

	my $node = $tree->getSubNode(@_);
	
	if(defined $node) {

	    my @subs = $node->getSubNodes();

	    if(@subs) {
		$ret = $#subs + 1;
	    }
	}
    }
    
    return $ret;
}

#private method: costruct DOM Tree according to the absolute path provided
sub _construct_dom_tree {

    my $self = shift;

    my $subnodesNum=0;
    my $valuePresent=0;

    return unless defined $self;

    opendir my $dir, $self->{_dir} or return;
    my @entries = grep !/^\./, readdir $dir;
    closedir $dir;

    while(@entries) {

	my $entry = shift @entries;

	if($entry) {
	    my $fn = $self->{_dir} . "/" . $entry;
	    if( -f $fn) {
		if($entry eq "node.val") {
		    my $value=`cat $fn`;
		    while(chomp $value) {};
		    $self->{_value} = $value;
		    $valuePresent++;
		}
	    } elsif (-d $fn) {
		my $subnode = new Vyatta::ConfigDOMTree($fn,$entry);
		if(defined $subnode) {
		    if(! defined $self->{_subnodes} ) {
			$self->{_subnodes} = {};
		    }
		    $self->{_subnodes}->{$entry} = $subnode;
		    $subnodesNum++;
		}
	    }
	}
    }

    return if ( $valuePresent < 1 && $subnodesNum < 1 );
    
    return $self;
}

1;
