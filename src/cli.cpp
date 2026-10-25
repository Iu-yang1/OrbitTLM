#include "core.h"
#include "schema.h"
#include "transport.h"
#include "business.h"
#include "by04.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
using namespace orbit;
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName("OrbitTLM-cli");
    QCommandLineParser args;
    args.setApplicationDescription("Satellite telemetry batch analysis; all data quality flags retained");
    args.addHelpOption();
    args.addOptions({
        QCommandLineOption({"l","log"},"FEC LOG path","file"),
        QCommandLineOption({"b","bin"},"223-byte BIN path","file"),
        QCommandLineOption({"s","satellite"},"ASRTU-1 or BY04 (8104)","name","ASRTU-1"),
        QCommandLineOption({"p","profile"},"Optional engineering rules JSON","file"),
        QCommandLineOption("json","Write full ordered JSON","file"),
        QCommandLineOption("packets-json","Write reassembled KISS packets with unverified application integrity","file"),
        QCommandLineOption("telemetry-json","Write business packet fields and engineering units to JSON","file"),
        QCommandLineOption("telemetry-csv","Write business packet fields and engineering units to CSV","file"),
        QCommandLineOption("by04-ssdv-bin","Save native 218-byte BY04 VC1 DSLWP SSDV image packets","file"),
        QCommandLineOption("csv","Write full ordered CSV","file"),
        QCommandLineOption("quaternion-csv","Write SAT-S3 official-schema calculated q0..q3; CRC unverified","file"),
        QCommandLineOption("candidate-quaternion-csv","Write experimental legacy frame candidate quaternions","file"),
        QCommandLineOption("sqlite","Persist one session to SQLite","file")
    });
    args.process(app);
    QTextStream out(stdout),err(stderr);
    const QString sat=args.value("satellite");
    if(sat!="ASRTU-1"&&sat!="BY04"){
        err<<"ERROR: satellite must be ASRTU-1 or BY04\n";return 2;
    }
    if(!args.isSet("log")&&!args.isSet("bin")){
        err<<"ERROR: pass --log and/or --bin\n";return 2;
    }
    QString reason;
    const auto logs=args.isSet("log")?Protocol::fromLog(args.value("log"),sat,&reason):QVector<Frame>{};
    if(!reason.isEmpty()){err<<"LOG: "<<reason<<"\n";return 3;}
    const auto bins=args.isSet("bin")?Protocol::fromBin(args.value("bin"),sat,&reason):QVector<Frame>{};
    if(!reason.isEmpty()){err<<"BIN: "<<reason<<"\n";return 3;}
    Stats stats;
    auto frames=Protocol::fuse(logs,bins,&stats);
    if(args.isSet("profile")){
        Schema schema;
        if(!schema.load(args.value("profile"),&reason)){
            err<<"Schema: "<<reason<<"\n";return 4;
        }
        if(schema.satellite()!=sat){
            err<<"Schema satellite mismatch\n";return 4;
        }
        for(auto &f:frames)schema.apply(f);
    }
    TransportStats packetStats;
    const auto packets=Transport::reassemble(frames,&packetStats);
    if(args.isSet("packets-json")&&
       !Transport::exportJson(args.value("packets-json"),packets,&reason)){
        err<<"KISS packets: "<<reason<<"\n";return 6;
    }
    QVector<BusinessPacket> business;
    BusinessStats bs;
    if(sat=="ASRTU-1"){
        BusinessCatalog catalog;
        if(!catalog.loadDirectory(QCoreApplication::applicationDirPath()+
             "/protocols/asrtu1",&reason)){
            err<<"ASRTU-1 catalog: "<<reason<<"\n";return 8;
        }
        business=catalog.decodeAll(packets,&bs);
        if(args.isSet("telemetry-json") &&
           !BusinessCatalog::exportJson(args.value("telemetry-json"),business,&reason)){
            err<<"Telemetry JSON: "<<reason<<"\n";return 8;
        }
        if(args.isSet("telemetry-csv") &&
           !BusinessCatalog::exportCsv(args.value("telemetry-csv"),business,&reason)){
            err<<"Telemetry CSV: "<<reason<<"\n";return 8;
        }
        if(args.isSet("quaternion-csv") &&
           !BusinessCatalog::exportQuaternionCsv(args.value("quaternion-csv"),business,&reason)){
            err<<"SAT-S3 quaternion CSV: "<<reason<<"\n";return 8;
        }
    }else if(sat=="BY04"){
        business=By04Decoder::decodeAll(packets);
        for(const auto &item:business){
            if(item.identification=="by04_observed_layout_candidate")++bs.recognized;
            else ++bs.unknown;
            for(const auto &f:item.fields){
                if(f.status=="by04_observed_float32")++bs.decodedFields;
                else if(f.status.startsWith("raw_by04_"))++bs.unsupportedFields;
            }
        }
        if(args.isSet("quaternion-csv")){
            err<<"BY04 does not reuse SAT-S3 quaternion engineering fields; no verified BY04 mission schema available\n";
            return 8;
        }
        if(args.isSet("telemetry-json") &&
           !BusinessCatalog::exportJson(args.value("telemetry-json"),business,&reason)){
            err<<"BY04 telemetry JSON: "<<reason<<"\n";return 8;
        }
        if(args.isSet("telemetry-csv") &&
           !BusinessCatalog::exportCsv(args.value("telemetry-csv"),business,&reason)){
            err<<"BY04 telemetry CSV: "<<reason<<"\n";return 8;
        }
        if(args.isSet("by04-ssdv-bin")){
            const auto imagePackets=By04Decoder::images(frames);
            if(!ImageTools::savePackets(imagePackets,args.value("by04-ssdv-bin"),&reason)){
                err<<"BY04 DSLWP SSDV: "<<reason<<"\n";return 8;
            }
        }
    }
    if(args.isSet("sqlite")){
        Database db;
        if(!db.open(args.value("sqlite"),&reason)||
           !db.save(sat+" batch import",frames,&reason)){
            err<<"SQLite: "<<reason<<"\n";return 5;
        }
    }
    if(args.isSet("csv")&&!Protocol::exportCsv(args.value("csv"),frames,&reason)){
        err<<"CSV: "<<reason<<"\n";return 6;
    }
    if(args.isSet("json")&&!Protocol::exportJson(args.value("json"),frames,&reason)){
        err<<"JSON: "<<reason<<"\n";return 6;
    }
    if(args.isSet("candidate-quaternion-csv") &&
       !Protocol::exportAttitudeCsv(args.value("candidate-quaternion-csv"),frames,&reason)){
        err<<"Experimental quaternion: "<<reason<<"\n";return 6;
    }
    const QJsonObject result{
        {"satellite",sat},{"log_observations",stats.logObservations},
        {"bin_blocks",stats.binBlocks},{"unique_frames",stats.unique},
        {"log_fec_reported",stats.trustedFromLog},
        {"structural_only",stats.structuralOnly},
        {"invalid",stats.invalid},{"duplicate_observations",stats.duplicates},
        {"application_kiss_packets",packets.size()},
        {"catalog_business_recognized",bs.recognized},
        {"catalog_business_unknown",bs.unknown},
        {"engineering_fields_decoded",bs.decodedFields},
        {"engineering_fields_raw_only",bs.unsupportedFields},
        {"engineering_fields_out_of_range",bs.truncatedFields},
        {"application_interrupted_by_gap",packetStats.interruptedByCounterGap},
        {"application_incomplete_at_eof",packetStats.incompleteAtEof},
        {"by04_layout_status",sat=="BY04"?"observed_candidates_not_official_wire_dictionary":"not_applicable"},
        {"note","223 bytes = 5-byte TM header + 218-byte data region; BY04 VC1 is native SSDV, VC0/2 use KISS; application CRC not verified"}
    };
    out<<QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return 0;
}
