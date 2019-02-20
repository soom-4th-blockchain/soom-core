// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "consensus/validation.h"
#include "base58.h"
#include "init.h"
#include "gateway-payments.h"
#include "gateway-sync.h"
#include "gatewayman.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "util.h"

#include "evo/deterministicgws.h"

#include <string>

/** Object for who's going to get paid on which blocks */
CGatewayPayments gwpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapGatewayBlocks;
CCriticalSection cs_mapGatewayPaymentVotes;

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*/

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet)
{
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= blockReward);
    if(fDebug) LogPrintf("block.vtx[0]->GetValueOut() %lld <= blockReward %lld\n", block.vtx[0]->GetValueOut(), blockReward);

    if(!isBlockRewardValueMet) {
        strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward",
                                nBlockHeight, block.vtx[0]->GetValueOut(), blockReward);
    }
    return isBlockRewardValueMet;

}


bool IsFoundationTxValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
	CBitcoinAddress FoundationAddress((Params().GetFoundationAddress()));
	CScript FoundationScript = GetScriptForDestination(FoundationAddress.Get());
	CAmount FoundationPayment = 0;

	if(blockReward != 0 )
	{
	    FoundationPayment = blockReward / 10;
	}

    for (const auto& txout : txNew.vout) {
		if (FoundationScript == txout.scriptPubKey && FoundationPayment == txout.nValue) {
			LogPrint("gwpayments", "IsFoundationTxValid -- Found required payment\n");
			return true;
		}
	}

	CTxDestination address1;
	ExtractDestination(FoundationScript, address1);
	CBitcoinAddress address2(address1);

	LogPrintf("IsFoundationTxValid -- ERROR: Missing required payment, payees: '%s', amount: %f SOOM\n", address2.ToString(), (float)(blockReward / 10)/COIN);

	return false;
}


bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
	// foundation transaction reward valid

    const Consensus::Params& consensusParams = Params().GetConsensus();
	if(nBlockHeight >= consensusParams.nFoundationPaymentsStartBlock && blockReward != 0)
	{
		if(!IsFoundationTxValid(txNew, nBlockHeight, blockReward))
		{
			return false;
		}
	}
    if(!gatewaySync.IsSynced() || fLiteMode) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if(fDebug) LogPrintf("%s -- WARNING: Client not synced, skipping block payee checks\n", __func__);
        return true;
    }

    // we can only check gateway payments
    if(gwpayments.IsTransactionValid(txNew, nBlockHeight, blockReward - (blockReward / 10))) {
        LogPrint("gwpayments", "%s -- Valid gateway payment at height %d: %s", __func__, nBlockHeight, txNew.ToString());
        return true;
    }


    if(sporkManager.IsSporkActive(SPORK_8_GATEWAY_PAYMENT_ENFORCEMENT)) {
        LogPrintf("IsBlockPayeeValid -- ERROR: Invalid gateway payment detected at height %d: %s", nBlockHeight, txNew.ToString());
        return false;
    }

    LogPrintf("IsBlockPayeeValid -- WARNING: Gateway payment enforcement is disabled, accepting any payee\n");
    return true;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutGatewayPaymentsRet, CTxOut& txoutFoundationRet)
{
    txoutFoundationRet = CTxOut();

    CScript payee;

    CBitcoinAddress FoundationAddess(Params().GetFoundationAddress());
    CScript FoundationScript = GetScriptForDestination(FoundationAddess.Get());
    CAmount FoundationPayment = 0;

	if(nBlockHeight >= Params().GetConsensus().nFoundationPaymentsStartBlock && blockReward != 0)
	{
		FoundationPayment = blockReward / 10;
        blockReward -= FoundationPayment;
		txNew.vout[0].nValue -= FoundationPayment;
		txoutFoundationRet = CTxOut(FoundationPayment, FoundationScript);
		txNew.vout.push_back(txoutFoundationRet);

		LogPrintf("CGatewayPayments::FillBlockPayee -- foundation payment %lld to %s\n", FoundationPayment, FoundationAddess.ToString());
	}


    // FILL BLOCK PAYEE WITH GATEWAY PAYMENT

    if (!gwpayments.GetGatewayTxOuts(nBlockHeight, blockReward, voutGatewayPaymentsRet)) {
        LogPrint("gwpayments", "%s -- no gateway to pay (GW list probably empty)\n", __func__);
    }
    txNew.vout.insert(txNew.vout.end(), voutGatewayPaymentsRet.begin(), voutGatewayPaymentsRet.end());

    std::string voutGatewayStr;
    for (const auto& txout : voutGatewayPaymentsRet) {
        // subtract GW payment from miner reward
        txNew.vout[0].nValue -= txout.nValue;
        if (!voutGatewayStr.empty())
            voutGatewayStr += ",";
        voutGatewayStr += txout.ToString();
    }
    LogPrint("gwpayments", "%s -- nBlockHeight %d blockReward %lld voutGatewayPaymentsRet \"%s\" txoutFoundationRet %s txNew %s", __func__,
                            nBlockHeight, blockReward, voutGatewayStr, txoutFoundationRet.ToString(), txNew.ToString());
}

