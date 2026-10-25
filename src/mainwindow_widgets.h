#pragma once
#include <QPainter>
#include <QPixmap>
#include <QQuaternion>
#include <QVector3D>
#include <QWidget>
#include <algorithm>
#include <cmath>
class AttitudeWidget final : public QWidget {
public:
    explicit AttitudeWidget(QWidget *parent=nullptr):QWidget(parent){setMinimumSize(220,210);}
    void setQuaternion(const QQuaternion &q,bool available){
        active_=available;
        rotation_=q.normalized();
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(),QColor("#f7faff"));
        if(!active_){
            p.setPen(QColor("#758397"));
            p.drawText(rect(),Qt::AlignCenter,
                QStringLiteral("暂无姿态数据"));
            return;
        }
        constexpr QVector3D corners[8]={{-1,-.65f,-.35f},{1,-.65f,-.35f},
            {1,.65f,-.35f},{-1,.65f,-.35f},{-1,-.65f,.35f},
            {1,-.65f,.35f},{1,.65f,.35f},{-1,.65f,.35f}};
        constexpr int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
            {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        QPointF projected[8];
        const qreal scale=qreal(std::min(width(),height()))*0.26;
        const QPointF center(width()/2.,height()/2.);
        for(int i=0;i<8;++i){
            const QVector3D v=rotation_.rotatedVector(corners[i]);
            const qreal perspective=4.0/(4.0+v.z());
            projected[i]=center+QPointF(v.x()*scale*perspective,-v.y()*scale*perspective);
        }
        p.setPen(QPen(QColor("#377fc0"),2.3));
        for(const auto &edge:edges)p.drawLine(projected[edge[0]],projected[edge[1]]);
        p.setPen(QColor("#718096"));
        p.drawText(rect().adjusted(10,0,-10,-8),Qt::AlignBottom|Qt::AlignHCenter,
            QStringLiteral("相对姿态"));
    }
private:
    QQuaternion rotation_;
    bool active_=false;
};
class ImagePreview final : public QWidget {
public:
    explicit ImagePreview(QWidget *parent=nullptr):QWidget(parent){setMinimumSize(260,190);}
    void setImage(const QPixmap &image){image_=image;update();}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(),QColor("#f7faff"));
        if(image_.isNull()){
            p.setPen(QColor("#758397"));
            p.drawText(rect().adjusted(20,20,-20,-20),Qt::AlignCenter|Qt::TextWordWrap,
                QStringLiteral("暂无恢复图像\n\n可扫描 SSDV 候选数据包，\n使用外部解码器还原图片"));
            return;
        }
        const QSize scaled=image_.size().scaled(size()-QSize(20,20),Qt::KeepAspectRatio);
        const QRect dst(QPoint((width()-scaled.width())/2,(height()-scaled.height())/2),scaled);
        p.drawPixmap(dst,image_,image_.rect());
    }
private:
    QPixmap image_;
};
