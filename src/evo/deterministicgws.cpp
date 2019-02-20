// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "deterministicgws.h"
#include "specialtx.h"

#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include "script/standard.h"
#include "spork.h"
#include "validation.h"
#include "validationinterface.h"

#include "llgq/quorums_commitment.h"
#include "llgq/quorums_utils.h"

#include <univalue.h>

static const std::string DB_LIST_SNAPSHOT = "dgw_S";
static const std::string DB_LIST_DIFF = "dgw_D";

CDeterministicGWManager* deterministicGWManager;

std::string CDeterministicGWState::ToString() const
{
    CTxDestination dest;
    std::string payoutAddress = "unknown";
    std::string operatorPayoutAddress = "none";
    if (ExtractDestination(scriptPayout, dest)) {
        payoutAddress = CBitcoinAddress(dest).ToString();
    }
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        operatorPayoutAddress = CBitcoinAddress(dest).ToString();
    }

    return strprintf("CDeterministicGWState(nRegisteredHeight=%d, nLastPaidHeight=%d, nPoSePenalty=%d, nPoSeRevivedHeight=%d, nPoSeBanHeight=%d, nRevocationReason=%d, "
        "ownerAddress=%s, pubKeyOperator=%s, votingAddress=%s, addr=%s, payoutAddress=%s, operatorPayoutAddress=%s)",
        nRegisteredHeight, nLastPaidHeight, nPoSePenalty, nPoSeRevivedHeight, nPoSeBanHeight, nRevocationReason,
        CBitcoinAddress(keyIDOwner).ToString(), pubKeyOperator.ToString(), CBitcoinAddress(keyIDVoting).ToString(), addr.ToStringIPPort(false), payoutAddress, operatorPayoutAddress);
}

void CDeterministicGWState::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("service", addr.ToStringIPPort(false)));
    obj.push_back(Pair("registeredHeight", nRegisteredHeight));
    obj.push_back(Pair("lastPaidHeight", nLastPaidHeight));
    obj.push_back(Pair("PoSePenalty", nPoSePenalty));
    obj.push_back(Pair("PoSeRevivedHeight", nPoSeRevivedHeight));
    obj.push_back(Pair("PoSeBanHeight", nPoSeBanHeight));
    obj.push_back(Pair("revocationReason", nRevocationReason));
    obj.push_back(Pair("ownerAddress", CBitcoinAddress(keyIDOwner).ToString()));
    obj.push_back(Pair("votingAddress", CBitcoinAddress(keyIDVoting).ToString()));

    CTxDestination dest;
    if (ExtractDestination(scriptPayout, dest)) {
        CBitcoinAddress payoutAddress(dest);
        obj.push_back(Pair("payoutAddress", payoutAddress.ToString()));
    }
    obj.push_back(Pair("pubKeyOperator", pubKeyOperator.ToString()));
    if (ExtractDestination(scriptOperatorPayout, dest)) {
        CBitcoinAddress operatorPayoutAddress(dest);
        obj.push_back(Pair("operatorPayoutAddress", operatorPayoutAddress.ToString()));
    }
}

std::string CDeterministicGW::ToString() const
{
    return strprintf("CDeterministicGW(proTxHash=%s, collateralOutpoint=%s, nOperatorReward=%f, state=%s", proTxHash.ToString(), collateralOutpoint.ToStringShort(), (double)nOperatorReward / 100, pdgwState->ToString());
}

void CDeterministicGW::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();

    UniValue stateObj;
    pdgwState->ToJson(stateObj);

    obj.push_back(Pair("proTxHash", proTxHash.ToString()));
    obj.push_back(Pair("collateralHash", collateralOutpoint.hash.ToString()));
    obj.push_back(Pair("collateralIndex", (int)collateralOutpoint.n));
    obj.push_back(Pair("operatorReward", (double)nOperatorReward / 100));
    obj.push_back(Pair("state", stateObj));
}

bool CDeterministicGWList::IsGWValid(const uint256& proTxHash) const
{
    auto p = gwMap.find(proTxHash);
    if (p == nullptr) {
        return false;
    }
    return IsGWValid(*p);
}

bool CDeterministicGWList::IsGWPoSeBanned(const uint256& proTxHash) const
{
    auto p = gwMap.find(proTxHash);
    if (p == nullptr) {
        return false;
    }
    return IsGWPoSeBanned(*p);
}

bool CDeterministicGWList::IsGWValid(const CDeterministicGWCPtr& dgw) const
{
    return !IsGWPoSeBanned(dgw);
}

