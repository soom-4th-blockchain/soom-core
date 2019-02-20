// Copyright (c) 2017-2018 The Dash Core developers
// Copyright (c) 2017-2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cbtx.h"
#include "deterministicgws.h"
#include "simplifiedgws.h"
#include "specialtx.h"

#include "chainparams.h"
#include "univalue.h"
#include "validation.h"

bool CheckCbTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state)
{
    if (tx.nType != TRANSACTION_COINBASE) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-type");
    }

    if (!tx.IsCoinBase()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-invalid");
    }

    CCbTx cbTx;
    if (!GetTxPayload(tx, cbTx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-payload");
    }

    if (cbTx.nVersion == 0 || cbTx.nVersion > CCbTx::CURRENT_VERSION) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-version");
    }

    if (pindexPrev && pindexPrev->nHeight + 1 != cbTx.nHeight) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-height");
    }

    return true;
}

// This can only be done after the block has been fully processed, as otherwise we won't have the finished GW list
bool CheckCbTxMerkleRootGWList(const CBlock& block, const CBlockIndex* pindex, CValidationState& state)
{
    if (block.vtx[0]->nType != TRANSACTION_COINBASE) {
        return true;
    }

    CCbTx cbTx;
    if (!GetTxPayload(*block.vtx[0], cbTx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-payload");
    }

    if (pindex) {
        uint256 calculatedMerkleRoot;
        if (!CalcCbTxMerkleRootGWList(block, pindex->pprev, calculatedMerkleRoot, state)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-gwmerkleroot");
        }
        if (calculatedMerkleRoot != cbTx.merkleRootGWList) {
            return state.DoS(100, false, REJECT_INVALID, "bad-cbtx-gwmerkleroot");
        }
    }

    return true;
}

bool CalcCbTxMerkleRootGWList(const CBlock& block, const CBlockIndex* pindexPrev, uint256& merkleRootRet, CValidationState& state)
{
    LOCK(deterministicGWManager->cs);

    CDeterministicGWList tmpGWList;
    if (!deterministicGWManager->BuildNewListFromBlock(block, pindexPrev, state, tmpGWList, false)) {
        return false;
    }

    CSimplifiedGWList sml(tmpGWList);

    bool mutated = false;
    merkleRootRet = sml.CalcMerkleRoot(&mutated);
    return !mutated;
}

std::string CCbTx::ToString() const
{
    return strprintf("CCbTx(nHeight=%d, nVersion=%d, merkleRootGWList=%s)",
        nVersion, nHeight, merkleRootGWList.ToString());
}

void CCbTx::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("version", (int)nVersion));
    obj.push_back(Pair("height", (int)nHeight));
    obj.push_back(Pair("merkleRootGWList", merkleRootGWList.ToString()));
}
