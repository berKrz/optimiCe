{
  description = "optimiCe - a runner for metaheuristic optimization algorithms, written in C";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
    in
    {
      devShells.x86_64-linux.default = pkgs.mkShell {
        packages = with pkgs; [
          gcc
          clang-tools
          gdb
          bear
          tomlc17
        ];
      };

      packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
        pname = "optimice";
        version = "0.1.0";
        src = ./.;
        buildPhase = "make";
        installPhase = ''
          mkdir -p $out/bin
          cp optimice $out/bin/
        '';
      };
    };
}
