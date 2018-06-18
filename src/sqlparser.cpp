#include "sqlparser.h"
#include <QDebug>
namespace SqlParser
{

struct AliasSearchResult
{
    AliasSearchResult() = default;
    AliasSearchResult(AliasSearchStatus s, int l, const QStringList &w): status(s), level(l), words(w) {}
    AliasSearchStatus status = AliasSearchStatus::NotFound;
    int level = 0;
    QStringList words;
};

struct Entity
{
    QChar separator;
    QStringList items;

    void clear()
    {
        separator = QChar::Null;
        items.clear();
    }

    bool ieq(const QString &value) const
    {
        return (items.count() == 1 && items.back().toLower() == value);
    }

    bool eq(const QString &value) const
    {
        return (items.count() == 1 && items.back() == value);
    }
};

AliasSearchResult test(const QString &alias, const QList<Entity> &entities, int level = 0) noexcept
{
    qDebug() << alias << ": ";
    int count = entities.count();
    for (int i = 0; i < count; ++i)
    {
        const Entity &e = entities[i];
        qDebug() << "\t(" << level << ", " << e.separator << ") " << e.items;

        if (e.separator == '.' && e.items.front() == alias)
            continue;
        if (e.items.back() == alias)
        {
            if (e.items.count() == 1) // alias or table name
            {
                if (i > 0)
                {
                    if (entities[i - 1].ieq("as") && i > count - 1)
                        return { AliasSearchStatus::Name, level, entities[i - 2].items };
                    return { AliasSearchStatus::Name, level, entities[i - 1].items };
                }
                else
                {
                    /* could be a problem:
                    select t.*
                    from schema.t tmp, schema.xyz t
                    */
                    return { AliasSearchStatus::Name, level, e.items };
                }
            }
            else // table name (may be followed by alias - check it)
            {
                if (i < count - 1)
                {
                     if (entities[i + 1].ieq("as") && i < count - 2 && entities[i + 2].eq(alias))
                         return { AliasSearchStatus::Name, level, e.items };
                }
                else
                    return { AliasSearchStatus::Name, level, e.items };
            }
        }
    }
    return {};
}

AliasSearchResult explainAlias(const QString &alias, const QString &text, int pos, bool backward) noexcept
{
    QList<Entity> entities;
    Entity entity;

    auto addEntity = [&]() -> bool {
        if (entity.items.empty())
            return !entities.isEmpty();
        if (backward)
            entities.prepend(entity);
        else
            entities.append(entity);
        return true;
    };

    QStringList terminators {"with", "copy", "alter", "create", "drop", "truncate", "disable", "enable",
                             "declare", "begin", "commit", "do", "while", "loop", "exec", "execute", "show"};
    QStringList dividers {"select", "update", "delete", "insert", "from",
                          "using", "where", "group", "order", "left", "join", "on",
                          "and", "not", "or"}; // just to narrow down

    uint8_t mode = 0xFF;
    int8_t comment_nest_level = 0;
    int8_t scope_level = 0;
    int8_t result_level = 0;
    QChar c, prevChar;
    QString word;
    int last_pos = backward ? -1 : text.length();
    int delta = 1;
    if (backward)
    {
        --pos;
        delta = -1;
    }

    auto applyWord = [&]() -> AliasSearchResult
    {
        if (!scope_level)
        {
            if (mode == 1 && dividers.contains(word.toLower()))
            {
                if (addEntity())
                {
                    auto res = test(alias, entities, result_level);
                    if (res.status != AliasSearchStatus::NotFound)
                        return res;
                    entities.clear();
                }
                entity.clear();
            }
            else
            {
                if (entity.separator.isNull())
                {
                    addEntity();
                    entity.clear();
                }
                if (backward)
                    entity.items.prepend(word);
                else
                    entity.items.append(word);
            }
        }
        word.clear();
        return {};
    };

    while (pos != last_pos)
    {
        c = text[pos];
        switch (mode)
        {
        case 2:
            if (c != '"')
            {
                word += c;
                break;
            }
            //[[fallthrough]];
        case 1:
            if (c.isLetterOrNumber() || c == '_')
            {
                word += c;
                break;
            }

            if (!word.isEmpty())
            {
                if (backward)
                    std::reverse(word.begin(), word.end());

                if (mode == 1 && terminators.contains(word.toLower()))
                    break;

                auto res = applyWord();
                if (res.status != AliasSearchStatus::NotFound)
                    return res;
            }

            if (mode == 2)
            {
                // skip quotation mark
                prevChar = c;
                pos += delta;
                if (pos == last_pos)
                    break;
                c = text[pos];
            }
            mode = 0xFF;
            //[[fallthrough]];
        case 0xFF:
            word.clear();
            if (c.isLetter() || c == '_')
            {
                if (!entity.separator.isNull() && prevChar != entity.separator)
                {
                    addEntity();
                    entity.clear();
                }
                mode = 1;
                word += c;
            }
            else if (c == '"')
            {
                if (!entity.separator.isNull() && prevChar != entity.separator)
                {
                    addEntity();
                    entity.clear();
                }
                mode = 2;
            }
            else if (c == '\'')
            {
                if (!entity.separator.isNull() && prevChar != entity.separator)
                {
                    addEntity();
                    entity.clear();
                }
                mode = 3;
            }
            else if (c == '*' && prevChar == '/')
            {
                mode = 4;
                comment_nest_level = 1;
            }
            else if (c == '-' && prevChar == '-')
                mode = 5;
            else if (!c.isSpace() && c != QChar::ParagraphSeparator && c != QChar::LineSeparator)
            {
                if (c != '.')
                {
                    if (addEntity())
                    {
                        auto res = test(alias, entities, result_level);
                        if (res.status != AliasSearchStatus::NotFound)
                            return res;
                        entities.clear();
                    }
                    entity.clear();

                    if (c == (backward ? ')' : '('))
                        ++scope_level;
                    else if (c == (backward ? '(' : ')'))
                    {
                        --scope_level;
                        if (scope_level < 0)
                        {
                            scope_level = 0;
                            --result_level;
                        }
                    }
                }
                else
                {
                    entity.separator = c;
                }
            }
            break;
        case 3:
            if (c == '\'')
                mode = 0xFF;
            break;
        case 4:
            /* multiline comments may be nested */
            if (c == '*' && prevChar == '/')
                ++comment_nest_level;
            else if (c == '/' && prevChar == '*')
                --comment_nest_level;

            if (!comment_nest_level)
                mode = 0xFF;
            break;
        case 5:
            if (c == '\n' || c == QChar::ParagraphSeparator || c == QChar::LineSeparator)
                mode = 0xFF;
        }

        if (c == ';')
            break;
        if (mode != 2 && mode != 3 && !c.isSpace() && c != QChar::ParagraphSeparator && c != QChar::LineSeparator)
            prevChar = c;
        pos += delta;
    }

    if (mode == 1 && !word.isEmpty())
    {
        if (backward)
            std::reverse(word.begin(), word.end());
        auto res = applyWord();
        if (res.status != AliasSearchStatus::NotFound)
            return res;
    }

    if (addEntity())
        return test(alias, entities, result_level);
    return {};
}

QPair<AliasSearchStatus, QStringList> explainAlias(const QString &alias, const QString &text, int pos) noexcept
{
    //qDebug() << "--- DOWN ---";
    auto resDown = explainAlias(alias, text, pos, false);
    //qDebug() << "--- UP ---";
    auto resUp = explainAlias(alias, text, pos, true);

    if (resDown.status == AliasSearchStatus::NotFound)
        return { resUp.status, resUp.words };
    if (resUp.status == AliasSearchStatus::NotFound)
        return { resDown.status, resDown.words };
    if (resDown.level > resUp.level)
        return { resDown.status, resDown.words };
    return { resUp.status, resUp.words };
}

}; // sqlparser namespace