bool CDeterministicGWList::IsGWPoSeBanned(const CDeterministicGWCPtr& dgw) const
{
    assert(dgw);
    const CDeterministicGWState& state = *dgw->pdgwState;
    return state.nPoSeBanHeight != -1;
}

CDeterministicGWCPtr CDeterministicGWList::GetGW(const uint256& proTxHash) const
{
    auto p = gwMap.find(proTxHash);
    if (p == nullptr) {
        return nullptr;
    }
    return *p;
}

CDeterministicGWCPtr CDeterministicGWList::GetValidGW(const uint256& proTxHash) const
{
    auto dgw = GetGW(proTxHash);
    if (dgw && !IsGWValid(dgw)) {
        return nullptr;
    }
    return dgw;
}

CDeterministicGWCPtr CDeterministicGWList::GetGWByOperatorKey(const CBLSPublicKey& pubKey)
{
    for (const auto& p : gwMap) {
        if (p.second->pdgwState->pubKeyOperator == pubKey) {
            return p.second;
        }
    }
    return nullptr;
}

CDeterministicGWCPtr CDeterministicGWList::GetGWByCollateral(const COutPoint& collateralOutpoint) const
{
    return GetUniquePropertyGW(collateralOutpoint);
}

static int CompareByLastPaid_GetHeight(const CDeterministicGW& dgw)
{
    int height = dgw.pdgwState->nLastPaidHeight;
    if (dgw.pdgwState->nPoSeRevivedHeight != -1 && dgw.pdgwState->nPoSeRevivedHeight > height) {
        height = dgw.pdgwState->nPoSeRevivedHeight;
    } else if (height == 0) {
        height = dgw.pdgwState->nRegisteredHeight;
    }
    return height;
}

static bool CompareByLastPaid(const CDeterministicGW& _a, const CDeterministicGW& _b)
{
    int ah = CompareByLastPaid_GetHeight(_a);
    int bh = CompareByLastPaid_GetHeight(_b);
    if (ah == bh) {
        return _a.proTxHash < _b.proTxHash;
    } else {
        return ah < bh;
    }
}
static bool CompareByLastPaid(const CDeterministicGWCPtr& _a, const CDeterministicGWCPtr& _b)
{
    return CompareByLastPaid(*_a, *_b);
}

CDeterministicGWCPtr CDeterministicGWList::GetGWPayee() const
{
    if (gwMap.size() == 0) {
        return nullptr;
    }

    CDeterministicGWCPtr best;
    ForEachGW(true, [&](const CDeterministicGWCPtr& dgw) {
        if (!best || CompareByLastPaid(dgw, best)) {
            best = dgw;
        }
    });

    return best;
}

std::vector<CDeterministicGWCPtr> CDeterministicGWList::GetProjectedGWPayees(int nCount) const
{
    std::vector<CDeterministicGWCPtr> result;
    result.reserve(nCount);

    CDeterministicGWList tmpGWList = *this;
    for (int h = nHeight; h < nHeight + nCount; h++) {
        tmpGWList.SetHeight(h);

        CDeterministicGWCPtr payee = tmpGWList.GetGWPayee();
        // push the original GW object instead of the one from the temporary list
        result.push_back(GetGW(payee->proTxHash));

        CDeterministicGWStatePtr newState = std::make_shared<CDeterministicGWState>(*payee->pdgwState);
        newState->nLastPaidHeight = h;
        tmpGWList.UpdateGW(payee->proTxHash, newState);
    }

    return result;
}

std::vector<CDeterministicGWCPtr> CDeterministicGWList::CalculateQuorum(size_t maxSize, const uint256& modifier) const
{
    auto scores = CalculateScores(modifier);

    // sort is descending order
    std::sort(scores.rbegin(), scores.rend(), [](const std::pair<arith_uint256, CDeterministicGWCPtr>& a, std::pair<arith_uint256, CDeterministicGWCPtr>& b) {
        if (a.first == b.first) {
            // this should actually never happen, but we should stay compatible with how the non deterministic GWs did the sorting
            return a.second->collateralOutpoint < b.second->collateralOutpoint;
        }
        return a.first < b.first;
    });

    // take top maxSize entries and return it
    std::vector<CDeterministicGWCPtr> result;
    result.resize(std::min(maxSize, scores.size()));
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = std::move(scores[i].second);
    }
    return result;
}

