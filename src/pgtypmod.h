#ifndef PGTYPMOD_H
#define PGTYPMOD_H

#include <QString>
#include <cstdint>

/// A column's type modifier, decoded into the parts sqt needs.
struct PgTypmod
{
    /// What to append to the type name, parentheses included: "(16,0)", "(10)",
    /// " day to second(3)". Empty when the type carries no modifier.
    QString suffix;
    /// Declared length/precision, -1 when the type has none.
    int length = -1;
    /// Declared scale, -1 when the type has none (a numeric may have 0 or less).
    int16_t scale = -1;
};

/// Decodes the atttypmod/PQfmod value the way the server's own format_type()
/// does, so a column header reads exactly what psql's \gdesc would print.
///
/// Every type packs its modifier differently: numeric keeps precision and scale
/// in two halves of (typmod - VARHDRSZ), the datetime types store a bare
/// precision with no header offset at all, interval adds a bitmask of the fields
/// it spans, and the varlena types (varchar, bpchar) store length + VARHDRSZ.
/// An unknown type falls back to the varlena rule, which is the best guess
/// available without asking the server to run its typmodout function.
///
/// \param typeOid element type oid for an array - the modifier of an array
///        describes its elements
/// \param typmod  -1 means "no modifier given"
PgTypmod pgDecodeTypmod(int typeOid, int typmod);

#endif // PGTYPMOD_H
