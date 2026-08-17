#include <QtTest>
#include "textcodec.h"

/// The point of these tests is that they hold on a Qt without ICU, where
/// QStringConverter knows none of the single-byte encodings: the expectations
/// are spelled out as literal bytes rather than compared against Qt.
class TestTextCodec : public QObject
{
    Q_OBJECT
private slots:
    void decodesKnownBytes_data();
    void decodesKnownBytes();
    void encodesKnownText_data();
    void encodesKnownText();
    void roundTripsEveryByte_data();
    void roundTripsEveryByte();
    void keepsAsciiIntact_data();
    void keepsAsciiIntact();
    void foldsNames_data();
    void foldsNames();
    void offersTheSingleByteEncodings();
    void handlesUnicodeAsQTextStreamDid();
    void detectsBom_data();
    void detectsBom();
    void substitutesUnmappableCharacters();
    void refusesUnknownEncoding();
    void handlesEmptyInput();
};

namespace
{
// "Привет" and "Ёж" as bytes, per encoding
const QByteArray privet_cp1251 = QByteArray::fromHex("cff0e8e2e5f2");
const QByteArray privet_cp866  = QByteArray::fromHex("8fe0a8a2a5e2");
const QString    privet        = QString::fromUtf8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");

const QByteArray yozh_cp1251 = QByteArray::fromHex("a8e6");
const QByteArray yozh_cp866  = QByteArray::fromHex("f0a6");
const QString    yozh        = QString::fromUtf8("\xD0\x81\xD0\xB6");
} // anonymous namespace


void TestTextCodec::decodesKnownBytes_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("encoding");
    QTest::addColumn<QString>("text");

    QTest::newRow("cp1251 privet") << privet_cp1251 << "windows-1251" << privet;
    QTest::newRow("cp866 privet")  << privet_cp866  << "cp866"        << privet;
    QTest::newRow("cp1251 yozh")   << yozh_cp1251   << "windows-1251" << yozh;
    QTest::newRow("cp866 yozh")    << yozh_cp866    << "cp866"        << yozh;
    // the letters cp1251 has and cp866 does not, and the box drawing of cp866
    QTest::newRow("cp1251 ge with upturn") << QByteArray::fromHex("a5b4")
                                           << "windows-1251"
                                           << QString::fromUtf8("\xD2\x90\xD2\x91");
    QTest::newRow("cp866 box") << QByteArray::fromHex("c4b3dacd")
                               << "cp866"
                               << QString::fromUtf8("\xE2\x94\x80\xE2\x94\x82\xE2\x94\x8C\xE2\x95\x90");
    QTest::newRow("cp1251 numero") << QByteArray::fromHex("b9") << "windows-1251"
                                   << QString::fromUtf8("\xE2\x84\x96");
    QTest::newRow("cp866 numero")  << QByteArray::fromHex("fc") << "cp866"
                                   << QString::fromUtf8("\xE2\x84\x96");
    // the cp1252 range that is not latin-1: smart quotes, en/em dash, euro
    QTest::newRow("cp1252 punctuation")
            << QByteArray::fromHex("9192939496978085")
            << "windows-1252"
            << QString::fromUtf8("\xE2\x80\x98\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x9D"
                                 "\xE2\x80\x93\xE2\x80\x94\xE2\x82\xAC\xE2\x80\xA6");

    // latin-1 accented letters are shared by cp1252 and iso-8859-1
    QTest::newRow("cp1252 latin1 part") << QByteArray::fromHex("e9fc")
                                        << "windows-1252"
                                        << QString::fromUtf8("\xC3\xA9\xC3\xBC");
    // undefined by the vendor mapping, kept as the matching C1 point
    QTest::newRow("cp1251 undefined 0x98") << QByteArray::fromHex("98")
                                           << "windows-1251"
                                           << QString(QChar(0x0098));
    QTest::newRow("cp1252 undefined 0x81") << QByteArray::fromHex("81")
                                           << "windows-1252"
                                           << QString(QChar(0x0081));
}

void TestTextCodec::decodesKnownBytes()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, encoding);
    QFETCH(QString, text);

    bool ok = false;
    QCOMPARE(TextCodec::decode(bytes, encoding, &ok), text);
    QVERIFY(ok);
}

void TestTextCodec::encodesKnownText_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("encoding");
    QTest::addColumn<QByteArray>("bytes");

    QTest::newRow("cp1251 privet") << privet << "windows-1251" << privet_cp1251;
    QTest::newRow("cp866 privet")  << privet << "cp866"        << privet_cp866;
    QTest::newRow("cp1251 yozh")   << yozh   << "windows-1251" << yozh_cp1251;
    QTest::newRow("cp866 yozh")    << yozh   << "cp866"        << yozh_cp866;
    QTest::newRow("cp1252 euro")   << QString::fromUtf8("\xE2\x82\xAC")
                                   << "windows-1252" << QByteArray::fromHex("80");
    QTest::newRow("ascii stays ascii") << QStringLiteral("select 1;\n")
                                       << "windows-1251"
                                       << QByteArray("select 1;\n");
}

