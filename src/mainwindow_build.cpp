#include "mainwindow.h"
#include "mainwindow_widgets.h"
#include <QAction>
#include <QAbstractButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QApplication>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QResource>
#include <QDir>
#include <QDesktopServices>
#include <QCoreApplication>
#include <QUrl>
#include <QFont>
#include <QScreen>
#include <QFormLayout>
#include <QFrame>
#include <functional>
#include <algorithm>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

static void registerOrbitTlmI18nResources(){
    Q_INIT_RESOURCE(orbittlm_i18n);
    Q_INIT_RESOURCE(orbittlm_qt_i18n);
}

namespace {
QLabel *subtitle(const QString &text,QWidget *parent=nullptr){
    auto *label=new QLabel(text,parent);
    label->setObjectName("subtitle");
    label->setWordWrap(true);
    return label;
}
QWidget *metricCard(const QString &label,QLabel *&number){
    auto *panel=new QFrame;
    panel->setObjectName("metricCard");
    auto *layout=new QVBoxLayout(panel);
    layout->setSpacing(4);layout->setContentsMargins(14,10,14,10);
    layout->addWidget(subtitle(label));
    number=new QLabel(QStringLiteral("—"));
    number->setObjectName("metricNumber");
    number->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(number);
    return panel;
}
QGroupBox *statusCard(const QString &title){
    auto *card=new QGroupBox(title);
    card->setObjectName("statusCard");
    auto *layout=new QVBoxLayout(card);
    layout->setContentsMargins(4,13,4,4);
    layout->setSpacing(0);
    auto *table=new QTableWidget(0,4,card);
    table->setObjectName("statusTable");
    table->setHorizontalHeaderLabels({QStringLiteral("参数"),QStringLiteral("当前值"),
                                      QStringLiteral("参数"),QStringLiteral("当前值")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(16);
    table->setShowGrid(true);
    table->setWordWrap(false);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    layout->addWidget(table,1);
    return card;
}
void setupTable(QTableWidget *table){
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->setShowGrid(false);
    table->setAlternatingRowColors(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setWordWrap(false);
}
}
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent){
    setWindowTitle(uiText(QStringLiteral("OrbitTLM · 卫星遥测回放工作台")));
    setMinimumSize(720,480);
    const QRect available=QGuiApplication::primaryScreen()?QGuiApplication::primaryScreen()->availableGeometry():QRect(0,0,1280,800);
    resize(qBound(720,available.width()-80,1080),qBound(480,available.height()-100,640));
    live_=new orbit::LiveTcp(this);
    loadApplicationSettings();
    // Load translation data before building widgets so dynamically populated
    // catalog tabs can choose their proper English labels on first creation.
    loadLanguageFile();
    applyStyles();
    buildToolbar();
    buildWorkspace();
    buildMenus();
    applyLanguage();
    timer_=new QTimer(this);timer_->setInterval(500);
    connect(timer_,&QTimer::timeout,this,[this]{
        if(business_.isEmpty()){timer_->stop();}
        else seekVisible(1);
        if(timer_&&!timer_->isActive()&&playPauseButton_){
            QSignalBlocker blocker(playPauseButton_);
            playPauseButton_->setChecked(false);
            playPauseButton_->setText(uiText(QStringLiteral("播放")));
        }
    });
    connect(live_,&orbit::LiveTcp::status,this,[this](const QString &message){
        const QString visibleStatus=message==QStringLiteral("Connected")?uiText(QStringLiteral("TCP：已连接")):
            (message==QStringLiteral("Disconnected")?uiText(QStringLiteral("TCP：未连接")):uiText(message));
        statusBar()->showMessage(visibleStatus,7500);
        if(message==QStringLiteral("Connected")){
            tcpConnected_=true;tcpConnecting_=false;
            if(tcpToggleButton_)tcpToggleButton_->setText(uiText(QStringLiteral("断开 TCP")));
            if(tcpStatusLabel_){tcpStatusLabel_->setText(uiText(QStringLiteral("TCP：已连接")));
                tcpStatusLabel_->setStyleSheet(QStringLiteral("color:#15803d;font-weight:600;"));}
        }else if(message.startsWith(QStringLiteral("正在连接"))){
            tcpConnected_=false;tcpConnecting_=true;
            if(tcpToggleButton_)tcpToggleButton_->setText(uiText(QStringLiteral("取消连接")));
            if(tcpStatusLabel_){tcpStatusLabel_->setText(uiText(QStringLiteral("TCP：连接中")));
                tcpStatusLabel_->setStyleSheet(QStringLiteral("color:#b45309;font-weight:600;"));}
        }else if(message.startsWith(QStringLiteral("连接失败"))){
            tcpConnected_=false;tcpConnecting_=false;
            if(tcpToggleButton_)tcpToggleButton_->setText(uiText(QStringLiteral("连接 TCP")));
            if(tcpStatusLabel_){tcpStatusLabel_->setText(uiText(QStringLiteral("TCP：连接失败")));
                tcpStatusLabel_->setToolTip(message);
                tcpStatusLabel_->setStyleSheet(QStringLiteral("color:#b91c1c;font-weight:600;"));}
        }else if(message==QStringLiteral("Disconnected")){
            tcpConnected_=false;tcpConnecting_=false;
            if(tcpToggleButton_)tcpToggleButton_->setText(uiText(QStringLiteral("连接 TCP")));
            if(tcpStatusLabel_){
                tcpStatusLabel_->setText(uiText(QStringLiteral("TCP：未连接")));
                tcpStatusLabel_->setStyleSheet(QString());
                tcpStatusLabel_->setToolTip(QString());
            }
        }
    });
    connect(live_,&orbit::LiveTcp::received,this,[this](const orbit::Frame &frame){
        bool inserted=false;
        QString storageError;
        const bool stored=satelliteStore_.append(frame,&inserted,&storageError);
        if(!stored){
            statusBar()->showMessage(uiText(QStringLiteral("SQLite 写入失败：%1")).arg(storageError),10000);
        }else if(!inserted){
            statusBar()->showMessage(uiText(QStringLiteral("重复帧已去重：%1")).arg(frame.satellite),3000);
            return;
        }
        for(const auto &existing:frames_){
            if(existing.satellite==frame.satellite&&existing.hash==frame.hash)return;
        }
        frames_.push_back(frame);
        std::stable_sort(frames_.begin(),frames_.end(),[](const orbit::Frame &left,const orbit::Frame &right){
            if(left.captureLocal.isValid()!=right.captureLocal.isValid())return left.captureLocal.isValid();
            if(left.captureLocal.isValid()&&right.captureLocal.isValid()&&left.captureLocal!=right.captureLocal)
                return left.captureLocal<right.captureLocal;
            if(left.satellite!=right.satellite)return left.satellite<right.satellite;
            return left.hash<right.hash;
        });
        rebuildSession(true);
        if(stored){
            statusBar()->showMessage(uiText(QStringLiteral("已保存至 %1.db（按时间排序，重复帧自动忽略）")).arg(frame.satellite),5000);
        }
    });
    const QString dataDir=orbit::applicationDataDirectory();
    QDir().mkpath(dataDir);
    const QString dbFile=QDir(dataDir).filePath(QStringLiteral("orbittlm.sqlite"));
    const QStringList legacyDbFiles={
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/orbittlm.sqlite"),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/orbittlm.sqlite"))};
    for(const QString &legacyDbFile:legacyDbFiles)
        if(!QFileInfo::exists(dbFile)&&QFileInfo::exists(legacyDbFile))QFile::copy(legacyDbFile,dbFile);
    QString error;
    if(!db_.open(dbFile,&error))
        statusBar()->showMessage(uiText(QStringLiteral("SQLite 数据库无法打开："))+error,18000);
    error.clear();
    if(catalog_.loadDirectory(QApplication::applicationDirPath()+"/protocols/asrtu1",&error)){
        catalogReady_=true;
    }else{
        statusBar()->showMessage(uiText(QStringLiteral("缺少 ASRTU-1 遥测字段库："))+error,18000);
    }
    updatePaths();
    updateMetrics();
    updateWebHistory();
    showEvent(-1);
    setDiagnosticsVisible(false);
    // Attempt the saved TCP endpoint once on every application start; never show a modal prompt.
    QTimer::singleShot(0,this,[this]{
        if(live_&&!tcpHost_.trimmed().isEmpty()&&tcpPort_>=1&&tcpPort_<=65535)
            live_->start(tcpHost_.trimmed(),quint16(tcpPort_),QStringLiteral("AUTO"));
    });
}
void MainWindow::applyStyles(){
    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background:#f3f6fb; color:#182940; }
        QWidget { color:#1f3045; font-size:9pt; }
        QMenuBar { background:#ffffff; padding:3px 5px; border-bottom:1px solid #e0e7f0; }
        QMenuBar::item:selected, QMenu::item:selected {background:#e7f1fd;}
        QMenu { background:#ffffff; border:1px solid #dbe3ee; }
        QToolBar { background:#ffffff; border:0; border-bottom:1px solid #e0e7f0;
                   spacing:4px; padding:3px 6px; }
        QToolButton, QPushButton {padding:3px 7px; border:1px solid #d6e0ec;
                  border-radius:7px; background:#ffffff; font-weight:500;}
        QToolButton:hover,QPushButton:hover {background:#edf5ff; border-color:#92bcf0;}
        QPushButton#primaryButton {background:#256cb4; border-color:#256cb4; color:white;}
        QPushButton#primaryButton:hover {background:#1f5b98;}
        QComboBox,QSpinBox,QLineEdit {background:white; border:1px solid #d4deeb;
                  border-radius:6px; padding:2px 5px; min-height:15px;}
        QLineEdit:focus,QComboBox:focus {border:1px solid #3c87d0;}
        QFrame#metricCard, QFrame#heroCard, QFrame#panel {
                  background:#ffffff; border:1px solid #e2e8f1; border-radius:10px;}
        QLabel#subtitle {color:#6d7f95; font-size:12px;}
        QLabel#metricNumber {font-size:18px; font-weight:700; color:#1d3b5e;}
        QLabel#currentHeading {font-size:16px; font-weight:700; color:#152d4c;}
        QLabel#detailHeading {font-size:13px; font-weight:700; color:#193958;}
        QLabel#note {color:#6b7f96; font-size:12px;}
        QTabWidget::pane {background:white; border:1px solid #e1e8f1;
                         border-radius:8px;}
        QTabBar::tab {background:#edf2f8; border:0; border-radius:4px;
                      margin-right:2px; padding:3px 6px; min-width:42px;}
        QTabBar::tab:selected {background:#dcecff; color:#18558f; font-weight:700;}
        QTabBar::tab:hover {background:#e3effb;}
        QTableWidget,QTreeWidget,QPlainTextEdit {background:white;
                     alternate-background-color:#f9fbff; border:1px solid #e1e8f1;
                     border-radius:6px; gridline-color:#edf2f6;}
        QHeaderView::section {background:#f5f8fc; border:none;
                              border-bottom:1px solid #e1e8f1;
                              color:#56718b; font-weight:600; padding:2px 3px;}
        QTableWidget::item {padding:1px 3px; border-bottom:1px solid #eff2f7;}
        QTableWidget::item:selected,QTreeWidget::item:selected {background:#e2f1ff;color:#183c64;}
        QGroupBox#statusCard {background:#ffffff; border:1px solid #e1e8f1;
                             border-radius:9px; margin-top:12px; font-weight:700;}
        QGroupBox#statusCard::title {subcontrol-origin:margin; left:14px;
                                    padding:0 6px;color:#245b8c;}
        QLabel#statusContent {font-weight:400; color:#38516c; line-height:150%; padding:2px;}
        QScrollArea#statusScroll {background:transparent;}
        QLabel#attitudeModeLabel {font-size:11px; font-weight:700; color:#174f83;
                                  background:#eaf4ff; border:1px solid #cfe4fb;
                                  border-radius:6px; padding:7px 10px;}
        QSplitter::handle {background:#edf1f7;}
        QStatusBar {background:#ffffff; color:#61768d; border-top:1px solid #e0e7f0;}
        QSlider::groove:horizontal {height:6px; background:#d9e4f0; border-radius:3px;}
        QSlider::handle:horizontal {width:13px;background:#337fc3;
                                  border:2px solid #ffffff; margin:-5px 0;border-radius:7px;}
    )"));
}
void MainWindow::buildToolbar(){
    auto *bar=addToolBar(QStringLiteral("遥测工作台"));
    bar->setMovable(false);
    auto *log=bar->addAction(QStringLiteral("导入 LOG"));
    connect(log,&QAction::triggered,this,[this]{
        const QString p=QFileDialog::getOpenFileName(this,uiText(QStringLiteral("选择解码日志")),{},
                                                     uiText(QStringLiteral("日志 (*.log *.txt);;全部文件 (*)")));
        if(!p.isEmpty()){logPath_=p;updatePaths();}
    });
    auto *bin=bar->addAction(QStringLiteral("导入 BIN"));
    connect(bin,&QAction::triggered,this,[this]{
        const QString p=QFileDialog::getOpenFileName(this,uiText(QStringLiteral("选择 223 字节帧文件")),{},
                                                     uiText(QStringLiteral("数据帧 (*.bin);;全部文件 (*)")));
        if(!p.isEmpty()){binPath_=p;updatePaths();}
    });
    bar->addSeparator();
    auto *satelliteLabel=new QLabel(QStringLiteral("卫星  "),bar);
    bar->addWidget(satelliteLabel);
    satelliteSelect_=new QComboBox(bar);
    satelliteSelect_->addItem(QStringLiteral("自动识别"),QStringLiteral("AUTO"));
    satelliteSelect_->addItem(QStringLiteral("阿斯图系列 · ASRTU-1"),QStringLiteral("ASRTU-1"));
    satelliteSelect_->addItem(QStringLiteral("BY70-4（BY04）"),QStringLiteral("BY04"));
    bar->addWidget(satelliteSelect_);
    auto *import=bar->addAction(QStringLiteral("开始导入"));
    connect(import,&QAction::triggered,this,[this]{importSession();});
    bar->addSeparator();
    tcpToggleButton_=new QPushButton(QStringLiteral("连接 TCP"),bar);
    tcpToggleButton_->setObjectName(QStringLiteral("tcpToggleButton"));
    bar->addWidget(tcpToggleButton_);
    tcpStatusLabel_=new QLabel(QStringLiteral("TCP：未连接"),bar);
    tcpStatusLabel_->setObjectName(QStringLiteral("tcpStatusLabel"));
    tcpStatusLabel_->setMinimumWidth(112);
    bar->addWidget(tcpStatusLabel_);
    connect(tcpToggleButton_,&QPushButton::clicked,this,[this]{
        if(tcpConnected_||tcpConnecting_){
            live_->stop();
            tcpConnected_=false;tcpConnecting_=false;
            tcpToggleButton_->setText(uiText(QStringLiteral("连接 TCP")));
        }else{
            if(tcpHost_.trimmed().isEmpty()||tcpPort_<1||tcpPort_>65535){configureTcpServer();return;}
            live_->start(tcpHost_.trimmed(),quint16(tcpPort_),QStringLiteral("AUTO"));
        }
    });
}
void MainWindow::buildMenus(){
    auto *file=menuBar()->addMenu(QStringLiteral("文件(&F)"));
    file->addAction(QStringLiteral("打开 HTML 遥测历史"),[this]{
        updateWebHistory();
        const QString path=QDir(satelliteStore_.rootDirectory()).filePath(QStringLiteral("index.html"));
        if(!QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            QMessageBox::warning(this,uiText(QStringLiteral("打开 HTML 历史失败")),path);
    });
    file->addSeparator();
    file->addAction(QStringLiteral("统一导出…"),[this]{showExportDialog();});
    auto *tools=menuBar()->addMenu(QStringLiteral("工具(&T)"));
    tools->addAction(QStringLiteral("TCP 服务器设置…"),[this]{configureTcpServer();});
    languageMenu_=tools->addMenu(QStringLiteral("界面语言"));
    languageMenu_->setObjectName(QStringLiteral("languageMenu"));
    // Locale names are metadata in the embedded dictionaries; language codes
    // remain stable identifiers in source code and settings.
    const QStringList supportedLanguages={QStringLiteral("zh-CN"),QStringLiteral("en-US"),QStringLiteral("ja-JP")};
    for(const QString &code:supportedLanguages){
        QString displayName=code;
        QFile localeFile(QStringLiteral(":/i18n/")+code+QStringLiteral(".json"));
        if(localeFile.open(QIODevice::ReadOnly)){
            const QJsonObject localeRoot=QJsonDocument::fromJson(localeFile.readAll()).object();
            displayName=localeRoot.value(QStringLiteral("displayName")).toString(code);
        }
        auto *languageAction=languageMenu_->addAction(displayName);
        languageAction->setObjectName(QStringLiteral("languageAction_")+code);
        languageAction->setCheckable(true);
        languageAction->setData(code);
        languageAction->setChecked(code==languageCode_);
        connect(languageAction,&QAction::triggered,this,[this,code]{
            languageCode_=code;
            applyLanguage();
            updatePaths();
            // Translation pass uses the retained source text for live summaries and
            // updates all existing table/tree cells in-place. Avoid rebuilding frame
            // tables, rescanning the replay history, recreating charts, or decoding an
            // SSDV JPEG as a side effect of changing the language.
            if(positionLabel_)
                positionLabel_->setText(currentIndex_<0?QStringLiteral("0 / %1").arg(business_.size()):
                    QStringLiteral("%1 / %2").arg(currentIndex_+1).arg(business_.size()));
            if(rawHex_){
                // Raw HEX is a plain-text document rather than a table cell, so only
                // reformat the currently selected packet; this does not walk history.
                updateRawPacket(currentIndex_);
            }
            if(tcpStatusLabel_){
                const QString status=tcpConnected_?QStringLiteral("TCP：已连接"):
                    (tcpConnecting_?QStringLiteral("TCP：连接中"):QStringLiteral("TCP：未连接"));
                tcpStatusLabel_->setText(uiText(status));
            }
            if(tcpToggleButton_)
                tcpToggleButton_->setText(uiText(tcpConnected_?QStringLiteral("断开 TCP"):
                    (tcpConnecting_?QStringLiteral("取消连接"):QStringLiteral("连接 TCP"))));
            if(playPauseButton_)
                playPauseButton_->setText(uiText(playPauseButton_->isChecked()?QStringLiteral("暂停"):QStringLiteral("播放")));
            if(replayCollapseButton_)
                replayCollapseButton_->setText(uiText(replayListCollapsed_?QStringLiteral("展开时间轴"):QStringLiteral("收起时间轴")));
            // Chart objects are not QWidget descendants, so update their stored
            // source titles directly without rebuilding their data series.
            if(engineeringChart_&&engineeringChart_->chart()){
                auto *chart=engineeringChart_->chart();
                const QString title=chart->property("orbitSourceTitle").toString();
                if(!title.isEmpty())chart->setTitle(uiText(title));
            }
            if(attitudeChart_&&attitudeChart_->chart()){
                auto *chart=attitudeChart_->chart();
                const QString title=chart->property("orbitSourceTitle").toString();
                if(!title.isEmpty())chart->setTitle(uiText(title));
            }
            if(imageCoverage_){
                const QString sourceFormat=imageCoverage_->property("orbitSourceFormat").toString();
                if(!sourceFormat.isEmpty())imageCoverage_->setFormat(uiText(sourceFormat));
            }
            saveApplicationSettings();
            updateWebHistory();
        });
    }
    tools->addAction(QStringLiteral("载入自定义字段规则 JSON"),[this]{
        const QString p=QFileDialog::getOpenFileName(this,uiText(QStringLiteral("选择字段规则")),{},
                                                       uiText(QStringLiteral("JSON 文件 (*.json)")));
        if(p.isEmpty())return;
        orbit::Schema candidate;QString error;
        if(!candidate.load(p,&error)){QMessageBox::warning(this,uiText(QStringLiteral("字段规则错误")),error);return;}
        if(candidate.satellite()!=resolvedSatellite()){
            QMessageBox::warning(this,uiText(QStringLiteral("卫星不匹配")),uiText(QStringLiteral("该规则属于另一颗卫星")));return;
        }
        customSchema_=candidate;hasCustomSchema_=true;
        for(auto &f:frames_)customSchema_.apply(f);
        inspectFrame(0);
        statusBar()->showMessage(uiText(QStringLiteral("已载入独立字段规则，尚需验证字段物理含义")),9000);
    });
    auto *debug=tools->addAction(QStringLiteral("显示调试信息"));
    debug->setCheckable(true);
    connect(debug,&QAction::toggled,this,[this](bool active){setDiagnosticsVisible(active);});
}
void MainWindow::setReplayListCollapsed(bool collapsed){
    if(!replayPanel_||replayListCollapsed_==collapsed)return;
    replayListCollapsed_=collapsed;
    if(timeline_)timeline_->setVisible(!collapsed);
    replayPanel_->setMaximumHeight(collapsed?100:QWIDGETSIZE_MAX);
    replayPanel_->setMinimumHeight(collapsed?0:145);
    if(replayCollapseButton_)
        replayCollapseButton_->setText(uiText(collapsed?QStringLiteral("展开时间轴"):QStringLiteral("收起时间轴")));
    if(workspaceSplitter_){
        const int total=qMax(220,workspaceSplitter_->height());
        const int panelHeight=collapsed?98:178;
        workspaceSplitter_->setSizes({qMax(1,total-panelHeight),panelHeight});
    }
}

void MainWindow::loadApplicationSettings(){
    const QString dataDirectory=orbit::applicationDataDirectory();
    QDir().mkpath(dataDirectory);
    const QString settingsPath=QDir(dataDirectory).filePath(QStringLiteral("app-settings.json"));
    const QString legacySettings=QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/app-settings.json"));
    if(!QFileInfo::exists(settingsPath)&&QFileInfo::exists(legacySettings))QFile::copy(legacySettings,settingsPath);
    QFile settingsFile(settingsPath);
    if(!settingsFile.open(QIODevice::ReadOnly))return;
    const QJsonObject settings=QJsonDocument::fromJson(settingsFile.readAll()).object();
    tcpHost_=settings.value(QStringLiteral("tcpHost")).toString(tcpHost_).trimmed();
    tcpPort_=settings.value(QStringLiteral("tcpPort")).toInt(tcpPort_);
    languageCode_=settings.value(QStringLiteral("language")).toString(languageCode_);
    if(tcpHost_.isEmpty())tcpHost_=QStringLiteral("127.0.0.1");
    if(tcpPort_<1||tcpPort_>65535)tcpPort_=9985;
}

void MainWindow::saveApplicationSettings(){
    const QString dataDirectory=orbit::applicationDataDirectory();
    if(!QDir().mkpath(dataDirectory)){
        statusBar()->showMessage(uiText(QStringLiteral("无法创建应用配置目录：%1")).arg(dataDirectory),9000);return;
    }
    QJsonObject settings;
    settings.insert(QStringLiteral("tcpHost"),tcpHost_.trimmed());
    settings.insert(QStringLiteral("tcpPort"),tcpPort_);
    settings.insert(QStringLiteral("language"),languageCode_);
    QSaveFile file(QDir(dataDirectory).filePath(QStringLiteral("app-settings.json")));
    if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(settings).toJson(QJsonDocument::Indented))<0||!file.commit())
        statusBar()->showMessage(uiText(QStringLiteral("保存应用配置失败：%1")).arg(file.errorString()),9000);
}

void MainWindow::configureTcpServer(){
    QDialog dialog(this);
    dialog.setWindowTitle(uiText(QStringLiteral("TCP 服务器设置")));
    auto *layout=new QFormLayout(&dialog);
    auto *hostEdit=new QLineEdit(tcpHost_,&dialog);
    hostEdit->setPlaceholderText(uiText(QStringLiteral("例如 192.168.1.100")));
    auto *portEdit=new QSpinBox(&dialog);
    portEdit->setRange(1,65535);
    portEdit->setValue(tcpPort_);
    layout->addRow(uiText(QStringLiteral("服务器地址")),hostEdit);
    layout->addRow(uiText(QStringLiteral("端口")),portEdit);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(uiText(QStringLiteral("确定")));
    buttons->button(QDialogButtonBox::Cancel)->setText(uiText(QStringLiteral("取消")));
    layout->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    if(hostEdit->text().trimmed().isEmpty()){
        QMessageBox::warning(this,uiText(QStringLiteral("TCP 服务器设置")),uiText(QStringLiteral("服务器地址不能为空。")));return;
    }
    tcpHost_=hostEdit->text().trimmed();
    tcpPort_=portEdit->value();
    saveApplicationSettings();
    live_->start(tcpHost_,quint16(tcpPort_),QStringLiteral("AUTO"));
}

bool MainWindow::loadLanguageFile(){
    static const bool resourcesRegistered=[](){
        registerOrbitTlmI18nResources();
        return true;
    }();
    Q_UNUSED(resourcesRegistered);
    const QString code=languageCode_.isEmpty()?QStringLiteral("zh-CN"):languageCode_;
    QByteArray contents;
    const QString resourcePath=QStringLiteral(":/i18n/")+code+QStringLiteral(".json");
    QFile file(resourcePath);
    if(file.open(QIODevice::ReadOnly))contents=file.readAll();
    if(contents.isEmpty()&&code!=QStringLiteral("zh-CN")){
        languageCode_=QStringLiteral("zh-CN");
        QFile fallback(QStringLiteral(":/i18n/zh-CN.json"));
        if(fallback.open(QIODevice::ReadOnly))contents=fallback.readAll();
    }
    if(contents.isEmpty())return false;
    const QJsonObject root=QJsonDocument::fromJson(contents).object();
    const QJsonObject map=root.value(QStringLiteral("translations")).toObject();
    if(map.isEmpty())return false;
    QCoreApplication::removeTranslator(&qtTranslator_);
    if(languageCode_==QStringLiteral("ja-JP")&&
       qtTranslator_.load(QStringLiteral(":/qt-i18n/qtbase_ja.qm")))
        QCoreApplication::installTranslator(&qtTranslator_);
    translations_=map;
    webTranslations_=root.value(QStringLiteral("webTranslations")).toObject();
    uiTextCache_.clear();
    // Build the substring-fallback replacement order only once per language load.
    // Previously uiText(), orbitWidgetText() and localizeItemText() each sorted
    // the whole dictionary for every visible and hidden field, making English
    // switching disproportionately slow on telemetry catalogs.
    translationKeysByLength_.clear();
    const QStringList allKeys=translations_.keys();
    translationKeysByLength_.reserve(allKeys.size());
    for(const QString &key:allKeys){
        if(!key.isEmpty() && orbitContainsCjk(key) && translations_.value(key).toString()!=key)
            translationKeysByLength_.push_back(key);
    }
    std::sort(translationKeysByLength_.begin(),translationKeysByLength_.end(),
              [](const QString &a,const QString &b){return a.size()>b.size();});
    if(QCoreApplication::instance()){
        QCoreApplication::instance()->setProperty("orbitTlmTranslations",QVariant::fromValue(map));
        QCoreApplication::instance()->setProperty("orbitTlmTranslationKeys",translationKeysByLength_);
        QCoreApplication::instance()->setProperty("orbitTlmLanguage",languageCode_);
    }
    return true;
}

void MainWindow::applyLanguage(){
    if(!loadLanguageFile())return;
    auto translated=[this](QObject *object,const QString &current,const char *propertyName)->QString{
        const QVariant saved=object->property(propertyName);
        QString source;
        if(saved.isValid())source=saved.toString();
        else{
            source=current;
            object->setProperty(propertyName,source);
        }
        return uiText(source);
    };
    setWindowTitle(translated(this,windowTitle(),"orbitSourceWindowTitle"));
    const auto dynamicLabel=[this](const QLabel *label){
        return label==currentMeta_||label==integrityLabel_||label==frameMetric_||
            label==recognizedMetric_||label==unknownMetric_||
            label==imageMetric_||label==positionLabel_||label==tcpStatusLabel_;
    };
    for(QWidget *widget:findChildren<QWidget*>()){
        if(auto *label=qobject_cast<QLabel*>(widget)){
            if(!dynamicLabel(label)){
                const QString value=translated(label,label->text(),"orbitSourceText");
                if(label->text()!=value)label->setText(value);
            }
        }
        if(auto *button=qobject_cast<QAbstractButton*>(widget)){
            const QString value=translated(button,button->text(),"orbitSourceText");
            if(button->text()!=value)button->setText(value);
        }
        if(auto *group=qobject_cast<QGroupBox*>(widget)){
            const QString value=translated(group,group->title(),"orbitSourceText");
            if(group->title()!=value)group->setTitle(value);
        }
        if(auto *line=qobject_cast<QLineEdit*>(widget)){
            const QString value=translated(line,line->placeholderText(),"orbitSourcePlaceholder");
            if(line->placeholderText()!=value)line->setPlaceholderText(value);
        }
        if(auto *plain=qobject_cast<QPlainTextEdit*>(widget)){
            const QString value=translated(plain,plain->placeholderText(),"orbitSourcePlaceholder");
            if(plain->placeholderText()!=value)plain->setPlaceholderText(value);
        }
        if(!widget->toolTip().isEmpty()){
            const QString tipSource=widget->property("orbitSourceToolTip").isValid()
                ?widget->property("orbitSourceToolTip").toString():widget->toolTip();
            if(!widget->property("orbitSourceToolTip").isValid())widget->setProperty("orbitSourceToolTip",tipSource);
            const QString tip=uiText(tipSource);
            if(widget->toolTip()!=tip)widget->setToolTip(tip);
        }
        if(auto *combo=qobject_cast<QComboBox*>(widget)){
            constexpr int role=Qt::UserRole+121;
            for(int i=0;i<combo->count();++i){
                QVariant saved=combo->itemData(i,role);
                QString source;
                if(saved.isValid())source=saved.toString();
                else{source=combo->itemText(i);combo->setItemData(i,source,role);}
                const QString value=uiText(source);
                if(combo->itemText(i)!=value)combo->setItemText(i,value);
            }
        }
        if(auto *tabs=qobject_cast<QTabWidget*>(widget)){
            // Translate only the actual Qt tab widget pages (the top-level page
            // set and the raw-data sub-tabs). AutoWrapTabWidget keeps its own
            // original page labels and is translated in the dedicated pass
            // below; it must not be replaced by a nested QTabWidget wrapper.
            QStringList sources=tabs->property("orbitSourceTabTexts").toStringList();
            while(sources.size()<tabs->count())sources.push_back(tabs->tabText(sources.size()));
            if(sources.size()>tabs->count())sources=sources.mid(0,tabs->count());
            tabs->setProperty("orbitSourceTabTexts",sources);
            for(int i=0;i<tabs->count();++i){
                const QString value=uiText(sources[i]);
                if(tabs->tabText(i)!=value)tabs->setTabText(i,value);
            }
        }
        if(auto *menu=qobject_cast<QMenu*>(widget)){
            const QString source=translated(menu,menu->title(),"orbitSourceText");
            if(menu->menuAction())menu->menuAction()->setProperty("orbitSourceText",menu->property("orbitSourceText"));
            if(menu->title()!=source)menu->setTitle(source);
        }
        if(auto *toolbar=qobject_cast<QToolBar*>(widget))
            toolbar->setWindowTitle(translated(toolbar,toolbar->windowTitle(),"orbitSourceWindowTitle"));
        if(auto *table=qobject_cast<QTableWidget*>(widget)){
            const bool hadUpdates=table->updatesEnabled();
            table->setUpdatesEnabled(false);
            constexpr int headerRole=Qt::UserRole+122;
            constexpr int cellRole=Qt::UserRole+123;
            for(int i=0;i<table->columnCount();++i){
                if(auto *headerItem=table->horizontalHeaderItem(i)){
                    QVariant saved=headerItem->data(headerRole);QString source;
                    if(saved.isValid())source=saved.toString();else{source=headerItem->text();headerItem->setData(headerRole,source);}
                    const QString value=uiText(source);
                    if(headerItem->text()!=value)headerItem->setText(value);
                }
            }
            for(int row=0;row<table->rowCount();++row)for(int col=0;col<table->columnCount();++col){
                if(auto *cell=table->item(row,col)){
                    QVariant saved=cell->data(cellRole);QString source;
                    if(saved.isValid())source=saved.toString();else{source=cell->text();cell->setData(cellRole,source);}
                    const QString value=uiText(source);
                    if(cell->text()!=value)cell->setText(value);
                    const QVariant tipSource=cell->data(Qt::UserRole+124);
                    if(tipSource.isValid()){
                        const QString tip=uiText(tipSource.toString());
                        if(cell->toolTip()!=tip)cell->setToolTip(tip);
                    }
                }
            }
            table->setUpdatesEnabled(hadUpdates);
        }
        if(auto *tree=qobject_cast<QTreeWidget*>(widget)){
            const bool hadUpdates=tree->updatesEnabled();
            tree->setUpdatesEnabled(false);
            constexpr int role=Qt::UserRole+122;
            if(auto *header=tree->headerItem())for(int i=0;i<tree->columnCount();++i){
                QVariant saved=header->data(i,role);QString source;
                if(saved.isValid())source=saved.toString();else{source=header->text(i);header->setData(i,role,source);}
                const QString value=uiText(source);
                if(header->text(i)!=value)header->setText(i,value);
            }
            std::function<void(QTreeWidgetItem*)> translateNode=[&](QTreeWidgetItem *node){
                if(!node)return;
                for(int column=0;column<tree->columnCount();++column){
                    QVariant saved=node->data(column,Qt::UserRole+124);QString source;
                    if(saved.isValid())source=saved.toString();
                    else{source=node->text(column);node->setData(column,Qt::UserRole+124,source);}
                    const QString value=uiText(source);
                    if(node->text(column)!=value)node->setText(column,value);
                    const QVariant tooltipSource=node->data(column,Qt::UserRole+126);
                    if(tooltipSource.isValid()){
                        const QString tip=uiText(tooltipSource.toString());
                        if(node->toolTip(column)!=tip)node->setToolTip(column,tip);
                    }
                }
                for(int child=0;child<node->childCount();++child)translateNode(node->child(child));
            };
            for(int row=0;row<tree->topLevelItemCount();++row)translateNode(tree->topLevelItem(row));
            tree->setUpdatesEnabled(hadUpdates);
        }
        if(auto *wrapped=qobject_cast<AutoWrapTabWidget*>(widget)){
            QStringList translatedTabs;
            translatedTabs.reserve(wrapped->count());
            for(int i=0;i<wrapped->count();++i){
                QWidget *page=wrapped->widget(i);
                QString source=page?page->property("orbitSourceTabText").toString():QString();
                if(source.isEmpty())source=wrapped->tabText(i);
                translatedTabs.push_back(uiText(source));
                if(page){
                    QString tooltipSource=page->property("orbitSourceTabTooltip").toString();
                    if(tooltipSource.isEmpty())tooltipSource=source;
                    wrapped->setTabToolTip(i,tooltipSource);
                }
            }
            wrapped->setTabTexts(translatedTabs);
        }
    }
    for(QAction *action:findChildren<QAction*>()){
        const QString value=translated(action,action->text(),"orbitSourceText");
        if(action->text()!=value)action->setText(value);
    }
    if(languageMenu_)for(QAction *action:languageMenu_->actions()){
        QSignalBlocker blocker(action);
        action->setCheckable(true);
        action->setChecked(action->data().toString()==languageCode_);
    }
    updateGeometry();
    update();
}

void MainWindow::setTranslatedLabel(QLabel *label,const QString &source){
    if(!label)return;
    label->setProperty("orbitSourceText",source);
    label->setText(uiText(source));
}

QString MainWindow::uiText(const QString &source) const{
    const auto cached=uiTextCache_.constFind(source);
    if(cached!=uiTextCache_.constEnd())return cached.value();
    const auto exact=translations_.constFind(source);
    if(exact!=translations_.constEnd()){
        const QString result=exact.value().toString(source);
        if(source.size()<=256&&uiTextCache_.size()<20000)uiTextCache_.insert(source,result);
        return result;
    }
    if(languageCode_==QStringLiteral("zh-CN")||!orbitContainsCjk(source))return source;
    const QString translated=orbitTranslateSource(source,translations_,translationKeysByLength_);
    if(source.size()<=256&&uiTextCache_.size()<20000)uiTextCache_.insert(source,translated);
    return translated;
}

void MainWindow::showExportDialog(){
    QDialog dialog(this);
    dialog.setWindowTitle(uiText(QStringLiteral("统一导出")));
    auto *layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(uiText(QStringLiteral("选择要导出的数据类型（可多选）")),&dialog));
    auto *grid=new QGridLayout;
    layout->addLayout(grid);
    auto makeCheck=[&](const QString &label,bool enabled,int row,int col){
        auto *box=new QCheckBox(uiText(label),&dialog);box->setEnabled(enabled);grid->addWidget(box,row,col);return box;
    };
    auto *frameCsv=makeCheck(QStringLiteral("传输帧 CSV"),!frames_.isEmpty(),0,0);
    auto *frameJson=makeCheck(QStringLiteral("传输帧 JSON"),!frames_.isEmpty(),0,1);
    auto *engineeringCsv=makeCheck(QStringLiteral("业务遥测 CSV"),!business_.isEmpty(),1,0);
    auto *engineeringJson=makeCheck(QStringLiteral("业务遥测 JSON"),!business_.isEmpty(),1,1);
    auto *quaternion=makeCheck(QStringLiteral("SAT-S3 四元数 CSV"),!business_.isEmpty()&&resolvedSatellite()!=QStringLiteral("BY04"),2,0);
    auto *candidateQuaternion=makeCheck(QStringLiteral("实验性候选四元数 CSV"),!frames_.isEmpty(),2,1);
    auto *kiss=makeCheck(QStringLiteral("KISS 应用包 JSON"),!packets_.isEmpty(),3,0);
    auto *ssdv=makeCheck(QStringLiteral("SSDV 候选分片"),!images_.isEmpty(),3,1);
    auto *jpeg=makeCheck(QStringLiteral("重建当前 SSDV 图像为 JPEG"),!images_.isEmpty(),4,0);
    auto *directoryRow=new QHBoxLayout;
#ifdef Q_OS_LINUX
    const QString exportRoot=orbit::applicationDataDirectory();
#else
    const QString exportRoot=QCoreApplication::applicationDirPath();
#endif
    auto *directory=new QLineEdit(QDir(exportRoot).filePath(QStringLiteral("exports")),&dialog);
    auto *browse=new QPushButton(uiText(QStringLiteral("浏览…")),&dialog);
    directoryRow->addWidget(directory,1);directoryRow->addWidget(browse);
    layout->addWidget(new QLabel(uiText(QStringLiteral("导出目录")),&dialog));layout->addLayout(directoryRow);
    connect(browse,&QPushButton::clicked,&dialog,[&]{
        const QString selected=QFileDialog::getExistingDirectory(&dialog,uiText(QStringLiteral("选择导出目录")),directory->text());
        if(!selected.isEmpty())directory->setText(selected);
    });
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(uiText(QStringLiteral("开始导出")));
    buttons->button(QDialogButtonBox::Cancel)->setText(uiText(QStringLiteral("取消")));
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    QList<QCheckBox*> selected;
    for(QCheckBox *box:{frameCsv,frameJson,engineeringCsv,engineeringJson,quaternion,candidateQuaternion,kiss,ssdv,jpeg})
        if(box->isEnabled()&&box->isChecked())selected.push_back(box);
    if(selected.isEmpty()){
        QMessageBox::information(this,uiText(QStringLiteral("统一导出")),uiText(QStringLiteral("请至少选择一种可导出的数据。")));return;
    }
    const QString enteredDirectory=directory->text().trimmed();
    if(enteredDirectory.isEmpty()){
        QMessageBox::warning(this,uiText(QStringLiteral("统一导出")),uiText(QStringLiteral("无法创建导出目录。")));return;
    }
    const QString outDir=QDir::cleanPath(enteredDirectory);
    if(!QDir().mkpath(outDir)){
        QMessageBox::warning(this,uiText(QStringLiteral("统一导出")),uiText(QStringLiteral("无法创建导出目录。")));return;
    }
    QStringList planned;
    if(frameCsv->isChecked())planned<<QStringLiteral("frames.csv");
    if(frameJson->isChecked())planned<<QStringLiteral("frames.json");
    if(engineeringCsv->isChecked())planned<<QStringLiteral("engineering.csv");
    if(engineeringJson->isChecked())planned<<QStringLiteral("engineering.json");
    if(quaternion->isChecked())planned<<QStringLiteral("SAT-S3-quaternions.csv");
    if(candidateQuaternion->isChecked())planned<<QStringLiteral("candidate-quaternion.csv");
    if(kiss->isChecked())planned<<QStringLiteral("kiss-packets.json");
    if(ssdv->isChecked())planned<<QStringLiteral("satellite.ssdv");
    if(jpeg->isChecked()){
        int imageId=selectedImageId();
        if(imageId<0&&!images_.isEmpty())imageId=images_.first().imageId;
        if(imageId>=0)planned<<QStringLiteral("SSDV_ID%1.jpg").arg(imageId);
    }
    QStringList existing;
    for(const QString &name:planned)if(QFileInfo::exists(QDir(outDir).filePath(name)))existing<<name;
    if(!existing.isEmpty()&&QMessageBox::question(this,uiText(QStringLiteral("确认覆盖")),
    uiText(QStringLiteral("以下文件已存在，是否覆盖？\n%1")).arg(existing.join(QStringLiteral("\n"))),
    QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    exportDirectoryOverride_=QDir(outDir).absolutePath();
    if(frameCsv->isChecked())exportFrames(true);
    if(frameJson->isChecked())exportFrames(false);
    if(engineeringCsv->isChecked())exportEngineering(true);
    if(engineeringJson->isChecked())exportEngineering(false);
    if(quaternion->isChecked())exportQuaternions(false);
    if(candidateQuaternion->isChecked())exportQuaternions(true);
    if(kiss->isChecked())exportKiss();
    if(ssdv->isChecked())exportSsdv();
    if(jpeg->isChecked())decodeSsdv();
    exportDirectoryOverride_.clear();
    statusBar()->showMessage(uiText(QStringLiteral("所选导出任务已执行：%1")).arg(QDir(outDir).absolutePath()),9000);
}

void MainWindow::buildWorkspace(){
    auto *root=new QWidget;
    setCentralWidget(root);
    auto *main=new QVBoxLayout(root);
    main->setContentsMargins(12,12,12,9);
    main->setSpacing(9);
    auto *fileBar=new QFrame;
    fileBar->setObjectName("panel");
    auto *fileLayout=new QHBoxLayout(fileBar);
    fileLayout->setContentsMargins(15,9,15,9);
    sourceLabel_=new QLabel;
    sourceLabel_->setWordWrap(true);
    fileLayout->addWidget(sourceLabel_,1);
    message_=new QLabel(QStringLiteral("离线会话 · 应用层 CRC 未校验"));
    message_->setObjectName("note");
    fileLayout->addWidget(message_);
    main->addWidget(fileBar);

    workspaceSplitter_=new QSplitter(Qt::Vertical);
    workspaceSplitter_->setChildrenCollapsible(false);
    // A splitter child index is only valid after that child has been added.
    // Configure the replay panel's collapsible state after adding both children below.

    pages_=new QTabWidget;
    pages_->setObjectName("mainPages");
    pages_->setMinimumHeight(250);
    buildOverview(pages_);
    buildEngineering(pages_);
    buildAttitude(pages_);
    buildImages(pages_);
    buildRaw(pages_);
    buildDiagnostics(pages_);
    workspaceSplitter_->addWidget(pages_);

    const auto syncRawView=[this](){
        if(!rawHex_||!rawPage_||!pages_)return;
        const bool active=pages_->currentWidget()==rawPage_&&rawPage_->currentWidget()==rawHex_;
        if(active)updateRawPacket(currentIndex_);
        else rawHex_->clear();
    };
    connect(pages_,&QTabWidget::currentChanged,this,[syncRawView](int){syncRawView();});
    if(rawPage_)connect(rawPage_,&QTabWidget::currentChanged,this,[syncRawView](int){syncRawView();});

    replayPanel_=new QFrame;
    replayPanel_->setObjectName("panel");
    replayPanel_->setMinimumHeight(0);
    auto *replayLayout=new QVBoxLayout(replayPanel_);
    replayLayout->setContentsMargins(8,4,8,4);
    replayLayout->setSpacing(3);
    auto *header=new QWidget(replayPanel_);
    auto *headerLayout=new QHBoxLayout(header);
    headerLayout->setContentsMargins(0,0,0,0);
    headerLayout->setSpacing(8);
    auto *headerTitle=new QLabel(QStringLiteral("数据回放时间轴"),header);
    headerTitle->setObjectName("detailHeading");
    headerLayout->addWidget(headerTitle);
    eventSearch_=new QLineEdit(header);
    eventSearch_->setObjectName(QStringLiteral("replaySearch"));
    eventSearch_->setPlaceholderText(uiText(QStringLiteral("搜索时间、分系统或数据包")));
    eventSearch_->setMaximumWidth(270);
    headerLayout->addWidget(eventSearch_);
    recognizedOnly_=new QCheckBox(QStringLiteral("仅显示已识别数据"),header);
    headerLayout->addWidget(recognizedOnly_);
    headerLayout->addStretch(1);
    replayCollapseButton_=new QPushButton(QStringLiteral("收起时间轴"),header);
    replayCollapseButton_->setObjectName(QStringLiteral("replayCollapseButton"));
    headerLayout->addWidget(replayCollapseButton_);
    replayLayout->addWidget(header);

    replayContent_=new QWidget(replayPanel_);
    auto *contentLayout=new QVBoxLayout(replayContent_);
    contentLayout->setContentsMargins(0,0,0,0);
    contentLayout->setSpacing(4);
    timeline_=new ReplayTimelineWidget(replayContent_);
    timeline_->setObjectName(QStringLiteral("replayKeyframeTimeline"));
    contentLayout->addWidget(timeline_);
    auto *transport=new QWidget(replayContent_);
    auto *transportLayout=new QHBoxLayout(transport);
    transportLayout->setContentsMargins(0,0,0,0);
    transportLayout->setSpacing(6);
    auto *back=new QPushButton(QStringLiteral("上一条"),transport);
    back->setObjectName(QStringLiteral("replayPreviousButton"));
    auto *next=new QPushButton(QStringLiteral("下一条"),transport);
    next->setObjectName(QStringLiteral("replayNextButton"));
    playPauseButton_=new QPushButton(QStringLiteral("播放"),transport);
    playPauseButton_->setObjectName(QStringLiteral("playPauseButton"));
    playPauseButton_->setCheckable(true);
    auto *zoomOut=new QPushButton(QStringLiteral("−"),transport);
    zoomOut->setObjectName(QStringLiteral("timelineZoomOut"));
    zoomOut->setToolTip(uiText(QStringLiteral("缩小时间轴范围，显示更多事件")));
    zoomOut->setFixedWidth(38);
    auto *zoomIn=new QPushButton(QStringLiteral("+"),transport);
    zoomIn->setObjectName(QStringLiteral("timelineZoomIn"));
    zoomIn->setToolTip(uiText(QStringLiteral("放大时间轴范围，查看事件细节")));
    zoomIn->setFixedWidth(38);
    transportLayout->addWidget(back);
    transportLayout->addWidget(playPauseButton_);
    transportLayout->addWidget(next);
    transportLayout->addSpacing(8);
    transportLayout->addWidget(zoomOut);
    transportLayout->addWidget(zoomIn);
    transportLayout->addStretch(1);
    speedSelect_=new QComboBox(transport);
    speedSelect_->addItems({"0.5×","1×","2×","4×","8×"});
    speedSelect_->setCurrentIndex(1);
    speedSelect_->setMaximumWidth(90);
    transportLayout->addWidget(speedSelect_);
    positionLabel_=new QLabel(QStringLiteral("0 / 0"),transport);
    positionLabel_->setMinimumWidth(70);
    positionLabel_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
    transportLayout->addWidget(positionLabel_);
    contentLayout->addWidget(transport);
    replayLayout->addWidget(replayContent_);

    // Kept as an internal event model so existing filter and previous/next logic
    // continue to operate on the same stable event indices. The visible surface is
    // now the bottom keyframe timeline rather than a left-hand table.
    eventTable_=new QTableWidget(0,4,replayPanel_);
    eventTable_->setObjectName("eventTimelineModel");
    eventTable_->setHorizontalHeaderLabels({QStringLiteral("时间"),QStringLiteral("数据包 / 事件"),
                                             QStringLiteral("状态"),QStringLiteral("来源")});
    setupTable(eventTable_);
    eventTable_->hide();

    workspaceSplitter_->addWidget(replayPanel_);
    workspaceSplitter_->setCollapsible(1,true);
    workspaceSplitter_->setStretchFactor(0,1);
    workspaceSplitter_->setStretchFactor(1,0);
    workspaceSplitter_->setSizes({670,178});
    main->addWidget(workspaceSplitter_,1);

    connect(eventTable_,&QTableWidget::itemSelectionChanged,this,[this]{
        const int row=eventTable_->currentRow();
        if(row>=0)selectEvent(row);
    });
    connect(eventSearch_,&QLineEdit::textChanged,this,[this]{applyEventFilter();});
    connect(recognizedOnly_,&QCheckBox::toggled,this,[this]{applyEventFilter();});
    connect(replayCollapseButton_,&QPushButton::clicked,this,[this]{setReplayListCollapsed(!replayListCollapsed_);});
    timeline_->onActivated=[this](int index){selectEvent(index);};
    connect(back,&QPushButton::clicked,this,[this]{seekVisible(-1);});
    connect(next,&QPushButton::clicked,this,[this]{seekVisible(1);});
    connect(playPauseButton_,&QPushButton::toggled,this,[this](bool playing){
        if(playing&&!business_.isEmpty()){
            playPauseButton_->setText(uiText(QStringLiteral("暂停")));
            timer_->start();
        }else{
            timer_->stop();
            playPauseButton_->setText(uiText(QStringLiteral("播放")));
        }
    });
    connect(zoomOut,&QPushButton::clicked,timeline_,[this]{timeline_->zoomOut();});
    connect(zoomIn,&QPushButton::clicked,timeline_,[this]{timeline_->zoomIn();});
    connect(speedSelect_,&QComboBox::currentIndexChanged,this,[this](int i){
        const int intervals[5]={1000,500,250,125,63};if(timer_)timer_->setInterval(intervals[i]);
    });
    statusBar()->showMessage(uiText(QStringLiteral("就绪 · 传输帧 FEC 证据与应用层 CRC 状态独立显示")));
}
void MainWindow::buildOverview(QTabWidget *tabs){
    auto *page=new QWidget;
    auto *layout=new QVBoxLayout(page);
    layout->setContentsMargins(5,5,5,5);
    layout->setSpacing(4);

    auto *hero=new QFrame;hero->setObjectName("heroCard");
    auto *heroLayout=new QHBoxLayout(hero);
    heroLayout->setContentsMargins(8,5,8,5);
    heroLayout->setSpacing(8);
    currentTitle_=new QLabel(QStringLiteral("当前数据包：未解析"));
    currentTitle_->setObjectName("currentHeading");
    currentTitle_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heroLayout->addWidget(currentTitle_,1);
    attitudeModeLabel_=new QLabel(QStringLiteral("UTC累计秒：未解析"));
    attitudeModeLabel_->setObjectName("attitudeModeLabel");
    attitudeModeLabel_->setMinimumWidth(220);
    attitudeModeLabel_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
    attitudeModeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    heroLayout->addWidget(attitudeModeLabel_);
    layout->addWidget(hero,0);

    auto *cards=new QWidget;
    auto *cardsLayout=new QGridLayout(cards);
    cardsLayout->setContentsMargins(0,0,0,0);
    cardsLayout->setHorizontalSpacing(4);cardsLayout->setVerticalSpacing(4);
    powerCard_=statusCard(QStringLiteral("总线电压与电流"));
    attitudeCard_=statusCard(QStringLiteral("姿态与轨道"));
    thermalCard_=statusCard(QStringLiteral("各总线与部件温度"));
    payloadCard_=statusCard(QStringLiteral("部件开关状态"));
    for(auto *card:{powerCard_,attitudeCard_,thermalCard_,payloadCard_})
        overviewTables_.insert(card,card->findChild<QTableWidget*>("statusTable"));
    cardsLayout->addWidget(powerCard_,0,0);
    cardsLayout->addWidget(attitudeCard_,0,1);
    cardsLayout->addWidget(thermalCard_,1,0);
    cardsLayout->addWidget(payloadCard_,1,1);
    cardsLayout->setRowStretch(0,1);cardsLayout->setRowStretch(1,1);
    cardsLayout->setColumnStretch(0,1);cardsLayout->setColumnStretch(1,1);
    layout->addWidget(cards,1);
    tabs->addTab(page,QStringLiteral("总览"));
}

void MainWindow::buildEngineering(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    engineeringSummary_=subtitle(QStringLiteral("选择一条已识别的业务包，查看分系统字段与工程量"));
    layout->addWidget(engineeringSummary_);
    engineeringSearch_=new QLineEdit;
    engineeringSearch_->setPlaceholderText(uiText(QStringLiteral("筛选当前分包字段名或编号")));
    layout->addWidget(engineeringSearch_);
    auto *split=new QSplitter(Qt::Horizontal);
    engineeringTable_=new QTableWidget(0,3);
    engineeringTable_->setHorizontalHeaderLabels({QStringLiteral("时间"),QStringLiteral("分系统"),QStringLiteral("字段数")});
    setupTable(engineeringTable_);
    engineeringTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    engineeringTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    engineeringTable_->verticalHeader()->setDefaultSectionSize(22);
    split->addWidget(engineeringTable_);
    auto *right=new QWidget;auto *rightLayout=new QVBoxLayout(right);
    rightLayout->setContentsMargins(5,2,5,2);
    fieldTree_=new QTreeWidget;
    fieldTree_->setHeaderLabels({QStringLiteral("参数"),QStringLiteral("数值"),QStringLiteral("单位"),QStringLiteral("解析状态")});
    fieldTree_->setColumnHidden(3,true);
    fieldTree_->setColumnWidth(0,245);
    fieldTree_->setColumnWidth(1,132);
    fieldTree_->setRootIsDecorated(true);
    rightLayout->addWidget(fieldTree_,3);
    fieldDetail_=subtitle(QStringLiteral("选择参数查看原始 HEX、字段位置与换算公式"));
    fieldDetail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(fieldDetail_);
    engineeringChart_=new QChartView(new QChart);
    engineeringChart_->setRenderHint(QPainter::Antialiasing);
    engineeringChart_->setMinimumHeight(120);
    rightLayout->addWidget(engineeringChart_,2);
    split->addWidget(right);
    split->setStretchFactor(1,1);split->setSizes({230,700});
    layout->addWidget(split,1);
    connect(engineeringTable_,&QTableWidget::itemSelectionChanged,this,[this]{
        const auto *item=engineeringTable_->item(engineeringTable_->currentRow(),0);
        if(item){
            const int idx=item->data(Qt::UserRole).toInt();
            if(idx>=0)selectEvent(idx);
        }
    });
    connect(fieldTree_,&QTreeWidget::itemSelectionChanged,this,[this]{
        const auto chosen=fieldTree_->selectedItems();
        if(chosen.isEmpty())return;
        const auto *item=chosen.first();
        if(item->data(0,Qt::UserRole).isNull())return;
        const QString code=item->data(0,Qt::UserRole).toString();
        const QString packet=item->data(0,Qt::UserRole+1).toString();
        fieldDetail_->setText(uiText(QStringLiteral("字段编号 %1 · 位位置 %2 · 换算公式 %3\n英文名：%4\n定义来源：%5\n当前会话尚未找到该字段的已解码样本。"))
            .arg(code,item->data(0,Qt::UserRole+2).toString(),item->data(0,Qt::UserRole+3).toString(),
                 item->data(0,Qt::UserRole+4).toString(),item->data(0,Qt::UserRole+5).toString()));
        bool hasSample=false;
        for(int i=0;i<business_.size();++i)if(i<=currentIndex_ && business_[i].packetName==packet)
            for(const auto &f:business_[i].fields)if(f.code==code){
                fieldDetail_->setText(uiText(QStringLiteral("字段编号 %1 · 位位置 %2\n原始 HEX：%3 · 原始值：%4\n当前工程值：%5 %6\n换算公式：%7\n定义来源：%8"))
                    .arg(f.code,f.bits,f.rawHex,f.rawNumber,f.value,f.unit,f.formula,f.definitionEvidence));
                hasSample=true;
                break;
            }
        Q_UNUSED(hasSample);
        plotEngineering(packet,code,currentIndex_);
    });
    connect(engineeringSearch_,&QLineEdit::textChanged,this,[this](const QString &needle){
        if(!fieldTree_)return;
        std::function<bool(QTreeWidgetItem*)> filter=[&](QTreeWidgetItem *node){
            bool childVisible=false;
            for(int i=0;i<node->childCount();++i)
                if(filter(node->child(i)))childVisible=true;
            const bool selfMatch=needle.isEmpty()||node->text(0).contains(needle,Qt::CaseInsensitive)||
                node->data(0,Qt::UserRole).toString().contains(needle,Qt::CaseInsensitive);
            const bool visible=selfMatch||childVisible;
            node->setHidden(!visible);
            return visible;
        };
        for(int i=0;i<fieldTree_->topLevelItemCount();++i)filter(fieldTree_->topLevelItem(i));
    });
    tabs->addTab(page,QStringLiteral("业务遥测"));
    buildComponentTabs(tabs);
}
void MainWindow::buildComponentTabs(QTabWidget *tabs){
    auto *page=new QWidget;
    auto *layout=new QVBoxLayout(page);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(3);

    // Two visible tab levels: subsystem group first, official telemetry packet name second.
    // The JSON metadata is derived only from official tree labels and field identifiers;
    // sampled f_text/f_double values are deliberately not copied into the catalog.
    auto *systems=new AutoWrapTabWidget(page);
    systems->setObjectName("officialSystemTabs");
    systems->setDocumentMode(true);
    systems->setTabPresentation(false,QStringLiteral("system"));

    const QString catalogRoot=QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("telemetry-trees"));
    for(const QString &treeSatellite : {QStringLiteral("ASRTU-1"),QStringLiteral("BY04")}){
        const QString catalogName=treeSatellite+QStringLiteral(".json");
        QFile file(QDir(catalogRoot).filePath(catalogName));
        if(!file.open(QIODevice::ReadOnly)){
            file.setFileName(QStringLiteral(":/telemetry-trees/")+catalogName);
            if(!file.open(QIODevice::ReadOnly)){
                auto *errorPage=new QWidget;
                errorPage->setProperty("treeSatellite",treeSatellite);
                auto *errorLayout=new QVBoxLayout(errorPage);
                errorLayout->addWidget(new QLabel(QStringLiteral("官方遥测字段目录无法读取：%1").arg(catalogName)));
                errorPage->setProperty("orbitSourceTabText",QStringLiteral("目录错误"));
                const int errorTab=systems->addTab(errorPage,uiText(QStringLiteral("目录错误")));
                systems->setTabToolTip(errorTab,uiText(treeSatellite+QStringLiteral(" · 目录错误")));
                continue;
            }
        }
        QJsonParseError parseError;
        const QByteArray catalogBytes=file.readAll();
        file.close();
        const QJsonDocument document=QJsonDocument::fromJson(catalogBytes,&parseError);
        const QJsonArray packetArray=document.object().value(QStringLiteral("packets")).toArray();
        if(!document.isObject()||packetArray.isEmpty()){
            auto *errorPage=new QWidget;
            errorPage->setProperty("treeSatellite",treeSatellite);
            auto *errorLayout=new QVBoxLayout(errorPage);
            errorLayout->addWidget(new QLabel(QStringLiteral("遥测目录格式错误：%1（%2）")
                .arg(catalogName,parseError.errorString())));
            errorPage->setProperty("orbitSourceTabText",QStringLiteral("目录错误"));
            const int errorTab=systems->addTab(errorPage,uiText(QStringLiteral("目录错误")));
            systems->setTabToolTip(errorTab,uiText(treeSatellite+QStringLiteral(" · 目录错误")));
            continue;
        }

        QStringList groupOrder;
        QHash<QString,int> groupIndices;
        QVector<QVector<QJsonObject>> packetsByGroup;
        for(const auto &packetValue:packetArray){
            if(!packetValue.isObject())continue;
            const QJsonObject packet=packetValue.toObject();
            const QString groupName=packet.value(QStringLiteral("group")).toString(
                                        QStringLiteral("其他分系统"));
            if(!groupIndices.contains(groupName)){
                groupIndices.insert(groupName,groupOrder.size());
                groupOrder.push_back(groupName);
                packetsByGroup.push_back(QVector<QJsonObject>());
            }
            packetsByGroup[groupIndices.value(groupName)].push_back(packet);
        }

        for(int groupIndex=0;groupIndex<groupOrder.size();++groupIndex){
            const QString groupName=groupOrder[groupIndex];
            auto *groupPage=new QWidget;
            groupPage->setProperty("treeSatellite",treeSatellite);
            groupPage->setProperty("orbitSourceTabText",groupName);
            auto *groupLayout=new QVBoxLayout(groupPage);
            groupLayout->setContentsMargins(2,2,2,2);
            groupLayout->setSpacing(2);

            auto *packetTabs=new AutoWrapTabWidget(groupPage);
            packetTabs->setObjectName("officialPacketTabs");
            packetTabs->setDocumentMode(true);
            packetTabs->setTabPresentation(true,QStringLiteral("packet"));

            for(const QJsonObject &packet:packetsByGroup[groupIndex]){
                const QString officialName=packet.value(QStringLiteral("official_name")).toString();
                const QString packetCode=packet.value(QStringLiteral("packet_code")).toString();
                const QJsonArray fields=packet.value(QStringLiteral("fields")).toArray();

                auto *table=new QTableWidget(fields.isEmpty()?1:fields.size(),4,packetTabs);
                table->setProperty("orbitSourceTabText",officialName);
                table->setObjectName("officialComponentTable");
                table->setProperty("treeSatellite",treeSatellite);
                table->setProperty("officialPacketCode",packetCode);
                table->setProperty("officialPacketName",officialName);
                table->setHorizontalHeaderLabels({QStringLiteral("字段名称"),
                    QStringLiteral("完整字段编号"),QStringLiteral("当前值"),QStringLiteral("接收时间")});
                setupTable(table);
                table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
                table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
                table->verticalHeader()->setDefaultSectionSize(17);
                table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Interactive);
                table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Interactive);
                table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
                table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
                table->setColumnWidth(0,250);
                table->setColumnWidth(1,190);

                if(fields.isEmpty()){
                    auto *nameItem=new QTableWidgetItem(uiText(QStringLiteral("附件中未提供该包的字段条目")));
                    nameItem->setData(Qt::UserRole+123,QStringLiteral("附件中未提供该包的字段条目"));
                    nameItem->setData(Qt::UserRole+2,true);
                    const QString emptyCatalogTooltip=QStringLiteral("保留官方数据包位置；附件中的对应 JSON data 对象为空，未臆造字段。");
                    nameItem->setData(Qt::UserRole+124,emptyCatalogTooltip);
                    nameItem->setToolTip(uiText(emptyCatalogTooltip));
                    table->setItem(0,0,nameItem);
                    table->setItem(0,1,new QTableWidgetItem(QString()));
                    auto *statusItem=new QTableWidgetItem(uiText(QStringLiteral("未解析")));
                    statusItem->setData(Qt::UserRole+123,QStringLiteral("未解析"));
                    table->setItem(0,2,statusItem);
                    table->setItem(0,3,new QTableWidgetItem(QString()));
                }else{
                    for(int row=0;row<fields.size();++row){
                        const QJsonObject field=fields.at(row).toObject();
                        const QString fieldName=field.value(QStringLiteral("name")).toString();
                        const QString fullCode=field.value(QStringLiteral("code")).toString();
                        const QString englishName=field.value(QStringLiteral("english_name")).toString();
                        const QString sourceDisplay=fieldName.isEmpty()?fullCode:fieldName;
                        const QString localizedDisplay=uiText(sourceDisplay);
                        auto *nameItem=new QTableWidgetItem(localizedDisplay);
                        nameItem->setData(Qt::UserRole,fullCode);
                        nameItem->setData(Qt::UserRole+1,englishName);
                        nameItem->setData(Qt::UserRole+123,sourceDisplay);
                        const QString nameTooltip=QStringLiteral("官方字段：%1\n英文名：%2").arg(fullCode,englishName);
                        nameItem->setData(Qt::UserRole+124,nameTooltip);
                        nameItem->setToolTip(uiText(nameTooltip));
                        table->setItem(row,0,nameItem);
                        table->setItem(row,1,new QTableWidgetItem(fullCode));
                        auto *statusItem=new QTableWidgetItem(uiText(QStringLiteral("未解析")));
                        statusItem->setData(Qt::UserRole+123,QStringLiteral("未解析"));
                        table->setItem(row,2,statusItem);
                        table->setItem(row,3,new QTableWidgetItem(QString()));
                    }
                }
                packetTabs->addTab(table,officialName);
                componentTables_.insert(treeSatellite+QLatin1Char('|')+packetCode,table);
            }
            groupLayout->addWidget(packetTabs,1);
            const int systemTab=systems->addTab(groupPage,uiText(groupName));
            systems->setTabToolTip(systemTab,uiText(treeSatellite+QStringLiteral(" · ")+groupName));
        }
    }

    layout->addWidget(systems,1);
    tabs->addTab(page,QStringLiteral("分系统数据"));
    if(satelliteSelect_){
        connect(satelliteSelect_,&QComboBox::currentTextChanged,this,
                [this]{updateComponentTabs(currentIndex_);populateEngineering(currentIndex_);updateOverview(currentIndex_);updateAttitude(currentIndex_);});
    }
}
void MainWindow::buildAttitude(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    attitudeInfo_=subtitle(QStringLiteral("仅展示 SAT-S3 已识别字段；四元数参考系与应用层 CRC 未验证"));
    layout->addWidget(attitudeInfo_);
    auto *quats=new QWidget;auto *grid=new QGridLayout(quats);
    grid->setContentsMargins(0,0,0,0);
    for(int i=0;i<4;++i){
        QLabel *number=nullptr;
        grid->addWidget(metricCard(QString("q%1").arg(i),number),0,i);
        quaternion_[i]=number;
    }
    QLabel *n=nullptr;
    grid->addWidget(metricCard(QStringLiteral("四元数模长"),n),1,0,1,2);
    normLabel_=n;
    auto *explanation=new QLabel(QStringLiteral("SAT-S3：有符号 32 位整数 / 10000\n仅作数值与相对姿态展示，未定义绝对旋转参考系"));
    explanation->setWordWrap(true);
    grid->addWidget(explanation,1,2,1,2);
    const QStringList axes={QStringLiteral("X"),QStringLiteral("Y"),QStringLiteral("Z")};
    for(int i=0;i<3;++i){
        QLabel *rate=nullptr;
        grid->addWidget(metricCard(QStringLiteral("角速度 %1").arg(axes[i]),rate),2,i);
        angularVelocity_[i]=rate;
    }
    layout->addWidget(quats);
    auto *views=new QSplitter(Qt::Horizontal);
    attitudeWidget_=new AttitudeWidget;
    views->addWidget(attitudeWidget_);
    attitudeChart_=new QChartView(new QChart);
    attitudeChart_->setRenderHint(QPainter::Antialiasing);
    views->addWidget(attitudeChart_);
    views->setSizes({380,650});
    layout->addWidget(views,1);
    tabs->addTab(page,QStringLiteral("姿态与轨道"));
}
void MainWindow::buildImages(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    imageInfo_=subtitle(QStringLiteral("SSDV 图片恢复：候选图像数据不等价于 CRC 验证成功"));
    layout->addWidget(imageInfo_);
    auto *row=new QHBoxLayout;
    row->addWidget(new QLabel(uiText(QStringLiteral("SSDV 图像归属"))));
    imageSatelliteSelect_=new QComboBox;
    imageSatelliteSelect_->addItem(uiText(QStringLiteral("自动 CRC 识别")),QStringLiteral("AUTO"));
    imageSatelliteSelect_->addItem(uiText(QStringLiteral("手动标记 JAMX（不跳过 CRC）")),QStringLiteral("JAMX"));
    imageSatelliteSelect_->setToolTip(uiText(QStringLiteral("仅修改 SSDV 图像的卫星归属标签；不会绕过 CRC 校验或强行解码损坏分片。")));
    row->addWidget(imageSatelliteSelect_);
    row->addWidget(new QLabel(QStringLiteral("图像业务 APID（BY04 固定 VC1）")));
    imageApid_=new QSpinBox;
    imageApid_->setRange(0,2047);imageApid_->setValue(240);
    row->addWidget(imageApid_);
    auto *scan=new QPushButton(QStringLiteral("扫描候选分片"));
    row->addWidget(scan);
    auto *save=new QPushButton(QStringLiteral("统一导出…"));
    row->addWidget(save);
    auto *exportImageBin=new QPushButton(QStringLiteral("导出所选图像 BIN"));
    exportImageBin->setObjectName(QStringLiteral("exportSelectedImageBin"));
    row->addWidget(exportImageBin);
    auto *importImageBinButton=new QPushButton(QStringLiteral("导入 BIN 并解码"));
    importImageBinButton->setObjectName(QStringLiteral("importImageBin"));
    row->addWidget(importImageBinButton);
    auto *decode=new QPushButton(QStringLiteral("重建 JPEG 预览"));
    row->addWidget(decode);
    row->addStretch(1);
    layout->addLayout(row);
    imageCoverageNote_=subtitle(QStringLiteral("总分片数未知 · 无法推断整幅图片完成度"));
    layout->addWidget(imageCoverageNote_);
    imageCoverage_=new QProgressBar;
    imageCoverage_->setRange(0,1);
    imageCoverage_->setValue(0);
    imageCoverage_->setFormat(uiText(QStringLiteral("尚无观测序号")));
    layout->addWidget(imageCoverage_);
    auto *split=new QSplitter(Qt::Horizontal);
    imagePreview_=new ImagePreview;
    split->addWidget(imagePreview_);
    imageTable_=new QTableWidget(0,5);
    imageTable_->setHorizontalHeaderLabels({QStringLiteral("卫星 / 图像 ID"),
        QStringLiteral("帧数"),QStringLiteral("分辨率"),QStringLiteral("缺块 / 坏块") ,QStringLiteral("校验状态")});
    setupTable(imageTable_);
    imageTable_->horizontalHeader()->setStretchLastSection(true);
    split->addWidget(imageTable_);
    split->setSizes({550,430});
    layout->addWidget(split,1);
    connect(scan,&QPushButton::clicked,this,[this]{scanSsdv();});
    connect(imageTable_,&QTableWidget::itemSelectionChanged,this,[this]{
        const int imageId=selectedImageId();
        if(imageId>=0){
            updateImageProgress(imageId);
            decodeSsdvImage(imageId,false);
        }
    });
    connect(save,&QPushButton::clicked,this,[this]{showExportDialog();});
    connect(exportImageBin,&QPushButton::clicked,this,[this]{exportSelectedImageBin();});
    connect(importImageBinButton,&QPushButton::clicked,this,[this]{importImageBin();});
    connect(decode,&QPushButton::clicked,this,[this]{decodeSsdv();});
    tabs->addTab(page,QStringLiteral("图像数据"));
}
void MainWindow::buildRaw(QTabWidget *tabs){
    rawPage_=new QTabWidget;
    auto *page=rawPage_;
    rawHex_=new QPlainTextEdit;
    rawHex_->setReadOnly(true);
    rawHex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawHex_->setPlainText(uiText(QStringLiteral("选择业务包后显示 KISS / HEX 数据。")));
    page->addTab(rawHex_,QStringLiteral("KISS / HEX"));

    packetTable_=new QTableWidget(0,6);
    packetTable_->setHorizontalHeaderLabels({QStringLiteral("VCID"),
        QStringLiteral("帧号范围"),QStringLiteral("字节数"),QStringLiteral("候选 APID"),
        QStringLiteral("FEC 证据"),QStringLiteral("校验状态")});
    setupTable(packetTable_);
    page->addTab(packetTable_,QStringLiteral("KISS 应用包"));

    auto *framePage=new QWidget;
    auto *frameLayout=new QVBoxLayout(framePage);
    auto *note=subtitle(QStringLiteral("选择下方传输帧可查看完整原始帧。BY04 / ASRTU-1 信息帧为 223 字节时将逐字节显示；不会将 KISS 内容误当作原始帧。"));
    frameLayout->addWidget(note);
    rawFrameTable_=new QTableWidget(0,6);
    rawFrameTable_->setHorizontalHeaderLabels({QStringLiteral("接收时间"),QStringLiteral("卫星"),
        QStringLiteral("主帧计数"),QStringLiteral("VCID"),QStringLiteral("帧长度"),QStringLiteral("结构状态")});
    setupTable(rawFrameTable_);
    rawFrameTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    frameLayout->addWidget(rawFrameTable_,2);
    rawFrameHex_=new QPlainTextEdit;
    rawFrameHex_->setReadOnly(true);
    rawFrameHex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawFrameHex_->setPlaceholderText(uiText(QStringLiteral("选择一条原始帧以显示十六进制内容。")));
    frameLayout->addWidget(rawFrameHex_,3);
    connect(rawFrameTable_,&QTableWidget::currentCellChanged,this,
            [this](int row,int,int,int){inspectRawFrame(row);});
    page->addTab(framePage,QStringLiteral("223 字节原始帧"));
    tabs->addTab(page,QStringLiteral("原始数据"));
}
void MainWindow::buildDiagnostics(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    diagnosticsSummary_=subtitle(QStringLiteral("底层帧列表、重组诊断、错误来源及协议调试信息"));
    layout->addWidget(diagnosticsSummary_);
    auto *split=new QSplitter(Qt::Vertical);
    frameTable_=new QTableWidget(0,7);
    frameTable_->setHorizontalHeaderLabels({QStringLiteral("接收时间"),
        QStringLiteral("MFC"),"VCID","APID",QStringLiteral("序号"),
        QStringLiteral("来源 / FEC 证据"),QStringLiteral("结构状态")});
    setupTable(frameTable_);
    frameTable_->horizontalHeader()->setStretchLastSection(true);
    split->addWidget(frameTable_);
    debugText_=new QPlainTextEdit;
    debugText_->setReadOnly(true);
    debugText_->setLineWrapMode(QPlainTextEdit::NoWrap);
    split->addWidget(debugText_);
    split->setSizes({420,210});
    layout->addWidget(split);
    connect(frameTable_,&QTableWidget::itemSelectionChanged,this,[this]{
        inspectFrame(frameTable_->currentRow());
    });
    diagnosticTab_=tabs->addTab(page,QStringLiteral("调试信息"));
}