/*
with tmp as
(
    select t.typname, t.oid
    from pg_type t
        left join pg_class tmp on t.typrelid = tmp.oid
    limit 1
)
select *
from tmp
    cross join (select 1 as tmp) t

*Мысли вслух*
Учитывать комментариии и строковые литералы.
Учитывать текущую область видимости относительно исходной позиции:
открывающаяся по направлению поиска скобка увеличивает уровень (вложенная область видимости),
закрывающая - уменьшает (выход наружу).
При обнаружении вложенной области видимости игнорируем всё до выхода на прежний уровень,
т.е. относительно исходной позиции уровень анализируемой области видимости может только уменьшаться.

Алиасы между select и from, между on и join(или where) и после where могут ссылаться на уровни выше.
Алиасы после from, между join и on видят только свой уровень.
* Пока пофиг.

Критерии обнаружения границы зоны поиска:
1) ';'
2) with, copy, alter, create, drop, truncate,
    disable, enable, declare, begin, commit,
    do, while, loop, exec, execute, show, ...(?);
3) Нужно фиксировать пройденные ключевые слова. // Пока пофиг.
При прохождении других ключевых слов на том же самом уровне (при выходе наверх вернуться невозможно!)
нужно проверять их последовательность: не может повториться update, where не может оказаться до select и т.п..
Как только нашлось ключевое слово, которое нарушает правильную последовательность,
то останавливаем поиск или ищем выше (бывает select union select).
*/
