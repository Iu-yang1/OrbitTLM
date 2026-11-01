#include "ssdv.h"
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <algorithm>
#include <cstdint>
#include <map>
#include <limits>
#include <new>
#include <utility>
#include <vector>
extern "C" {
#include <third_party/ssdv_dslwp/ssdv.h>
}
namespace orbit {
namespace {
// Publicly documented JAMX01 CRC profile (e.g. SatNOGS observation 14907288).
constexpr std::uint32_t kJamxCrcInitialValue = 0x6AAAC1C5U;
// A second, independently observed profile in the user's 2026-10-31 JAMX
// capture: 46 of 50 packets validate with this exact seed. This value is an
// empirical compatibility profile, not a claim that it is the universal JAMX
// seed. Keep the published profile above as the primary protocol definition.
constexpr std::uint32_t kJamxCaptureCrcInitialValue = 0xF87754B4U;
constexpr int kShortSsdvPayloadSize = 218;
constexpr int kShortSsdvCrcDataSize = 214;

std::uint32_t ssdvCrc32(const char *data, int length, std::uint32_t initialValue) {
    std::uint32_t crc = initialValue;
    for (int offset = 0; offset < length; ++offset) {
        std::uint32_t value =
            (crc ^ std::uint8_t(data[offset])) & 0xFFU;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U
                ? (value >> 1U) ^ 0xEDB88320U
                : value >> 1U;
        }
        crc = (crc >> 8U) ^ value;
    }
    return crc ^ 0xFFFFFFFFU;
}

// JAMX uses the 218-byte DSLWP compact SSDV packet layout but a different
// CRC32 initial value. The trailer is stored in network byte order.
bool hasJamxCrc(const QByteArray &packet) {
    if (packet.size() != kShortSsdvPayloadSize)
        return false;

    const auto *bytes =
        reinterpret_cast<const std::uint8_t *>(packet.constData());
    const std::uint32_t expected =
        (std::uint32_t(bytes[kShortSsdvCrcDataSize]) << 24U) |
        (std::uint32_t(bytes[kShortSsdvCrcDataSize + 1]) << 16U) |
        (std::uint32_t(bytes[kShortSsdvCrcDataSize + 2]) << 8U) |
        std::uint32_t(bytes[kShortSsdvCrcDataSize + 3]);
    const std::uint32_t published = ssdvCrc32(
        packet.constData(), kShortSsdvCrcDataSize, kJamxCrcInitialValue);
    if (published == expected)
        return true;
    const std::uint32_t observedCapture = ssdvCrc32(
        packet.constData(), kShortSsdvCrcDataSize,
        kJamxCaptureCrcInitialValue);
    return observedCapture == expected;
}

// Validate the packet layout before exposing a candidate to the SSDV state machine.
// CRC failure is allowed for diagnostics, but malformed headers are never accepted.
bool inspectPacket(const QByteArray &raw, bool shortFormat, ssdv_packet_info_t &info,
                   QByteArray &corrected, bool &crcPassed, bool allowHeaderOnly=false,
                   bool *jamxCrcPassed=nullptr) {
    const int expected=shortFormat?SSDV_PKT_SIZE_DSLWP:SSDV_PKT_SIZE;
    if (jamxCrcPassed)
        *jamxCrcPassed=false;
    if(raw.size()!=expected)return false;
    if(!shortFormat){
        if(quint8(raw[0])!=0x55)return false;
        const int type=quint8(raw[1]);
        if(type!=0x66&&type!=0x67)return false;
    }
    corrected=raw;
    int correctedSymbols=0;
    crcPassed=ssdv_dec_is_packet(reinterpret_cast<std::uint8_t*>(corrected.data()),
                                 &correctedSymbols,
                                 shortFormat?ssdv_dslwp_mode:ssdv_normal_mode)==SSDV_OK;
    bool jamxPassed=false;
    if (shortFormat && !crcPassed && hasJamxCrc(raw)) {
        // ssdv_dec_is_packet() tests a private copy, but keep JAMX's original
        // bytes as the decoder input because its CRC is valid under this mode.
        corrected=raw;
        crcPassed=true;
        jamxPassed=true;
    }
    if (jamxCrcPassed)
        *jamxCrcPassed=jamxPassed;
    const QByteArray &header=crcPassed?corrected:raw;
    if(shortFormat){
        if(header.size()!=SSDV_PKT_SIZE_DSLWP)return false;
        // DSLWP's compact 218-byte format begins at image_id and omits the
        // sync/type/callsign fields. The top two flag bits are reserved.
        if((quint8(header[5])&0xC0)!=0)return false;
        ssdv_dec_header(&info,reinterpret_cast<const std::uint8_t*>(header.constData()),ssdv_dslwp_mode);
    }else{
        if((quint8(header[11])&0xC0)!=0)return false;
        ssdv_dec_header(&info,reinterpret_cast<const std::uint8_t*>(header.constData()),ssdv_normal_mode);
    }
    if(info.width<=0||info.height<=0||info.width>4080||info.height>4080)return false;
    if(quint64(info.width)*quint64(info.height)>16646400ULL)return false;
    if(info.mcu_count==0||info.mcu_count>65535)return false;
    // A packet stream cannot contain more sequence numbers than its MCU count.
    if(info.packet_id>info.mcu_count)return false;
    const int payloadLength=shortFormat?205:(quint8(header[1])==0x67?237:205);
    // 0xffff is a decoder-internal, header-only sentinel. Never accept it from
    // incoming telemetry; it is allowed solely for a synthetic bootstrap packet.
    if(info.mcu_id==0xFFFF&&!allowHeaderOnly)return false;
    if(info.mcu_id!=0xFFFF &&
       (info.mcu_id>=info.mcu_count||info.mcu_offset>=payloadLength))return false;
    // A real sequence-zero packet starts at MCU zero / byte offset zero.
    if(info.packet_id==0 && info.mcu_id!=0 &&
       !(allowHeaderOnly&&info.mcu_id==0xFFFF))return false;
    if(info.packet_id==0 && info.mcu_offset!=0)return false;
    return true;
}

bool crcPass(const ImagePacket &packet) {
    return packet.status==QStringLiteral("ssdv_crc32_pass")||
           packet.status==QStringLiteral("ssdv_crc_pass");
}

QString candidateGroupKey(const ImagePacket &packet, bool includeSatellite=true) {
    // Same numeric image ID may be reused across captures. Keep CRC-failed
    // fragments tied to a complete header signature, not merely the image ID.
    const bool shortFormat=packet.bytes.size()==SSDV_PKT_SIZE_DSLWP;
    if(!shortFormat && packet.bytes.size()!=SSDV_PKT_SIZE)
        return QStringLiteral("invalid|%1").arg(packet.bytes.size());
    const int base=shortFormat?0:6;
    const int flags=quint8(packet.bytes[base+5])&~0x04; // EOI can differ on the final packet.
    const QString type=shortFormat?QStringLiteral("DSLWP"):QString::number(quint8(packet.bytes[1]));
    const QString callsign=shortFormat?QStringLiteral("DSLWP"):
        QString::fromLatin1(packet.bytes.mid(2,4).toHex());
    const QString satellite = includeSatellite
        ? canonicalSatelliteName(packet.satellite) : QString();
    return QStringLiteral("%1|%2|%3|%4x%5|%6|%7|%8")
        .arg(satellite).arg(packet.bytes.size())
        .arg(packet.imageId).arg(packet.width).arg(packet.height).arg(type).arg(flags).arg(callsign);
}

void propagateJamxIdentity(QVector<ImagePacket> &candidates) {
    // A damaged JAMX fragment cannot prove its own CRC variant. Once a valid
    // JAMX fragment establishes the image signature, associate matching
    // damaged peers with the same JAMX image so the normal corroboration rule
    // can retain them for diagnostics/recovery.
    QSet<QString> verifiedJamxGroups;
    for (const auto &packet : candidates) {
        if (crcPass(packet) && canonicalSatelliteName(packet.satellite) ==
                                   QStringLiteral("JAMX")) {
            verifiedJamxGroups.insert(candidateGroupKey(packet, false));
        }
    }
    if (verifiedJamxGroups.isEmpty())
        return;
    for (auto &packet : candidates) {
        const QString satellite=canonicalSatelliteName(packet.satellite);
        // A different CRC-valid variant is its own positive identification;
        // never relabel it merely because another spacecraft reused an image ID.
        if (crcPass(packet) || satellite == QStringLiteral("JAMX") ||
            satellite == QStringLiteral("BY04"))
            continue;
        if (verifiedJamxGroups.contains(candidateGroupKey(packet, false)))
            packet.satellite = QStringLiteral("JAMX");
    }
}

QVector<ImagePacket> keepCorroboratedCandidates(const QVector<ImagePacket> &candidates) {
    // One CRC-passed fragment can form a partial image on its own, but a single
    // CRC-failed header-looking packet is not enough evidence to call something
    // SSDV. Require two distinct CRC-passed sequence numbers before admitting
    // corrupt peers, and prefer a CRC-passed copy when a sequence is duplicated.
    QHash<QString,QSet<int>> verifiedSequences;
    for(const auto &packet:candidates)
        if(crcPass(packet))verifiedSequences[candidateGroupKey(packet)].insert(packet.packetId);
    QVector<ImagePacket> result;
    result.reserve(candidates.size());
    for(const auto &packet:candidates){
        const QString key=candidateGroupKey(packet);
        if(crcPass(packet)){result.push_back(packet);continue;}
        const auto verified=verifiedSequences.constFind(key);
        if(verified==verifiedSequences.cend()||verified.value().size()<2||
           verified.value().contains(packet.packetId))continue;
        result.push_back(packet);
    }
    return result;
}
}
QVector<ImagePacket> ImageTools::candidates(const QVector<Frame> &frames,int apid){
    return candidatesFromKiss(Transport::reassemble(frames),apid);
}
QVector<ImagePacket> ImageTools::candidatesFromFrames(const QVector<Frame> &frames){
    QVector<ImagePacket> provisional;
    QSet<QByteArray> seen;
    for(const Frame &frame:frames){
        // Only VC1's exact 223-byte TM information frames carry the compact
        // 218-byte SSDV format. This is shared by DSLWP/BY04/ASRTU-1 and JAMX;
        // JAMX is distinguished by its alternate CRC32 initial value.
        if(frame.bytes.size()!=223||frame.vcid!=1)continue;
        const QByteArray payload=frame.bytes.mid(5,218);
        if(seen.contains(payload))continue;
        QByteArray corrected;
        bool passed=false;
        bool jamxPassed=false;
        ssdv_packet_info_t info{};
        if(!inspectPacket(payload,true,info,corrected,passed,false,&jamxPassed))continue;
        seen.insert(payload);
        ImagePacket packet;
        packet.bytes=passed?corrected:payload;
        packet.apid=-1;packet.imageId=info.image_id;packet.packetId=info.packet_id;
        packet.width=info.width;packet.height=info.height;
        packet.satellite=jamxPassed?QStringLiteral("JAMX"):
                                    canonicalSatelliteName(frame.satellite);
        packet.captureLocal=frame.captureLocal;packet.sources=frame.sources;
        packet.fecEvidence=frame.quality;packet.vcid=frame.vcid;
        packet.firstMaster=frame.master;packet.lastMaster=frame.master;
        packet.status=passed?QStringLiteral("ssdv_crc32_pass"):
                             QStringLiteral("ssdv_crc32_failed");
        provisional.push_back(packet);
    }
    propagateJamxIdentity(provisional);
    auto out=keepCorroboratedCandidates(provisional);
    std::stable_sort(out.begin(),out.end(),[](const ImagePacket &a,const ImagePacket &b){
        if(a.satellite!=b.satellite)return a.satellite<b.satellite;
        if(a.imageId!=b.imageId)return a.imageId<b.imageId;
        return a.packetId<b.packetId;
    });
    return out;
}
QVector<ImagePacket> ImageTools::candidatesFromKiss(
        const QVector<ApplicationPacket> &packets,int apid){
    QVector<ImagePacket> provisional;
    QByteArray stream;
    int lastSequence=-1;
    for(const auto &packet:packets){
        // Never stitch arbitrary TM regions. Reassemble only the explicitly
        // selected APID stream and restart at transport sequence discontinuities.
        if(packet.apid!=apid||packet.classification!="ccsds_space_packet_candidate")continue;
        if(lastSequence>=0&&(lastSequence+1)%16384!=packet.sequence)stream.clear();
        lastSequence=packet.sequence;
        stream+=packet.data;
        while(stream.size()>=SSDV_PKT_SIZE){
            const int pos=stream.indexOf(char(0x55));
            if(pos<0){stream.clear();break;}
            if(pos>0)stream.remove(0,pos);
            if(stream.size()<SSDV_PKT_SIZE)break;
            const QByteArray raw=stream.left(SSDV_PKT_SIZE);
            QByteArray corrected;
            bool passed=false;
            ssdv_packet_info_t info{};
            if(inspectPacket(raw,false,info,corrected,passed)){
                ImagePacket candidate;
                candidate.bytes=passed?corrected:raw;candidate.apid=apid;
                candidate.imageId=info.image_id;candidate.packetId=info.packet_id;
                candidate.width=info.width;candidate.height=info.height;
                candidate.satellite=canonicalSatelliteName(packet.satellite);
                candidate.captureLocal=packet.captureLocal;candidate.sources=packet.sources;
                candidate.fecEvidence=packet.fecEvidence;candidate.vcid=packet.vcid;
                candidate.firstMaster=packet.firstMaster;candidate.lastMaster=packet.lastMaster;
                candidate.status=passed?QStringLiteral("ssdv_crc_pass"):
                                       QStringLiteral("ssdv_crc_failed");
                provisional.push_back(candidate);
                stream.remove(0,SSDV_PKT_SIZE);
            }else{
                // False sync/header matches are discarded one byte at a time;
                // they cannot become image rows just because dimensions look plausible.
                stream.remove(0,1);
            }
        }
        if(stream.size()>4096)stream=stream.right(SSDV_PKT_SIZE-1);
    }
    auto out=keepCorroboratedCandidates(provisional);
    return out;
}
bool ImageTools::savePackets(const QVector<ImagePacket> &packets,const QString &filename,QString *error){
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    for(const auto &p:packets)if(file.write(p.bytes)!=p.bytes.size()){
        if(error)*error=file.errorString();file.cancelWriting();return false;
    }
    if(!file.commit()){if(error)*error=file.errorString();return false;}
    return true;
}


