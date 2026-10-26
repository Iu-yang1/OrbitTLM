#include "by04.h"
#include <QSet>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
// Resource registration must be outside the orbit namespace for static linking.
static void registerBy04Catalog(){Q_INIT_RESOURCE(by04_catalog);}
namespace orbit {
namespace {
int u8(const QByteArray &data,int index){return int(quint8(data.at(index)));}
QString hex(unsigned value,int digits=2){
    return QStringLiteral("0x%1").arg(value,digits,16,QChar('0')).toUpper();
}
EngineeringField field(const QString &code,const QString &name,
                       int value,int bytes,const QString &status="protocol_header_only"){
    QString raw=QString("%1").arg(quint32(value),bytes*2,16,QChar('0')).toUpper();
    return {code,"",name,"","",raw,QString::number(value),
            QString::number(value),"",status};
}
const QVector<QJsonObject> &by04Catalog(){
    static const auto catalog=[]{
        registerBy04Catalog();
        QVector<QJsonObject> kinds;
        for(const auto &name:{"OBC-SF","BUV-S0"}){
            QFile file(QString(":/by04/%1.json").arg(name));
            if(file.open(QIODevice::ReadOnly)){
                const auto doc=QJsonDocument::fromJson(file.readAll());
                if(doc.isObject())kinds.push_back(doc.object());
            }
        }
        return kinds;
    }();
    return catalog;
}
bool decodeBusiness(const ApplicationPacket &packet,BusinessPacket &result){
    // ASRTU envelope structure, with an independent BY04 field catalog.
    if(packet.satellite!="BY04" || packet.bytes.size()<7)return false;
    const auto &b=packet.assembledBytes.isEmpty()?packet.bytes:packet.assembledBytes;
    if((u8(b,2)>>6)!=3)return false;
    const int identity=(u8(b,0)<<8)|u8(b,1);
    const int declaredOctets=((u8(b,4)<<8)|u8(b,5))+7;
    const int suffix=b.size()-declaredOctets;
    if(suffix!=0&&suffix!=2&&suffix!=4)return false;
    for(const auto &kind:by04Catalog()){
        if(identity!=kind.value("identity").toInt() ||
           packet.vcid!=kind.value("vcid").toInt() ||
           declaredOctets!=kind.value("kiss_octets").toInt() ||
           u8(b,6)!=kind.value("subtype").toInt())continue;
        result.packetName="BY04-"+kind.value("packet").toString();
        result.packetLabel=kind.value("packet").toString()=="OBC-SF"?
            QStringLiteral("计算机 · 关键遥测"):QStringLiteral("基带板 · UV 遥测");
        result.identification="by04_kiss_envelope";
        result.definitionEvidence=kind.value("identification_evidence").toString();
        result.fieldBaseKissOctet=7;
        result.mainType=u8(b,1);result.subType=u8(b,6);
        const QByteArray payload=b.mid(7,declaredOctets-7);
        for(const auto &entry:kind.value("fields").toArray()){
            const auto d=entry.toObject();
            auto f=BusinessCatalog::decodeField(d,payload);
            if(f.status=="truncated_field"){++result.skippedFields;continue;}
            result.fields.push_back(std::move(f));
        }
        return true;
    }
    return false;
}
quint32 crc32Dslwp(const QByteArray &frame) {
    // Applies ONLY to 218-byte VC1 DSLWP SSDV image packets.
    quint32 crc=0x4EE4FDE1u;
    for(int i=0;i<214;++i){
        quint32 part=(crc ^ quint8(frame.at(i)))&0xffu;
        for(int bit=0;bit<8;++bit)
            part=(part&1u)?(part>>1u)^0xEDB88320u:(part>>1u);
        crc=(crc>>8u)^part;
    }
    return crc ^ 0xffffffffu;
}
}
int By04Decoder::spacecraftId(const QByteArray &tm){
    if(tm.size()!=223)return -1;
    return ((quint8(tm[0])&0x3fu)<<4)|(quint8(tm[1])>>4);
}
BusinessPacket By04Decoder::decode(const ApplicationPacket &packet){
    BusinessPacket result;
    result.transport=packet;
    result.packetLabel=QStringLiteral("BY04 未分类应用数据包");
    if(packet.satellite!="BY04"){
        result.identification="by04_wrong_satellite";return result;
    }
    if(packet.assemblyStatus=="incomplete_segments"){
        result.identification="by04_incomplete_segments";
        result.packetLabel=QStringLiteral("分块数据 · 未收齐");return result;
    }
    if(decodeBusiness(packet,result))return result;
    if(packet.classification!="ccsds_space_packet_candidate" ||
       packet.apid<0 || packet.bytes.size()<6) {
        result.identification="by04_opaque_kiss";
        return result;
    }
    const QByteArray &b=packet.bytes;
    const int identity=(u8(b,0)<<8)|u8(b,1);
    const int sequence=(u8(b,2)<<8)|u8(b,3);
    const int declaredLength=((u8(b,4)<<8)|u8(b,5))+1;
    if(((identity>>13)&7)!=0 || declaredLength!=packet.data.size()) {
        result.identification="by04_opaque_kiss";
        return result;
    }
    result.packetName=QStringLiteral("BY04-CCSDS");
    result.packetLabel=QStringLiteral("BY04 · CCSDS 包头 %1").arg(hex(packet.apid,3));
    result.identification="by04_ccsds_header_only";
    // APID is not a BY04 mission subsystem main/sub type.
    result.mainType=-1;
    result.subType=-1;
    result.fields.push_back(field("by04.ccsds.apid",QStringLiteral("应用进程标识 APID"),
                                  packet.apid,2));
    result.fields.push_back(field("by04.ccsds.sequence_flags",
                                  QStringLiteral("包序列标志"),(sequence>>14)&3,1));
    result.fields.push_back(field("by04.ccsds.sequence_count",
                                  QStringLiteral("包序列计数"),sequence&0x3fff,2));
    result.fields.push_back(field("by04.ccsds.data_octets",
                                  QStringLiteral("CCSDS 用户数据长度"),declaredLength,2));
    result.fields.push_back(field("by04.ccsds.extra_octets",
                                  QStringLiteral("外部未定义尾部字节数"),
                                  std::max(0,packet.opaqueSuffixBytes),1));
    if(!packet.data.isEmpty()){
        const QString preview=QString::fromLatin1(packet.data.left(32).toHex(' ').toUpper());
        result.fields.push_back({"by04.payload.preview","","业务数据前 32 字节（未定义字段）","","",
                                 QString::fromLatin1(packet.data.left(32).toHex().toUpper()),
                                 "",preview,"","raw_unverified_business"});
        result.fields.push_back(field("by04.payload.first_octet",
             QStringLiteral("业务载荷首字节（含义未确认）"),u8(packet.data,0),1,
             "raw_unverified_business"));
    }
    return result;
}
QVector<BusinessPacket> By04Decoder::decodeAll(const QVector<ApplicationPacket> &packets){
    QVector<BusinessPacket> decoded;
    decoded.reserve(packets.size());
    // BY04's referenced envelope has one subtype octet in each secondary header.
    for(const auto &packet:Transport::assembleSegments(packets,1))decoded.push_back(decode(packet));
    BusinessCatalog::orderForReplay(decoded);
    return decoded;
}
QVector<ImagePacket> By04Decoder::images(const QVector<Frame> &frames){
    QVector<ImagePacket> packets;
    QSet<QByteArray> seen;
    for(const Frame &f:frames){
        // The upstream receiver identifies BY04 short TM VC1 using 0x2052.
        if(!f.complete || f.bytes.size()!=223 || f.vcid!=1 ||
           spacecraftId(f.bytes)!=SpacecraftId)continue;
        const QByteArray data=f.bytes.mid(5,218);
        if(seen.contains(data))continue;
        // DSLWP SSDV header (9 octets), defined separately from KISS.
        const int width=u8(data,3)*16, height=u8(data,4)*16;
        const int mcuMode=u8(data,5)&3;
        const int mcuOffset=u8(data,6);
        const int mcuId=(u8(data,7)<<8)|u8(data,8);
        int mcuCount=u8(data,3)*u8(data,4);
        mcuCount*=mcuMode==3?4:(mcuMode==1||mcuMode==2?2:1);
        if(width<=0||height<=0||width>8192||height>8192||
           mcuCount<=0||mcuCount>65535||
           (mcuId!=0xffff && (mcuId>=mcuCount||mcuOffset>=205)))continue;
        seen.insert(data);
        ImagePacket image;
        image.bytes=data;image.apid=-1;
        image.imageId=u8(data,0);
        image.packetId=(u8(data,1)<<8)|u8(data,2);
        image.width=width;image.height=height;
        const quint32 expected=(quint32(u8(data,214))<<24)|
                               (quint32(u8(data,215))<<16)|
                               (quint32(u8(data,216))<<8)|quint32(u8(data,217));
        image.status=crc32Dslwp(data)==expected?
                        "dslwp_crc32_pass":"dslwp_crc32_failed";
        packets.push_back(image);
    }
    std::stable_sort(packets.begin(),packets.end(),[](const ImagePacket &a,const ImagePacket &b){
        if(a.imageId!=b.imageId)return a.imageId<b.imageId;
        return a.packetId<b.packetId;
    });
    return packets;
}
}
