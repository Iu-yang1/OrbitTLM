#include "mainwindow.h"
#include "mainwindow_widgets.h"
#include "by04.h"
#include <QApplication>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QColor>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLineSeries>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QSet>
#include <QQuaternion>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

using orbit::BusinessPacket;
using orbit::EngineeringField;
namespace {
QString categoryFor(const QString &name){
    if(name.contains(QStringLiteral("时间"))||name.contains(QStringLiteral("时钟"))||
       name.contains(QStringLiteral("计数")))return QStringLiteral("时间与计数");
    if(name.contains(QStringLiteral("四元数"))||name.contains(QStringLiteral("姿态"))||
       name.contains(QStringLiteral("角速度"))||name.contains(QStringLiteral("飞轮"))||
       name.contains(QStringLiteral("磁场")))return QStringLiteral("姿态与测量");
    if(name.contains(QStringLiteral("温度"))||name.contains(QStringLiteral("热")))return QStringLiteral("温度与热控");
    if(name.contains(QStringLiteral("电压"))||name.contains(QStringLiteral("电流"))||
       name.contains(QStringLiteral("电源"))||name.contains(QStringLiteral("充电")))return QStringLiteral("电源与配电");
    if(name.contains(QStringLiteral("状态"))||name.contains(QStringLiteral("模式"))||
       name.contains(QStringLiteral("开关")))return QStringLiteral("状态与模式");
    return QStringLiteral("其他工程量");
}
QString shortTime(const QDateTime &time){
    return time.isValid()?time.toString("HH:mm:ss.zzz"):QStringLiteral("时间未知");
}
QString displayValue(const EngineeringField &f){
    if(!f.rawHex.isEmpty()&&(f.status=="table_raw_hex" ||
       (f.status.startsWith("raw_")&&f.value==f.rawHex)))return "0x"+f.rawHex;
    if(!f.status.startsWith("raw_")){
        bool ok=false;const double number=f.value.toDouble(&ok);
        if(ok&&std::isfinite(number))return QString::number(number,'g',8);
    }
    return f.value;
}
bool numberForPlot(const EngineeringField &f,double &value,bool &raw){
    raw=f.status=="table_raw_hex"||f.status.startsWith("raw_");
    if(f.status=="raw_nonfinite_float"||f.status=="invalid_source_bit_position")return false;
    bool ok=false;
    if(raw||f.status.startsWith("table_enum"))value=f.rawNumber.toDouble(&ok);
    else if(f.status=="table_formula"||f.status=="reference_formula"||f.status=="protocol_header_only")
        value=f.value.toDouble(&ok);
    return ok&&std::isfinite(value);
}
bool recognizedBusiness(const BusinessPacket &p){
    return !p.packetName.isEmpty()&&p.identification!="by04_ccsds_header_only";
}
QString labelStatus(const QString &s){
    if(s=="reference_formula")return QStringLiteral("参考换算");
    if(s=="by04_observed_float32")return QStringLiteral("已解析");
    if(s=="raw_by04_conversion_unknown")return QStringLiteral("原码");
    if(s=="table_formula"||s=="table_enum"||s=="table_string"||s=="table_raw_hex")
        return QStringLiteral("已解析");
    if(s=="table_enum_unknown_value")return QStringLiteral("未知枚举值");
    if(s=="invalid_source_bit_position")return QStringLiteral("原始字段定义异常");
    if(s=="raw_unsupported_formula"||s.startsWith("raw_"))return QStringLiteral("原码");
    if(s=="candidate_unverified")return QStringLiteral("实验性候选");
    if(s=="protocol_header_only")return QStringLiteral("CCSDS 协议字段");
    if(s=="raw_unverified_business")return QStringLiteral("业务含义未定义");
    return s.isEmpty()?QStringLiteral("未知"):s;
}
QTableWidgetItem *item(const QString &text,const QString &tooltip={}){
    auto *i=new QTableWidgetItem(text);
    if(!tooltip.isEmpty())i->setToolTip(tooltip);
    return i;
}
}
QString MainWindow::satellite() const{
    return satelliteSelect_?satelliteSelect_->currentData().toString():"ASRTU-1";
}
QString MainWindow::qualityLabel(const QString &value) const{
    if(value=="log_fec_reported")return QStringLiteral("日志报告 FEC 成功");
    if(value=="mixed_fec_unverified")return QStringLiteral("混合来源·部分未经验证");
    if(value=="bin_only_unverified"||value=="structural_only")
        return QStringLiteral("仅结构恢复");
    if(value=="invalid")return QStringLiteral("帧结构异常");
    return value.isEmpty()?QStringLiteral("未验证"):value;
}
QString MainWindow::sourceLabel(const QStringList &sources) const{
    bool hasLog=false,hasBin=false,hasLive=false;
    for(const auto &s:sources){
        if(s.startsWith("log:"))hasLog=true;
        if(s.startsWith("bin:"))hasBin=true;
        if(s.startsWith("live:"))hasLive=true;
    }
    QStringList labels;
    if(hasLog)labels<<QStringLiteral("日志");
    if(hasBin)labels<<QStringLiteral("BIN");
    if(hasLive)labels<<QStringLiteral("TCP");
    return labels.isEmpty()?QStringLiteral("未知"):labels.join("+");
}
QString MainWindow::utcLabel(const QDateTime &time)const{
    return time.isValid()?time.toString("yyyy-MM-dd HH:mm:ss.zzz"):
                          QStringLiteral("接收时间未知");
}
QString MainWindow::eventTitle(const BusinessPacket &packet)const{
    if(packet.identification=="by04_kiss_envelope")return packet.packetLabel;
    if(packet.identification=="by04_ccsds_header_only")
        return packet.packetLabel;
    if(packet.identification=="by04_opaque_kiss")
        return QStringLiteral("BY04 · 未分类 KISS 业务包");
    if(packet.packetName.isEmpty())return QStringLiteral("未识别数据 · %1 B").arg(packet.transport.bytes.size());
    if(!packet.packetName.isEmpty()){
        const QString &id=packet.packetName;
        if(id.startsWith("SAT"))return QStringLiteral("卫星平台遥测 · ")+id;
        if(id.startsWith("ZGK"))return QStringLiteral("姿轨控遥测 · ")+id;
        if(id.startsWith("PDB"))return QStringLiteral("配电遥测 · ")+id;
        if(id.startsWith("TCB"))return QStringLiteral("热控遥测 · ")+id;
        if(id.startsWith("GPS"))return QStringLiteral("GNSS 遥测 · ")+id;
        if(id.startsWith("SSY")||id.startsWith("SSZ"))
            return QStringLiteral("星敏感器遥测 · ")+id;
        if(id.startsWith("CAM")||id.startsWith("CMS"))
            return QStringLiteral("相机遥测 · ")+id;
        if(id.startsWith("TTC"))return QStringLiteral("测控遥测 · ")+id;
        if(id.startsWith("BBB"))return QStringLiteral("数传遥测 · ")+id;
        return QStringLiteral("遥测业务包 · ")+id;
    }
    return {};
}
const BusinessPacket *MainWindow::latestRecognized(int index,
                                                   const QStringList &prefixes)const{
    if(index<0)return nullptr;
    for(int i=std::min(index,int(business_.size())-1);i>=0;--i){
        const auto &p=business_[i];
        for(const auto &prefix:prefixes)
            if(p.packetName.startsWith(prefix)&&!p.fields.isEmpty())
                return &p;
    }
    return nullptr;
}
void MainWindow::updatePaths(){
    auto basename=[](const QString &p){return p.isEmpty()?QStringLiteral("未选择"):QFileInfo(p).fileName();};
    sourceLabel_->setText(QStringLiteral("解码日志：%1     数据帧文件：%2")
                              .arg(basename(logPath_),basename(binPath_)));
    sourceLabel_->setToolTip(QStringLiteral("LOG: %1\nBIN: %2").arg(logPath_,binPath_));
}
void MainWindow::importSession(){
    if(logPath_.isEmpty()&&binPath_.isEmpty()){
        QMessageBox::information(this,QStringLiteral("导入会话"),
                  QStringLiteral("请选择解码日志或 223 字节 BIN 文件。"));
        return;
    }
    QString error;
    if(!openSession(logPath_,binPath_,satellite(),&error))
        QMessageBox::warning(this,QStringLiteral("导入失败"),error);
}
bool MainWindow::openSession(const QString &logPath,const QString &binPath,const QString &sat,QString *failure){
    QString error;
    if((sat!="BY04"&&sat!="ASRTU-1") || (logPath.isEmpty()&&binPath.isEmpty())){
        if(failure)*failure=QStringLiteral("请选择卫星和数据文件");return false;
    }
    auto log=logPath.isEmpty()?QVector<orbit::Frame>{}:orbit::Protocol::fromLog(logPath,sat,&error);
    if(!error.isEmpty()){if(failure)*failure=error;return false;}
    auto bin=binPath.isEmpty()?QVector<orbit::Frame>{}:orbit::Protocol::fromBin(binPath,sat,&error);
    if(!error.isEmpty()){if(failure)*failure=error;return false;}
    logPath_=logPath;binPath_=binPath;
    satelliteSelect_->setCurrentIndex(satelliteSelect_->findData(sat));
    updatePaths();
    frames_=orbit::Protocol::fuse(log,bin,&frameStats_);
    if(hasCustomSchema_&&customSchema_.satellite()==sat)
        for(auto &f:frames_)customSchema_.apply(f);
    rebuildSession();
    if(!db_.save(sat+QStringLiteral(" · LOG/BIN 离线会话"),frames_,&error))
        statusBar()->showMessage(QStringLiteral("数据可回放，但未保存到 SQLite：")+error,12000);
    else statusBar()->showMessage(QStringLiteral("导入完成"),5000);
    return true;
}
void MainWindow::loadHistory(){
    QString error;
    const auto entries=db_.sessions(&error);
    if(!error.isEmpty()){QMessageBox::warning(this,QStringLiteral("SQLite 错误"),error);return;}
    if(entries.isEmpty()){
        QMessageBox::information(this,QStringLiteral("历史会话"),QStringLiteral("还没有已保存的会话。"));
        return;
    }
    QStringList choices;
    for(const auto &p:entries)choices<<QString("#%1 · %2").arg(p.first).arg(p.second);
    bool ok=false;
    const QString selected=QInputDialog::getItem(this,QStringLiteral("打开历史会话"),
        QStringLiteral("请选择会话"),choices,0,false,&ok);
    if(!ok||selected.isEmpty())return;
    const int i=choices.indexOf(selected);
    frames_=db_.load(entries[i].first,&error);
    if(!error.isEmpty()){QMessageBox::warning(this,QStringLiteral("历史会话读取失败"),error);return;}
    if(!frames_.isEmpty()){
        const QString sat=frames_.first().satellite;
        for(int j=0;j<satelliteSelect_->count();++j)
            if(satelliteSelect_->itemData(j).toString()==sat)satelliteSelect_->setCurrentIndex(j);
    }
    frameStats_={};
    frameStats_.unique=frames_.size();
    for(const auto &f:frames_){
        if(f.quality=="log_fec_reported")++frameStats_.trustedFromLog;
        else if(f.complete)++frameStats_.structuralOnly;
        else ++frameStats_.invalid;
    }
    rebuildSession();
    sourceLabel_->setText(QStringLiteral("历史会话：")+selected);
    statusBar()->showMessage(QStringLiteral("历史会话已恢复：")+selected,9000);
}
void MainWindow::rebuildSession(){
    timer_->stop();
    packets_=orbit::Transport::reassemble(frames_,&transportStats_);
    orbit::BusinessStats bs;
    business_=catalogReady_&&satellite()=="ASRTU-1"?
        catalog_.decodeAll(packets_,&bs):
        (satellite()=="BY04"?orbit::By04Decoder::decodeAll(packets_):
                             QVector<BusinessPacket>{});
    // BY04 headers are decoded independently; its physical engineering field
    // maps must not be borrowed from the ASRTU-1 catalog.
    if(business_.isEmpty()&&!packets_.isEmpty()){
        business_.reserve(packets_.size());
        for(const auto &p:packets_){
            BusinessPacket event;event.transport=p;
            event.packetLabel=QStringLiteral("待定义的卫星业务包");
            business_.push_back(event);
        }
    }
    populateEvents();
    updateFrameList();
    scanSsdv();
    updateMetrics();
    timeline_->setRange(0,std::max(0,int(business_.size())-1));
    currentIndex_=-1;
    if(!business_.isEmpty())selectEvent(0);
    else showEvent(-1);
}
void MainWindow::populateEvents(){
    QSignalBlocker blocked(eventTable_);
    eventTable_->setRowCount(business_.size());
    for(int i=0;i<business_.size();++i){
        const auto &b=business_[i];
        const QString title=eventTitle(b);
        const QString basis=b.transport.timestampBasis;
        const QString source=sourceLabel(b.transport.sources);
        const QString time=b.transport.captureLocal.isValid()?shortTime(b.transport.captureLocal):
            (b.transport.firstMaster>=0?QStringLiteral("BIN · #%1").arg(b.transport.firstMaster):QStringLiteral("无接收时间"));
        eventTable_->setItem(i,0,item(time,b.transport.captureLocal.isValid()?
            QStringLiteral("接收记录时间\n%1").arg(basis):QStringLiteral("此帧没有接收时间记录；按原始帧顺序排列")));
        eventTable_->setItem(i,1,item(title,
            QStringLiteral("应用数据包字节：%1\nCRC：未校验").arg(b.transport.bytes.size())));
        const QString status=b.identification=="by04_kiss_envelope"?
            QStringLiteral("已解析"):
            b.identification=="by04_ccsds_header_only"?
            QStringLiteral("包头"):
            (b.packetName.isEmpty()?QStringLiteral("类型未知"):
                                    QStringLiteral("已解析"));
        auto *quality=item(status,qualityLabel(b.transport.fecEvidence));
        quality->setForeground(b.packetName.isEmpty()?QColor("#73859a"):QColor("#216a9f"));
        eventTable_->setItem(i,2,quality);
        eventTable_->setItem(i,3,item(source));
    }
    applyEventFilter();
}
void MainWindow::applyEventFilter(){
    if(!eventTable_)return;
    const QString query=eventSearch_->text().trimmed();
    const bool recognized=recognizedOnly_->isChecked();
    for(int row=0;row<eventTable_->rowCount();++row){
        const bool matchKind=!recognized || recognizedBusiness(business_[row]);
        bool matchText=query.isEmpty();
        for(int col=0;col<eventTable_->columnCount()&&!matchText;++col){
            auto *i=eventTable_->item(row,col);
            if(i&&i->text().contains(query,Qt::CaseInsensitive))matchText=true;
        }
        eventTable_->setRowHidden(row,!(matchKind&&matchText));
    }
}
void MainWindow::selectEvent(int index){
    if(index<0||index>=business_.size()){showEvent(-1);return;}
    currentIndex_=index;
    {
        QSignalBlocker block(eventTable_);
        eventTable_->selectRow(index);
    }
    {
        QSignalBlocker block(timeline_);
        timeline_->setValue(index);
    }
    showEvent(index);
}
void MainWindow::seekVisible(int direction){
    if(business_.isEmpty()){timer_->stop();return;}
    const int from=currentIndex_ < 0?(direction>0?-1:int(business_.size())):currentIndex_;
    for(int i=from+direction;i>=0&&i<business_.size();i+=direction){
        if(!eventTable_->isRowHidden(i)){selectEvent(i);return;}
    }
    timer_->stop();
}
void MainWindow::showEvent(int index){
    positionLabel_->setText(index<0?QStringLiteral("0 / %1").arg(business_.size()):
                          QString("%1 / %2").arg(index+1).arg(business_.size()));
    updateOverview(index);
    populateEngineering(index);
    updateAttitude(index);
    updateRawPacket(index);
}
void MainWindow::updateMetrics(){
    QDateTime earliest,latest;
    for(const auto &f:frames_)if(f.captureLocal.isValid()){
        if(!earliest.isValid()||f.captureLocal<earliest)earliest=f.captureLocal;
        if(!latest.isValid()||f.captureLocal>latest)latest=f.captureLocal;
    }
    if(earliest.isValid()){
        timeMetric_->setText(earliest.date()==latest.date()?
            earliest.toString("HH:mm")+" — "+latest.toString("HH:mm"):
            earliest.toString("MM-dd HH:mm")+"—"+latest.toString("MM-dd HH:mm"));
    }else timeMetric_->setText(QStringLiteral("未记录"));
    frameMetric_->setText(QString::number(frames_.size()));
    int good=0;
    for(const auto &p:business_)
        if(recognizedBusiness(p))++good;
    const bool by04=satellite()=="BY04";
    recognizedOnly_->setText(by04?QStringLiteral("仅显示已识别数据"):
                                  QStringLiteral("仅显示已识别数据"));
    auto label=[](QLabel *value){
        return value->parentWidget()->findChild<QLabel*>("subtitle");
    };
    if(auto *caption=label(recognizedMetric_))
        caption->setText(by04?QStringLiteral("已识别业务包"):
                             QStringLiteral("已识别业务包"));
    if(auto *caption=label(unknownMetric_))
        caption->setText(by04?QStringLiteral("未分类 KISS 业务包"):
                             QStringLiteral("未识别业务包"));
    recognizedMetric_->setText(QString::number(good));
    unknownMetric_->setText(QString::number(business_.size()-good));
    imageMetric_->setText(QString::number(images_.size()));
    transportMetric_->setText(QStringLiteral("%1 包").arg(packets_.size()));
    message_->setText(satellite()=="BY04"?
        QStringLiteral("BY04 · %1 包 · 图像 %2")
            .arg(packets_.size()).arg(images_.size()):
        QStringLiteral("%1 包 · 已识别 %2 · 未识别 %3")
            .arg(packets_.size()).arg(good).arg(business_.size()-good));
}
void MainWindow::updateOverview(int index){
    if(index<0||index>=business_.size()){
        currentTitle_->setText(QStringLiteral("尚未导入数据"));
        currentMeta_->setText(QStringLiteral("选择 LOG 或 BIN，然后导入"));
        integrityLabel_->setText(QStringLiteral("离线会话"));
    }else{
        const auto &b=business_[index];
        currentTitle_->setText(eventTitle(b));
        currentMeta_->setText(QStringLiteral("记录时间：%1　　来源：%2　　虚拟信道：%3")
            .arg(utcLabel(b.transport.captureLocal),sourceLabel(b.transport.sources))
            .arg(b.transport.vcid));
        integrityLabel_->setText(QStringLiteral("%1 · CRC 未校验")
           .arg(qualityLabel(b.transport.fecEvidence)));
    }
    updateFieldCard(powerCard_,{"PDB","SAT"},index);
    updateFieldCard(attitudeCard_,{"SAT-S3","ZGK","SSY","SSZ","GPS"},index);
    updateFieldCard(thermalCard_,{"TCB"},index);
    updateFieldCard(payloadCard_,{"CAM","CMS","TTC","BBB"},index);
}
void MainWindow::updateFieldCard(QGroupBox *card,const QStringList &prefixes,int index){
    auto *content=card->findChild<QLabel*>("statusContent");
    if(!content)return;
    const BusinessPacket *chosen=latestRecognized(index,satellite()=="BY04"?
        (card==payloadCard_?QStringList{"BY04-BUV"}:QStringList{"BY04-OBC"}):prefixes);
    if(!chosen){
        content->setText(QStringLiteral("暂无数据"));
        return;
    }
    QString text=QStringLiteral("<span style='color:#68809b'>来源 %1 · 接收 %2</span><br>")
        .arg(chosen->packetName.toHtmlEscaped(),
             shortTime(chosen->transport.captureLocal).toHtmlEscaped());
    int shown=0;
    for(const auto &f:chosen->fields){
        if(satellite()=="BY04"){
            const QString group=categoryFor(f.nameCn);
            if(card==powerCard_&&group!=QStringLiteral("电源与配电"))continue;
            if(card==attitudeCard_&&!f.nameCn.startsWith(QStringLiteral("姿态四元数")))continue;
            if(card==thermalCard_&&group!=QStringLiteral("温度与热控"))continue;
            if(card==payloadCard_&&!f.nameCn.contains(QStringLiteral("接收信号"))&&
               !f.nameCn.contains(QStringLiteral("接收频偏")))continue;
        }
        if(f.value.isEmpty()||f.status.startsWith("raw_")||f.status=="invalid_source_bit_position")
            continue;
        if(f.nameCn.isEmpty())continue;
        text+=QStringLiteral("<div style='margin-top:5px'><b>%1</b>：%2 %3</div>")
            .arg(f.nameCn.toHtmlEscaped(),displayValue(f).toHtmlEscaped(),
                f.unit=="1"?QString():f.unit.toHtmlEscaped());
        if(++shown>=4)break;
    }
    if(!shown)text+=QStringLiteral("字段存在，但暂未取得可显示的工程量");

    content->setText(text);
}
void MainWindow::populateEngineering(int index){
    const auto *previous=fieldTree_->currentItem();
    const QString previousCode=previous?previous->data(0,Qt::UserRole).toString():QString();
    const QString previousPacket=previous?previous->data(0,Qt::UserRole+1).toString():QString();
    QSignalBlocker block(engineeringTable_);
    engineeringTable_->setRowCount(0);
    fieldTree_->clear();
    fieldDetail_->clear();fieldDetail_->setToolTip({});
    if(index<0||index>=business_.size()){
        engineeringSummary_->setText(QStringLiteral("未选择数据"));
        plotEngineering({},{},-1);return;
    }
    int highlighted=-1;
    for(int i=0;i<=index && i<business_.size();++i){
        const auto &p=business_[i];
        if(p.packetName.isEmpty())continue;
        const int row=engineeringTable_->rowCount();
        engineeringTable_->insertRow(row);
        auto *time=item(shortTime(p.transport.captureLocal));
        time->setData(Qt::UserRole,i);
        engineeringTable_->setItem(row,0,time);
        engineeringTable_->setItem(row,1,item(p.packetName));
        engineeringTable_->setItem(row,2,item(QStringLiteral("%1 项").arg(p.fields.size())));
        highlighted=i;
    }
    const auto *chosen=&business_[index];
    if(chosen->fields.isEmpty()){
        engineeringSummary_->setText(QStringLiteral("%1 · 无业务字段").arg(eventTitle(*chosen)));
        engineeringSummary_->setToolTip(QStringLiteral("完整字节保留在“原始数据”中"));
        plotEngineering({},{},-1);
        return;
    }
    engineeringSummary_->setText(QStringLiteral("%1  /  %2 项参数")
        .arg(eventTitle(*chosen)).arg(chosen->fields.size()));
    engineeringSummary_->setToolTip(chosen->definitionEvidence);
    QHash<QString,QTreeWidgetItem*> groups;
    QSignalBlocker treeBlocked(fieldTree_);
    QTreeWidgetItem *selected=nullptr;int bestScore=-1;bool retained=false;
    const QString needle=engineeringSearch_->text();
    for(const auto &f:chosen->fields){
        const QString group=categoryFor(f.nameCn);
        auto *root=groups.value(group,nullptr);
        if(!root){
            root=new QTreeWidgetItem(fieldTree_,{group,"","",""});
            auto font=root->font(0);font.setBold(true);root->setFont(0,font);
            groups.insert(group,root);
        }
        auto *field=new QTreeWidgetItem(root,{
            f.nameCn.isEmpty()?f.code:f.nameCn,displayValue(f),f.unit=="1"?QString():f.unit,labelStatus(f.status)});
        field->setData(0,Qt::UserRole,f.code);
        field->setData(0,Qt::UserRole+1,chosen->packetName);
        field->setToolTip(0,f.code+"\n"+f.nameEn+"\n"+f.bits+"\n"+f.dataType+"\n"+f.definitionEvidence);
        field->setToolTip(1,QStringLiteral("原始 HEX：")+f.rawHex);
        if(needle.isEmpty()||f.nameCn.contains(needle,Qt::CaseInsensitive)||f.code.contains(needle,Qt::CaseInsensitive)){
            if(f.code==previousCode&&chosen->packetName==previousPacket){selected=field;retained=true;}
            else if(!retained){
                double number=0;bool raw=false;int score=0;
                if(numberForPlot(f,number,raw)){
                    score=raw?1:2;
                    if(!f.unit.isEmpty())score+=1;
                    if(f.nameCn.contains(QStringLiteral("温度"))||f.nameCn.contains(QStringLiteral("电压"))||
                       f.nameCn.contains(QStringLiteral("四元数")))score+=3;
                }
                if(score>bestScore){bestScore=score;selected=field;}
            }
        }
    }
    fieldTree_->expandAll();
    applyEngineeringFilter();
    if(highlighted>=0){
        for(int row=0;row<engineeringTable_->rowCount();++row){
            auto *i=engineeringTable_->item(row,0);
            if(i&&i->data(Qt::UserRole).toInt()==highlighted){
                engineeringTable_->selectRow(row);break;
            }
        }
    }
    treeBlocked.unblock();
    if(selected){fieldTree_->setCurrentItem(selected);fieldTree_->scrollToItem(selected);}
    else plotEngineering({},{},-1);
}
void MainWindow::applyEngineeringFilter(){
    const QString needle=engineeringSearch_->text();
    for(int i=0;i<fieldTree_->topLevelItemCount();++i){
        auto *group=fieldTree_->topLevelItem(i);int shown=0;
        for(int j=0;j<group->childCount();++j){
            auto *field=group->child(j);
            const bool matches=needle.isEmpty()||field->text(0).contains(needle,Qt::CaseInsensitive)||
                field->data(0,Qt::UserRole).toString().contains(needle,Qt::CaseInsensitive);
            field->setHidden(!matches);if(matches)++shown;
        }
        group->setHidden(shown==0);
    }
}
void MainWindow::plotEngineering(const QString &packet,const QString &code,int upTo){
    auto *chart=new QChart;
    chart->legend()->hide();
    chart->setBackgroundVisible(false);
    if(!code.isEmpty()&&upTo>=0){
        auto *series=new QLineSeries;
        QDateTime first;
        bool timed=true,rawSeries=false;QString name=code;
        for(int i=0;i<=upTo&&i<business_.size();++i){
            const auto &p=business_[i];if(p.packetName!=packet)continue;
            for(const auto &f:p.fields)if(f.code==code){
                double value=0;bool raw=false;
                if(numberForPlot(f,value,raw)&&!p.transport.captureLocal.isValid())timed=false;
            }
        }
        for(int i=0;i<=upTo&&i<business_.size();++i){
            const auto &p=business_[i];
            if(p.packetName!=packet)continue;
            for(const auto &f:p.fields)if(f.code==code){
                double value=0;bool raw=false;
                if(!numberForPlot(f,value,raw))continue;
                rawSeries=rawSeries||raw;name=f.nameCn;
                if(!first.isValid()&&p.transport.captureLocal.isValid())
                    first=p.transport.captureLocal;
                const qreal elapsed=(timed&&first.isValid())?
                    qreal(first.msecsTo(p.transport.captureLocal))/1000.0 : qreal(i);
                series->append(elapsed,value);
            }
        }
        if(series->count()){
            series->setPointsVisible(series->count()==1);
            chart->addSeries(series);chart->createDefaultAxes();
            chart->setTitle(name+(rawSeries?QStringLiteral(" · 原码"):QString())+
                (timed?QStringLiteral("（接收相对秒）"):QStringLiteral("（包序）")));
        }else{
            delete series;
            chart->setTitle(QStringLiteral("所选参数暂无可用数值序列"));
        }
    }else chart->setTitle(QStringLiteral("当前数据没有可绘制的业务参数"));
    engineeringChart_->setChart(chart);
}
void MainWindow::updateAttitude(int index){
    const bool by04=satellite()=="BY04";
    const QString packetName=by04?QStringLiteral("BY04-OBC-SF"):QStringLiteral("SAT-S3");
    const QString quaternionName=by04?QStringLiteral("姿态四元数q%1"):QStringLiteral("解算四元数q%1");
    const auto *last=latestRecognized(index,{packetName});
    bool has[4]{false,false,false,false};
    double q[4]{};
    if(last){
        for(const auto &f:last->fields)for(int j=0;j<4;j++)
            if(f.nameCn==quaternionName.arg(j)&&
               f.status=="table_formula"){
                bool ok=false;const double value=f.value.toDouble(&ok);
                if(ok&&std::isfinite(value)){q[j]=value;has[j]=true;}
            }
    }
    const bool ready=std::all_of(std::begin(has),std::end(has),[](bool a){return a;});
    double norm=0;
    for(int i=0;i<4;i++){
        quaternion_[i]->setText(ready?QString::number(q[i],'f',5):QStringLiteral("—"));
        norm+=q[i]*q[i];
    }
    norm=std::sqrt(norm);
    normLabel_->setText(ready?QString::number(norm,'f',6):QStringLiteral("—"));
    attitudeWidget_->setQuaternion(
        QQuaternion(float(q[0]),float(q[1]),float(q[2]),float(q[3])),
        ready&&std::abs(norm-1.0)<0.05);
    attitudeInfo_->setText(ready?QStringLiteral("%1 · %2")
        .arg(packetName,shortTime(last->transport.captureLocal)):QStringLiteral("暂无姿态数据"));
    attitudeInfo_->setToolTip(QStringLiteral("相对姿态示意，q0 为标量；参考坐标系和天线安装矩阵未定义"));
    auto *chart=new QChart;
    chart->setBackgroundVisible(false);
    QVector<QLineSeries*> series;
    for(int k=0;k<4;k++){auto *s=new QLineSeries;s->setName(QString("q%1").arg(k));series.push_back(s);}
    QDateTime first;
    if(index>=0)for(int i=0;i<=index&&i<business_.size();++i){
        const auto &p=business_[i];
        if(p.packetName!=packetName)continue;
        double values[4]{};bool found[4]{};
        for(const auto &f:p.fields)for(int j=0;j<4;j++)
            if(f.nameCn==quaternionName.arg(j)&&f.status=="table_formula"){
                bool ok=false;const double val=f.value.toDouble(&ok);
                if(ok&&std::isfinite(val)){values[j]=val;found[j]=true;}
            }
        if(!std::all_of(std::begin(found),std::end(found),[](bool x){return x;}))continue;
        if(!first.isValid()&&p.transport.captureLocal.isValid())first=p.transport.captureLocal;
        const qreal x=(first.isValid()&&p.transport.captureLocal.isValid())?
            qreal(first.msecsTo(p.transport.captureLocal))/1000.0:qreal(i);
        for(int j=0;j<4;j++)series[j]->append(x,values[j]);
    }
    const bool nonempty=series[0]->count()>0;
    for(auto *s:series){if(nonempty)chart->addSeries(s);else delete s;}
    if(nonempty){chart->createDefaultAxes();chart->legend()->setVisible(true);}
    chart->setTitle(nonempty?QStringLiteral("解算四元数时序（相对记录时间）"):
        QStringLiteral("暂无姿态时序"));
    attitudeChart_->setChart(chart);
}
void MainWindow::updateRawPacket(int index){
    if(index<0||index>=business_.size()){
        rawHex_->setPlainText(QStringLiteral("当前没有可检查的 KISS 应用包。"));
        return;
    }
    const auto &packet=business_[index];
    QString payload=QStringLiteral("当前包：%1\n识别方式：%2\nCRC：未校验\nFEC 来源：%3\n"
                   "KISS 字节数：%4\n接收时间：%5\n\n完整 KISS 解码内容：\n%6")
        .arg(eventTitle(packet),packet.identification,
             qualityLabel(packet.transport.fecEvidence))
        .arg(packet.transport.bytes.size())
        .arg(utcLabel(packet.transport.captureLocal),
             QString::fromLatin1(packet.transport.bytes.toHex(' ').toUpper()));
    if(!packet.transport.data.isEmpty())
        payload+=QStringLiteral("\n\n内部 Space Packet 候选数据：\n")+
                 QString::fromLatin1(packet.transport.data.toHex(' ').toUpper());
    rawHex_->setPlainText(payload);
}
void MainWindow::updateFrameList(){
    QSignalBlocker blocked(frameTable_);
    frameTable_->setRowCount(frames_.size());
    for(int i=0;i<frames_.size();++i){
        const auto &f=frames_[i];
        const QStringList cells={shortTime(f.captureLocal),
            f.master<0?"—":QString::number(f.master),
            f.vcid<0?"—":QString::number(f.vcid),
            f.apid<0?"—":QString("0x%1").arg(f.apid,3,16,QChar('0')).toUpper(),
            f.sequence<0?"—":QString::number(f.sequence),
            sourceLabel(f.sources)+" · "+qualityLabel(f.quality),
            f.complete?QStringLiteral("结构可读"):QStringLiteral("结构异常")};
        for(int j=0;j<cells.size();++j)
            frameTable_->setItem(i,j,item(cells[j]));
    }
    QSignalBlocker packetBlocked(packetTable_);
    packetTable_->setRowCount(packets_.size());
    for(int i=0;i<packets_.size();++i){
        const auto &p=packets_[i];
        const QStringList values={QString::number(p.vcid),
            QString("%1 — %2").arg(p.firstMaster).arg(p.lastMaster),
            QString::number(p.bytes.size()),
            p.apid<0?"—":QString("0x%1").arg(p.apid,3,16,QChar('0')).toUpper(),
            qualityLabel(p.fecEvidence),QStringLiteral("CRC 未校验")};
        for(int j=0;j<values.size();++j)
            packetTable_->setItem(i,j,item(values[j]));
    }
    diagnosticsSummary_->setText(
        QStringLiteral("传输帧 %1 条 · KISS %2 包 · 计数缺口中断 %3 次 · 非法转义 %4 次 · "
        "超长包 %5 个 · EOF 未完成 %6 个")
        .arg(frames_.size()).arg(packets_.size())
        .arg(transportStats_.interruptedByCounterGap)
        .arg(transportStats_.malformedEscapePackets)
        .arg(transportStats_.oversizedPackets)
        .arg(transportStats_.incompleteAtEof));
}
void MainWindow::inspectFrame(int index){
    if(index<0||index>=frames_.size()){debugText_->clear();return;}
    const auto &f=frames_[index];
    const int scid=f.bytes.size()>=2?
        ((quint8(f.bytes[0])&0x3f)<<4)|(quint8(f.bytes[1])>>4):-1;
    QString result=QStringLiteral("接收时间：%1\n记录时间依据：%2\n航天器标识 SCID：0x%3\nMFC：%4\n虚拟信道：%5\n"
        "候选 APID：%6\nFEC/来源：%7\n帧质量：%8\n校验：应用层 CRC 未执行\n"
        "诊断：%9\nSHA-256：%10\n\n原始 223 字节信息帧：\n%11")
        .arg(utcLabel(f.captureLocal),f.timeBasis)
        .arg(scid,3,16,QChar('0')).arg(f.master).arg(f.vcid).arg(f.apid)
        .arg(sourceLabel(f.sources)).arg(qualityLabel(f.quality))
        .arg(f.error,f.hash,QString::fromLatin1(f.bytes.toHex(' ').toUpper()));
    debugText_->setPlainText(result);
}
void MainWindow::setDiagnosticsVisible(bool visible){
    if(pages_&&diagnosticTab_>=0)pages_->setTabVisible(diagnosticTab_,visible);
    if(navigation_&&diagnosticTab_>=0)navigation_->setTabVisible(diagnosticTab_,visible);
}
void MainWindow::scanSsdv(){
    if(!imageApid_)return;
    const bool by04=satellite()=="BY04";
    images_=by04?orbit::By04Decoder::images(frames_):
                 orbit::ImageTools::candidatesFromKiss(packets_,imageApid_->value());
    imageApid_->setEnabled(!by04);
    imageTable_->setRowCount(images_.size());
    for(int i=0;i<images_.size();++i){
        const auto &p=images_[i];
        const QStringList row={QString::number(p.imageId),
            QString::number(p.packetId),
            QString("%1 × %2").arg(p.width).arg(p.height),
            p.status=="dslwp_crc32_pass"?QStringLiteral("DSLWP CRC32 通过"):
            (p.status=="dslwp_crc32_failed"?QStringLiteral("DSLWP CRC32 失败"):
                                               QStringLiteral("候选，CRC 未校验"))};
        for(int j=0;j<row.size();++j)imageTable_->setItem(i,j,item(row[j]));
    }
    imageInfo_->setText(by04?
        QStringLiteral("BY04 · VC1 218 字节 DSLWP SSDV · %1 个有效结构分片；仅通过独立 DSLWP CRC 的分片标记为已校验。")
            .arg(images_.size()):
        QStringLiteral("APID %1：发现 %2 个 SSDV 候选分片；JPEG 还原需要外部解码工具。")
            .arg(imageApid_->value()).arg(images_.size()));
    if(imageMetric_)imageMetric_->setText(QString::number(images_.size()));
    if(!images_.isEmpty())updateImageProgress(images_.first().imageId);
    else{
        imageCoverage_->setRange(0,1);
        imageCoverage_->setValue(0);
        imageCoverage_->setFormat(QStringLiteral("尚无观测序号"));
        imageCoverageNote_->setText(QStringLiteral("尚无候选分片 · 总包数未知"));
    }
}
void MainWindow::updateImageProgress(int imageId){
    QSet<int> unique;
    int lo=std::numeric_limits<int>::max(),hi=-1;
    for(const auto &p:images_)if(p.imageId==imageId){
        unique.insert(p.packetId);
        lo=std::min(lo,p.packetId);hi=std::max(hi,p.packetId);
    }
    if(hi<lo)return;
    const int span=hi-lo+1;
    const int missing=span-unique.size();
    imageCoverage_->setRange(0,std::max(1,span));
    imageCoverage_->setValue(unique.size());
    imageCoverage_->setFormat(QStringLiteral("已观测 %v / %m 个序号"));
    imageCoverageNote_->setText(QStringLiteral(
       "图像 ID %1 · 观测包序号 %2–%3 · 范围内缺口 %4 个 · 总包数未知，"
       "此进度只反映观测序号跨度，不是图片完整度")
       .arg(imageId).arg(lo).arg(hi).arg(missing));
}
void MainWindow::exportSsdv(){
    if(images_.isEmpty()){
        QMessageBox::information(this,QStringLiteral("保存图像数据"),
            QStringLiteral("当前没有 SSDV 候选分片，请先扫描正确的图像 APID。"));return;
    }
    const QString path=QFileDialog::getSaveFileName(this,
       QStringLiteral("保存 SSDV 候选数据"),"satellite.ssdv",
       QStringLiteral("SSDV 数据 (*.ssdv *.bin)"));
    if(path.isEmpty())return;
    QString error;
    if(!orbit::ImageTools::savePackets(images_,path,&error))
        QMessageBox::warning(this,QStringLiteral("保存失败"),error);
    else statusBar()->showMessage(QStringLiteral("已保存候选分片；未独立完成图像 CRC 验证"),9500);
}
void MainWindow::decodeSsdv(){
    const QString tool=QFileDialog::getOpenFileName(this,
        QStringLiteral("选择可信任的 SSDV 外部解码程序"));
    if(tool.isEmpty())return;
    const QString input=QFileDialog::getOpenFileName(this,
        QStringLiteral("选择已提取的 SSDV 数据文件"),{},
        QStringLiteral("SSDV (*.ssdv *.bin);;全部文件 (*)"));
    if(input.isEmpty())return;
    const QString output=QFileDialog::getSaveFileName(this,
        QStringLiteral("指定 JPEG 输出文件"),"decoded.jpg",
        QStringLiteral("JPEG 图像 (*.jpg *.jpeg)"));
    if(output.isEmpty())return;
    auto *process=new QProcess(this);
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this,process,output](int exitCode,QProcess::ExitStatus status){
            QPixmap pixmap(output);
            if(exitCode==0&&status==QProcess::NormalExit&&!pixmap.isNull()){
                decodedImage_=pixmap;imagePath_=output;
                imagePreview_->setImage(decodedImage_);
                imageInfo_->setText(QStringLiteral("JPEG 已由外部工具生成：%1 × %2 像素。"
                    "这不代表星载应用 CRC 已经验证。")
                    .arg(pixmap.width()).arg(pixmap.height()));
                statusBar()->showMessage(QStringLiteral("外部 JPEG 解码完成"),9000);
            }else{
                QMessageBox::warning(this,QStringLiteral("SSDV 解码失败"),
                    QStringLiteral("外部工具未成功输出可读取的 JPEG。\n")+
                    QString::fromUtf8(process->readAllStandardError()));
            }
            process->deleteLater();
        });
    process->start(tool,{"-d",input,output});
}
void MainWindow::exportFrames(bool csv){
    if(frames_.isEmpty())return;
    const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出原始传输帧"),
        csv?"frames.csv":"frames.json",csv?QStringLiteral("CSV (*.csv)"):QStringLiteral("JSON (*.json)"));
    if(path.isEmpty())return;
    QString error;
    const bool ok=csv?orbit::Protocol::exportCsv(path,frames_,&error):
                      orbit::Protocol::exportJson(path,frames_,&error);
    if(!ok)QMessageBox::warning(this,QStringLiteral("导出失败"),error);
    else statusBar()->showMessage(QStringLiteral("原始传输帧已导出"),8000);
}
void MainWindow::exportEngineering(bool csv){
    if(business_.isEmpty()){
        QMessageBox::information(this,QStringLiteral("业务遥测"),
             QStringLiteral("暂无可导出的业务包；请先导入会话。"));return;
    }
    const QString path=QFileDialog::getSaveFileName(this,
        QStringLiteral("导出业务遥测字段"),
        csv?"engineering.csv":"engineering.json",
        csv?QStringLiteral("CSV (*.csv)"):QStringLiteral("JSON (*.json)"));
    if(path.isEmpty())return;
    QString error;
    const bool ok=csv?orbit::BusinessCatalog::exportCsv(path,business_,&error):
                      orbit::BusinessCatalog::exportJson(path,business_,&error);
    if(!ok)QMessageBox::warning(this,QStringLiteral("导出失败"),error);
    else statusBar()->showMessage(QStringLiteral("业务遥测已按已知接收时间排序导出"),8000);
}
void MainWindow::exportQuaternions(bool experimental){
    if(frames_.isEmpty())return;
    const QString path=QFileDialog::getSaveFileName(this,
        experimental?QStringLiteral("实验性候选四元数（非正式姿态）"):
                     QStringLiteral("四元数 CSV"),
        experimental?"candidate-quaternion.csv":"quaternions.csv",
        QStringLiteral("CSV (*.csv)"));
    if(path.isEmpty())return;
    QString error;
    const bool ok=experimental?
        orbit::Protocol::exportAttitudeCsv(path,frames_,&error):
        orbit::BusinessCatalog::exportQuaternionCsv(path,business_,&error);
    if(!ok)QMessageBox::warning(this,QStringLiteral("导出失败"),error);
    else statusBar()->showMessage(QStringLiteral("四元数已导出"),9000);
}
void MainWindow::exportKiss(){
    if(packets_.isEmpty())return;
    const QString path=QFileDialog::getSaveFileName(this,QStringLiteral("导出 KISS 应用包"),
                                                     "kiss-packets.json",QStringLiteral("JSON (*.json)"));
    if(path.isEmpty())return;
    QString error;
    if(!orbit::Transport::exportJson(path,packets_,&error))
        QMessageBox::warning(this,QStringLiteral("导出失败"),error);
    else statusBar()->showMessage(QStringLiteral("KISS 应用包已导出"),8000);
}
