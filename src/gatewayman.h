// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GATEWAYMAN_H
#define GATEWAYMAN_H

#include "gateway.h"
#include "sync.h"

using namespace std;

class CGatewayMan;
class CConnman;

extern CGatewayMan gwnodeman;

class CGatewayMan
{
public:
    typedef std::pair<arith_uint256, CGateway*> score_pair_t;
    typedef std::vector<score_pair_t> score_pair_vec_t;
    typedef std::pair<int, CGateway> rank_pair_t;
    typedef std::vector<rank_pair_t> rank_pair_vec_t;

private:
    static const std::string SERIALIZATION_VERSION_STRING;

    static const int GWEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int GWB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int GWB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int GWB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int GWB_RECOVERY_WAIT_SECONDS      = 60;
    static const int GWB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block height
    int nCachedBlockHeight;

    // map to hold all GWs
    std::map<COutPoint, CGateway> mapGateways;
    // who's asked for the Gateway list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForGatewayList;
    // who we asked for the Gateway list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForGatewayList;
    // which Gateways we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForGatewayListEntry;
    // who we asked for the gateway verification
    std::map<CNetAddr, CGatewayVerification> mWeAskedForVerification;

    // these maps are used for gateway recovery from GATEWAY_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mGwbRecoveryRequests;
    std::map<uint256, std::vector<CGatewayBroadcast> > mGwbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledGwbRequestConnections;


    int64_t nLastWatchdogVoteTime;

    friend class CGatewaySync;
    /// Find an entry
    CGateway* Find(const COutPoint& outpoint);

    bool GetGatewayScores(const uint256& nBlockHash, score_pair_vec_t& vecGatewayScoresRet, int nMinProtocol = 0);

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CGatewayBroadcast> > mapSeenGatewayBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CGatewayPing> mapSeenGatewayPing;
    // Keep track of all verifications I've seen
    std::map<uint256, CGatewayVerification> mapSeenGatewayVerification;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(mapGateways);
        READWRITE(mAskedUsForGatewayList);
        READWRITE(mWeAskedForGatewayList);
        READWRITE(mWeAskedForGatewayListEntry);
        READWRITE(mGwbRecoveryRequests);
        READWRITE(mGwbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);


        READWRITE(mapSeenGatewayBroadcast);
        READWRITE(mapSeenGatewayPing);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CGatewayMan();

    /// Add an entry
    bool Add(CGateway &gw);

    /// Ask (source) node for gwb
    void AskForGW(CNode *pnode, const COutPoint& outpoint, CConnman& connman);
    void AskForGwb(CNode *pnode, const uint256 &hash);

    bool PoSeBan(const COutPoint &outpoint);
  
    /// Check all Gateways
    void Check();

    /// Check all Gateways and remove inactive
    void CheckAndRemove(CConnman& connman);
    /// This is dummy overload to be used for dumping/loading gwcache.dat
    void CheckAndRemove() {}

    /// Clear Gateway vector
    void Clear();

    /// Count Gateways filtered by nProtocolVersion.
    /// Gateway nProtocolVersion should match or be above the one specified in param here.
    int CountGateways(int nProtocolVersion = -1);
    /// Count enabled Gateways filtered by nProtocolVersion.
    /// Gateway nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Gateways by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void GwegUpdate(CNode* pnode, CConnman& connman);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const COutPoint& outpoint, CGateway& gatewayRet);
    bool Has(const COutPoint& outpoint);

    bool GetGatewayInfo(const COutPoint& outpoint, gateway_info_t& gwInfoRet);
    bool GetGatewayInfo(const CPubKey& pubKeyGateway, gateway_info_t& gwInfoRet);
    bool GetGatewayInfo(const CScript& payee, gateway_info_t& gwInfoRet);

    /// Find an entry in the gateway list that is next to be paid
    bool GetNextGatewayInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCountRet, gateway_info_t& gwInfoRet);
    /// Same as above but use current block height
    bool GetNextGatewayInQueueForPayment(bool fFilterSigTime, int& nCountRet, gateway_info_t& gwInfoRet);

    /// Find a random entry
    gateway_info_t FindRandomNotInVec(const std::vector<COutPoint> &vecToExclude, int nProtocolVersion = -1);

    std::map<COutPoint, CGateway> GetFullGatewayMap() { return mapGateways; }

    bool GetGatewayRanks(rank_pair_vec_t& vecGatewayRanksRet, int nBlockHeight = -1, int nMinProtocol = 0);
    bool GetGatewayRank(const COutPoint &outpoint, int& nRankRet, int nBlockHeight = -1, int nMinProtocol = 0);

    void ProcessGatewayConnections(CConnman& connman);
    std::pair<CService, std::set<uint256> > PopScheduledGwbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    void DoFullVerificationStep(CConnman& connman);
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CGateway*>& vSortedByAddr, CConnman& connman);
    void SendVerifyReply(CNode* pnode, CGatewayVerification& gwv, CConnman& connman);
    void ProcessVerifyReply(CNode* pnode, CGatewayVerification& gwv);
    void ProcessVerifyBroadcast(CNode* pnode, const CGatewayVerification& gwv);

    /// Return the number of (unique) Gateways
    int size() { return mapGateways.size(); }

    std::string ToString() const;

    /// Update gateway list and maps using provided CGatewayBroadcast
    void UpdateGatewayList(CGatewayBroadcast gwb, CConnman& connman);
    /// Perform complete check and only then update list and maps
    bool CheckGwbAndUpdateGatewayList(CNode* pfrom, CGatewayBroadcast gwb, int& nDos, CConnman& connman);
    bool IsGwbRecoveryRequested(const uint256& hash) { return mGwbRecoveryRequests.count(hash); }

    void UpdateLastPaid(const CBlockIndex* pindex);


    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const COutPoint& outpoint, uint64_t nVoteTime = 0);

    void CheckGateway(const CPubKey& pubKeyGateway, bool fForce);

    bool IsGatewayPingedWithin(const COutPoint& outpoint, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetGatewayLastPing(const COutPoint& outpoint, const CGatewayPing& gwp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

};

#endif