std::string GetLegacyRequiredPaymentsString(int nBlockHeight)
{

    // PAY GATEWAY
    return gwpayments.GetRequiredPaymentsString(nBlockHeight);
}

std::string GetRequiredPaymentsString(int nBlockHeight, const CDeterministicGWCPtr &payee)
{
    std::string strPayee = "Unknown";
    if (payee) {
        CTxDestination dest;
        if (!ExtractDestination(payee->pdgwState->scriptPayout, dest))
            assert(false);
        strPayee = CBitcoinAddress(dest).ToString();
    }
    return strPayee;
}

std::map<int, std::string> GetRequiredPaymentsStrings(int nStartHeight, int nEndHeight)
{
    std::map<int, std::string> mapPayments;

    LOCK(cs_main);
    int nChainTipHeight = chainActive.Height();

    bool doProjection = false;
    for(int h = nStartHeight; h < nEndHeight; h++) {
        if (deterministicGWManager->IsDeterministicGWsSporkActive(h)) {
            if (h <= nChainTipHeight) {
                auto payee = deterministicGWManager->GetListForBlock(chainActive[h - 1]->GetBlockHash()).GetGWPayee();
                mapPayments.emplace(h, GetRequiredPaymentsString(h, payee));
            } else {
                doProjection = true;
                break;
            }
        } else {
            mapPayments.emplace(h, GetLegacyRequiredPaymentsString(h));
        }
    }
    if (doProjection) {
        auto projection = deterministicGWManager->GetListAtChainTip().GetProjectedGWPayees(nEndHeight - nChainTipHeight);
        for (size_t i = 0; i < projection.size(); i++) {
            auto payee = projection[i];
            int h = nChainTipHeight + 1 + i;
            mapPayments.emplace(h, GetRequiredPaymentsString(h, payee));
        }
    }

    return mapPayments;
}

void CGatewayPayments::Clear()
{
    LOCK2(cs_mapGatewayBlocks, cs_mapGatewayPaymentVotes);
    mapGatewayBlocks.clear();
    mapGatewayPaymentVotes.clear();
}

bool CGatewayPayments::UpdateLastVote(const CGatewayPaymentVote& vote)
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive())
        return false;

    LOCK(cs_mapGatewayPaymentVotes);

    const auto it = mapGatewaysLastVote.find(vote.gatewayOutpoint);
    if (it != mapGatewaysLastVote.end()) {
        if (it->second == vote.nBlockHeight)
            return false;
        it->second = vote.nBlockHeight;
        return true;
    }

    //record this gateway voted
    mapGatewaysLastVote.emplace(vote.gatewayOutpoint, vote.nBlockHeight);
    return true;
}

/**
*   GetGatewayTxOuts
*
*   Get gateway payment tx outputs
*/

