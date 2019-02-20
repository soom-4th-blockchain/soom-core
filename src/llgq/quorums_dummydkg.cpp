// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "quorums_dummydkg.h"

#include "quorums_blockprocessor.h"
#include "quorums_commitment.h"
#include "quorums_utils.h"

#include "evo/specialtx.h"

#include "activegateway.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/validation.h"
#include "net.h"
#include "net_processing.h"
#include "primitives/block.h"
#include "spork.h"
#include "validation.h"

namespace llgq
{

CDummyDKG* quorumDummyDKG;

void CDummyDKG::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (strCommand == NetMsgType::QCONTRIB) {
        if (!sporkManager.IsSporkActive(SPORK_17_QUORUM_DKG_ENABLED)) {
            return;
        }

        CDummyContribution qc;
        try {
            vRecv >> qc;
        } catch (...) {
            // When we switch to the real DKG, non-upgraded nodes will get to this point. Let them just ignore the
            // incompatible messages
            return;
        }

        uint256 hash = ::SerializeHash(qc);
        {
            LOCK(cs_main);
            connman.RemoveAskFor(hash);
        }

        ProcessDummyContribution(pfrom->id, qc);
    } else if (strCommand == NetMsgType::QDCOMMITMENT) {
        if (!Params().GetConsensus().fLLGQAllowDummyCommitments) {
            Misbehaving(pfrom->id, 100);
            return;
        }
        if (!sporkManager.IsSporkActive(SPORK_17_QUORUM_DKG_ENABLED)) {
            return;
        }

        CDummyCommitment qc;
        vRecv >> qc;

        uint256 hash = ::SerializeHash(qc);
        {
            LOCK(cs_main);
            connman.RemoveAskFor(hash);
        }

        ProcessDummyCommitment(pfrom->id, qc);
    }
}

void CDummyDKG::ProcessDummyContribution(NodeId from, const llgq::CDummyContribution& qc)
{
    if (!Params().GetConsensus().llgqs.count((Consensus::LLGQType)qc.llgqType)) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid commitment type %d, peer=%d\n", __func__,
                  qc.llgqType, from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    auto type = (Consensus::LLGQType)qc.llgqType;
    const auto& params = Params().GetConsensus().llgqs.at(type);

    int curQuorumHeight;
    const CBlockIndex* quorumIndex;
    {
        LOCK(cs_main);
        curQuorumHeight = chainActive.Height() - (chainActive.Height() % params.dkgInterval);
        quorumIndex = chainActive[curQuorumHeight];
    }
    uint256 quorumHash = quorumIndex->GetBlockHash();
    if (qc.quorumHash != quorumHash) {
        LogPrintf("CDummyDKG::%s -- dummy contrinution for wrong quorum, peer=%d\n", __func__,
                  from);
        return;
    }

    auto members = CLLGQUtils::GetAllQuorumMembers(type, qc.quorumHash);
    if (members.size() != params.size) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid members count %d, peer=%d\n", __func__,
                  members.size(), from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }
    if (qc.signer >= members.size()) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid signer %d, peer=%d\n", __func__,
                  qc.signer, from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    auto signer = members[qc.signer];

    {
        LOCK(sessionCs);
        if (curSessions[type].dummyContributionsFromMembers.count(signer->proTxHash)) {
            return;
        }
    }

    // verify member sig
    if (!qc.sig.VerifyInsecure(signer->pdgwState->pubKeyOperator, qc.GetSignHash())) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid memberSig, peer=%d\n", __func__,
                  from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    LogPrintf("CDummyDKG::%s -- processed dummy contribution for quorum %s:%d, signer=%d, peer=%d\n", __func__,
              qc.quorumHash.ToString(), qc.llgqType, qc.signer, from);

    uint256 hash = ::SerializeHash(qc);
    {
        LOCK(sessionCs);
        curSessions[type].dummyContributions[hash] = qc;
        curSessions[type].dummyContributionsFromMembers[signer->proTxHash] = hash;
    }

    CInv inv(MSG_QUORUM_DUMMY_CONTRIBUTION, hash);
    g_connman->RelayInv(inv, DGW_PROTO_VERSION);
}

