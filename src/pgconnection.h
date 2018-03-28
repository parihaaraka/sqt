#ifndef PGCONNECTION_H
#define PGCONNECTION_H

#include <QObject>
#include "dbconnection.h"
#include <memory>
#include <libpq-fe.h>
#include "pgparams.h"

class QSocketNotifier;
class PgConnection : public DbConnection
{
    Q_OBJECT
public:
    PgConnection();
    ~PgConnection();
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
    virtual bool execute(const QString &query, const QVector<QVariant> *params = nullptr, int limit = -1) override;

private:
    enum class async_stage { none, connecting, sending_query, flush, wait_ready_read };
    QSocketNotifier *_readNotifier, *_writeNotifier;
    PGconn *_conn = nullptr;
    async_stage _async_stage = async_stage::none;
    DataTable* _temp_result; ///< temporary resultset for asynchronous processing
    QString _query_tmp; ///< query storage during asynchronous connection if needed
    PgParams _params_tmp;
    int _temp_result_rowcount;

    virtual void openAsync() noexcept;
    bool isIdle() const noexcept;
    static void noticeReceiver(void *arg, const PGresult *res);
    void fetchNotifications();
    void fetch() noexcept;
    void asyncConnectionProceed();
    void readyReadSocket();
    void readyWriteSocket();
    void watchSocket(int mode);
    int appendRawDataToTable(DataTable &dst, PGresult *src) noexcept;
    std::string finalConnectionString() const noexcept;
};

#endif // PGCONNECTION_H
