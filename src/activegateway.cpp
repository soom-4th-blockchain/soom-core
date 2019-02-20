// Copyright (c) 2014-2018 The Dash Core developers
// Copyright (c) 2017-2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "evo/deterministicgws.h"
#include "init.h"
#include "gateway.h"
#include "gateway-sync.h"
#include "gatewayman.h"
#include "netbase.h"
#include "protocol.h"
#include "warnings.h"

// Keep track of the active Gateway
CActiveGatewayInfo activeGatewayInfo;
CActiveLegacyGatewayManager legacyActiveGatewayManager;
CActiveDeterministicGatewayManager* activeGatewayManager;

std::string CActiveDeterministicGatewayManager::GetStateString() const
{
    switch (state) {
    case GATEWAY_WAITING_FOR_PROTX:
        return "WAITING_FOR_PROTX";
    case GATEWAY_POSE_BANNED:
        return "POSE_BANNED";
    case GATEWAY_REMOVED:
        return "REMOVED";
    case GATEWAY_OPERATOR_KEY_CHANGED:
        return "OPERATOR_KEY_CHANGED";
    case GATEWAY_READY:
        return "READY";
    case GATEWAY_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string CActiveDeterministicGatewayManager::GetStatus() const
{
    switch (state) {
    case GATEWAY_WAITING_FOR_PROTX:
        return "Waiting for ProTx to appear on-chain";
    case GATEWAY_POSE_BANNED:
        return "Gateway was PoSe banned";
    case GATEWAY_REMOVED:
        return "Gateway removed from list";
    case GATEWAY_OPERATOR_KEY_CHANGED:
        return "Operator key changed or revoked";
    case GATEWAY_READY:
        return "Ready";
    case GATEWAY_ERROR:
        return "Error. " + strError;
    default:
        return "Unknown";
    }
}

void CActiveDeterministicGatewayManager::Init()
{
    LOCK(cs_main);

    if (!fGatewayMode) return;

    if (!deterministicGWManager->IsDeterministicGWsSporkActive()) return;

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        state = GATEWAY_ERROR;
        strError = "Gateway must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveDeterministicGatewayManager::Init -- ERROR: %s\n", strError);
        return;
    }

    if (!GetLocalAddress(activeGatewayInfo.service)) {
        state = GATEWAY_ERROR;
        return;
    }

    CDeterministicGWList gwList = deterministicGWManager->GetListAtChainTip();

    CDeterministicGWCPtr dgw = gwList.GetGWByOperatorKey(*activeGatewayInfo.blsPubKeyOperator);
    if (!dgw) {
        // GW not appeared on the chain yet
        return;
    }

    if (!gwList.IsGWValid(dgw->proTxHash)) {
        if (gwList.IsGWPoSeBanned(dgw->proTxHash)) {
            state = GATEWAY_POSE_BANNED;
        } else {
            state = GATEWAY_REMOVED;
        }
        return;
    }

    gwListEntry = dgw;

    LogPrintf("CActiveDeterministicGatewayManager::Init -- proTxHash=%s, proTx=%s\n", gwListEntry->proTxHash.ToString(), gwListEntry->ToString());

    if (activeGatewayInfo.service != gwListEntry->pdgwState->addr) {
        state = GATEWAY_ERROR;
        strError = "Local address does not match the address from ProTx";
        LogPrintf("CActiveDeterministicGatewayManager::Init -- ERROR: %s", strError);
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        // Check socket connectivity
        LogPrintf("CActiveDeterministicGatewayManager::Init -- Checking inbound connection to '%s'\n", activeGatewayInfo.service.ToString());
        SOCKET hSocket;
        bool fConnected = ConnectSocket(activeGatewayInfo.service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            state = GATEWAY_ERROR;
            strError = "Could not connect to " + activeGatewayInfo.service.ToString();
            LogPrintf("CActiveDeterministicGatewayManager::Init -- ERROR: %s\n", strError);
            return;
        }
    }

    activeGatewayInfo.proTxHash = gwListEntry->proTxHash;
    activeGatewayInfo.outpoint = gwListEntry->collateralOutpoint;
    state = GATEWAY_READY;
}

void CActiveDeterministicGatewayManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    LOCK(cs_main);

    if (!fGatewayMode) return;

    if (!deterministicGWManager->IsDeterministicGWsSporkActive(pindexNew->nHeight)) return;

    if (state == GATEWAY_READY) {
        auto gwList = deterministicGWManager->GetListForBlock(pindexNew->GetBlockHash());
        if (!gwList.IsGWValid(gwListEntry->proTxHash)) {
            // GW disappeared from GW list
            state = GATEWAY_REMOVED;
            activeGatewayInfo.proTxHash = uint256();
            activeGatewayInfo.outpoint.SetNull();
            // GW might have reappeared in same block with a new ProTx
            Init();
        } else if (gwList.GetGW(gwListEntry->proTxHash)->pdgwState->pubKeyOperator != gwListEntry->pdgwState->pubKeyOperator) {
            // GW operator key changed or revoked
            state = GATEWAY_OPERATOR_KEY_CHANGED;
            activeGatewayInfo.proTxHash = uint256();
            activeGatewayInfo.outpoint.SetNull();
            // GW might have reappeared in same block with a new ProTx
            Init();
        }
    } else {
        // GW might have (re)appeared with a new ProTx or we've found some peers and figured out our local address
        Init();
    }
}

bool CActiveDeterministicGatewayManager::GetLocalAddress(CService& addrRet)
{
    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(addrRet) && CGateway::IsValidNetAddr(addrRet);
    if (!fFoundLocal && Params().NetworkIDString() == CBaseChainParams::REGTEST) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }
    if (!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        g_connman->ForEachNodeContinueIf(CConnman::AllNodes, [&fFoundLocal, &empty](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(activeGatewayInfo.service, &pnode->addr) && CGateway::IsValidNetAddr(activeGatewayInfo.service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
            LogPrintf("CActiveDeterministicGatewayManager::GetLocalAddress -- ERROR: %s\n", strError);
            return false;
        }
    }
    return true;
}

/********* LEGACY *********/

void CActiveLegacyGatewayManager::ManageState(CConnman& connman)
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) return;

    LogPrint("gateway", "CActiveLegacyGatewayManager::ManageState -- Start\n");
    if (!fGatewayMode) {
        LogPrint("gateway", "CActiveLegacyGatewayManager::ManageState -- Not a gateway, returning\n");
        return;
    }
    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !gatewaySync.IsBlockchainSynced()) {
        nState = ACTIVE_GATEWAY_SYNC_IN_PROCESS;
        LogPrintf("CActiveLegacyGatewayManager::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_GATEWAY_SYNC_IN_PROCESS) {
        nState = ACTIVE_GATEWAY_INITIAL;
    }

    LogPrint("gateway", "CActiveLegacyGatewayManager::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == GATEWAY_UNKNOWN) {
        ManageStateInitial(connman);
    }

    if (eType == GATEWAY_REMOTE) {
        ManageStateRemote();
    }

    SendGatewayPing(connman);
}

std::string CActiveLegacyGatewayManager::GetStateString() const
{
    switch (nState) {
    case ACTIVE_GATEWAY_INITIAL:
        return "INITIAL";
    case ACTIVE_GATEWAY_SYNC_IN_PROCESS:
        return "SYNC_IN_PROCESS";
    case ACTIVE_GATEWAY_INPUT_TOO_NEW:
        return "INPUT_TOO_NEW";
    case ACTIVE_GATEWAY_NOT_CAPABLE:
        return "NOT_CAPABLE";
    case ACTIVE_GATEWAY_STARTED:
        return "STARTED";
    default:
        return "UNKNOWN";
    }
}

std::string CActiveLegacyGatewayManager::GetStatus() const
{
    switch (nState) {
    case ACTIVE_GATEWAY_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_GATEWAY_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Gateway";
    case ACTIVE_GATEWAY_INPUT_TOO_NEW:
        return strprintf("Gateway input must have at least %d confirmations", Params().GetConsensus().nGatewayMinimumConfirmations);
    case ACTIVE_GATEWAY_NOT_CAPABLE:
        return "Not capable gateway: " + strNotCapableReason;
    case ACTIVE_GATEWAY_STARTED:
        return "Gateway successfully started";
    default:
        return "Unknown";
    }
}

