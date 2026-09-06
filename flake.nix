{
    description = "The development environment for the Calculus package manager (on NixOS for now)";

    inputs = {
        nixpkgs.url = "nixpkgs/nixos-26.05";
    };

    outputs = { self, nixpkgs }:
    let
        system = "x86_64-linux";
        pkgs = import nixpkgs { inherit system; };
    in
    {
        devShells.${system}.default = pkgs.mkShell {
            name = "calculus";
            buildInputs = with pkgs; [
                git
                gcc
                lua5_5
                cmake
                pkg-config
                libgit2
            ];

            packages = with pkgs; [
                clang-tools
                gdb
            ];
        };
    };
}