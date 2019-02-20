// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "gwnotificationinterface.h"
#include "instantx.h"
#include "gatewayman.h"
#include "gateway-payments.h"
#include "gateway-sync.h"

#include "evo/deterministicgws.h"

#include "llgq/quorums_dummydkg.h"
void CGWNotificationInterface::InitializeCurrentBlockTip()
{
    LOCK(cs_main);
    UpdatedBlockTip(chainActive.Tip(), NULL, IsInitialBlockDownload());
}

void CGWNotificationInterface::AcceptedBlockHeader(const CBlockIndex *pindexNew)
{
    gatewaySync.AcceptedBlockHeader(pindexNew);
}

void CGWNotificationInterface::NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload)
{
    gatewaySync.NotifyHeaderTip(pindexNew, fInitialDownload, connman);
}

void CGWNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload)
{
    if (pindexNew == pindexFork) // blocks were disconnected without any new ones
        return;

    deterministicGWManager->UpdatedBlockTip(pindexNew);
    llgq::quorumDummyDKG->UpdatedBlockTip(pindexNew, fInitialDownload);

    gatewaySync.UpdatedBlockTip(pindexNew, fInitialDownload, connman);


    // update instantsend autolock activation flag (we reuse the DIP3 deployment)
    instantsend.isAutoLockBip9Active =
            (VersionBitsState(pindexNew, Params().GetConsensus(), Consensus::DEPLOYMENT_DIP0003, versionbitscache) == THRESHOLD_ACTIVE);


    if (fInitialDownload)
        return;

    if (fLiteMode)
        return;
    gwnodeman.UpdatedBlockTip(pindexNew);
    instantsend.UpdatedBlockTip(pindexNew);
    gwpayments.UpdatedBlockTip(pindexNew, connman);
}

void CGWNotificationInterface::SyncTransaction(const CTransaction &tx, const CBlockIndex *pindex, int posInBlock)
{
    instantsend.SyncTransaction(tx, pindex, posInBlock);
}
