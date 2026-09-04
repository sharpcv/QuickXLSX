# Vendored dependencies

`packages/` contains XMake package definitions pinned to archives in `sources/`. The archives and SHA-256 hashes make dependency resolution independent of the network. Configure normally with `xmake f -c -y`; the root project registers this package directory before declaring requirements.

Versions: zlib-ng 2.3.3, minizip-ng 4.0.10, pugixml 1.15, simdutf 8.2.0, fast_float 8.2.1, fmt 10.2.1. Upstream license files are contained in each source archive.