std::vector<std::pair<arith_uint256, CDeterministicGWCPtr>> CDeterministicGWList::CalculateScores(const uint256& modifier) const
{
    std::vector<std::pair<arith_uint256, CDeterministicGWCPtr>> scores;
    scores.reserve(GetAllGWsCount());
    ForEachGW(true, [&](const CDeterministicGWCPtr& dgw) {
        if (dgw->pdgwState->confirmedHash.IsNull()) {
            // we only take confirmed GWs into account to avoid hash grinding on the ProRegTxHash to sneak GWs into a
            // future quorums
            return;
        }
        // calculate sha256(sha256(proTxHash, confirmedHash), modifier) per GW
        // Please note that this is not a double-sha256 but a single-sha256
        // The first part is already precalculated (confirmedHashWithProRegTxHash)
        // TODO When https://github.com/bitcoin/bitcoin/pull/13191 gets backported, implement something that is similar but for single-sha256
        uint256 h;
        CSHA256 sha256;
        sha256.Write(dgw->pdgwState->confirmedHashWithProRegTxHash.begin(), dgw->pdgwState->confirmedHashWithProRegTxHash.size());
        sha256.Write(modifier.begin(), modifier.size());
        sha256.Finalize(h.begin());

        scores.emplace_back(UintToArith256(h), dgw);
    });

    return scores;
}

int CDeterministicGWList::CalcMaxPoSePenalty() const
{
    // Maximum PoSe penalty is dynamic and equals the number of registered GWs
    // It's however at least 100.
    // This means that the max penalty is usually equal to a full payment cycle
    return std::max(100, (int)GetAllGWsCount());
}

int CDeterministicGWList::CalcPenalty(int percent) const
{
    assert(percent > 0);
    return (CalcMaxPoSePenalty() * percent) / 100;
}

void CDeterministicGWList::PoSePunish(const uint256& proTxHash, int penalty, bool debugLogs)
{
    assert(penalty > 0);

    auto dgw = GetGW(proTxHash);
    assert(dgw);

    int maxPenalty = CalcMaxPoSePenalty();

    auto newState = std::make_shared<CDeterministicGWState>(*dgw->pdgwState);
    newState->nPoSePenalty += penalty;
    newState->nPoSePenalty = std::min(maxPenalty, newState->nPoSePenalty);

    if (debugLogs) {
        LogPrintf("CDeterministicGWList::%s -- punished GW %s, penalty %d->%d (max=%d)\n",
                  __func__, proTxHash.ToString(), dgw->pdgwState->nPoSePenalty, newState->nPoSePenalty, maxPenalty);
    }

    if (newState->nPoSePenalty >= maxPenalty && newState->nPoSeBanHeight == -1) {
        newState->nPoSeBanHeight = nHeight;
        if (debugLogs) {
            LogPrintf("CDeterministicGWList::%s -- banned GW %s at height %d\n",
                      __func__, proTxHash.ToString(), nHeight);
        }
    }
    UpdateGW(proTxHash, newState);
}

void CDeterministicGWList::PoSeDecrease(const uint256& proTxHash)
{
    auto dgw = GetGW(proTxHash);
    assert(dgw);
    assert(dgw->pdgwState->nPoSePenalty > 0 && dgw->pdgwState->nPoSeBanHeight == -1);

    auto newState = std::make_shared<CDeterministicGWState>(*dgw->pdgwState);
    newState->nPoSePenalty--;
    UpdateGW(proTxHash, newState);
}

CDeterministicGWListDiff CDeterministicGWList::BuildDiff(const CDeterministicGWList& to) const
{
    CDeterministicGWListDiff diffRet;
    diffRet.prevBlockHash = blockHash;
    diffRet.blockHash = to.blockHash;
    diffRet.nHeight = to.nHeight;

    to.ForEachGW(false, [&](const CDeterministicGWCPtr& toPtr) {
        auto fromPtr = GetGW(toPtr->proTxHash);
        if (fromPtr == nullptr) {
            diffRet.addedGWs.emplace(toPtr->proTxHash, toPtr);
        } else if (*toPtr->pdgwState != *fromPtr->pdgwState) {
            diffRet.updatedGWs.emplace(toPtr->proTxHash, toPtr->pdgwState);
        }
    });
    ForEachGW(false, [&](const CDeterministicGWCPtr& fromPtr) {
        auto toPtr = to.GetGW(fromPtr->proTxHash);
        if (toPtr == nullptr) {
            diffRet.removedGws.insert(fromPtr->proTxHash);
        }
    });

    return diffRet;
}

