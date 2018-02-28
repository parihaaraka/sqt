#include "pgparams.h"

PgParams &PgParams::add(std::string &&param)
{
    _temps.push_back(param);
    return addref(_temps.back().data(), static_cast<int>(_temps.back().size()));
}

PgParams &PgParams::add(const std::string &param)
{
    return add(std::string(param));
}

PgParams &PgParams::add(const char *param, int size)
{
    if (!param)
        return addref(nullptr, 0);

    if (size == -1)
        return add(std::string(param));

    return add(std::string(param, static_cast<size_t>(size)));
}

PgParams &PgParams::operator<<(std::string &&param)
{
    return add(param);
}

PgParams &PgParams::operator<<(const std::string &param)
{
    return add(param);
}

PgParams &PgParams::operator<<(const char *param)
{
    return add(param);
}

PgParams &PgParams::addref(std::string &&param)
{
    return add(param);
}

PgParams &PgParams::addref(const std::string &param)
{
    return addref(param.data(), param.size());
}

PgParams &PgParams::addref(const char *param, int size)
{
    _param_pointers.push_back(param);
    _param_lengths.push_back(size);
    return *this;
}

PgParams &PgParams::add(const QString &param)
{
    if (param.isNull())
        return add(nullptr);
    return add(param.toStdString());
}

PgParams &PgParams::add(const QVariant &param)
{
    if (!param.isValid() || param.isNull())
        return add(nullptr);
    return add(param.toString().toStdString());
}

PgParams &PgParams::clear()
{
    _param_pointers.clear();
    _param_lengths.clear();
    _temps.clear();
    return *this;
}
