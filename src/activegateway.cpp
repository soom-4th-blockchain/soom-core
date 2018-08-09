// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "gateway.h"
#include "gateway-sync.h"
#include "gatewayman.h"
#include "protocol.h"
#include "util.h"

// Keep track of the active Gateway
CActiveGateway activeGateway;

void CActiveGateway::ManageState(CConnman& connman)
{
    LogPrint("gateway", "CActiveGateway::ManageState -- Start\n");
    if(!fGateWay) {
        LogPrint("gateway", "CActiveGateway::ManageState -- Not a gateway, returning\n");
        return;
    }

    if(Params().NetworkIDString() != CBaseChainParams::REGTEST && !gatewaySync.IsBlockchainSynced()) {
        nState = ACTIVE_GATEWAY_SYNC_IN_PROCESS;
        LogPrintf("CActiveGateway::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if(nState == ACTIVE_GATEWAY_SYNC_IN_PROCESS) {
        nState = ACTIVE_GATEWAY_INITIAL;
    }

    LogPrint("gateway", "CActiveGateway::ManageState -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    if(eType == GATEWAY_UNKNOWN) {
        ManageStateInitial(connman);
    }

    if(eType == GATEWAY_REMOTE) {
        ManageStateRemote();
    }

    SendGatewayPing(connman);
}

std::string CActiveGateway::GetStateString() const
{
    switch (nState) {
        case ACTIVE_GATEWAY_INITIAL:         return "INITIAL";
        case ACTIVE_GATEWAY_SYNC_IN_PROCESS: return "SYNC_IN_PROCESS";
        case ACTIVE_GATEWAY_INPUT_TOO_NEW:   return "INPUT_TOO_NEW";
        case ACTIVE_GATEWAY_NOT_CAPABLE:     return "NOT_CAPABLE";
        case ACTIVE_GATEWAY_STARTED:         return "STARTED";
        default:                                return "UNKNOWN";
    }
}

std::string CActiveGateway::GetStatus() const
{
    switch (nState) {
        case ACTIVE_GATEWAY_INITIAL:         return "Node just started, not yet activated";
        case ACTIVE_GATEWAY_SYNC_IN_PROCESS: return "Sync in progress. Must wait until sync is complete to start Gateway";
        case ACTIVE_GATEWAY_INPUT_TOO_NEW:   return strprintf("Gateway input must have at least %d confirmations", Params().GetConsensus().nGatewayMinimumConfirmations);
        case ACTIVE_GATEWAY_NOT_CAPABLE:     return "Not capable gateway: " + strNotCapableReason;
        case ACTIVE_GATEWAY_STARTED:         return "Gateway successfully started";
        default:                                return "Unknown";
    }
}

std::string CActiveGateway::GetTypeString() const
{
    std::string strType;
    switch(eType) {
    case GATEWAY_REMOTE:
        strType = "REMOTE";
        break;
    default:
        strType = "UNKNOWN";
        break;
    }
    return strType;
}

bool CActiveGateway::SendGatewayPing(CConnman& connman)
{
    if(!fPingerEnabled) {
        LogPrint("gateway", "CActiveGateway::SendGatewayPing -- %s: gateway ping service is disabled, skipping...\n", GetStateString());
        return false;
    }

    if(!gwnodeman.Has(outpoint)) {
        strNotCapableReason = "Gateway not in gateway list";
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        LogPrintf("CActiveGateway::SendGatewayPing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CGatewayPing gwp(outpoint);    
    if(!gwp.Sign(keyGateway, pubKeyGateway)) {
        LogPrintf("CActiveGateway::SendGatewayPing -- ERROR: Couldn't sign Gateway Ping\n");
        return false;
    }

    // Update lastPing for our gateway in Gateway list
    if(gwnodeman.IsGatewayPingedWithin(outpoint, GATEWAY_MIN_GWP_SECONDS, gwp.sigTime)) {
        LogPrintf("CActiveGateway::SendGatewayPing -- Too early to send Gateway Ping\n");
        return false;
    }

    gwnodeman.SetGatewayLastPing(outpoint, gwp);

    LogPrintf("CActiveGateway::SendGatewayPing -- Relaying ping, collateral=%s\n", outpoint.ToStringShort());
    gwp.Relay(connman);

    return true;
}

void CActiveGateway::ManageStateInitial(CConnman& connman)
{
    LogPrint("gateway", "CActiveGateway::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Gateway must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveGateway::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(service) && CGateway::IsValidNetAddr(service);
    if(!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        connman.ForEachNodeContinueIf(CConnman::AllNodes, [&fFoundLocal, &empty, this](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(service, &pnode->addr) && CGateway::IsValidNetAddr(service);
            return !fFoundLocal;
        });
        // nothing and no live connections, can't do anything for now
        if (empty) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
            LogPrintf("CActiveGateway::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    }

    if(!fFoundLocal) {
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveGateway::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

	if(!fLocalGateWay)
    {
	    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
	    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
	        if(service.GetPort() != mainnetDefaultPort) {
	            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
	            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(), mainnetDefaultPort);
	            LogPrintf("CActiveGateway::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
	            return;
	        }
	    } else if(service.GetPort() == mainnetDefaultPort) {
	        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
	        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(), mainnetDefaultPort);
	        LogPrintf("CActiveGateway::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
	        return;
	    }
	}
    LogPrintf("CActiveGateway::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());

    if(!connman.ConnectNode(CAddress(service, NODE_NETWORK), NULL, true)) {
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveGateway::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = GATEWAY_REMOTE;

    LogPrint("gateway", "CActiveGateway::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n", GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveGateway::ManageStateRemote()
{
    LogPrint("gateway", "CActiveGateway::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyGateway.GetID() = %s\n", 
             GetStatus(), GetTypeString(), fPingerEnabled, pubKeyGateway.GetID().ToString());

    gwnodeman.CheckGateway(pubKeyGateway, true);
    gateway_info_t infoGw;
    if(gwnodeman.GetGatewayInfo(pubKeyGateway, infoGw)) {
        if(infoGw.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveGateway::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(service != infoGw.addr) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this gateway changed recently.";
            LogPrintf("CActiveGateway::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(!CGateway::IsValidStateForAutoStart(infoGw.nActiveState)) {
            nState = ACTIVE_GATEWAY_NOT_CAPABLE;
            strNotCapableReason = strprintf("Gateway in %s state", CGateway::StateToString(infoGw.nActiveState));
            LogPrintf("CActiveGateway::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if(nState != ACTIVE_GATEWAY_STARTED) {
            LogPrintf("CActiveGateway::ManageStateRemote -- STARTED!\n");
            outpoint = infoGw.vin.prevout;
            service = infoGw.addr;
            fPingerEnabled = true;
            nState = ACTIVE_GATEWAY_STARTED;
        }
    }
    else {
        nState = ACTIVE_GATEWAY_NOT_CAPABLE;
        strNotCapableReason = "Gateway not in gateway list";
        LogPrintf("CActiveGateway::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}
