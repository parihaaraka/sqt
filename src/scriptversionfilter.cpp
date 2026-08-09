#include "scriptversionfilter.h"
#include <QObject>
#include <QRegularExpression>
#include <vector>

namespace Scripting
{

QString versionSpecificPart(const QString &script, int version)
{
    const QRegularExpression re(R"(\/\*\s*(if|elif|else|endif)\s+version\s*(\d+)?\s*\*\/)");
    QRegularExpressionMatchIterator i = re.globalMatch(script);

    // no boundaries at all - the whole script suits every version
    if (!i.hasNext())
        return script;

    // Every open 'if' remembers two things. Whether its enclosing block was
    // being emitted, because a block nested into a skipped one stays skipped no
    // matter how well its own version matches. And whether one of its branches
    // has been taken already, because the rest of them must be skipped even
    // when their versions match too (the first suitable branch wins).
    struct Frame { bool enclosingEmits; bool branchTaken; };
    std::vector<Frame> stack;
    bool emits = true;

    QString res;
    res.reserve(script.size());
    int pos = 0;

    auto appendPart = [&res, &script](int from, int len)
    {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        res.append(script.midRef(from, len));
#else
        res.append(QStringView{script}.mid(from, len));
#endif
    };

    while (i.hasNext())
    {
        const QRegularExpressionMatch match = i.next();
        // the text in front of a directive belongs to the block it terminates
        if (emits)
            appendPart(pos, match.capturedStart() - pos);
        pos = match.capturedEnd();

        const QString cond = match.captured(1);
        if (cond == "if")
        {
            // a versionless 'if' has an empty capture, which toInt()s to 0 and
            // makes the block unconditional
            const bool take = emits && version >= match.captured(2).toInt();
            stack.push_back({emits, take});
            emits = take;
        }
        else if (cond == "elif" || cond == "else")
        {
            if (stack.empty())
                throw QObject::tr("invalid dbms version boundaries");

            Frame &frame = stack.back();
            const bool take = frame.enclosingEmits && !frame.branchTaken &&
                    (cond == "else" || version >= match.captured(2).toInt());
            frame.branchTaken = frame.branchTaken || take;
            emits = take;
        }
        else // endif
        {
            if (stack.empty())
                throw QObject::tr("invalid dbms version boundaries");

            emits = stack.back().enclosingEmits;
            stack.pop_back();
        }
    }

    if (!stack.empty())
        throw QObject::tr("invalid dbms version boundaries");

    // the stack is empty, hence the tail belongs to the top level and is emitted
    appendPart(pos, script.size() - pos);
    return res;
}

}
