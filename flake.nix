{
  description = "optimiCe - a runner for metaheuristic optimization algorithms, written in C";

  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";

  outputs =
    { self, nixpkgs, ... }:
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
          pkg-config
          tomlc17
        ];
      };

      packages.x86_64-linux.default = pkgs.stdenv.mkDerivation {
        pname = "optimice";
        version = "0.1.0";
        src = ./.;
        nativeBuildInputs = [ pkgs.pkg-config ];
        buildInputs = [ pkgs.tomlc17 ];
        # The source Nix builds from has no .git, so the commit is passed in; dirtyShortRev is
        # what the flake gets when built from a tree with uncommitted changes
        buildPhase = "make VERSION=${self.shortRev or self.dirtyShortRev or "unknown"}";
        installPhase = ''
          mkdir -p $out/bin
          cp optimice $out/bin/
        '';
      };
    };
}