CSimplifiedGWListDiff CDeterministicGWList::BuildSimplifiedDiff(const CDeterministicGWList& to) const
{
    CSimplifiedGWListDiff diffRet;
    diffRet.baseBlockHash = blockHash;
    diffRet.blockHash = to.blockHash;

    to.ForEachGW(false, [&](const CDeterministicGWCPtr& toPtr) {
        auto fromPtr = GetGW(toPtr->proTxHash);
        if (fromPtr == nullptr) {
            diffRet.gwList.emplace_back(*toPtr);
        } else {
            CSimplifiedGWListEntry sme1(*toPtr);
            CSimplifiedGWListEntry sme2(*fromPtr);
            if (sme1 != sme2) {
                diffRet.gwList.emplace_back(*toPtr);
            }
        }
    });
    ForEachGW(false, [&](const CDeterministicGWCPtr& fromPtr) {
        auto toPtr = to.GetGW(fromPtr->proTxHash);
        if (toPtr == nullptr) {
            diffRet.deletedGWs.emplace_back(fromPtr->proTxHash);
        }
    });

    return diffRet;
}

CDeterministicGWList CDeterministicGWList::ApplyDiff(const CDeterministicGWListDiff& diff) const
{
    assert(diff.prevBlockHash == blockHash && diff.nHeight == nHeight + 1);

    CDeterministicGWList result = *this;
    result.blockHash = diff.blockHash;
    result.nHeight = diff.nHeight;

    for (const auto& hash : diff.removedGws) {
        result.RemoveGW(hash);
    }
    for (const auto& p : diff.addedGWs) {
        result.AddGW(p.second);
    }
    for (const auto& p : diff.updatedGWs) {
        result.UpdateGW(p.first, p.second);
    }

    return result;
}

void CDeterministicGWList::AddGW(const CDeterministicGWCPtr& dgw)
{
    assert(!gwMap.find(dgw->proTxHash));
    gwMap = gwMap.set(dgw->proTxHash, dgw);
    AddUniqueProperty(dgw, dgw->collateralOutpoint);
    if (dgw->pdgwState->addr != CService()) {
        AddUniqueProperty(dgw, dgw->pdgwState->addr);
    }
    AddUniqueProperty(dgw, dgw->pdgwState->keyIDOwner);
    if (dgw->pdgwState->pubKeyOperator.IsValid()) {
        AddUniqueProperty(dgw, dgw->pdgwState->pubKeyOperator);
    }
}

void CDeterministicGWList::UpdateGW(const uint256& proTxHash, const CDeterministicGWStateCPtr& pdgwState)
{
    auto oldDgw = gwMap.find(proTxHash);
    assert(oldDgw != nullptr);
    auto dgw = std::make_shared<CDeterministicGW>(**oldDgw);
    auto oldState = dgw->pdgwState;
    dgw->pdgwState = pdgwState;
    gwMap = gwMap.set(proTxHash, dgw);

    UpdateUniqueProperty(dgw, oldState->addr, pdgwState->addr);
    UpdateUniqueProperty(dgw, oldState->keyIDOwner, pdgwState->keyIDOwner);
    UpdateUniqueProperty(dgw, oldState->pubKeyOperator, pdgwState->pubKeyOperator);
}

void CDeterministicGWList::RemoveGW(const uint256& proTxHash)
{
    auto dgw = GetGW(proTxHash);
    assert(dgw != nullptr);
    DeleteUniqueProperty(dgw, dgw->collateralOutpoint);
    if (dgw->pdgwState->addr != CService()) {
        DeleteUniqueProperty(dgw, dgw->pdgwState->addr);
    }
    DeleteUniqueProperty(dgw, dgw->pdgwState->keyIDOwner);
    if (dgw->pdgwState->pubKeyOperator.IsValid()) {
        DeleteUniqueProperty(dgw, dgw->pdgwState->pubKeyOperator);
    }
    gwMap = gwMap.erase(proTxHash);
}

CDeterministicGWManager::CDeterministicGWManager(CEvoDB& _evoDb) :
    evoDb(_evoDb)
{
}

