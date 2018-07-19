#ifndef PGCONNECTION_H
#define PGCONNECTION_H

#include <QObject>
#include "dbconnection.h"
#include <memory>
#include <libpq-fe.h>
#include "pgparams.h"
#include "copycontext.h"

class QSocketNotifier;

class PgConnection : public DbConnection
{
    Q_OBJECT
public:
    PgConnection();
    virtual ~PgConnection() override;
    virtual DbConnection* clone() override;

    virtual bool open() override;
    virtual void close() noexcept override;
    virtual bool isOpened() const noexcept override;
    virtual void cancel() noexcept override;
    virtual QString context() const noexcept override;
    virtual QString database() const noexcept override;
    virtual QString dbmsInfo() const noexcept override;
    virtual QString dbmsName() const noexcept override;
    virtual QString dbmsVersion() const noexcept override;
    virtual QString transactionStatus() const noexcept override;
    virtual int dbmsComparableVersion() override;
    virtual bool isUnquotedType(int sqlType) const noexcept override;
    virtual bool isNumericType(int sqlType) const noexcept override;
    virtual QMetaType::Type sqlTypeToVariant(int sqlType) const noexcept override;
    virtual void executeAsync(const QString &query, const QVector<QVariant> *params = nullptr) noexcept override;
    virtual bool execute(const QString &query, const QVector<QVariant> *params = nullptr) override;

    virtual QString escapeIdentifier(const QString &identifier) override;
    virtual QPair<QString,int> typeInfo(int sqlType) override;
    virtual void clarifyTableStructure(DataTable &table) override;

private:
    enum class async_stage
    {
        none,
        connecting,
        sending_query,
        flush,
        flush_copy,
        wait_ready_read,
        copy_out,
        copy_in
    };
    QSocketNotifier *_readNotifier, *_writeNotifier;
    PGconn *_conn = nullptr;
    async_stage _async_stage = async_stage::none;
    DataTable* _temp_result; ///< temporary resultset for asynchronous processing
    QString _query_tmp; ///< query storage during asynchronous connection if needed
    PgParams _params_tmp;
    int _temp_result_rowcount;
    PgCopyContext _copy_context;
    std::vector<char> _copy_in_buf;
    /*!
    * \brief <oid, <name, element oid>>
    *
    * Although pg's Oid is unsigned int, it's small values let us use signed int to
    * support both ms sql and postgresql. Or may be we should not spare bits and switch
    * to int64_t? Then it's necessary to change sqlType in DbConnection interface
    * (and fix DataTable).
    */
    QHash<int, QPair<QString, int>> _data_types; ///< non-static, not version-specific storage because of db-level user types

    virtual void openAsync() noexcept;
    bool isIdle() const noexcept;
    static void noticeReceiver(void *arg, const PGresult *res);
    void fetchNotifications();
    void fetch() noexcept;
    void asyncConnectionProceed();
    void getCopyData();
    void putCopyData();
    void readyReadSocket();
    void readyWriteSocket();
    int appendRawDataToTable(DataTable &dst, PGresult *src) noexcept;
    std::string finalConnectionString() const noexcept;

private slots:
    void watchSocket(int mode);

signals:
    void closeConnectionWanted();
};

#endif // PGCONNECTION_H
