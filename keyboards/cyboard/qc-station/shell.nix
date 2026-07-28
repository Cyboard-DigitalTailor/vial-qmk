# Standalone dev shell for the wired QC station host tool: Python + pyserial.
# (The vial-qmk repo-root shell.nix already provides these too, alongside the
# firmware toolchain — use that if you're also building the .uf2.)
{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "cyboard-wired-qc-station";
  buildInputs = [
    (pkgs.python3.withPackages (ps: [ ps.pyserial ]))
    pkgs.udisks   # auto-mount the RPI-RP2 bootloader drive during auto-flash
  ];
}
