#ifndef PGPARAMS_H
#define PGPARAMS_H

#include <vector>
#include <string>
#include <QString>
#include <QVariant>

class PgParams
{
public:
    PgParams& add(std::string &&param);
    PgParams& add(const std::string &param);
    PgParams& add(const char *param, int size = -1);

    PgParams& operator<<(std::string &&param);
    PgParams& operator<<(const std::string &param);
    PgParams& operator<<(const char *param);

    PgParams& addref(std::string &&param);
    PgParams& addref(const std::string &param);
    PgParams& addref(const char *param, int size = -1);

    PgParams& add(const QString &param);
    PgParams& add(const QVariant &param);

    const char* const* values() const { return _param_pointers.data(); }
    const int* lengths() const { return _param_lengths.data(); }
    size_t count() const { return _param_lengths.size(); }
    PgParams& clear();

private:
    std::vector<const char*> _param_pointers;
    std::vector<int> _param_lengths;
    std::vector<std::string> _temps;
};


#endif // PGPARAMS_H
