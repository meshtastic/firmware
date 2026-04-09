{
  description = "Nix flake to compile Meshtastic firmware";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

    # Shim to make flake.nix work with stable Nix.
    flake-compat = {
      url = "github:NixOS/flake-compat";
      flake = false;
    };
  };

  outputs =
    inputs:
    let
      lib = inputs.nixpkgs.lib;

      forAllSystems =
        fn:
        lib.genAttrs lib.systems.flakeExposed (
          system:
          fn {
            pkgs = import inputs.nixpkgs {
              inherit system;
            };
            inherit system;
          }
        );
    in
    {
      devShells = forAllSystems (
        { pkgs, ... }:
        let
          python3 = pkgs.python312.withPackages (
            ps: with ps; [
              google
            ]
          );
        in
        {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              python3
              platformio
            ];

            shellHook = ''
              # Set up PlatformIO to use a local core directory.
              export PLATFORMIO_CORE_DIR=$PWD/.platformio
              # Tell pip to put packages into $PIP_PREFIX instead of the usual
              # location. This is especially necessary under NixOS to avoid having
              # pip trying to write to the read-only Nix store. For more info,
              # see https://wiki.nixos.org/wiki/Python
              export PIP_PREFIX=$PWD/.python3
              export PYTHONPATH="$PIP_PREFIX/${python3.sitePackages}"
              export PATH="$PIP_PREFIX/bin:$PATH"
              # Avoids reproducibility issues with some Python packages
              # See https://nixos.org/manual/nixpkgs/stable/#python-setup.py-bdist_wheel-cannot-create-.whl
              unset SOURCE_DATE_EPOCH
            '';
          };
        }
      );
    };
}
