// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEGATEWAY_H
#define ACTIVEGATEWAY_H

#include "chainparams.h"
#include "key.h"
#include "net.h"
#include "primitives/transaction.h"

class CActiveGateway;

static const int ACTIVE_GATEWAY_INITIAL          = 0; // initial state
static const int ACTIVE_GATEWAY_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_GATEWAY_INPUT_TOO_NEW    = 2;
static const int ACTIVE_GATEWAY_NOT_CAPABLE      = 3;
static const int ACTIVE_GATEWAY_STARTED          = 4;

extern CActiveGateway activeGateway;

// Responsible for activating the Gateway and pinging the network
class CActiveGateway
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

public:
    // Keys for the active Gateway
    CPubKey pubKeyGateway;
    CKey keyGateway;

    // Initialized while registering Gateway
    COutPoint outpoint;
    CService service;

    int nState; // should be one of ACTIVE_GATEWAY_XXXX
    std::string strNotCapableReason;


    CActiveGateway()
        : eType(GATEWAY_UNKNOWN),
          fPingerEnabled(false),
          pubKeyGateway(),
          keyGateway(),
          outpoint(),
          service(),
          nState(ACTIVE_GATEWAY_INITIAL)
    {}

    /// Manage state of active Gateway
    void ManageState(CConnman& connman);

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial(CConnman& connman);
    void ManageStateRemote();
};

#endif
