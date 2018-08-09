// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "openuridialog.h"
#include "ui_openuridialog.h"

#include "guiutil.h"
#include "walletmodel.h"

#include <QUrl>
#include <QGraphicsDropShadowEffect>
#include <QtWidgets/QDialogButtonBox>

OpenURIDialog::OpenURIDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OpenURIDialog)
{
    ui->setupUi(this);

    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect;
    shadow->setBlurRadius(9.0);
    shadow->setColor(QColor(0, 0, 0, 160));
    shadow->setOffset(4.0);
    ui->label_2->setGraphicsEffect(shadow);

    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("&OK"));
    ui->buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("&Cancel"));

#if QT_VERSION >= 0x040700
    ui->uriEdit->setPlaceholderText("soom:");
#endif
}

OpenURIDialog::~OpenURIDialog()
{
    delete ui;
}

QString OpenURIDialog::getURI()
{
    return ui->uriEdit->text();
}

void OpenURIDialog::accept()
{
    SendCoinsRecipient rcp;
    if(GUIUtil::parseBitcoinURI(getURI(), &rcp))
    {
        /* Only accept value URIs */
        QDialog::accept();
    } else {
        ui->uriEdit->setValid(false);
    }
}

void OpenURIDialog::on_selectFileButton_clicked()
{
    QString filename = GUIUtil::getOpenFileName(this, tr("Select payment request file to open"), "", "", NULL);
    if(filename.isEmpty())
        return;
    QUrl fileUri = QUrl::fromLocalFile(filename);
    ui->uriEdit->setText("soom:?r=" + QUrl::toPercentEncoding(fileUri.toString()));
}