void CDummyDKG::ProcessDummyCommitment(NodeId from, const llgq::CDummyCommitment& qc)
{
    if (!Params().GetConsensus().llgqs.count((Consensus::LLGQType)qc.llgqType)) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid commitment type %d, peer=%d\n", __func__,
                  qc.llgqType, from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    auto type = (Consensus::LLGQType)qc.llgqType;
    const auto& params = Params().GetConsensus().llgqs.at(type);

    if (qc.validMembers.size() != params.size) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid validMembers size %d, peer=%d\n", __func__,
                  qc.validMembers.size(), from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    int curQuorumHeight;
    const CBlockIndex* quorumIndex;
    {
        LOCK(cs_main);
        curQuorumHeight = chainActive.Height() - (chainActive.Height() % params.dkgInterval);
        quorumIndex = chainActive[curQuorumHeight];
    }
    uint256 quorumHash = quorumIndex->GetBlockHash();
    if (qc.quorumHash != quorumHash) {
        LogPrintf("CDummyDKG::%s -- dummy commitment for wrong quorum, peer=%d\n", __func__,
                  from);
        return;
    }

    auto members = CLLGQUtils::GetAllQuorumMembers(type, qc.quorumHash);
    if (members.size() != params.size) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid members count %d, peer=%d\n", __func__,
                  members.size(), from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }
    if (qc.signer >= members.size()) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid signer %d, peer=%d\n", __func__,
                  qc.signer, from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }
    if (qc.CountValidMembers() < params.minSize) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid validMembers count %d, peer=%d\n", __func__,
                  qc.CountValidMembers(), from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    auto signer = members[qc.signer];

    {
        LOCK(sessionCs);
        for (const auto& p : curSessions[type].dummyCommitmentsFromMembers) {
            if (p.second.count(signer->proTxHash)) {
                return;
            }
        }
    }

    auto svec = BuildDeterministicSvec(type, qc.quorumHash);
    auto vvec = BuildVvec(svec);
    auto vvecHash = ::SerializeHash(vvec);

    auto commitmentHash = CLLGQUtils::BuildCommitmentHash((uint8_t)type, qc.quorumHash, qc.validMembers, vvec[0], vvecHash);

    // verify member sig
    if (!qc.membersSig.VerifyInsecure(signer->pdgwState->pubKeyOperator, commitmentHash)) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid memberSig, peer=%d\n", __func__,
                  from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    // recover public key share
    CBLSPublicKey sharePk;
    if (!sharePk.PublicKeyShare(vvec, CBLSId::FromHash(signer->proTxHash))) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- failed to recover public key share, peer=%d\n", __func__,
                  from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    // verify sig share
    if (!qc.quorumSig.VerifyInsecure(sharePk, commitmentHash)) {
        LOCK(cs_main);
        LogPrintf("CDummyDKG::%s -- invalid quorumSig, peer=%d\n", __func__,
                  from);
        if (from != -1) {
            Misbehaving(from, 100);
        }
        return;
    }

    LogPrintf("CDummyDKG::%s -- processed dummy commitment for quorum %s:%d, validMembers=%d, signer=%d, peer=%d\n", __func__,
              qc.quorumHash.ToString(), qc.llgqType, qc.CountValidMembers(), qc.signer, from);

    uint256 hash = ::SerializeHash(qc);
    {
        LOCK(sessionCs);
        curSessions[type].dummyCommitments[hash] = qc;
        curSessions[type].dummyCommitmentsFromMembers[commitmentHash][signer->proTxHash] = hash;
    }

    CInv inv(MSG_QUORUM_DUMMY_COMMITMENT, hash);
    g_connman->RelayInv(inv, DGW_PROTO_VERSION);
}

