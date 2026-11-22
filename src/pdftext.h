#ifndef PDFTEXT_H
#define PDFTEXT_H

#include <QByteArray>
#include <QString>

//----------------------------------------------

/**
 * @brief Plain text from a PDF (FlateDecode and LZWDecode content streams).
 * @param error set when nothing readable was found.
 */
QString extractPdfText(const QByteArray &data, QString *error);

#endif // PDFTEXT_H
