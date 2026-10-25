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
};
QTEST_MAIN(UiSmoke)
#include "ui_smoke.moc"
