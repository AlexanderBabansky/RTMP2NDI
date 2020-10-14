#include "librtmp/librtmp.h"
#include "main.h"

rtmp_qt::rtmp_qt(QWidget* parent):
	QMainWindow(0) {
	ui.setupUi(this);
}

void rtmp_qt::Start() {
	ui.pbStart->setEnabled(false);
	ui.sbPort->setEnabled(false);
	rtmp = new LibRTMP(ui.sbPort->value());
	ui.pbStop->setEnabled(true);
}

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,int nCmdShow){	
	int argc = 0;
	char* argv[1];
	QApplication app(argc,argv);
	rtmp_qt mainWindow;
	mainWindow.show();
	return app.exec();
}

void rtmp_qt::Stop()
{
	ui.pbStop->setEnabled(false);
	delete rtmp;	
	ui.pbStart->setEnabled(true);
	ui.sbPort->setEnabled(true);
}

void rtmp_qt::About()
{
	if (!abWin) {
		abWin = new about(this);
	}
	abWin->show();
}

about::about(QWidget* parent):
	QDialog(parent)
{
	ui.setupUi(this);
}