bool CDeterministicGWManager::ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& _state)
{
    LOCK(cs);

    int nHeight = pindex->nHeight;

    CDeterministicGWList newList;
    if (!BuildNewListFromBlock(block, pindex->pprev, _state, newList, true)) {
        return false;
    }

    if (newList.GetHeight() == -1) {
        newList.SetHeight(nHeight);
    }

    newList.SetBlockHash(block.GetHash());

    CDeterministicGWList oldList = GetListForBlock(pindex->pprev->GetBlockHash());
    CDeterministicGWListDiff diff = oldList.BuildDiff(newList);

    evoDb.Write(std::make_pair(DB_LIST_DIFF, diff.blockHash), diff);
    if ((nHeight % SNAPSHOT_LIST_PERIOD) == 0) {
        evoDb.Write(std::make_pair(DB_LIST_SNAPSHOT, diff.blockHash), newList);
        LogPrintf("CDeterministicGWManager::%s -- Wrote snapshot. nHeight=%d, mapCurGWs.allGWsCount=%d\n",
            __func__, nHeight, newList.GetAllGWsCount());
    }

    if (nHeight == GetSpork15Value()) {
        LogPrintf("CDeterministicGWManager::%s -- spork15 is active now. nHeight=%d\n", __func__, nHeight);
    }

    CleanupCache(nHeight);

    return true;
}

bool CDeterministicGWManager::UndoBlock(const CBlock& block, const CBlockIndex* pindex)
{
    LOCK(cs);

    int nHeight = pindex->nHeight;
    uint256 blockHash = block.GetHash();

    evoDb.Erase(std::make_pair(DB_LIST_DIFF, blockHash));
    evoDb.Erase(std::make_pair(DB_LIST_SNAPSHOT, blockHash));
    gwListsCache.erase(blockHash);

    if (nHeight == GetSpork15Value()) {
        LogPrintf("CDeterministicGWManager::%s -- spork15 is not active anymore. nHeight=%d\n", __func__, nHeight);
    }

    return true;
}

void CDeterministicGWManager::UpdatedBlockTip(const CBlockIndex* pindex)
{
    LOCK(cs);

    tipHeight = pindex->nHeight;
    tipBlockHash = pindex->GetBlockHash();
}

