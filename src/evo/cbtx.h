// Copyright (c) 2017-2018 The Dash Core developers
// Copyright (c) 2017-2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SOOM_CBTX_H
#define SOOM_CBTX_H

#include "consensus/validation.h"
#include "primitives/transaction.h"

class CBlock;
class CBlockIndex;
class UniValue;

// coinbase transaction
class CCbTx
{
public:
    static const uint16_t CURRENT_VERSION = 1;

public:
    uint16_t nVersion{CURRENT_VERSION};
    int32_t nHeight{0};
    uint256 merkleRootGWList;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nHeight);
        READWRITE(merkleRootGWList);
    }

    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};

bool CheckCbTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state);

bool CheckCbTxMerkleRootGWList(const CBlock& block, const CBlockIndex* pindex, CValidationState& state);
bool CalcCbTxMerkleRootGWList(const CBlock& block, const CBlockIndex* pindexPrev, uint256& merkleRootRet, CValidationState& state);

#endif //SOOM_CBTX_H
