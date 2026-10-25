#include "core.h"
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
namespace orbit {
namespace {
int byte(const QByteArray &data,int pos) { return static_cast<unsigned char>(data.at(pos)); }
QString sha(const QByteArray &data) {
    return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());
}
bool kiss(const QByteArray &encoded,QByteArray &out) {
    out.clear();
    for (int i=0;i<encoded.size();++i) {
        const int c=byte(encoded,i);
        if(c!=0xdb) { out.append(char(c));continue; }
        if(i+1==encoded.size()) return false;
        const int next=byte(encoded,++i);
        if(next==0xdc) out.append(char(0xc0));
        else if(next==0xdd)out.append(char(0xdb));
        else return false;
    }
    return true;
}
QString escaped(QString s) {s.replace('"',"\"\"");return "\""+s+"\"";}
QString hex(const QByteArray &x) {return QString::fromLatin1(x.toHex(' ').toUpper());}
void candidateQuaternion(Frame &f) {
    // Observed candidate, NOT identified as actual spacecraft attitude.
    if(f.satellite!="ASRTU-1" || f.apid!=0x0f0 || f.payload.size()<156) return;
    float q[4]{};double norm=0.0;
    for(int i=0;i<4;++i){
        const quint32 bits=qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(f.payload.constData()+140+4*i));
        q[i]=std::bit_cast<float>(bits);
        if(!std::isfinite(q[i]))return;
        norm+=double(q[i])*q[i];
    }
    if(std::abs(norm-1.0)>0.001)return;
    for(int i=0;i<4;i++)f.fields.push_back({
        QString("candidate.quaternion.q%1").arg(i),
        QString::number(double(q[i]),'g',10),"","candidate_unverified"});
    f.fields.push_back({"candidate.quaternion.norm",
        QString::number(std::sqrt(norm),'g',10),"","candidate_unverified"});
}
}
Frame Protocol::parse(const QByteArray &raw,const QString &satellite) {
    Frame f;
    f.bytes=raw;f.hash=sha(raw);f.satellite=satellite;
    if(raw.size()!=223) {
        f.error="RS information frame requires exactly 223 bytes (5-byte TM + 218 data)";
        return f;
    }
    const int first=byte(raw,0), second=byte(raw,1);
    if((first>>6)!=0) {
        f.error="Unsupported CCSDS short TM transfer-frame version";
        return f;
    }
    // The fifth octet is a first-header pointer field, NOT a packet length.
    // The on-air RS codeword is longer than these 223 decoded information octets.
    f.master=byte(raw,2);
    f.virtualCount=byte(raw,3);
    f.vcid=(second>>1)&0x07;
    f.complete=true; // TM structural validity only; application packets may cross frame boundaries.
    f.quality="structural_only";
    f.error="TM frame accepted; application CRC and RS parity not verified here";
    const int scid=((first&0x3f)<<4)|(second>>4);
    f.fields.push_back({"tm.spacecraft_id",QString::number(scid),"","verified_protocol"});
    f.fields.push_back({"tm.vcid",QString::number(f.vcid),"","verified_protocol"});
    f.fields.push_back({"tm.first_header_pointer",QString::number(byte(raw,4)),"","verified_protocol"});
    f.fields.push_back({"tm.master_count",QString::number(f.master),"","verified_protocol"});
    f.fields.push_back({"tm.virtual_count",QString::number(f.virtualCount),"","verified_protocol"});
    // Compatibility preview: a single KISS packet MAY contain a CCSDS Space Packet.
    // Neither the six-byte header nor a single complete packet is mandatory.
    const QByteArray zone=raw.mid(5,218);
    f.payload=zone;
    // BY04 VC1 is a binary DSLWP SSDV stream; arbitrary 0xC0 bytes
    // inside compressed image data must never be treated as KISS delimiters.
    if(satellite=="BY04" && f.vcid==1)return f;
    const int open=zone.indexOf(char(0xc0));
    if(open<0)return f;
    const int close=zone.indexOf(char(0xc0),open+1);
    if(close<0)return f;
    QByteArray packet;
    if(!kiss(zone.mid(open+1,close-open-1),packet))return f;
    if(packet.size()<7)return f;
    const int header=(byte(packet,0)<<8)|byte(packet,1);
    const int seq=(byte(packet,2)<<8)|byte(packet,3);
    const int length=1+(byte(packet,4)<<8)+byte(packet,5);
    if((header>>13)!=0 || length+6!=packet.size())return f;
    f.apid=header&0x7ff;
    f.sequence=seq&0x3fff;
    f.payload=packet.mid(6);
    f.fields.push_back({"candidate.ccsds.apid",QString::number(f.apid),"","header_candidate"});
    f.fields.push_back({"candidate.ccsds.sequence",QString::number(f.sequence),"","header_candidate"});
    candidateQuaternion(f); // experimental offset within *candidate* inner payload
    return f;
}
QVector<Frame> Protocol::fromLog(const QString &path,const QString &satellite,QString *error){
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly|QIODevice::Text)){
        if(error)*error=file.errorString();return {};
    }
    const QRegularExpression tag(
        R"(^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+) FEC frame #\d+ \[([^\]]+)\])");
    const QRegularExpression row(R"(^[0-9A-Fa-f]{4}:\s+((?:[0-9A-Fa-f]{2}\s*){1,16})$)");
    QVector<Frame> records;QByteArray buffer;QString when,route;bool active=false;
    auto flush=[&]{
        if(buffer.isEmpty())return;
        Frame f=parse(buffer,satellite);
        f.sources.push_back("log:"+route);
        f.captureLocal=QDateTime::fromString(when,"yyyy-MM-dd HH:mm:ss.zzz");
        f.timeBasis="logger_local_unverified";
        if(f.complete)f.quality="log_fec_reported";
        records.push_back(std::move(f));buffer.clear();
    };
    QTextStream in(&file);
    while(!in.atEnd()){
        const QString line=in.readLine().trimmed();
        const auto hdr=tag.match(line);
        if(hdr.hasMatch()){
            flush();active=true;when=hdr.captured(1);route=hdr.captured(2);continue;
        }
        if(!active)continue;
        const auto hexRow=row.match(line);
        if(hexRow.hasMatch()){
            QString s=hexRow.captured(1);
            s.remove(QRegularExpression(R"(\s+)"));
            buffer+=QByteArray::fromHex(s.toLatin1());
        }else if(line.size()>=19 && line.at(4)=='-' && line.at(7)=='-' &&
                 line.at(10)==' ' && line.at(13)==':' && !line.contains("FEC frame")){
            flush();active=false;
        }
    }
    flush();return records;
}
QVector<Frame> Protocol::fromBin(const QString &path,const QString &satellite,QString *error){
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){
        if(error)*error=file.errorString();return {};
    }
    QVector<Frame> records;int index=0;
    while(true){
        const QByteArray raw=file.read(223);
        if(raw.isEmpty())break;
        Frame f=parse(raw,satellite);
        f.sources.push_back("bin:block:"+QString::number(index++));
        records.push_back(std::move(f));
    }
    return records;
}
QVector<Frame> Protocol::fuse(const QVector<Frame> &logs,const QVector<Frame> &bins,Stats *stats){
    Stats s;s.logObservations=logs.size();s.binBlocks=bins.size();
    QHash<QString,int> seen;QVector<Frame> result;
    auto insert=[&](const Frame &f){
        const auto it=seen.constFind(f.hash);
        if(it!=seen.constEnd()){
            Frame &prior=result[*it];
            ++s.duplicates;
            for(const QString &src:f.sources)if(!prior.sources.contains(src))prior.sources<<src;
            if(!prior.captureLocal.isValid() && f.captureLocal.isValid()){
                prior.captureLocal=f.captureLocal;prior.timeBasis=f.timeBasis;
            }
            if(f.quality=="log_fec_reported" && prior.complete)prior.quality=f.quality;
        }else{
            seen.insert(f.hash,result.size());result.push_back(f);
        }
    };
    for(const Frame &f:bins)insert(f);
    for(const Frame &f:logs)insert(f);
    // BIN order keeps untimed continuation frames between their timed neighbors.
    if(!bins.isEmpty()){
        QSet<QString> binHashes,emitted;
        for(const auto &f:bins)binHashes.insert(f.hash);
        QHash<QString,QVector<QString>> before;
        QVector<QString> pending;
        for(const auto &f:logs){
            if(binHashes.contains(f.hash)){
                before[f.hash]+=pending;pending.clear();
            }else if(!pending.contains(f.hash))pending.push_back(f.hash);
        }
        QVector<Frame> ordered;ordered.reserve(result.size());
        auto emitHash=[&](const QString &hash){
            if(!emitted.contains(hash)){ordered.push_back(result[seen[hash]]);emitted.insert(hash);}
        };
        for(const auto &f:bins){
            for(const auto &hash:before.value(f.hash))emitHash(hash);
            emitHash(f.hash);
        }
        for(const auto &hash:pending)emitHash(hash);
        result=std::move(ordered);
    }
    s.unique=result.size();
    for(const Frame &f:result){
        if(!f.complete)++s.invalid;
        else if(f.quality=="log_fec_reported")++s.trustedFromLog;
        else ++s.structuralOnly;
    }
    if(stats)*stats=s;
    return result;
}
QJsonObject Protocol::toJson(const Frame &f){
    QJsonObject object{{"hash",f.hash},{"satellite",f.satellite},
        {"capture_local",f.captureLocal.isValid()?f.captureLocal.toString(Qt::ISODateWithMs):QString()},
        {"time_basis",f.timeBasis},{"spacecraft_utc",QJsonValue::Null},
        {"quality",f.quality},{"diagnostic",f.error},
        {"master_count",f.master},{"virtual_count",f.virtualCount},
        {"vcid",f.vcid},{"apid",f.apid},{"sequence",f.sequence},
        {"complete",f.complete},{"raw_hex",hex(f.bytes)},{"payload_hex",hex(f.payload)}};
    QJsonArray fields,sources;
    for(const auto &x:f.sources)sources.append(x);
    for(const auto &x:f.fields)fields.append(
        QJsonObject{{"name",x.name},{"value",x.value},{"unit",x.unit},{"status",x.status}});
    object.insert("sources",sources);object.insert("fields",fields);return object;
}
bool Protocol::exportCsv(const QString &path,const QVector<Frame> &frames,QString *error){
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly|QIODevice::Text)){
        if(error)*error=f.errorString();return false;
    }
    QTextStream out(&f);
    out<<"satellite,capture_local,time_basis,spacecraft_utc,master,vcid,apid,sequence,quality,field,value,unit,field_status,sha256\n";
    for(const Frame &frame:frames){
        const QString left=escaped(frame.satellite)+","+
            escaped(frame.captureLocal.isValid()?frame.captureLocal.toString(Qt::ISODateWithMs):QString())+","+
            escaped(frame.timeBasis)+",,"+QString::number(frame.master)+","+
            QString::number(frame.vcid)+","+QString::number(frame.apid)+","+
            QString::number(frame.sequence)+","+escaped(frame.quality)+",";
        if(frame.fields.isEmpty())out<<left<<",,,,"<<escaped(frame.hash)<<"\n";
        for(const auto &x:frame.fields)
            out<<left<<escaped(x.name)<<","<<escaped(x.value)<<","<<escaped(x.unit)<<","<<
                escaped(x.status)<<","<<escaped(frame.hash)<<"\n";
    }
    if(!f.commit()){if(error)*error=f.errorString();return false;}
    return true;
}
bool Protocol::exportJson(const QString &path,const QVector<Frame> &frames,QString *error){
    QJsonArray array;
    for(const auto &f:frames)array.append(toJson(f));
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}
bool Protocol::exportAttitudeCsv(const QString &path,const QVector<Frame> &frames,QString *error){
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly|QIODevice::Text)){
        if(error)*error=output.errorString();return false;
    }
    QTextStream csv(&output);
    csv<<"satellite,capture_local,time_basis,spacecraft_utc,q0,q1,q2,q3,norm,quality,semantic_status,frame_sha256\n";
    for(const auto &f:frames){
        // Only FEC-reported records: physical attitude semantics still unverified.
        if(f.quality!="log_fec_reported")continue;
        QString values[5];bool has[5]={false,false,false,false,false};
        const QString names[5]={"candidate.quaternion.q0","candidate.quaternion.q1",
             "candidate.quaternion.q2","candidate.quaternion.q3","candidate.quaternion.norm"};
        for(const auto &field:f.fields)
            for(int i=0;i<5;++i)if(field.name==names[i]&&field.status=="candidate_unverified"){
                values[i]=field.value;has[i]=true;
            }
        if(!std::all_of(std::begin(has),std::end(has),[](bool x){return x;}))continue;
        csv<<escaped(f.satellite)<<","<<
             escaped(f.captureLocal.toString(Qt::ISODateWithMs))<<","<<
             escaped(f.timeBasis)<<",";
        // Empty spacecraft_utc is intentional; do not mislabel recorder time as satellite UTC.
        csv<<",";
        for(const auto &v:values)csv<<v<<",";
        csv<<escaped(f.quality)<<",candidate_unverified,"<<escaped(f.hash)<<"\n";
    }
    if(!output.commit()){if(error)*error=output.errorString();return false;}
    return true;
}
Database::Database():connection_("OrbitTLM_"+QUuid::createUuid().toString(QUuid::WithoutBraces)){}
Database::~Database(){db_={};QSqlDatabase::removeDatabase(connection_);}
bool Database::open(const QString &path,QString *error){
    db_=QSqlDatabase::addDatabase("QSQLITE",connection_);
    db_.setDatabaseName(path);
    if(!db_.open()){if(error)*error=db_.lastError().text();return false;}
    QSqlQuery query(db_);
    const QStringList statements={
        "PRAGMA foreign_keys = ON",
        "CREATE TABLE IF NOT EXISTS sessions(id INTEGER PRIMARY KEY, name TEXT NOT NULL, imported_utc TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS frames(id INTEGER PRIMARY KEY, session_id INTEGER NOT NULL REFERENCES sessions(id), hash TEXT NOT NULL, satellite TEXT, "
        "capture_local TEXT, time_basis TEXT, quality TEXT, diagnostic TEXT, master INTEGER, virtual_count INTEGER, vcid INTEGER, apid INTEGER, "
        "sequence INTEGER, sources TEXT, raw BLOB, payload BLOB, UNIQUE(session_id,hash))",
        "CREATE TABLE IF NOT EXISTS fields(id INTEGER PRIMARY KEY, frame_id INTEGER NOT NULL REFERENCES frames(id), name TEXT, value TEXT, unit TEXT, status TEXT)",
        "CREATE INDEX IF NOT EXISTS frame_time ON frames(session_id,capture_local)",
        "CREATE INDEX IF NOT EXISTS field_name ON fields(name)"
    };
    for(const QString &sql:statements)if(!query.exec(sql)){
        if(error)*error=query.lastError().text();return false;
    }
    return true;
}
bool Database::save(const QString &name,const QVector<Frame> &frames,QString *error){
    if(!db_.isOpen()){if(error)*error="Database not open";return false;}
    if(!db_.transaction()){if(error)*error=db_.lastError().text();return false;}
    QSqlQuery session(db_);
    session.prepare("INSERT INTO sessions(name,imported_utc) VALUES(?,?)");
    session.addBindValue(name);
    session.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if(!session.exec()){if(error)*error=session.lastError().text();db_.rollback();return false;}
    const auto sessionId=session.lastInsertId().toLongLong();
    QSqlQuery insert(db_),field(db_);
    insert.prepare("INSERT INTO frames(session_id,hash,satellite,capture_local,time_basis,quality,diagnostic,master,virtual_count,vcid,apid,sequence,sources,raw,payload) "
                   "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    field.prepare("INSERT INTO fields(frame_id,name,value,unit,status) VALUES(?,?,?,?,?)");
    for(const Frame &f:frames){
        insert.bindValue(0,sessionId);insert.bindValue(1,f.hash);insert.bindValue(2,f.satellite);
        insert.bindValue(3,f.captureLocal.isValid()?QVariant(f.captureLocal.toString(Qt::ISODateWithMs)):QVariant());
        insert.bindValue(4,f.timeBasis);insert.bindValue(5,f.quality);insert.bindValue(6,f.error);
        insert.bindValue(7,f.master);insert.bindValue(8,f.virtualCount);insert.bindValue(9,f.vcid);
        insert.bindValue(10,f.apid);insert.bindValue(11,f.sequence);
        insert.bindValue(12,f.sources.join(';'));insert.bindValue(13,f.bytes);insert.bindValue(14,f.payload);
        if(!insert.exec()){if(error)*error=insert.lastError().text();db_.rollback();return false;}
        const auto frameId=insert.lastInsertId().toLongLong();
        for(const auto &x:f.fields){
            field.bindValue(0,frameId);field.bindValue(1,x.name);field.bindValue(2,x.value);
            field.bindValue(3,x.unit);field.bindValue(4,x.status);
            if(!field.exec()){if(error)*error=field.lastError().text();db_.rollback();return false;}
        }
    }
    if(!db_.commit()){if(error)*error=db_.lastError().text();return false;}
    return true;
}
QVector<QPair<qint64,QString>> Database::sessions(QString *error) const {
    QVector<QPair<qint64,QString>> result;
    if(!db_.isOpen()){if(error)*error="Database not open";return result;}
    QSqlQuery q(db_);
    if(!q.exec("SELECT id,name FROM sessions ORDER BY id DESC")){
        if(error)*error=q.lastError().text();return result;
    }
    while(q.next())result.push_back({q.value(0).toLongLong(),q.value(1).toString()});
    return result;
}
QVector<Frame> Database::load(qint64 sessionId,QString *error) const {
    QVector<Frame> frames;
    QSqlQuery q(db_);
    q.prepare("SELECT id,hash,satellite,capture_local,time_basis,quality,diagnostic,"
              "master,virtual_count,vcid,apid,sequence,sources,raw,payload FROM frames "
              "WHERE session_id=? ORDER BY id");
    q.addBindValue(sessionId);
    if(!q.exec()){if(error)*error=q.lastError().text();return frames;}
    QSqlQuery sample(db_);
    sample.prepare("SELECT name,value,unit,status FROM fields WHERE frame_id=? ORDER BY id");
    while(q.next()){
        Frame f;
        f.hash=q.value(1).toString();f.satellite=q.value(2).toString();
        if(!q.value(3).isNull())f.captureLocal=QDateTime::fromString(q.value(3).toString(),Qt::ISODateWithMs);
        f.timeBasis=q.value(4).toString();f.quality=q.value(5).toString();
        f.error=q.value(6).toString();f.master=q.value(7).toInt();
        f.virtualCount=q.value(8).toInt();f.vcid=q.value(9).toInt();
        f.apid=q.value(10).toInt();f.sequence=q.value(11).toInt();
        f.sources=q.value(12).toString().split(';',Qt::SkipEmptyParts);
        f.bytes=q.value(13).toByteArray();f.payload=q.value(14).toByteArray();
        f.complete=f.quality!="invalid";
        sample.bindValue(0,q.value(0));
        if(!sample.exec()){if(error)*error=sample.lastError().text();return {};}
        while(sample.next())f.fields.push_back({
            sample.value(0).toString(),sample.value(1).toString(),
            sample.value(2).toString(),sample.value(3).toString()});
        frames.push_back(f);
    }
    return frames;
}
LiveTcp::LiveTcp(QObject *parent):QObject(parent){
    connect(&socket_,&QTcpSocket::readyRead,this,[this]{
        queued_+=socket_.readAll();
        while(queued_.size()>=223){
            const QByteArray bytes=queued_.left(223);queued_.remove(0,223);
            Frame frame=Protocol::parse(bytes,satellite_);
            frame.sources<<"live:tcp";frame.captureLocal=QDateTime::currentDateTime();
            frame.timeBasis="receiver_local";
            emit received(frame);
        }
    });
    connect(&socket_,&QTcpSocket::connected,this,[this]{emit status("Connected");});
    connect(&socket_,&QTcpSocket::disconnected,this,[this]{emit status("Disconnected");});
    connect(&socket_,&QTcpSocket::errorOccurred,this,[this](auto){emit status(socket_.errorString());});
}
void LiveTcp::start(const QString &host,quint16 port,const QString &satellite){
    socket_.abort();queued_.clear();satellite_=satellite;socket_.connectToHost(host,port);
}
void LiveTcp::stop(){socket_.disconnectFromHost();queued_.clear();}
}
