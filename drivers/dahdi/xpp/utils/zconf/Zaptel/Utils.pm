package Zaptel::Utils;

# Accessors (miniperl does not have Class:Accessor)
our $AUTOLOAD;
sub AUTOLOAD {
	my $self = shift;
	my $name = $AUTOLOAD;
	$name =~ s/.*://;   # strip fully-qualified portion
	return if $name =~ /^[A-Z_]+$/;	# ignore special methods (DESTROY)
	my $key = uc($name);
	my $val = shift;
	if (defined $val) {
		#print STDERR "set: $key = $val\n";
		return $self->{$key} = $val;
	} else {
		if(!exists $self->{$key}) {
			#$self->xpp_dump;
			#die "Trying to get uninitialized '$key'";
		}
		my $val = $self->{$key};
		#print STDERR "get: $key ($val)\n";
		return $val;
	}
}

sub xpp_dump($) {
	my $self = shift || die;
	printf STDERR "Dump a %s\n", ref($self);
	foreach my $k (sort keys %{$self}) {
		my $val = $self->{$k};
		$val = '**UNDEF**' if !defined $val;
		printf STDERR "    %-20s %s\n", $k, $val;
	}
}

# Based on Autoloader

sub import {
	my $pkg = shift;
	my $callpkg = caller;

	#print STDERR "import: $pkg, $callpkg\n";
	#
	# Export symbols, but not by accident of inheritance.
	#
	die "Sombody inherited Zaptel::Utils" if $pkg ne 'Zaptel::Utils';
	no strict 'refs';
	*{ $callpkg . '::AUTOLOAD' } = \&AUTOLOAD;
	*{ $callpkg . '::xpp_dump' } = \&xpp_dump;
}

1;
