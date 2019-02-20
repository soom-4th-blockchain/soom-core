// Copyright (c) 2014-2018 The Dash Core developers
// Copyright (c) 2017-2019 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEGATEWAY_H
#define ACTIVEGATEWAY_H

#include "chainparams.h"
#include "key.h"
#include "net.h"
#include "primitives/transaction.h"
#include "validationinterface.h"

#include "evo/deterministicgws.h"
#include "evo/providertx.h"

struct CActiveGatewayInfo;
class CActiveLegacyGatewayManager;
class CActiveDeterministicGatewayManager;

static const int ACTIVE_GATEWAY_INITIAL          = 0; // initial state
static const int ACTIVE_GATEWAY_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_GATEWAY_INPUT_TOO_NEW    = 2;
static const int ACTIVE_GATEWAY_NOT_CAPABLE      = 3;
static const int ACTIVE_GATEWAY_STARTED          = 4;

extern CActiveGatewayInfo activeGatewayInfo;
extern CActiveLegacyGatewayManager legacyActiveGatewayManager;
extern CActiveDeterministicGatewayManager* activeGatewayManager;

struct CActiveGatewayInfo {
    // Keys for the active Gateway
    CKeyID legacyKeyIDOperator;
    CKey legacyKeyOperator;

    std::unique_ptr<CBLSPublicKey> blsPubKeyOperator;
    std::unique_ptr<CBLSSecretKey> blsKeyOperator;

    // Initialized while registering Gateway
    uint256 proTxHash;
    COutPoint outpoint;
    CService service;
};


class CActiveDeterministicGatewayManager : public CValidationInterface
{
public:
    enum gateway_state_t {
        GATEWAY_WAITING_FOR_PROTX,
        GATEWAY_POSE_BANNED,
        GATEWAY_REMOVED,
        GATEWAY_OPERATOR_KEY_CHANGED,
        GATEWAY_READY,
        GATEWAY_ERROR,
    };

private:
    CDeterministicGWCPtr gwListEntry;
    gateway_state_t state{GATEWAY_WAITING_FOR_PROTX};
    std::string strError;

public:
    virtual void UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload);

    void Init();

    CDeterministicGWCPtr GetDGW() const { return gwListEntry; }

    std::string GetStateString() const;
    std::string GetStatus() const;

private:
    bool GetLocalAddress(CService& addrRet);
};

// Responsible for activating the Gateway and pinging the network (legacy GW list)
class CActiveLegacyGatewayManager
{
public:
    enum gateway_type_enum_t {
        GATEWAY_UNKNOWN = 0,
        GATEWAY_REMOTE  = 1
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    gateway_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Gateway
    bool SendGatewayPing(CConnman& connman);

    //  sentinel ping data
    int64_t nSentinelPingTime;
    uint32_t nSentinelVersion;

public:
    int nState; // should be one of ACTIVE_GATEWAY_XXXX
    std::string strNotCapableReason;


    CActiveLegacyGatewayManager() :
        eType(GATEWAY_UNKNOWN),
        fPingerEnabled(false),
        nState(ACTIVE_GATEWAY_INITIAL)
    {
    }

    /// Manage state of active Gateway
    void ManageState(CConnman& connman);

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

    bool UpdateSentinelPing(int version);

    void DoMaintenance(CConnman& connman);

private:
    void ManageStateInitial(CConnman& connman);
    void ManageStateRemote();
};

#endif
