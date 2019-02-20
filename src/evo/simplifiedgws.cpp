// Copyright (c) 2017-2018 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cbtx.h"
#include "core_io.h"
#include "deterministicgws.h"
#include "simplifiedgws.h"
#include "specialtx.h"

#include "base58.h"
#include "chainparams.h"
#include "consensus/merkle.h"
#include "univalue.h"
#include "validation.h"

CSimplifiedGWListEntry::CSimplifiedGWListEntry(const CDeterministicGW& dgw) :
    proRegTxHash(dgw.proTxHash),
    confirmedHash(dgw.pdgwState->confirmedHash),
    service(dgw.pdgwState->addr),
    pubKeyOperator(dgw.pdgwState->pubKeyOperator),
    keyIDVoting(dgw.pdgwState->keyIDVoting),
    isValid(dgw.pdgwState->nPoSeBanHeight == -1)
{
}

uint256 CSimplifiedGWListEntry::CalcHash() const
{
    CHashWriter hw(SER_GETHASH, CLIENT_VERSION);
    hw << *this;
    return hw.GetHash();
}

std::string CSimplifiedGWListEntry::ToString() const
{
    return strprintf("CSimplifiedGWListEntry(proRegTxHash=%s, confirmedHash=%s, service=%s, pubKeyOperator=%s, votingAddress=%s, isValie=%d)",
        proRegTxHash.ToString(), confirmedHash.ToString(), service.ToString(false), pubKeyOperator.ToString(), CBitcoinAddress(keyIDVoting).ToString(), isValid);
}

void CSimplifiedGWListEntry::ToJson(UniValue& obj) const
{
    obj.clear();
    obj.setObject();
    obj.push_back(Pair("proRegTxHash", proRegTxHash.ToString()));
    obj.push_back(Pair("confirmedHash", confirmedHash.ToString()));
    obj.push_back(Pair("service", service.ToString(false)));
    obj.push_back(Pair("pubKeyOperator", pubKeyOperator.ToString()));
    obj.push_back(Pair("votingAddress", CBitcoinAddress(keyIDVoting).ToString()));
    obj.push_back(Pair("isValid", isValid));
}

CSimplifiedGWList::CSimplifiedGWList(const std::vector<CSimplifiedGWListEntry>& smlEntries)
{
    gwList = smlEntries;

    std::sort(gwList.begin(), gwList.end(), [&](const CSimplifiedGWListEntry& a, const CSimplifiedGWListEntry& b) {
        return a.proRegTxHash.Compare(b.proRegTxHash) < 0;
    });
}

CSimplifiedGWList::CSimplifiedGWList(const CDeterministicGWList& dgwList)
{
    gwList.reserve(dgwList.GetAllGWsCount());

    dgwList.ForEachGW(false, [this](const CDeterministicGWCPtr& dgw) {
        gwList.emplace_back(*dgw);
    });

    std::sort(gwList.begin(), gwList.end(), [&](const CSimplifiedGWListEntry& a, const CSimplifiedGWListEntry& b) {
        return a.proRegTxHash.Compare(b.proRegTxHash) < 0;
    });
}

uint256 CSimplifiedGWList::CalcMerkleRoot(bool* pmutated) const
{
    std::vector<uint256> leaves;
    leaves.reserve(gwList.size());
    for (const auto& e : gwList) {
        leaves.emplace_back(e.CalcHash());
    }
    return ComputeMerkleRoot(leaves, pmutated);
}

void CSimplifiedGWListDiff::ToJson(UniValue& obj) const
{
    obj.setObject();

    obj.push_back(Pair("baseBlockHash", baseBlockHash.ToString()));
    obj.push_back(Pair("blockHash", blockHash.ToString()));

    CDataStream ssCbTxMerkleTree(SER_NETWORK, PROTOCOL_VERSION);
    ssCbTxMerkleTree << cbTxMerkleTree;
    obj.push_back(Pair("cbTxMerkleTree", HexStr(ssCbTxMerkleTree.begin(), ssCbTxMerkleTree.end())));

    obj.push_back(Pair("cbTx", EncodeHexTx(*cbTx)));

    UniValue deletedGWsArr(UniValue::VARR);
    for (const auto& h : deletedGWs) {
        deletedGWsArr.push_back(h.ToString());
    }
    obj.push_back(Pair("deletedGWs", deletedGWsArr));

    UniValue gwListArr(UniValue::VARR);
    for (const auto& e : gwList) {
        UniValue eObj;
        e.ToJson(eObj);
        gwListArr.push_back(eObj);
    }
    obj.push_back(Pair("gwList", gwListArr));

    CCbTx cbTxPayload;
    if (GetTxPayload(*cbTx, cbTxPayload)) {
        obj.push_back(Pair("merkleRootGWList", cbTxPayload.merkleRootGWList.ToString()));
    }
}

bool BuildSimplifiedGWListDiff(const uint256& baseBlockHash, const uint256& blockHash, CSimplifiedGWListDiff& gwListDiffRet, std::string& errorRet)
{
    AssertLockHeld(cs_main);
    gwListDiffRet = CSimplifiedGWListDiff();

    BlockMap::iterator baseBlockIt = mapBlockIndex.begin();
    if (!baseBlockHash.IsNull()) {
        baseBlockIt = mapBlockIndex.find(baseBlockHash);
    }
    auto blockIt = mapBlockIndex.find(blockHash);
    if (baseBlockIt == mapBlockIndex.end()) {
        errorRet = strprintf("block %s not found", baseBlockHash.ToString());
        return false;
    }
    if (blockIt == mapBlockIndex.end()) {
        errorRet = strprintf("block %s not found", blockHash.ToString());
        return false;
    }

    if (!chainActive.Contains(baseBlockIt->second) || !chainActive.Contains(blockIt->second)) {
        errorRet = strprintf("block %s and %s are not in the same chain", baseBlockHash.ToString(), blockHash.ToString());
        return false;
    }
    if (baseBlockIt->second->nHeight > blockIt->second->nHeight) {
        errorRet = strprintf("base block %s is higher then block %s", baseBlockHash.ToString(), blockHash.ToString());
        return false;
    }

    LOCK(deterministicGWManager->cs);

    auto baseDgwList = deterministicGWManager->GetListForBlock(baseBlockHash);
    auto dgwList = deterministicGWManager->GetListForBlock(blockHash);
    gwListDiffRet = baseDgwList.BuildSimplifiedDiff(dgwList);

    // TODO store coinbase TX in CBlockIndex
    CBlock block;
    if (!ReadBlockFromDisk(block, blockIt->second, Params().GetConsensus())) {
        errorRet = strprintf("failed to read block %s from disk", blockHash.ToString());
        return false;
    }

    gwListDiffRet.cbTx = block.vtx[0];

    std::vector<uint256> vHashes;
    std::vector<bool> vMatch(block.vtx.size(), false);
    for (const auto& tx : block.vtx) {
        vHashes.emplace_back(tx->GetHash());
    }
    vMatch[0] = true; // only coinbase matches
    gwListDiffRet.cbTxMerkleTree = CPartialMerkleTree(vHashes, vMatch);

    return true;
}
