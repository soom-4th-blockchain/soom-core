// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SOOM_QUORUMS_BLOCKPROCESSOR_H
#define SOOM_QUORUMS_BLOCKPROCESSOR_H

#include "llgq/quorums_commitment.h"

#include "consensus/params.h"
#include "primitives/transaction.h"
#include "sync.h"

#include <map>

class CNode;
class CConnman;

namespace llgq
{

class CQuorumBlockProcessor
{
private:
    CEvoDB& evoDb;

    // TODO cleanup
    CCriticalSection minableCommitmentsCs;
    std::map<std::pair<Consensus::LLGQType, uint256>, uint256> minableCommitmentsByQuorum;
    std::map<uint256, CFinalCommitment> minableCommitments;

public:
    CQuorumBlockProcessor(CEvoDB& _evoDb) : evoDb(_evoDb) {}

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex);

    void AddMinableCommitment(const CFinalCommitment& fqc);
    bool HasMinableCommitment(const uint256& hash);
    bool GetMinableCommitmentByHash(const uint256& commitmentHash, CFinalCommitment& ret);
    bool GetMinableCommitment(Consensus::LLGQType llgqType, int nHeight, CFinalCommitment& ret);
    bool GetMinableCommitmentTx(Consensus::LLGQType llgqType, int nHeight, CTransactionRef& ret);

    bool HasMinedCommitment(Consensus::LLGQType llgqType, const uint256& quorumHash);
    bool GetMinedCommitment(Consensus::LLGQType llgqType, const uint256& quorumHash, CFinalCommitment& ret);

private:
    bool GetCommitmentsFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, std::map<Consensus::LLGQType, CFinalCommitment>& ret, CValidationState& state);
    bool ProcessCommitment(const CBlockIndex* pindex, const CFinalCommitment& qc, CValidationState& state);
    bool IsMiningPhase(Consensus::LLGQType llgqType, int nHeight);
    bool IsCommitmentRequired(Consensus::LLGQType llgqType, int nHeight);
    uint256 GetQuorumBlockHash(Consensus::LLGQType llgqType, int nHeight);
};

extern CQuorumBlockProcessor* quorumBlockProcessor;

}

#endif//SOOM_QUORUMS_BLOCKPROCESSOR_H
