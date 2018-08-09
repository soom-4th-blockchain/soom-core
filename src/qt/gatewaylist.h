// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GATEWAYLIST_H
#define GATEWAYLIST_H

#include "primitives/transaction.h"
#include "platformstyle.h"
#include "sync.h"
#include "util.h"

#include <QMenu>
#include <QTimer>
#include <QWidget>

#define MY_GATEWAYLIST_UPDATE_SECONDS                 60
#define GATEWAYLIST_UPDATE_SECONDS                    15
#define GATEWAYLIST_FILTER_COOLDOWN_SECONDS            3

namespace Ui {
    class GatewayList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

/** Gateway Manager page widget */
class GatewayList : public QWidget
{
    Q_OBJECT

public:
    explicit GatewayList(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~GatewayList();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void StartAlias(std::string strAlias);
    void StartAll(std::string strCommand = "start-all");

private:
    QMenu *contextMenu;
    int64_t nTimeFilterUpdated;
    bool fFilterUpdated;

public Q_SLOTS:
    void updateMyGatewayInfo(QString strAlias, QString strAddr, const COutPoint& outpoint);
    void updateMyNodeList(bool fForce = false);
    void updateNodeList();

Q_SIGNALS:

private:
    QTimer *timer;
    Ui::GatewayList *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;

    // Protects tableWidgetGateways
    CCriticalSection cs_gwlist;

    // Protects tableWidgetMyGateways
    CCriticalSection cs_mygwlist;

    QString strCurrentFilter;

private Q_SLOTS:
    void showContextMenu(const QPoint &);
    void on_filterLineEdit_textChanged(const QString &strFilterIn);
    void on_startButton_clicked();
    void on_startAllButton_clicked();
    void on_startMissingButton_clicked();
    void on_tableWidgetMyGateways_itemSelectionChanged();
    void on_UpdateButton_clicked();
};
#endif // GATEWAYLIST_H