void TestTextCodec::encodesKnownText()
{
    QFETCH(QString, text);
    QFETCH(QString, encoding);
    QFETCH(QByteArray, bytes);

    bool ok = false;
    QCOMPARE(TextCodec::encode(text, encoding, &ok), bytes);
    QVERIFY(ok);
}

void TestTextCodec::roundTripsEveryByte_data()
{
    QTest::addColumn<QString>("encoding");
    QTest::newRow("windows-1251") << "windows-1251";
    QTest::newRow("cp866")        << "cp866";
    QTest::newRow("windows-1252") << "windows-1252";
}

/// Every one of the 256 bytes must decode and come back unchanged: a file that
/// is not really in this encoding still has to survive an open and a save.
void TestTextCodec::roundTripsEveryByte()
{
    QFETCH(QString, encoding);

    QByteArray all(256, Qt::Uninitialized);
    for (int i = 0; i < 256; ++i)
        all[i] = char(i);

    bool ok = false;
    const QString text = TextCodec::decode(all, encoding, &ok);
    QVERIFY(ok);
    QCOMPARE(text.size(), 256);
    // bijective, so no two bytes may share a code point
    QCOMPARE(QSet<QChar>(text.cbegin(), text.cend()).size(), 256);

    ok = false;
    QCOMPARE(TextCodec::encode(text, encoding, &ok), all);
    QVERIFY(ok);
}

void TestTextCodec::keepsAsciiIntact_data()
{
    roundTripsEveryByte_data();
}

/// The low half is ASCII in all three tables. Worth its own test because ICU's
/// IBM866 differs here (it swaps 0x1A/0x1C/0x7F) and a file written by the Qt 5
/// build must read back unchanged.
void TestTextCodec::keepsAsciiIntact()
{
    QFETCH(QString, encoding);

    QByteArray ascii(128, Qt::Uninitialized);
    for (int i = 0; i < 128; ++i)
        ascii[i] = char(i);

    const QString text = TextCodec::decode(ascii, encoding);
    QCOMPARE(text.size(), 128);
    for (int i = 0; i < 128; ++i)
        QCOMPARE(text.at(i).unicode(), char16_t(i));
    QCOMPARE(TextCodec::encode(text, encoding), ascii);
}

void TestTextCodec::foldsNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("canonical");

    QTest::newRow("windows-1251") << "windows-1251" << "windows-1251";
    QTest::newRow("Windows-1251") << "Windows-1251" << "windows-1251";
    QTest::newRow("cp1251")       << "cp1251"       << "windows-1251";
    QTest::newRow("CP1251")       << "CP1251"       << "windows-1251";
    QTest::newRow("cp_1251")      << "cp_1251"      << "windows-1251";
    QTest::newRow("windows 1251") << "windows 1251" << "windows-1251";
    QTest::newRow("1251")         << "1251"         << "windows-1251";
    QTest::newRow("cp866")        << "cp866"        << "cp866";
    QTest::newRow("CP-866")       << "CP-866"       << "cp866";
    QTest::newRow("IBM866")       << "IBM866"       << "cp866";
    QTest::newRow("windows-1252") << "windows-1252" << "windows-1252";
    QTest::newRow("cp1252")       << "cp1252"       << "windows-1252";
    // whatever Qt itself supports keeps working
    QTest::newRow("UTF-8")    << "UTF-8"    << "UTF-8";
    QTest::newRow("utf8")     << "utf8"     << "UTF-8";
    QTest::newRow("UTF-16LE") << "UTF-16LE" << "UTF-16LE";
    QTest::newRow("nonsense") << "no-such-encoding" << "";
    QTest::newRow("empty")    << ""         << "";
}

void TestTextCodec::foldsNames()
{
    QFETCH(QString, name);
    QFETCH(QString, canonical);
    QCOMPARE(TextCodec::canonicalName(name), canonical);
}

void TestTextCodec::offersTheSingleByteEncodings()
{
    const QStringList all = TextCodec::availableEncodings();
    // the three that Qt alone cannot do, which is why this unit exists
    QVERIFY(all.contains("windows-1251"));
    QVERIFY(all.contains("cp866"));
    QVERIFY(all.contains("windows-1252"));
    QVERIFY(all.contains("UTF-8"));
    // every name offered must be usable, and already canonical
    for (const QString &name: all)
        QCOMPARE(TextCodec::canonicalName(name), name);
}