bool CDeterministicGWManager::BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& _state, CDeterministicGWList& gwListRet, bool debugLogs)
{
    AssertLockHeld(cs);

    int nHeight = pindexPrev->nHeight + 1;

    CDeterministicGWList oldList = GetListForBlock(pindexPrev->GetBlockHash());
    CDeterministicGWList newList = oldList;
    newList.SetBlockHash(uint256()); // we can't know the final block hash, so better not return a (invalid) block hash
    newList.SetHeight(nHeight);

    auto payee = oldList.GetGWPayee();

    // we iterate the oldList here and update the newList
    // this is only valid as long these have not diverged at this point, which is the case as long as we don't add
    // code above this loop that modifies newList
    oldList.ForEachGW(false, [&](const CDeterministicGWCPtr& dgw) {
        if (!dgw->pdgwState->confirmedHash.IsNull()) {
            // already confirmed
            return;
        }
        // this works on the previous block, so confirmation will happen one block after nGatewayMinimumConfirmations
        // has been reached, but the block hash will then point to the block at nGatewayMinimumConfirmations
        int nConfirmations = pindexPrev->nHeight - dgw->pdgwState->nRegisteredHeight;
        if (nConfirmations >= Params().GetConsensus().nGatewayMinimumConfirmations) {
            CDeterministicGWState newState = *dgw->pdgwState;
            newState.UpdateConfirmedHash(dgw->proTxHash, pindexPrev->GetBlockHash());
            newList.UpdateGW(dgw->proTxHash, std::make_shared<CDeterministicGWState>(newState));
        }
    });

    DecreasePoSePenalties(newList);

    // we skip the coinbase
    for (int i = 1; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];

        if (tx.nVersion != 3) {
            // only interested in special TXs
            continue;
        }

        if (tx.nType == TRANSACTION_PROVIDER_REGISTER) {
            CProRegTx proTx;
            if (!GetTxPayload(tx, proTx)) {
                assert(false); // this should have been handled already
            }

            auto dgw = std::make_shared<CDeterministicGW>();
            dgw->proTxHash = tx.GetHash();

            // collateralOutpoint is either pointing to an external collateral or to the ProRegTx itself
            if (proTx.collateralOutpoint.hash.IsNull()) {
                dgw->collateralOutpoint = COutPoint(tx.GetHash(), proTx.collateralOutpoint.n);
            } else {
                dgw->collateralOutpoint = proTx.collateralOutpoint;
            }

            Coin coin;
            if (!proTx.collateralOutpoint.hash.IsNull() && (!GetUTXOCoin(dgw->collateralOutpoint, coin) || coin.out.nValue != COLLATERAL_COINS)) {
                // should actually never get to this point as CheckProRegTx should have handled this case.
                // We do this additional check nevertheless to be 100% sure
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral");
            }

            auto replacedDgw = newList.GetGWByCollateral(dgw->collateralOutpoint);
            if (replacedDgw != nullptr) {
                // This might only happen with a ProRegTx that refers an external collateral
                // In that case the new ProRegTx will replace the old one. This means the old one is removed
                // and the new one is added like a completely fresh one, which is also at the bottom of the payment list
                newList.RemoveGW(replacedDgw->proTxHash);
                if (debugLogs) {
                    LogPrintf("CDeterministicGWManager::%s -- GW %s removed from list because collateral was used for a new ProRegTx. collateralOutpoint=%s, nHeight=%d, mapCurGWs.allGWsCount=%d\n",
                              __func__, replacedDgw->proTxHash.ToString(), dgw->collateralOutpoint.ToStringShort(), nHeight, newList.GetAllGWsCount());
                }
            }

            if (newList.HasUniqueProperty(proTx.addr)) {
                return _state.DoS(100, false, REJECT_CONFLICT, "bad-protx-dup-addr");
            }
            if (newList.HasUniqueProperty(proTx.keyIDOwner) || newList.HasUniqueProperty(proTx.pubKeyOperator)) {
                return _state.DoS(100, false, REJECT_CONFLICT, "bad-protx-dup-key");
            }

            dgw->nOperatorReward = proTx.nOperatorReward;
            dgw->pdgwState = std::make_shared<CDeterministicGWState>(proTx);

            CDeterministicGWState dgwState = *dgw->pdgwState;
            dgwState.nRegisteredHeight = nHeight;

            if (proTx.addr == CService()) {
                // start in banned pdgwState as we need to wait for a ProUpServTx
                dgwState.nPoSeBanHeight = nHeight;
            }

            dgw->pdgwState = std::make_shared<CDeterministicGWState>(dgwState);

            newList.AddGW(dgw);

            if (debugLogs) {
                LogPrintf("CDeterministicGWManager::%s -- GW %s added at height %d: %s\n",
                    __func__, tx.GetHash().ToString(), nHeight, proTx.ToString());
            }
        } else if (tx.nType == TRANSACTION_PROVIDER_UPDATE_SERVICE) {
            CProUpServTx proTx;
            if (!GetTxPayload(tx, proTx)) {
                assert(false); // this should have been handled already
            }

            if (newList.HasUniqueProperty(proTx.addr) && newList.GetUniquePropertyGW(proTx.addr)->proTxHash != proTx.proTxHash) {
                return _state.DoS(100, false, REJECT_CONFLICT, "bad-protx-dup-addr");
            }

            CDeterministicGWCPtr dgw = newList.GetGW(proTx.proTxHash);
            if (!dgw) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            auto newState = std::make_shared<CDeterministicGWState>(*dgw->pdgwState);
            newState->addr = proTx.addr;
            newState->scriptOperatorPayout = proTx.scriptOperatorPayout;

            if (newState->nPoSeBanHeight != -1) {
                // only revive when all keys are set
                if (newState->pubKeyOperator.IsValid() && !newState->keyIDVoting.IsNull() && !newState->keyIDOwner.IsNull()) {
                    newState->nPoSePenalty = 0;
                    newState->nPoSeBanHeight = -1;
                    newState->nPoSeRevivedHeight = nHeight;

                    if (debugLogs) {
                        LogPrintf("CDeterministicGWManager::%s -- GW %s revived at height %d\n",
                            __func__, proTx.proTxHash.ToString(), nHeight);
                    }
                }
            }

            newList.UpdateGW(proTx.proTxHash, newState);
            if (debugLogs) {
                LogPrintf("CDeterministicGWManager::%s -- GW %s updated at height %d: %s\n",
                    __func__, proTx.proTxHash.ToString(), nHeight, proTx.ToString());
            }
        } else if (tx.nType == TRANSACTION_PROVIDER_UPDATE_REGISTRAR) {
            CProUpRegTx proTx;
            if (!GetTxPayload(tx, proTx)) {
                assert(false); // this should have been handled already
            }

            CDeterministicGWCPtr dgw = newList.GetGW(proTx.proTxHash);
            if (!dgw) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            auto newState = std::make_shared<CDeterministicGWState>(*dgw->pdgwState);
            if (newState->pubKeyOperator != proTx.pubKeyOperator) {
                // reset all operator related fields and put GW into PoSe-banned state in case the operator key changes
                newState->ResetOperatorFields();
                newState->BanIfNotBanned(nHeight);
            }
            newState->pubKeyOperator = proTx.pubKeyOperator;
            newState->keyIDVoting = proTx.keyIDVoting;
            newState->scriptPayout = proTx.scriptPayout;

            newList.UpdateGW(proTx.proTxHash, newState);

            if (debugLogs) {
                LogPrintf("CDeterministicGWManager::%s -- GW %s updated at height %d: %s\n",
                    __func__, proTx.proTxHash.ToString(), nHeight, proTx.ToString());
            }
        } else if (tx.nType == TRANSACTION_PROVIDER_UPDATE_REVOKE) {
            CProUpRevTx proTx;
            if (!GetTxPayload(tx, proTx)) {
                assert(false); // this should have been handled already
            }

            CDeterministicGWCPtr dgw = newList.GetGW(proTx.proTxHash);
            if (!dgw) {
                return _state.DoS(100, false, REJECT_INVALID, "bad-protx-hash");
            }
            auto newState = std::make_shared<CDeterministicGWState>(*dgw->pdgwState);
            newState->ResetOperatorFields();
            newState->BanIfNotBanned(nHeight);
            newState->nRevocationReason = proTx.nReason;

            newList.UpdateGW(proTx.proTxHash, newState);

            if (debugLogs) {
                LogPrintf("CDeterministicGWManager::%s -- GW %s revoked operator key at height %d: %s\n",
                    __func__, proTx.proTxHash.ToString(), nHeight, proTx.ToString());
            }
        } else if (tx.nType == TRANSACTION_QUORUM_COMMITMENT) {
            llgq::CFinalCommitmentTxPayload qc;
            if (!GetTxPayload(tx, qc)) {
                assert(false); // this should have been handled already
            }
            if (!qc.commitment.IsNull()) {
                HandleQuorumCommitment(qc.commitment, newList, debugLogs);
            }
        }
    }

    // we skip the coinbase
    for (int i = 1; i < (int)block.vtx.size(); i++) {
        const CTransaction& tx = *block.vtx[i];

        // check if any existing GW collateral is spent by this transaction
        for (const auto& in : tx.vin) {
            auto dgw = newList.GetGWByCollateral(in.prevout);
            if (dgw && dgw->collateralOutpoint == in.prevout) {
                newList.RemoveGW(dgw->proTxHash);

                if (debugLogs) {
                    LogPrintf("CDeterministicGWManager::%s -- GW %s removed from list because collateral was spent. collateralOutpoint=%s, nHeight=%d, mapCurGWs.allGWsCount=%d\n",
                              __func__, dgw->proTxHash.ToString(), dgw->collateralOutpoint.ToStringShort(), nHeight, newList.GetAllGWsCount());
                }
            }
        }
    }

    // The payee for the current block was determined by the previous block's list but it might have disappeared in the
    // current block. We still pay that GW one last time however.
    if (payee && newList.HasGW(payee->proTxHash)) {
        auto newState = std::make_shared<CDeterministicGWState>(*newList.GetGW(payee->proTxHash)->pdgwState);
        newState->nLastPaidHeight = nHeight;
        newList.UpdateGW(payee->proTxHash, newState);
    }

    gwListRet = std::move(newList);

    return true;
}

