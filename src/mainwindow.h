#pragma once
#include "core.h"
#include "telemetry_store.h"
#include "business.h"
#include "schema.h"
#include "ssdv.h"
#include "transport.h"
#include <QMainWindow>
#include <QPixmap>
#include <QHash>
#include <QStringList>
#include <QJsonObject>
#include <QTranslator>
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
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;
class QSpinBox;
class QProgressBar;
class QStackedWidget;
class QFrame;
class QAction;
class QMenu;
class QSplitter;
class AttitudeWidget;
class ImagePreview;
class ReplayTimelineWidget;
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget *parent=nullptr);
private:
    friend class UiSmoke;
    void buildMenus();
    void setReplayListCollapsed(bool collapsed);
    void showExportDialog();
    void configureTcpServer();
    void loadApplicationSettings();
    void saveApplicationSettings();
    void applyLanguage();
    bool loadLanguageFile();
    QString uiText(const QString &source) const;
    void setTranslatedLabel(QLabel *label,const QString &source);
    void buildToolbar();
    void buildWorkspace();
    void buildOverview(QTabWidget *tabs);
    void buildEngineering(QTabWidget *tabs);
    void buildComponentTabs(QTabWidget *tabs);
    void updateComponentTabs(int index);
    bool updateWebHistory(QString *error=nullptr);
    void buildAttitude(QTabWidget *tabs);
    void buildImages(QTabWidget *tabs);
    void buildRaw(QTabWidget *tabs);
    void buildDiagnostics(QTabWidget *tabs);
    void applyStyles();
    void updatePaths();
    void importSession();
    void loadHistory();
    void rebuildSession(bool preferLatest=false);
    void updateMetrics();
    void populateEvents();
    void applyEventFilter();
    void showEvent(int index);
    void selectEvent(int index);
    void seekVisible(int direction);
    void populateEngineering(int index);
    void plotEngineering(const QString &packet,const QString &code,int upTo);
    void updateAttitude(int index);
    void updateOverview(int index);
    void updateRawPacket(int index);
    void updateFrameList();
    void inspectFrame(int index);
    void inspectRawFrame(int index);
    void exportFrames(bool csv);
    void exportEngineering(bool csv);
    void exportQuaternions(bool experimental=false);
    void exportKiss();
    void exportSsdv();
    void exportSelectedImageBin();
    void importImageBin();
    void decodeSsdv();
    bool decodeSsdvImage(int imageId,bool showErrors);
    void scanSsdv();
    void updateImageProgress(int imageId);
    void populateImageTable();
    int selectedImageId() const;
    QString selectedImageSatellite() const;
    int selectedImagePacketSize() const;
    void appendSsdvTimelineEvents();
    QString satellite() const;
    QString resolvedSatellite() const;
    QString eventTitle(const orbit::BusinessPacket &packet) const;
    QString eventTitleSource(const orbit::BusinessPacket &packet) const;
    QString qualityLabel(const QString &status) const;
    QString sourceLabel(const QStringList &sources) const;
    QString utcLabel(const QDateTime &dt) const;
    const orbit::BusinessPacket *latestRecognized(int index,const QStringList &prefixes) const;
    void updateFieldCard(QGroupBox *card,const QStringList &prefixes,int index);
    void setDiagnosticsVisible(bool enabled);

    QString logPath_,binPath_;
    orbit::Database db_;
    orbit::SatelliteTelemetryStore satelliteStore_;
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
    QSplitter *workspaceSplitter_=nullptr;
    QFrame *replayPanel_=nullptr;
    QWidget *replayContent_=nullptr;
    QPushButton *replayCollapseButton_=nullptr;
    QPushButton *tcpToggleButton_=nullptr,*playPauseButton_=nullptr;
    bool tcpConnected_=false,tcpConnecting_=false;
    QAction *replayListAction_=nullptr;
    QMenu *languageMenu_=nullptr;
    bool replayListCollapsed_=false;
    int replayPanelWidth_=490;
    QString tcpHost_=QStringLiteral("127.0.0.1");
    int tcpPort_=9985;
    QString languageCode_=QStringLiteral("zh-CN");
    QTranslator qtTranslator_;
    QJsonObject translations_;
    QJsonObject webTranslations_;
    QStringList translationKeysByLength_;
    mutable QHash<QString,QString> uiTextCache_;
    QString exportDirectoryOverride_;
    QLabel *sourceLabel_=nullptr,*message_=nullptr,*currentTitle_=nullptr,
           *currentMeta_=nullptr,*integrityLabel_=nullptr,*attitudeModeLabel_=nullptr,*frameMetric_=nullptr,
           *recognizedMetric_=nullptr,*unknownMetric_=nullptr,*timeMetric_=nullptr,
           *imageMetric_=nullptr,*transportMetric_=nullptr,*positionLabel_=nullptr,
           *engineeringSummary_=nullptr,*fieldDetail_=nullptr,*attitudeInfo_=nullptr,
           *imageInfo_=nullptr,*diagnosticsSummary_=nullptr;
    QComboBox *satelliteSelect_=nullptr,*speedSelect_=nullptr;
    QLabel *tcpStatusLabel_=nullptr;
    QLineEdit *eventSearch_=nullptr,*engineeringSearch_=nullptr;
    QCheckBox *recognizedOnly_=nullptr;
    QTableWidget *eventTable_=nullptr,*engineeringTable_=nullptr,
                 *frameTable_=nullptr,*packetTable_=nullptr,*imageTable_=nullptr,*rawFrameTable_=nullptr;
    QTreeWidget *fieldTree_=nullptr;
    QChartView *engineeringChart_=nullptr,*attitudeChart_=nullptr;
    AttitudeWidget *attitudeWidget_=nullptr;
    ImagePreview *imagePreview_=nullptr;
    QPlainTextEdit *rawHex_=nullptr,*debugText_=nullptr,*rawFrameHex_=nullptr;
    ReplayTimelineWidget *timeline_=nullptr;
    QTimer *timer_=nullptr;
    QTabWidget *pages_=nullptr,*rawPage_=nullptr;
    int diagnosticTab_=-1;
    QGroupBox *powerCard_=nullptr,*attitudeCard_=nullptr,*thermalCard_=nullptr,*payloadCard_=nullptr;
    QHash<QGroupBox*,QTableWidget*> overviewTables_;
    QString fieldTreeSatellite_;
    QHash<QString,QTreeWidgetItem*> fieldTreeFieldItems_;
    QHash<QString,QTableWidget*> componentTables_;
    QHash<QString,QStringList> componentPrefixes_,componentKeywords_;
    QLabel *quaternion_[4]{};
    QLabel *angularVelocity_[3]{};
    QLabel *normLabel_=nullptr;
    QSpinBox *imageApid_=nullptr;
    QComboBox *imageSatelliteSelect_=nullptr;
    QProgressBar *imageCoverage_=nullptr;
    QLabel *imageCoverageNote_=nullptr;
};
