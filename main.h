#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/qdialog.h>
#include "ui_rtmp_qt.h"
#include "ui_about.h"
#include "librtmp/librtmp.h"

class about : public QDialog {
    Q_OBJECT
private:
    Ui::about ui;
public:
    about(QWidget* parent = Q_NULLPTR);
};

class rtmp_qt : public QMainWindow
{
    Q_OBJECT

public:
    rtmp_qt(QWidget* parent = Q_NULLPTR);

public slots:
    void Start();
    void Stop();
    void About();
private:
    about* abWin=0;
    Ui::rtmp_qt ui;
    LibRTMP* rtmp = 0;
};