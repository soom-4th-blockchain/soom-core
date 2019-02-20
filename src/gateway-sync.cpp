// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "init.h"
#include "validation.h"
#include "gateway-payments.h"
#include "gateway-sync.h"
#include "gatewayman.h"
#include "netfulfilledman.h"
#include "netmessagemaker.h"
#include "ui_interface.h"
#include "evo/deterministicgws.h"

class CGatewaySync;
CGatewaySync gatewaySync;

void CGatewaySync::Fail()
{
    nTimeLastFailure = GetTime();
    nCurrentAsset = GATEWAY_SYNC_FAILED;
}

void CGatewaySync::Reset()
{
    nCurrentAsset = GATEWAY_SYNC_INITIAL;
    nTriedPeerCount = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastBumped = GetTime();
    nTimeLastFailure = 0;
}

void CGatewaySync::BumpAssetLastTime(const std::string& strFuncName)
{
    if(IsSynced() || IsFailed()) return;
    nTimeLastBumped = GetTime();
    LogPrint("gwsync", "CGatewaySync::BumpAssetLastTime -- %s\n", strFuncName);
}

std::string CGatewaySync::GetAssetName()
{
    switch(nCurrentAsset)
    {
        case(GATEWAY_SYNC_INITIAL):         return "GATEWAY_SYNC_INITIAL";
        case(GATEWAY_SYNC_WAITING):         return "GATEWAY_SYNC_WAITING";
        case(GATEWAY_SYNC_LIST):            return "GATEWAY_SYNC_LIST";
        case(GATEWAY_SYNC_GWW):             return "GATEWAY_SYNC_GWW";
        case(GATEWAY_SYNC_FAILED):          return "GATEWAY_SYNC_FAILED";
        case GATEWAY_SYNC_FINISHED:         return "GATEWAY_SYNC_FINISHED";
        default:                            return "UNKNOWN";
    }
}

void CGatewaySync::SwitchToNextAsset(CConnman& connman)
{
    switch(nCurrentAsset)
    {
        case(GATEWAY_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case(GATEWAY_SYNC_INITIAL):
            nCurrentAsset = GATEWAY_SYNC_WAITING;
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GATEWAY_SYNC_WAITING):
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
                nCurrentAsset = GATEWAY_SYNC_GWW;
            } else {
                nCurrentAsset = GATEWAY_SYNC_LIST;
            }
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GATEWAY_SYNC_LIST):
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nCurrentAsset = GATEWAY_SYNC_GWW;
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case(GATEWAY_SYNC_GWW):
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Completed %s in %llds\n", GetAssetName(), GetTime() - nTimeAssetSyncStarted);
            nCurrentAsset = GATEWAY_SYNC_FINISHED;
            uiInterface.NotifyAdditionalDataSyncProgressChanged(1);
            //try to activate our gateway if possible
            legacyActiveGatewayManager.ManageState(connman);

            connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
                netfulfilledman.AddFulfilledRequest(pnode->addr, "full-sync");
            });
            LogPrintf("CGatewaySync::SwitchToNextAsset -- Sync has finished\n");

            break;
    }
    nTriedPeerCount = 0;
    nTimeAssetSyncStarted = GetTime();
    BumpAssetLastTime("CGatewaySync::SwitchToNextAsset");
}

std::string CGatewaySync::GetSyncStatus()
{
    switch (gatewaySync.nCurrentAsset) {
        case GATEWAY_SYNC_INITIAL:       return _("Synchroning blockchain...");
        case GATEWAY_SYNC_WAITING:       return _("Synchronization pending...");
        case GATEWAY_SYNC_LIST:          return _("Synchronizing gateways...");
        case GATEWAY_SYNC_GWW:           return _("Synchronizing gateway payments...");
        case GATEWAY_SYNC_FAILED:        return _("Synchronization failed");
        case GATEWAY_SYNC_FINISHED:      return _("Synchronization finished");
        default:                            return "";
    }
}

