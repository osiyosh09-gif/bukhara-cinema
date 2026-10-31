# Third-party components / Учинчи томон компонентлари

BUKHARA CINEMA's own code and the following components are shipped together.
All of them are linked **dynamically** (separate DLL files next to
`BukharaCinema.exe`), so a user may replace them with compatible builds, as the
GNU LGPL requires.

| Component | License | Used for | Source |
| --- | --- | --- | --- |
| Qt 6 (Core, Gui, Widgets) | GNU LGPL v3 (`LGPL-3.0.txt`, `GPL-3.0.txt`) | User interface | https://download.qt.io/ |
| MinGW-w64 GCC runtime (libgcc, libstdc++) | GPL v3 with GCC Runtime Library Exception | C++ runtime | https://gcc.gnu.org/ |
| winpthreads | MIT / ZPL | Threads runtime | https://www.mingw-w64.org/ |
| Qt dependencies from MSYS2 (zlib, libpng, freetype, harfbuzz, pcre2, zstd, double-conversion, ICU, md4c, …) | Permissive licenses (zlib, MIT, BSD, FreeType, ICU) | Fonts, images, text | https://packages.msys2.org/ |

The Windows build writes the exact list of packaged DLLs, their MSYS2 package,
version and license to `licenses/components.txt` and copies each package's
license files into `licenses/<package>/`.

## Planned (not shipped yet)

| Component | License | Phase | Note |
| --- | --- | --- | --- |
| MLT Framework | GNU LGPL v2.1 | 2 | GPL-only MLT modules (e.g. movit, frei0r plugins) will **not** be shipped |
| FFmpeg | GNU LGPL v2.1+ build | 1–4 | Built **without** GPL parts (no libx264 / libx265) |
| H.264 encoding | Intel Quick Sync (oneVPL, MIT) / Windows Media Foundation | 4 | Uses the codec licensed with Windows and the Intel driver |
