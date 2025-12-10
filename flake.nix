{
  description = "Nix flake to compile Meshtastic firmware";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      ...
    }:
    (flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        python312 = pkgs.python312.withPackages (
          ps: with ps; [
            google
          ]
        );
      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            python312
            pkgs.platformio
          ];

          shellHook = ''
            # Set up PlatformIO to use a local core directory.
            export PLATFORMIO_CORE_DIR=$PWD/.platformio
            # Tell pip to put packages into $PIP_PREFIX instead of the usual
            # location. This is especialy necessary under NixOS to avoid having
            # pip trying to write to the read-only Nix store. For more info,
            # see https://wiki.nixos.org/wiki/Python
            export PIP_PREFIX=$PWD/.python312
            export PYTHONPATH="$PIP_PREFIX/${python312.sitePackages}"
            export PATH="$PIP_PREFIX/bin:$PATH"
            # Avoids reproducibility issues with some Python packages
            # See https://nixos.org/manual/nixpkgs/stable/#python-setup.py-bdist_wheel-cannot-create-.whl
            unset SOURCE_DATE_EPOCH
          '';
        };
      }
    ));
}