void CDummyDKG::UpdatedBlockTip(const CBlockIndex* pindex, bool fInitialDownload)
{
    if (fInitialDownload) {
        return;
    }

    if (!fGatewayMode) {
        return;
    }

    bool fDIP0003Active = VersionBitsState(chainActive.Tip(), Params().GetConsensus(), Consensus::DEPLOYMENT_DIP0003, versionbitscache) == THRESHOLD_ACTIVE;
    if (!fDIP0003Active) {
        return;
    }

    if (!sporkManager.IsSporkActive(SPORK_17_QUORUM_DKG_ENABLED)) {
        return;
    }

    for (const auto& p : Params().GetConsensus().llgqs) {
        const auto& params = p.second;
        int phaseIndex = pindex->nHeight % params.dkgInterval;
        if (phaseIndex == 0) {
            CreateDummyContribution(params.type, pindex);
        } else if (phaseIndex == params.dkgPhaseBlocks * 2) {
            CreateDummyCommitment(params.type, pindex);
        } else if (phaseIndex == params.dkgPhaseBlocks * 4) {
            CreateFinalCommitment(params.type, pindex);
        }
    }
}

void CDummyDKG::CreateDummyContribution(Consensus::LLGQType llgqType, const CBlockIndex* pindex)
{
    const auto& params = Params().GetConsensus().llgqs.at(llgqType);
    int quorumHeight = pindex->nHeight - (pindex->nHeight % params.dkgInterval);
    const CBlockIndex* quorumIndex;
    {
        LOCK(cs_main);
        quorumIndex = chainActive[quorumHeight];
    }
    uint256 quorumHash = quorumIndex->GetBlockHash();

    auto members = CLLGQUtils::GetAllQuorumMembers(llgqType, quorumHash);
    if (members.size() != params.size) {
        return;
    }

    int myIdx = -1;
    for (size_t i = 0; i < members.size(); i++) {
        if (members[i]->collateralOutpoint == activeGatewayInfo.outpoint) {
            myIdx = (int)i;
            break;
        }
    }
    if (myIdx == -1) {
        return;
    }
    auto signer = members[myIdx];

    CDummyContribution qc;
    qc.llgqType = (uint8_t)llgqType;
    qc.quorumHash = quorumHash;
    qc.signer = (uint16_t)myIdx;
    qc.sig = activeGatewayInfo.blsKeyOperator->Sign(qc.GetSignHash());

    ProcessDummyContribution(-1, qc);
}

void CDummyDKG::CreateDummyCommitment(Consensus::LLGQType llgqType, const CBlockIndex* pindex)
{
    const auto& params = Params().GetConsensus().llgqs.at(llgqType);
    int quorumHeight = pindex->nHeight - (pindex->nHeight % params.dkgInterval);
    const CBlockIndex* quorumIndex;
    {
        LOCK(cs_main);
        quorumIndex = chainActive[quorumHeight];
    }
    uint256 quorumHash = quorumIndex->GetBlockHash();

    auto members = CLLGQUtils::GetAllQuorumMembers(llgqType, quorumHash);
    if (members.size() != params.size) {
        return;
    }

    int myIdx = -1;
    for (size_t i = 0; i < members.size(); i++) {
        if (members[i]->collateralOutpoint == activeGatewayInfo.outpoint) {
            myIdx = (int)i;
            break;
        }
    }
    if (myIdx == -1) {
        return;
    }
    auto signer = members[myIdx];

    auto svec = BuildDeterministicSvec(llgqType, quorumHash);
    auto vvec = BuildVvec(svec);
    auto vvecHash = ::SerializeHash(vvec);
    auto validMembers = GetValidMembers(llgqType, members);
    if (std::count(validMembers.begin(), validMembers.end(), true) < params.minSize) {
        return;
    }

    auto commitmentHash = CLLGQUtils::BuildCommitmentHash((uint8_t)llgqType, quorumHash, validMembers, vvec[0], vvecHash);

    CDummyCommitment qc;
    qc.llgqType = (uint8_t)llgqType;
    qc.quorumHash = quorumHash;
    qc.signer = (uint16_t)myIdx;
    qc.validMembers = validMembers;

    qc.membersSig = activeGatewayInfo.blsKeyOperator->Sign(commitmentHash);

    CBLSSecretKey skShare;
    if (!skShare.SecretKeyShare(svec, CBLSId::FromHash(signer->proTxHash))) {
        return;
    }
    qc.quorumSig = skShare.Sign(commitmentHash);

    ProcessDummyCommitment(-1, qc);
}

