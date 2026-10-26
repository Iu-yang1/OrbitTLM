#pragma once
#include "core.h"
#include "business.h"
#include "schema.h"
#include "ssdv.h"
#include "transport.h"
#include <QMainWindow>
#include <QPixmap>
#include <QHash>
class QCheckBox;
class QComboBox;
class QChartView;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QTabWidget;
class QTabBar;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;
class QSpinBox;
class QProgressBar;
class QStackedWidget;
class QFrame;
class AttitudeWidget;
class ImagePreview;
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent=nullptr);
    bool openSession(const QString &log,const QString &bin,const QString &satellite,QString *error=nullptr);
private:
    void buildMenus();
    void buildToolbar();
    void buildWorkspace();
    void buildOverview(QTabWidget *tabs);
    void buildEngineering(QTabWidget *tabs);
    void buildAttitude(QTabWidget *tabs);
    void buildImages(QTabWidget *tabs);
    void buildRaw(QTabWidget *tabs);
    void buildDiagnostics(QTabWidget *tabs);
    void applyStyles();
    void updatePaths();
    void importSession();
    void loadHistory();
    void rebuildSession();
    void updateMetrics();
    void populateEvents();
    void applyEventFilter();
    void showEvent(int index);
    void selectEvent(int index);
    void seekVisible(int direction);
    void populateEngineering(int index);
    void applyEngineeringFilter();
    void plotEngineering(const QString &packet,const QString &code,int upTo);
    void updateAttitude(int index);
    void updateOverview(int index);
    void updateRawPacket(int index);
    void updateFrameList();
    void inspectFrame(int index);
    void exportFrames(bool csv);
    void exportEngineering(bool csv);
    void exportQuaternions(bool experimental=false);
    void exportKiss();
    void exportSsdv();
    void decodeSsdv();
    void scanSsdv();
    void updateImageProgress(int imageId);
    QString satellite() const;
    QString eventTitle(const orbit::BusinessPacket &packet) const;
    QString qualityLabel(const QString &status) const;
    QString sourceLabel(const QStringList &sources) const;
    QString utcLabel(const QDateTime &dt) const;
    const orbit::BusinessPacket *latestRecognized(int index,const QStringList &prefixes) const;
    void updateFieldCard(QGroupBox *card,const QStringList &prefixes,int index);
    void setDiagnosticsVisible(bool enabled);

    QString logPath_,binPath_;
    orbit::Database db_;
    orbit::BusinessCatalog catalog_;
    orbit::Schema customSchema_;
    bool catalogReady_=false,hasCustomSchema_=false;
    orbit::LiveTcp *live_=nullptr;
    orbit::Stats frameStats_;
    orbit::TransportStats transportStats_;
    QVector<orbit::Frame> frames_;
    QVector<orbit::ApplicationPacket> packets_;
    QVector<orbit::BusinessPacket> business_;
    QVector<orbit::ImagePacket> images_;
    QPixmap decodedImage_;
    QString imagePath_;
    int currentIndex_=-1;
    QLabel *sourceLabel_=nullptr,*message_=nullptr,*currentTitle_=nullptr,
           *currentMeta_=nullptr,*integrityLabel_=nullptr,*frameMetric_=nullptr,
           *recognizedMetric_=nullptr,*unknownMetric_=nullptr,*timeMetric_=nullptr,
           *imageMetric_=nullptr,*transportMetric_=nullptr,*positionLabel_=nullptr,
           *engineeringSummary_=nullptr,*fieldDetail_=nullptr,*attitudeInfo_=nullptr,
           *imageInfo_=nullptr,*diagnosticsSummary_=nullptr;
    QComboBox *satelliteSelect_=nullptr,*speedSelect_=nullptr;
    QLineEdit *eventSearch_=nullptr,*engineeringSearch_=nullptr;
    QCheckBox *recognizedOnly_=nullptr;
    QTableWidget *eventTable_=nullptr,*engineeringTable_=nullptr,
                 *frameTable_=nullptr,*packetTable_=nullptr,*imageTable_=nullptr;
    QTreeWidget *fieldTree_=nullptr;
    QChartView *engineeringChart_=nullptr,*attitudeChart_=nullptr;
    AttitudeWidget *attitudeWidget_=nullptr;
    ImagePreview *imagePreview_=nullptr;
    QPlainTextEdit *rawHex_=nullptr,*debugText_=nullptr;
    QSlider *timeline_=nullptr;
    QTimer *timer_=nullptr;
    QTabWidget *pages_=nullptr;
    QTabBar *navigation_=nullptr;
    int diagnosticTab_=-1;
    QGroupBox *powerCard_=nullptr,*attitudeCard_=nullptr,*thermalCard_=nullptr,*payloadCard_=nullptr;
    QLabel *quaternion_[4]{};
    QLabel *normLabel_=nullptr;
    QSpinBox *imageApid_=nullptr;
    QProgressBar *imageCoverage_=nullptr;
    QLabel *imageCoverageNote_=nullptr;
};