/// The unicode paths must stay byte-identical to what the QTextStream they
/// replaced produced: no BOM on write, a BOM eaten on read.
void TestTextCodec::handlesUnicodeAsQTextStreamDid()
{
    const QString text = QStringLiteral("ab");

    QCOMPARE(TextCodec::encode(text, "UTF-8"), QByteArray("ab"));
    QCOMPARE(TextCodec::encode(text, "UTF-16LE"), QByteArray::fromHex("61006200"));
    QCOMPARE(TextCodec::encode(text, "UTF-16BE"), QByteArray::fromHex("00610062"));

    QCOMPARE(TextCodec::decode(QByteArray::fromHex("efbbbf6162"), "UTF-8"), text);
    QCOMPARE(TextCodec::decode(QByteArray::fromHex("fffe61006200"), "UTF-16"), text);
    QCOMPARE(TextCodec::decode(QByteArray::fromHex("feff00610062"), "UTF-16"), text);

    // a BOM is not text, so a file consisting of one decodes to nothing
    QVERIFY(TextCodec::decode(QByteArray::fromHex("efbbbf"), "UTF-8").isEmpty());

    // non-BMP characters survive utf-8 whole
    const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
    QCOMPARE(TextCodec::decode(TextCodec::encode(emoji, "UTF-8"), "UTF-8"), emoji);
}

void TestTextCodec::detectsBom_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("encoding");

    QTest::newRow("utf-8")    << QByteArray::fromHex("efbbbf6162") << "UTF-8";
    QTest::newRow("utf-16le") << QByteArray::fromHex("fffe6100")   << "UTF-16LE";
    QTest::newRow("utf-16be") << QByteArray::fromHex("feff0061")   << "UTF-16BE";
    QTest::newRow("utf-32le") << QByteArray::fromHex("fffe000061000000") << "UTF-32LE";
    QTest::newRow("utf-32be") << QByteArray::fromHex("0000feff00000061") << "UTF-32BE";
    QTest::newRow("none")     << QByteArray("select 1")            << "";
    QTest::newRow("cp1251 text") << privet_cp1251 << "";
    QTest::newRow("empty")    << QByteArray()                      << "";
    // a lone 0xFF 0xFE is a utf-16 mark, not the start of a utf-32 one
    QTest::newRow("short")    << QByteArray::fromHex("fffe")        << "UTF-16LE";
}

void TestTextCodec::detectsBom()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, encoding);
    QCOMPARE(TextCodec::bomEncoding(data), encoding);
}

void TestTextCodec::substitutesUnmappableCharacters()
{
    // a chinese character has no place in cp1251
    bool ok = true;
    QCOMPARE(TextCodec::encode(QString::fromUtf8("a\xE4\xB8\xAD" "b"), "windows-1251", &ok),
             QByteArray("a?b"));
    QVERIFY(!ok);

    // cyrillic has no place in cp1252 either
    ok = true;
    QCOMPARE(TextCodec::encode(yozh, "windows-1252", &ok), QByteArray("??"));
    QVERIFY(!ok);

    // a surrogate pair is one character and costs exactly one '?'
    ok = true;
    QCOMPARE(TextCodec::encode(QString::fromUtf8("a\xF0\x9F\x98\x80" "b"), "cp866", &ok),
             QByteArray("a?b"));
    QVERIFY(!ok);

    // ok stays true when everything fits
    ok = false;
    QCOMPARE(TextCodec::encode(privet, "windows-1251", &ok), privet_cp1251);
    QVERIFY(ok);
}

void TestTextCodec::refusesUnknownEncoding()
{
    bool ok = true;
    QVERIFY(TextCodec::decode(QByteArray("abc"), "no-such-encoding", &ok).isEmpty());
    QVERIFY(!ok);

    ok = true;
    QVERIFY(TextCodec::encode(QStringLiteral("abc"), "no-such-encoding", &ok).isEmpty());
    QVERIFY(!ok);

    // an empty name is not an encoding either
    ok = true;
    QVERIFY(TextCodec::encode(QStringLiteral("abc"), QString(), &ok).isEmpty());
    QVERIFY(!ok);
}

void TestTextCodec::handlesEmptyInput()
{
    bool ok = false;
    QVERIFY(TextCodec::decode(QByteArray(), "windows-1251", &ok).isEmpty());
    QVERIFY(ok);

    ok = false;
    QVERIFY(TextCodec::encode(QString(), "windows-1251", &ok).isEmpty());
    QVERIFY(ok);

    ok = false;
    QVERIFY(TextCodec::encode(QString(), "UTF-8", &ok).isEmpty());
    QVERIFY(ok);
}

QTEST_APPLESS_MAIN(TestTextCodec)
#include "tst_textcodec.moc"