void CDummyDKG::CreateFinalCommitment(Consensus::LLGQType llgqType, const CBlockIndex* pindex)
{
    const auto& params = Params().GetConsensus().llgqs.at(llgqType);
    int quorumHeight = pindex->nHeight - (pindex->nHeight % params.dkgInterval);
    const CBlockIndex* quorumIndex;
    {
        LOCK(cs_main);
        quorumIndex = chainActive[quorumHeight];
    }
    uint256 quorumHash = quorumIndex->GetBlockHash();

    auto members = CLLGQUtils::GetAllQuorumMembers(llgqType, quorumHash);
    if (members.size() != params.size) {
        return;
    }

    auto svec = BuildDeterministicSvec(llgqType, quorumHash);
    auto vvec = BuildVvec(svec);
    auto vvecHash = ::SerializeHash(vvec);

    LOCK(sessionCs);
    auto& session = curSessions[llgqType];

    for (const auto& p : session.dummyCommitmentsFromMembers) {
        const auto& commitmentHash = p.first;
        if (p.second.size() < params.minSize) {
            continue;
        }

        const auto& firstQc = session.dummyCommitments[p.second.begin()->second];

        CFinalCommitment fqc(params, quorumHash);
        fqc.validMembers = firstQc.validMembers;
        fqc.quorumPublicKey = vvec[0];
        fqc.quorumVvecHash = vvecHash;

        std::vector<CBLSSignature> quorumSigs;
        std::vector<CBLSId> quorumSigIds;
        std::vector<CBLSSignature> memberSigs;
        std::vector<CBLSPublicKey> memberPubKeys;

        for (const auto& p2 : p.second) {
            const auto& proTxHash = p2.first;
            const auto& qc = session.dummyCommitments[p2.second];

            int signerIdx = -1;
            for (size_t i = 0; i < members.size(); i++) {
                if (members[i]->proTxHash == proTxHash) {
                    signerIdx = (int)i;
                    break;
                }
            }
            if (signerIdx == -1) {
                break;
            }
            fqc.signers[signerIdx] = true;
            if (quorumSigs.size() < params.threshold) {
                quorumSigs.emplace_back(qc.quorumSig);
                quorumSigIds.emplace_back(CBLSId::FromHash(proTxHash));
            }
            memberSigs.emplace_back(qc.membersSig);
            memberPubKeys.emplace_back(members[signerIdx]->pdgwState->pubKeyOperator);
        }

        if (!fqc.quorumSig.Recover(quorumSigs, quorumSigIds)) {
            LogPrintf("CDummyDKG::%s -- recovery failed for quorum %s:%d, validMembers=%d, signers=%d\n", __func__,
                      fqc.quorumHash.ToString(), fqc.llgqType, fqc.CountValidMembers(), fqc.CountSigners());
            continue;
        }
        fqc.membersSig = CBLSSignature::AggregateSecure(memberSigs, memberPubKeys, commitmentHash);

        if (!fqc.Verify(members, true)) {
            LogPrintf("CDummyDKG::%s -- self created final commitment is invalid for quorum %s:%d, validMembers=%d, signers=%d\n", __func__,
                      fqc.quorumHash.ToString(), fqc.llgqType, fqc.CountValidMembers(), fqc.CountSigners());
            continue;
        }

        LogPrintf("CDummyDKG::%s -- self created final commitment for quorum %s:%d, validMembers=%d, signers=%d\n", __func__,
                  fqc.quorumHash.ToString(), fqc.llgqType, fqc.CountValidMembers(), fqc.CountSigners());
        quorumBlockProcessor->AddMinableCommitment(fqc);
    }

    // reset for next round
    curSessions[llgqType] = CDummyDKGSession();
}

