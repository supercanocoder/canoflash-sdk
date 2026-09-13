# Third-party notices

The SDK's own code and documentation use the [MIT licence](LICENSE).

The lobby includes the Butano 8x16 font and associated font/UTF-8 helper
definitions by Gustavo Valiente. The bundled graphics are the variant used
by the CanoFlash interface. Their original zlib notice is reproduced in
[LICENSES/Butano-zlib.txt](LICENSES/Butano-zlib.txt), and the source attribution
is retained in the font header.

Butano itself is an external build dependency for two examples and is not
vendored in this SDK. Its own distribution contains additional dependency
notices. See [the Butano repository](https://github.com/GValiente/butano).

The CanoFlash logo is bundled with the lobby example.

## Compiled example ROMs

The ROMs in `roms/` include code from their build dependencies. The plain C
example uses libgba and the devkitARM runtime; the Butano examples use Butano
and its dependencies. Upstream notices are preserved in
[LICENSES/runtime](LICENSES/runtime), including the complete notice set shipped
with the Butano revision recorded in [the ROM build notes](roms/README.md).
This notice set covers optional engine components as well; it does not mean
every component is used by each example.
