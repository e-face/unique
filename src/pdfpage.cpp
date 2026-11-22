#include "pdfpage.h"
//----------------------------------------------
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

//----------------------------------------------

namespace
{

const int kMaxSide = 8000;
const int kBitmapBgra = 4;
const int kRenderFlags = 0x01 | 0x02 | 0x04;

typedef void (*InitLibraryFn)();
typedef void *(*LoadMemDocumentFn)(const void *data, int size, const char *password);
typedef void (*CloseDocumentFn)(void *document);
typedef int (*GetPageCountFn)(void *document);
typedef void *(*LoadPageFn)(void *document, int pageIndex);
typedef void (*ClosePageFn)(void *page);
typedef double (*GetPageSizeFn)(void *page);
typedef unsigned long (*GetLastErrorFn)();
typedef void *(*BitmapCreateExFn)(int width, int height, int format,
                                  void *firstScan, int stride);
typedef int (*BitmapFillRectFn)(void *bitmap, int left, int top, int width, int height,
                                unsigned int color);
typedef void (*RenderPageBitmapFn)(void *bitmap, void *page, int startX, int startY,
                                   int sizeX, int sizeY, int rotate, int flags);
typedef void (*BitmapDestroyFn)(void *bitmap);

struct PdfiumApi
{
    QLibrary            library;
    bool                ready;
    InitLibraryFn       initLibrary;
    LoadMemDocumentFn   loadMemDocument;
    CloseDocumentFn     closeDocument;
    GetPageCountFn      getPageCount;
    LoadPageFn          loadPage;
    ClosePageFn         closePage;
    GetPageSizeFn       getPageWidth;
    GetPageSizeFn       getPageHeight;
    GetLastErrorFn      getLastError;
    BitmapCreateExFn    bitmapCreateEx;
    BitmapFillRectFn    bitmapFillRect;
    RenderPageBitmapFn  renderPageBitmap;
    BitmapDestroyFn     bitmapDestroy;

    PdfiumApi()
        : ready(false)
        , initLibrary(0)
        , loadMemDocument(0)
        , closeDocument(0)
        , getPageCount(0)
        , loadPage(0)
        , closePage(0)
        , getPageWidth(0)
        , getPageHeight(0)
        , getLastError(0)
        , bitmapCreateEx(0)
        , bitmapFillRect(0)
        , renderPageBitmap(0)
        , bitmapDestroy(0)
    {
    }
};

//----------------------------------------------

static QString findLibrary()
{
    const QDir dir(QCoreApplication::applicationDirPath());
    const QStringList names = QStringList()
        << QString::fromUtf8("pdfium.dll")
        << QString::fromUtf8("libpdfium.so")
        << QString::fromUtf8("libpdfium.dylib");
    foreach (const QString &name, names)
    {
        const QString path = dir.filePath(name);
        if (QFile::exists(path))
            return path;
    }
    const QStringList bundled = QStringList()
        << QString::fromUtf8("../3rdparty/pdfium/bin/pdfium.dll")
        << QString::fromUtf8("../3rdparty/pdfium/lib/libpdfium.so")
        << QString::fromUtf8("../3rdparty/pdfium/lib/libpdfium.dylib");
    foreach (const QString &relative, bundled)
    {
        const QString path = QDir::cleanPath(dir.filePath(relative));
        if (QFile::exists(path))
            return path;
    }
    return QString();
}

//----------------------------------------------

template <typename Fn>
static bool takeSymbol(QLibrary *library, const char *name, Fn *slot, QString *error)
{
    *slot = (Fn)library->resolve(name);
    if (*slot)
        return true;
    if (error)
    {
        *error = QString::fromUtf8("PDF renderer library is missing %1.")
                     .arg(QString::fromLatin1(name));
    }
    return false;
}

//----------------------------------------------

static PdfiumApi *libraryApi(QString *error)
{
    static PdfiumApi api;
    if (api.ready)
        return &api;

    const QString path = findLibrary();
    if (path.isEmpty())
    {
        if (error)
        {
            *error = QString::fromUtf8(
                "PDF renderer library was not found next to the program. "
                "Expected pdfium.dll, libpdfium.so, or libpdfium.dylib.");
        }
        return 0;
    }

    api.library.setFileName(path);
    api.library.setLoadHints(QLibrary::PreventUnloadHint);
    if (!api.library.load())
    {
        QString base = path;
        const int dot = base.lastIndexOf(QChar('.'));
        const int slash = base.lastIndexOf(QChar('/'));
        if (dot > slash)
            base.truncate(dot);
        api.library.setFileName(base);
        if (!api.library.load())
        {
            if (error)
            {
                *error = QString::fromUtf8("Failed to load the PDF renderer (%1): %2")
                             .arg(path, api.library.errorString());
            }
            return 0;
        }
    }

    if (!takeSymbol(&api.library, "FPDF_InitLibrary", &api.initLibrary, error)
        || !takeSymbol(&api.library, "FPDF_LoadMemDocument", &api.loadMemDocument, error)
        || !takeSymbol(&api.library, "FPDF_CloseDocument", &api.closeDocument, error)
        || !takeSymbol(&api.library, "FPDF_GetPageCount", &api.getPageCount, error)
        || !takeSymbol(&api.library, "FPDF_LoadPage", &api.loadPage, error)
        || !takeSymbol(&api.library, "FPDF_ClosePage", &api.closePage, error)
        || !takeSymbol(&api.library, "FPDF_GetPageWidth", &api.getPageWidth, error)
        || !takeSymbol(&api.library, "FPDF_GetPageHeight", &api.getPageHeight, error)
        || !takeSymbol(&api.library, "FPDF_GetLastError", &api.getLastError, error)
        || !takeSymbol(&api.library, "FPDFBitmap_CreateEx", &api.bitmapCreateEx, error)
        || !takeSymbol(&api.library, "FPDFBitmap_FillRect", &api.bitmapFillRect, error)
        || !takeSymbol(&api.library, "FPDF_RenderPageBitmap", &api.renderPageBitmap, error)
        || !takeSymbol(&api.library, "FPDFBitmap_Destroy", &api.bitmapDestroy, error))
    {
        api.library.unload();
        return 0;
    }

    api.initLibrary();
    api.ready = true;
    return &api;
}

//----------------------------------------------

static QString documentError(unsigned long code)
{
    if (code == 3)
        return QString::fromUtf8("Not a PDF, or the file is damaged.");
    if (code == 4)
        return QString::fromUtf8("This PDF asks for a password.");
    if (code == 5)
        return QString::fromUtf8("This PDF uses a security scheme the renderer does not support.");
    return QString::fromUtf8("Failed to open the PDF (error %1).").arg(code);
}

}

