#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QNetworkAccessManager>
#include <QMutex>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

protected:
    void showError(const QString& msg);
    qint64 getFileSize(const QString& url);
    void singleDownload(const QString& url, const QString& filename);
    void multiDownload(const QString& url, qint64 fileSize, const QString& filename, int threadCount);


private slots:
    void on_downloadBtn_clicked();
    void on_brwoserPathBtn_clicked();
    void download_progress_change(qint64 bytesReceived, qint64 bytesTotal);

private:
    QNetworkAccessManager *requestManager;
    const int maxThreadNum = 8;
    QMutex lock;

    Ui::Widget *ui;
};
#endif // WIDGET_H
