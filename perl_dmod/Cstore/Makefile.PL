# Copyright (C) 2010 Vyatta, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

package MY;

use 5.010000;
use ExtUtils::MakeMaker;

my $PMOD_DIR = '$(SITEPREFIX)/share/perl5';

sub constants{
  my $self = shift;
  my $orig_txt = $self->SUPER::constants(@_);
  $orig_txt =~ s#= \$\(SITEPREFIX\)/(lib|share)/.*#= $PMOD_DIR#g;
  return $orig_txt;
}

sub c_o {
  my $self = shift;
  my $orig_txt = $self->SUPER::c_o(@_);
  $orig_txt =~ s/\.c(\s)/.cpp$1/g;
  return $orig_txt;
}

sub xs_c {
  my $self = shift;
  my $orig_txt = $self->SUPER::xs_c(@_);
  $orig_txt =~ s/\.c(\s)/.cpp$1/g;
  return $orig_txt;
}

sub xs_o {
  my $self = shift;
  my $orig_txt = $self->SUPER::xs_o(@_);
  $orig_txt =~ s/\.c(\s)/.cpp$1/g;
  return $orig_txt;
}

sub install {
  my $self = shift;
  my $orig_txt = $self->SUPER::install(@_);
  $orig_txt =~ s/pure_install doc_install/pure_install/g;
  $orig_txt =~ s/\$\(INST_MAN3DIR\) .*/undef undef/g;
  return $orig_txt;
}

sub clean {
  my $self = shift;
  my $orig_txt = $self->SUPER::clean(@_);
  $orig_txt =~ s/Cstore\.c\s/Cstore.cpp /g;
  return $orig_txt;
}

sub dynamic_lib {
  my $self = shift;
  my $orig_txt = $self->SUPER::dynamic_lib(@_);
  $orig_txt =~ s/(\s)LD_RUN_PATH=\S+\s+/$1/g;
  return $orig_txt;
}

WriteMakefile(
    NAME              => 'Cstore',
    VERSION_FROM      => 'lib/Cstore.pm',
    PREREQ_PM         => {},
    ($] >= 5.005 ?
      (ABSTRACT_FROM  => 'lib/Cstore.pm',
       AUTHOR         => 'Vyatta <eng@vyatta.com>') : ()),
    # note: MM will convert LIBS to absolute path in Makefile.
    #       => regenerate Makefile every time
    LIBS   => ['-L../../src/.libs -lvyatta-cfg'],
    DEFINE => '',
    INC    => '-I../../src',
    CC => 'g++',
    PREFIX => '/opt/vyatta',
    INSTALLDIRS => 'site',
);

