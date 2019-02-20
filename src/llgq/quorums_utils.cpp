// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "quorums_utils.h"

#include "chainparams.h"
#include "random.h"

namespace llgq
{

std::vector<CDeterministicGWCPtr> CLLGQUtils::GetAllQuorumMembers(Consensus::LLGQType llgqType, const uint256& blockHash)
{
    auto& params = Params().GetConsensus().llgqs.at(llgqType);
    auto allGws = deterministicGWManager->GetListForBlock(blockHash);
    auto modifier = ::SerializeHash(std::make_pair((uint8_t)llgqType, blockHash));
    return allGws.CalculateQuorum(params.size, modifier);
}

uint256 CLLGQUtils::BuildCommitmentHash(uint8_t llgqType, const uint256& blockHash, const std::vector<bool>& validMembers, const CBLSPublicKey& pubKey, const uint256& vvecHash)
{
    CHashWriter hw(SER_NETWORK, 0);
    hw << llgqType;
    hw << blockHash;
    hw << DYNBITSET(validMembers);
    hw << pubKey;
    hw << vvecHash;
    return hw.GetHash();
}


}
