#ifndef QRCODEDIALOG_H
#define QRCODEDIALOG_H

#include "walletmodel.h"

#include <QDialog>
#include <QImage>

namespace Ui {
    class QRCodeDialog;
}
class OptionsModel;

class QRCodeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QRCodeDialog(const QString &addr, const QString &label, bool enableReq, QWidget *parent = 0);
    ~QRCodeDialog();

    void setModel(OptionsModel *model);
    void setInfo(const SendCoinsRecipient &info);

private Q_SLOTS:
    void on_btnSaveAs_clicked();

private:
    Ui::QRCodeDialog *ui;
    OptionsModel *model;
    SendCoinsRecipient info;
    QString address;
    QImage myImage;

    void generatesQRCodes();
};

#endif // QRCODEDIALOG_H
