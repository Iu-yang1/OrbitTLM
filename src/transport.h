#pragma once
#include "core.h"
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace orbit {
struct TransportStats {
    int decodedPackets = 0;
    int interruptedByCounterGap = 0;
    int malformedEscapePackets = 0;
    int oversizedPackets = 0;
    int incompleteAtEof = 0;
};
struct ApplicationPacket {
    QByteArray bytes;               // entire decoded KISS content, opaque by default
    QByteArray data;                // optional CCSDS Space Packet user bytes
    QString satellite;
    QStringList sources;
    QDateTime captureLocal;         // no spacecraft UTC implied
    QString timestampBasis = "unknown";
    QString integrity = "not_crc_verified"; // KISS structural recovery; application checksum not evaluated
    QString fecEvidence = "unknown";
    QString classification = "opaque_variable_length";
    QString diagnostic;
    int vcid = -1;
    int firstMaster = -1;
    int lastMaster = -1;
    int apid = -1;                  // populated only if optional 6-byte header validates
    int sequence = -1;
    int opaqueSuffixBytes = -1; // bytes after optional inner packet length (CRC unverified)
    int contributingFrames = 0;
    int replayOrder = -1;
    QByteArray assembledBytes;
    QVector<QByteArray> segments;
    QString assemblyStatus;
};
class Transport {
public:
    // Each input is an RS-decoded 223-byte TM *information* frame.
    // KISS decoding happens on the 218-byte TM data zone, across VC frame boundaries.
    // Input must be ordered by on-air frame sequence. No synthetic timestamps.
    static QVector<ApplicationPacket> reassemble(const QVector<Frame> &frames,
                                                TransportStats *stats = nullptr);
    static QByteArray frameData(const QByteArray &informationFrame);
    // secondaryHeaderOctets is mission-defined, never inferred from CCSDS flags.
    static QVector<ApplicationPacket> assembleSegments(const QVector<ApplicationPacket> &packets,
                                                       int secondaryHeaderOctets=0);
    static bool exportJson(const QString &path,const QVector<ApplicationPacket> &packets,
                           QString *error=nullptr);
};
}
