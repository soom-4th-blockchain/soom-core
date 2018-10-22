// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "netbase.h"
#include "gateway.h"
#include "gateway-payments.h"
#include "gateway-sync.h"
#include "gatewayman.h"
#include "messagesigner.h"
#include "script/standard.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif // ENABLE_WALLET

#include <boost/lexical_cast.hpp>


CGateway::CGateway() :
    gateway_info_t{ GATEWAY_ENABLED, PROTOCOL_VERSION, GetAdjustedTime()}
{}

CGateway::CGateway(CService addr, COutPoint outpoint, CPubKey pubKeyCollateralAddress, CPubKey pubKeyGateway, int nProtocolVersionIn) :
    gateway_info_t{ GATEWAY_ENABLED, nProtocolVersionIn, GetAdjustedTime(),
                       outpoint, addr, pubKeyCollateralAddress, pubKeyGateway}
{}

CGateway::CGateway(const CGateway& other) :
    gateway_info_t{other},
    lastPing(other.lastPing),
    vchSig(other.vchSig),
    nCollateralMinConfBlockHash(other.nCollateralMinConfBlockHash),
    nBlockLastPaid(other.nBlockLastPaid),
    nPoSeBanScore(other.nPoSeBanScore),
    nPoSeBanHeight(other.nPoSeBanHeight),
    fUnitTest(other.fUnitTest)
{}

CGateway::CGateway(const CGatewayBroadcast& gwb) :
    gateway_info_t{ gwb.nActiveState, gwb.nProtocolVersion, gwb.sigTime,
                       gwb.outpoint, gwb.addr, gwb.pubKeyCollateralAddress, gwb.pubKeyGateway},
    lastPing(gwb.lastPing),
    vchSig(gwb.vchSig)
{}