void CGatewaySync::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if(IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CGatewaySync::ProcessTick(CConnman& connman)
{
    static int nTick = 0;
    nTick++;

    // reset the sync process if the last call to this function was more than 60 minutes ago (client was in sleep mode)
    static int64_t nTimeLastProcess = GetTime();
    if(GetTime() - nTimeLastProcess > 60*60) {
        LogPrintf("CGatewaySync::ProcessTick -- WARNING: no actions for too long, restarting sync...\n");
        Reset();
        SwitchToNextAsset(connman);
        nTimeLastProcess = GetTime();
        return;
    }

    if(GetTime() - nTimeLastProcess < GATEWAY_SYNC_TICK_SECONDS) {
        // too early, nothing to do here
        return;
    }

    nTimeLastProcess = GetTime();

    // reset sync status in case of any other sync failure
    if(IsFailed()) {
        if(nTimeLastFailure + (1*60) < GetTime()) { // 1 minute cooldown after failed sync
            LogPrintf("CGatewaySync::ProcessTick -- WARNING: failed to sync, trying again...\n");
            Reset();
            SwitchToNextAsset(connman);
        }
        return;
    }

    // gradually request the rest of the votes after sync finished
    if(IsSynced()) {
	    // LogPrintf("CGatewaySync::ProcessTick -- IsSynced true" );
        return;
    }

    // Calculate "progress" for LOG reporting / GUI notification
    double nSyncProgress = double(nTriedPeerCount + (nCurrentAsset - 1) * 8) / (8*4);
    LogPrintf("CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d nTriedPeerCount %d nSyncProgress %f\n", nTick, nCurrentAsset, nTriedPeerCount, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(nSyncProgress);

    std::vector<CNode*> vNodesCopy = connman.CopyNodeVector(CConnman::FullyConnectedOnly);

    for (auto& pnode : vNodesCopy)
    {
        CNetMsgMaker msgMaker(pnode->GetSendVersion());

        // Don't try to sync any data from outbound "gateway" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "gateway" connection
        // initiated from another node, so skip it too.
        if(pnode->fGateway || (fGatewayMode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if(Params().NetworkIDString() == CBaseChainParams::REGTEST)
        {
            if (nCurrentAsset == GATEWAY_SYNC_WAITING) {
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETSPORKS)); //get current network sporks
                SwitchToNextAsset(connman);
            } else if (nCurrentAsset == GATEWAY_SYNC_LIST) {
                if (!deterministicGWManager->IsDeterministicGWsSporkActive()) {
                    gwnodeman.GwegUpdate(pnode, connman);
                }
                SwitchToNextAsset(connman);
            } else if (nCurrentAsset == GATEWAY_SYNC_GWW) {
                if (!deterministicGWManager->IsDeterministicGWsSporkActive()) {
                    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GATEWAYPAYMENTSYNC)); //sync payment votes
                }
                SwitchToNextAsset(connman);
            }
            connman.ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if(netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CGatewaySync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC

            if(!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // always get sporks first, only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETSPORKS));
                LogPrintf("CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d -- requesting sporks from peer %d\n", nTick, nCurrentAsset, pnode->id);
            }

            // INITIAL TIMEOUT

            if(nCurrentAsset == GATEWAY_SYNC_WAITING) {
                if(GetTime() - nTimeLastBumped > GATEWAY_SYNC_TIMEOUT_SECONDS) {
                    // At this point we know that:
                    // a) there are peers (because we are looping on at least one of them);
                    // b) we waited for at least GATEWAY_SYNC_TIMEOUT_SECONDS since we reached
                    //    the headers tip the last time (i.e. since we switched from
                    //     GATEWAY_SYNC_INITIAL to GATEWAY_SYNC_WAITING and bumped time);
                    // c) there were no blocks (UpdatedBlockTip, NotifyHeaderTip) or headers (AcceptedBlockHeader)
                    //    for at least GATEWAY_SYNC_TIMEOUT_SECONDS.
                    // We must be at the tip already, let's move to the next asset.
                    SwitchToNextAsset(connman);
                }
            }

            // GWLIST : SYNC GATEWAY LIST FROM OTHER CONNECTED CLIENTS

            if(nCurrentAsset == GATEWAY_SYNC_LIST) {
                if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                LogPrint("gateway", "CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nCurrentAsset, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                if(GetTime() - nTimeLastBumped > GATEWAY_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d -- timeout\n", nTick, nCurrentAsset);
                    if (nCurrentAsset == 0) {
                        LogPrintf("CGatewaySync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without gateway list, fail here and try later
                        Fail();
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // request from three peers max
                if (nTriedPeerCount > 2) {
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "gateway-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "gateway-list-sync");

                if (pnode->nVersion < gwpayments.GetMinGatewayPaymentsProto()) continue;
                nTriedPeerCount++;

                gwnodeman.GwegUpdate(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // GWW : SYNC GATEWAY PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if(nCurrentAsset == GATEWAY_SYNC_GWW) {
                if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                LogPrint("gwpayments", "CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d nTimeLastBumped %lld GetTime() %lld diff %lld\n", nTick, nCurrentAsset, nTimeLastBumped, GetTime(), GetTime() - nTimeLastBumped);
                // check for timeout first
                // This might take a lot longer than GATEWAY_SYNC_TIMEOUT_SECONDS due to new blocks,
                // but that should be OK and it should timeout eventually.
                if(GetTime() - nTimeLastBumped > GATEWAY_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d -- timeout\n", nTick, nCurrentAsset);
                    if (nTriedPeerCount == 0) {
                        LogPrintf("CGatewaySync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        connman.ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if gwpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if(nTriedPeerCount > 1 && gwpayments.IsEnoughData()) {
                    LogPrintf("CGatewaySync::ProcessTick -- nTick %d nCurrentAsset %d -- found enough data\n", nTick, nCurrentAsset);
                    SwitchToNextAsset(connman);
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // request from three peers max
                if (nTriedPeerCount > 2) {
                    connman.ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if(netfulfilledman.HasFulfilledRequest(pnode->addr, "gateway-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "gateway-payment-sync");

                if(pnode->nVersion < gwpayments.GetMinGatewayPaymentsProto()) continue;
                nTriedPeerCount++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GATEWAYPAYMENTSYNC));
                // ask node for missing pieces only (old nodes will not be asked)
                gwpayments.RequestLowDataPaymentBlocks(pnode, connman);

                connman.ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }
        }
    }
    // looped through all nodes, release them
    connman.ReleaseNodeVector(vNodesCopy);
}


void CGatewaySync::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    LogPrint("gwsync", "CGatewaySync::AcceptedBlockHeader -- pindexNew->nHeight: %d\n", pindexNew->nHeight);

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block header arrives while we are still syncing blockchain
        BumpAssetLastTime("CGatewaySync::AcceptedBlockHeader");
    }
}

void CGatewaySync::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint("gwsync", "CGatewaySync::NotifyHeaderTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CGatewaySync::NotifyHeaderTip");
    }
}

void CGatewaySync::UpdatedBlockTip(const CBlockIndex *pindexNew, bool fInitialDownload, CConnman& connman)
{
    LogPrint("gwsync", "CGatewaySync::UpdatedBlockTip -- pindexNew->nHeight: %d fInitialDownload=%d\n", pindexNew->nHeight, fInitialDownload);

    if (IsFailed() || IsSynced() || !pindexBestHeader)
        return;

    if (!IsBlockchainSynced()) {
        // Postpone timeout each time new block arrives while we are still syncing blockchain
        BumpAssetLastTime("CGatewaySync::UpdatedBlockTip");
    }

    if (fInitialDownload) {
        // switched too early
        if (IsBlockchainSynced()) {
            Reset();
        }

        // no need to check any further while still in IBD mode
        return;
    }

    // Note: since we sync headers first, it should be ok to use this
    static bool fReachedBestHeader = false;
    bool fReachedBestHeaderNew = pindexNew->GetBlockHash() == pindexBestHeader->GetBlockHash();

    if (fReachedBestHeader && !fReachedBestHeaderNew) {
        // Switching from true to false means that we previousely stuck syncing headers for some reason,
        // probably initial timeout was not enough,
        // because there is no way we can update tip not having best header
        Reset();
        fReachedBestHeader = false;
        return;
    }

    fReachedBestHeader = fReachedBestHeaderNew;

    LogPrint("gwsync", "CGatewaySync::UpdatedBlockTip -- pindexNew->nHeight: %d pindexBestHeader->nHeight: %d fInitialDownload=%d fReachedBestHeader=%d\n",
                pindexNew->nHeight, pindexBestHeader->nHeight, fInitialDownload, fReachedBestHeader);

    if (!IsBlockchainSynced() && fReachedBestHeader) {
        if (fLiteMode) {
            // nothing to do in lite mode, just finish the process immediately
            nCurrentAsset = GATEWAY_SYNC_FINISHED;
            return;
        }
        // Reached best header while being in initial mode.
        // We must be at the tip already, let's move to the next asset.
        SwitchToNextAsset(connman);
    }
}

void CGatewaySync::DoMaintenance(CConnman &connman)
{
    if (ShutdownRequested()) return;

    ProcessTick(connman);
}
