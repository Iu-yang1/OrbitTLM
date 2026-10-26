#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QVector>
#include <QStringList>
#include <QTcpSocket>
namespace orbit {
struct Field {
    QString name;
    QString value;
    QString unit;
    QString status; // verified_protocol / candidate_unverified / raw
};
struct Frame {
    QByteArray bytes, payload;
    QString hash, satellite = "ASRTU-1", quality = "invalid", error;
    QStringList sources;
    QDateTime captureLocal; // LOG clock timezone unverified
    QString timeBasis = "unknown";
    int master = -1, virtualCount = -1, vcid = -1, apid = -1, sequence = -1;
    QVector<Field> fields;
    bool complete = false;
};
struct Stats {
    int logObservations = 0, binBlocks = 0, unique = 0, trustedFromLog = 0;
    int structuralOnly = 0, invalid = 0, duplicates = 0;
};
class Protocol {
public:
    static QString detectSatellite(const QByteArray &raw);
    static Frame parse(const QByteArray &raw, const QString &satellite);
    static QVector<Frame> fromLog(const QString &path,const QString &satellite,QString *error=nullptr);
    static QVector<Frame> fromBin(const QString &path,const QString &satellite,QString *error=nullptr);
    static QVector<Frame> fuse(const QVector<Frame> &logs,const QVector<Frame> &bins,Stats *stats=nullptr);
    static bool exportCsv(const QString &path,const QVector<Frame> &frames,QString *error=nullptr);
    static bool exportJson(const QString &path,const QVector<Frame> &frames,QString *error=nullptr);
    static bool exportAttitudeCsv(const QString &path,const QVector<Frame> &frames,QString *error=nullptr);
    static QJsonObject toJson(const Frame &frame);
};
class Database {
public:
    Database();
    ~Database();
    bool open(const QString &path, QString *error=nullptr);
    bool save(const QString &name,const QVector<Frame> &frames,QString *error=nullptr);
    QVector<QPair<qint64,QString>> sessions(QString *error=nullptr) const;
    QVector<Frame> load(qint64 sessionId,QString *error=nullptr) const;
private:
    QString connection_;
    QSqlDatabase db_;
};
class LiveTcp final : public QObject {
    Q_OBJECT
public:
    enum class ConnectionState {Disconnected,Connecting,Connected,Failed};
    Q_ENUM(ConnectionState)
    explicit LiveTcp(QObject *parent=nullptr);
    ConnectionState connectionState() const {return state_;}
    void start(const QString &host,quint16 port,const QString &satellite);
    void stop();
signals:
    void received(const orbit::Frame &frame);
    void status(const QString &message);
    void connectionStateChanged(orbit::LiveTcp::ConnectionState state,const QString &detail);
private:
    void setConnectionState(ConnectionState state,const QString &detail);
    QTcpSocket socket_;
    QByteArray queued_;
    QString satellite_ = "AUTO",endpoint_,connectionId_;
    ConnectionState state_=ConnectionState::Disconnected;
};
}
Q_DECLARE_METATYPE(orbit::Frame)