QVector<ImagePacket> ImageTools::parseBinaryPackets(
        const QByteArray &data,int apid,bool prefer218,QString *error){
    QVector<ImagePacket> provisional;
    if(data.isEmpty()){
        if(error)*error=QStringLiteral("BIN 文件为空。");
        return provisional;
    }
    const bool packets218=(data.size()%SSDV_PKT_SIZE_DSLWP==0)&&
        ((data.size()%SSDV_PKT_SIZE)!=0||prefer218);
    if(packets218){
        for(int offset=0;offset+SSDV_PKT_SIZE_DSLWP<=data.size();offset+=SSDV_PKT_SIZE_DSLWP){
            const QByteArray bytes=data.mid(offset,SSDV_PKT_SIZE_DSLWP);
            QByteArray corrected;bool passed=false;bool jamxPassed=false;
            ssdv_packet_info_t info{};
            if(!inspectPacket(bytes,true,info,corrected,passed,false,&jamxPassed))continue;
            ImagePacket packet;
            packet.bytes=passed?corrected:bytes;packet.apid=apid;
            packet.imageId=info.image_id;packet.packetId=info.packet_id;
            packet.width=info.width;packet.height=info.height;
            if(jamxPassed)packet.satellite=QStringLiteral("JAMX");
            packet.status=passed?QStringLiteral("ssdv_crc32_pass"):
                                 QStringLiteral("ssdv_crc32_failed");
            provisional.push_back(packet);
        }
    }else if(data.size()%SSDV_PKT_SIZE==0){
        for(int offset=0;offset+SSDV_PKT_SIZE<=data.size();offset+=SSDV_PKT_SIZE){
            const QByteArray bytes=data.mid(offset,SSDV_PKT_SIZE);
            QByteArray corrected;bool passed=false;ssdv_packet_info_t info{};
            if(!inspectPacket(bytes,false,info,corrected,passed))continue;
            ImagePacket packet;
            packet.bytes=passed?corrected:bytes;packet.apid=apid;
            packet.imageId=info.image_id;packet.packetId=info.packet_id;
            packet.width=info.width;packet.height=info.height;
            packet.status=passed?QStringLiteral("ssdv_crc_pass"):
                                 QStringLiteral("ssdv_crc_failed");
            provisional.push_back(packet);
        }
    }else{
        if(error)*error=QStringLiteral(
            "BIN 长度不符合已支持的分片格式（218 字节 SSDV 分片或 256 字节标准 SSDV 包）。");
        return provisional;
    }
    propagateJamxIdentity(provisional);
    auto out=keepCorroboratedCandidates(provisional);
    if(out.isEmpty()&&error)*error=QStringLiteral(
        "文件中没有可验证或被有效分片佐证的 SSDV 数据；请确认导入的是图像 BIN，而不是遥测原始帧。");
    return out;
}

