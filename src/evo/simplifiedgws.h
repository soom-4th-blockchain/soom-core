// Copyright (c) 2017-2018 The Dash Core developers
// Copyright (c) 2017-2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SOOM_SIMPLIFIEDGWS_H
#define SOOM_SIMPLIFIEDGWS_H

#include "bls/bls.h"
#include "merkleblock.h"
#include "netaddress.h"
#include "pubkey.h"
#include "serialize.h"

class UniValue;
class CDeterministicGWList;
class CDeterministicGW;

class CSimplifiedGWListEntry
{
public:
    uint256 proRegTxHash;
    uint256 confirmedHash;
    CService service;
    CBLSPublicKey pubKeyOperator;
    CKeyID keyIDVoting;
    bool isValid;

public:
    CSimplifiedGWListEntry() {}
    CSimplifiedGWListEntry(const CDeterministicGW& dgw);

    bool operator==(const CSimplifiedGWListEntry& rhs) const
    {
        return proRegTxHash == rhs.proRegTxHash &&
               confirmedHash == rhs.confirmedHash &&
               service == rhs.service &&
               pubKeyOperator == rhs.pubKeyOperator &&
               keyIDVoting == rhs.keyIDVoting &&
               isValid == rhs.isValid;
    }

    bool operator!=(const CSimplifiedGWListEntry& rhs) const
    {
        return !(rhs == *this);
    }

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(proRegTxHash);
        READWRITE(confirmedHash);
        READWRITE(service);
        READWRITE(pubKeyOperator);
        READWRITE(keyIDVoting);
        READWRITE(isValid);
    }

public:
    uint256 CalcHash() const;

    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

class CSimplifiedGWList
{
public:
    std::vector<CSimplifiedGWListEntry> gwList;

public:
    CSimplifiedGWList() {}
    CSimplifiedGWList(const std::vector<CSimplifiedGWListEntry>& smlEntries);
    CSimplifiedGWList(const CDeterministicGWList& dgwList);

    uint256 CalcMerkleRoot(bool* pmutated = NULL) const;
};

/// P2P messages

class CGetSimplifiedGWListDiff
{
public:
    uint256 baseBlockHash;
    uint256 blockHash;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(baseBlockHash);
        READWRITE(blockHash);
    }
};

class CSimplifiedGWListDiff
{
public:
    uint256 baseBlockHash;
    uint256 blockHash;
    CPartialMerkleTree cbTxMerkleTree;
    CTransactionRef cbTx;
    std::vector<uint256> deletedGWs;
    std::vector<CSimplifiedGWListEntry> gwList;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(baseBlockHash);
        READWRITE(blockHash);
        READWRITE(cbTxMerkleTree);
        READWRITE(cbTx);
        READWRITE(deletedGWs);
        READWRITE(gwList);
    }

public:
    void ToJson(UniValue& obj) const;
};

bool BuildSimplifiedGWListDiff(const uint256& baseBlockHash, const uint256& blockHash, CSimplifiedGWListDiff& gwListDiffRet, std::string& errorRet);

#endif //SOOM_SIMPLIFIEDGWS_H
