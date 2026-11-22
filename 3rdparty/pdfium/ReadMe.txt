PDFium 160.0.8165.0
Precompiled shared library used to render PDF pages to PNG.

The program calls the public C API (FPDF_LoadMemDocument, FPDF_RenderPageBitmap)
and does not use a platform PDF viewer. The same calls run on Windows, Linux,
and macOS. Only the file name of the library changes:

  Windows   pdfium.dll        beside the executable
  Linux     libpdfium.so
  macOS     libpdfium.dylib

This tree contains the Windows x86 build from
https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium/8165
(pdfium-win-x86.tgz). Linux and macOS builds from that same release drop in
under the names above.

Licenses for PDFium and the libraries it contains are in LICENSE and licenses/.
