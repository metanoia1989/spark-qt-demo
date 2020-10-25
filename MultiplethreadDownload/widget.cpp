#include "widget.h"
#include "ui_widget.h"
#include "utils.h"
#include <QUrl>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QRegExp>
#include <QRegExpValidator>
#include <QFileDialog>
#include <QDir>
#include <QThread>
#include <QEventLoop>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    // 设置URL链接验证器
    QRegExp regex(R"(http[s]://.+)");
    auto *validator = new QRegExpValidator(regex, nullptr);
    ui->urlInput->setValidator(validator);

    // 设置最大的线程并发数
    int processer_num = QThread::idealThreadCount();
    processer_num = processer_num > maxThreadNum ? maxThreadNum : processer_num;
    ui->threadCountSpinbox->setMaximum(processer_num);
    ui->threadCountSpinbox->setValue(processer_num);

    // 设置保存路径为当前程序运行路径
    ui->savePathInput->setText(QDir::currentPath());

    requestManager = new QNetworkAccessManager(this);
}

Widget::~Widget()
{
    delete ui;
}


/**
 * @brief 展示错误信息
 * @param msg
 */
void Widget::showError(const QString& msg)
{
    auto msgBox = new QMessageBox(this);
    msgBox->setWindowTitle(tr("请求出错"));
    msgBox->setIcon(QMessageBox::Critical);
    msgBox->setText(msg);
    msgBox->exec();
    ui->downloadBtn->setEnabled(true);
}

/**
 * @brief 获取要下载的文件大小
 * @param url
 */
qint64 Widget::getFileSize(const QString& url)
{
    QEventLoop event;
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = requestManager->head(request);
    connect(reply, &QNetworkReply::errorOccurred, [this, reply](QNetworkReply::NetworkError error){
        if (error != QNetworkReply::NoError) {
            return showError(reply->errorString());
        }
    });
    connect(reply, &QNetworkReply::finished, &event, &QEventLoop::quit);
    event.exec();
    qint64 fileSize = 0;
    if (reply->rawHeader("Accept-Ranges") == QByteArrayLiteral("bytes")
            && reply->hasRawHeader(QString("Content-Length").toLocal8Bit())) {
        fileSize = reply->header(QNetworkRequest::ContentLengthHeader).toUInt();
    }
    reply->deleteLater();
    return fileSize;
}


/**
 * @brief 开始下载文件
 * @param url
 * @param filename
 */
void Widget::singleDownload(const QString& url, const QString& filename)
{
    QFile file(filename);
    if (file.exists())
        file.remove();
    if (!file.open(QIODevice::WriteOnly)) {
        return showError(file.errorString());
    }

    QEventLoop event;
    QNetworkAccessManager mgr;
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = mgr.get(request);
    connect(reply, &QNetworkReply::readyRead, this, [&file, reply](){
        file.write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, &event, &QEventLoop::quit);
    connect(reply, &QNetworkReply::errorOccurred,  [this, reply](QNetworkReply::NetworkError error){
        if (error != QNetworkReply::NoError) showError(reply->errorString());
    });
    event.exec();
    file.flush();
    file.close();
    ui->downloadBtn->setEnabled(true);
}

/**
 * @brief 多线程下载
 * @param url
 * @param fileSize
 * @param filename
 * @param threadCount
 */
void Widget::multiDownload(const QString &url, qint64 fileSize, const QString &filename, int threadCount)
{
    QFile file(filename);
    if (file.exists())
        file.remove();
    if (!file.open(QIODevice::WriteOnly)) {
        return showError(file.errorString());
    }
    file.resize(fileSize);

    // 任务等分
    qint64 segmentSize = fileSize / threadCount;
    QVector<QPair<qint64, qint64>> vec(threadCount);
    for (int i = 0; i < threadCount; i++) {
        vec[i].first = i * segmentSize;
        vec[i].second = i * segmentSize + segmentSize - 1;
    }
    vec[threadCount-1].second = fileSize; // 余数部分加入最后一个

    qint64 bytesReceived = 0; // 下载接收的总字节数
    // 任务队列
    auto mapCaller = [&, this](const QPair<qint64, qint64>& pair) -> qint64 {
        QEventLoop event;
        QNetworkAccessManager mgr;
        QNetworkRequest request;
        request.setUrl(url);
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        request.setRawHeader("Range", QString("bytes=%1-%2").arg(pair.first).arg(pair.second).toLocal8Bit());
        QNetworkReply *reply = mgr.get(request);
        qint64 writePos = pair.first;
        connect(reply, &QNetworkReply::readyRead, [&, reply, this](){
            QByteArray data = reply->readAll();
            lock.lock();
            file.peek(writePos);
            file.write(data);
            bytesReceived += data.size();
            lock.unlock();
            writePos += data.size();
        });
        connect(reply, &QNetworkReply::finished, &event, &QEventLoop::quit);
        event.exec();
        return writePos - pair.first;
    };
    QFuture<void> future = QtConcurrent::map(vec, mapCaller);
    QFutureWatcher<void> futureWatcher;
    QEventLoop loop;
    connect(&futureWatcher, &QFutureWatcher<void>::finished, &loop,  &QEventLoop::quit, Qt::QueuedConnection);
    futureWatcher.setFuture(future);
    if (!future.isFinished()) {
        loop.exec();
    }
    file.close();
    qDebug() << "下载完毕！！";
    ui->downloadBtn->setEnabled(true);
}

/**
 * @brief  点击开始下载
 */
void Widget::on_downloadBtn_clicked()
{
    QString url = ui->urlInput->text().trimmed();
    if (url.isEmpty()) {
        return showError(tr("请求链接不允许为空！"));
        return ;
    }

    // 选择保存路径
    QString savePath = ui->savePathInput->text().trimmed();
    if (savePath.isEmpty()) {
        savePath = QFileDialog::getExistingDirectory();
        if (savePath.isEmpty()) return;
    } else {
        if(!QDir(savePath).exists()) {
            return showError(tr("路径不存在！"));
        }
    }

    qint64 fileSize = getFileSize(url);
    QString sizeText = fileSize == 0 ? "未知大小" : Utils::sizeFormat(fileSize);
    ui->filesizeLabel->setText(sizeText);
    ui->downloadBtn->setEnabled(false);

    int process_num = ui->threadCountSpinbox->text().toInt();

    QDir::setCurrent(savePath);
    QString filename = QFileInfo(url).fileName();
    if (fileSize == 0 || process_num == 1) {
        singleDownload(url, filename);
    } else {
        multiDownload(url, fileSize, filename, process_num);
    }
}

/**
 * @brief 设置保存下载文件的路径
 */
void Widget::on_brwoserPathBtn_clicked()
{
    QString savePath = QFileDialog::getExistingDirectory();
    if (!savePath.isEmpty()) {
        ui->savePathInput->setText(savePath);
    }
}

void Widget::download_progress_change(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal <= 0)
        return;
    ui->downProgressBar->setMaximum(10000); // 最大值
    ui->downProgressBar->setValue((bytesReceived * 10000) / bytesTotal); // 当前值
    ui->downProgressBar->setTextVisible(true);
}
