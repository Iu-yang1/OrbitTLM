#include "mainwindow.h"
#include <QtTest/QtTest>
#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <QTabWidget>
#include <QTabBar>
#include <QTreeWidget>
#include <QCheckBox>
#include <QTemporaryDir>
#include <QFile>
#include <QFontDatabase>
#include <QChartView>
#include <QChart>
#include <QLineSeries>
#include <QLineEdit>
#include <QStandardPaths>

class UiSmoke:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){
#ifdef Q_OS_WIN
        // Windows' offscreen platform has no system font database.
        if(qEnvironmentVariable("QT_QPA_PLATFORM")=="offscreen"){
            const QString fonts=qEnvironmentVariable("WINDIR")+"/Fonts/";
            const int id=QFontDatabase::addApplicationFont(fonts+"msyh.ttc");
            if(id>=0)QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first(),10));
        }
#endif
    }
    void startupShowsChineseOverviewAndHidesDebug() {
        QStandardPaths::setTestModeEnabled(true);
        MainWindow window;
        window.show();
        QApplication::processEvents();
        // Best-effort offscreen render snapshot, not a prerequisite for tests.
        const QPixmap preview=window.grab();
        if(!preview.isNull())
            preview.save(QApplication::applicationDirPath()+"/OrbitTLM-UI-preview.png");
        const auto *tabs=window.findChild<QTabWidget*>("mainPages");
        QVERIFY(tabs!=nullptr);
        QVERIFY(tabs->count()>=6);
        QCOMPARE(tabs->tabText(0),QStringLiteral("总览"));
        QCOMPARE(tabs->tabText(1),QStringLiteral("业务遥测"));
        QCOMPARE(tabs->tabText(2),QStringLiteral("姿态与轨道"));
        QCOMPARE(tabs->tabText(3),QStringLiteral("图像数据"));
        QCOMPARE(tabs->tabText(4),QStringLiteral("原始数据"));
        QVERIFY(!tabs->isTabVisible(tabs->count()-1));
        const auto *event=window.findChild<QTableWidget*>("eventTimeline");
        QVERIFY(event!=nullptr);
        QCOMPARE(event->columnCount(),4);
        QCOMPARE(event->horizontalHeaderItem(0)->text(),QStringLiteral("时间"));
        QCOMPARE(event->horizontalHeaderItem(1)->text(),QStringLiteral("数据包 / 事件"));
        QVERIFY(event->rowCount()==0);
        const auto *overview=window.findChild<QLabel*>("currentHeading");
        QVERIFY(overview!=nullptr);
        QVERIFY(overview->text().contains(QStringLiteral("尚未")));
    }
    void switchingTabsWithEmptyDataDoesNotCrash() {
        MainWindow window;
        auto *tabs=window.findChild<QTabWidget*>("mainPages");
        QVERIFY(tabs!=nullptr);
        for(int i=0;i<tabs->count()-1;i++){
            tabs->setCurrentIndex(i);
            QCOMPARE(tabs->currentIndex(),i);
        }
    }
    void by04ImportNavigationAndCompactFields(){
        QFile source(QApplication::applicationDirPath()+"/fixtures/by04_business_capture.b64");
        QVERIFY(source.open(QIODevice::ReadOnly));
        QTemporaryDir temp;const auto path=temp.filePath("by04.bin");
        QFile bin(path);QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write(QByteArray::fromBase64(source.readAll()));bin.close();
        MainWindow window;QString error;
        QVERIFY2(window.openSession(qEnvironmentVariable("ORBITTLM_PREVIEW_LOG"),path,"BY04",&error),qPrintable(error));
        window.show();QApplication::processEvents();
        auto *events=window.findChild<QTableWidget*>("eventTimeline");QVERIFY(events);
        int row=-1;
        for(int i=0;i<events->rowCount();++i)
            if(events->item(i,1)->text().startsWith(QStringLiteral("计算机"))){row=i;break;}
        QVERIFY(row>=0);events->selectRow(row);
        auto *navigation=window.findChild<QTabBar*>("workspaceNavigation");QVERIFY(navigation);
        auto *pages=window.findChild<QTabWidget*>("mainPages");QVERIFY(pages);
        QVERIFY(!navigation->isTabVisible(navigation->count()-1));
        navigation->setCurrentIndex(1);QCOMPARE(pages->currentIndex(),1);
        auto *fields=window.findChild<QTreeWidget*>("engineeringFields");QVERIFY(fields);
        auto *details=window.findChild<QCheckBox*>("fieldDetailsToggle");QVERIFY(details);
        QVERIFY(!details->isChecked());QVERIFY(fields->isColumnHidden(3));
        const auto q=fields->findItems(QStringLiteral("姿态四元数q0"),Qt::MatchExactly|Qt::MatchRecursive);
        QCOMPARE(q.size(),1);QVERIFY(qAbs(q[0]->text(1).toDouble()-0.21687696874141693)<1e-8);
        for(int i=events->rowCount()-1;i>=0;--i)
            if(events->item(i,1)->text().startsWith(QStringLiteral("计算机"))){events->selectRow(i);break;}
        navigation->setCurrentIndex(0);QApplication::processEvents();
        window.grab().save(QApplication::applicationDirPath()+"/OrbitTLM-BY04-overview.png");
        navigation->setCurrentIndex(1);
        const auto lastQ=fields->findItems(QStringLiteral("姿态四元数q0"),Qt::MatchExactly|Qt::MatchRecursive);
        QCOMPARE(lastQ.size(),1);
        fields->setCurrentItem(lastQ[0]);QApplication::processEvents();
        window.grab().save(QApplication::applicationDirPath()+"/OrbitTLM-BY04-fields.png");
        navigation->setCurrentIndex(2);QCOMPARE(pages->currentIndex(),2);
        QApplication::processEvents();
        window.grab().save(QApplication::applicationDirPath()+"/OrbitTLM-BY04-attitude.png");
        window.resize(1050,690);QApplication::processEvents();
        QVERIFY2(window.width()<=1100,"Layout expanded beyond the compact desktop width");
        window.grab().save(QApplication::applicationDirPath()+"/OrbitTLM-BY04-compact.png");
        const int before=events->rowCount();
        QVERIFY(!window.openSession({},temp.filePath("missing.bin"),"BY04",&error));
        QCOMPARE(events->rowCount(),before);
    }
    void asrtuUntimedFragmentDoesNotDisplayPreviousTelemetry(){
        QFile source(QApplication::applicationDirPath()+"/fixtures/asrtu_capture_20261024.b64");
        QVERIFY(source.open(QIODevice::ReadOnly));
        QTemporaryDir temp;const QString path=temp.filePath("asrtu.bin");
        QFile bin(path);QVERIFY(bin.open(QIODevice::WriteOnly));
        bin.write(QByteArray::fromBase64(source.readAll()));bin.close();
        MainWindow window;window.show();QString error;
        const QString log=QApplication::applicationDirPath()+"/fixtures/asrtu_capture_20261024_fec.txt";
        auto *events=window.findChild<QTableWidget*>("eventTimeline");QVERIFY(events);
        auto *fields=window.findChild<QTreeWidget*>("engineeringFields");QVERIFY(fields);
        auto *summary=window.findChild<QLabel*>("engineeringSummary");QVERIFY(summary);
        auto *detail=window.findChild<QLabel*>("fieldDetail");QVERIFY(detail);
        auto *chart=window.findChild<QChartView*>("engineeringChart");QVERIFY(chart);
        auto *pages=window.findChild<QTabBar*>("workspaceNavigation");QVERIFY(pages);
        auto *search=window.findChild<QLineEdit*>("engineeringSearch");QVERIFY(search);
        for(int repeat=0;repeat<3;++repeat){
            QVERIFY2(window.openSession(log,path,"ASRTU-1",&error),qPrintable(error));
            pages->setCurrentIndex(1);QCOMPARE(events->rowCount(),88);
            int pdb=-1,unknown=-1,sat=-1,nextSat=-1,untimed=0;
            for(int row=0;row<events->rowCount();++row){
                const auto title=events->item(row,1)->text();
                if(title.contains("PDB-S0"))pdb=row;
                if(title.contains("SAT-S0")){if(sat<0)sat=row;else if(nextSat<0)nextSat=row;}
                if(events->item(row,0)->text().startsWith("BIN")){++untimed;if(unknown<0)unknown=row;}
            }
            QCOMPARE(untimed,10);QVERIFY(pdb>=0&&unknown>0&&sat>=0&&nextSat>=0);
            QCOMPARE(events->item(unknown,0)->text(),QStringLiteral("BIN · #105"));
            QVERIFY(!events->item(unknown-1,0)->text().startsWith("BIN"));
            QVERIFY(!events->item(unknown+10,0)->text().startsWith("BIN"));
            events->selectRow(pdb);QVERIFY(fields->topLevelItemCount()>0);
            QVERIFY(!chart->chart()->series().isEmpty());
            const auto raw=fields->findItems(QStringLiteral("离轨球展开0x4F开0x46关"),Qt::MatchExactly|Qt::MatchRecursive);
            QCOMPARE(raw.size(),1);QCOMPARE(raw[0]->text(1),QString("0x46"));
            fields->setCurrentItem(raw[0]);
            auto *rawSeries=qobject_cast<QLineSeries*>(chart->chart()->series().value(0));QVERIFY(rawSeries);
            QVERIFY(chart->chart()->title().contains(QStringLiteral("原码")));
            QCOMPARE(rawSeries->points().first().y(),70.0);
            events->selectRow(unknown);
            QCOMPARE(fields->topLevelItemCount(),0);QVERIFY(detail->text().isEmpty());
            QVERIFY(!summary->text().contains("PDB"));QVERIFY(chart->chart()->series().isEmpty());
            if(!repeat)window.grab().save(QApplication::applicationDirPath()+"/OrbitTLM-ASRTU-raw-fragment.png");
            events->selectRow(sat);
            const auto temperatures=fields->findItems(QStringLiteral("3.3V温度"),Qt::MatchExactly|Qt::MatchRecursive);
            QCOMPARE(temperatures.size(),1);QCOMPARE(temperatures[0]->text(1),QString("26"));
            fields->setCurrentItem(temperatures[0]);
            const QString code=temperatures[0]->data(0,Qt::UserRole).toString();
            auto *temperatureSeries=qobject_cast<QLineSeries*>(chart->chart()->series().value(0));QVERIFY(temperatureSeries);
            QCOMPARE(temperatureSeries->points().first().y(),26.0);
            events->selectRow(nextSat);
            QVERIFY(fields->currentItem());QCOMPARE(fields->currentItem()->data(0,Qt::UserRole).toString(),code);
            search->setText(QStringLiteral("温度"));events->selectRow(sat);
            for(int group=0;group<fields->topLevelItemCount();++group)
                for(int child=0;child<fields->topLevelItem(group)->childCount();++child){
                    const auto *field=fields->topLevelItem(group)->child(child);
                    if(!field->isHidden())QVERIFY(field->text(0).contains(QStringLiteral("温度")));
                }
            search->clear();QApplication::processEvents();
            if(!repeat)window.grab().save(QApplication::applicationDirPath()+"/OrbitTLM-ASRTU-fixed.png");
        }
    }
};
QTEST_MAIN(UiSmoke)
#include "ui_smoke.moc"
