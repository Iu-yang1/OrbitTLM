#include "business.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace orbit {
namespace {
quint32 key(int main,int sub){return (quint32(main)<<8)|quint32(sub);}
QString quoted(QString value){value.replace('"',"\"\"");return "\""+value+"\"";}
struct Raw {
    quint64 bits=0;
    qint64 signedValue=0;
    QString hex;
    bool hasNumber=false,wide=false,aligned=false;
};
bool readBits(const QByteArray &buffer,int octet,int highBit,int length,Raw &r){
    if(octet<0||highBit<0||highBit>7||length<=0)return false;
    const qint64 start=qint64(octet)*8+7-highBit;
    if(start<0||start+length>qint64(buffer.size())*8)return false;
    r.aligned=(start%8==0 && length%8==0);
    QByteArray span;
    if(r.aligned)span=buffer.mid(int(start/8),length/8);
    else {
        quint8 tmp=0;int used=0;
        for(qint64 k=0;k<length;k++){
            const qint64 bit=start+k;
            tmp=quint8((tmp<<1)|((quint8(buffer.at(int(bit/8)))>>(7-(bit%8)))&1));
            if(++used==8){span.append(char(tmp));tmp=0;used=0;}
        }
        if(used)span.append(char(tmp<<(8-used)));
    }
    r.hex=QString::fromLatin1(span.toHex().toUpper());
    r.wide=length>64;
    if(r.wide)return true;
    quint64 u=0;
    for(qint64 k=0;k<length;k++){
        const qint64 bit=start+k;
        u=(u<<1)|((quint8(buffer.at(int(bit/8)))>>(7-(bit%8)))&1);
    }
    r.bits=u;r.hasNumber=true;
    r.hex=QString::number(u,16).rightJustified((length+7)/8*2,'0').toUpper();
    r.signedValue=length==64?std::bit_cast<qint64>(u):
        ((u & (quint64(1)<<(length-1)))?
             std::bit_cast<qint64>(u | (~quint64(0)<<length)):qint64(u));
    return true;
}
QString fmt(double value){
    if(!std::isfinite(value))return {};
    return QString::number(value,'g',15);
}
// Small, closed arithmetic interpreter for source-table formulas. No eval,
// scripts, environment access or implicit fixes to unknown expressions.
class Arithmetic {
public:
    Arithmetic(QString expression,double raw):s_(std::move(expression)),raw_(raw){}
    bool calculate(double &value){
        pos_=0;ok_=true;value=sum();space();return ok_&&pos_==s_.size()&&std::isfinite(value);
    }
private:
    void space(){while(pos_<s_.size()&&s_[pos_].isSpace())++pos_;}
    bool take(QChar c){space();if(pos_<s_.size()&&s_[pos_]==c){++pos_;return true;}return false;}
    double sum(){
        double x=product();
        while(ok_){if(take('+'))x+=product();else if(take('-'))x-=product();else break;}
        return x;
    }
    double product(){
        double x=atom();
        while(ok_){if(take('*'))x*=atom();else if(take('/')){double y=atom();if(y==0)ok_=false;else x/=y;}else break;}
        return x;
    }
    double atom(){
        space();if(take('+'))return atom();if(take('-'))return -atom();
        if(take('(')){double x=sum();if(!take(')'))ok_=false;return x;}
        const auto tail=s_.mid(pos_);
        for(const QString &symbol:{"UX","SX","FX","HX","STR","X"}){
            if(tail.startsWith(symbol) &&
                (pos_+symbol.size()==s_.size() || !s_.at(pos_+symbol.size()).isLetterOrNumber())){
                pos_+=symbol.size();return raw_;
            }
        }
        static const QRegularExpression number(
            R"(^((?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?))");
        const auto match=number.match(tail);
        if(!match.hasMatch()){ok_=false;return 0;}
        bool parsed=false;
        double value=match.captured(1).toDouble(&parsed);
        if(!parsed)ok_=false;
        pos_+=match.capturedLength(1);return value;
    }
    QString s_;int pos_=0;double raw_=0;bool ok_=true;
};
struct Result {QString value,unit,status;};
Result convert(const QString &formula,const Raw &r,int length){
    const QString f=formula.trimmed();
    static const QRegularExpression outer(R"(^F(?:\.\d+)?=(.+)$)");
    const auto m=outer.match(f);
    if(!m.hasMatch())return {r.hex,"","raw_unsupported_formula"};
    QString expression=m.captured(1).trimmed();
    QString unit;
    static const QRegularExpression unitRx(R"(<([^<>]+)>$)");
    auto u=unitRx.match(expression);
    if(u.hasMatch()){unit=u.captured(1);expression.chop(u.capturedLength());expression=expression.trimmed();}
    static const QRegularExpression enumeration(R"(^\{(UX|X)\|(.+)\}$)");
    auto e=enumeration.match(expression);
    if(e.hasMatch()){
        if(!r.hasNumber)return {r.hex,unit,"raw_field_too_wide"};
        const QString items=e.captured(2);
        static const QRegularExpression entry(R"(([0-9A-Fa-f]+)H\(([^)]*)\))");
        auto it=entry.globalMatch(items);
        while(it.hasNext()){
            const auto match=it.next();bool ok=false;
            auto n=match.captured(1).toULongLong(&ok,16);
            if(ok&&n==r.bits)
                return {QString::number(r.bits)+" ("+match.captured(2)+")",unit,"table_enum"};
        }
        return {QString::number(r.bits),unit,"table_enum_unknown_value"};
    }
    if(expression=="X"||expression=="HX"){
        return {r.hex,unit,"table_raw_hex"};
    }
    if(expression=="STR"){
        if(!r.aligned)return {r.hex,unit,"raw_unaligned_string"};
        const QByteArray value=QByteArray::fromHex(r.hex.toLatin1());
        const QString string=QString::fromLatin1(value).trimmed();
        return {string,unit,"table_string"};
    }
    if(!r.hasNumber)return {r.hex,unit,"raw_field_too_wide"};
    double value=double(r.bits);
    if(expression.contains("SX"))value=double(r.signedValue);
    if(expression.contains("FX")){
        if(length==32)value=double(std::bit_cast<float>(quint32(r.bits)));
        else if(length==64)value=std::bit_cast<double>(r.bits);
        else return {r.hex,unit,"raw_invalid_float_width"};
        if(!std::isfinite(value))return {r.hex,unit,"raw_nonfinite_float"};
    }
    double result=0;
    if(!Arithmetic(expression,value).calculate(result))
        return {r.hex,unit,"raw_unsupported_formula"};
    return {fmt(result),unit,"table_formula"};
}
QJsonObject fieldJson(const EngineeringField &f){
    return {{"code",f.code},{"bits",f.bits},{"name_cn",f.nameCn},
        {"name_en",f.nameEn},{"formula",f.formula},{"raw_hex",f.rawHex},
        {"raw_number",f.rawNumber},{"value",f.value},
        {"unit",f.unit},{"status",f.status},{"data_type",f.dataType},
        {"definition_evidence",f.definitionEvidence}};
}
}
EngineeringField BusinessCatalog::decodeField(const QJsonObject &d,const QByteArray &payload){
    EngineeringField f;
    f.code=d.value("id").toString();f.bits=d.value("bits").toString();
    f.nameCn=d.value("name_cn").toString();f.nameEn=d.value("name_en").toString();
    f.formula=d.value("formula").toString();f.dataType=d.value("data_type").toString();
    f.definitionEvidence=d.value("formula_source").toString();
    const auto address=f.bits.split('.');Raw r;
    bool octetOk=false,bitOk=false,widthOk=false;
    const int octet=address.value(0).toInt(&octetOk),bit=address.value(1).toInt(&bitOk),width=address.value(2).toInt(&widthOk);
    if(address.size()!=3 || !octetOk || !bitOk || !widthOk || !readBits(payload,octet,bit,width,r)){
        f.status="truncated_field";return f;
    }
    const auto result=convert(f.formula,r,width);
    f.rawHex=r.hex;f.rawNumber=r.hasNumber?QString::number(r.bits):QString();
    f.value=result.value;f.unit=result.unit;f.status=result.status;
    if(f.dataType=="raw_bits"&&!f.status.startsWith("raw_"))f.status="raw_bits";
    if((f.dataType=="sample_checked_linear"||f.dataType=="adapted_unit_reference")&&f.status=="table_formula")
        f.status="reference_formula";
    return f;
}
bool BusinessCatalog::loadDirectory(const QString &directory,QString *error){
    QDir dir(directory);
    if(!dir.exists()){if(error)*error="Telemetry catalog not found: "+directory;return false;}
    const auto names=dir.entryList({"*.json"},QDir::Files,QDir::Name);
    QMap<quint32,Kind> loaded;
    for(const QString &name:names){
        QFile file(dir.filePath(name));
        if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return false;}
        QJsonParseError issue;
        const QJsonDocument doc=QJsonDocument::fromJson(file.readAll(),&issue);
        if(issue.error!=QJsonParseError::NoError || !doc.isObject()){
            if(error)*error=name+": "+issue.errorString();return false;
        }
        const auto obj=doc.object();
        Kind kind;kind.name=obj.value("packet").toString();
        kind.main=obj.value("main_type").toInt(-1);
        kind.sub=obj.value("sub_type").toInt(-1);
        if(kind.name.isEmpty()||kind.main<0||kind.main>255||kind.sub<0||kind.sub>255){
            if(error)*error="Invalid packet header in "+name;return false;
        }
        for(const auto &entry:obj.value("fields").toArray()){
            const auto x=entry.toObject();
            Definition d;
            d.code=x.value("id").toString();
            d.bits=x.value("bits").toString();
            d.nameCn=x.value("name_cn").toString();
            d.nameEn=x.value("name_en").toString();
            d.formula=x.value("formula").toString();
            const QStringList loc=d.bits.split('.');
            if(loc.size()!=3){if(error)*error="Malformed bit position "+d.bits;return false;}
            bool a=false,b=false,c=false;
            d.offset=loc[0].toInt(&a);d.msb=loc[1].toInt(&b);d.length=loc[2].toInt(&c);
            // Published mission tables contain occasional malformed ranges
            // (e.g. negative length). Preserve their identities as invalid,
            // not silently "fix" them and not reject all other telemetry fields.
            if(!a||!b||!c||d.offset<0||d.msb<0||d.msb>7||d.length<1||d.length>65536)
                d.length=-1;
            kind.fields.push_back(d);
        }
        const quint32 identity=key(kind.main,kind.sub);
        if(loaded.contains(identity)){if(error)*error="Duplicate packet type in "+name;return false;}
        loaded.insert(identity,kind);
    }
    if(loaded.isEmpty()){if(error)*error="No packet definitions in "+directory;return false;}
    kinds_=std::move(loaded);return true;
}
QVector<QPair<int,int>> BusinessCatalog::packetTypeIds()const{
    QVector<QPair<int,int>> ids;
    for(const auto &packet:kinds_)ids.push_back({packet.main,packet.sub});
    return ids;
}
int BusinessCatalog::fieldDefinitions()const{
    int total=0;for(const auto &k:kinds_)total+=k.fields.size();return total;
}
BusinessPacket BusinessCatalog::decode(const ApplicationPacket &packet)const{
    BusinessPacket out;out.transport=packet;
    // Two explicit forms:
    // a) native subsystem packet starts with type/subtype,
    // b) receiver's legacy KISS envelope has main type at [1], subtype [6],
    //    and telemetry bytes start at [7]. The latter is a weaker hypothesis.
    const QByteArray &bytes=packet.bytes;
    if(bytes.size()<2)return out;
    const auto at=[](const QByteArray &b,int i){return int(quint8(b.at(i)));};
    const Kind *kind=nullptr;
    QByteArray payload;
    const auto native=kinds_.constFind(key(at(bytes,0),at(bytes,1)));
    if(native!=kinds_.constEnd()){
        kind=&native.value();payload=bytes;
        out.identification="native_type_prefix";
    }else if(bytes.size()>8){
        const auto legacy=kinds_.constFind(key(at(bytes,1),at(bytes,6)));
        if(legacy!=kinds_.constEnd()){
            kind=&legacy.value();payload=bytes.mid(7);
            out.identification="legacy_kiss_envelope_candidate";
        }
    }
    if(!kind){
        // Candidate envelope identifiers are diagnostic only, NOT a packet match.
        if(bytes.size()>8){
            out.mainType=at(bytes,1);out.subType=at(bytes,6);
            out.identification="unmatched_legacy_envelope_candidate";
        }else{
            out.mainType=at(bytes,0);out.subType=at(bytes,1);
            out.identification="unmatched_native_prefix_candidate";
        }
        out.packetLabel=QString("Unknown candidate %1/%2")
           .arg(out.mainType,2,16,QChar('0')).arg(out.subType,2,16,QChar('0')).toUpper();
        return out;
    }
    out.packetName=kind->name;out.mainType=kind->main;out.subType=kind->sub;
    out.packetLabel=kind->name;
    for(const Definition &d:kind->fields){
        if(d.length<1){
            out.fields.push_back({d.code,d.bits,d.nameCn,d.nameEn,d.formula,
                                  "","","","","invalid_source_bit_position"});
            ++out.skippedFields;
            continue;
        }
        Raw raw;
        if(!readBits(payload,d.offset,d.msb,d.length,raw)){
            ++out.skippedFields;continue;
        }
        auto conv=convert(d.formula,raw,d.length);
        EngineeringField field{
            d.code,d.bits,d.nameCn,d.nameEn,d.formula,raw.hex,
            raw.hasNumber?QString::number(raw.bits):QString(),
            conv.value,conv.unit,conv.status
        };
        // Formula matching a table does not establish packet CRC integrity.
        out.fields.push_back(std::move(field));
    }
    return out;
}
QVector<BusinessPacket> BusinessCatalog::decodeAll(const QVector<ApplicationPacket> &packets,
                                                    BusinessStats *stats)const{
    QVector<BusinessPacket> decoded;decoded.reserve(packets.size());
    BusinessStats counts;
    for(const auto &packet:packets){
        auto result=decode(packet);
        if(result.packetName.isEmpty())++counts.unknown;
        else ++counts.recognized;
        counts.truncatedFields+=result.skippedFields;
        counts.decodedFields+=result.fields.size();
        for(const auto &f:result.fields)
            if(f.status.startsWith("raw_"))++counts.unsupportedFields;
        decoded.push_back(std::move(result));
    }
    orderForReplay(decoded);
    if(stats)*stats=counts;
    return decoded;
}
void BusinessCatalog::orderForReplay(QVector<BusinessPacket> &packets){
    // Source order anchors untimed BIN fragments between their neighboring LOG records.
    const bool ordered=std::all_of(packets.cbegin(),packets.cend(),[](const auto &p){return p.transport.replayOrder>=0;});
    std::stable_sort(packets.begin(),packets.end(),[ordered](const BusinessPacket &a,const BusinessPacket &b){
        if(ordered)return a.transport.replayOrder<b.transport.replayOrder;
        const auto &ta=a.transport.captureLocal,&tb=b.transport.captureLocal;
        if(ta.isValid()!=tb.isValid())return ta.isValid();
        if(!ta.isValid())return false;
        return ta<tb;
    });
}
QJsonObject BusinessCatalog::toJson(const BusinessPacket &packet){
    QJsonArray fields,sources,segments;
    for(const auto &b:packet.transport.segments)segments.append(QString::fromLatin1(b.toHex()));
    for(const auto &f:packet.fields)fields.append(fieldJson(f));
    for(const auto &s:packet.transport.sources)sources.append(s);
    return {
        {"packet",packet.packetName},{"packet_label",packet.packetLabel},
        {"main_type",packet.mainType},{"sub_type",packet.subType},
        {"ccsds_apid",packet.transport.apid},
        {"ccsds_packet_sequence",packet.transport.sequence},
        {"identification",packet.identification},
        {"replay_order",packet.transport.replayOrder},
        {"definition_evidence",packet.definitionEvidence},
        {"field_base_kiss_octet",packet.fieldBaseKissOctet},
        {"field_buffer",packet.transport.assembledBytes.isEmpty()?"raw_kiss_hex":"assembled_packet_hex"},
        {"assembly_status",packet.transport.assemblyStatus},
        {"raw_segments_hex",segments},
        {"assembled_packet_hex",QString::fromLatin1(packet.transport.assembledBytes.toHex())},
        {"skipped_fields",packet.skippedFields},{"fields",fields},
        {"vcid",packet.transport.vcid},{"fec_evidence",packet.transport.fecEvidence},
        {"integrity",packet.transport.integrity},{"sources",sources},
        {"first_master",packet.transport.firstMaster},
        {"last_master",packet.transport.lastMaster},
        {"capture_local",packet.transport.captureLocal.isValid()
             ?packet.transport.captureLocal.toString(Qt::ISODateWithMs):QString()},
        {"time_basis",packet.transport.timestampBasis},
        {"spacecraft_utc",QJsonValue::Null},
        {"raw_kiss_hex",QString::fromLatin1(packet.transport.bytes.toHex())}
    };
}
bool BusinessCatalog::exportJson(const QString &path,const QVector<BusinessPacket> &packets,
                                 QString *error){
    QJsonArray array;
    for(const auto &p:packets)array.append(toJson(p));
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}
bool BusinessCatalog::exportCsv(const QString &path,const QVector<BusinessPacket> &packets,
                                QString *error){
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){if(error)*error=file.errorString();return false;}
    QTextStream out(&file);
    out<<"packet,main_type,sub_type,capture_local,time_basis,spacecraft_utc,vcid,fec_evidence,"
          "integrity,field_code,name_cn,name_en,bit_position,raw_hex,raw_number,formula,value,unit,status,"
          "data_type,definition_evidence,identification,field_base_kiss_octet\n";
    for(const auto &p:packets){
        const auto &t=p.transport;
        const QString lead=quoted(p.packetName)+","+QString::number(p.mainType)+","+
            QString::number(p.subType)+","+
            quoted(t.captureLocal.isValid()?t.captureLocal.toString(Qt::ISODateWithMs):QString())+","+
            quoted(t.timestampBasis)+",,"+QString::number(t.vcid)+","+
            quoted(t.fecEvidence)+","+quoted(t.integrity)+",";
        if(p.fields.isEmpty()){
            const QStringList rawRow={"","","","",
                quoted(QString::fromLatin1(t.bytes.toHex().toUpper())),"","","","",
                quoted("unclassified_packet_raw_bytes"),"",quoted(p.definitionEvidence),
                quoted(p.identification),QString::number(p.fieldBaseKissOctet)};
            out<<lead<<rawRow.join(',')<<"\n";continue;
        }
        for(const auto &f:p.fields)
            out<<lead<<quoted(f.code)<<","<<quoted(f.nameCn)<<","<<quoted(f.nameEn)<<","<<
                quoted(f.bits)<<","<<quoted(f.rawHex)<<","<<quoted(f.rawNumber)<<","<<
                quoted(f.formula)<<","<<quoted(f.value)<<","<<quoted(f.unit)<<","<<
                quoted(f.status)<<","<<quoted(f.dataType)<<","<<quoted(f.definitionEvidence)<<","<<
                quoted(p.identification)<<","<<p.fieldBaseKissOctet<<"\n";
    }
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}
bool BusinessCatalog::exportQuaternionCsv(const QString &path,const QVector<BusinessPacket> &packets,
                                          QString *error){
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::Text)){if(error)*error=file.errorString();return false;}
    QTextStream out(&file);
    out<<"capture_local,time_basis,spacecraft_utc,packet,identification,q0,q1,q2,q3,norm,"
          "fec_evidence,integrity,status\n";
    for(const auto &p:packets){
        const bool by04=p.packetName=="BY04-OBC-SF";
        if(p.packetName!="SAT-S3"&&!by04)continue;
        QString values[4];bool found[4]{};
        for(const auto &f:p.fields)
            for(int i=0;i<4;++i)
                if(f.nameCn==QString(by04?"姿态四元数q%1":"解算四元数q%1").arg(i)&&f.status=="table_formula"){
                    values[i]=f.value;found[i]=true;
                }
        if(!std::all_of(std::begin(found),std::end(found),[](bool a){return a;}))continue;
        double norm=0;
        for(const auto &v:values){bool ok=false;double n=v.toDouble(&ok);if(!ok)continue;norm+=n*n;}
        const auto &t=p.transport;
        out<<quoted(t.captureLocal.toString(Qt::ISODateWithMs))<<","<<
             quoted(t.timestampBasis)<<",,"<<quoted(p.packetName)<<","<<
             quoted(p.identification)<<",";
        for(const auto &v:values)out<<v<<",";
        out<<fmt(std::sqrt(norm))<<","<<quoted(t.fecEvidence)<<","<<
             quoted(t.integrity)<<",table_formula_crc_unverified\n";
    }
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}
} // namespace orbit