//
// When a new gateway broadcast is sent, update our information
//
bool CGateway::UpdateFromNewBroadcast(CGatewayBroadcast& gwb, CConnman& connman)
{
    if(gwb.sigTime <= sigTime && !gwb.fRecovery) return false;

    pubKeyGateway = gwb.pubKeyGateway;
    sigTime = gwb.sigTime;
    vchSig = gwb.vchSig;
    nProtocolVersion = gwb.nProtocolVersion;
    addr = gwb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if(!gwb.lastPing || (gwb.lastPing && gwb.lastPing.CheckAndUpdate(this, true, nDos, connman))) {
        lastPing = gwb.lastPing;
        gwnodeman.mapSeenGatewayPing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Gateway privkey...
    if(fGatewayMode && pubKeyGateway == activeGateway.pubKeyGateway) {
        nPoSeBanScore = -GATEWAY_POSE_BAN_MAX_SCORE;
        if(nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeGateway.ManageState(connman);
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CGateway::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your GW: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Gateway depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CGateway::CalculateScore(const uint256& blockHash) const
{
    // Deterministically calculate a "score" for a Gateway based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << outpoint << nCollateralMinConfBlockHash << blockHash;
    return UintToArith256(ss.GetHash());
}

CGateway::CollateralStatus CGateway::CheckCollateral(const COutPoint& outpoint, const CPubKey& pubkey)
{
    int nHeight;
    return CheckCollateral(outpoint, pubkey, nHeight);
}

CGateway::CollateralStatus CGateway::CheckCollateral(const COutPoint& outpoint, const CPubKey& pubkey, int& nHeightRet)
{
    AssertLockHeld(cs_main);

    Coin coin;
    if(!GetUTXOCoin(outpoint, coin)) {
        return COLLATERAL_UTXO_NOT_FOUND;
    }

    if(coin.out.nValue != 5000 * COIN) {
        return COLLATERAL_INVALID_AMOUNT;
    }

    if(pubkey == CPubKey() || coin.out.scriptPubKey != GetScriptForDestination(pubkey.GetID())) {
        return COLLATERAL_INVALID_PUBKEY;
    }

    nHeightRet = coin.nHeight;
    return COLLATERAL_OK;
}

void CGateway::Check(bool fForce)
{
    AssertLockHeld(cs_main);
    LOCK(cs);

    if(ShutdownRequested()) return;

    if(!fForce && (GetTime() - nTimeLastChecked < GATEWAY_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state\n", outpoint.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if(IsOutpointSpent()) return;

    int nHeight = 0;
    if(!fUnitTest) {
        Coin coin;
        if(!GetUTXOCoin(outpoint, coin)) {
            nActiveState = GATEWAY_OUTPOINT_SPENT;
            LogPrint("gateway", "CGateway::Check -- Failed to find Gateway UTXO, gateway=%s\n", outpoint.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if(IsPoSeBanned()) {
        if(nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Gateway still will be on the edge and can be banned back easily if it keeps ignoring gwverify
        // or connect attempts. Will require few gwverify messages to strengthen its position in gw list.
        LogPrintf("CGateway::Check -- Gateway %s is unbanned and back in list now\n", outpoint.ToStringShort());
        DecreasePoSeBanScore();
    } else if(nPoSeBanScore >= GATEWAY_POSE_BAN_MAX_SCORE) {
        nActiveState = GATEWAY_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + gwnodeman.size();
        LogPrintf("CGateway::Check -- Gateway %s is banned till block %d now\n", outpoint.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurGateway = fGatewayMode && activeGateway.pubKeyGateway == pubKeyGateway;

                   // gateway doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < gwpayments.GetMinGatewayPaymentsProto() ||
                   // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                   (fOurGateway && nProtocolVersion < PROTOCOL_VERSION);

    if(fRequireUpdate) {
        nActiveState = GATEWAY_UPDATE_REQUIRED;
        if(nActiveStatePrev != nActiveState) {
            LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old gateways on start, give them a chance to receive updates...
    bool fWaitForPing = !gatewaySync.IsGatewayListSynced() && !IsPingedWithin(GATEWAY_MIN_GWP_SECONDS);

    if(fWaitForPing && !fOurGateway) {
        // ...but if it was already expired before the initial check - return right away
        if(IsExpired() || IsSentinelPingExpired() || IsNewStartRequired()) {
            LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state, waiting for ping\n", outpoint.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own gateway
    if(!fWaitForPing || fOurGateway) {

        if(!IsPingedWithin(GATEWAY_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = GATEWAY_NEW_START_REQUIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }

        if(!IsPingedWithin(GATEWAY_EXPIRATION_SECONDS)) {
            nActiveState = GATEWAY_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }

        // part 1: expire based on soomd ping
        bool fSentinelPingActive = gatewaySync.IsSynced() && gwnodeman.IsSentinelPingActive();
        bool fSentinelPingExpired = fSentinelPingActive && !IsPingedWithin(GATEWAY_SENTINEL_PING_MAX_SECONDS);
        LogPrint("gateway", "CGateway::Check -- outpoint=%s, GetAdjustedTime()=%d, fSentinelPingExpired=%d\n",
                outpoint.ToStringShort(), GetAdjustedTime(), fSentinelPingExpired);

        if(fSentinelPingExpired) {
            nActiveState = GATEWAY_SENTINEL_PING_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    // We require GWs to be in PRE_ENABLED until they either start to expire or receive a ping and go into ENABLED state
    // Works on mainnet/testnet only and not the case on regtest.
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        if (lastPing.sigTime - sigTime < GATEWAY_MIN_GWP_SECONDS) {
            nActiveState = GATEWAY_PRE_ENABLED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if(!fWaitForPing || fOurGateway) {
        // part 2: expire based on sentinel info
        bool fSentinelPingActive = gatewaySync.IsSynced() && gwnodeman.IsSentinelPingActive();
        bool fSentinelPingExpired = fSentinelPingActive && !lastPing.fSentinelIsCurrent;

        LogPrint("gateway", "CGateway::Check -- outpoint=%s, GetAdjustedTime()=%d, fSentinelPingExpired=%d\n",
                outpoint.ToStringShort(), GetAdjustedTime(), fSentinelPingExpired);

        if(fSentinelPingExpired) {
            nActiveState = GATEWAY_SENTINEL_PING_EXPIRED;
            if(nActiveStatePrev != nActiveState) {
                LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    nActiveState = GATEWAY_ENABLED; // OK
    if(nActiveStatePrev != nActiveState) {
        LogPrint("gateway", "CGateway::Check -- Gateway %s is in %s state now\n", outpoint.ToStringShort(), GetStateString());
    }
}

bool CGateway::IsValidNetAddr()
{
    return IsValidNetAddr(addr);
}

bool CGateway::IsValidNetAddr(CService addrIn)
{
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
            (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

gateway_info_t CGateway::GetInfo() const
{
    gateway_info_t info{*this};
    info.nTimeLastPing = lastPing.sigTime;
    info.fInfoValid = true;
    return info;
}

std::string CGateway::StateToString(int nStateIn)
{
    switch(nStateIn) {
        case GATEWAY_PRE_ENABLED:            return "PRE_ENABLED";
        case GATEWAY_ENABLED:                return "ENABLED";
        case GATEWAY_EXPIRED:                return "EXPIRED";
        case GATEWAY_OUTPOINT_SPENT:         return "OUTPOINT_SPENT";
        case GATEWAY_UPDATE_REQUIRED:        return "UPDATE_REQUIRED";
        case GATEWAY_SENTINEL_PING_EXPIRED:  return "SENTINEL_PING_EXPIRED";
        case GATEWAY_NEW_START_REQUIRED:     return "NEW_START_REQUIRED";
        case GATEWAY_POSE_BAN:               return "POSE_BAN";
        default:                             return "UNKNOWN";
    }
}

std::string CGateway::GetStateString() const
{
    return StateToString(nActiveState);
}

std::string CGateway::GetStatus() const
{
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

void CGateway::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack)
{
    if(!pindex) return;

    const CBlockIndex *BlockReading = pindex;

    CScript gwpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    // LogPrint("gwpayments", "CGateway::UpdateLastPaidBlock -- searching for block with payment to %s\n", outpoint.ToStringShort());

    LOCK(cs_mapGatewayBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
        if(gwpayments.mapGatewayBlocks.count(BlockReading->nHeight) &&
            gwpayments.mapGatewayBlocks[BlockReading->nHeight].HasPayeeWithVotes(gwpayee, 2))
        {
            CBlock block;
            if(!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) 
                continue; // shouldn't really happen

            CAmount nGatewayPayment = GetGatewayPayment(BlockReading->nHeight, block.vtx[0]->GetValueOut());

            for (const auto& txout : block.vtx[0]->vout)
                if(gwpayee == txout.scriptPubKey && nGatewayPayment == txout.nValue) {
                    nBlockLastPaid = BlockReading->nHeight;
                    nTimeLastPaid = BlockReading->nTime;
                    LogPrint("gwpayments", "CGateway::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", outpoint.ToStringShort(), nBlockLastPaid);
                    return;
                }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this gateway wasn't found in latest gwpayments blocks
    // or it was found in gwpayments blocks but wasn't found in the blockchain.
    // LogPrint("gwpayments", "CGateway::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", outpoint.ToStringShort(), nBlockLastPaid);
}

#ifdef ENABLE_WALLET
bool CGatewayBroadcast::Create(const std::string& strService, const std::string& strKeyGateway, const std::string& strTxHash, const std::string& strOutputIndex, std::string& strErrorRet, CGatewayBroadcast &gwbRet, bool fOffline)
{
    COutPoint outpoint;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyGatewayNew;
    CKey keyGatewayNew;

    auto Log = [&strErrorRet](std::string sErr)->bool
    {
        strErrorRet = sErr;
        LogPrintf("CGatewayBroadcast::Create -- %s\n", strErrorRet);
        return false;
    };

    // Wait for sync to finish because gwb simply won't be relayed otherwise
    if (!fOffline && !gatewaySync.IsSynced())
        return Log("Sync in progress. Must wait until sync is complete to start Gateway");

    if (!CMessageSigner::GetKeysFromSecret(strKeyGateway, keyGatewayNew, pubKeyGatewayNew))
        return Log(strprintf("Invalid gateway key %s", strKeyGateway));

    if (!pwalletMain->GetGatewayOutpointAndKeys(outpoint, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex))
        return Log(strprintf("Could not allocate outpoint %s:%s for gateway %s", strTxHash, strOutputIndex, strService));

    CService service;
    if (!Lookup(strService.c_str(), service, 0, false))
        return Log(strprintf("Invalid address %s for gateway.", strService));
	if(!fLocalGateWay)
    {		
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort)
            return Log(strprintf("Invalid port %u for gateway %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort));
    } else if (service.GetPort() == mainnetDefaultPort)
        return Log(strprintf("Invalid port %u for gateway %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort));
	}
    return Create(outpoint, service, keyCollateralAddressNew, pubKeyCollateralAddressNew, keyGatewayNew, pubKeyGatewayNew, strErrorRet, gwbRet);
}

bool CGatewayBroadcast::Create(const COutPoint& outpoint, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyGatewayNew, const CPubKey& pubKeyGatewayNew, std::string &strErrorRet, CGatewayBroadcast &gwbRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("gateway", "CGatewayBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyGatewayNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyGatewayNew.GetID().ToString());

    auto Log = [&strErrorRet,&gwbRet](std::string sErr)->bool
    {
        strErrorRet = sErr;
        LogPrintf("CGatewayBroadcast::Create -- %s\n", strErrorRet);
        gwbRet = CGatewayBroadcast();
        return false;
    };

    CGatewayPing gwp(outpoint);
    if (!gwp.Sign(keyGatewayNew, pubKeyGatewayNew))
        return Log(strprintf("Failed to sign ping, gateway=%s", outpoint.ToStringShort()));

    gwbRet = CGatewayBroadcast(service, outpoint, pubKeyCollateralAddressNew, pubKeyGatewayNew, PROTOCOL_VERSION);

    if (!gwbRet.IsValidNetAddr())
        return Log(strprintf("Invalid IP address, gateway=%s", outpoint.ToStringShort()));

    gwbRet.lastPing = gwp;
    if (!gwbRet.Sign(keyCollateralAddressNew))
        return Log(strprintf("Failed to sign broadcast, gateway=%s", outpoint.ToStringShort()));

    return true;
}
#endif // ENABLE_WALLET

bool CGatewayBroadcast::SimpleCheck(int& nDos)
{
    nDos = 0;

    AssertLockHeld(cs_main);

    // make sure addr is valid
    if(!IsValidNetAddr()) {
        LogPrintf("CGatewayBroadcast::SimpleCheck -- Invalid addr, rejected: gateway=%s  addr=%s\n",
                    outpoint.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGatewayBroadcast::SimpleCheck -- Signature rejected, too far into the future: gateway=%s\n", outpoint.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if(!lastPing || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = GATEWAY_EXPIRED;
    }

    if(nProtocolVersion < gwpayments.GetMinGatewayPaymentsProto()) {
        LogPrintf("CGatewayBroadcast::SimpleCheck -- outdated Gateway: gateway=%s  nProtocolVersion=%d\n", outpoint.ToStringShort(), nProtocolVersion);
        nActiveState = GATEWAY_UPDATE_REQUIRED;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if(pubkeyScript.size() != 25) {
        LogPrintf("CGatewayBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyGateway.GetID());

    if(pubkeyScript2.size() != 25) {
        LogPrintf("CGatewayBroadcast::SimpleCheck -- pubKeyGateway has the wrong size\n");
        nDos = 100;
        return false;
    }

	if(!fLocalGateWay)
    {
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(addr.GetPort() != mainnetDefaultPort) return false;
    } else if(addr.GetPort() == mainnetDefaultPort) return false;
	}
    return true;
}

bool CGatewayBroadcast::Update(CGateway* pgw, int& nDos, CConnman& connman)
{
    nDos = 0;

    AssertLockHeld(cs_main);

    if(pgw->sigTime == sigTime && !fRecovery) {
        // mapSeenGatewayBroadcast in CGatewayMan::CheckGwbAndUpdateGatewayList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if(pgw->sigTime > sigTime) {
        LogPrintf("CGatewayBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Gateway %s %s\n",
                      sigTime, pgw->sigTime, outpoint.ToStringShort(), addr.ToString());
        return false;
    }

    pgw->Check();

    // gateway is banned by PoSe
    if(pgw->IsPoSeBanned()) {
        LogPrintf("CGatewayBroadcast::Update -- Banned by PoSe, gateway=%s\n", outpoint.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if(pgw->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CGatewayBroadcast::Update -- Got mismatched pubKeyCollateralAddress and outpoint\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CGatewayBroadcast::Update -- CheckSignature() failed, gateway=%s\n", outpoint.ToStringShort());
        return false;
    }

    // if ther was no gateway broadcast recently or if it matches our Gateway privkey...
    if(!pgw->IsBroadcastedWithin(GATEWAY_MIN_GWB_SECONDS) || (fGatewayMode && pubKeyGateway == activeGateway.pubKeyGateway)) {
        // take the newest entry
        LogPrintf("CGatewayBroadcast::Update -- Got UPDATED Gateway entry: addr=%s\n", addr.ToString());
        if(pgw->UpdateFromNewBroadcast(*this, connman)) {
            pgw->Check();
            Relay(connman);
        }
        gatewaySync.BumpAssetLastTime("CGatewayBroadcast::Update");
    }

    return true;
}

bool CGatewayBroadcast::CheckOutpoint(int& nDos)
{
    // we are a gateway with the same outpoint (i.e. already activated) and this gwb is ours (matches our Gateway privkey)
    // so nothing to do here for us
    if(fGatewayMode && outpoint == activeGateway.outpoint && pubKeyGateway == activeGateway.pubKeyGateway) {
        return false;
    }

    AssertLockHeld(cs_main);

    int nHeight;
    CollateralStatus err = CheckCollateral(outpoint, pubKeyCollateralAddress, nHeight);
    if (err == COLLATERAL_UTXO_NOT_FOUND) {
        LogPrint("gateway", "CGatewayBroadcast::CheckOutpoint -- Failed to find Gateway UTXO, gateway=%s\n", outpoint.ToStringShort());
        return false;
    }

    if (err == COLLATERAL_INVALID_AMOUNT) {
        LogPrint("gateway", "CGatewayBroadcast::CheckOutpoint -- Gateway UTXO should have 5000 SOOM, gateway=%s\n", outpoint.ToStringShort());
        nDos = 33;
        return false;
    }

    if(err == COLLATERAL_INVALID_PUBKEY) {
        LogPrint("gateway", "CGatewayBroadcast::CheckOutpoint -- Gateway UTXO should match pubKeyCollateralAddress, gateway=%s\n", outpoint.ToStringShort());
        nDos = 33;
        return false;
    }

    if(chainActive.Height() - nHeight + 1 < Params().GetConsensus().nGatewayMinimumConfirmations) {
        LogPrintf("CGatewayBroadcast::CheckOutpoint -- Gateway UTXO must have at least %d confirmations, gateway=%s\n",
                Params().GetConsensus().nGatewayMinimumConfirmations, outpoint.ToStringShort());
        // UTXO is legit but has not enough confirmations.
        // Maybe we miss few blocks, let this gwb be checked again later.
        gwnodeman.mapSeenGatewayBroadcast.erase(GetHash());
        return false;
    }

    LogPrint("gateway", "CGatewayBroadcast::CheckOutpoint -- Gateway UTXO verified\n");

    // Verify that sig time is legit, should be at least not earlier than the timestamp of the block
    // at which collateral became nGatewayMinimumConfirmations blocks deep.
    // NOTE: this is not accurate because block timestamp is NOT guaranteed to be 100% correct one.
    CBlockIndex* pRequiredConfIndex = chainActive[nHeight + Params().GetConsensus().nGatewayMinimumConfirmations - 1]; // block where tx got nGatewayMinimumConfirmations
    if(pRequiredConfIndex->GetBlockTime() > sigTime) {
        LogPrintf("CGatewayBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Gateway %s %s\n",
                  sigTime, Params().GetConsensus().nGatewayMinimumConfirmations, pRequiredConfIndex->GetBlockTime(), outpoint.ToStringShort(), addr.ToString());
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CGatewayBroadcast::CheckOutpoint -- CheckSignature() failed, gateway=%s\n", outpoint.ToStringShort());
        return false;
    }

    // remember the block hash when collateral for this gateway had minimum required confirmations
    nCollateralMinConfBlockHash = pRequiredConfIndex->GetBlockHash();

    return true;
}

uint256 CGatewayBroadcast::GetHash() const
{
    // Note: doesn't match serialization

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << outpoint << uint8_t{} << 0xffffffff; // adding dummy values here to match old hashing format
    ss << pubKeyCollateralAddress;
    ss << sigTime;
    return ss.GetHash();
}

uint256 CGatewayBroadcast::GetSignatureHash() const
{
    // TODO: replace with "return SerializeHash(*this);" after migration to 70209
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << outpoint;
    ss << addr;
    ss << pubKeyCollateralAddress;
    ss << pubKeyGateway;
    ss << sigTime;
    ss << nProtocolVersion;
    return ss.GetHash();
}

bool CGatewayBroadcast::Sign(const CKey& keyCollateralAddress)
{
    std::string strError;

    sigTime = GetAdjustedTime();

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::SignHash(hash, keyCollateralAddress, vchSig)) {
            LogPrintf("CGatewayBroadcast::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, pubKeyCollateralAddress, vchSig, strError)) {
            LogPrintf("CGatewayBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubKeyCollateralAddress.GetID().ToString() + pubKeyGateway.GetID().ToString() +
                        boost::lexical_cast<std::string>(nProtocolVersion);

        if (!CMessageSigner::SignMessage(strMessage, vchSig, keyCollateralAddress)) {
            LogPrintf("CGatewayBroadcast::Sign -- SignMessage() failed\n");
            return false;
        }

        if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
            LogPrintf("CGatewayBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CGatewayBroadcast::CheckSignature(int& nDos) const
{
    std::string strError = "";
    nDos = 0;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, pubKeyCollateralAddress, vchSig, strError)) {
            // maybe it's in old format
            std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                            pubKeyCollateralAddress.GetID().ToString() + pubKeyGateway.GetID().ToString() +
                            boost::lexical_cast<std::string>(nProtocolVersion);

            if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)){
                // nope, not in old format either
                LogPrintf("CGatewayBroadcast::CheckSignature -- Got bad Gateway announce signature, error: %s\n", strError);
                nDos = 100;
                return false;
            }
        }
    } else {
        std::string strMessage = addr.ToString(false) + boost::lexical_cast<std::string>(sigTime) +
                        pubKeyCollateralAddress.GetID().ToString() + pubKeyGateway.GetID().ToString() +
                        boost::lexical_cast<std::string>(nProtocolVersion);

        if (!CMessageSigner::VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)){
            LogPrintf("CGatewayBroadcast::CheckSignature -- Got bad Gateway announce signature, error: %s\n", strError);
            nDos = 100;
            return false;
        }
    }

    return true;
}

void CGatewayBroadcast::Relay(CConnman& connman) const
{
    // Do not relay until fully synced
    if(!gatewaySync.IsSynced()) {
        LogPrint("gateway", "CGatewayBroadcast::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GATEWAY_ANNOUNCE, GetHash());
    connman.RelayInv(inv);
}

uint256 CGatewayPing::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        // TODO: replace with "return SerializeHash(*this);" after migration to 70209
        ss << gatewayOutpoint;
        ss << blockHash;
        ss << sigTime;
        ss << fSentinelIsCurrent;
        ss << nSentinelVersion;
        ss << nDaemonVersion;
    } else {
        // Note: doesn't match serialization

        ss << gatewayOutpoint << uint8_t{} << 0xffffffff; // adding dummy values here to match old hashing format
        ss << sigTime;
    }
    return ss.GetHash();
}

uint256 CGatewayPing::GetSignatureHash() const
{
    return GetHash();
}

CGatewayPing::CGatewayPing(const COutPoint& outpoint)
{
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    gatewayOutpoint = outpoint;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    nDaemonVersion = CLIENT_VERSION;
}

bool CGatewayPing::Sign(const CKey& keyGateway, const CPubKey& pubKeyGateway)
{
    std::string strError;

    sigTime = GetAdjustedTime();

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::SignHash(hash, keyGateway, vchSig)) {
            LogPrintf("CGatewayPing::Sign -- SignHash() failed\n");
            return false;
        }

        if (!CHashSigner::VerifyHash(hash, pubKeyGateway, vchSig, strError)) {
            LogPrintf("CGatewayPing::Sign -- VerifyHash() failed, error: %s\n", strError);
            return false;
        }
    } else {
        std::string strMessage = CTxIn(gatewayOutpoint).ToString() + blockHash.ToString() +
                    boost::lexical_cast<std::string>(sigTime);

        if (!CMessageSigner::SignMessage(strMessage, vchSig, keyGateway)) {
            LogPrintf("CGatewayPing::Sign -- SignMessage() failed\n");
            return false;
        }

        if (!CMessageSigner::VerifyMessage(pubKeyGateway, vchSig, strMessage, strError)) {
            LogPrintf("CGatewayPing::Sign -- VerifyMessage() failed, error: %s\n", strError);
            return false;
        }
    }

    return true;
}

bool CGatewayPing::CheckSignature(const CPubKey& pubKeyGateway, int &nDos) const 
{   
    std::string strError = "";
    nDos = 0;

    if (sporkManager.IsSporkActive(SPORK_6_NEW_SIGS)) {
        uint256 hash = GetSignatureHash();

        if (!CHashSigner::VerifyHash(hash, pubKeyGateway, vchSig, strError)) {
            std::string strMessage = CTxIn(gatewayOutpoint).ToString() + blockHash.ToString() +
                        boost::lexical_cast<std::string>(sigTime);

            if (!CMessageSigner::VerifyMessage(pubKeyGateway, vchSig, strMessage, strError)) {
                LogPrintf("CGatewayPing::CheckSignature -- Got bad Gateway ping signature, gateway=%s, error: %s\n", gatewayOutpoint.ToStringShort(), strError);
                nDos = 33;
                return false;
            }
        }
    } else {
        std::string strMessage = CTxIn(gatewayOutpoint).ToString() + blockHash.ToString() +
                    boost::lexical_cast<std::string>(sigTime);

        if (!CMessageSigner::VerifyMessage(pubKeyGateway, vchSig, strMessage, strError)) {
            LogPrintf("CGatewayPing::CheckSignature -- Got bad Gateway ping signature, gateway=%s, error: %s\n", gatewayOutpoint.ToStringShort(), strError);
            nDos = 33;
            return false;
        }
    }

    return true;
}

bool CGatewayPing::SimpleCheck(int& nDos)
{
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CGatewayPing::SimpleCheck -- Signature rejected, too far into the future, gateway=%s\n", gatewayOutpoint.ToStringShort());
        nDos = 1;
        return false;
    }

    {
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("gateway", "CGatewayPing::SimpleCheck -- Gateway ping is invalid, unknown block hash: gateway=%s blockHash=%s\n", gatewayOutpoint.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("gateway", "CGatewayPing::SimpleCheck -- Gateway ping verified: gateway=%s  blockHash=%s  sigTime=%d\n", gatewayOutpoint.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CGatewayPing::CheckAndUpdate(CGateway* pgw, bool fFromNewBroadcast, int& nDos, CConnman& connman)
{
    AssertLockHeld(cs_main);

    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pgw == NULL) {
        LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- Couldn't find Gateway entry, gateway=%s\n", gatewayOutpoint.ToStringShort());
        return false;
    }

    if(!fFromNewBroadcast) {
        if (pgw->IsUpdateRequired()) {
            LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- gateway protocol is outdated, gateway=%s\n", gatewayOutpoint.ToStringShort());
            return false;
        }

        if (pgw->IsNewStartRequired()) {
            LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- gateway is completely expired, new start is required, gateway=%s\n", gatewayOutpoint.ToStringShort());
            return false;
        }
    }

    {
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            LogPrintf("CGatewayPing::CheckAndUpdate -- Gateway ping is invalid, block hash is too old: gateway=%s  blockHash=%s\n", gatewayOutpoint.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- New ping: gateway=%s  blockHash=%s  sigTime=%d\n", gatewayOutpoint.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("gwping - Found corresponding gw for outpoint: %s\n", gatewayOutpoint.ToStringShort());
    // update only if there is no known ping for this gateway or
    // last ping was more then GATEWAY_MIN_GWP_SECONDS-60 ago comparing to this one
    if (pgw->IsPingedWithin(GATEWAY_MIN_GWP_SECONDS - 60, sigTime)) {
        LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- Gateway ping arrived too early, gateway=%s\n", gatewayOutpoint.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pgw->pubKeyGateway, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this gw for quite a while
    // (NOTE: assuming that GATEWAY_EXPIRATION_SECONDS/2 should be enough to finish gw list sync)
    if(!gatewaySync.IsGatewayListSynced() && !pgw->IsPingedWithin(GATEWAY_EXPIRATION_SECONDS/2)) {
        // let's bump sync timeout
        LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- bumping sync timeout, gateway=%s\n", gatewayOutpoint.ToStringShort());
        gatewaySync.BumpAssetLastTime("CGatewayPing::CheckAndUpdate");
    }

    // let's store this ping as the last one
    LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- Gateway ping accepted, gateway=%s\n", gatewayOutpoint.ToStringShort());
    pgw->lastPing = *this;

    // and update gwnodeman.mapSeenGatewayBroadcast.lastPing which is probably outdated
    CGatewayBroadcast gwb(*pgw);
    uint256 hash = gwb.GetHash();
    if (gwnodeman.mapSeenGatewayBroadcast.count(hash)) {
        gwnodeman.mapSeenGatewayBroadcast[hash].second.lastPing = *this;
    }

    // force update, ignoring cache
    pgw->Check(true);
    // relay ping for nodes in ENABLED/EXPIRED/WATCHDOG_EXPIRED state only, skip everyone else
    if (!pgw->IsEnabled() && !pgw->IsExpired() && !pgw->IsSentinelPingExpired()) return false;

    LogPrint("gateway", "CGatewayPing::CheckAndUpdate -- Gateway ping acceepted and relayed, gateway=%s\n", gatewayOutpoint.ToStringShort());
    Relay(connman);

    return true;
}

void CGatewayPing::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if(!gatewaySync.IsSynced()) {
        LogPrint("gateway", "CGatewayPing::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GATEWAY_PING, GetHash());
    connman.RelayInv(inv);
}