std::string CActiveLegacyGatewayManager::GetTypeString() const
{
    std::string strType;
    switch (eType) {
    case GATEWAY_REMOTE:
        strType = "REMOTE";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveLegacyGatewayManager::SendGatewayPing(CConnman& connman)
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) return false;

    if (!fPingerEnabled) {
        LogPrint("gateway", "CActiveLegacyGatewayManager::SendGatewayPing -- %s: gateway ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if (!gwnodeman.Has(activeGatewayInfo.outpoint)) {
        strNotCapableReason = "Gateway not in gateway list";
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        LogPrintf("CActiveLegacyGatewayManager::SendGatewayPing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CGatewayPing gwp(activeGatewayInfo.outpoint);
    gwp.nSentinelVersion = nSentinelVersion;
    gwp.fSentinelIsCurrent =
        (abs(GetAdjustedTime() - nSentinelPingTime) < GATEWAY_SENTINEL_PING_MAX_SECONDS);
    if (!gwp.Sign(activeGatewayInfo.legacyKeyOperator, activeGatewayInfo.legacyKeyIDOperator)) {
        LogPrintf("CActiveLegacyGatewayManager::SendGatewayPing -- ERROR: Couldn't sign Gateway Ping\n");
        return false;
    }

    // Update lastPing for our gateway in Gateway list
    if (gwnodeman.IsGatewayPingedWithin(activeGatewayInfo.outpoint, GATEWAY_MIN_GWP_SECONDS, gwp.sigTime)) {
        LogPrintf("CActiveLegacyGatewayManager::SendGatewayPing -- Too early to send Gateway Ping\n");
        return false;
    }

    gwnodeman.SetGatewayLastPing(activeGatewayInfo.outpoint, gwp);

    LogPrintf("CActiveLegacyGatewayManager::SendGatewayPing -- Relaying ping, collateral=%s\n", activeGatewayInfo.outpoint.ToStringShort());
    gwp.Relay(connman);

    return true;
}

bool CActiveLegacyGatewayManager::UpdateSentinelPing(int version)
{
    nSentinelVersion = version;
    nSentinelPingTime = GetAdjustedTime();

    return true;
}

void CActiveLegacyGatewayManager::DoMaintenance(CConnman& connman)
{
    if (ShutdownRequested()) return;

    ManageState(connman);
}

void CActiveLegacyGatewayManager::ManageStateInitial(CConnman& connman)
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) return;

    LogPrint("gateway", "CActiveLegacyGatewayManager::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Gateway must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(activeGatewayInfo.service) && CGateway::IsValidNetAddr(activeGatewayInfo.service);
    if (!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        connman.ForEachNodeContinueIf(CConnman::AllNodes, [&fFoundLocal, &empty](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(activeGatewayInfo.service, &pnode->addr) && CGateway::IsValidNetAddr(activeGatewayInfo.service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }

    if (!fFoundLocal && Params().NetworkIDString() == CBaseChainParams::REGTEST) {
        if (Lookup("127.0.0.1", activeGatewayInfo.service, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (activeGatewayInfo.service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", activeGatewayInfo.service.GetPort(), mainnetDefaultPort);
            LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (activeGatewayInfo.service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", activeGatewayInfo.service.GetPort(), mainnetDefaultPort);
        LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST) {
        // Check socket connectivity
        LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- Checking inbound connection to '%s'\n", activeGatewayInfo.service.ToString());
        SOCKET hSocket;
        bool fConnected = ConnectSocket(activeGatewayInfo.service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Could not connect to " + activeGatewayInfo.service.ToString();
            LogPrintf("CActiveLegacyGatewayManager::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }
    // Default to REMOTE
    eType = GATEWAY_REMOTE;

    LogPrint("gateway", "CActiveLegacyGatewayManager::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveLegacyGatewayManager::ManageStateRemote()
{
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) return;

    LogPrint("gateway", "CActiveLegacyGatewayManager::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, keyIDOperator = %s\n",
        GetStatus(), GetTypeString(), fPingerEnabled, activeGatewayInfo.legacyKeyIDOperator.ToString());

    gwnodeman.CheckGateway(activeGatewayInfo.legacyKeyIDOperator, true);
    gateway_info_t infoGw;
    if (gwnodeman.GetGatewayInfo(activeGatewayInfo.legacyKeyIDOperator, infoGw)) {
        if (infoGw.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (activeGatewayInfo.service != infoGw.addr) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this gateway changed recently.";
            LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CGateway::IsValidStateForAutoStart(infoGw.nActiveState)) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = strprintf("Gateway in %s state", CGateway::StateToString(infoGw.nActiveState));
            LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        auto dgw = deterministicGWManager->GetListAtChainTip().GetGWByCollateral(infoGw.outpoint);
        if (dgw) {
            if (dgw->pdgwState->addr != infoGw.addr) {
                nState = ACTIVE_GATEWAY_NOT_CAPABLE;
                strNotCapableReason = strprintf("Gateway collateral is a ProTx and ProTx address does not match local address");
                LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- Collateral is a ProTx\n");
        }
        if (nState != ACTIVE_GATEWAY_STARTED) {
            LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- STARTED!\n");
            activeGatewayInfo.outpoint = infoGw.outpoint;
            activeGatewayInfo.service = infoGw.addr;
            fPingerEnabled = true;
            nState = ACTIVE_GATEWAY_STARTED;
        }
    } else {
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Gateway not in gateway list";
        LogPrintf("CActiveLegacyGatewayManager::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}
