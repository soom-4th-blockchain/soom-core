// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gatewaylist.h"
#include "ui_gatewaylist.h"

#include "activegateway.h"
#include "clientmodel.h"
#include "clientversion.h"
#include "init.h"
#include "guiutil.h"
#include "gateway-sync.h"
#include "gatewayconfig.h"
#include "gatewayman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "netbase.h"
#include <univalue.h>
#include <QTimer>
#include <QMessageBox>
#include <QtGui/QClipboard>

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
    int columnAddressWidth = 180;
    int columnProtocolWidth = 90;
    int columnStatusWidth = 80;
    int columnActiveWidth = 130;
    int columnLastSeenWidth = 130;

    int columnPoSeScoreWidth = 80;
    int columnRegisteredWidth = 80;
    int columnLastPaidWidth = 80;
    int columnNextPaymentWidth = 100;
    int columnPayeeWidth = 130;
    int columnOperatorRewardWidth = 130;
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

    ui->tableWidgetGatewaysDIP3->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(1, columnStatusWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(2, columnPoSeScoreWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(3, columnRegisteredWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(4, columnLastPaidWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(5, columnNextPaymentWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(6, columnPayeeWidth);
    ui->tableWidgetGatewaysDIP3->setColumnWidth(7, columnOperatorRewardWidth);

    // dummy column for proTxHash
    // TODO use a proper table model for the MN list
    ui->tableWidgetGatewaysDIP3->insertColumn(8);
    ui->tableWidgetGatewaysDIP3->setColumnHidden(8, true);
    ui->tableWidgetMyGateways->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tableWidgetGatewaysDIP3->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction *startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyGateways, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    QAction* copyProTxHashAction = new QAction(tr("Copy ProTx Hash"), this);
    QAction* copyCollateralOutpointAction = new QAction(tr("Copy Collateral Outpoint"), this);
    contextMenuDIP3 = new QMenu();
    contextMenuDIP3->addAction(copyProTxHashAction);
    contextMenuDIP3->addAction(copyCollateralOutpointAction);
    connect(ui->tableWidgetGatewaysDIP3, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenuDIP3(const QPoint&)));
    connect(ui->tableWidgetGatewaysDIP3, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(extraInfoDIP3_clicked()));
    connect(copyProTxHashAction, SIGNAL(triggered()), this, SLOT(copyProTxHash_clicked()));
    connect(copyCollateralOutpointAction, SIGNAL(triggered()), this, SLOT(copyCollateralOutpoint_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateDIP3List()));
    timer->start(1000);

    fFilterUpdated = false;
    fFilterUpdatedDIP3 = false;
    nTimeFilterUpdated = GetTime();
    nTimeFilterUpdatedDIP3 = GetTime();
    updateNodeList();
    updateDIP3List();
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

void GatewayList::showContextMenuDIP3(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetGatewaysDIP3->itemAt(point);
    if (item) contextMenuDIP3->exec(QCursor::pos());
}

static bool CheckWalletOwnsScript(const CScript& script)
{
    CTxDestination dest;
    if (ExtractDestination(script, dest)) {
        if ((boost::get<CKeyID>(&dest) && pwalletMain->HaveKey(*boost::get<CKeyID>(&dest))) || (boost::get<CScriptID>(&dest) && pwalletMain->HaveCScript(*boost::get<CScriptID>(&dest)))) {
            return true;
        }
    }
    return false;
}
void GatewayList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    for (const auto& gwe : gatewayConfig.getEntries()) {
        if(gwe.getAlias() == strAlias) {
            std::string strError;
            CGatewayBroadcast gwb;

            bool fSuccess = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

            int nDoS;
            if (fSuccess && !gwnodeman.CheckGwbAndUpdateGatewayList(NULL, gwb, nDoS, *g_connman)) {
                strError = "Failed to verify MNB";
                fSuccess = false;
            }
            if(fSuccess) {
                strStatusHtml += "<br>Successfully started gateway.";
                gwnodeman.NotifyGatewayUpdates(*g_connman);
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

    for (const auto& gwe : gatewayConfig.getEntries()) {
        std::string strError;
        CGatewayBroadcast gwb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(gwe.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && gwnodeman.Has(outpoint)) continue;

        bool fSuccess = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

        int nDoS;
        if (fSuccess && !gwnodeman.CheckGwbAndUpdateGatewayList(NULL, gwb, nDoS, *g_connman)) {
            strError = "Failed to verify GWB";
            fSuccess = false;
        }

        if(fSuccess) {
            nCountSuccessful++;
            gwnodeman.NotifyGatewayUpdates(*g_connman);
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + gwe.getAlias() + ". Error: " + strError;
        }
    }

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

    if (ShutdownRequested()) {
        return;
    }
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        return;
    }

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

    // Find selected row
    QItemSelectionModel* selectionModel = ui->tableWidgetMyGateways->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();
    int nSelectedRow = selected.count() ? selected.at(0).row() : 0;

    ui->tableWidgetMyGateways->setSortingEnabled(false);
    for (const auto& gwe : gatewayConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(gwe.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        updateMyGatewayInfo(QString::fromStdString(gwe.getAlias()), QString::fromStdString(gwe.getIp()), COutPoint(uint256S(gwe.getTxHash()), nOutputIndex));
    }
    ui->tableWidgetMyGateways->selectRow(nSelectedRow);
    ui->tableWidgetMyGateways->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void GatewayList::updateNodeList()
{

    if (ShutdownRequested()) {
        return;
    }

    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        // we misuse the fact that updateNodeList is called regularely here and remove both tabs
        if (ui->tabWidget->indexOf(ui->tabDIP3Gateways) != 0) {
            // remove "My Gateway" and "All Gateways" tabs
            ui->tabWidget->removeTab(0);
            ui->tabWidget->removeTab(0);
        }
        return;
    }

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

    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        ui->countLabel->setText(QString::number(0));
        return;
    }
    int offsetFromUtc = GetOffsetFromUtc();

    std::map<COutPoint, CGateway> mapGateways = gwnodeman.GetFullGatewayMap();
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

void GatewayList::updateDIP3List()
{
    if (ShutdownRequested()) {
        return;
    }

    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        ui->dip3NoteLabel->setVisible(false);
    }

    TRY_LOCK(cs_dip3list, fLockAcquired);
    if (!fLockAcquired) return;

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in GATEWAYLIST_UPDATE_SECONDS seconds
    // or GATEWAYLIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdatedDIP3
                             ? nTimeFilterUpdatedDIP3 - GetTime() + GATEWAYLIST_FILTER_COOLDOWN_SECONDS
                             : nTimeListUpdated - GetTime() + GATEWAYLIST_UPDATE_SECONDS;

    if (fFilterUpdatedDIP3) {
        ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    }
    if (nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdatedDIP3 = false;

    QString strToFilter;
    ui->countLabelDIP3->setText("Updating...");
    ui->tableWidgetGatewaysDIP3->setSortingEnabled(false);
    ui->tableWidgetGatewaysDIP3->clearContents();
    ui->tableWidgetGatewaysDIP3->setRowCount(0);

    auto gwList = deterministicGWManager->GetListAtChainTip();
    auto projectedPayees = gwList.GetProjectedGWPayees(gwList.GetValidGWsCount());
    std::map<uint256, int> nextPayments;
    for (size_t i = 0; i < projectedPayees.size(); i++) {
        const auto& dgw = projectedPayees[i];
        nextPayments.emplace(dgw->proTxHash, gwList.GetHeight() + (int)i + 1);
    }

    std::set<COutPoint> setOutpts;
    if (pwalletMain && ui->checkBoxMyGatewaysOnly->isChecked()) {
        LOCK(pwalletMain->cs_wallet);
        std::vector<COutPoint> vOutpts;
        pwalletMain->ListProTxCoins(vOutpts);
        for (const auto& outpt : vOutpts) {
            setOutpts.emplace(outpt);
        }
    }

    gwList.ForEachGW(false, [&](const CDeterministicGWCPtr& dgw) {
        if (pwalletMain && ui->checkBoxMyGatewaysOnly->isChecked()) {
            LOCK(pwalletMain->cs_wallet);
            bool fMyGateway = setOutpts.count(dgw->collateralOutpoint) ||
                pwalletMain->HaveKey(dgw->pdgwState->keyIDOwner) ||
                pwalletMain->HaveKey(dgw->pdgwState->keyIDVoting) ||
                CheckWalletOwnsScript(dgw->pdgwState->scriptPayout) ||
                CheckWalletOwnsScript(dgw->pdgwState->scriptOperatorPayout);
            if (!fMyGateway) return;
        }
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem* addressItem = new QTableWidgetItem(QString::fromStdString(dgw->pdgwState->addr.ToString()));
        QTableWidgetItem* statusItem = new QTableWidgetItem(gwList.IsGWValid(dgw) ? tr("ENABLED") : (gwList.IsGWPoSeBanned(dgw) ? tr("POSE_BANNED") : tr("UNKNOWN")));
        QTableWidgetItem* PoSeScoreItem = new QTableWidgetItem(QString::number(dgw->pdgwState->nPoSePenalty));
        QTableWidgetItem* registeredItem = new QTableWidgetItem(QString::number(dgw->pdgwState->nRegisteredHeight));
        QTableWidgetItem* lastPaidItem = new QTableWidgetItem(QString::number(dgw->pdgwState->nLastPaidHeight));
        QTableWidgetItem* nextPaymentItem = new QTableWidgetItem(nextPayments.count(dgw->proTxHash) ? QString::number(nextPayments[dgw->proTxHash]) : tr("UNKNOWN"));

        CTxDestination payeeDest;
        QString payeeStr;
        if (ExtractDestination(dgw->pdgwState->scriptPayout, payeeDest)) {
            payeeStr = QString::fromStdString(CBitcoinAddress(payeeDest).ToString());
        } else {
            payeeStr = tr("UNKNOWN");
        }
        QTableWidgetItem* payeeItem = new QTableWidgetItem(payeeStr);

        QString operatorRewardStr;
        if (dgw->nOperatorReward) {
            operatorRewardStr += QString::number(dgw->nOperatorReward / 100.0, 'f', 2) + "% ";

            if (dgw->pdgwState->scriptOperatorPayout != CScript()) {
                CTxDestination operatorDest;
                if (ExtractDestination(dgw->pdgwState->scriptOperatorPayout, operatorDest)) {
                    operatorRewardStr += tr("to %1").arg(QString::fromStdString(CBitcoinAddress(operatorDest).ToString()));
                } else {
                    operatorRewardStr += tr("to UNKNOWN");
                }
            } else {
                operatorRewardStr += tr("but not claimed");
            }
        } else {
            operatorRewardStr = tr("NONE");
        }
        QTableWidgetItem* operatorRewardItem = new QTableWidgetItem(operatorRewardStr);
        QTableWidgetItem* proTxHashItem = new QTableWidgetItem(QString::fromStdString(dgw->proTxHash.ToString()));

        if (strCurrentFilterDIP3 != "") {
            strToFilter = addressItem->text() + " " +
                          statusItem->text() + " " +
                          PoSeScoreItem->text() + " " +
                          registeredItem->text() + " " +
                          lastPaidItem->text() + " " +
                          nextPaymentItem->text() + " " +
                          payeeItem->text() + " " +
                          operatorRewardItem->text() + " " +
                          proTxHashItem->text();
            if (!strToFilter.contains(strCurrentFilterDIP3)) return;
        }

        ui->tableWidgetGatewaysDIP3->insertRow(0);
        ui->tableWidgetGatewaysDIP3->setItem(0, 0, addressItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 1, statusItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 2, PoSeScoreItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 3, registeredItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 4, lastPaidItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 5, nextPaymentItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 6, payeeItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 7, operatorRewardItem);
        ui->tableWidgetGatewaysDIP3->setItem(0, 8, proTxHashItem);
    });

    ui->countLabelDIP3->setText(QString::number(ui->tableWidgetGatewaysDIP3->rowCount()));
    ui->tableWidgetGatewaysDIP3->setSortingEnabled(true);
}
void GatewayList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", GATEWAYLIST_FILTER_COOLDOWN_SECONDS)));
}

void GatewayList::on_filterLineEditDIP3_textChanged(const QString& strFilterIn)
{
    strCurrentFilterDIP3 = strFilterIn;
    nTimeFilterUpdatedDIP3 = GetTime();
    fFilterUpdatedDIP3 = true;
    ui->countLabelDIP3->setText(QString::fromStdString(strprintf("Please wait... %d", GATEWAYLIST_FILTER_COOLDOWN_SECONDS)));
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
    QMessageBox question(QMessageBox::Question, tr("Confirm gateway start"),
        tr("Are you sure you want to start gateway %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        this);
    question.button(QMessageBox::Cancel)->setStyleSheet(QString("background-color:#ffffff; color:#4E586D;"));

    int retval = question.exec();
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
    QMessageBox question(QMessageBox::Question, tr("Confirm all gateways start"),
        tr("Are you sure you want to start ALL gateways?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        this);
    question.button(QMessageBox::Cancel)->setStyleSheet(QString("background-color:#ffffff; color:#4E586D;"));

    int retval = question.exec();
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
    QMessageBox question(QMessageBox::Question, tr("Confirm missing gateways start"),
        tr("Are you sure you want to start MISSING gateways?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        this);
    question.button(QMessageBox::Cancel)->setStyleSheet(QString("background-color:#ffffff; color:#4E586D;"));

    int retval = question.exec();
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

void GatewayList::on_checkBoxMyGatewaysOnly_stateChanged(int state)
{
    // no cooldown
    nTimeFilterUpdatedDIP3 = GetTime() - GATEWAYLIST_FILTER_COOLDOWN_SECONDS;
    fFilterUpdatedDIP3 = true;
}

CDeterministicGWCPtr GatewayList::GetSelectedDIP3GW()
{
    std::string strProTxHash;
    {
        LOCK(cs_dip3list);

        QItemSelectionModel* selectionModel = ui->tableWidgetGatewaysDIP3->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if (selected.count() == 0) return nullptr;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strProTxHash = ui->tableWidgetGatewaysDIP3->item(nSelectedRow, 8)->text().toStdString();
    }

    uint256 proTxHash;
    proTxHash.SetHex(strProTxHash);

    auto gwList = deterministicGWManager->GetListAtChainTip();
    return gwList.GetGW(proTxHash);
}

void GatewayList::extraInfoDIP3_clicked()
{
    auto dgw = GetSelectedDIP3GW();
    if (!dgw) {
        return;
    }

    UniValue json(UniValue::VOBJ);
    dgw->ToJson(json);

    // Title of popup window
    QString strWindowtitle = tr("Additional information for DIP3 Gateway %1").arg(QString::fromStdString(dgw->proTxHash.ToString()));
    QString strText = QString::fromStdString(json.write(2));

    QMessageBox::information(this, strWindowtitle, strText);
}

void GatewayList::copyProTxHash_clicked()
{
    auto dgw = GetSelectedDIP3GW();
    if (!dgw) {
        return;
    }

    QApplication::clipboard()->setText(QString::fromStdString(dgw->proTxHash.ToString()));
}

void GatewayList::copyCollateralOutpoint_clicked()
{
    auto dgw = GetSelectedDIP3GW();
    if (!dgw) {
        return;
    }

    QApplication::clipboard()->setText(QString::fromStdString(dgw->collateralOutpoint.ToStringShort()));
}
