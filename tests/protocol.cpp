#include "core.h"
#include "schema.h"
#include "ssdv.h"
#include "transport.h"
#include "business.h"
#include "by04.h"
#include <QCoreApplication>
#include <QtTest/QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QSet>
#include <QtEndian>
#include <bit>
using namespace orbit;
namespace {
QByteArray syntheticFrame(bool escaping=false) {
    QByteArray packet;
    packet.append(char(0x08));packet.append(char(0xf0));
    packet.append(char(0xfb));packet.append(char(0xa1));
    packet.append(char(0x00));packet.append(char(0xbf));
    QByteArray payload(192,char(0));
    const quint32 one=qToBigEndian(std::bit_cast<quint32>(1.0f));
    memcpy(payload.data()+140,&one,4);
    if(escaping)payload[30]=char(0xc0);
    packet+=payload;
    QByteArray encoded;
    for(char c:packet){
        if(quint8(c)==0xc0){encoded.append(char(0xdb));encoded.append(char(0xdc));}
        else if(quint8(c)==0xdb){encoded.append(char(0xdb));encoded.append(char(0xdd));}
        else encoded.append(c);
    }
    QByteArray frame;
    frame.append(QByteArray::fromHex("205491bf00"));
    frame.append(char(0xc0));frame+=encoded;frame.append(char(0xc0));
    frame.append(QByteArray(223-frame.size(),char(0xaa)));
    return frame;
}
}
class ProtocolTest:public QObject {
    Q_OBJECT
private slots:
    void automaticSatelliteRecognitionUsesSpacecraftId() {
        Frame f=Protocol::parse(syntheticFrame(),"AUTO");
        QVERIFY(f.complete);
        QCOMPARE(By04Decoder::spacecraftId(f.bytes),0x205);
        QCOMPARE(f.satellite,QString("BY04"));
        QCOMPARE(f.vcid,2);
    }
    void timestampedTcpArchiveCanBeReimportedAsLog() {
        QTemporaryDir temp;QVERIFY(temp.isValid());
        const QByteArray frame=syntheticFrame();
        QString log=QStringLiteral("2026-10-28 12:34:56.789 TCP frame #1 [tcp]\n")+
                    QStringLiteral("pdu length = 223 bytes\npdu vector contents =\n");
        for(int offset=0;offset<frame.size();offset+=16){
            const QStringList octets=QString::fromLatin1(frame.mid(offset,16).toHex(' ').toUpper()).split(' ');
            log+=QStringLiteral("%1: %2\n").arg(offset,4,16,QChar('0')).arg(octets.join(' '));
        }
        QFile file(temp.filePath("tcp.log"));
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Text));
        file.write(log.toUtf8());file.close();
        QString error;
        const auto restored=Protocol::fromLog(file.fileName(),"AUTO",&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(restored.size(),1);
        QCOMPARE(restored[0].satellite,QString("BY04"));
        QCOMPARE(restored[0].captureLocal,QDateTime::fromString("2026-10-28 12:34:56.789","yyyy-MM-dd HH:mm:ss.zzz"));
        QCOMPARE(restored[0].quality,QString("structural_only"));
    }
    void parseKnownStructure() {
        Frame f=Protocol::parse(syntheticFrame(),"ASRTU-1");
        QVERIFY2(f.complete,qPrintable(f.error));
        QCOMPARE(f.vcid,2);QCOMPARE(f.master,0x91);
        QCOMPARE(f.apid,0xf0);QCOMPARE(f.sequence,0x3ba1);
        QCOMPARE(f.payload.size(),192);
        QVERIFY(f.fields.size() >= 10);
        bool hasCandidate=false;
        for(const auto &x:f.fields)
            if(x.name=="candidate.quaternion.q0" && x.status=="candidate_unverified")
                hasCandidate=true;
        QVERIFY(hasCandidate);
    }
    void kissEscapeBeforeLength() {
        Frame f=Protocol::parse(syntheticFrame(true),"ASRTU-1");
        QVERIFY2(f.complete,qPrintable(f.error));
        QCOMPARE(quint8(f.payload[30]),quint8(0xc0));
    }
    void corruptFrameIsNotPromoted() {
        QByteArray raw=syntheticFrame();raw[5]=char(0x42);
        Frame f=Protocol::parse(raw,"ASRTU-1");
        QVERIFY(f.complete); // the 5-byte TM frame is still valid
        QCOMPARE(f.apid,-1);  // but the KISS subpacket preview is unavailable
        QCOMPARE(f.quality,QString("structural_only"));
    }
    void duplicateFusionAndNoFakeUtc(){
        Frame log=Protocol::parse(syntheticFrame(),"ASRTU-1");
        log.quality="log_fec_reported";log.sources<<"log:primary";
        log.captureLocal=QDateTime::fromString("2026-10-24 23:06:02.776","yyyy-MM-dd HH:mm:ss.zzz");
        log.timeBasis="logger_local_unverified";
        Frame bin=Protocol::parse(syntheticFrame(),"ASRTU-1");bin.sources<<"bin:block:0";
        Stats s;
        auto frames=Protocol::fuse({log},{bin},&s);
        QCOMPARE(frames.size(),1);
        QCOMPARE(s.duplicates,1);
        QCOMPARE(s.trustedFromLog,1);
        QCOMPARE(frames[0].sources.size(),2);
        const auto obj=Protocol::toJson(frames[0]);
        QVERIFY(obj.value("spacecraft_utc").isNull());
    }
    void persistAndExport(){
        QTemporaryDir temp;QVERIFY(temp.isValid());
        Frame f=Protocol::parse(syntheticFrame(),"ASRTU-1");
        f.sources<<"bin:block:0";
        Database db;QString error;
        QVERIFY2(db.open(temp.filePath("db.sqlite"),&error),qPrintable(error));
        QVERIFY2(db.save("test",{f},&error),qPrintable(error));
        const auto sessions=db.sessions(&error);
        QCOMPARE(sessions.size(),1);
        const auto restored=db.load(sessions[0].first,&error);
        QCOMPARE(restored.size(),1);
        QCOMPARE(restored[0].hash,f.hash);
        QCOMPARE(restored[0].fields.size(),f.fields.size());
        QVERIFY2(Protocol::exportCsv(temp.filePath("samples.csv"),{f},&error),qPrintable(error));
        QVERIFY2(Protocol::exportJson(temp.filePath("samples.json"),{f},&error),qPrintable(error));
        f.quality="log_fec_reported";
        QVERIFY2(Protocol::exportAttitudeCsv(temp.filePath("attitude.csv"),{f},&error),qPrintable(error));
        QFile qcsv(temp.filePath("attitude.csv"));
        QVERIFY(qcsv.open(QIODevice::ReadOnly));
        const auto lines=qcsv.readAll();
        QVERIFY(lines.contains("candidate_unverified"));
        QVERIFY(lines.contains("q0,q1,q2,q3,norm"));
        QFile csv(temp.filePath("samples.csv"));QVERIFY(csv.open(QIODevice::ReadOnly));
        QVERIFY(csv.readAll().contains("candidate.quaternion.q0"));
        QFile json(temp.filePath("samples.json"));QVERIFY(json.open(QIODevice::ReadOnly));
        QVERIFY(json.readAll().contains("candidate_unverified"));
    }
    void by04BatteryTemperaturesUseDecimalHundredths(){
        auto decode=[](const QString &code,const QString &name,int raw){
            QJsonObject definition;
            definition.insert(QStringLiteral("id"),code);
            definition.insert(QStringLiteral("bits"),QStringLiteral("0.7.16"));
            definition.insert(QStringLiteral("name_cn"),name);
            definition.insert(QStringLiteral("formula"),QStringLiteral("F=HX"));
            definition.insert(QStringLiteral("data_type"),QStringLiteral("raw_bits"));
            QByteArray payload;
            payload.append(char((raw>>8)&0xff));
            payload.append(char(raw&0xff));
            return BusinessCatalog::decodeField(definition,payload);
        };
        const auto first=decode(QStringLiteral("OBC-CF0066.7.16"),QStringLiteral("电池温度1"),2690);
        QCOMPARE(first.rawNumber,QStringLiteral("2690"));
        QCOMPARE(first.value,QStringLiteral("26.9"));
        QCOMPARE(first.unit,QStringLiteral("℃"));
        QCOMPARE(first.status,QStringLiteral("table_formula"));
        const auto second=decode(QStringLiteral("OBC-CF0068.7.16"),QStringLiteral("电池温度2"),2671);
        QCOMPARE(second.value,QStringLiteral("26.71"));
        QCOMPARE(second.unit,QStringLiteral("℃"));
    }
    void externalSchemaIsSatelliteIsolated(){
        QTemporaryDir temp;QVERIFY(temp.isValid());
        QFile file(temp.filePath("fields.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"satellite":"BY04","revision":"v1","fields":[{"name":"raw.test","apid":"0xF0","offset":30,"type":"u8","status":"schema_declared_unverified"}]})");
        file.close();
        Schema schema;QString error;
        QVERIFY2(schema.load(file.fileName(),&error),qPrintable(error));
        Frame by04=Protocol::parse(syntheticFrame(),"BY04");
        QCOMPARE(schema.apply(by04),1);
        QCOMPARE(by04.fields.back().status,QString("schema_declared_unverified"));
        Frame asrtu=Protocol::parse(syntheticFrame(),"ASRTU-1");
        QCOMPARE(schema.apply(asrtu),0);
    }
    void ssdvOnlyWhenExplicitApidAndConsecutivePackets(){
        QByteArray bytes(256,char(0));
        bytes[0]=char(0x55);bytes[1]=char(0x67);
        bytes[6]=char(2);bytes[7]=char(0);bytes[8]=char(1);
        bytes[9]=char(20);bytes[10]=char(15);
        Frame a,b;
        a.apid=0x321;a.sequence=11;a.complete=true;a.quality="log_fec_reported";
        a.payload=bytes.left(128);
        b.apid=a.apid;b.sequence=12;b.complete=true;b.quality=a.quality;
        b.payload=bytes.mid(128);
        ApplicationPacket pa,pb;
        pa.apid=0x321;pa.sequence=11;pa.fecEvidence="log_fec_reported";
        pa.classification="ccsds_space_packet_candidate";
        pa.data=bytes.left(128);
        pb=pa;pb.sequence=12;pb.data=bytes.mid(128);
        // Header-looking telemetry is not an SSDV image unless its CRC passes
        // or it is corroborated by another valid packet in that same image.
        const auto found=ImageTools::candidatesFromKiss({pa,pb},0x321);
        QCOMPARE(found.size(),0);
        QCOMPARE(ImageTools::candidatesFromKiss({pa,pb},0x320).size(),0);
        b.sequence=14;
        pb.sequence=14;
        QCOMPARE(ImageTools::candidatesFromKiss({pa,pb},0x321).size(),0);
    }

    void oneCrcBadSsdvLikePacketIsNotPromoted(){
        QByteArray bytes(218,char(0x24));
        bytes[0]=char(58); bytes[1]=char(0); bytes[2]=char(1);
        bytes[3]=char(126); bytes[4]=char(16); bytes[5]=char(0);
        bytes[6]=char(0); bytes[7]=char(0); bytes[8]=char(1);
        QString error;
        const auto found=ImageTools::parseBinaryPackets(bytes,0x321,true,&error);
        QCOMPARE(found.size(),0);
        QVERIFY(!error.isEmpty());
    }

    void ssdvAllCrcBadPacketsAreRejectedSafely(){
        auto makeNormalPacket=[](int packetId){
            QByteArray bytes(256,char(0x11));
            bytes[0]=char(0x55); bytes[1]=char(0x67); // SSDV NO-FEC packet header
            bytes[6]=char(42);                       // image ID
            bytes[7]=char((packetId>>8)&0xff); bytes[8]=char(packetId&0xff);
            bytes[9]=char(20); bytes[10]=char(15);   // 320 x 240
            bytes[11]=char(0);                      // quality 4 / MCU mode 0
            bytes[12]=char(0); bytes[13]=char(0); bytes[14]=char(0);
            return bytes; // Deliberately invalid CRC; retain its structurally valid header.
        };
        QVector<ImagePacket> packets;
        for(const int packetId : {0,2}){
            ImagePacket packet;
            packet.bytes=makeNormalPacket(packetId);
            packet.imageId=42; packet.packetId=packetId;
            packet.width=320; packet.height=240;
            packet.status=QStringLiteral("candidate_unverified");
            packets.push_back(packet);
        }
        const DecodedImage decoded=ImageTools::decodeImage(packets,42,false);
        QVERIFY(decoded.jpeg.isEmpty());
        QVERIFY(!decoded.error.isEmpty());
        QVERIFY(decoded.badPacketIds.contains(0));
        QVERIFY(decoded.badPacketIds.contains(2));
    }
    void dslwpBadOnlyImageFailsWithoutInvokingJpegDecode(){
        QByteArray bytes(218,char(0x22));
        bytes[0]=char(9);                         // image ID
        bytes[1]=char(0); bytes[2]=char(2);       // only packet 2 was received
        bytes[3]=char(20); bytes[4]=char(15);     // 320 x 240
        bytes[5]=char(0);                         // quality 4 / MCU mode 0
        bytes[6]=char(0); bytes[7]=char(0); bytes[8]=char(2);
        ImagePacket packet;
        packet.bytes=bytes; packet.imageId=9; packet.packetId=2;
        packet.width=320; packet.height=240;
        packet.status=QStringLiteral("ssdv_crc32_failed");
        const DecodedImage decoded=ImageTools::decodeImage({packet},9,true);
        QVERIFY(decoded.jpeg.isEmpty());
        QVERIFY(!decoded.error.isEmpty());
        QVERIFY(decoded.badPacketIds.contains(2));
        QVERIFY(decoded.missingPacketIds.contains(0));
        QVERIFY(decoded.missingPacketIds.contains(1));
        QVERIFY(decoded.badPacketIds.contains(2));
    }
    void ssdvBinaryExportKeepsEveryFrameByte(){
        QTemporaryDir temp;QVERIFY(temp.isValid());
        QVector<ImagePacket> packets;
        ImagePacket first,second;
        first.imageId=7;first.packetId=0;first.bytes=QByteArray(256,char(0x11));
        second.imageId=7;second.packetId=1;second.bytes=QByteArray(256,char(0x22));
        packets<<first<<second;
        QString error;
        const QString path=temp.filePath(QStringLiteral("image_7.bin"));
        QVERIFY2(ImageTools::savePackets(packets,path,&error),qPrintable(error));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray exported=file.readAll();
        QCOMPARE(exported.size(),512);
        QCOMPARE(exported.left(256),first.bytes);
        QCOMPARE(exported.mid(256),second.bytes);
    }
    void physicalHeaderIsFiveBytes(){
        const QByteArray info=syntheticFrame();
        const auto data=Transport::frameData(info);
        QCOMPARE(data.size(),218);
        QCOMPARE(quint8(data[0]),quint8(0xc0));
        QCOMPARE(quint8(info[4]),quint8(0x00)); // first header pointer, not length
        Frame f=Protocol::parse(info,"ASRTU-1");
        QVERIFY(f.complete);
        QCOMPARE(f.quality,QString("structural_only"));
    }
    void variableLengthPacketsInOneTmFrame(){
        QByteArray raw(223,char(0xc0));
        raw.replace(0,5,QByteArray::fromHex("2054010100"));
        int offset=5;
        for(const QByteArray &p:{QByteArray::fromHex("011122"),
                                 QByteArray::fromHex("02AABBCCDD"),
                                 QByteArray::fromHex("03")}) {
            raw[offset++]=char(0xc0);
            for(char c:p)raw[offset++]=c;
            raw[offset++]=char(0xc0);
        }
        Frame f=Protocol::parse(raw,"ASRTU-1");
        QVERIFY(f.complete);
        const auto packets=Transport::reassemble({f});
        QCOMPARE(packets.size(),3);
        QCOMPARE(packets[0].bytes,QByteArray::fromHex("011122"));
        QCOMPARE(packets[1].bytes,QByteArray::fromHex("02AABBCCDD"));
        QCOMPARE(packets[2].bytes,QByteArray::fromHex("03"));
        QCOMPARE(packets[0].integrity,QString("not_crc_verified"));
    }
    void crossTmFragmentAndKissEscape(){
        QByteArray rawA(223,char(0x31));
        QByteArray rawB(223,char(0xc0));
        rawA.replace(0,5,QByteArray::fromHex("2054010100"));
        rawB.replace(0,5,QByteArray::fromHex("2054020200"));
        rawA[5]=char(0xc0);
        rawA[222]=char(0xdb); // FESC at end of the first 218-octet block
        rawB[5]=char(0xdc); // TFEND arrives in the next TM frame
        rawB[6]=char(0xdb);rawB[7]=char(0xdd); // escaped FESC
        rawB[8]=char(0x42);rawB[9]=char(0xc0);
        Frame a=Protocol::parse(rawA,"ASRTU-1");
        Frame b=Protocol::parse(rawB,"ASRTU-1");
        QVERIFY(a.complete && b.complete);
        const auto packets=Transport::reassemble({a,b});
        QCOMPARE(packets.size(),1);
        QCOMPARE(packets[0].bytes.size(),219);
        QCOMPARE(quint8(packets[0].bytes[216]),quint8(0xc0));
        QCOMPARE(quint8(packets[0].bytes[217]),quint8(0xdb));
        QCOMPARE(quint8(packets[0].bytes[218]),quint8(0x42));
        QCOMPARE(packets[0].contributingFrames,2);
    }
    void gapMustNotJoinUnrelatedFrames(){
        QByteArray rawA(223,char(0x31));
        QByteArray rawB(223,char(0xc0));
        rawA.replace(0,5,QByteArray::fromHex("2054010100"));
        rawB.replace(0,5,QByteArray::fromHex("2054020400")); // virtual count gap
        rawA[5]=char(0xc0);
        rawB[5]=char(0x42);rawB[6]=char(0xc0);
        QCOMPARE(Transport::reassemble({Protocol::parse(rawA,"ASRTU-1"),
                                       Protocol::parse(rawB,"ASRTU-1")}).size(),0);
    }
    void sequenceGapRejectsIncompleteWithoutCRC(){
        QByteArray a(223,char(0x41)),b(223,char(0xc0));
        a.replace(0,5,QByteArray::fromHex("2054010100"));
        b.replace(0,5,QByteArray::fromHex("2054020400"));
        a[5]=char(0xc0);b[5]=char(0xc0);
        const auto frames=QVector<Frame>{Protocol::parse(a,"ASRTU-1"),Protocol::parse(b,"ASRTU-1")};
        TransportStats stats;
        auto packets=Transport::reassemble(frames,&stats);
        QCOMPARE(packets.size(),0);
        QCOMPARE(stats.interruptedByCounterGap,1);
    }
    void malformedKissEscapeMustNotProducePacket(){
        QByteArray tm(223,char(0xc0));
        tm.replace(0,5,QByteArray::fromHex("2054010100"));
        tm[5]=char(0xc0);tm[6]=char(0x33);tm[7]=char(0xdb);
        tm[8]=char(0xc0); // unexpected FEND, not TFEND/TFESC
        auto f=Protocol::parse(tm,"ASRTU-1");
        TransportStats stats;
        const auto packets=Transport::reassemble({f},&stats);
        QCOMPARE(packets.size(),0);
        QCOMPARE(stats.malformedEscapePackets,1);
        QCOMPARE(stats.decodedPackets,0);
    }
    void optionalInnerHeaderWithUnknownChecksumSuffix(){
        QByteArray raw(223,char(0xc0));
        raw.replace(0,5,QByteArray::fromHex("2054010100"));
        QByteArray candidate=QByteArray::fromHex("08f0fba10000");
        candidate.append(char(0x42));
        candidate.append(QByteArray::fromHex("1234"));
        raw.replace(6,candidate.size(),candidate);
        const auto packets=Transport::reassemble({Protocol::parse(raw,"ASRTU-1")});
        QCOMPARE(packets.size(),1);
        QCOMPARE(packets[0].apid,0xf0);
        QCOMPARE(packets[0].opaqueSuffixBytes,2);
        QCOMPARE(packets[0].data,QByteArray(1,char(0x42)));
        QCOMPARE(packets[0].integrity,QString("not_crc_verified"));
    }
    void optionalInnerHeaderRetainsOneByteOpaqueSuffix(){
        QByteArray frame(223,char(0xaa));
        frame.replace(0,5,QByteArray::fromHex("2054010100"));
        const QByteArray packet=QByteArray::fromHex("08F0FBA1000042EE");
        frame[5]=char(0xc0);
        frame.replace(6,packet.size(),packet);
        frame[6+packet.size()]=char(0xc0);
        const auto packets=Transport::reassemble({Protocol::parse(frame,"BY04")});
        QCOMPARE(packets.size(),1);
        QCOMPARE(packets[0].classification,QString("ccsds_space_packet_candidate"));
        QCOMPARE(packets[0].apid,0xf0);
        QCOMPARE(packets[0].opaqueSuffixBytes,1);
        QCOMPARE(packets[0].data,QByteArray::fromHex("42"));
    }
    void all27AsrtuCatalogPacketsLoad(){
        BusinessCatalog catalog;QString error;
        const QString folder=QCoreApplication::applicationDirPath()+"/protocols/asrtu1";
        QVERIFY2(catalog.loadDirectory(folder,&error),qPrintable(error));
        QCOMPARE(catalog.packetKinds(),27);
        QVERIFY(catalog.fieldDefinitions()>1600);
    }
    void satelliteS3QuaternionFollowsPublishedSignedScale(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+"/protocols/asrtu1",&err),
                 qPrintable(err));
        QByteArray raw(200,char(0));
        raw[0]=char(0xF0);raw[1]=char(0x03);
        const int entries[4]={10000,-5000,2500,0};
        for(int i=0;i<4;++i){
            const quint32 bits=qToBigEndian(quint32(entries[i]));
            memcpy(raw.data()+12+4*i,&bits,4);
        }
        ApplicationPacket packet;
        packet.bytes=raw;packet.satellite="ASRTU-1";
        packet.fecEvidence="log_fec_reported";
        packet.timestampBasis="logger_local_unverified";
        const auto result=catalog.decode(packet);
        QCOMPARE(result.packetName,QString("SAT-S3"));
        QCOMPARE(result.identification,QString("native_type_prefix"));
        QVector<QString> qs(4);
        for(const auto &x:result.fields)
            for(int i=0;i<4;++i)if(x.nameCn==QString("解算四元数q%1").arg(i)) {
                QCOMPARE(x.status,QString("table_formula"));
                qs[i]=x.value;
            }
        QCOMPARE(qs,QVector<QString>({"1","-0.5","0.25","0"}));
        QTemporaryDir tmp;QVERIFY(tmp.isValid());
        const auto path=tmp.filePath("quaternions.csv");
        QVERIFY2(BusinessCatalog::exportQuaternionCsv(path,{result},&err),qPrintable(err));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray csv=file.readAll();
        QVERIFY(csv.contains("SAT-S3"));
        QVERIFY(csv.contains("not_crc_verified"));
        QVERIFY(csv.contains("1,-0.5,0.25,0"));
    }
    void legacyWrappedBusinessPacketIsRecognizedWithoutSpacePacket(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+"/protocols/asrtu1",&err),
                 qPrintable(err));
        QByteArray body(200,char(0));body[0]=char(0xF0);body[1]=char(0x03);
        QByteArray envelope=QByteArray::fromHex("08f0fba100bf03")+body;
        ApplicationPacket packet;packet.bytes=envelope;packet.satellite="ASRTU-1";
        const auto p=catalog.decode(packet);
        QCOMPARE(p.packetName,QString("SAT-S3"));
        QCOMPARE(p.identification,QString("legacy_kiss_envelope_candidate"));
        QVERIFY(p.fields.size()>20);
        QCOMPARE(p.transport.integrity,QString("not_crc_verified"));
    }
    void bitFieldAndLinearEngineeringFormula(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+"/protocols/asrtu1",&err),
                 qPrintable(err));
        QByteArray body(220,char(0));body[0]=char(0xAA);body[1]=char(0x00);
        // ZGK-S0 field 91.7.16: F=-0.003323*SX+56.08
        body[91]=char(0x27);body[92]=char(0x10); // 10000 signed
        ApplicationPacket frame;frame.bytes=body;frame.satellite="ASRTU-1";
        const auto result=catalog.decode(frame);
        QCOMPARE(result.packetName,QString("ZGK-S0"));
        bool found=false;
        for(const auto &field:result.fields)
            if(field.bits=="0091.7.16") {
                found=true;
                QCOMPARE(field.status,QString("table_formula"));
                QVERIFY(std::abs(field.value.toDouble()-22.85)<1e-8);
            }
        QVERIFY(found);
        // One-bit PDB switch is at byte 12, bit 7.
        QByteArray pdb(220,char(0));pdb[0]=char(0x04);pdb[1]=char(0);pdb[12]=char(0x80);
        frame.bytes=pdb;
        const auto power=catalog.decode(frame);
        QCOMPARE(power.packetName,QString("PDB-S0"));
        bool bitFound=false;
        for(const auto &field:power.fields)if(field.bits=="0012.7.1"){
            bitFound=true;QVERIFY(field.value.contains("开"));
        }
        QVERIFY(bitFound);
    }
    void outOfRangeFieldsNeverReadBeyondPayload(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+"/protocols/asrtu1",&err),
                 qPrintable(err));
        ApplicationPacket sample;sample.bytes=QByteArray::fromHex("f003");
        const auto decoded=catalog.decode(sample);
        QCOMPARE(decoded.packetName,QString("SAT-S3"));
        QVERIFY(decoded.skippedFields>0);
        QCOMPARE(decoded.transport.integrity,QString("not_crc_verified"));
    }
    void fieldTableCoverageAcrossAll27PacketKinds(){
        BusinessCatalog catalog;QString error;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+
                 "/protocols/asrtu1",&error),qPrintable(error));
        int decoded=0,unknownFormula=0,invalidSource=0;
        for(const auto &id:catalog.packetTypeIds()){
            QByteArray raw(8192,char(0));
            raw[0]=char(id.first);
            raw[1]=char(id.second);
            ApplicationPacket p;p.bytes=raw;p.satellite="ASRTU-1";
            auto out=catalog.decode(p);
            QVERIFY2(!out.packetName.isEmpty(),qPrintable(QString::number(id.first)));
            for(const auto &field:out.fields){
                if(field.status=="raw_unsupported_formula")++unknownFormula;
                else if(field.status=="invalid_source_bit_position")++invalidSource;
                else ++decoded;
            }
        }
        QVERIFY(decoded>1800);
        QVERIFY(unknownFormula<40);
        QVERIFY(invalidSource<20);
    }
    void ieeeFloatAndEnumBitConversion(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+
                 "/protocols/asrtu1",&err),qPrintable(err));
        QByteArray bytes(220,char(0));
        bytes[0]=char(0xAA);bytes[1]=char(0); // ZGK-S0
        const quint32 ieee=qToBigEndian(std::bit_cast<quint32>(1.25f));
        memcpy(bytes.data()+14,&ieee,4);
        ApplicationPacket p;p.bytes=bytes;
        auto rec=catalog.decode(p);
        bool haveFloat=false;
        for(const auto &f:rec.fields)
            if(f.bits=="0014.7.32" && f.formula=="F=FX"){
                haveFloat=true;
                QVERIFY(std::abs(f.value.toDouble()-1.25)<1e-6);
                QCOMPARE(f.status,QString("table_formula"));
            }
        QVERIFY(haveFloat);
    }
    void chronologicalBusinessExportKeepsUnknownTimeDistinct(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+
                 "/protocols/asrtu1",&err),qPrintable(err));
        ApplicationPacket a,b,c;
        a.bytes=QByteArray::fromHex("f003");
        b.bytes=a.bytes;c.bytes=a.bytes;
        a.captureLocal=QDateTime::fromString("2026-10-24T23:08:00",Qt::ISODate);
        b.captureLocal=QDateTime::fromString("2026-10-24T23:06:00",Qt::ISODate);
        const auto packets=catalog.decodeAll({a,c,b});
        QCOMPARE(packets.size(),3);
        QCOMPARE(packets[0].transport.captureLocal,b.captureLocal);
        QCOMPARE(packets[1].transport.captureLocal,a.captureLocal);
        QVERIFY(!packets[2].transport.captureLocal.isValid());
        const auto j=BusinessCatalog::toJson(packets[2]);
        QVERIFY(j.value("spacecraft_utc").isNull());
        QCOMPARE(j.value("integrity").toString(),QString("not_crc_verified"));
    }
    void unknownTelemetrySubtypeRemainsOpaque(){
        BusinessCatalog catalog;QString err;
        QVERIFY2(catalog.loadDirectory(QCoreApplication::applicationDirPath()+
                 "/protocols/asrtu1",&err),qPrintable(err));
        ApplicationPacket p;p.bytes=QByteArray::fromHex("08f0fba100bf0f127482d900");
        const auto b=catalog.decode(p);
        QVERIFY(b.packetName.isEmpty());
        QCOMPARE(b.mainType,0xf0);
        QCOMPARE(b.subType,0x0f);
        QCOMPARE(b.fields.size(),0);
        QCOMPARE(b.identification,QString("unmatched_legacy_envelope_candidate"));
    }
    void by04KnownSpacecraftIdAndVcDemultiplex(){
        QByteArray vc1(223,char(0));vc1[0]=char(0x20);vc1[1]=char(0x52);
        vc1[2]=char(1);vc1[3]=char(1);
        QByteArray vc2=vc1;vc2[1]=char(0x54);
        QCOMPARE(canonicalSatelliteName(QStringLiteral("BY70-4")),QStringLiteral("BY04"));
        QCOMPARE(canonicalSatelliteName(QStringLiteral("BY-04")),QStringLiteral("BY04"));
        QCOMPARE(canonicalSatelliteName(QStringLiteral("BY04uv")),QStringLiteral("BY04"));
        QCOMPARE(canonicalSatelliteName(QStringLiteral("ASRTU1")),QStringLiteral("ASRTU-1"));
        const Frame aliasFrame=Protocol::parse(vc1,QStringLiteral("BY70-4"));
        QCOMPARE(aliasFrame.satellite,QStringLiteral("BY04"));
        const Frame knownId=Protocol::parse(vc1,QStringLiteral("AUTO"));
        QCOMPARE(knownId.satellite,QStringLiteral("BY04"));
        Frame f1=Protocol::parse(vc1,"BY04");
        Frame f2=Protocol::parse(vc2,"BY04");
        QCOMPARE(By04Decoder::spacecraftId(vc1),0x205);
        QCOMPARE(By04Decoder::spacecraftId(vc2),0x205);
        QCOMPARE(f1.vcid,1);QCOMPARE(f2.vcid,2);
        const auto packets=Transport::reassemble({f1,f2});
        QCOMPARE(packets.size(),0); // VC1 SSDV bytes are NOT a KISS stream
    }
    void by04CcsdsSpacePacketGetsHeaderFieldsNotInventedEngineering(){
        QByteArray payload(192,char(0));
        payload[0]=char(0x0f);payload[1]=char(0x12);
        payload[2]=char(0x74);payload[3]=char(0x82);
        // APID 0x0F1 is not present in the pinned BY04 catalog. Keep this
        // fixture on the generic CCSDS-header-only path.
        const QByteArray source=QByteArray::fromHex("08F1FBA100BF")+payload;
        QByteArray frame=QByteArray::fromHex("2054010100");
        frame.append(char(0xc0));frame+=source;frame.append(char(0xc0));
        frame+=QByteArray(223-frame.size(),char(0xaa));
        const auto f=Protocol::parse(frame,"BY04");
        const auto packets=Transport::reassemble({f});
        QCOMPARE(packets.size(),1);
        QCOMPARE(packets[0].apid,0xf1);
        QCOMPARE(packets[0].sequence,0x3ba1);
        QCOMPARE(packets[0].data.size(),192);
        const auto output=By04Decoder::decodeAll(packets);
        QCOMPARE(output.size(),1);
        QCOMPARE(output[0].packetName,QString("BY04-CCSDS"));
        QCOMPARE(output[0].identification,QString("by04_ccsds_header_only"));
        QCOMPARE(output[0].fields.size(),7);
        QCOMPARE(output[0].fields[0].value,QString("241"));
        QCOMPARE(output[0].fields[3].value,QString("192"));
        QCOMPARE(output[0].fields[6].status,QString("raw_unverified_business"));
        QVERIFY(output[0].packetLabel.contains("BY04"));
    }
    void by04OBCSFMapDecodesMatchingCcsdsPacket(){
        QByteArray payload(192,char(0));
        payload[0]=char(0x0f);payload[1]=char(0x12);
        payload[2]=char(0x74);payload[3]=char(0x82);
        const QByteArray source=QByteArray::fromHex("08F0FBA100BF")+payload;
        QByteArray frame=QByteArray::fromHex("2054010100");
        frame.append(char(0xc0));frame+=source;frame.append(char(0xc0));
        frame+=QByteArray(223-frame.size(),char(0xaa));
        const auto f=Protocol::parse(frame,"BY04");
        const auto packets=Transport::reassemble({f});
        QCOMPARE(packets.size(),1);
        QCOMPARE(packets[0].apid,0xf0);
        QCOMPARE(packets[0].sequence,0x3ba1);
        const auto output=By04Decoder::decodeAll(packets);
        QCOMPARE(output.size(),1);
        QCOMPARE(output[0].packetName,QString("BY04-OBC-SF"));
        QCOMPARE(output[0].identification,QString("by04_kiss_envelope"));
        QCOMPARE(output[0].mainType,0xf0);
        QCOMPARE(output[0].subType,0x0f);
        QVERIFY(!output[0].fields.isEmpty());
        QCOMPARE(output[0].fields[0].code,QString("OBC-CF0000.7.32"));
        QCOMPARE(output[0].fields[0].nameCn,QString("UTC时间"));
        QCOMPARE(output[0].fields[0].value,QString("309625344"));
        QCOMPARE(output[0].fields[0].unit,QString("s"));
        QCOMPARE(output[0].fields[0].status,QString("table_formula"));
    }
    void by04SsdvReferenceFrameIsRecognizedFromRawTm(){
        const QByteArray frame=QByteArray::fromHex(
            "2052EC25FF3300111E1600160155A467E79B96F65FFEBD7255CF565D0F5F2FA3"
            "65ED1F53A7494515C27D505145140051451401AFA668A6FEC679CB1565E2203A"
            "123AE7F95749A17FC816DB3C1DA7F99A7E8F088748B651DD031FA9E6AD4108B7"
            "87CB5E80923F139AEA8412D4F031188751B8BEFA15758FF903DDFFD7235CCDF6"
            "8A6D34986EF73190E3CD53D067A57613C2B7103C4DF75B8350EA30ACDA6DC46D"
            "D0C669CE0A44E1EBCA934977D4F3EAE93C25F7AEFFE03FD6B9A1C8AE97C25F7A"
            "EBFE03FD6B0A7F11EAE37FDDD9D3D2D2515D47822D251450067EB74167F72B"
        );
        QCOMPARE(frame.size(),223);
        const Frame parsed=Protocol::parse(frame,QStringLiteral("BY04"));
        QVERIFY(parsed.complete);
        QCOMPARE(parsed.vcid,1);
        QCOMPARE(By04Decoder::spacecraftId(parsed.bytes),0x205);
        const auto packets=By04Decoder::images({parsed});
        QCOMPARE(packets.size(),1);
        QCOMPARE(packets[0].imageId,0x33);
        QCOMPARE(packets[0].packetId,0x0011);
        QCOMPARE(packets[0].width,480);
        QCOMPARE(packets[0].height,352);
        QCOMPARE(packets[0].bytes.size(),218);
        QCOMPARE(packets[0].status,QString("ssdv_crc32_pass"));
        const auto genericBy04=ImageTools::candidatesFromFrames({parsed});
        QCOMPARE(genericBy04.size(),1);
        QCOMPARE(genericBy04[0].imageId,0x33);
        QByteArray asrtuRaw=frame;
        asrtuRaw[0]=char(0x03);
        asrtuRaw[1]=char(0x22);
        const Frame asrtuFrame=Protocol::parse(asrtuRaw,QStringLiteral("ASRTU-1"));
        const auto genericAsrtu=ImageTools::candidatesFromFrames({asrtuFrame});
        QCOMPARE(genericAsrtu.size(),1);
        QCOMPARE(genericAsrtu[0].imageId,0x33);
        QString parseError;
        const auto imported=ImageTools::parseBinaryPackets(packets[0].bytes,240,true,&parseError);
        QVERIFY2(parseError.isEmpty(),qPrintable(parseError));
        QCOMPARE(imported.size(),1);
        QCOMPARE(imported[0].imageId,0x33);
        QCOMPARE(imported[0].packetId,0x0011);
        QCOMPARE(imported[0].status,QString("ssdv_crc32_pass"));
        // A single later packet has no verified image-start context. Refuse to
        // enter the upstream C JPEG decoder with this underdetermined stream.
        const auto partial=ImageTools::decodeImage(imported,0x33,true);
        QVERIFY(partial.jpeg.isEmpty());
        QVERIFY(partial.error.contains(QStringLiteral("仅有一个非首包")));
        QCOMPARE(partial.receivedPackets,1);
        QCOMPARE(partial.width,480);
        QCOMPARE(partial.height,352);
        QVERIFY(partial.missingPacketIds.contains(0));
    }
    void by04DslwpNativeVcidOneImageWithSourceVerifiedCrc(){
        QByteArray frame(223,char(0));
        frame[0]=char(0x20);frame[1]=char(0x52);
        frame[2]=char(17);frame[3]=char(7);
        QByteArray data(218,char(0));
        data[0]=char(0x2b);data[1]=char(0);data[2]=char(0x22);
        data[3]=char(20);data[4]=char(15); // 320 x 240
        data[7]=char(0xff);data[8]=char(0xff);
        // Independent known reference CRC vector (CRC32 with the DSLWP seed).
        data.replace(214,4,QByteArray::fromHex("790B3FDF"));
        frame.replace(5,218,data);
        const auto parsed=Protocol::parse(frame,"BY04");
        const auto recovered=By04Decoder::images({parsed});
        QCOMPARE(recovered.size(),1);
        QCOMPARE(recovered[0].imageId,43);
        QCOMPARE(recovered[0].packetId,34);
        QCOMPARE(recovered[0].width,320);
        QCOMPARE(recovered[0].height,240);
        QCOMPARE(recovered[0].bytes.size(),218);
        QCOMPARE(recovered[0].status,QString("ssdv_crc32_pass"));
        auto altered=frame;altered[5+40]=char(0x77);
        const auto damaged=By04Decoder::images({Protocol::parse(altered,"BY04")});
        QCOMPARE(damaged.size(),1);
        QCOMPARE(damaged[0].status,QString("ssdv_crc32_failed"));
    }
    void by04MixedVcAndWrongSpacecraftIdAreNotParsedAsKiss(){
        QByteArray payload(192,char(0));
        QByteArray encoded=QByteArray::fromHex("08D2FBA100BF")+payload;
        auto wrap=[&](QByteArray header) {
            header.append(char(0xc0));header+=encoded;header.append(char(0xc0));
            header+=QByteArray(223-header.size(),char(0xaa));
            return Protocol::parse(header,"BY04");
        };
        const auto legitimate=wrap(QByteArray::fromHex("2054010100"));
        const auto wrongScid=wrap(QByteArray::fromHex("0324010200"));
        const auto rawVc1=wrap(QByteArray::fromHex("2052010100"));
        const auto pkts=Transport::reassemble({legitimate,wrongScid,rawVc1});
        QCOMPARE(pkts.size(),1);
        QCOMPARE(pkts[0].apid,0x0d2);
    }
    void by04RealWorldDslwpFixtureRegression(){
        QFile fixture(QCoreApplication::applicationDirPath()+
                      "/fixtures/by04_ssdv_bad_packet.b64");
        QVERIFY2(fixture.open(QIODevice::ReadOnly),qPrintable(fixture.errorString()));
        const QByteArray raw=QByteArray::fromBase64(fixture.readAll());
        QCOMPARE(raw.size(),11*223);
        QVector<Frame> frames;
        for(int i=0;i<raw.size();i+=223){
            Frame f=Protocol::parse(raw.mid(i,223),"BY04");
            QVERIFY(f.complete);
            QCOMPARE(f.vcid,1);
            QCOMPARE(By04Decoder::spacecraftId(f.bytes),0x205);
            frames.push_back(f);
        }
        TransportStats stats;
        const auto falseKiss=Transport::reassemble(frames,&stats);
        QCOMPARE(falseKiss.size(),0);
        const auto images=By04Decoder::images(frames);
        QCOMPARE(images.size(),11);
        int crcPass=0,crcFail=0;
        for(const auto &packet:images){
            QCOMPARE(packet.bytes.size(),218);
            if(packet.status=="ssdv_crc32_pass")++crcPass;
            if(packet.status=="ssdv_crc32_failed")++crcFail;
            QCOMPARE(packet.width,640);
            QCOMPARE(packet.height,480);
        }
        QCOMPARE(crcPass,10);
        QCOMPARE(crcFail,1);
    }
    void jamxRealWorldFixtureRecognizesAndRebuildsImage(){
        QFile fixture(QCoreApplication::applicationDirPath()+
                      "/fixtures/jamx_satnogs_14907288.b64");
        QVERIFY2(fixture.open(QIODevice::ReadOnly),qPrintable(fixture.errorString()));
        const QByteArray raw=QByteArray::fromBase64(fixture.readAll());
        QCOMPARE(raw.size(),97*223);

        QVector<Frame> frames;
        for(int offset=0;offset<raw.size();offset+=223){
            const Frame frame=Protocol::parse(raw.mid(offset,223),"AUTO");
            QVERIFY(frame.complete);
            QCOMPARE(frame.vcid,1);
            frames.push_back(frame);
        }

        const auto packets=ImageTools::candidatesFromFrames(frames);
        QCOMPARE(packets.size(),56); // 97 received frames, 56 distinct SSDV packets.
        QSet<int> packetIds;
        for(const auto &packet:packets){
            QCOMPARE(packet.satellite,QString("JAMX"));
            QCOMPARE(packet.imageId,0);
            QCOMPARE(packet.width,640);
            QCOMPARE(packet.height,480);
            QCOMPARE(packet.bytes.size(),218);
            QCOMPARE(packet.status,QString("ssdv_crc32_pass"));
            packetIds.insert(packet.packetId);
        }
        QCOMPARE(packetIds.size(),56);
        QVERIFY(packetIds.contains(0));

        const DecodedImage decoded=ImageTools::decodeImage(packets,0,true);
        QVERIFY2(!decoded.jpeg.isEmpty(),qPrintable(decoded.error));
        QCOMPARE(decoded.width,640);
        QCOMPARE(decoded.height,480);
        QCOMPARE(decoded.receivedPackets,56);
        QCOMPARE(decoded.crcPassedPackets,56);
    }
    void jamxCorruptOnlyFrameIsNotPromotedToImage(){
        QFile fixture(QCoreApplication::applicationDirPath()+
                      "/fixtures/jamx_satnogs_14907288.b64");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray raw=QByteArray::fromBase64(fixture.readAll());
        QByteArray damaged=raw.left(223);
        damaged[5+80]=char(quint8(damaged[5+80])^0x01);
        const Frame frame=Protocol::parse(damaged,"AUTO");
        QVERIFY(frame.complete);
        QCOMPARE(frame.vcid,1);
        // A structurally plausible fragment whose CRC fails both recognized
        // variants must not be labelled as JAMX on its own.
        const auto candidates=ImageTools::candidatesFromFrames({frame});
        QVERIFY(candidates.isEmpty());
    }
    void jamxBinaryPacketImportRecognizesCrcVariant(){
        QFile fixture(QCoreApplication::applicationDirPath()+
                      "/fixtures/jamx_satnogs_14907288.b64");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QByteArray raw=QByteArray::fromBase64(fixture.readAll());
        QByteArray uniquePackets;
        QSet<QByteArray> seen;
        for(int offset=0;offset<raw.size();offset+=223){
            const QByteArray packet=raw.mid(offset+5,218);
            if(!seen.contains(packet)){
                seen.insert(packet);
                uniquePackets+=packet;
            }
        }
        QString error;
        const auto imported=ImageTools::parseBinaryPackets(uniquePackets,240,true,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(imported.size(),56);
        for(const auto &packet:imported){
            QCOMPARE(packet.satellite,QString("JAMX"));
            QCOMPARE(packet.status,QString("ssdv_crc32_pass"));
        }
    }
    void jamxUserCapture218PacketBinUsesObservedCrcProfile(){
        QFile fixture(QCoreApplication::applicationDirPath()+
                      "/fixtures/jamx_capture_20261031_id2_218.b64");
        QVERIFY2(fixture.open(QIODevice::ReadOnly),qPrintable(fixture.errorString()));
        const QByteArray raw=QByteArray::fromBase64(fixture.readAll());
        QCOMPARE(raw.size(),50*218);

        QString error;
        const auto packets=ImageTools::parseBinaryPackets(raw,240,true,&error);
        QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(packets.size(),50);

        int crcPassed=0,crcFailed=0;
        for(const auto &packet:packets){
            QCOMPARE(packet.satellite,QString("JAMX"));
            QCOMPARE(packet.imageId,2);
            QCOMPARE(packet.width,640);
            QCOMPARE(packet.height,480);
            if(packet.status==QStringLiteral("ssdv_crc32_pass"))++crcPassed;
            else if(packet.status==QStringLiteral("ssdv_crc32_failed"))++crcFailed;
        }
        // The capture has four FEC/CRC-damaged fragments; the other 46 share
        // a stable capture-observed CRC profile and must be admitted as JAMX.
        QCOMPARE(crcPassed,46);
        QCOMPARE(crcFailed,4);

        const DecodedImage decoded=ImageTools::decodeImage(packets,2,true);
        QVERIFY2(!decoded.jpeg.isEmpty(),qPrintable(decoded.error));
        QVERIFY(decoded.jpeg.startsWith(QByteArray::fromHex("FFD8")));
        QCOMPARE(decoded.imageId,2);
        QCOMPARE(decoded.width,640);
        QCOMPARE(decoded.height,480);
        QCOMPARE(decoded.receivedPackets,46);
        QCOMPARE(decoded.crcPassedPackets,46);
    }
    void jamxUserCapture223FrameStreamIsDetectedOnVc1(){
        QFile fixture(QCoreApplication::applicationDirPath()+
                      "/fixtures/jamx_capture_20261031_frames223.b64");
        QVERIFY2(fixture.open(QIODevice::ReadOnly),qPrintable(fixture.errorString()));
        const QByteArray raw=QByteArray::fromBase64(fixture.readAll());
        QCOMPARE(raw.size(),189*223);

        QVector<Frame> frames;
        int structurallyInvalidFrames=0;
        for(int offset=0;offset<raw.size();offset+=223){
            const Frame frame=Protocol::parse(raw.mid(offset,223),"AUTO");
            if(!frame.complete){
                // The receiver capture contains one FEC output whose first
                // octet is 0xDB (CCSDS version bits are 11, not 00). It is a
                // malformed TM frame and must be rejected, not promoted to
                // a VC1 SSDV candidate. Keep the other valid frames.
                ++structurallyInvalidFrames;
                continue;
            }
            frames.push_back(frame);
        }
        QCOMPARE(structurallyInvalidFrames,1);
        QCOMPARE(frames.size(),188);
        const auto packets=ImageTools::candidatesFromFrames(frames);
        // The raw stream has 57 unique VC1 payloads. Six fail strict SSDV
        // structural validation (for example, their MCU IDs are outside the
        // image's MCU count). Of the 51 structurally valid candidates, one
        // CRC-failed copy of packet ID 48 is suppressed in favor of its
        // CRC-passed copy. The final candidate set therefore contains 50.
        QCOMPARE(packets.size(),50);
        int crcPassed=0,crcFailed=0;
        for(const auto &packet:packets){
            QCOMPARE(packet.satellite,QString("JAMX"));
            QCOMPARE(packet.imageId,2);
            QCOMPARE(packet.bytes.size(),218);
            if(packet.status==QStringLiteral("ssdv_crc32_pass"))++crcPassed;
            else if(packet.status==QStringLiteral("ssdv_crc32_failed"))++crcFailed;
        }
        QCOMPARE(crcPassed,46);
        QCOMPARE(crcFailed,4);
    }
    void by04DoesNotMislabelAttitude(){
        Frame f=Protocol::parse(syntheticFrame(),"BY04");
        QVERIFY(f.complete);
        QVERIFY(f.fields.size()>=4);
        for(const auto &x:f.fields)QVERIFY(!x.name.startsWith("candidate.quaternion"));
    }
    void malformedDslwpPacketIsRejectedByEmbeddedDecoder(){
        ImagePacket packet;
        packet.imageId=7;packet.packetId=0;packet.width=640;packet.height=480;
        packet.bytes=QByteArray(217,char(0));
        packet.status="ssdv_crc32_failed";
        const auto decoded=ImageTools::decodeImage({packet},7,true);
        QVERIFY(decoded.jpeg.isEmpty());
        QVERIFY(!decoded.error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(ProtocolTest)
#include "protocol.moc"
