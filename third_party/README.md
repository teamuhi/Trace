# third_party

Vendored, single-header dependencies that are not available through vcpkg live here.

## msf_gif

- File: `msf_gif/msf_gif.h`, version 2.4 (GIF encoder), used by `src/io/image_io.cpp` to save GIF files.
- Source: https://github.com/notnullnotvoid/msf_gif, commit `e280bc2189ed32b743c7f634cd21c0eb5def7046` (2025-09-10), unmodified.
- License: MIT or public domain (Unlicense), at your choice; the full text is at the end of the header.
- It is compiled once, in `src/io/codec_impl.cpp`, and added to the include path as a system directory.

To update it, replace the header with a newer upstream copy, update the version and commit above, and run the I/O tests.

## nanosvg

- Files: `nanosvg/nanosvg.h` (SVG parser) and `nanosvg/nanosvgrast.h` (rasterizer), the `master` branch of https://github.com/memononen/nanosvg as of 2026-10-31, unmodified. License: zlib, in `nanosvg/LICENSE.txt`.
- It is compiled once, in `src/core/svg_icon.cpp`, and added to the include path of `trace_core` as a system directory. `rasterizeSvgCoverage` draws an SVG into a white-on-transparent coverage mask that the app tints; `src/app/svg_icons.cpp` caches the textures.
- It does not read `<style>` blocks (`inlineSvgClassStyles` copies simple class rules onto the elements first) and skips embedded bitmaps, text, gradients and filters. It also drops zero-length segments, so a dot has to be drawn as a short line (0.1 units or more) with a round cap.

To update it, replace the two headers with newer upstream copies, update the date above, and run the tests (`test_svg_icon.cpp` rasterizes every icon).

## fonts

- `fonts/Inter.ttf` (variable font, default instance): Inter, https://github.com/rsms/inter, from the Google Fonts repository. License: SIL Open Font License 1.1, see `fonts/OFL-Inter.txt`.
- `fonts/JetBrainsMono-Regular.ttf`: JetBrains Mono, https://github.com/JetBrains/JetBrainsMono. License: SIL Open Font License 1.1, see `fonts/OFL-JetBrainsMono.txt`.
- Both are unmodified. CMake turns them into C arrays in the build directory (`generated/embedded_fonts.cpp`) at configure time, and `src/app/fonts.cpp` loads them from memory as the "Inter" and "JetBrains Mono" interface fonts. Nothing is read from disk at run time.

To update a font, replace the file, keep the license text next to it, and reconfigure.
