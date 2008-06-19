package XppConfig;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2008, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id$
#
use strict;

my $conf_file = "/etc/dahdi/xpp.conf";

sub subst_var($$) {
	my $lookup = shift;
	my $string = shift;

	if(defined $lookup->{$string}) {
		return $lookup->{$string};
	} else {
		return $string;
	}
}

sub read_config($) {
	my $input = shift || die;
	my %xpp_config;
	my $lookup = \%xpp_config;

	open(F, $input) || die "Failed reading configuration $input: $!\n";
LINE:
	while(<F>) {
		chomp;
		s/#.*//;	# strip comments
		next unless /\S/;
		s/^\s*//;
		if(s/\\$//) {
			my $next = <F>;
			$next =~ s/^\s*//;
			$_ .= " $next";
			redo LINE;
		}
		my ($key, $value) = split(/=/, $_, 2);
		# Trim whitespace around key/value
		$key =~ s/^\s*(\S+)\s*$/$1/;
		$value =~ s/^\s*(\S+)\s*$/$1/;
		# Variable substitution
		my $new_value = $value;
		$new_value =~ s/\$(\w+)/subst_var($lookup,$1)/eg;
		$xpp_config{$key} = $new_value;
	}
	close F;
	return %xpp_config;
}

sub import {
	my $pack = shift || die "Import without package?";
	my $init_dir = shift || die "$pack::import -- missing init_dir parameter";
	my $local_conf = "$init_dir/xpp.conf";
	$conf_file = $local_conf if -r $local_conf;
	my %x = read_config($conf_file);
}

sub show_vars {
	my $assoc = shift;
	foreach (sort keys %{$assoc}) {
		print "$_\t$assoc->{$_}\n";
	}
}

sub source_vars {
	my @keys = @_;
	my %conf = read_config($conf_file);
	my %result;
	my $k;
	my $v;

	foreach (@keys) {
		if(defined $conf{$_}) {
			$result{$_} = $conf{$_};
		}
	}
	return ($conf_file, %result);
}


1;
