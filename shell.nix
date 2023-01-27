with import <nixpkgs> {};
{ pkgs ? import <nixpkgs> {} }:

stdenv.mkDerivation {
  name = "nettop";
  src = ./.;

  buildInputs = [ 
	git 
	libtool 
	autoconf 
	automake 
	autogen 
	gnumake 
	cmake 
	clang 
	gcc
	ncurses
	libcurses
	libpcap
	libnl
	dbus
  ];
}