void CDeterministicGWManager::HandleQuorumCommitment(llgq::CFinalCommitment& qc, CDeterministicGWList& gwList, bool debugLogs)
{
    // The commitment has already been validated at this point so it's safe to use members of it

    auto members = llgq::CLLGQUtils::GetAllQuorumMembers((Consensus::LLGQType)qc.llgqType, qc.quorumHash);

    for (size_t i = 0; i < members.size(); i++) {
        if (!gwList.HasGW(members[i]->proTxHash)) {
            continue;
        }
        if (!qc.validMembers[i]) {
            // punish GW for failed DKG participation
            // The idea is to immediately ban a GW when it fails 2 DKG sessions with only a few blocks in-between
            // If there were enough blocks between failures, the GW has a chance to recover as he reduces his penalty by 1 for every block
            // If it however fails 3 times in the timespan of a single payment cycle, it should definitely get banned
            gwList.PoSePunish(members[i]->proTxHash, gwList.CalcPenalty(66), debugLogs);
        }
    }
}

void CDeterministicGWManager::DecreasePoSePenalties(CDeterministicGWList& gwList)
{
    std::vector<uint256> toDecrease;
    toDecrease.reserve(gwList.GetValidGWsCount() / 10);
    // only iterate and decrease for valid ones (not PoSe banned yet)
    // if a GW ever reaches the maximum, it stays in PoSe banned state until revived
    gwList.ForEachGW(true, [&](const CDeterministicGWCPtr& dgw) {
        if (dgw->pdgwState->nPoSePenalty > 0 && dgw->pdgwState->nPoSeBanHeight == -1) {
            toDecrease.emplace_back(dgw->proTxHash);
        }
    });

    for (const auto& proTxHash : toDecrease) {
        gwList.PoSeDecrease(proTxHash);
    }
}

