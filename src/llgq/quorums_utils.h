// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SOOM_QUORUMS_UTILS_H
#define SOOM_QUORUMS_UTILS_H

#include "consensus/params.h"

#include "evo/deterministicgws.h"

#include <vector>

namespace llgq
{

class CLLGQUtils
{
public:
    // includes members which failed DKG
    static std::vector<CDeterministicGWCPtr> GetAllQuorumMembers(Consensus::LLGQType llgqType, const uint256& blockHash);

    static uint256 BuildCommitmentHash(uint8_t llgqType, const uint256& blockHash, const std::vector<bool>& validMembers, const CBLSPublicKey& pubKey, const uint256& vvecHash);
};

}

#endif//SOOM_QUORUMS_UTILS_H
