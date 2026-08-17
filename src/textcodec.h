#ifndef TEXTCODEC_H
#define TEXTCODEC_H

#include <QString>
#include <QStringList>
#include <QByteArray>

/// Text conversion for the encodings the file dialog offers.
///
/// Qt 6 dropped QTextCodec, and QStringConverter only knows the unicode
/// encodings plus latin-1 - "windows-1251" and friends are simply unsupported,
/// which is what made saving a cp1251 script fail. Qt 6.8 can reach ICU's
/// converters by name, but only when Qt was built with ICU: the distribution
/// Qt the deb is built against is older than 6.8, and the official windows
/// binaries carry no ICU. Since the point of these encodings is that files in
/// them open everywhere the program runs, the few single-byte ones we care
/// about are converted from tables kept here, and only the rest is handed to
/// Qt.
///
/// Names are matched leniently ("CP1251", "windows-1251" and "cp_1251" are one
/// and the same) and reported back in one canonical spelling.
namespace TextCodec
{

/// Every encoding this unit converts, in canonical spelling. The same list on
/// every platform and every Qt 6 - which is the reason this unit exists.
QStringList availableEncodings();

/// The canonical spelling of `name`, or an empty string when nothing can
/// convert it. Beyond availableEncodings() this also accepts whatever the Qt in
/// use happens to support, so an ICU-enabled build keeps taking exotic names
/// from the settings.
QString canonicalName(const QString &name);

/// The unicode encoding `data` announces with a leading BOM, or an empty string
/// when there is none. QTextStream used to do this detection on its own
/// (autoDetectUnicode), overriding the encoding it had been given, and openFile
/// keeps that behaviour: a file that says what it is is believed.
QString bomEncoding(const QByteArray &data);

/// Decodes `data`, which is assumed to be whole (not a chunk of a stream).
/// A leading BOM of the unicode encodings is consumed, as QTextStream used to.
/// `ok`, when given, reports whether every byte was meaningful; malformed input
/// still decodes, with U+FFFD in place of the bad bytes.
QString decode(const QByteArray &data, const QString &encoding, bool *ok = nullptr);

/// Encodes `text` whole. No BOM is written, again as QTextStream did.
/// `ok`, when given, is false if the text does not fit the encoding; the
/// characters that do not are replaced with '?' rather than dropped.
QByteArray encode(const QString &text, const QString &encoding, bool *ok = nullptr);

} // namespace TextCodec

#endif // TEXTCODEC_H
