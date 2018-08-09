// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef GATEWAY_SYNC_H
#define GATEWAY_SYNC_H

#include "chain.h"
#include "net.h"

#include <univalue.h>

class CGatewaySync;

static const int GATEWAY_SYNC_FAILED          = -1;
static const int GATEWAY_SYNC_INITIAL         = 0; // sync just started, was reset recently or still in IDB
static const int GATEWAY_SYNC_WAITING         = 1; // waiting after initial to see if we can get more headers/blocks
static const int GATEWAY_SYNC_LIST            = 2;
static const int GATEWAY_SYNC_GWW             = 3;
static const int GATEWAY_SYNC_FINISHED        = 999;

static const int GATEWAY_SYNC_TICK_SECONDS    = 6;
static const int GATEWAY_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int GATEWAY_SYNC_ENOUGH_PEERS    = 6;

extern CGatewaySync gatewaySync;

//
// CGatewaySync : Sync gateway assets in stages
//

class CGatewaySync
{
private:
    // Keep track of current asset
    int nRequestedGatewayAssets;
    // Count peers we've requested the asset from
    int nRequestedGatewayAttempt;

    // Time when current gateway asset sync started
    int64_t nTimeAssetSyncStarted;
    // ... last bumped
    int64_t nTimeLastBumped;
    // ... or failed
    int64_t nTimeLastFailure;

    void Fail();
    void ClearFulfilledRequests(CConnman& connman);

public:
    CGatewaySync() { Reset(); }

    bool IsFailed() { return nRequestedGatewayAssets == GATEWAY_SYNC_FAILED; }
    bool IsBlockchainSynced() { return nRequestedGatewayAssets > GATEWAY_SYNC_WAITING; }
    bool IsGatewayListSynced() { return nRequestedGatewayAssets > GATEWAY_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedGatewayAssets > GATEWAY_SYNC_GWW; }
    bool IsSynced() { return nRequestedGatewayAssets == GATEWAY_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedGatewayAssets; }
    int GetAttempt() { return nRequestedGatewayAttempt; }
    void BumpAssetLastTime(std::string strFuncName);
    int64_t GetAssetStartTime() { return nTimeAssetSyncStarted; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset(CConnman& connman);

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick(CConnman& connman);

    void AcceptedBlockHeader(const CBlockIndex *pindexNew);
    void NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);
    void UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman);
};

void ThreadCheckGatewaySync(CConnman& connman);

#endif
