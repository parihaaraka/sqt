#ifndef SQLPARSER_H
#define SQLPARSER_H

#include <QString>
#include <QStringList>

namespace SqlParser
{

enum class AliasSearchStatus
{
    NotFound,  ///< aliased object not found
    NotParsed, ///< parsing of the object is not implemented (completion impossible)
    Name,      ///< caller should acquire columns from database
    Fields     ///< words ready to use within completer
};

QPair<AliasSearchStatus, QStringList> explainAlias(const QString &alias, const QString &text, int pos) noexcept;

}; // sqlparser namespace

#endif // SQLPARSER_H
