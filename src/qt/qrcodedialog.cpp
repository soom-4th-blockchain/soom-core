#include "qrcodedialog.h"
#include "ui_qrcodedialog.h"

#include "bitcoinunits.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"

#include <QPixmap>
#include <QUrl>

#include <qrencode.h>

QRCodeDialog::QRCodeDialog(const QString &addr, const QString &label, bool enableReq, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QRCodeDialog),
    model(0),
    address(addr)
{
    ui->setupUi(this);

    setWindowTitle(QString("%1").arg(address));

    ui->lblQRCode->setVisible(true);
    ui->btnSaveAs->setEnabled(false);
    ui->closeButton->setText(tr("&Close"));

    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(accept()));

    generatesQRCodes();
}

QRCodeDialog::~QRCodeDialog()
{
    delete ui;
}

void QRCodeDialog::setModel(OptionsModel *model)
{
    this->model = model;
}

void QRCodeDialog::setInfo(const SendCoinsRecipient &info)
{
    this->info = info;
    generatesQRCodes();
}

void QRCodeDialog::generatesQRCodes()
{
    ui->outUri->clear();
    ui->btnSaveAs->setEnabled(false);

    QString uri = GUIUtil::formatBitcoinURI(info);
    QString html;
    html += "<html><font face='verdana, arial, helvetica, sans-serif'>";
    html += "<b>"+tr("URI")+"</b>: ";
    html += "<a href=\""+uri+"\">" + GUIUtil::HtmlEscape(uri) + "</a><br>";
    html += "<b>"+tr("Address")+"</b>: " + GUIUtil::HtmlEscape(info.address) + "<br>";

    if(!info.label.isEmpty())
        html += "<b>"+tr("Label")+"</b>: " + GUIUtil::HtmlEscape(info.label) + "<br>";
    ui->outUri->setText(html);


    ui->lblQRCode->setText("");
    if(!uri.isEmpty())
    {
        // limit URI length to prevent a DoS against the QR-Code dialog
        if (uri.length() > MAX_URI_LENGTH)
        {
            ui->lblQRCode->setText(tr("Resulting URI too long, try to reduce the text for label / message."));
        }
        else
        {
            QRcode *code = QRcode_encodeString(uri.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
            if (!code)
            {
                ui->lblQRCode->setText(tr("Error encoding URI into QR Code."));
                return;
            }
            myImage = QImage(code->width + 8, code->width + 8, QImage::Format_RGB32);
            myImage.fill(0xffffff);
            unsigned char *p = code->data;
            for (int y = 0; y < code->width; y++)
            {
                for (int x = 0; x < code->width; x++)
                {
                    myImage.setPixel(x + 4, y + 4, ((*p & 1) ? 0x0 : 0xffffff));
                    p++;
                }
            }
            QRcode_free(code);

            ui->lblQRCode->setPixmap(QPixmap::fromImage(myImage).scaled(300, 300));
            ui->btnSaveAs->setEnabled(true);
        }
    }
}

void QRCodeDialog::on_btnSaveAs_clicked()
{
    QString fn = GUIUtil::getSaveFileName(this, tr("Save QR Code"), QString(), tr("PNG Images (*.png)"), NULL);
    if (!fn.isEmpty())
        myImage.scaled(QR_IMAGE_SIZE, QR_IMAGE_SIZE).save(fn);
}

void QRCodeDialog::on_btnCopyAddress_clicked()
{
    GUIUtil::setClipboard(info.address);
}