std::vector<bool> CDummyDKG::GetValidMembers(Consensus::LLGQType llgqType, const std::vector<CDeterministicGWCPtr>& members)
{
    const auto& params = Params().GetConsensus().llgqs.at(llgqType);
    std::vector<bool> ret(params.size, false);

    LOCK(sessionCs);

    // Valid members are members that sent us a dummy contribution in this session

    for (size_t i = 0; i < params.size; i++) {
        if (curSessions[llgqType].dummyContributionsFromMembers.count(members[i]->proTxHash)) {
            ret[i] = true;
        }
    }

    return ret;
}

// The returned secret key vector is NOT SECURE AT ALL!!
// It is known by everyone. This is only for testnet/devnet/regtest, so this is fine. Also, we won't do any meaningful
// things with the commitments. This is only needed to make the final commitments validate
BLSSecretKeyVector CDummyDKG::BuildDeterministicSvec(Consensus::LLGQType llgqType, const uint256& quorumHash)
{
    const auto& params = Params().GetConsensus().llgqs.at(llgqType);

    CHash256 seed_;
    seed_.Write((unsigned char*)&llgqType, 1);
    seed_.Write(quorumHash.begin(), quorumHash.size());

    uint256 seed;
    seed_.Finalize(seed.begin());

    BLSSecretKeyVector svec;
    svec.reserve(params.size);
    for (size_t i = 0; i < params.threshold; i++) {
        CBLSSecretKey sk;
        while (true) {
            seed = ::SerializeHash(seed);
            sk.SetBuf(seed.begin(), seed.size());
            if (sk.IsValid()) {
                break;
            }
        }
        svec.emplace_back(sk);
    }

    return svec;
}

BLSPublicKeyVector CDummyDKG::BuildVvec(const BLSSecretKeyVector& svec)
{
    BLSPublicKeyVector vvec;
    vvec.reserve(svec.size());
    for (size_t i = 0; i < svec.size(); i++) {
        vvec.emplace_back(svec[i].GetPublicKey());
    }
    return vvec;
}

bool CDummyDKG::HasDummyContribution(const uint256& hash)
{
    LOCK(sessionCs);
    for (const auto& p : curSessions) {
        auto it = p.second.dummyContributions.find(hash);
        if (it != p.second.dummyContributions.end()) {
            return true;
        }
    }
    return false;
}

bool CDummyDKG::GetDummyContribution(const uint256& hash, CDummyContribution& ret)
{
    LOCK(sessionCs);
    for (const auto& p : curSessions) {
        auto it = p.second.dummyContributions.find(hash);
        if (it != p.second.dummyContributions.end()) {
            ret = it->second;
            return true;
        }
    }
    return false;
}

bool CDummyDKG::HasDummyCommitment(const uint256& hash)
{
    LOCK(sessionCs);
    for (const auto& p : curSessions) {
        auto it = p.second.dummyCommitments.find(hash);
        if (it != p.second.dummyCommitments.end()) {
            return true;
        }
    }
    return false;
}

bool CDummyDKG::GetDummyCommitment(const uint256& hash, CDummyCommitment& ret)
{
    LOCK(sessionCs);
    for (const auto& p : curSessions) {
        auto it = p.second.dummyCommitments.find(hash);
        if (it != p.second.dummyCommitments.end()) {
            ret = it->second;
            return true;
        }
    }
    return false;
}

}