DecodedImage ImageTools::decodeImage(const QVector<ImagePacket> &packets,int imageId,bool shortPacketFormat){
    DecodedImage decoded;
    decoded.imageId=imageId;
    const int expectedSize=shortPacketFormat?SSDV_PKT_SIZE_DSLWP:SSDV_PKT_SIZE;
    const ssdv_mode_t mode=shortPacketFormat?ssdv_dslwp_mode:ssdv_normal_mode;
    struct Candidate {
        ImagePacket packet;
        ssdv_packet_info_t info{};
        QByteArray corrected;
        QString signature;
        bool crcPassed=false;
    };
    QVector<Candidate> candidates;
    candidates.reserve(qMin(packets.size(),16384));
    QHash<QString,QSet<int>> verifiedSequences;
    // The on-wire sequence number is 16-bit, so more than 65536 distinct input
    // records cannot add unique packets. Cap work on corrupt/hostile input.
    const int inputLimit=qMin(packets.size(),65536);
    for(int index=0;index<inputLimit;++index){
        const auto &packet=packets[index];
        if(packet.imageId!=imageId||packet.bytes.size()!=expectedSize||packet.packetId<0)continue;
        QByteArray correctedBytes;
        bool validationPassed=false;
        ssdv_packet_info_t info{};
        if(!inspectPacket(packet.bytes,shortPacketFormat,info,correctedBytes,validationPassed))continue;
        if(info.image_id!=imageId||info.packet_id!=packet.packetId)continue;
        if(packet.width>0&&packet.width!=info.width)continue;
        if(packet.height>0&&packet.height!=info.height)continue;
        const QByteArray &header=validationPassed?correctedBytes:packet.bytes;
        const int base=shortPacketFormat?0:6;
        const int flags=quint8(header[base+5])&~0x04;
        const QString type=shortPacketFormat?QStringLiteral("DSLWP"):QString::number(quint8(header[1]));
        const QString callsign=shortPacketFormat?QStringLiteral("DSLWP"):
            QString::fromLatin1(header.mid(2,4).toHex());
        const QString signature=QStringLiteral("%1|%2|%3x%4|%5|%6")
            .arg(canonicalSatelliteName(packet.satellite)).arg(type)
            .arg(info.width).arg(info.height).arg(flags).arg(callsign);
        Candidate candidate;candidate.packet=packet;candidate.info=info;
        candidate.corrected=correctedBytes;candidate.signature=signature;candidate.crcPassed=validationPassed;
        candidates.push_back(candidate);
        if(validationPassed)verifiedSequences[signature].insert(info.packet_id);
    }

    QString selectedSignature;
    int strongest=0;
    for(const auto &candidate:candidates){
        const int count=verifiedSequences.value(candidate.signature).size();
        if(count>strongest){strongest=count;selectedSignature=candidate.signature;}
    }
    if(strongest<=0){
        decoded.error=QStringLiteral("所选图像没有 CRC 通过的有效 SSDV 分片；为避免误识别数据导致崩溃，已跳过 JPEG 重建。");
        QSet<int> structurallyObserved;
        int maxSequence=-1;
        for(const auto &candidate:candidates){
            const int id=candidate.info.packet_id;
            structurallyObserved.insert(id);maxSequence=qMax(maxSequence,id);
            if(!decoded.badPacketIds.contains(id))decoded.badPacketIds.push_back(id);
        }
        for(int id=0;id<=maxSequence;++id)
            if(!structurallyObserved.contains(id))decoded.missingPacketIds.push_back(id);
        std::sort(decoded.badPacketIds.begin(),decoded.badPacketIds.end());
        return decoded;
    }

    std::map<int,ImagePacket> ordered;
    QSet<int> observedIds;
    QSet<int> badIds;
    const QSet<int> verifiedForImage=verifiedSequences.value(selectedSignature);
    for(const auto &candidate:candidates){
        if(candidate.signature!=selectedSignature)continue;
        const int id=candidate.info.packet_id;
        if(candidate.crcPassed){
            observedIds.insert(id);
            ImagePacket validated=candidate.packet;
            validated.bytes=candidate.corrected;
            validated.width=candidate.info.width;validated.height=candidate.info.height;
            validated.status=shortPacketFormat?QStringLiteral("ssdv_crc32_pass"):
                                               QStringLiteral("ssdv_crc_pass");
            if(ordered.find(id)==ordered.end())ordered.emplace(id,validated);
        }else if(verifiedForImage.size()>=2&&!verifiedForImage.contains(id)){
            observedIds.insert(id);badIds.insert(id);
        }
    }
    for(const auto &entry:ordered)badIds.remove(entry.first);
    for(int id:badIds)decoded.badPacketIds.push_back(id);
    std::sort(decoded.badPacketIds.begin(),decoded.badPacketIds.end());
    if(ordered.empty()){
        decoded.error=QStringLiteral("所选图像没有可安全解码的 CRC 通过分片。");
        return decoded;
    }
    // The upstream C decoder assumes entropy data is safe once a CRC passes.
    // A lone later fragment (packet ID > 0) has no image-start context and may
    // trigger unsafe gap-fill / entropy parsing when preceded by a synthetic
    // header packet. Refuse this underdetermined case before entering C code.
    // Preserve gap diagnostics even when the stream is too underdetermined to
    // send into the C decoder.
    int lastObserved=-1;
    for(int id:observedIds)lastObserved=qMax(lastObserved,id);
    for(int id=0;id<=lastObserved;++id)if(!observedIds.contains(id))decoded.missingPacketIds.push_back(id);
    // Two or more verified fragments can still reconstruct a partial image.
    if(ordered.size()==1 && ordered.begin()->first!=0){
        decoded.receivedPackets=1;
        decoded.crcPassedPackets=1;
        decoded.width=ordered.begin()->second.width;
        decoded.height=ordered.begin()->second.height;
        decoded.error=QStringLiteral("仅有一个非首包的 CRC 通过分片，缺少可靠图像起始上下文；已跳过 JPEG 解码以确保稳定性。");
        return decoded;
    }

    const ImagePacket *metadataSource=&ordered.begin()->second;
    decoded.width=metadataSource->width;
    decoded.height=metadataSource->height;
    decoded.receivedPackets=int(ordered.size());
    decoded.crcPassedPackets=int(ordered.size());
    for(const auto &entry:ordered){
        if(entry.second.width!=decoded.width||entry.second.height!=decoded.height){
            decoded.error=QStringLiteral("图像分片尺寸不一致，已停止重建以避免损坏的 JPEG 输出。");
            return decoded;
        }
    }

    auto makeBootstrapPacket=[&](const ImagePacket &source)->ImagePacket{
        ImagePacket bootstrap=source;
        QByteArray bytes=source.bytes;
        int payloadStart=0,payloadLength=0;
        if(shortPacketFormat){
            if(bytes.size()!=SSDV_PKT_SIZE_DSLWP||bytes.size()<9)return ImagePacket{};
            bytes[1]=char(0);bytes[2]=char(0);
            bytes[5]=char(quint8(bytes[5])&~0x04); // never inherit EOI
            bytes[6]=char(0);
            bytes[7]=char(0);bytes[8]=char(0);       // start at MCU zero
            payloadStart=9;payloadLength=205;
        }else{
            if(bytes.size()!=SSDV_PKT_SIZE||bytes.size()<15)return ImagePacket{};
            bytes[0]=char(0x55);
            bytes[7]=char(0);bytes[8]=char(0);
            bytes[11]=char(quint8(bytes[11])&~0x04); // never inherit EOI
            bytes[12]=char(0);
            bytes[13]=char(0);bytes[14]=char(0);    // start at MCU zero
            payloadStart=15;payloadLength=(quint8(bytes[1])==0x67?237:205);
        }
        // The upstream decoder must consume packet zero normally so it advances
        // its packet state. A 0xFFFF MCU sentinel causes an early return without
        // advancing packet_id. Zero-valued entropy bytes form a blank bootstrap
        // run; all real bytes after this packet come only from CRC-verified input.
        if(payloadStart+payloadLength>bytes.size())return ImagePacket{};
        std::fill(bytes.begin()+payloadStart,bytes.begin()+payloadStart+payloadLength,char(0));
        bootstrap.bytes=bytes;bootstrap.packetId=0;
        bootstrap.status=QStringLiteral("synthetic_bootstrap");
        return bootstrap;
    };

    std::map<int,ImagePacket> feedOrdered=ordered;
    bool syntheticBootstrap=false;
    if(feedOrdered.find(0)==feedOrdered.end()){
        ImagePacket bootstrap=makeBootstrapPacket(*metadataSource);
        if(bootstrap.bytes.isEmpty()){
            decoded.error=QStringLiteral("无法从候选分片读取 SSDV 图像头，不能生成部分预览。");
            return decoded;
        }
        feedOrdered.emplace(0,std::move(bootstrap));
        syntheticBootstrap=true;
    }

    // Validated packet signatures, CRC checks, dimension caps, and bounded output
    // are all enforced before the C decoder sees the payload.
    std::vector<std::uint8_t> jpegBuffer;
    try{
        jpegBuffer.resize(64U*1024U*1024U);
    }catch(const std::bad_alloc&){
        decoded.error=QStringLiteral("系统内存不足，无法分配 SSDV JPEG 缓冲区。");
        return decoded;
    }
    int restartBudget=qMin(int(feedOrdered.size())+1,8);
    bool replacedPacketZero=false;
    while(!feedOrdered.empty()&&restartBudget-->=0){
        ssdv_t decoder{};
        if(ssdv_dec_init(&decoder)!=SSDV_OK){decoded.error=QStringLiteral("SSDV 解码器初始化失败。");return decoded;}
        if(ssdv_dec_set_buffer(&decoder,jpegBuffer.data(),jpegBuffer.size())!=SSDV_OK){
            decoded.error=QStringLiteral("SSDV JPEG 缓冲区初始化失败。");return decoded;
        }
        int rejectedId=-1;
        for(const auto &entry:feedOrdered){
            if(entry.second.bytes.size()!=expectedSize){rejectedId=entry.first;break;}
            ssdv_packet_info_t check{};QByteArray corrected;bool good=false;
            const bool synthetic=entry.second.status==QStringLiteral("synthetic_bootstrap");
            if(!inspectPacket(entry.second.bytes,shortPacketFormat,check,corrected,good,synthetic)||
               (!good&&!synthetic)){
                rejectedId=entry.first;break;
            }
            if(entry.first!=0&&!good){rejectedId=entry.first;break;}
            const auto *bytes=reinterpret_cast<const std::uint8_t*>(entry.second.bytes.constData());
            const char result=ssdv_dec_feed(&decoder,bytes,mode);
            if(result==SSDV_BUFFER_FULL){decoded.error=QStringLiteral("SSDV JPEG 输出缓冲区已满。");return decoded;}
            if(result!=SSDV_FEED_ME&&result!=SSDV_OK){rejectedId=entry.first;break;}
            if(!(syntheticBootstrap&&entry.first==0)){
                ssdv_packet_info_t info{};
                ssdv_dec_header(&info,bytes,mode);
                decoded.complete=decoded.complete||info.eoi!=0;
            }
            if(result==SSDV_OK&&!(synthetic&&entry.first==0))break;
        }
        if(rejectedId>=0){
            if(rejectedId==0&&syntheticBootstrap){
                decoded.error=QStringLiteral("无法从候选分片建立 SSDV JPEG 图像头，无法生成部分预览。");return decoded;
            }
            if(rejectedId==0&&!syntheticBootstrap&&!replacedPacketZero){
                const auto zero=feedOrdered.find(0);
                const ImagePacket *source=zero==feedOrdered.end()?metadataSource:&zero->second;
                ImagePacket bootstrap=makeBootstrapPacket(*source);
                if(!bootstrap.bytes.isEmpty()){
                    if(!decoded.badPacketIds.contains(0))decoded.badPacketIds.push_back(0);
                    feedOrdered[0]=std::move(bootstrap);ordered.erase(0);
                    decoded.receivedPackets=int(ordered.size());syntheticBootstrap=true;
                    replacedPacketZero=true;decoded.complete=false;continue;
                }
            }
            if(!decoded.badPacketIds.contains(rejectedId))decoded.badPacketIds.push_back(rejectedId);
            feedOrdered.erase(rejectedId);
            ordered.erase(rejectedId);
            decoded.receivedPackets=int(ordered.size());decoded.complete=false;
            continue;
        }
        std::uint8_t *jpeg=nullptr;std::size_t jpegLength=0;
        if(ssdv_dec_get_jpeg(&decoder,&jpeg,&jpegLength)!=SSDV_OK||jpeg==nullptr||jpegLength==0||
           jpegLength>std::size_t(std::numeric_limits<int>::max())||jpegLength>jpegBuffer.size()){
            decoded.error=QStringLiteral("有效分片不足以生成 JPEG，或输出超出安全缓冲区限制。");return decoded;
        }
        decoded.jpeg=QByteArray(reinterpret_cast<const char*>(jpeg),int(jpegLength));
        decoded.receivedPackets=int(ordered.size());
        std::sort(decoded.badPacketIds.begin(),decoded.badPacketIds.end());
        return decoded;
    }
    decoded.error=QStringLiteral("有效图像头或分片不足，无法重建 JPEG。");
    std::sort(decoded.badPacketIds.begin(),decoded.badPacketIds.end());
    return decoded;
}
}
