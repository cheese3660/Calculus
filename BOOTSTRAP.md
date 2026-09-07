# Bootstrapping Calculus Packages

There is a single package called bootstrap that has the following in a ./bin, ./lib, and ./include format
-> musl, binutils, gcc (built with musl), busybox

This needs to bootstrap a fully featured GLIBC then GCC built with that GLIBC

What are the steps to achieve this?