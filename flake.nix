{
    description = "The development environment for the Calculus package manager (on NixOS for now)"

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

            buildInputs = with pkgs; [
                git
                gcc
                nim
                lua
            ];
            shellHook = ''
                echo "Entered Calculus development environment";
            ''
        };
    };
}