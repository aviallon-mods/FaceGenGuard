{
  description = "FaceGenGuard off-game test toolchain. The plugin itself is a Windows SKSE DLL and cross-builds via tools/build.sh (xwin + vcpkg + clang-cl); this shell exists for the plain-C++ test suite in tests/, which builds and runs on Linux too.";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = [
            pkgs.cmake
            pkgs.gcc
            pkgs.ninja
          ];
          shellHook = ''
            echo "FaceGenGuard test shell (Linux):"
            echo "  cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release"
            echo "  cmake --build build-tests && ./build-tests/fgtests"
            echo "The plugin DLL cross-builds with tools/build.sh (see tools/setup-toolchain.sh)."
          '';
        };
      });
    };
}
