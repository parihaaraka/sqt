#include <QtTest>
#include "sqlparser.h"

using SqlParser::AliasSearchStatus;
using SqlParser::explainAlias;

/// The parser answers a single question of the code completion: what is behind
/// the alias the cursor stands at. It is fed unfinished, and sometimes plainly
/// broken, text as the user types, which is what most of the cases are about.
class TestSqlParser : public QObject
{
    Q_OBJECT

    /// the position defaults to the place right after the alias and the dot
    static QPair<AliasSearchStatus, QStringList> at(const QString &alias, const QString &sql)
    {
        const int pos = sql.indexOf(alias + ".") + alias.length() + 1;
        return explainAlias(alias, sql, pos);
    }

private slots:
    void tableIsFoundAfterTheAlias()
    {
        const auto r = at("t", "select t. from mytable t where 1=1");
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"mytable"}));
    }

    /// the schema is kept, the caller needs it to ask the database for columns
    void schemaIsKept()
    {
        const auto r = at("t", "select t. from public.mytable t");
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"public", "mytable"}));
    }

    void tableWithoutAnAliasIsFoundByItsName()
    {
        const auto r = at("mytable", "select mytable. from mytable");
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"mytable"}));
    }

    void joinedTableIsFound()
    {
        const auto r = at("b", "select b. from a_tab a inner join b_tab b on a.id = b.id");
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"b_tab"}));
    }

    void updateAndDeleteAreUnderstood()
    {
        const auto u = explainAlias("t", "update mytable t set x = 1 where t.", 35);
        QCOMPARE(u.first, AliasSearchStatus::Name);
        QCOMPARE(u.second, QStringList({"mytable"}));

        const auto d = explainAlias("t", "delete from mytable t where t.", 30);
        QCOMPARE(d.first, AliasSearchStatus::Name);
        QCOMPARE(d.second, QStringList({"mytable"}));
    }

    /// the alias is written before the from clause, so the search goes forward
    void aliasIsFoundAheadOfTheCursor()
    {
        const auto r = explainAlias("t", "select t.x, t. from mytable t", 14);
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"mytable"}));
    }

    void unknownAliasIsNotFound()
    {
        const auto r = at("zz", "select zz. from mytable t");
        QCOMPARE(r.first, AliasSearchStatus::NotFound);
        QVERIFY(r.second.isEmpty());
    }

    /// A quoted identifier at the very end of the text used to send the scanner
    /// past the end of the string: the loop compared the position with the
    /// boundary for equality, while the quote handling could step over it.
    void quotedIdentifierAtTheEndDoesNotOverrun()
    {
        const auto r = explainAlias("T", "select \"T\". from mytable \"T\"", 11);
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"mytable"}));
    }

    void quotedIdentifierIsFoundBackwards()
    {
        const auto r = explainAlias("T", "select * from mytable \"T\" where \"T\".", 36);
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"mytable"}));
    }

    /// what the cursor stands at is a matter of the caller, not of the parser
    void positionOutsideTheTextIsSurvived()
    {
        QCOMPARE(explainAlias("t", "select t. from mytable t", 1000).first,
                 AliasSearchStatus::Name);
        QCOMPARE(explainAlias("t", "select t. from mytable t", -5).first,
                 AliasSearchStatus::Name);
        QCOMPARE(explainAlias("t", QString(), 0).first, AliasSearchStatus::NotFound);
        QCOMPARE(explainAlias("t", QString(), 10).first, AliasSearchStatus::NotFound);
    }

    void nothingIsFoundInGarbage()
    {
        QCOMPARE(explainAlias("t", "seleeect t. frooom", 11).first, AliasSearchStatus::NotFound);
        QCOMPARE(explainAlias("t", ";;;", 2).first, AliasSearchStatus::NotFound);
        QCOMPARE(explainAlias("t", "((((", 3).first, AliasSearchStatus::NotFound);
        QCOMPARE(explainAlias("t", "'unterminated string", 5).first, AliasSearchStatus::NotFound);
        QCOMPARE(explainAlias("t", "/* unterminated comment", 5).first, AliasSearchStatus::NotFound);
        QCOMPARE(explainAlias("t", "\"unterminated quote", 5).first, AliasSearchStatus::NotFound);
    }

    /// the statement the cursor is in ends at the semicolon
    void theNeighbouringStatementIsNotConsulted()
    {
        const auto r = explainAlias("t", "select * from other_table t;\nselect t. from mytable t", 38);
        QCOMPARE(r.first, AliasSearchStatus::Name);
        QCOMPARE(r.second, QStringList({"mytable"}));
    }
};

QTEST_APPLESS_MAIN(TestSqlParser)

#include "tst_sqlparser.moc"
