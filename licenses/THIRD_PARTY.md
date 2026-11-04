# Third-party components / Учинчи томон компонентлари

BUKHARA CINEMA's own code and the following components are shipped together.
The Windows test `ffmpegIsLgpl` fails the build if the bundled FFmpeg reports
a GPL license. MLT is built from source with `GPL=OFF`; the build fails if any
MLT module other than core, avformat and sdl2 ends up in the package.
All of them are linked **dynamically** (separate DLL files next to
`BukharaCinema.exe`), so a user may replace them with compatible builds, as the
GNU LGPL requires.

| Component | License | Used for | Source |
| --- | --- | --- | --- |
| Qt 6 (Core, Gui, Widgets) | GNU LGPL v3 (`LGPL-3.0.txt`, `GPL-3.0.txt`) | User interface | https://download.qt.io/ |
| FFmpeg 8.1 (libavformat, libavcodec, libavfilter, libavutil, libswscale, libswresample) | GNU LGPL v2.1+ (`ffmpeg/LICENSE.txt`); LGPL-only build from https://github.com/BtbN/FFmpeg-Builds | Reading media files, thumbnails | https://ffmpeg.org/download.html |
| MLT Framework 7.42 (libmlt, libmlt++, modules core, avformat, sdl2) | GNU LGPL v2.1 (`mlt/COPYING`) | Video engine: timeline playback | https://github.com/mltframework/mlt |
| SDL 2 (MSYS2 package) | zlib | Sound output | https://libsdl.org/ |
| dlfcn-win32, libiconv (MSYS2 packages, needed by MLT) | MIT; GNU LGPL v2.1 | Module loading, text encoding | https://packages.msys2.org/ |
| MinGW-w64 GCC runtime (libgcc, libstdc++) | GPL v3 with GCC Runtime Library Exception | C++ runtime | https://gcc.gnu.org/ |
| winpthreads | MIT / ZPL | Threads runtime | https://www.mingw-w64.org/ |
| Qt dependencies from MSYS2 (zlib, libpng, freetype, harfbuzz, pcre2, zstd, double-conversion, ICU, md4c, …) | Permissive licenses (zlib, MIT, BSD, FreeType, ICU) | Fonts, images, text | https://packages.msys2.org/ |

The Windows build writes the exact list of packaged DLLs, their MSYS2 package,
version and license to `licenses/components.txt` and copies each package's
license files into `licenses/<package>/`.

## Build tools (not shipped inside the program)

| Tool | License | Note |
| --- | --- | --- |
| Inno Setup 6.7 | Free for **non-commercial** use only | Creates the installer. Commercial distribution needs a paid Inno Setup license, or a switch to NSIS (zlib license, free for any use). |

## Planned (not shipped yet)

| Component | License | Phase | Note |
| --- | --- | --- | --- |
| H.264 encoding | Intel Quick Sync (oneVPL, MIT) / Windows Media Foundation | 4 | Uses the codec licensed with Windows and the Intel driver |
