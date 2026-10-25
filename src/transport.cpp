#include "transport.h"
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <array>

namespace orbit {
namespace {
int octet(char c) { return static_cast<unsigned char>(c); }
bool isPaddingOnly(const QByteArray &bytes) {
    if(bytes.isEmpty())return true;
    return std::all_of(bytes.cbegin(),bytes.cend(),
                       [](char c){return static_cast<unsigned char>(c)==0xaa;});
}
struct State {
    QByteArray buffer;
    bool started = false;
    bool escaped = false;
    bool oversized = false;
    bool haveCount = false;
    int previousCount = -1;
    int firstMaster = -1, lastMaster = -1;
    int frameCount=0;
    bool allFecReported=true;
    bool anyFecReported=false;
    QDateTime firstTime;
    QString timeBasis="unknown";
    QStringList sources;
    void resetPacket(){
        buffer.clear();escaped=false;oversized=false;
        firstMaster=-1;lastMaster=-1;frameCount=0;
        allFecReported=true;anyFecReported=false;
        firstTime={};timeBasis="unknown";sources.clear();
    }
};
void recognizeOptionalSpacePacket(ApplicationPacket &packet) {
    const QByteArray &b=packet.bytes;
    if(b.size()<7)return;
    const int id=(octet(b[0])<<8)|octet(b[1]);
    const int seq=(octet(b[2])<<8)|octet(b[3]);
    const int length=1+(octet(b[4])<<8)+octet(b[5]);
    if((id>>13)!=0 || length<1)return;
    const int expected=length+6;
    const int suffix=b.size()-expected;
    // Application CRC may be appended, but is NOT checked. These are merely
    // candidate layouts, never a claim of checksum validity.
    if(suffix!=0 && suffix!=2 && suffix!=4)return;
    packet.apid=id&0x7ff;
    packet.sequence=seq&0x3fff;
    packet.data=b.mid(6,length);
    packet.opaqueSuffixBytes=suffix;
    packet.classification="ccsds_space_packet_candidate";
    packet.diagnostic=QString("Candidate inner header; %1 suffix bytes unverified").arg(suffix);
}
}
QByteArray Transport::frameData(const QByteArray &frame) {
    return frame.size()==223?frame.mid(5,218):QByteArray();
}
QVector<ApplicationPacket> Transport::reassemble(const QVector<Frame> &frames,TransportStats *stats){
    QVector<ApplicationPacket> out;
    TransportStats observed;
    QHash<QString,State> states;
    constexpr int MaximumKissPacket=65536;
    // The original recorder clock may be absent for BIN-only frames. A bounded
    // sequence window can be ordered by the TM master counter without inventing time.
    QVector<const Frame*> ordered;
    ordered.reserve(frames.size());
    QVector<int> counters;
    for(const auto &f:frames){
        ordered.push_back(&f);
        if(f.master>=0) counters.push_back(f.master);
    }
    const bool recordedOrder=std::any_of(frames.cbegin(),frames.cend(),[](const Frame &f){
        return !f.sources.isEmpty();
    });
    if(!recordedOrder && counters.size()>=2 && counters.size()<128){
        std::sort(counters.begin(),counters.end());
        counters.erase(std::unique(counters.begin(),counters.end()),counters.end());
        int gap=-1,start=0;
        for(int i=0;i<counters.size();++i){
            const int next=counters[(i+1)%counters.size()];
            const int distance=(next-counters[i]+256)%256;
            if(distance>gap){gap=distance;start=next;}
        }
        // If the sample covers nearly an entire 256-count cycle, ordering is
        // ambiguous and we preserve source order instead.
        if(gap>128){
            std::stable_sort(ordered.begin(),ordered.end(),[start](const Frame *a,const Frame *b){
                if(a->master<0)return false;
                if(b->master<0)return true;
                return ((a->master-start+256)%256)<((b->master-start+256)%256);
            });
        }
    }
    for(const Frame *source:ordered){
        const Frame &frame=*source;
        if(frame.vcid<0)continue;
        if(frame.satellite=="BY04"){
            if(frame.bytes.size()!=223)continue;
            const int scid=((quint8(frame.bytes[0])&0x3f)<<4)|(quint8(frame.bytes[1])>>4);
            // BY04 uses spacecraft ID 0x205. VC1 is the native 218-byte
            // DSLWP SSDV image service, NOT KISS. VC0/VC2 may carry KISS.
            if(scid!=0x205 || frame.vcid==1)continue;
        }
        const int scid=frame.bytes.size()==223?
            ((quint8(frame.bytes[0])&0x3f)<<4)|(quint8(frame.bytes[1])>>4):-1;
        const QString streamKey=frame.satellite+":"+QString::number(scid)+":"+QString::number(frame.vcid);
        State &s=states[streamKey];
        // Only the per-VC counter can assess continuity; master count interleaves VCs.
        if(s.haveCount && frame.virtualCount>=0 && ((s.previousCount+1)&0xff)!=frame.virtualCount){
            if(s.started && !s.buffer.isEmpty() && !isPaddingOnly(s.buffer))
                ++observed.interruptedByCounterGap;
            s.resetPacket();
            s.started=false;
        }
        s.previousCount=frame.virtualCount;
        s.haveCount=frame.virtualCount>=0;
        if(!frame.complete || frame.bytes.size()!=223){
            if(s.started && !s.buffer.isEmpty())++observed.interruptedByCounterGap;
            s.resetPacket();s.started=false;continue;
        }
        const QByteArray data=frame.bytes.mid(5,218);
        bool countedThisFrame=false;
        for(char c:data){
            const int b=octet(c);
            if(b==0xc0){
                // A delimiter cannot legally terminate a pending FESC sequence.
                // Fail closed; do not publish partial bytes with a guessed escape.
                if(s.escaped)++observed.malformedEscapePackets;
                if(s.started && !s.buffer.isEmpty() && !s.oversized &&
                   !s.escaped && !isPaddingOnly(s.buffer)){
                    ApplicationPacket pkt;
                    pkt.bytes=s.buffer;pkt.satellite=frame.satellite;
                    pkt.vcid=frame.vcid;
                    pkt.firstMaster=s.firstMaster;
                    pkt.lastMaster=s.lastMaster;
                    pkt.contributingFrames=s.frameCount;
                    pkt.sources=s.sources;
                    pkt.captureLocal=s.firstTime;
                    pkt.timestampBasis=s.timeBasis;
                    pkt.fecEvidence=s.allFecReported && s.anyFecReported
                        ? "log_fec_reported"
                        : (s.anyFecReported?"mixed_fec_unverified":"bin_only_unverified");
                    recognizeOptionalSpacePacket(pkt);
                    ++observed.decodedPackets;
                    out.push_back(std::move(pkt));
                }
                s.resetPacket();
                s.started=true;
                countedThisFrame=false;
                continue;
            }
            // Bytes outside any KISS boundary belong to an incomplete or unusable fragment.
            if(!s.started || s.oversized)continue;
            if(s.firstMaster<0)s.firstMaster=frame.master;
            s.lastMaster=frame.master;
            if(!countedThisFrame){
                ++s.frameCount;countedThisFrame=true;
                if(frame.quality=="log_fec_reported")s.anyFecReported=true;
                else s.allFecReported=false;
            }
            if(!frame.sources.isEmpty())
                for(const QString &source:frame.sources)
                    if(!s.sources.contains(source))s.sources<<source;
            if(!s.firstTime.isValid() && frame.captureLocal.isValid()){
                s.firstTime=frame.captureLocal;s.timeBasis=frame.timeBasis;
            }
            if(s.escaped){
                s.escaped=false;
                if(b==0xdc)s.buffer.append(char(0xc0));
                else if(b==0xdd)s.buffer.append(char(0xdb));
                else {++observed.malformedEscapePackets;s.oversized=true;s.buffer.clear();}
            }else if(b==0xdb)s.escaped=true;
            else s.buffer.append(c);
            if(s.buffer.size()>MaximumKissPacket){
                ++observed.oversizedPackets;s.oversized=true;s.buffer.clear();
            }
        }
    }
    // No closing FEND: never publish a partial packet as a valid application packet.
    for(auto it=states.constBegin();it!=states.constEnd();++it)
        if(it.value().started && !it.value().buffer.isEmpty() &&
           !it.value().oversized && !isPaddingOnly(it.value().buffer))
            ++observed.incompleteAtEof;
    if(stats)*stats=observed;
    return out;
}
QVector<ApplicationPacket> Transport::assembleSegments(const QVector<ApplicationPacket> &packets,
                                                      int secondaryHeaderOctets){
    struct Group {ApplicationPacket first;QByteArray payload,lastBytes;int last=-1,order=0;};
    QHash<QString,Group> pending;
    QVector<ApplicationPacket> out;
    auto flush=[&](const QString &key){
        auto it=pending.find(key);if(it==pending.end())return;
        it->first.assemblyStatus="incomplete_segments";
        out.push_back(it->first);pending.erase(it);
    };
    int inputOrder=0;
    for(const auto &p:packets){
        const int order=inputOrder++;
        const auto &b=p.bytes;
        if(p.classification!="ccsds_space_packet_candidate" || b.size()<7 ||
           (p.opaqueSuffixBytes!=0&&p.opaqueSuffixBytes!=2&&p.opaqueSuffixBytes!=4) ||
           secondaryHeaderOctets<0 || p.data.size()<secondaryHeaderOctets){
            out.push_back(p);continue;
        }
        const int id=(octet(b[0])<<8)|octet(b[1]);
        if((id>>13)!=0 || (id&0x1000)!=0 ||
           ((octet(b[4])<<8)|octet(b[5]))+7+p.opaqueSuffixBytes!=b.size() ||
           p.data!=b.mid(6,b.size()-6-p.opaqueSuffixBytes)){
            out.push_back(p);continue;
        }
        const int seq=(octet(b[2])<<8)|octet(b[3]);
        const int flags=seq>>14,count=seq&0x3fff;
        const QString key=p.satellite+":"+QString::number(p.vcid)+":"+QString::number(id);
        if(flags==3){flush(key);out.push_back(p);continue;}
        if(flags==1){
            if(pending.contains(key)&&pending[key].lastBytes==b)continue;
            flush(key);Group g;g.order=order;g.first=p;g.first.segments={b};
            g.payload=p.data;g.last=count;g.lastBytes=b;pending.insert(key,g);continue;
        }
        if(!pending.contains(key)){
            auto orphan=p;orphan.assemblyStatus="incomplete_segments";orphan.segments={b};
            out.push_back(orphan);continue;
        }
        auto &g=pending[key];
        if(g.last==count&&g.lastBytes==b)continue;
        if(((g.last+1)&0x3fff)!=count ||
           p.data.left(secondaryHeaderOctets)!=g.payload.left(secondaryHeaderOctets) ||
           g.payload.size()+p.data.size()-secondaryHeaderOctets>65536 || g.first.segments.size()>=4096){
            flush(key);auto orphan=p;orphan.assemblyStatus="incomplete_segments";
            orphan.segments={b};out.push_back(orphan);continue;
        }
        g.payload+=p.data.mid(secondaryHeaderOctets);g.last=count;g.lastBytes=b;
        g.first.segments.push_back(b);g.first.lastMaster=p.lastMaster;
        g.first.contributingFrames+=p.contributingFrames;
        for(const auto &source:p.sources)if(!g.first.sources.contains(source))g.first.sources<<source;
        if(g.first.fecEvidence!=p.fecEvidence)g.first.fecEvidence="mixed_fec_unverified";
        if(flags==2){
            auto complete=g.first;
            complete.data=g.payload;complete.assembledBytes=complete.bytes.left(6);
            complete.assembledBytes[2]=char((quint8(complete.assembledBytes[2])&0x3f)|0xc0);
            const int length=complete.data.size()-1;
            complete.assembledBytes[4]=char(length>>8);complete.assembledBytes[5]=char(length&255);
            complete.assembledBytes+=complete.data;
            complete.assemblyStatus="reassembled_segments";
            out.push_back(std::move(complete));pending.remove(key);
        }
    }
    auto keys=pending.keys();
    std::sort(keys.begin(),keys.end(),[&](const auto &a,const auto &b){return pending[a].order<pending[b].order;});
    for(const auto &key:keys)flush(key);
    return out;
}
bool Transport::exportJson(const QString &path,const QVector<ApplicationPacket> &packets,QString *error){
    QJsonArray a;
    for(const auto &p:packets){
        QJsonArray src;
        for(const auto &s:p.sources)src.append(s);
        a.append(QJsonObject{
          {"satellite",p.satellite},{"vcid",p.vcid},
          {"first_master",p.firstMaster},{"last_master",p.lastMaster},
          {"contributing_frames",p.contributingFrames},
          {"capture_local",p.captureLocal.isValid()?p.captureLocal.toString(Qt::ISODateWithMs):QString()},
          {"timestamp_basis",p.timestampBasis},{"spacecraft_utc",QJsonValue::Null},
          {"classification",p.classification},{"integrity",p.integrity},
          {"opaque_suffix_bytes",p.opaqueSuffixBytes},
          {"fec_evidence",p.fecEvidence},
          {"apid",p.apid},{"packet_sequence",p.sequence},
          {"data_hex",QString::fromLatin1(p.bytes.toHex())},
          {"user_data_hex",QString::fromLatin1(p.data.toHex())},
          {"diagnostic",p.diagnostic},{"sources",src}
        });
    }
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly)){if(error)*error=f.errorString();return false;}
    f.write(QJsonDocument(a).toJson(QJsonDocument::Indented));
    if(!f.commit()){if(error)*error=f.errorString();return false;}
    return true;
}
}
