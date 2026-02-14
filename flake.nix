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
              protobuf
              grpcio-tools
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
              # Add PlatformIO tool packages (mklittlefs, esptool, etc.) to PATH.
              # PIO's platform plugin only registers tools for explicit targets
              # (e.g. buildfs), but Meshtastic builds the LittleFS image as part
              # of the default build via platformio-custom.py.
              for d in "$PLATFORMIO_CORE_DIR"/packages/tool-*/; do
                [ -d "$d" ] && export PATH="$d:$PATH"
              done
              # Do NOT set PIP_PREFIX here. PlatformIO uses `pip install --target`
              # internally (e.g. for esptool), and pip's --target implementation
              # uses --home under the hood. PIP_PREFIX adds --prefix, causing
              # "Cannot set --home and --prefix together". Python deps needed at
              # build time (protobuf, grpcio-tools) are provided via withPackages.
              # Avoids reproducibility issues with some Python packages.
              # See https://nixos.org/manual/nixpkgs/stable/#python-setup.py-bdist_wheel-cannot-create-.whl
              unset SOURCE_DATE_EPOCH
            '';
          };
        }
      );
    };
}
