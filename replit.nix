{ pkgs }: {
	deps = [
		pkgs.nmap
  pkgs.groff
  pkgs.rlwrap
  pkgs.iproute2
  pkgs.mkinitcpio-nfs-utils
  pkgs.python39Packages.ipython
  pkgs.toybox
        pkgs.less
        pkgs.clang_12
		pkgs.ccls
		pkgs.gdb
		pkgs.gnumake
        pkgs.openssl
        pkgs.lua5_4
        pkgs.libevent
        pkgs.luajit
        pkgs.valgrind
	];
}