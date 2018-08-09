// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef BITCOIN_GWNOTIFICATIONINTERFACE_H
#define BITCOIN_GWNOTIFICATIONINTERFACE_H

#include "validationinterface.h"

class CGWNotificationInterface : public CValidationInterface
{
public:
    CGWNotificationInterface(CConnman& connmanIn): connman(connmanIn) {}
    virtual ~CGWNotificationInterface() = default;

    // a small helper to initialize current block height in sub-modules on startup
    void InitializeCurrentBlockTip();

protected:
    // CValidationInterface
    void AcceptedBlockHeader(const CBlockIndex *pindexNew) override;
    void NotifyHeaderTip(const CBlockIndex *pindexNew, bool fInitialDownload) override;
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) override;
    void SyncTransaction(const CTransaction &tx, const CBlock *pblock) override;

private:
    CConnman& connman;
};

#endif // BITCOIN_GWNOTIFICATIONINTERFACE_H
