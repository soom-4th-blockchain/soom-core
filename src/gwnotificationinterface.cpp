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

    gatewaySync.UpdatedBlockTip(pindexNew, fInitialDownload, connman);

    if (fInitialDownload)
        return;

    gwnodeman.UpdatedBlockTip(pindexNew);
    instantsend.UpdatedBlockTip(pindexNew);
    gwpayments.UpdatedBlockTip(pindexNew, connman);
}

void CGWNotificationInterface::SyncTransaction(const CTransaction &tx, const CBlock *pblock)
{
    instantsend.SyncTransaction(tx, pblock);
}