//----------------------------------------------

QString renderPdfPages(const QByteArray &data, const QList<int> &pages, int dpi,
                       const QString &outputDir, const QString &filePrefix,
                       QList<PdfRenderedPage> *images, const bool *cancelled)
{
    if (!images)
        return QString::fromUtf8("Nowhere to store rendered pages.");
    images->clear();
    if (data.isEmpty())
        return QString::fromUtf8("PDF is empty.");
    if (pages.isEmpty())
        return QString::fromUtf8("No pages to render.");
    if (dpi < 1)
        return QString::fromUtf8("dpi must be positive.");

    static QMutex mutex;
    QMutexLocker lock(&mutex);

    QString error;
    PdfiumApi *api = libraryApi(&error);
    if (!api)
        return error;

    void *document = api->loadMemDocument(data.constData(), data.size(), 0);
    if (!document)
        return documentError(api->getLastError());

    const int pageCount = api->getPageCount(document);
    foreach (int pageNumber, pages)
    {
        PdfRenderedPage rendered;
        rendered.page = pageNumber;
        rendered.width = 0;
        rendered.height = 0;
        if (cancelled && *cancelled)
        {
            api->closeDocument(document);
            if (images->isEmpty())
                return QString::fromUtf8("Interrupted.");
            return QString();
        }
        if (pageNumber < 1 || pageNumber > pageCount)
        {
            rendered.error = QString::fromUtf8("Page %1 is outside this PDF (%2 pages).")
                                 .arg(pageNumber)
                                 .arg(pageCount);
            images->append(rendered);
            continue;
        }

        void *page = api->loadPage(document, pageNumber - 1);
        if (!page)
        {
            rendered.error = QString::fromUtf8("Failed to open page %1.").arg(pageNumber);
            images->append(rendered);
            continue;
        }

        const int widthPx = qRound(api->getPageWidth(page) * dpi / 72.0);
        const int heightPx = qRound(api->getPageHeight(page) * dpi / 72.0);
        if (widthPx < 1 || heightPx < 1 || widthPx > kMaxSide || heightPx > kMaxSide)
        {
            api->closePage(page);
            rendered.error = QString::fromUtf8(
                                 "Page %1 would be %2x%3 pixels. Lower dpi (side limit is %4).")
                                 .arg(pageNumber)
                                 .arg(widthPx)
                                 .arg(heightPx)
                                 .arg(kMaxSide);
            images->append(rendered);
            continue;
        }

        QImage image(widthPx, heightPx, QImage::Format_ARGB32);
        if (image.isNull())
        {
            api->closePage(page);
            rendered.error = QString::fromUtf8("Not enough memory for page %1.").arg(pageNumber);
            images->append(rendered);
            continue;
        }
        image.fill(0xffffffff);
        void *bitmap = api->bitmapCreateEx(widthPx, heightPx, kBitmapBgra,
                                           image.bits(), image.bytesPerLine());
        if (!bitmap)
        {
            api->closePage(page);
            rendered.error = QString::fromUtf8("Failed to create the bitmap for page %1.")
                                 .arg(pageNumber);
            images->append(rendered);
            continue;
        }
        api->bitmapFillRect(bitmap, 0, 0, widthPx, heightPx, 0xffffffff);
        api->renderPageBitmap(bitmap, page, 0, 0, widthPx, heightPx, 0, kRenderFlags);
        api->bitmapDestroy(bitmap);
        api->closePage(page);

        const QString fileName = QString::fromUtf8("%1-p%2-%3dpi.png")
                                     .arg(filePrefix)
                                     .arg(pageNumber)
                                     .arg(dpi);
        const QString path = QDir(outputDir).filePath(fileName);
        if (!image.save(path, "PNG"))
        {
            rendered.error = QString::fromUtf8("Failed to write %1.").arg(path);
            images->append(rendered);
            continue;
        }
        rendered.path = path;
        rendered.width = widthPx;
        rendered.height = heightPx;
        images->append(rendered);
    }

    api->closeDocument(document);
    return QString();
}