bool CGatewayPayments::GetGatewayTxOuts(int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutGatewayPaymentsRet) const
{
    // make sure it's not filled yet
    voutGatewayPaymentsRet.clear();

    if(!GetBlockTxOuts(nBlockHeight, blockReward, voutGatewayPaymentsRet)) {
        if (deterministicGWManager->IsDeterministicGWsSporkActive(nBlockHeight)) {
            LogPrintf("CGatewayPayments::%s -- deterministic gateway lists enabled and no payee\n", __func__);
            return false;
        }

        // no gateway detected...
        int nCount = 0;
        gateway_info_t gwInfo;
        if(!gwnodeman.GetNextGatewayInQueueForPayment(nBlockHeight, true, nCount, gwInfo)) {
            // ...and we can't calculate it on our own
            LogPrintf("CGatewayPayments::%s -- Failed to detect gateway to pay\n", __func__);
            return false;
        }
        // fill payee with locally calculated winner and hope for the best
        CScript payee = GetScriptForDestination(gwInfo.keyIDCollateralAddress);
        CAmount gatewayPayment = GetGatewayPayment(nBlockHeight, blockReward);
        voutGatewayPaymentsRet.emplace_back(gatewayPayment, payee);
    }

    for (const auto& txout : voutGatewayPaymentsRet) {
        CTxDestination address1;
        ExtractDestination(txout.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CGatewayPayments::%s -- Gateway payment %lld to %s\n", __func__, txout.nValue, address2.ToString());
    }

    return true;
}

int CGatewayPayments::GetMinGatewayPaymentsProto() const {
    return sporkManager.IsSporkActive(SPORK_10_GATEWAY_PAY_UPDATED_NODES)
            ? MIN_GATEWAY_PAYMENT_PROTO_VERSION_2
            : MIN_GATEWAY_PAYMENT_PROTO_VERSION_1;
}

void CGatewayPayments::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive())
        return;

    if(fLiteMode) return; // disable all Soom specific functionality

    if (strCommand == NetMsgType::GATEWAYPAYMENTSYNC) { //Gateway Payments Request Sync

        if(pfrom->nVersion < GetMinGatewayPaymentsProto()) {
            LogPrint("gwpayments", "GATEWAYPAYMENTSYNC -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", GetMinGatewayPaymentsProto())));
            return;
        }

        // Ignore such requests until we are fully synced.
        // We could start processing this after gateway list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!gatewaySync.IsSynced()) return;

        if(netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::GATEWAYPAYMENTSYNC)) {
            LOCK(cs_main);
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("GATEWAYPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::GATEWAYPAYMENTSYNC);

        Sync(pfrom, connman);
        LogPrintf("GATEWAYPAYMENTSYNC -- Sent Gateway payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::GATEWAYPAYMENTVOTE) { // Gateway Payments Vote for the Winner

        CGatewayPaymentVote vote;
        vRecv >> vote;

        if(pfrom->nVersion < GetMinGatewayPaymentsProto()) {
            LogPrint("gwpayments", "GATEWAYPAYMENTVOTE -- peer=%d using obsolete version %i\n", pfrom->id, pfrom->nVersion);
            connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                               strprintf("Version must be %d or greater", GetMinGatewayPaymentsProto())));
            return;
        }

        uint256 nHash = vote.GetHash();

        {
            LOCK(cs_main);
            connman.RemoveAskFor(nHash);
        }

        // TODO: clear setAskFor for MSG_GATEWAY_PAYMENT_BLOCK too

        // Ignore any payments messages until gateway list is synced
        if(!gatewaySync.IsGatewayListSynced()) return;

        {
            LOCK(cs_mapGatewayPaymentVotes);

            auto res = mapGatewayPaymentVotes.emplace(nHash, vote);

            // Avoid processing same vote multiple times if it was already verified earlier
            if(!res.second && res.first->second.IsVerified()) {
                LogPrint("gwpayments", "GATEWAYPAYMENTVOTE -- hash=%s, nBlockHeight=%d/%d seen\n",
                            nHash.ToString(), vote.nBlockHeight, nCachedBlockHeight);
                return;
            }

            // Mark vote as non-verified when it's seen for the first time,
            // AddOrUpdatePaymentVote() below should take care of it if vote is actually ok
            res.first->second.MarkAsNotVerified();
        }

        int nFirstBlock = nCachedBlockHeight - GetStorageLimit();
        if(vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > nCachedBlockHeight+20) {
            LogPrint("gwpayments", "GATEWAYPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, nCachedBlockHeight);
            return;
        }

        std::string strError = "";
        if(!vote.IsValid(pfrom, nCachedBlockHeight, strError, connman)) {
            LogPrint("gwpayments", "GATEWAYPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        gateway_info_t gwInfo;
        if(!gwnodeman.GetGatewayInfo(vote.gatewayOutpoint, gwInfo)) {
            // gw was not found, so we can't check vote, some info is probably missing
            LogPrintf("GATEWAYPAYMENTVOTE -- gateway is missing %s\n", vote.gatewayOutpoint.ToStringShort());
            gwnodeman.AskForGW(pfrom, vote.gatewayOutpoint, connman);
            return;
        }

        int nDos = 0;
        if(!vote.CheckSignature(gwInfo.legacyKeyIDOperator, nCachedBlockHeight, nDos)) {
            if(nDos) {
                LOCK(cs_main);
                LogPrintf("GATEWAYPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("gwpayments", "GATEWAYPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            gwnodeman.AskForGW(pfrom, vote.gatewayOutpoint, connman);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a gw which changed its key),
            // so just quit here.
            return;
        }

        if(!UpdateLastVote(vote)) {
            LogPrintf("GATEWAYPAYMENTVOTE -- gateway already voted, gateway=%s\n", vote.gatewayOutpoint.ToStringShort());
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("gwpayments", "GATEWAYPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s, hash=%s new\n",
                    address2.ToString(), vote.nBlockHeight, nCachedBlockHeight, vote.gatewayOutpoint.ToStringShort(), nHash.ToString());

        if(AddOrUpdatePaymentVote(vote)){
            vote.Relay(connman);
            gatewaySync.BumpAssetLastTime("GATEWAYPAYMENTVOTE");
        }
    }
}

uint256 CGatewayPaymentVote::GetHash() const
{
    // Note: doesn't match serialization

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << *(CScriptBase*)(&payee);
    ss << nBlockHeight;
    ss << gatewayOutpoint;
    return ss.GetHash();
}

uint256 CGatewayPaymentVote::GetSignatureHash() const
{
    return SerializeHash(*this);
}

bool CGatewayPaymentVote::Sign()
{
    std::string strError;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if(!CHashSigner::SignHash(hash, activeGatewayInfo.legacyKeyOperator, vchSig)) {
            LogPrintf("CGatewayPaymentVote::%s -- SignHash() failed\n", __func__);
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, activeGatewayInfo.legacyKeyIDOperator, vchSig, strError)) {
            LogPrintf("CGatewayPaymentVote::%s -- VerifyHash() failed, error: %s\n", __func__, strError);
            return false;
        }
    } else {
        std::string strMessage = gatewayOutpoint.ToStringShort() +
                    std::to_string(nBlockHeight) +
                    ScriptToAsmStr(payee);

        if(!CMessageSigner::SignMessage(strMessage, vchSig, activeGatewayInfo.legacyKeyOperator)) {
            LogPrintf("CGatewayPaymentVote::%s -- SignMessage() failed\n", __func__);
            return false;
        }

        if(!CMessageSigner::VerifyMessage(activeGatewayInfo.legacyKeyIDOperator, vchSig, strMessage, strError)) {
            LogPrintf("CGatewayPaymentVote::%s -- VerifyMessage() failed, error: %s\n", __func__, strError);
            return false;
        }
    }

    return true;
}

bool CGatewayPayments::GetBlockTxOuts(int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutGatewayPaymentsRet) const
{
    voutGatewayPaymentsRet.clear();

    CAmount gatewayReward = GetGatewayPayment(nBlockHeight, blockReward);

    if (deterministicGWManager->IsDeterministicGWsSporkActive(nBlockHeight)) {
        uint256 blockHash;
        {
            LOCK(cs_main);
            if (nBlockHeight - 1 > chainActive.Height()) {
                // there are some cases (e.g. IsScheduled) where legacy/compatibility code runs into this method with
                // block heights above the chain tip. Return false in this case
                // TODO remove this when removing the compatibility code and make sure this method is only called with
                // correct block height
                return false;
            }
            blockHash = chainActive[nBlockHeight - 1]->GetBlockHash();
        }
        uint256 proTxHash;
        auto dgwPayee = deterministicGWManager->GetListForBlock(blockHash).GetGWPayee();
        if (!dgwPayee) {
            return false;
        }

        CAmount operatorReward = 0;
        if (dgwPayee->nOperatorReward != 0 && dgwPayee->pdgwState->scriptOperatorPayout != CScript()) {
            // This calculation might eventually turn out to result in 0 even if an operator reward percentage is given.
            // This will however only happen in a few years when the block rewards drops very low.
            operatorReward = (gatewayReward * dgwPayee->nOperatorReward) / 10000;
            gatewayReward -= operatorReward;
        }

        if (gatewayReward > 0) {
            voutGatewayPaymentsRet.emplace_back(gatewayReward, dgwPayee->pdgwState->scriptPayout);
        }
        if (operatorReward > 0) {
            voutGatewayPaymentsRet.emplace_back(operatorReward, dgwPayee->pdgwState->scriptOperatorPayout);
        }

        return true;
    } else {
        LOCK(cs_mapGatewayBlocks);
        auto it = mapGatewayBlocks.find(nBlockHeight);
        CScript payee;
        if (it == mapGatewayBlocks.end() || !it->second.GetBestPayee(payee)) {
            return false;
        }
        voutGatewayPaymentsRet.emplace_back(gatewayReward, payee);
        return true;
    }
}

// Is this gateway scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CGatewayPayments::IsScheduled(const gateway_info_t& gwInfo, int nNotBlockHeight) const
{
    LOCK(cs_mapGatewayBlocks);

    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        auto projectedPayees = deterministicGWManager->GetListAtChainTip().GetProjectedGWPayees(8);
        for (const auto &dgw : projectedPayees) {
            if (dgw->collateralOutpoint == gwInfo.outpoint) {
                return true;
            }
        }
        return false;
    }

    if(!gatewaySync.IsGatewayListSynced()) return false;

    CScript gwpayee;
    gwpayee = GetScriptForDestination(gwInfo.keyIDCollateralAddress);

    for(int64_t h = nCachedBlockHeight; h <= nCachedBlockHeight + 8; h++){
        if(h == nNotBlockHeight) continue;
        std::vector<CTxOut> voutGatewayPayments;
        if(GetBlockTxOuts(h, 0, voutGatewayPayments)) {
            for (const auto& txout : voutGatewayPayments) {
                if (txout.scriptPubKey == gwpayee)
                    return true;
            }
        }
    }

    return false;
}

bool CGatewayPayments::AddOrUpdatePaymentVote(const CGatewayPaymentVote& vote)
{
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    uint256 nVoteHash = vote.GetHash();

    if(HasVerifiedPaymentVote(nVoteHash)) return false;

    LOCK2(cs_mapGatewayBlocks, cs_mapGatewayPaymentVotes);

    mapGatewayPaymentVotes[nVoteHash] = vote;

    auto it = mapGatewayBlocks.emplace(vote.nBlockHeight, CGatewayBlockPayees(vote.nBlockHeight)).first;
    it->second.AddPayee(vote);

    LogPrint("gwpayments", "CGatewayPayments::%s -- added, hash=%s\n", __func__, nVoteHash.ToString());

    return true;
}

bool CGatewayPayments::HasVerifiedPaymentVote(const uint256& hashIn) const
{
    LOCK(cs_mapGatewayPaymentVotes);
    const auto it = mapGatewayPaymentVotes.find(hashIn);
    return it != mapGatewayPaymentVotes.end() && it->second.IsVerified();
}

void CGatewayBlockPayees::AddPayee(const CGatewayPaymentVote& vote)
{
    LOCK(cs_vecPayees);

    uint256 nVoteHash = vote.GetHash();

    for (auto& payee : vecPayees) {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(nVoteHash);
            return;
        }
    }
    CGatewayPayee payeeNew(vote.payee, nVoteHash);
    vecPayees.push_back(payeeNew);
}

