// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GATEWAY_H
#define GATEWAY_H

#include "key.h"
#include "validation.h"
#include "spork.h"

class CGateway;
class CGatewayBroadcast;
class CConnman;

static const int GATEWAY_CHECK_SECONDS               =   5;
static const int GATEWAY_MIN_GWB_SECONDS             =   5 * 60;
static const int GATEWAY_MIN_GWP_SECONDS             =  10 * 60;
static const int GATEWAY_EXPIRATION_SECONDS          =  65 * 60;
static const int GATEWAY_WATCHDOG_MAX_SECONDS        = 120 * 60;
static const int GATEWAY_NEW_START_REQUIRED_SECONDS  = 180 * 60;

static const int GATEWAY_POSE_BAN_MAX_SCORE          = 5;

//
// The Gateway Ping Class : Contains a different serialize method for sending pings from gateways throughout the network
//

class CGatewayPing
{
public:
    CTxIn vin{};
    uint256 blockHash{};
    int64_t sigTime{}; //gwb message times
    std::vector<unsigned char> vchSig{};
   
    CGatewayPing() = default;

    CGatewayPing(const COutPoint& outpoint);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);        
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    bool IsExpired() const { return GetAdjustedTime() - sigTime > GATEWAY_NEW_START_REQUIRED_SECONDS; }

    bool Sign(const CKey& keyGateway, const CPubKey& pubKeyGateway);
    bool CheckSignature(CPubKey& pubKeyGateway, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CGateway* pgw, bool fFromNewBroadcast, int& nDos, CConnman& connman);
    void Relay(CConnman& connman);
};

inline bool operator==(const CGatewayPing& a, const CGatewayPing& b)
{
    return a.vin == b.vin && a.blockHash == b.blockHash;
}
inline bool operator!=(const CGatewayPing& a, const CGatewayPing& b)
{
    return !(a == b);
}

struct gateway_info_t
{
    // Note: all these constructors can be removed once C++14 is enabled.
    // (in C++11 the member initializers wrongly disqualify this as an aggregate)
    gateway_info_t() = default;
    gateway_info_t(gateway_info_t const&) = default;

    gateway_info_t(int activeState, int protoVer, int64_t sTime) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime} {}

    gateway_info_t(int activeState, int protoVer, int64_t sTime,
                      COutPoint const& outpoint, CService const& addr,
                      CPubKey const& pkCollAddr, CPubKey const& pkGW,
                      int64_t tWatchdogV = 0) :
        nActiveState{activeState}, nProtocolVersion{protoVer}, sigTime{sTime},
        vin{outpoint}, addr{addr},
        pubKeyCollateralAddress{pkCollAddr}, pubKeyGateway{pkGW},
        nTimeLastWatchdogVote{tWatchdogV} {}

    int nActiveState = 0;
    int nProtocolVersion = 0;
    int64_t sigTime = 0; //gwb message time

    CTxIn vin{};
    CService addr{};
    CPubKey pubKeyCollateralAddress{};
    CPubKey pubKeyGateway{};
    int64_t nTimeLastWatchdogVote = 0;

 
    int64_t nTimeLastChecked = 0;
    int64_t nTimeLastPaid = 0;
    int64_t nTimeLastPing = 0; //* not in CGW
    bool fInfoValid = false; //* not in CGW
};

