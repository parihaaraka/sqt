#ifndef SCRIPTVERSIONFILTER_H
#define SCRIPTVERSIONFILTER_H

#include <QString>

namespace Scripting
{

///
/// \brief The function extracts version specific part of script or entire content.
/// \param script Content of script file.
/// \param version Current dbms comparable version (or db-level compartibility level).
/// \return Version specific part of script.
///
/// A script may contain comments corresponding to regexp:
/// \/\*\s*(if|elif|else|endif)\s+version\s*(\d+)?\s*\*\/
/// For example:
/// /* if version 100000 */
/// select s.datname, s.pid, s.backend_type, s.usename --, ...
/// from pg_stat_activity
///
/// /* elif version 90600 */
/// select s.datname, s.pid, s.usename --, ...
/// from pg_stat_activity s
/// /* endif version */
///
/// Such boundaries split a script into parts acording to dbms minimal version.
/// Nesting is ok, order matters. The first branch whose version is satisfied
/// wins, the rest of the branches are skipped even if they match as well.
/// PostgreSQL uses libpq's PQserverVersion(), ODBC data sources must provide
/// version.sql or version.qs to return this value if used within scripts.
/// E.g. scripts/odbc/microsoft sql/version.sql
/// (uses compartibility_level as a comparable version).
///
/// Throws QString if the boundaries are unbalanced.
///
QString versionSpecificPart(const QString &script, int version);

}

#endif // SCRIPTVERSIONFILTER_H