bool CGatewayBlockPayees::GetBestPayee(CScript& payeeRet) const
{
    LOCK(cs_vecPayees);

    if(vecPayees.empty()) {
        LogPrint("gwpayments", "CGatewayBlockPayees::%s -- ERROR: couldn't find any payee\n", __func__);
        return false;
    }

    int nVotes = -1;
    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CGatewayBlockPayees::HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq) const
{
    LOCK(cs_vecPayees);

    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

    LogPrint("gwpayments", "CGatewayBlockPayees::%s -- ERROR: couldn't find any payee with %d+ votes\n", __func__, nVotesReq);
    return false;
}

bool CGatewayBlockPayees::IsTransactionValid(const CTransaction& txNew) const
{
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nGatewayPayment = GetGatewayPayment(nBlockHeight, txNew.GetValueOut());

    //require at least GWPAYMENTS_SIGNATURES_REQUIRED signatures

    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least GWPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if(nMaxSignatures < GWPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (const auto& payee : vecPayees) {
        if (payee.GetVoteCount() >= GWPAYMENTS_SIGNATURES_REQUIRED) {
            for (const auto& txout : txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nGatewayPayment == txout.nValue) {
                    LogPrint("gwpayments", "CGatewayBlockPayees::%s -- Found required payment\n", __func__);
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if(strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrintf("CGatewayBlockPayees::%s -- ERROR: Missing required payment, possible payees: '%s', amount: %f SOOM\n", __func__, strPayeesPossible, (float)nGatewayPayment/COIN);
    return false;
}

std::string CGatewayBlockPayees::GetRequiredPaymentsString() const
{
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "";

    for (const auto& payee : vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (!strRequiredPayments.empty())
            strRequiredPayments += ", ";

        strRequiredPayments += strprintf("%s:%d", address2.ToString(), payee.GetVoteCount());
    }

    if (strRequiredPayments.empty())
        return "Unknown";

    return strRequiredPayments;
}

std::string CGatewayPayments::GetRequiredPaymentsString(int nBlockHeight) const
{
    LOCK(cs_mapGatewayBlocks);
    const auto it = mapGatewayBlocks.find(nBlockHeight);
    return it == mapGatewayBlocks.end() ? "Unknown" : it->second.GetRequiredPaymentsString();
}

bool CGatewayPayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward) const
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive(nBlockHeight)) {
        std::vector<CTxOut> voutGatewayPayments;
        if (!GetBlockTxOuts(nBlockHeight, blockReward, voutGatewayPayments)) {
            LogPrintf("CGatewayPayments::%s -- ERROR failed to get payees for block at height %s\n", __func__, nBlockHeight);
            return true;
        }

        for (const auto& txout : voutGatewayPayments) {
            bool found = false;
            for (const auto& txout2 : txNew.vout) {
                if (txout == txout2) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                CTxDestination dest;
                if (!ExtractDestination(txout.scriptPubKey, dest))
                    assert(false);
                LogPrintf("CGatewayPayments::%s -- ERROR failed to find expected payee %s in block at height %s\n", __func__, CBitcoinAddress(dest).ToString(), nBlockHeight);
                return false;
            }
        }
        return true;
    } else {
        LOCK(cs_mapGatewayBlocks);
        const auto it = mapGatewayBlocks.find(nBlockHeight);
        return it == mapGatewayBlocks.end() ? true : it->second.IsTransactionValid(txNew);
    }
}

void CGatewayPayments::CheckAndRemove()
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        return;
    }

    if(!gatewaySync.IsBlockchainSynced()) return;

    LOCK2(cs_mapGatewayBlocks, cs_mapGatewayPaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CGatewayPaymentVote>::iterator it = mapGatewayPaymentVotes.begin();
    while(it != mapGatewayPaymentVotes.end()) {
        CGatewayPaymentVote vote = (*it).second;

        if(nCachedBlockHeight - vote.nBlockHeight > nLimit) {
            LogPrint("gwpayments", "CGatewayPayments::%s -- Removing old Gateway payment: nBlockHeight=%d\n", __func__, vote.nBlockHeight);
            mapGatewayPaymentVotes.erase(it++);
            mapGatewayBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CGatewayPayments::%s -- %s\n", __func__, ToString());
}

bool CGatewayPaymentVote::IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman) const
{
    gateway_info_t gwInfo;

    if(!gwnodeman.GetGatewayInfo(gatewayOutpoint, gwInfo)) {
        strError = strprintf("Unknown Gateway=%s", gatewayOutpoint.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Gateway
        if(gatewaySync.IsGatewayListSynced()) {
            gwnodeman.AskForGW(pnode, gatewayOutpoint, connman);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if(nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_GATEWAY_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = gwpayments.GetMinGatewayPaymentsProto();
    } else {
        // allow non-updated gateways for old blocks
        nMinRequiredProtocol = MIN_GATEWAY_PAYMENT_PROTO_VERSION_1;
    }

    if(gwInfo.nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Gateway protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", gwInfo.nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only gateways should try to check gateway rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify gateway rank for future block votes only.
    if(!fGatewayMode && nBlockHeight < nValidationHeight) return true;

    int nRank;

    if(!gwnodeman.GetGatewayRank(gatewayOutpoint, nRank, nBlockHeight - 101, nMinRequiredProtocol)) {
        LogPrint("gwpayments", "CGatewayPaymentVote::IsValid -- Can't calculate rank for gateway %s\n",
                    gatewayOutpoint.ToStringShort());
        return false;
    }

    if(nRank > GWPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have gateways mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Gateway %s is not in the top %d (%d)", gatewayOutpoint.ToStringShort(), GWPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new gww which is out of bounds, for old gww GW list itself might be way too much off
        if(nRank > GWPAYMENTS_SIGNATURES_TOTAL*2 && nBlockHeight > nValidationHeight) {
            LOCK(cs_main);
            strError = strprintf("Gateway %s is not in the top %d (%d)", gatewayOutpoint.ToStringShort(), GWPAYMENTS_SIGNATURES_TOTAL*2, nRank);
            LogPrintf("CGatewayPaymentVote::%s -- Error: %s\n", __func__, strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CGatewayPayments::ProcessBlock(int nBlockHeight, CConnman& connman)
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive(nBlockHeight)) {
        return true;
    }

    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if(fLiteMode || !fGatewayMode) return false;

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about gateways.
    if(!gatewaySync.IsGatewayListSynced()) return false;

    int nRank;

    if (!gwnodeman.GetGatewayRank(activeGatewayInfo.outpoint, nRank, nBlockHeight - 101, GetMinGatewayPaymentsProto())) {
        LogPrint("gwpayments", "CGatewayPayments::%s -- Unknown Gateway\n", __func__);
        return false;
    }

    if (nRank > GWPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("gwpayments", "CGatewayPayments::%s -- Gateway not in the top %d (%d)\n", __func__, GWPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }


    // LOCATE THE NEXT GATEWAY WHICH SHOULD BE PAID

    LogPrintf("CGatewayPayments::%s -- Start: nBlockHeight=%d, gateway=%s\n", __func__, nBlockHeight, activeGatewayInfo.outpoint.ToStringShort());

    // pay to the oldest GW that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    gateway_info_t gwInfo;

    if (!gwnodeman.GetNextGatewayInQueueForPayment(nBlockHeight, true, nCount, gwInfo)) {
        LogPrintf("CGatewayPayments::%s -- ERROR: Failed to find gateway to pay\n", __func__);
        return false;
    }

    LogPrintf("CGatewayPayments::%s -- Gateway found by GetNextGatewayInQueueForPayment(): %s\n", __func__, gwInfo.outpoint.ToStringShort());

    CScript payee = GetScriptForDestination(gwInfo.keyIDCollateralAddress);

    CGatewayPaymentVote voteNew(activeGatewayInfo.outpoint, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    LogPrintf("CGatewayPayments::%s -- vote: payee=%s, nBlockHeight=%d\n", __func__, address2.ToString(), nBlockHeight);

    // SIGN MESSAGE TO NETWORK WITH OUR GATEWAY KEYS

    LogPrintf("CGatewayPayments::%s -- Signing vote\n", __func__);
    if (voteNew.Sign()) {
        LogPrintf("CGatewayPayments::%s -- AddOrUpdatePaymentVote()\n", __func__);

        if (AddOrUpdatePaymentVote(voteNew)) {
            voteNew.Relay(connman);
            return true;
        }
    }

    return false;
}

void CGatewayPayments::CheckBlockVotes(int nBlockHeight)
{
    if (!gatewaySync.IsWinnersListSynced()) return;

    CGatewayMan::rank_pair_vec_t gws;
    if (!gwnodeman.GetGatewayRanks(gws, nBlockHeight - 101, GetMinGatewayPaymentsProto())) {
        LogPrintf("CGatewayPayments::%s -- nBlockHeight=%d, GetGatewayRanks failed\n", __func__, nBlockHeight);
        return;
    }

    std::string debugStr;

    debugStr += strprintf("CGatewayPayments::%s -- nBlockHeight=%d,\n  Expected voting GWs:\n", __func__, nBlockHeight);

    LOCK2(cs_mapGatewayBlocks, cs_mapGatewayPaymentVotes);

    int i{0};
    for (const auto& gw : gws) {
        CScript payee;
        bool found = false;

        const auto it = mapGatewayBlocks.find(nBlockHeight);
        if (it != mapGatewayBlocks.end()) {
            for (const auto& p : it->second.vecPayees) {
                for (const auto& voteHash : p.GetVoteHashes()) {
                    const auto itVote = mapGatewayPaymentVotes.find(voteHash);
                    if (itVote == mapGatewayPaymentVotes.end()) {
                        debugStr += strprintf("    - could not find vote %s\n",
                                              voteHash.ToString());
                        continue;
                    }
                    if (itVote->second.gatewayOutpoint == gw.second.outpoint) {
                        payee = itVote->second.payee;
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found) {
            CTxDestination address1;
            ExtractDestination(payee, address1);
            CBitcoinAddress address2(address1);

            debugStr += strprintf("    - %s - voted for %s\n",
                                  gw.second.outpoint.ToStringShort(), address2.ToString());
        } else {
            mapGatewaysDidNotVote.emplace(gw.second.outpoint, 0).first->second++;

            debugStr += strprintf("    - %s - no vote received\n",
                                  gw.second.outpoint.ToStringShort());
        }

        if (++i >= GWPAYMENTS_SIGNATURES_TOTAL) break;
    }

    if (mapGatewaysDidNotVote.empty()) {
        LogPrint("gwpayments", "%s", debugStr);
        return;
    }

    debugStr += "  Gateways which missed a vote in the past:\n";
    for (const auto& item : mapGatewaysDidNotVote) {
        debugStr += strprintf("    - %s: %d\n", item.first.ToStringShort(), item.second);
    }

    LogPrint("gwpayments", "%s", debugStr);
}

void CGatewayPaymentVote::Relay(CConnman& connman) const
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        return;
    }

    // Do not relay until fully synced
    if(!gatewaySync.IsSynced()) {
        LogPrint("gwpayments", "CGatewayPayments::%s -- won't relay until fully synced\n", __func__);
        return;
    }

    CInv inv(MSG_GATEWAY_PAYMENT_VOTE, GetHash());
    connman.RelayInv(inv);
}

bool CGatewayPaymentVote::CheckSignature(const CKeyID& keyIDOperator, int nValidationHeight, int &nDos) const
{
    // do not ban by default
    nDos = 0;
    std::string strError = "";

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, keyIDOperator, vchSig, strError)) {
            // could be a signature in old format
            std::string strMessage = gatewayOutpoint.ToStringShort() +
                        std::to_string(nBlockHeight) +
                        ScriptToAsmStr(payee);
            if(!CMessageSigner::VerifyMessage(keyIDOperator, vchSig, strMessage, strError)) {
                // nope, not in old format either
                // Only ban for future block vote when we are already synced.
                // Otherwise it could be the case when GW which signed this vote is using another key now
                // and we have no idea about the old one.
                if(gatewaySync.IsGatewayListSynced() && nBlockHeight > nValidationHeight) {
                    nDos = 20;
                }
                return error("CGatewayPaymentVote::CheckSignature -- Got bad Gateway payment signature, gateway=%s, error: %s",
                            gatewayOutpoint.ToStringShort(), strError);
            }
        }
    } else {
        std::string strMessage = gatewayOutpoint.ToStringShort() +
                    std::to_string(nBlockHeight) +
                    ScriptToAsmStr(payee);

        if (!CMessageSigner::VerifyMessage(keyIDOperator, vchSig, strMessage, strError)) {
            // Only ban for future block vote when we are already synced.
            // Otherwise it could be the case when GW which signed this vote is using another key now
            // and we have no idea about the old one.
            if(gatewaySync.IsGatewayListSynced() && nBlockHeight > nValidationHeight) {
                nDos = 20;
            }
            return error("CGatewayPaymentVote::CheckSignature -- Got bad Gateway payment signature, gateway=%s, error: %s",
                        gatewayOutpoint.ToStringShort(), strError);
        }
    }

    return true;
}

std::string CGatewayPaymentVote::ToString() const
{
    std::ostringstream info;

    info << gatewayOutpoint.ToStringShort() <<
            ", " << nBlockHeight <<
            ", " << ScriptToAsmStr(payee) <<
            ", " << (int)vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CGatewayPayments::Sync(CNode* pnode, CConnman& connman) const
{
    LOCK(cs_mapGatewayBlocks);

    if(!gatewaySync.IsWinnersListSynced()) return;

    int nInvCount = 0;

    for(int h = nCachedBlockHeight; h < nCachedBlockHeight + 20; h++) {
        const auto it = mapGatewayBlocks.find(h);
        if(it != mapGatewayBlocks.end()) {
            for (const auto& payee : it->second.vecPayees) {
                std::vector<uint256> vecVoteHashes = payee.GetVoteHashes();
                for (const auto& hash : vecVoteHashes) {
                    if(!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_GATEWAY_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CGatewayPayments::%s -- Sent %d votes to peer %d\n", __func__, nInvCount, pnode->id);
    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    connman.PushMessage(pnode, msgMaker.Make(NetMsgType::SYNCSTATUSCOUNT, GATEWAY_SYNC_GWW, nInvCount));
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CGatewayPayments::RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman) const
{
    if(!gatewaySync.IsGatewayListSynced()) return;

    CNetMsgMaker msgMaker(pnode->GetSendVersion());
    LOCK2(cs_main, cs_mapGatewayBlocks);

    std::vector<CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = chainActive.Tip();

    while(nCachedBlockHeight - pindex->nHeight < nLimit) {
        const auto it = mapGatewayBlocks.find(pindex->nHeight);
        if(it == mapGatewayBlocks.end()) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_GATEWAY_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if(vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CGatewayPayments::%s -- asking peer=%d for %d blocks\n", __func__, pnode->id, MAX_INV_SZ);
                connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if(!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    for (auto& gwBlockPayees : mapGatewayBlocks) {
        int nBlockHeight = gwBlockPayees.first;
        int nTotalVotes = 0;
        bool fFound = false;
        for (const auto& payee : gwBlockPayees.second.vecPayees) {
            if(payee.GetVoteCount() >= GWPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (GWPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if(fFound || nTotalVotes >= (GWPAYMENTS_SIGNATURES_TOTAL + GWPAYMENTS_SIGNATURES_REQUIRED)/2) {
            // so just move to the next block
            continue;
        }
        // DEBUG
        DBG (
            // Let's see why this failed
            for (const auto& payee : gwBlockPayees.second.vecPayees) {
                CTxDestination address1;
                ExtractDestination(payee.GetPayee(), address1);
                CBitcoinAddress address2(address1);
                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
            }
            printf("block %d votes total %d\n", nBlockHeight, nTotalVotes);
        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if(GetBlockHash(hash, nBlockHeight)) {
            vToFetch.push_back(CInv(MSG_GATEWAY_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if(vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CGatewayPayments::%s -- asking peer=%d for %d payment blocks\n", __func__, pnode->id, MAX_INV_SZ);
            connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
            // Start filling new batch
            vToFetch.clear();
        }
    }
    // Ask for the rest of it
    if(!vToFetch.empty()) {
        LogPrintf("CGatewayPayments::%s -- asking peer=%d for %d payment blocks\n", __func__, pnode->id, vToFetch.size());
        connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
    }
}

std::string CGatewayPayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapGatewayPaymentVotes.size() <<
            ", Blocks: " << (int)mapGatewayBlocks.size();

    return info.str();
}

bool CGatewayPayments::IsEnoughData() const
{
    float nAverageVotes = (GWPAYMENTS_SIGNATURES_TOTAL + GWPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CGatewayPayments::GetStorageLimit() const
{
    return std::max(int(gwnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CGatewayPayments::UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman)
{
    if(!pindex) return;

    if (deterministicGWManager->IsDeterministicGWsSporkActive(pindex->nHeight)) {
        return;
    }

    nCachedBlockHeight = pindex->nHeight;
    LogPrint("gwpayments", "CGatewayPayments::%s -- nCachedBlockHeight=%d\n", __func__, nCachedBlockHeight);

    int nFutureBlock = nCachedBlockHeight + 10;

    CheckBlockVotes(nFutureBlock - 1);
    ProcessBlock(nFutureBlock, connman);
}

void CGatewayPayments::DoMaintenance()
{
    if (ShutdownRequested()) return;

    CheckAndRemove();
}
