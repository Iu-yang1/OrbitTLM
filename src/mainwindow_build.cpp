#include "mainwindow.h"
#include "mainwindow_widgets.h"
#include <QAction>
#include <QApplication>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
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
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

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
    layout->setContentsMargins(15,20,15,12);
    auto *content=new QLabel(QStringLiteral("暂无已识别的遥测参数"));
    content->setObjectName("statusContent");
    content->setTextFormat(Qt::RichText);
    content->setAlignment(Qt::AlignTop|Qt::AlignLeft);
    content->setWordWrap(true);
    content->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(content);
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
    setWindowTitle(QStringLiteral("OrbitTLM · 卫星遥测回放工作台"));
    setMinimumSize(1050,690);
    resize(1480,910);
    live_=new orbit::LiveTcp(this);
    applyStyles();
    buildToolbar();
    buildWorkspace();
    buildMenus();
    timer_=new QTimer(this);timer_->setInterval(500);
    connect(timer_,&QTimer::timeout,this,[this]{
        if(business_.isEmpty()){timer_->stop();return;}
        seekVisible(1);
    });
    connect(live_,&orbit::LiveTcp::status,this,[this](const QString &message){
        statusBar()->showMessage(QStringLiteral("TCP 连接状态：")+message,7500);
    });
    connect(live_,&orbit::LiveTcp::received,this,[this](const orbit::Frame &f){
        for(const auto &existing:frames_)if(existing.hash==f.hash)return;
        frames_.push_back(f);
        rebuildSession();
        statusBar()->showMessage(QStringLiteral("接收到新的传输帧；尚未独立校验应用层 CRC"),7000);
    });
    const QString dbFile=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+
                         "/orbittlm.sqlite";
    QDir().mkpath(QFileInfo(dbFile).absolutePath());
    QString error;
    if(!db_.open(dbFile,&error))
        statusBar()->showMessage(QStringLiteral("SQLite 数据库无法打开：")+error,18000);
    error.clear();
    if(catalog_.loadDirectory(QApplication::applicationDirPath()+"/protocols/asrtu1",&error)){
        catalogReady_=true;
    }else{
        statusBar()->showMessage(QStringLiteral("缺少 ASRTU-1 遥测字段库：")+error,18000);
    }
    updatePaths();
    updateMetrics();
    showEvent(-1);
    setDiagnosticsVisible(false);
}
void MainWindow::applyStyles(){
    setStyleSheet(QStringLiteral(R"(
        QWidget {color:#1f2328; font-size:13px;}
        QMainWindow {background:#ffffff;}
        QMenuBar,QToolBar {background:#f6f8fa; border:0; border-bottom:1px solid #d1d9e0;}
        QToolBar {spacing:8px; padding:12px 16px;}
        QMenu {background:white; border:1px solid #d1d9e0; padding:4px;}
        QMenu::item {padding:7px 18px;}
        QMenu::item:selected {background:#eef1f4;}
        QToolButton,QPushButton {background:#f6f8fa; border:1px solid #d1d9e0;
            border-radius:6px; padding:6px 12px; font-weight:500;}
        QToolButton:hover,QPushButton:hover {background:#eff2f5; border-color:#b7c0c8;}
        QToolButton#primaryButton,QPushButton#primaryButton {background:#1f883d; color:white; border-color:#1f7a38;}
        QToolButton#primaryButton:hover {background:#1a7f37;}
        QComboBox,QSpinBox,QLineEdit {background:white; border:1px solid #d1d9e0;
            border-radius:6px; padding:5px 8px; min-height:20px;}
        QLineEdit:focus,QComboBox:focus {border:2px solid #0969da; padding:4px 7px;}
        QFrame#metricCard,QFrame#heroCard,QFrame#panel {background:white; border:1px solid #d1d9e0; border-radius:6px;}
        QLabel#subtitle,QLabel#note {color:#59636e; font-size:12px;}
        QLabel#metricNumber {font-size:19px; font-weight:600;}
        QLabel#currentHeading {font-size:20px; font-weight:600;}
        QLabel#detailHeading {font-size:14px; font-weight:600;}
        QLabel#brandTitle {font-size:19px; font-weight:600; padding-right:12px;}
        QTabWidget::pane {background:white; border:0;}
        QTabBar::tab {background:transparent; border:0; border-bottom:2px solid transparent;
            margin-right:8px; padding:11px 14px; min-width:48px;}
        QTabBar::tab:selected {border-bottom:2px solid #fd8c73; font-weight:600;}
        QTabBar::tab:hover {background:#f6f8fa;}
        QTabBar#workspaceNavigation {border-bottom:1px solid #d1d9e0;}
        QTableWidget,QTreeWidget,QPlainTextEdit {background:white; border:1px solid #d1d9e0;
            border-radius:6px; gridline-color:#d1d9e0; selection-background-color:#ddf4ff;}
        QHeaderView::section {background:#f6f8fa; border:0; border-bottom:1px solid #d1d9e0;
            color:#59636e; font-weight:500; padding:8px 6px;}
        QTableWidget::item {padding:5px 4px; border-bottom:1px solid #e8ecf0;}
        QTableWidget::item:selected,QTreeWidget::item:selected {background:#ddf4ff; color:#1f2328;}
        QGroupBox#statusCard {background:white; border:1px solid #d1d9e0; border-radius:6px; margin-top:10px;}
        QGroupBox#statusCard::title {subcontrol-origin:margin; left:12px; padding:0 5px; font-weight:600;}
        QLabel#statusContent {font-weight:400;}
        QSplitter::handle {background:#f6f8fa; width:10px; height:10px;}
        QStatusBar {background:#f6f8fa; color:#59636e; border-top:1px solid #d1d9e0;}
        QSlider::groove:horizontal {height:4px; background:#d1d9e0; border-radius:2px;}
        QSlider::handle:horizontal {width:12px; background:#0969da; border-radius:6px; margin:-4px 0;}
    )"));
}
void MainWindow::buildToolbar(){
    auto *bar=addToolBar(QStringLiteral("遥测工作台"));
    bar->setMovable(false);
    auto *brand=new QLabel(QStringLiteral("OrbitTLM"));
    brand->setObjectName("brandTitle");bar->addWidget(brand);
    auto *log=bar->addAction(QStringLiteral("选择 LOG"));
    connect(log,&QAction::triggered,this,[this]{
        const QString p=QFileDialog::getOpenFileName(this,QStringLiteral("选择解码日志"),{},
                                                     QStringLiteral("日志 (*.log *.txt);;全部文件 (*)"));
        if(!p.isEmpty()){logPath_=p;updatePaths();}
    });
    auto *bin=bar->addAction(QStringLiteral("选择 BIN"));
    connect(bin,&QAction::triggered,this,[this]{
        const QString p=QFileDialog::getOpenFileName(this,QStringLiteral("选择 223 字节帧文件"),{},
                                                     QStringLiteral("数据帧 (*.bin);;全部文件 (*)"));
        if(!p.isEmpty()){binPath_=p;updatePaths();}
    });
    bar->addSeparator();
    bar->addWidget(new QLabel(QStringLiteral("卫星  ")));
    satelliteSelect_=new QComboBox;
    satelliteSelect_->addItem("ASRTU-1",QStringLiteral("ASRTU-1"));
    satelliteSelect_->addItem(QStringLiteral("八一04星 · BY04"),QStringLiteral("BY04"));
    bar->addWidget(satelliteSelect_);
    auto *import=new QToolButton;
    import->setText(QStringLiteral("导入"));import->setObjectName("primaryButton");
    import->setAccessibleName(QStringLiteral("导入会话"));bar->addWidget(import);
    connect(import,&QToolButton::clicked,this,[this]{importSession();});
    auto *spacer=new QWidget;spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    bar->addWidget(spacer);
    auto *history=bar->addAction(QStringLiteral("历史会话"));
    connect(history,&QAction::triggered,this,[this]{loadHistory();});
    bar->addSeparator();
    auto *exportButton=new QToolButton;
    exportButton->setText(QStringLiteral("导出"));exportButton->setPopupMode(QToolButton::InstantPopup);
    auto *exports=new QMenu(exportButton);
    exports->addAction(QStringLiteral("遥测 CSV"),this,[this]{exportEngineering(true);});
    exports->addAction(QStringLiteral("遥测 JSON"),this,[this]{exportEngineering(false);});
    exports->addAction(QStringLiteral("四元数 CSV"),this,[this]{exportQuaternions(false);});
    exports->addAction(QStringLiteral("KISS 数据"),this,[this]{exportKiss();});
    exportButton->setMenu(exports);bar->addWidget(exportButton);
    bar->addSeparator();
    auto *connectTcp=bar->addAction(QStringLiteral("连接 TCP"));
    connect(connectTcp,&QAction::triggered,this,[this]{
        bool ok=false;
        const QString address=QInputDialog::getText(this,QStringLiteral("TCP 实时输入"),
          QStringLiteral("主机地址"),QLineEdit::Normal,"127.0.0.1",&ok);
        if(!ok||address.trimmed().isEmpty())return;
        const int port=QInputDialog::getInt(this,QStringLiteral("TCP 实时输入"),
            QStringLiteral("端口"),9985,1,65535,1,&ok);
        if(ok)live_->start(address,quint16(port),satellite());
    });
    auto *stop=bar->addAction(QStringLiteral("断开"));
    connect(stop,&QAction::triggered,live_,&orbit::LiveTcp::stop);
}
void MainWindow::buildMenus(){
    auto *file=menuBar()->addMenu(QStringLiteral("文件(&F)"));
    file->addAction(QStringLiteral("打开 SQLite 历史会话"),[this]{loadHistory();});
    file->addSeparator();
    file->addAction(QStringLiteral("导出传输帧 CSV"),[this]{exportFrames(true);});
    file->addAction(QStringLiteral("导出传输帧 JSON"),[this]{exportFrames(false);});
    file->addAction(QStringLiteral("导出业务遥测 CSV"),[this]{exportEngineering(true);});
    file->addAction(QStringLiteral("导出业务遥测 JSON"),[this]{exportEngineering(false);});
    file->addAction(QStringLiteral("导出四元数 CSV"),[this]{exportQuaternions(false);});
    file->addAction(QStringLiteral("导出实验性候选四元数"),[this]{exportQuaternions(true);});
    file->addAction(QStringLiteral("导出 KISS 应用包"),[this]{exportKiss();});
    auto *tools=menuBar()->addMenu(QStringLiteral("工具(&T)"));
    tools->addAction(QStringLiteral("载入自定义字段规则 JSON"),[this]{
        const QString p=QFileDialog::getOpenFileName(this,QStringLiteral("选择字段规则"),{},
                                                       QStringLiteral("JSON 文件 (*.json)"));
        if(p.isEmpty())return;
        orbit::Schema candidate;QString error;
        if(!candidate.load(p,&error)){QMessageBox::warning(this,QStringLiteral("字段规则错误"),error);return;}
        if(candidate.satellite()!=satellite()){
            QMessageBox::warning(this,QStringLiteral("卫星不匹配"),QStringLiteral("该规则属于另一颗卫星"));return;
        }
        customSchema_=candidate;hasCustomSchema_=true;
        for(auto &f:frames_)customSchema_.apply(f);
        inspectFrame(0);
        statusBar()->showMessage(QStringLiteral("已载入独立字段规则，尚需验证字段物理含义"),9000);
    });
    auto *debug=tools->addAction(QStringLiteral("显示调试信息"));
    debug->setCheckable(true);
    connect(debug,&QAction::toggled,this,[this](bool active){setDiagnosticsVisible(active);});
}
void MainWindow::buildWorkspace(){
    auto *root=new QWidget;
    setCentralWidget(root);
    auto *main=new QVBoxLayout(root);
    main->setContentsMargins(16,0,16,12);
    main->setSpacing(12);
    navigation_=new QTabBar;
    navigation_->setObjectName("workspaceNavigation");
    navigation_->setExpanding(false);navigation_->setDrawBase(false);
    navigation_->setAccessibleName(QStringLiteral("工作区导航"));
    main->addWidget(navigation_);
    auto *fileBar=new QFrame;
    fileBar->setObjectName("panel");
    auto *fileLayout=new QHBoxLayout(fileBar);
    fileLayout->setContentsMargins(15,9,15,9);
    sourceLabel_=new QLabel;
    sourceLabel_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    fileLayout->addWidget(sourceLabel_,1);
    message_=new QLabel(QStringLiteral("离线会话 · 应用层 CRC 未校验"));
    message_->setObjectName("note");
    fileLayout->addWidget(message_);
    main->addWidget(fileBar);
    auto *split=new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    auto *left=new QFrame;
    left->setMinimumWidth(260);
    left->setObjectName("panel");
    auto *leftLayout=new QVBoxLayout(left);
    leftLayout->setContentsMargins(13,12,13,12);
    leftLayout->setSpacing(8);
    auto *header=new QLabel(QStringLiteral("数据回放列表"));
    header->setObjectName("detailHeading");
    leftLayout->addWidget(header);
    eventSearch_=new QLineEdit;
    eventSearch_->setPlaceholderText(QStringLiteral("搜索时间、分系统或数据包"));
    leftLayout->addWidget(eventSearch_);
    recognizedOnly_=new QCheckBox(QStringLiteral("仅显示已识别数据"));
    leftLayout->addWidget(recognizedOnly_);
    eventTable_=new QTableWidget(0,4);
    eventTable_->setObjectName("eventTimeline");
    eventTable_->setHorizontalHeaderLabels({QStringLiteral("时间"),
        QStringLiteral("数据包 / 事件"),QStringLiteral("状态"),QStringLiteral("来源")});
    setupTable(eventTable_);
    eventTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    eventTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    eventTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    eventTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    eventTable_->verticalHeader()->setDefaultSectionSize(43);
    eventTable_->setColumnHidden(3,true);
    leftLayout->addWidget(eventTable_,1);
    connect(eventTable_,&QTableWidget::itemSelectionChanged,this,[this]{
        const int row=eventTable_->currentRow();
        if(row>=0)selectEvent(row);
    });
    connect(eventSearch_,&QLineEdit::textChanged,this,[this]{applyEventFilter();});
    connect(recognizedOnly_,&QCheckBox::toggled,this,[this]{applyEventFilter();});
    split->addWidget(left);
    pages_=new QTabWidget;
    pages_->setObjectName("mainPages");
    buildOverview(pages_);
    buildEngineering(pages_);
    buildAttitude(pages_);
    buildImages(pages_);
    buildRaw(pages_);
    buildDiagnostics(pages_);
    for(int i=0;i<pages_->count();++i)navigation_->addTab(pages_->tabText(i));
    pages_->tabBar()->hide();
    connect(navigation_,&QTabBar::currentChanged,pages_,&QTabWidget::setCurrentIndex);
    connect(pages_,&QTabWidget::currentChanged,navigation_,&QTabBar::setCurrentIndex);
    split->addWidget(pages_);
    split->setStretchFactor(0,0);
    split->setStretchFactor(1,1);
    split->setObjectName("workspaceSplit");
    split->setSizes({340,1100});
    main->addWidget(split,1);
    auto *transport=new QFrame;
    transport->setObjectName("panel");
    auto *transportLayout=new QHBoxLayout(transport);
    transportLayout->setContentsMargins(12,7,12,7);
    auto *back=new QPushButton(QStringLiteral("上一条"));
    auto *play=new QPushButton(QStringLiteral("播放"));
    auto *pause=new QPushButton(QStringLiteral("暂停"));
    auto *next=new QPushButton(QStringLiteral("下一条"));
    transportLayout->addWidget(back);
    transportLayout->addWidget(play);
    transportLayout->addWidget(pause);
    transportLayout->addWidget(next);
    timeline_=new QSlider(Qt::Horizontal);
    timeline_->setRange(0,0);
    transportLayout->addWidget(timeline_,1);
    speedSelect_=new QComboBox;
    speedSelect_->addItems({"0.5×","1×","2×","4×","8×"});
    speedSelect_->setCurrentIndex(1);
    transportLayout->addWidget(speedSelect_);
    positionLabel_=new QLabel(QStringLiteral("0 / 0"));
    positionLabel_->setMinimumWidth(63);
    transportLayout->addWidget(positionLabel_);
    main->addWidget(transport);
    connect(back,&QPushButton::clicked,this,[this]{seekVisible(-1);});
    connect(next,&QPushButton::clicked,this,[this]{seekVisible(1);});
    connect(play,&QPushButton::clicked,this,[this]{if(!business_.isEmpty())timer_->start();});
    connect(pause,&QPushButton::clicked,this,[this]{timer_->stop();});
    connect(timeline_,&QSlider::valueChanged,this,[this](int i){selectEvent(i);});
    connect(speedSelect_,&QComboBox::currentIndexChanged,this,[this](int i){
        const int intervals[5]={1000,500,250,125,63};if(timer_)timer_->setInterval(intervals[i]);
    });
    statusBar()->showMessage(QStringLiteral("就绪"));
}
void MainWindow::buildOverview(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    layout->setContentsMargins(12,13,12,13);layout->setSpacing(10);
    auto *metrics=new QWidget;auto *grid=new QGridLayout(metrics);
    grid->setContentsMargins(0,0,0,0);grid->setSpacing(8);
    grid->addWidget(metricCard(QStringLiteral("时间范围"),timeMetric_),0,0);
    grid->addWidget(metricCard(QStringLiteral("去重传输帧"),frameMetric_),0,1);
    grid->addWidget(metricCard(QStringLiteral("已识别业务包"),recognizedMetric_),0,2);
    grid->addWidget(metricCard(QStringLiteral("未识别"),unknownMetric_),0,3);
    grid->addWidget(metricCard(QStringLiteral("图像分片"),imageMetric_),0,4);
    grid->addWidget(metricCard(QStringLiteral("KISS 包"),transportMetric_),0,5);
    layout->addWidget(metrics);
    auto *hero=new QFrame;hero->setObjectName("heroCard");
    auto *heroLayout=new QVBoxLayout(hero);
    heroLayout->setContentsMargins(18,13,18,13);
    currentTitle_=new QLabel(QStringLiteral("尚未载入遥测会话"));
    currentTitle_->setObjectName("currentHeading");
    heroLayout->addWidget(currentTitle_);
    currentMeta_=subtitle(QStringLiteral("导入 decoder.log 与 223 字节 BIN，开始时间轴回放"));
    heroLayout->addWidget(currentMeta_);
    integrityLabel_=subtitle(QStringLiteral("没有载入数据；不会推断星载 UTC"));
    heroLayout->addWidget(integrityLabel_);
    layout->addWidget(hero);
    auto *cards=new QWidget;
    auto *cardsLayout=new QGridLayout(cards);
    cardsLayout->setContentsMargins(0,0,0,0);
    cardsLayout->setHorizontalSpacing(9);cardsLayout->setVerticalSpacing(9);
    powerCard_=statusCard(QStringLiteral("电源与配电"));
    attitudeCard_=statusCard(QStringLiteral("姿态与轨道"));
    thermalCard_=statusCard(QStringLiteral("热控与温度"));
    payloadCard_=statusCard(QStringLiteral("载荷与通信"));
    cardsLayout->addWidget(powerCard_,0,0);
    cardsLayout->addWidget(attitudeCard_,0,1);
    cardsLayout->addWidget(thermalCard_,1,0);
    cardsLayout->addWidget(payloadCard_,1,1);
    layout->addWidget(cards,1);
    tabs->addTab(page,QStringLiteral("总览"));
}
void MainWindow::buildEngineering(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    engineeringSummary_=subtitle(QStringLiteral("选择数据包查看参数"));
    engineeringSummary_->setObjectName("engineeringSummary");
    layout->addWidget(engineeringSummary_);
    engineeringSearch_=new QLineEdit;
    engineeringSearch_->setObjectName("engineeringSearch");
    engineeringSearch_->setPlaceholderText(QStringLiteral("筛选当前分包字段名或编号"));
    auto *filters=new QHBoxLayout;
    filters->addWidget(engineeringSearch_,1);
    auto *details=new QCheckBox(QStringLiteral("字段详情"));
    details->setObjectName("fieldDetailsToggle");filters->addWidget(details);
    layout->addLayout(filters);
    auto *split=new QSplitter(Qt::Horizontal);
    engineeringTable_=new QTableWidget(0,3);
    engineeringTable_->setHorizontalHeaderLabels({QStringLiteral("时间"),QStringLiteral("分系统"),QStringLiteral("状态")});
    setupTable(engineeringTable_);
    engineeringTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    engineeringTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    engineeringTable_->verticalHeader()->setDefaultSectionSize(34);
    engineeringTable_->setParent(page);engineeringTable_->hide();
    auto *right=new QWidget;auto *rightLayout=new QVBoxLayout(right);
    rightLayout->setContentsMargins(5,2,5,2);
    fieldTree_=new QTreeWidget;
    fieldTree_->setObjectName("engineeringFields");
    fieldTree_->setHeaderLabels({QStringLiteral("参数"),QStringLiteral("数值"),QStringLiteral("单位"),QStringLiteral("解析状态")});
    fieldTree_->setColumnWidth(0,245);
    fieldTree_->setColumnWidth(1,132);
    fieldTree_->setColumnHidden(3,true);
    fieldTree_->setRootIsDecorated(true);
    fieldTree_->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    fieldTree_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    fieldTree_->header()->setSectionResizeMode(2,QHeaderView::ResizeToContents);
    rightLayout->addWidget(fieldTree_,3);
    fieldDetail_=subtitle(QStringLiteral("选择参数查看原始 HEX、字段位置与换算公式"));
    fieldDetail_->setObjectName("fieldDetail");
    fieldDetail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(fieldDetail_);
    fieldDetail_->hide();
    connect(details,&QCheckBox::toggled,fieldDetail_,&QLabel::setVisible);
    engineeringChart_=new QChartView(new QChart);
    engineeringChart_->setObjectName("engineeringChart");
    engineeringChart_->setRenderHint(QPainter::Antialiasing);
    engineeringChart_->setMinimumHeight(165);
    rightLayout->addWidget(engineeringChart_,2);
    split->addWidget(right);
    split->setStretchFactor(0,1);
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
        if(currentIndex_<0||currentIndex_>=business_.size())return;
        if(business_[currentIndex_].packetName==packet)
            for(const auto &f:business_[currentIndex_].fields)if(f.code==code){
                fieldDetail_->setText(QStringLiteral("%1  ·  位位置 %2\n原码 0x%3  ·  公式 %4")
                    .arg(f.code,f.bits,f.rawHex,
                         f.formula.isEmpty()?QStringLiteral("—"):f.formula));
                fieldDetail_->setToolTip(f.definitionEvidence);
            }
        plotEngineering(packet,code,currentIndex_);
    });
    connect(engineeringSearch_,&QLineEdit::textChanged,this,[this]{applyEngineeringFilter();});
    tabs->addTab(page,QStringLiteral("业务遥测"));
}
void MainWindow::buildAttitude(QTabWidget *tabs){
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);
    attitudeInfo_=subtitle(QStringLiteral("暂无姿态数据"));
    layout->addWidget(attitudeInfo_);
    auto *quats=new QWidget;auto *grid=new QGridLayout(quats);
    grid->setContentsMargins(0,0,0,0);
    for(int i=0;i<4;++i){
        QLabel *number=nullptr;
        grid->addWidget(metricCard(QString("q%1").arg(i),number),0,i);
        quaternion_[i]=number;
    }
    QLabel *n=nullptr;
    grid->addWidget(metricCard(QStringLiteral("四元数模长"),n),1,0,1,4);
    normLabel_=n;
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
    row->addWidget(new QLabel(QStringLiteral("图像业务 APID（BY04 固定 VC1）")));
    imageApid_=new QSpinBox;
    imageApid_->setRange(0,2047);imageApid_->setValue(240);
    row->addWidget(imageApid_);
    auto *scan=new QPushButton(QStringLiteral("扫描候选分片"));
    row->addWidget(scan);
    auto *save=new QPushButton(QStringLiteral("保存 SSDV 数据"));
    row->addWidget(save);
    auto *decode=new QPushButton(QStringLiteral("外部解码为 JPEG"));
    row->addWidget(decode);
    row->addStretch(1);
    layout->addLayout(row);
    imageCoverageNote_=subtitle(QStringLiteral("总分片数未知 · 无法推断整幅图片完成度"));
    layout->addWidget(imageCoverageNote_);
    imageCoverage_=new QProgressBar;
    imageCoverage_->setRange(0,1);
    imageCoverage_->setValue(0);
    imageCoverage_->setFormat(QStringLiteral("尚无观测序号"));
    layout->addWidget(imageCoverage_);
    auto *split=new QSplitter(Qt::Horizontal);
    imagePreview_=new ImagePreview;
    split->addWidget(imagePreview_);
    imageTable_=new QTableWidget(0,4);
    imageTable_->setHorizontalHeaderLabels({QStringLiteral("图像 ID"),
        QStringLiteral("包序号"),QStringLiteral("宽 × 高"),QStringLiteral("校验状态")});
    setupTable(imageTable_);
    imageTable_->horizontalHeader()->setStretchLastSection(true);
    split->addWidget(imageTable_);
    split->setSizes({550,430});
    layout->addWidget(split,1);
    connect(scan,&QPushButton::clicked,this,[this]{scanSsdv();});
    connect(imageTable_,&QTableWidget::itemSelectionChanged,this,[this]{
        const int idx=imageTable_->currentRow();
        if(idx>=0&&idx<images_.size())updateImageProgress(images_[idx].imageId);
    });
    connect(save,&QPushButton::clicked,this,[this]{exportSsdv();});
    connect(decode,&QPushButton::clicked,this,[this]{decodeSsdv();});
    tabs->addTab(page,QStringLiteral("图像数据"));
}
void MainWindow::buildRaw(QTabWidget *tabs){
    auto *page=new QTabWidget;
    rawHex_=new QPlainTextEdit;
    rawHex_->setReadOnly(true);
    rawHex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawHex_->setPlainText(QStringLiteral("选择回放数据包后显示原始传输字节。"));
    page->addTab(rawHex_,QStringLiteral("KISS / HEX"));
    packetTable_=new QTableWidget(0,6);
    packetTable_->setHorizontalHeaderLabels({QStringLiteral("VCID"),
        QStringLiteral("帧号范围"),QStringLiteral("字节数"),QStringLiteral("候选 APID"),
        QStringLiteral("FEC 证据"),QStringLiteral("完整性")});
    setupTable(packetTable_);
    packetTable_->horizontalHeader()->setStretchLastSection(true);
    page->addTab(packetTable_,QStringLiteral("分包明细"));
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