//
// The Gateway Class. It contains the input of the 5000SOOM, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CGateway : public gateway_info_t
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    enum state {
        GATEWAY_PRE_ENABLED,
        GATEWAY_ENABLED,
        GATEWAY_EXPIRED,
        GATEWAY_OUTPOINT_SPENT,
        GATEWAY_UPDATE_REQUIRED,
        GATEWAY_WATCHDOG_EXPIRED,
        GATEWAY_NEW_START_REQUIRED,
        GATEWAY_POSE_BAN
    };

    enum CollateralStatus {
        COLLATERAL_OK,
        COLLATERAL_UTXO_NOT_FOUND,
        COLLATERAL_INVALID_AMOUNT
    };


    CGatewayPing lastPing{};
    std::vector<unsigned char> vchSig{};

    uint256 nCollateralMinConfBlockHash{};
    int nBlockLastPaid{};
    int nPoSeBanScore{};
    int nPoSeBanHeight{};
    bool fUnitTest = false;


    CGateway();
    CGateway(const CGateway& other);
    CGateway(const CGatewayBroadcast& gwb);
    CGateway(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGatewayNew, int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGateway);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        READWRITE(nActiveState);
        READWRITE(nCollateralMinConfBlockHash);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fUnitTest);
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CGatewayBroadcast& gwb, CConnman& connman);

    static CollateralStatus CheckCollateral(const COutPoint& outpoint);
    static CollateralStatus CheckCollateral(const COutPoint& outpoint, int& nHeightRet);
    void Check(bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(int nSeconds, int64_t nTimeToCheckAt = -1)
    {
        if(lastPing == CGatewayPing()) return false;

        if(nTimeToCheckAt == -1) {
            nTimeToCheckAt = GetAdjustedTime();
        }
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    bool IsEnabled() { return nActiveState == GATEWAY_ENABLED; }
    bool IsPreEnabled() { return nActiveState == GATEWAY_PRE_ENABLED; }
    bool IsPoSeBanned() { return nActiveState == GATEWAY_POSE_BAN; }
    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified() { return nPoSeBanScore <= -GATEWAY_POSE_BAN_MAX_SCORE; }
    bool IsExpired() { return nActiveState == GATEWAY_EXPIRED; }
    bool IsOutpointSpent() { return nActiveState == GATEWAY_OUTPOINT_SPENT; }
    bool IsUpdateRequired() { return nActiveState == GATEWAY_UPDATE_REQUIRED; }
    bool IsWatchdogExpired() { return nActiveState == GATEWAY_WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() { return nActiveState == GATEWAY_NEW_START_REQUIRED; }

    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return  nActiveStateIn == GATEWAY_ENABLED ||
                nActiveStateIn == GATEWAY_PRE_ENABLED ||
                nActiveStateIn == GATEWAY_EXPIRED ||
                nActiveStateIn == GATEWAY_WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment()
    {
        if(nActiveState == GATEWAY_ENABLED) {
            return true;
        }
        if(nActiveState == GATEWAY_WATCHDOG_EXPIRED) {
            return true;
        }

        return false;
    }

    /// Is the input associated with collateral public key? (and there is 5000 SOOM - checking if valid gateway)
    bool IsInputAssociatedWithPubkey();

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < GATEWAY_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -GATEWAY_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }
    void PoSeBan() { nPoSeBanScore = GATEWAY_POSE_BAN_MAX_SCORE; }

    gateway_info_t GetInfo();

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;

    int GetLastPaidTime() { return nTimeLastPaid; }
    int GetLastPaidBlock() { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    void UpdateWatchdogVoteTime(uint64_t nVoteTime = 0);

    CGateway& operator=(CGateway const& from)
    {
        static_cast<gateway_info_t&>(*this)=from;
        lastPing = from.lastPing;
        vchSig = from.vchSig;
        nCollateralMinConfBlockHash = from.nCollateralMinConfBlockHash;
        nBlockLastPaid = from.nBlockLastPaid;
        nPoSeBanScore = from.nPoSeBanScore;
        nPoSeBanHeight = from.nPoSeBanHeight;
        fUnitTest = from.fUnitTest;
        return *this;
    }
};

inline bool operator==(const CGateway& a, const CGateway& b)
{
    return a.vin == b.vin;
}
inline bool operator!=(const CGateway& a, const CGateway& b)
{
    return !(a.vin == b.vin);
}


//
// The Gateway Broadcast Class : Contains a different serialize method for sending gateways through the network
//

class CGatewayBroadcast : public CGateway
{
public:

    bool fRecovery;

    CGatewayBroadcast() : CGateway(), fRecovery(false) {}
    CGatewayBroadcast(const CGateway& gw) : CGateway(gw), fRecovery(false) {}
    CGatewayBroadcast(CService addrNew, COutPoint outpointNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyGatewayNew, int nProtocolVersionIn) :
        CGateway(addrNew, outpointNew, pubKeyCollateralAddressNew, pubKeyGatewayNew, nProtocolVersionIn), fRecovery(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyGateway);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    /// Create Gateway broadcast, needs to be relayed manually after that
    static bool Create(const COutPoint& outpoint, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyGatewayNew, const CPubKey& pubKeyGatewayNew, std::string &strErrorRet, CGatewayBroadcast &gwbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CGatewayBroadcast &gwbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CGateway* pgw, int& nDos, CConnman& connman);
    bool CheckOutpoint(int& nDos);

    bool Sign(const CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void Relay(CConnman& connman);
};

class CGatewayVerification
{
public:
    CTxIn vin1{};
    CTxIn vin2{};
    CService addr{};
    int nonce{};
    int nBlockHeight{};
    std::vector<unsigned char> vchSig1{};
    std::vector<unsigned char> vchSig2{};

    CGatewayVerification() = default;

    CGatewayVerification(CService addr, int nonce, int nBlockHeight) :
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight)
    {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const
    {
        CInv inv(MSG_GATEWAY_VERIFY, GetHash());
        g_connman->RelayInv(inv);
    }
};

#endif
