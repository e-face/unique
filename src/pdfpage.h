#ifndef PDFPAGE_H
#define PDFPAGE_H

#include <QByteArray>
#include <QList>
#include <QString>

//----------------------------------------------

/**
 * @brief One rendered PDF page. error is set when this page was skipped.
 */
struct PdfRenderedPage
{
    int     page;
    QString path;
    int     width;
    int     height;
    QString error;
};

//----------------------------------------------

/**
 * @brief Render PDF pages to PNG with PDFium.
 *        The shared library is loaded from the program directory:
 *        pdfium.dll, libpdfium.so, or libpdfium.dylib.
 * @param pages 1-based page numbers.
 * @param dpi dots per inch, already clamped by the caller.
 * @param cancelled optional flag checked between pages.
 * @return a fatal error, or an empty string when rendering finished.
 *         A skipped page is reported on that PdfRenderedPage.
 */
QString renderPdfPages(const QByteArray &data, const QList<int> &pages, int dpi,
                       const QString &outputDir, const QString &filePrefix,
                       QList<PdfRenderedPage> *images, const bool *cancelled);

#endif // PDFPAGE_H
