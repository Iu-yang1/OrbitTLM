#pragma once
#include "transport.h"
#include <QJsonObject>
#include <QMap>
#include <QVector>
namespace orbit {
struct EngineeringField {
    QString code, bits, nameCn, nameEn, formula;
    QString rawHex, rawNumber, value, unit, status;
    QString dataType, definitionEvidence;
};
struct BusinessPacket {
    ApplicationPacket transport;
    QString packetName, packetLabel, identification;
    int mainType=-1, subType=-1;
    int skippedFields=0;
    QVector<EngineeringField> fields;
    QString definitionEvidence;
    int fieldBaseKissOctet=-1;
};
struct BusinessStats {
    int recognized=0, unknown=0, decodedFields=0, unsupportedFields=0, truncatedFields=0;
};
class BusinessCatalog {
public:
    bool loadDirectory(const QString &directory,QString *error=nullptr);
    int packetKinds() const {return kinds_.size();}
    int fieldDefinitions() const;
    QVector<QPair<int,int>> packetTypeIds() const;
    BusinessPacket decode(const ApplicationPacket &packet) const;
    QVector<BusinessPacket> decodeAll(const QVector<ApplicationPacket> &packets,
                                      BusinessStats *stats=nullptr) const;
    static QJsonObject toJson(const BusinessPacket &packet);
    static EngineeringField decodeField(const QJsonObject &definition,const QByteArray &payload);
    static void orderForReplay(QVector<BusinessPacket> &packets);
    static bool exportJson(const QString &file,const QVector<BusinessPacket> &packets,
                           QString *error=nullptr);
    static bool exportCsv(const QString &file,const QVector<BusinessPacket> &packets,
                          QString *error=nullptr);
    static bool exportQuaternionCsv(const QString &file,const QVector<BusinessPacket> &packets,
                                    QString *error=nullptr);
private:
    struct Definition {
        QString code,bits,nameCn,nameEn,formula;
        int offset=0,msb=7,length=0;
    };
    struct Kind {
        QString name;
        int main=-1,sub=-1;
        QVector<Definition> fields;
    };
    QMap<quint32,Kind> kinds_;
};
}
