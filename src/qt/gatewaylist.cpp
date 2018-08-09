// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gatewaylist.h"
#include "ui_gatewaylist.h"

#include "activegateway.h"
#include "clientmodel.h"
#include "init.h"
#include "guiutil.h"
#include "gateway-sync.h"
#include "gatewayconfig.h"
#include "gatewayman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <QTimer>
#include <QMessageBox>

int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}

GatewayList::GatewayList(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::GatewayList),
    clientModel(0),
    walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 200;
    int columnProtocolWidth = 60;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    ui->tableWidgetMyGateways->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyGateways->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyGateways->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyGateways->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyGateways->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyGateways->setColumnWidth(5, columnLastSeenWidth);

    ui->tableWidgetGateways->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetGateways->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetGateways->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetGateways->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetGateways->setColumnWidth(4, columnLastSeenWidth);

    ui->tableWidgetMyGateways->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyGateways, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

GatewayList::~GatewayList()
{
    delete ui;
}

void GatewayList::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when gateway count changes
        connect(clientModel, SIGNAL(strGatewaysChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void GatewayList::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

void GatewayList::showContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidgetMyGateways->itemAt(point);
    if(item) contextMenu->exec(QCursor::pos());
}

void GatewayList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
        if(gwe.getAlias() == strAlias) {
            std::string strError;
            CGatewayBroadcast gwb;

            bool fSuccess = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

            if(fSuccess) {
                strStatusHtml += "<br>Successfully started gateway.";
                gwnodeman.UpdateGatewayList(gwb, *g_connman);
                gwb.Relay(*g_connman);
            } else {
                strStatusHtml += "<br>Failed to start gateway.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void GatewayList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
        std::string strError;
        CGatewayBroadcast gwb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(gwe.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && gwnodeman.Has(outpoint)) continue;

        bool fSuccess = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

        if(fSuccess) {
            nCountSuccessful++;
            gwnodeman.UpdateGatewayList(gwb, *g_connman);
            gwb.Relay(*g_connman);
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + gwe.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d gateways, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void GatewayList::updateMyGatewayInfo(QString strAlias, QString strAddr, const COutPoint& outpoint)
{
    bool fOldRowFound = false;
    int nNewRow = 0;

    for(int i = 0; i < ui->tableWidgetMyGateways->rowCount(); i++) {
        if(ui->tableWidgetMyGateways->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if(nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyGateways->rowCount();
        ui->tableWidgetMyGateways->insertRow(nNewRow);
    }

    gateway_info_t infoGw;
    bool fFound = gwnodeman.GetGatewayInfo(outpoint, infoGw);

    QTableWidgetItem *aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(fFound ? QString::fromStdString(infoGw.addr.ToString()) : strAddr);
    QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(fFound ? infoGw.nProtocolVersion : -1));
    QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(fFound ? CGateway::StateToString(infoGw.nActiveState) : "MISSING"));
    QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(fFound ? (infoGw.nTimeLastPing - infoGw.sigTime) : 0)));
    QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",
                                                                                                   fFound ? infoGw.nTimeLastPing + GetOffsetFromUtc() : 0)));
    QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(fFound ? CBitcoinAddress(infoGw.pubKeyCollateralAddress.GetID()).ToString() : ""));

    ui->tableWidgetMyGateways->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyGateways->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyGateways->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyGateways->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyGateways->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyGateways->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyGateways->setItem(nNewRow, 6, pubkeyItem);
}

void GatewayList::updateMyNodeList(bool fForce)
{
    TRY_LOCK(cs_mygwlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my gateway list only once in MY_GATEWAYLIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_GATEWAYLIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if(nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetGateways->setSortingEnabled(false);
    BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(gwe.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        updateMyGatewayInfo(QString::fromStdString(gwe.getAlias()), QString::fromStdString(gwe.getIp()), COutPoint(uint256S(gwe.getTxHash()), nOutputIndex));
    }
    ui->tableWidgetGateways->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void GatewayList::updateNodeList()
{
    TRY_LOCK(cs_gwlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in GATEWAYLIST_UPDATE_SECONDS seconds
    // or GATEWAYLIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + GATEWAYLIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + GATEWAYLIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetGateways->setSortingEnabled(false);
    ui->tableWidgetGateways->clearContents();
    ui->tableWidgetGateways->setRowCount(0);
    std::map<COutPoint, CGateway> mapGateways = gwnodeman.GetFullGatewayMap();
    int offsetFromUtc = GetOffsetFromUtc();

    for(auto& gwpair : mapGateways)
    {
        CGateway gw = gwpair.second;
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(gw.addr.ToString()));
        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(gw.nProtocolVersion));
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(gw.GetStatus()));
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(gw.lastPing.sigTime - gw.sigTime)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", gw.lastPing.sigTime + offsetFromUtc)));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(gw.pubKeyCollateralAddress.GetID()).ToString()));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetGateways->insertRow(0);
        ui->tableWidgetGateways->setItem(0, 0, addressItem);
        ui->tableWidgetGateways->setItem(0, 1, protocolItem);
        ui->tableWidgetGateways->setItem(0, 2, statusItem);
        ui->tableWidgetGateways->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetGateways->setItem(0, 4, lastSeenItem);
        ui->tableWidgetGateways->setItem(0, 5, pubkeyItem);
    }

    ui->countLabel->setText(QString::number(ui->tableWidgetGateways->rowCount()));
    ui->tableWidgetGateways->setSortingEnabled(true);
}

void GatewayList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", GATEWAYLIST_FILTER_COOLDOWN_SECONDS)));
}

void GatewayList::on_startButton_clicked()
{
    std::string strAlias;
    {
        LOCK(cs_mygwlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMyGateways->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMyGateways->item(nSelectedRow, 0)->text().toStdString();
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm gateway start"),
        tr("Are you sure you want to start gateway %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked ) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void GatewayList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all gateways start"),
        tr("Are you sure you want to start ALL gateways?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked ) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void GatewayList::on_startMissingButton_clicked()
{
    if(!gatewaySync.IsGatewayListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until gateway list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing gateways start"),
        tr("Are you sure you want to start MISSING gateways?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked ) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void GatewayList::on_tableWidgetMyGateways_itemSelectionChanged()
{
    if(ui->tableWidgetMyGateways->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void GatewayList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