CDeterministicGWList CDeterministicGWManager::GetListForBlock(const uint256& blockHash)
{
    LOCK(cs);

    auto it = gwListsCache.find(blockHash);
    if (it != gwListsCache.end()) {
        return it->second;
    }

    uint256 blockHashTmp = blockHash;
    CDeterministicGWList snapshot;
    std::list<CDeterministicGWListDiff> listDiff;

    while (true) {
        // try using cache before reading from disk
        it = gwListsCache.find(blockHashTmp);
        if (it != gwListsCache.end()) {
            snapshot = it->second;
            break;
        }

        if (evoDb.Read(std::make_pair(DB_LIST_SNAPSHOT, blockHashTmp), snapshot)) {
            break;
        }

        CDeterministicGWListDiff diff;
        if (!evoDb.Read(std::make_pair(DB_LIST_DIFF, blockHashTmp), diff)) {
            snapshot = CDeterministicGWList(blockHashTmp, -1);
            break;
        }

        listDiff.emplace_front(diff);
        blockHashTmp = diff.prevBlockHash;
    }

    for (const auto& diff : listDiff) {
        if (diff.HasChanges()) {
            snapshot = snapshot.ApplyDiff(diff);
        } else {
            snapshot.SetBlockHash(diff.blockHash);
            snapshot.SetHeight(diff.nHeight);
        }
    }

    gwListsCache.emplace(blockHash, snapshot);
    return snapshot;
}

CDeterministicGWList CDeterministicGWManager::GetListAtChainTip()
{
    LOCK(cs);
    return GetListForBlock(tipBlockHash);
}

bool CDeterministicGWManager::HasValidGWCollateralAtChainTip(const COutPoint& outpoint)
{
    auto gwList = GetListAtChainTip();
    auto dgw = gwList.GetGWByCollateral(outpoint);
    return dgw && gwList.IsGWValid(dgw);
}

bool CDeterministicGWManager::HasGWCollateralAtChainTip(const COutPoint& outpoint)
{
    auto gwList = GetListAtChainTip();
    auto dgw = gwList.GetGWByCollateral(outpoint);
    return dgw != nullptr;
}

int64_t CDeterministicGWManager::GetSpork15Value()
{
    return sporkManager.GetSporkValue(SPORK_15_DETERMINISTIC_GWS_ENABLED);
}

bool CDeterministicGWManager::IsProTxWithCollateral(const CTransactionRef& tx, uint32_t n)
{
    if (tx->nVersion != 3 || tx->nType != TRANSACTION_PROVIDER_REGISTER) {
        return false;
    }
    CProRegTx proTx;
    if (!GetTxPayload(*tx, proTx)) {
        return false;
    }

    if (!proTx.collateralOutpoint.hash.IsNull()) {
        return false;
    }
    if (proTx.collateralOutpoint.n >= tx->vout.size() || proTx.collateralOutpoint.n != n) {
        return false;
    }
    if (tx->vout[n].nValue != COLLATERAL_COINS) {
        return false;
    }
    return true;
}

bool CDeterministicGWManager::IsDeterministicGWsSporkActive(int nHeight)
{
    LOCK(cs);

    if (nHeight == -1) {
        nHeight = tipHeight;
    }

    int64_t spork15Value = GetSpork15Value();
    return nHeight >= spork15Value;
}

void CDeterministicGWManager::CleanupCache(int nHeight)
{
    AssertLockHeld(cs);

    std::vector<uint256> toDelete;
    for (const auto& p : gwListsCache) {
        if (p.second.GetHeight() + LISTS_CACHE_SIZE < nHeight) {
            toDelete.emplace_back(p.first);
        }
    }
    for (const auto& h : toDelete) {
        gwListsCache.erase(h);
    }
}
