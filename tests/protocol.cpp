#include "core.h"
#include "schema.h"
#include "ssdv.h"
#include "transport.h"
#include "business.h"
#include "by04.h"
#include <QCoreApplication>
#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>
#include <bit>
#include <algorithm>
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
        const auto found=ImageTools::candidatesFromKiss({pa,pb},0x321);
        QCOMPARE(found.size(),1);
        QCOMPARE(found[0].packetId,1);
        QCOMPARE(found[0].width,320);
        QCOMPARE(ImageTools::candidatesFromKiss({pa,pb},0x320).size(),0);
        b.sequence=14;
        pb.sequence=14;
        QCOMPARE(ImageTools::candidatesFromKiss({pa,pb},0x321).size(),0);
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
        payload[0]=char(0x0e);payload[1]=char(0x12); // unsupported business subtype
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
        QCOMPARE(packets[0].data.size(),192);
        const auto output=By04Decoder::decodeAll(packets);
        QCOMPARE(output.size(),1);
        QCOMPARE(output[0].packetName,QString("BY04-CCSDS"));
        QCOMPARE(output[0].identification,QString("by04_ccsds_header_only"));
        QCOMPARE(output[0].fields.size(),7);
        QCOMPARE(output[0].fields[0].value,QString("240"));
        QCOMPARE(output[0].fields[3].value,QString("192"));
        QCOMPARE(output[0].fields[6].status,QString("raw_unverified_business"));
        QVERIFY(output[0].packetLabel.contains("BY04"));
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
        QCOMPARE(recovered[0].status,QString("dslwp_crc32_pass"));
        auto altered=frame;altered[5+40]=char(0x77);
        const auto damaged=By04Decoder::images({Protocol::parse(altered,"BY04")});
        QCOMPARE(damaged.size(),1);
        QCOMPARE(damaged[0].status,QString("dslwp_crc32_failed"));
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
            if(packet.status=="dslwp_crc32_pass")++crcPass;
            if(packet.status=="dslwp_crc32_failed")++crcFail;
            QCOMPARE(packet.width,640);
            QCOMPARE(packet.height,480);
        }
        QCOMPARE(crcPass,10);
        QCOMPARE(crcFail,1);
    }
    void by04RealBusinessCaptureAndUnknownFormulas(){
        QFile fixture(QCoreApplication::applicationDirPath()+"/fixtures/by04_business_capture.b64");
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto raw=QByteArray::fromBase64(fixture.readAll());
        QCOMPARE(raw.size(),6244);
        QVector<Frame> frames;
        for(int i=0;i<raw.size();i+=223)frames.push_back(Protocol::parse(raw.mid(i,223),"BY04"));
        const auto packets=Transport::reassemble(frames);
        const auto business=By04Decoder::decodeAll(packets);
        int obc=0,buv=0,converted=0;
        for(const auto &p:business){
            if(p.identification!="by04_kiss_envelope")continue;
            QCOMPARE(p.mainType,p.packetName=="BY04-OBC-SF"?0xf0:0xd2);
            QCOMPARE(p.fieldBaseKissOctet,7);
            QCOMPARE(p.transport.integrity,QString("not_crc_verified"));
            if(p.packetName=="BY04-OBC-SF"){++obc;QCOMPARE(p.fields.size(),220);}
            if(p.packetName=="BY04-BUV-S0"){++buv;QCOMPARE(p.fields.size(),99);}
            for(const auto &f:p.fields){
                if(f.dataType=="ieee754_binary32_be_observed")++converted;
                if(f.code=="OBC-CF0044.7.16"){
                    QCOMPARE(f.status,QString("reference_formula"));
                    QCOMPARE(f.unit,QString("mV"));
                    QCOMPARE(f.value,f.rawNumber);
                }
            }
        }
        QCOMPARE(obc,10);QCOMPARE(buv,11);QCOMPARE(converted,204);
        // Real capture q0 is at KISS octet 7+139, not ASRTU SAT-S3 offset 12.
        const auto it=std::find_if(business.cbegin(),business.cend(),[](const auto &p){
            return p.packetName=="BY04-OBC-SF";
        });
        QVERIFY(it!=business.cend());
        const auto q=std::find_if(it->fields.cbegin(),it->fields.cend(),[](const auto &f){
            return f.code=="OBC-CF0139.7.32";
        });
        QVERIFY(q!=it->fields.cend());
        QCOMPARE(q->bits,QString("139.7.32"));
        QVERIFY(qAbs(q->value.toDouble()-0.21687696874141693)<1e-14);
        QTemporaryDir temp;QString error;
        QVERIFY(BusinessCatalog::exportJson(temp.filePath("by04.json"),{*it},&error));
        QFile json(temp.filePath("by04.json"));QVERIFY(json.open(QIODevice::ReadOnly));
        const auto saved=json.readAll();
        QVERIFY(saved.contains("by04_kiss_envelope"));
        QVERIFY(saved.contains("ieee754_binary32_be_observed"));
        QVERIFY(saved.contains("field_base_kiss_octet"));
        QVERIFY(BusinessCatalog::exportCsv(temp.filePath("by04.csv"),{*it},&error));
        QFile csv(temp.filePath("by04.csv"));QVERIFY(csv.open(QIODevice::ReadOnly));
        const auto rows=csv.readAll();
        QVERIFY(rows.contains(QStringLiteral("姿态四元数q0").toUtf8()));
        QVERIFY(rows.contains("reference_formula"));
    }
    void by04ObservedLayoutRejectsWrongEnvelopeAndNonfinite(){
        ApplicationPacket p;p.satellite="BY04";p.vcid=2;
        p.bytes=QByteArray::fromHex("08f0c00100bf0f")+QByteArray(191,0);
        QCOMPARE(By04Decoder::decode(p).identification,QString("by04_kiss_envelope"));
        for(int suffix:{2,4}){
            auto trailer=p;trailer.bytes+=QByteArray(suffix,char(0x55));
            QCOMPARE(By04Decoder::decode(trailer).fields.size(),220);
            QCOMPARE(By04Decoder::decode(trailer).transport.integrity,QString("not_crc_verified"));
        }
        p.satellite="ASRTU-1";
        QVERIFY(By04Decoder::decode(p).fields.isEmpty());
        p.satellite="BY04";p.vcid=1;
        QVERIFY(By04Decoder::decode(p).fields.isEmpty());
        p.vcid=2;p.bytes[5]=char(0xbe);
        QVERIFY(By04Decoder::decode(p).fields.isEmpty());
        p.bytes[5]=char(0xbf);p.bytes[6]=char(14);
        QVERIFY(By04Decoder::decode(p).fields.isEmpty());
        p.bytes[6]=char(15);p.bytes.replace(146,4,QByteArray::fromHex("7fc00000"));
        const auto decoded=By04Decoder::decode(p);
        for(const auto &f:decoded.fields)if(f.code=="OBC-CF0139.7.32"){
            QCOMPARE(f.status,QString("raw_nonfinite_float"));
            QCOMPARE(f.value,QString("7FC00000"));
        }
    }
    void by04DoesNotMislabelAttitude(){
        Frame f=Protocol::parse(syntheticFrame(),"BY04");
        QVERIFY(f.complete);
        QVERIFY(f.fields.size()>=4);
        for(const auto &x:f.fields)QVERIFY(!x.name.startsWith("candidate.quaternion"));
    }
    void fragmentedBy04SurvivesPartialLogAndDatabaseReplay(){
        QByteArray packet=QByteArray::fromHex("08f0c12300bf0f")+QByteArray(191,char(0xc0));
        QByteArray stream(216,char(0xaa));stream.append(char(0xc0));
        for(char c:packet){
            if(quint8(c)==0xc0)stream+=QByteArray::fromHex("dbdc");
            else if(quint8(c)==0xdb)stream+=QByteArray::fromHex("dbdd");
            else stream.append(c);
        }
        stream.append(char(0xc0));
        QVector<Frame> bins;
        for(int offset=0;offset<stream.size();offset+=218){
            QByteArray raw=QByteArray::fromHex("2054000000");
            raw[2]=char((254+bins.size())&255);raw[3]=raw[2];
            raw+=stream.mid(offset,218);raw+=QByteArray(223-raw.size(),char(0xaa));
            auto f=Protocol::parse(raw,"BY04");f.sources<<"bin:block:"+QString::number(bins.size());
            bins.push_back(f);
        }
        QCOMPARE(bins.size(),3);
        auto first=bins[0],last=bins[2];
        first.captureLocal=QDateTime::fromString("2026-10-25T23:00:00",Qt::ISODate);
        last.captureLocal=first.captureLocal.addSecs(2);
        first.quality=last.quality="log_fec_reported";
        const auto merged=Protocol::fuse({first,last},bins);
        QCOMPARE(merged[1].bytes,bins[1].bytes);
        const auto packets=Transport::reassemble(merged);
        QCOMPARE(packets.size(),1);QCOMPARE(packets[0].bytes,packet);
        QCOMPARE(packets[0].contributingFrames,3);
        QCOMPARE(By04Decoder::decode(packets[0]).fields.size(),220);
        QTemporaryDir temp;Database db;QString error;
        QVERIFY(db.open(temp.filePath("replay.sqlite"),&error));
        QVERIFY(db.save("split",merged,&error));
        const auto restored=db.load(db.sessions()[0].first,&error);
        QCOMPARE(Transport::reassemble(restored)[0].bytes,packet);
        auto missing=merged;missing.removeAt(1);
        QVERIFY(Transport::reassemble(missing).isEmpty());
    }
    void spacecraftStreamsNeverShareContinuationState(){
        QByteArray a(223,char(0x31)),b(223,char(0x42));
        a.replace(0,5,QByteArray::fromHex("0324010100"));a[5]=char(0xc0);
        b.replace(0,5,QByteArray::fromHex("2054020200"));b[222]=char(0xc0);
        QVERIFY(Transport::reassemble({Protocol::parse(a,"ASRTU-1"),
                                       Protocol::parse(b,"ASRTU-1")}).isEmpty());
    }
    void by04SegmentedSpacePacketsKeepOriginalBlocks(){
        auto block=[](int flag,int seq,const QByteArray &body){
            ApplicationPacket p;p.satellite="BY04";p.vcid=2;p.apid=0xf0;
            p.classification="ccsds_space_packet_candidate";p.opaqueSuffixBytes=0;
            p.bytes=QByteArray::fromHex("08f000000000");p.data=QByteArray(1,char(15))+body;
            p.bytes[2]=char((flag<<6)|(seq>>8));p.bytes[3]=char(seq);
            p.bytes[4]=char((p.data.size()-1)>>8);p.bytes[5]=char(p.data.size()-1);
            p.bytes+=p.data;p.sequence=seq;return p;
        };
        const auto a=block(1,16382,QByteArray(60,char(1)));
        const auto b=block(0,16383,QByteArray(70,char(2)));
        const auto c=block(2,0,QByteArray(61,char(3)));
        const auto result=By04Decoder::decodeAll({a,b,b,c});
        QCOMPARE(result.size(),1);QCOMPARE(result[0].fields.size(),220);
        QCOMPARE(result[0].transport.bytes,a.bytes);
        QCOMPARE(result[0].transport.segments.size(),3);
        QCOMPARE(result[0].transport.assembledBytes.size(),198);
        QCOMPARE(result[0].transport.assemblyStatus,QString("reassembled_segments"));
        QCOMPARE(BusinessCatalog::toJson(result[0])["raw_segments_hex"].toArray().size(),3);
        auto crcA=a,crcB=b,crcC=c;
        for(auto *p:{&crcA,&crcB,&crcC}){p->bytes+=QByteArray::fromHex("1234");p->opaqueSuffixBytes=2;}
        const auto trailers=By04Decoder::decodeAll({crcA,crcB,crcC});
        QCOMPARE(trailers.size(),1);QCOMPARE(trailers[0].fields.size(),220);
        QCOMPARE(trailers[0].transport.segments[0],crcA.bytes);
        for(const auto &p:By04Decoder::decodeAll({a,c}))QVERIFY(p.fields.isEmpty());
        auto wrong=b;wrong.vcid=0;
        for(const auto &p:By04Decoder::decodeAll({a,wrong,c}))QVERIFY(p.fields.isEmpty());
    }
    void by04ReferenceFormulaSamples(){
        int checked=0;
        for(const auto &name:{"OBC-SF","BUV-S0"}){
            QFile file(QString(":/by04/%1.json").arg(name));QVERIFY(file.open(QIODevice::ReadOnly));
            const auto kind=QJsonDocument::fromJson(file.readAll()).object();
            for(const auto &v:kind["fields"].toArray()){
                const auto d=v.toObject();const auto bits=d["bits"].toString().split('.');
                const int offset=bits[0].toInt(),msb=bits[1].toInt(),width=bits[2].toInt();
                QByteArray payload(191,0);const qint64 signedRaw=d["evidence"].toObject()["x_text"].toString().toLongLong();
                const quint64 raw=quint64(signedRaw);
                for(int i=0;i<width;++i){const int bit=offset*8+7-msb+i;
                    if((raw>>(width-i-1))&1)payload[bit/8]=char(quint8(payload[bit/8])|(1u<<(7-bit%8)));
                }
                const auto f=BusinessCatalog::decodeField(d,payload);
                if(f.status=="table_formula"||f.status=="reference_formula"){
                    const double expected=d["evidence"].toObject()["f_double"].toDouble();
                    QVERIFY2(qAbs(f.value.toDouble()-expected)<1e-7*std::max(1.0,qAbs(expected)),qPrintable(f.code));
                    ++checked;
                }
            }
        }
        QVERIFY(checked>=100);
    }
};
QTEST_GUILESS_MAIN(ProtocolTest)
#include "protocol.moc"
