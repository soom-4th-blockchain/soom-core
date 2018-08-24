// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "addrman.h"
#include "gateway-payments.h"
#include "gateway-sync.h"
#include "gatewayman.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "script/standard.h"
#include "util.h"

/** Gateway manager */
CGatewayMan gwnodeman;

const std::string CGatewayMan::SERIALIZATION_VERSION_STRING = "CGatewayMan-Version-7";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CGateway*>& t1,
                    const std::pair<int, CGateway*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreGW
{
    bool operator()(const std::pair<arith_uint256, CGateway*>& t1,
                    const std::pair<arith_uint256, CGateway*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareByAddr

{
    bool operator()(const CGateway* t1,
                    const CGateway* t2) const
    {
        return t1->addr < t2->addr;
    }
};

CGatewayMan::CGatewayMan()
: cs(),
  mapGateways(),
  mAskedUsForGatewayList(),
  mWeAskedForGatewayList(),
  mWeAskedForGatewayListEntry(),
  mWeAskedForVerification(),
  mGwbRecoveryRequests(),
  mGwbRecoveryGoodReplies(),
  listScheduledGwbRequestConnections(),
  nLastWatchdogVoteTime(0),
  mapSeenGatewayBroadcast(),
  mapSeenGatewayPing()
{}

bool CGatewayMan::Add(CGateway &gw)
{
    LOCK(cs);

    if (Has(gw.vin.prevout)) return false;

    LogPrint("gateway", "CGatewayMan::Add -- Adding new Gateway: addr=%s, %i now\n", gw.addr.ToString(), size() + 1);
    mapGateways[gw.vin.prevout] = gw;
    
    return true;
}

void CGatewayMan::AskForGW(CNode* pnode, const COutPoint& outpoint, CConnman& connman)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForGatewayListEntry.find(outpoint);
    if (it1 != mWeAskedForGatewayListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CGatewayMan::AskForGW -- Asking same peer %s for missing gateway entry again: %s\n", pnode->addr.ToString(), outpoint.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CGatewayMan::AskForGW -- Asking new peer %s for missing gateway entry: %s\n", pnode->addr.ToString(), outpoint.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CGatewayMan::AskForGW -- Asking peer %s for missing gateway entry for the first time: %s\n", pnode->addr.ToString(), outpoint.ToStringShort());
    }
    mWeAskedForGatewayListEntry[outpoint][pnode->addr] = GetTime() + GWEG_UPDATE_SECONDS;

    connman.PushMessage(pnode, NetMsgType::GWEG, CTxIn(outpoint));
}

bool CGatewayMan::PoSeBan(const COutPoint &outpoint)
{
    LOCK(cs);
    CGateway* pgw = Find(outpoint);
    if (!pgw) {
        return false;
    }
    pgw->PoSeBan();

    return true;
}

void CGatewayMan::Check()
{
    LOCK(cs);

    LogPrint("gateway", "CGatewayMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    for (auto& gwpair : mapGateways) {
        gwpair.second.Check();
    }
}

void CGatewayMan::CheckAndRemove(CConnman& connman)
{
    if(!gatewaySync.IsGatewayListSynced()) return;

    LogPrintf("CGatewayMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckGwbAndUpdateGatewayList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent gateways, prepare structures and make requests to reasure the state of inactive ones
        rank_pair_vec_t vecGatewayRanks;
        // ask for up to GWB_RECOVERY_MAX_ASK_ENTRIES gateway entries at a time
        int nAskForGwbRecovery = GWB_RECOVERY_MAX_ASK_ENTRIES;
        std::map<COutPoint, CGateway>::iterator it = mapGateways.begin();
        while (it != mapGateways.end()) {
            CGatewayBroadcast gwb = CGatewayBroadcast(it->second);
            uint256 hash = gwb.GetHash();
            // If collateral was spent ...
            if (it->second.IsOutpointSpent()) {
                LogPrint("gateway", "CGatewayMan::CheckAndRemove -- Removing Gateway: %s  addr=%s  %i now\n", it->second.GetStateString(), it->second.addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenGatewayBroadcast.erase(hash);
                mWeAskedForGatewayListEntry.erase(it->first);

                // and finally remove it from the list
              
                mapGateways.erase(it++);
               
            } else {
                bool fAsk = (nAskForGwbRecovery > 0) &&
                            gatewaySync.IsSynced() &&
                            it->second.IsNewStartRequired() &&
                            !IsGwbRecoveryRequested(hash);
                if(fAsk) {
                    // this gw is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecGatewayRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(nCachedBlockHeight);
                        GetGatewayRanks(vecGatewayRanks, nRandomBlockHeight);
                    }
                    bool fAskedForGwbRecovery = false;
                    // ask first GWB_RECOVERY_QUORUM_TOTAL gateways we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < GWB_RECOVERY_QUORUM_TOTAL && i < (int)vecGatewayRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForGatewayListEntry.count(it->first) && mWeAskedForGatewayListEntry[it->first].count(vecGatewayRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecGatewayRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledGwbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForGwbRecovery = true;
                    }
                    if(fAskedForGwbRecovery) {
                        LogPrint("gateway", "CGatewayMan::CheckAndRemove -- Recovery initiated, gateway=%s\n", it->first.ToStringShort());
                        nAskForGwbRecovery--;
                    }
                    // wait for gwb recovery replies for GWB_RECOVERY_WAIT_SECONDS seconds
                    mGwbRecoveryRequests[hash] = std::make_pair(GetTime() + GWB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for GATEWAY_NEW_START_REQUIRED gateways
        LogPrint("gateway", "CGatewayMan::CheckAndRemove -- mGwbRecoveryGoodReplies size=%d\n", (int)mGwbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CGatewayBroadcast> >::iterator itGwbReplies = mGwbRecoveryGoodReplies.begin();
        while(itGwbReplies != mGwbRecoveryGoodReplies.end()){
            if(mGwbRecoveryRequests[itGwbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itGwbReplies->second.size() >= GWB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this gw doesn't require new gwb, reprocess one of new gwbs
                    LogPrint("gateway", "CGatewayMan::CheckAndRemove -- reprocessing gwb, gateway=%s\n", itGwbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenGatewayBroadcast.erase(itGwbReplies->first);
                    int nDos;
                    itGwbReplies->second[0].fRecovery = true;
                    CheckGwbAndUpdateGatewayList(NULL, itGwbReplies->second[0], nDos, connman);
                }
                LogPrint("gateway", "CGatewayMan::CheckAndRemove -- removing gwb recovery reply, gateway=%s, size=%d\n", itGwbReplies->second[0].vin.prevout.ToStringShort(), (int)itGwbReplies->second.size());
                mGwbRecoveryGoodReplies.erase(itGwbReplies++);
            } else {
                ++itGwbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itGwbRequest = mGwbRecoveryRequests.begin();
        while(itGwbRequest != mGwbRecoveryRequests.end()){
            // Allow this gwb to be re-verified again after GWB_RECOVERY_RETRY_SECONDS seconds
            // if gw is still in GATEWAY_NEW_START_REQUIRED state.
            if(GetTime() - itGwbRequest->second.first > GWB_RECOVERY_RETRY_SECONDS) {
                mGwbRecoveryRequests.erase(itGwbRequest++);
            } else {
                ++itGwbRequest;
            }
        }

        // check who's asked for the Gateway list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForGatewayList.begin();
        while(it1 != mAskedUsForGatewayList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForGatewayList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Gateway list
        it1 = mWeAskedForGatewayList.begin();
        while(it1 != mWeAskedForGatewayList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForGatewayList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Gateways we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForGatewayListEntry.begin();
        while(it2 != mWeAskedForGatewayListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForGatewayListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CGatewayVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenGatewayBroadcast entries here, clean them on gwb updates!

        // remove expired mapSeenGatewayPing
        std::map<uint256, CGatewayPing>::iterator it4 = mapSeenGatewayPing.begin();
        while(it4 != mapSeenGatewayPing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("gateway", "CGatewayMan::CheckAndRemove -- Removing expired Gateway ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenGatewayPing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenGatewayVerification
        std::map<uint256, CGatewayVerification>::iterator itv2 = mapSeenGatewayVerification.begin();
        while(itv2 != mapSeenGatewayVerification.end()){
            if((*itv2).second.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS){
                LogPrint("gateway", "CGatewayMan::CheckAndRemove -- Removing expired Gateway verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenGatewayVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CGatewayMan::CheckAndRemove -- %s\n", ToString());
    }

   
}

void CGatewayMan::Clear()
{
    LOCK(cs);
    mapGateways.clear();
    mAskedUsForGatewayList.clear();
    mWeAskedForGatewayList.clear();
    mWeAskedForGatewayListEntry.clear();
    mapSeenGatewayBroadcast.clear();
    mapSeenGatewayPing.clear();
    nLastWatchdogVoteTime = 0;
}

int CGatewayMan::CountGateways(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? gwpayments.GetMinGatewayPaymentsProto() : nProtocolVersion;

    for (auto& gwpair : mapGateways) {
        if(gwpair.second.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CGatewayMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? gwpayments.GetMinGatewayPaymentsProto() : nProtocolVersion;

    for (auto& gwpair : mapGateways) {
        if(gwpair.second.nProtocolVersion < nProtocolVersion || !gwpair.second.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 gateways are allowed in 12.1, saving this for later
int CGatewayMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    for (auto& gwpair : mapGateways)
        if ((nNetworkType == NET_IPV4 && gwpair.second.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && gwpair.second.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && gwpair.second.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CGatewayMan::GwegUpdate(CNode* pnode, CConnman& connman)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForGatewayList.find(pnode->addr);
            if(it != mWeAskedForGatewayList.end() && GetTime() < (*it).second) {
                LogPrintf("CGatewayMan::GwegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }

    connman.PushMessage(pnode, NetMsgType::GWEG, CTxIn());
    int64_t askAgain = GetTime() + GWEG_UPDATE_SECONDS;
    mWeAskedForGatewayList[pnode->addr] = askAgain;

    LogPrint("gateway", "CGatewayMan::GwegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CGateway* CGatewayMan::Find(const COutPoint &outpoint)
{
    LOCK(cs);
    auto it = mapGateways.find(outpoint);
    return it == mapGateways.end() ? NULL : &(it->second);
}

bool CGatewayMan::Get(const COutPoint& outpoint, CGateway& gatewayRet)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    auto it = mapGateways.find(outpoint);
    if (it == mapGateways.end()) {
        return false;
    }

    gatewayRet = it->second;
    return true;
}

bool CGatewayMan::GetGatewayInfo(const COutPoint& outpoint, gateway_info_t& gwInfoRet)
{
    LOCK(cs);
    auto it = mapGateways.find(outpoint);
    if (it == mapGateways.end()) {
        return false;
    }
    gwInfoRet = it->second.GetInfo();
    return true;
}

bool CGatewayMan::GetGatewayInfo(const CPubKey& pubKeyGateway, gateway_info_t& gwInfoRet)
{
    LOCK(cs);
    for (auto& gwpair : mapGateways) {
        if (gwpair.second.pubKeyGateway == pubKeyGateway) {
            gwInfoRet = gwpair.second.GetInfo();
            return true;
        }
    }
    return false;
}

bool CGatewayMan::GetGatewayInfo(const CScript& payee, gateway_info_t& gwInfoRet)
{
    LOCK(cs);
    for (auto& gwpair : mapGateways) {
        CScript scriptCollateralAddress = GetScriptForDestination(gwpair.second.pubKeyCollateralAddress.GetID());
        if (scriptCollateralAddress == payee) {
            gwInfoRet = gwpair.second.GetInfo();
            return true;
        }
    }
    return false;
}

bool CGatewayMan::Has(const COutPoint& outpoint)
{
    LOCK(cs);
    return mapGateways.find(outpoint) != mapGateways.end();
}

//
// Deterministically select the oldest/best gateway to pay on the network
//
bool CGatewayMan::GetNextGatewayInQueueForPayment(bool fFilterSigTime, int& nCountRet, gateway_info_t& gwInfoRet)
{
    return GetNextGatewayInQueueForPayment(nCachedBlockHeight, fFilterSigTime, nCountRet, gwInfoRet);
}

bool CGatewayMan::GetNextGatewayInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCountRet, gateway_info_t& gwInfoRet)
{
    gwInfoRet = gateway_info_t();
    nCountRet = 0;

    if (!gatewaySync.IsWinnersListSynced()) {
        // without winner list we can't reliably find the next winner anyway
        return false;
    }

    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    std::vector<std::pair<int, CGateway*> > vecGatewayLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nGwCount = CountGateways();

    for (auto& gwpair : mapGateways) {
        if(!gwpair.second.IsValidForPayment()) continue;

        //check protocol version
        if(gwpair.second.nProtocolVersion < gwpayments.GetMinGatewayPaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if(gwpayments.IsScheduled(gwpair.second, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if(fFilterSigTime && gwpair.second.sigTime + (nGwCount*2.6*60) > GetAdjustedTime()) continue;

        //make sure it has at least as many confirmations as there are gateways
        if(GetUTXOConfirmations(gwpair.first) < nGwCount) continue;

        vecGatewayLastPaid.push_back(std::make_pair(gwpair.second.GetLastPaidBlock(), &gwpair.second));
    }

    nCountRet = (int)vecGatewayLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCountRet < nGwCount/3)
        return GetNextGatewayInQueueForPayment(nBlockHeight, false, nCountRet, gwInfoRet);

    // Sort them low to high
    sort(vecGatewayLastPaid.begin(), vecGatewayLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CGateway::GetNextGatewayInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return false;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nGwCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    CGateway *pBestGateway = NULL;
    BOOST_FOREACH (PAIRTYPE(int, CGateway*)& s, vecGatewayLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestGateway = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    if (pBestGateway) {
        gwInfoRet = pBestGateway->GetInfo();
    }
    return gwInfoRet.fInfoValid;
}

gateway_info_t CGatewayMan::FindRandomNotInVec(const std::vector<COutPoint> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? gwpayments.GetMinGatewayPaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CGatewayMan::FindRandomNotInVec -- %d enabled gateways, %d gateways to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return gateway_info_t();

    // fill a vector of pointers
    std::vector<CGateway*> vpGatewaysShuffled;
    for (auto& gwpair : mapGateways) {
        vpGatewaysShuffled.push_back(&gwpair.second);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpGatewaysShuffled.begin(), vpGatewaysShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CGateway* pgw, vpGatewaysShuffled) {
        if(pgw->nProtocolVersion < nProtocolVersion || !pgw->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const COutPoint &outpointToExclude, vecToExclude) {
            if(pgw->vin.prevout == outpointToExclude) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("gateway", "CGatewayMan::FindRandomNotInVec -- found, gateway=%s\n", pgw->vin.prevout.ToStringShort());
        return pgw->GetInfo();
    }

    LogPrint("gateway", "CGatewayMan::FindRandomNotInVec -- failed\n");
    return gateway_info_t();
}

bool CGatewayMan::GetGatewayScores(const uint256& nBlockHash, CGatewayMan::score_pair_vec_t& vecGatewayScoresRet, int nMinProtocol)
{
    vecGatewayScoresRet.clear();

    if (!gatewaySync.IsGatewayListSynced())
        return false;

    AssertLockHeld(cs);

    if (mapGateways.empty())
        return false;

    // calculate scores
    for (auto& gwpair : mapGateways) {
        if (gwpair.second.nProtocolVersion >= nMinProtocol) {
            vecGatewayScoresRet.push_back(std::make_pair(gwpair.second.CalculateScore(nBlockHash), &gwpair.second));
        }
    }

    sort(vecGatewayScoresRet.rbegin(), vecGatewayScoresRet.rend(), CompareScoreGW());
    return !vecGatewayScoresRet.empty();
}

bool CGatewayMan::GetGatewayRank(const COutPoint& outpoint, int& nRankRet, int nBlockHeight, int nMinProtocol)
{
    nRankRet = -1;

    if (!gatewaySync.IsGatewayListSynced())
        return false;

    // make sure we know about this block
    uint256 nBlockHash = uint256();
    if (!GetBlockHash(nBlockHash, nBlockHeight)) {
        LogPrintf("CGatewayMan::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    LOCK(cs);

    score_pair_vec_t vecGatewayScores;
    if (!GetGatewayScores(nBlockHash, vecGatewayScores, nMinProtocol))
        return false;

    int nRank = 0;
    for (auto& scorePair : vecGatewayScores) {
        nRank++;
        if(scorePair.second->vin.prevout == outpoint) {
            nRankRet = nRank;
            return true;
        }
    }

    return false;
}

bool CGatewayMan::GetGatewayRanks(CGatewayMan::rank_pair_vec_t& vecGatewayRanksRet, int nBlockHeight, int nMinProtocol)
{
    vecGatewayRanksRet.clear();

    if (!gatewaySync.IsGatewayListSynced())
        return false;

    // make sure we know about this block
    uint256 nBlockHash = uint256();
    if (!GetBlockHash(nBlockHash, nBlockHeight)) {
        LogPrintf("CGatewayMan::%s -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", __func__, nBlockHeight);
        return false;
    }

    LOCK(cs);

    score_pair_vec_t vecGatewayScores;
    if (!GetGatewayScores(nBlockHash, vecGatewayScores, nMinProtocol))
        return false;

    int nRank = 0;
    for (auto& scorePair : vecGatewayScores) {
        nRank++;
        vecGatewayRanksRet.push_back(std::make_pair(nRank, *scorePair.second));
    }

    return true;
}

void CGatewayMan::ProcessGatewayConnections(CConnman& connman)
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    connman.ForEachNode(CConnman::AllNodes, [](CNode* pnode) {
        if(pnode->fGateway) {
            LogPrintf("Closing Gateway connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    });
}

std::pair<CService, std::set<uint256> > CGatewayMan::PopScheduledGwbRequestConnection()
{
    LOCK(cs);
    if(listScheduledGwbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledGwbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledGwbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledGwbRequestConnections.begin();
    while(it != listScheduledGwbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledGwbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CGatewayMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(fLiteMode) return; // disable all Soom specific functionality

    if (strCommand == NetMsgType::GWANNOUNCE) { //Gateway Broadcast

        CGatewayBroadcast gwb;
        vRecv >> gwb;

        pfrom->setAskFor.erase(gwb.GetHash());

        if(!gatewaySync.IsBlockchainSynced()) return;

        LogPrint("gateway", "GWANNOUNCE -- Gateway announce, gateway=%s\n", gwb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckGwbAndUpdateGatewayList(pfrom, gwb, nDos, connman)) {
            // use announced Gateway as a peer
            connman.AddNewAddress(CAddress(gwb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

    } else if (strCommand == NetMsgType::GWPING) { //Gateway Ping

        CGatewayPing gwp;
        vRecv >> gwp;

        uint256 nHash = gwp.GetHash();

        pfrom->setAskFor.erase(nHash);

        if(!gatewaySync.IsBlockchainSynced()) return;

        LogPrint("gateway", "GWPING -- Gateway ping, gateway=%s\n", gwp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenGatewayPing.count(nHash)) return; //seen
        mapSeenGatewayPing.insert(std::make_pair(nHash, gwp));

        LogPrint("gateway", "GWPING -- Gateway ping, gateway=%s new\n", gwp.vin.prevout.ToStringShort());

        // see if we have this Gateway
        CGateway* pgw = Find(gwp.vin.prevout);
        
        // too late, new GWANNOUNCE is required
        if(pgw && pgw->IsNewStartRequired()) return;

        int nDos = 0;
        if(gwp.CheckAndUpdate(pgw, false, nDos, connman)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pgw != NULL) {
            // nothing significant failed, gw is a known one too
            return;
        }

        // something significant is broken or gw is unknown,
        // we might have to ask for a gateway entry once
        AskForGW(pfrom, gwp.vin.prevout, connman);

    } else if (strCommand == NetMsgType::GWEG) { //Get Gateway list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after gateway list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!gatewaySync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("gateway", "GWEG -- Gateway list, gateway=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator it = mAskedUsForGatewayList.find(pfrom->addr);
                if (it != mAskedUsForGatewayList.end() && it->second > GetTime()) {
                    Misbehaving(pfrom->GetId(), 34);
                    LogPrintf("GWEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                    return;
                }
                int64_t askAgain = GetTime() + GWEG_UPDATE_SECONDS;
                mAskedUsForGatewayList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        for (auto& gwpair : mapGateways) {
            if (vin != CTxIn() && vin != gwpair.second.vin) continue; // asked for specific vin but we are not there yet
            if (gwpair.second.addr.IsRFC1918() || gwpair.second.addr.IsLocal()) continue; // do not send local network gateway
            if (gwpair.second.IsUpdateRequired()) continue; // do not send outdated gateways

            LogPrint("gateway", "GWEG -- Sending Gateway entry: gateway=%s  addr=%s\n", gwpair.first.ToStringShort(), gwpair.second.addr.ToString());
            CGatewayBroadcast gwb = CGatewayBroadcast(gwpair.second);
            CGatewayPing gwp = gwpair.second.lastPing;
            uint256 hashGWB = gwb.GetHash();
            uint256 hashGWP = gwp.GetHash();
            pfrom->PushInventory(CInv(MSG_GATEWAY_ANNOUNCE, hashGWB));
            pfrom->PushInventory(CInv(MSG_GATEWAY_PING, hashGWP));
            nInvCount++;

            mapSeenGatewayBroadcast.insert(std::make_pair(hashGWB, std::make_pair(GetTime(), gwb)));
            mapSeenGatewayPing.insert(std::make_pair(hashGWP, gwp));

            if (vin.prevout == gwpair.first) {
                LogPrintf("GWEG -- Sent 1 Gateway inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            connman.PushMessage(pfrom, NetMsgType::SYNCSTATUSCOUNT, GATEWAY_SYNC_LIST, nInvCount);
            LogPrintf("GWEG -- Sent %d Gateway invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("gateway", "GWEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::GWVERIFY) { // Gateway Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CGatewayVerification gwv;
        vRecv >> gwv;

        pfrom->setAskFor.erase(gwv.GetHash());

        if(!gatewaySync.IsGatewayListSynced()) return;

        if(gwv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, gwv, connman);
        } else if (gwv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some gateway
            ProcessVerifyReply(pfrom, gwv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some gateway which verified another one
            ProcessVerifyBroadcast(pfrom, gwv);
        }
    }
}

// Verification of gateways via unique direct requests.

void CGatewayMan::DoFullVerificationStep(CConnman& connman)
{
    if(activeGateway.outpoint == COutPoint()) return;
    if(!gatewaySync.IsSynced()) return;

    rank_pair_vec_t vecGatewayRanks;
    GetGatewayRanks(vecGatewayRanks, nCachedBlockHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecGatewayRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CGateway> >::iterator it = vecGatewayRanks.begin();
    while(it != vecGatewayRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("gateway", "CGatewayMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin.prevout == activeGateway.outpoint) {
            nMyRank = it->first;
            LogPrint("gateway", "CGatewayMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d gateways\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this gateway is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS gateways
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecGatewayRanks.size()) return;

    std::vector<CGateway*> vSortedByAddr;
    for (auto& gwpair : mapGateways) {
        vSortedByAddr.push_back(&gwpair.second);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecGatewayRanks.begin() + nOffset;
    while(it != vecGatewayRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("gateway", "CGatewayMan::DoFullVerificationStep -- Already %s%s%s gateway %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecGatewayRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("gateway", "CGatewayMan::DoFullVerificationStep -- Verifying gateway %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr, connman)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecGatewayRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("gateway", "CGatewayMan::DoFullVerificationStep -- Sent verification requests to %d gateways\n", nCount);
}

// This function tries to find gateways with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CGatewayMan::CheckSameAddr()
{
    if(!gatewaySync.IsSynced() || mapGateways.empty()) return;

    std::vector<CGateway*> vBan;
    std::vector<CGateway*> vSortedByAddr;

    {
        LOCK(cs);

        CGateway* pprevGateway = NULL;
        CGateway* pverifiedGateway = NULL;

        for (auto& gwpair : mapGateways) {
            vSortedByAddr.push_back(&gwpair.second);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CGateway* pgw, vSortedByAddr) {
            // check only (pre)enabled gateways
            if(!pgw->IsEnabled() && !pgw->IsPreEnabled()) continue;
            // initial step
            if(!pprevGateway) {
                pprevGateway = pgw;
                pverifiedGateway = pgw->IsPoSeVerified() ? pgw : NULL;
                continue;
            }
            // second+ step
            if(pgw->addr == pprevGateway->addr) {
                if(pverifiedGateway) {
                    // another gateway with the same ip is verified, ban this one
                    vBan.push_back(pgw);
                } else if(pgw->IsPoSeVerified()) {
                    // this gateway with the same ip is verified, ban previous one
                    vBan.push_back(pprevGateway);
                    // and keep a reference to be able to ban following gateways with the same ip
                    pverifiedGateway = pgw;
                }
            } else {
                pverifiedGateway = pgw->IsPoSeVerified() ? pgw : NULL;
            }
            pprevGateway = pgw;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CGateway* pgw, vBan) {
        LogPrintf("CGatewayMan::CheckSameAddr -- increasing PoSe ban score for gateway %s\n", pgw->vin.prevout.ToStringShort());
        pgw->IncreasePoSeBanScore();
    }
}

bool CGatewayMan::SendVerifyRequest(const CAddress& addr, const std::vector<CGateway*>& vSortedByAddr, CConnman& connman)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::GWVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("gateway", "CGatewayMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = connman.ConnectNode(addr, NULL, true);
    if(pnode == NULL) {
        LogPrintf("CGatewayMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::GWVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CGatewayVerification gwv(addr, GetRandInt(999999), nCachedBlockHeight - 1);
    mWeAskedForVerification[addr] = gwv;
    LogPrintf("CGatewayMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", gwv.nonce, addr.ToString());
    connman.PushMessage(pnode, NetMsgType::GWVERIFY, gwv);

    return true;
}

void CGatewayMan::SendVerifyReply(CNode* pnode, CGatewayVerification& gwv, CConnman& connman)
{
    // only gateways can sign this, why would someone ask regular node?
    if(!fGateWay) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::GWVERIFY)+"-reply")) {
        // peer should not ask us that often
        LogPrintf("GatewayMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, gwv.nBlockHeight)) {
        LogPrintf("GatewayMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", gwv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeGateway.service.ToString(false), gwv.nonce, blockHash.ToString());

    if(!CMessageSigner::SignMessage(strMessage, gwv.vchSig1, activeGateway.keyGateway)) {
        LogPrintf("GatewayMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!CMessageSigner::VerifyMessage(activeGateway.pubKeyGateway, gwv.vchSig1, strMessage, strError)) {
        LogPrintf("GatewayMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    connman.PushMessage(pnode, NetMsgType::GWVERIFY, gwv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::GWVERIFY)+"-reply");
}

void CGatewayMan::ProcessVerifyReply(CNode* pnode, CGatewayVerification& gwv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::GWVERIFY)+"-request")) {
        LogPrintf("CGatewayMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != gwv.nonce) {
        LogPrintf("CGatewayMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, gwv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != gwv.nBlockHeight) {
        LogPrintf("CGatewayMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, gwv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, gwv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("GatewayMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", gwv.nBlockHeight, pnode->id);
        return;
    }

    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::GWVERIFY)+"-done")) {
        LogPrintf("CGatewayMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CGateway* prealGateway = NULL;
        std::vector<CGateway*> vpGatewaysToBan;
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(false), gwv.nonce, blockHash.ToString());
        for (auto& gwpair : mapGateways) {
            if(CAddress(gwpair.second.addr, NODE_NETWORK) == pnode->addr) {
                if(CMessageSigner::VerifyMessage(gwpair.second.pubKeyGateway, gwv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealGateway = &gwpair.second;
                    if(!gwpair.second.IsPoSeVerified()) {
                        gwpair.second.DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::GWVERIFY)+"-done");

                    // we can only broadcast it if we are an activated gateway
                    if(activeGateway.outpoint == COutPoint()) continue;
                    // update ...
                    gwv.addr = gwpair.second.addr;
                    gwv.vin1 = gwpair.second.vin;
                    gwv.vin2 = CTxIn(activeGateway.outpoint);
                    std::string strMessage2 = strprintf("%s%d%s%s%s", gwv.addr.ToString(false), gwv.nonce, blockHash.ToString(),
                                            gwv.vin1.prevout.ToStringShort(), gwv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!CMessageSigner::SignMessage(strMessage2, gwv.vchSig2, activeGateway.keyGateway)) {
                        LogPrintf("GatewayMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!CMessageSigner::VerifyMessage(activeGateway.pubKeyGateway, gwv.vchSig2, strMessage2, strError)) {
                        LogPrintf("GatewayMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = gwv;
                    mapSeenGatewayVerification.insert(std::make_pair(gwv.GetHash(), gwv));
                    gwv.Relay();

                } else {
                    vpGatewaysToBan.push_back(&gwpair.second);
                }
            }
        }
        // no real gateway found?...
        if(!prealGateway) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CGatewayMan::ProcessVerifyReply -- ERROR: no real gateway found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CGatewayMan::ProcessVerifyReply -- verified real gateway %s for addr %s\n",
                    prealGateway->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CGateway* pgw, vpGatewaysToBan) {
            pgw->IncreasePoSeBanScore();
            LogPrint("gateway", "CGatewayMan::ProcessVerifyReply -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealGateway->vin.prevout.ToStringShort(), pnode->addr.ToString(), pgw->nPoSeBanScore);
        }
        if(!vpGatewaysToBan.empty())
            LogPrintf("CGatewayMan::ProcessVerifyReply -- PoSe score increased for %d fake gateways, addr %s\n",
                        (int)vpGatewaysToBan.size(), pnode->addr.ToString());
    }
}

void CGatewayMan::ProcessVerifyBroadcast(CNode* pnode, const CGatewayVerification& gwv)
{
    std::string strError;

    if(mapSeenGatewayVerification.find(gwv.GetHash()) != mapSeenGatewayVerification.end()) {
        // we already have one
        return;
    }
    mapSeenGatewayVerification[gwv.GetHash()] = gwv;

    // we don't care about history
    if(gwv.nBlockHeight < nCachedBlockHeight - MAX_POSE_BLOCKS) {
        LogPrint("gateway", "CGatewayMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    nCachedBlockHeight, gwv.nBlockHeight, pnode->id);
        return;
    }

    if(gwv.vin1.prevout == gwv.vin2.prevout) {
        LogPrint("gateway", "CGatewayMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    gwv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, gwv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", gwv.nBlockHeight, pnode->id);
        return;
    }

    int nRank;

    if (!GetGatewayRank(gwv.vin2.prevout, nRank, gwv.nBlockHeight, MIN_POSE_PROTO_VERSION)) {
        LogPrint("gateway", "CGatewayMan::ProcessVerifyBroadcast -- Can't calculate rank for gateway %s\n",
                    gwv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("gateway", "CGatewayMan::ProcessVerifyBroadcast -- Gateway %s is not in top %d, current rank %d, peer=%d\n",
                    gwv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", gwv.addr.ToString(false), gwv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", gwv.addr.ToString(false), gwv.nonce, blockHash.ToString(),
                                gwv.vin1.prevout.ToStringShort(), gwv.vin2.prevout.ToStringShort());

        CGateway* pgw1 = Find(gwv.vin1.prevout);
        if(!pgw1) {
            LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- can't find gateway1 %s\n", gwv.vin1.prevout.ToStringShort());
            return;
        }

        CGateway* pgw2 = Find(gwv.vin2.prevout);
        if(!pgw2) {
            LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- can't find gateway2 %s\n", gwv.vin2.prevout.ToStringShort());
            return;
        }

        if(pgw1->addr != gwv.addr) {
            LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- addr %s does not match %s\n", gwv.addr.ToString(), pgw1->addr.ToString());
            return;
        }

        if(!CMessageSigner::VerifyMessage(pgw1->pubKeyGateway, gwv.vchSig1, strMessage1, strError)) {
            LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- VerifyMessage() for gateway1 failed, error: %s\n", strError);
            return;
        }

        if(!CMessageSigner::VerifyMessage(pgw2->pubKeyGateway, gwv.vchSig2, strMessage2, strError)) {
            LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- VerifyMessage() for gateway2 failed, error: %s\n", strError);
            return;
        }

        if(!pgw1->IsPoSeVerified()) {
            pgw1->DecreasePoSeBanScore();
        }
        gwv.Relay();

        LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- verified gateway %s for addr %s\n",
                    pgw1->vin.prevout.ToStringShort(), pgw1->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        for (auto& gwpair : mapGateways) {
            if(gwpair.second.addr != gwv.addr || gwpair.first == gwv.vin1.prevout) continue;
            gwpair.second.IncreasePoSeBanScore();
            nCount++;
            LogPrint("gateway", "CGatewayMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        gwpair.first.ToStringShort(), gwpair.second.addr.ToString(), gwpair.second.nPoSeBanScore);
        }
        if(nCount)
            LogPrintf("CGatewayMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake gateways, addr %s\n",
                        nCount, pgw1->addr.ToString());
    }
}

std::string CGatewayMan::ToString() const
{
    std::ostringstream info;

    info << "Gateways: " << (int)mapGateways.size() <<
            ", peers who asked us for Gateway list: " << (int)mAskedUsForGatewayList.size() <<
            ", peers we asked for Gateway list: " << (int)mWeAskedForGatewayList.size() <<
            ", entries in Gateway list we asked for: " << (int)mWeAskedForGatewayListEntry.size();

    return info.str();
}

void CGatewayMan::UpdateGatewayList(CGatewayBroadcast gwb, CConnman& connman)
{
    LOCK2(cs_main, cs);
    mapSeenGatewayPing.insert(std::make_pair(gwb.lastPing.GetHash(), gwb.lastPing));
    mapSeenGatewayBroadcast.insert(std::make_pair(gwb.GetHash(), std::make_pair(GetTime(), gwb)));

    LogPrintf("CGatewayMan::UpdateGatewayList -- gateway=%s  addr=%s\n", gwb.vin.prevout.ToStringShort(), gwb.addr.ToString());

    CGateway* pgw = Find(gwb.vin.prevout);
    if(pgw == NULL) {
        if(Add(gwb)) {
            gatewaySync.BumpAssetLastTime("CGatewayMan::UpdateGatewayList - new");
        }
    } else {
        CGatewayBroadcast gwbOld = mapSeenGatewayBroadcast[CGatewayBroadcast(*pgw).GetHash()].second;
        if(pgw->UpdateFromNewBroadcast(gwb, connman)) {
            gatewaySync.BumpAssetLastTime("CGatewayMan::UpdateGatewayList - seen");
            mapSeenGatewayBroadcast.erase(gwbOld.GetHash());
        }
    }
}

bool CGatewayMan::CheckGwbAndUpdateGatewayList(CNode* pfrom, CGatewayBroadcast gwb, int& nDos, CConnman& connman)
{
    // Need to lock cs_main here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gateway=%s\n", gwb.vin.prevout.ToStringShort());

        uint256 hash = gwb.GetHash();
        if(mapSeenGatewayBroadcast.count(hash) && !gwb.fRecovery) { //seen
            LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gateway=%s seen\n", gwb.vin.prevout.ToStringShort());
            // less then 2 pings left before this GW goes into non-recoverable state, bump sync timeout
            if(GetTime() - mapSeenGatewayBroadcast[hash].first > GATEWAY_NEW_START_REQUIRED_SECONDS - GATEWAY_MIN_GWP_SECONDS * 2) {
                LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gateway=%s seen update\n", gwb.vin.prevout.ToStringShort());
                mapSeenGatewayBroadcast[hash].first = GetTime();
                gatewaySync.BumpAssetLastTime("CGatewayMan::CheckGwbAndUpdateGatewayList - seen");
            }
            // did we ask this node for it?
            if(pfrom && IsGwbRecoveryRequested(hash) && GetTime() < mGwbRecoveryRequests[hash].first) {
                LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gwb=%s seen request\n", hash.ToString());
                if(mGwbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gwb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same gwb multiple times in recovery mode
                    mGwbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if(gwb.lastPing.sigTime > mapSeenGatewayBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CGateway gwTemp = CGateway(gwb);
                        gwTemp.Check();
                        LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gwb=%s seen request, addr=%s, better lastPing: %d min ago, projected gw state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetAdjustedTime() - gwb.lastPing.sigTime)/60, gwTemp.GetStateString());
                        if(gwTemp.IsValidStateForAutoStart(gwTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gateway=%s seen good\n", gwb.vin.prevout.ToStringShort());
                            mGwbRecoveryGoodReplies[hash].push_back(gwb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenGatewayBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), gwb)));

        LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- gateway=%s new\n", gwb.vin.prevout.ToStringShort());

        if(!gwb.SimpleCheck(nDos)) {
            LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- SimpleCheck() failed, gateway=%s\n", gwb.vin.prevout.ToStringShort());
            return false;
        }

        // search Gateway list
        CGateway* pgw = Find(gwb.vin.prevout);
        if(pgw) {
            CGatewayBroadcast gwbOld = mapSeenGatewayBroadcast[CGatewayBroadcast(*pgw).GetHash()].second;
            if(!gwb.Update(pgw, nDos, connman)) {
                LogPrint("gateway", "CGatewayMan::CheckGwbAndUpdateGatewayList -- Update() failed, gateway=%s\n", gwb.vin.prevout.ToStringShort());
                return false;
            }
            if(hash != gwbOld.GetHash()) {
                mapSeenGatewayBroadcast.erase(gwbOld.GetHash());
            }
            return true;
        }
    }

    if(gwb.CheckOutpoint(nDos)) {
        Add(gwb);
        gatewaySync.BumpAssetLastTime("CGatewayMan::CheckGwbAndUpdateGatewayList - new");
        // if it matches our Gateway privkey...
        if(fGateWay && gwb.pubKeyGateway == activeGateway.pubKeyGateway) {
            gwb.nPoSeBanScore = -GATEWAY_POSE_BAN_MAX_SCORE;
            if(gwb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CGatewayMan::CheckGwbAndUpdateGatewayList -- Got NEW Gateway entry: gateway=%s  sigTime=%lld  addr=%s\n",
                            gwb.vin.prevout.ToStringShort(), gwb.sigTime, gwb.addr.ToString());
                activeGateway.ManageState(connman);
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CGatewayMan::CheckGwbAndUpdateGatewayList -- wrong PROTOCOL_VERSION, re-activate your GW: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", gwb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        gwb.Relay(connman);
    } else {
        LogPrintf("CGatewayMan::CheckGwbAndUpdateGatewayList -- Rejected Gateway entry: %s  addr=%s\n", gwb.vin.prevout.ToStringShort(), gwb.addr.ToString());
        return false;
    }

    return true;
}

void CGatewayMan::UpdateLastPaid(const CBlockIndex* pindex)
{
    LOCK(cs);

    if(fLiteMode || !gatewaySync.IsWinnersListSynced() || mapGateways.empty()) return;

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a gateway
    // (GWs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fGateWay) ? gwpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    // LogPrint("gwpayments", "CGatewayMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
    //                         nCachedBlockHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    for (auto& gwpair: mapGateways) {
        gwpair.second.UpdateLastPaid(pindex, nMaxBlocksToScanBack);
    }

    IsFirstRun = false;
}

void CGatewayMan::UpdateWatchdogVoteTime(const COutPoint& outpoint, uint64_t nVoteTime)
{
    LOCK(cs);
    CGateway* pgw = Find(outpoint);
    if(!pgw) {
        return;
    }
    pgw->UpdateWatchdogVoteTime(nVoteTime);
    nLastWatchdogVoteTime = GetTime();
}

bool CGatewayMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any gateways have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= GATEWAY_WATCHDOG_MAX_SECONDS;
}


void CGatewayMan::CheckGateway(const CPubKey& pubKeyGateway, bool fForce)
{
    LOCK(cs);
    for (auto& gwpair : mapGateways) {
        if (gwpair.second.pubKeyGateway == pubKeyGateway) {
            gwpair.second.Check(fForce);
            return;
        }
    }
}

bool CGatewayMan::IsGatewayPingedWithin(const COutPoint& outpoint, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CGateway* pgw = Find(outpoint);
    return pgw ? pgw->IsPingedWithin(nSeconds, nTimeToCheckAt) : false;
}

void CGatewayMan::SetGatewayLastPing(const COutPoint& outpoint, const CGatewayPing& gwp)
{
    LOCK(cs);
    CGateway* pgw = Find(outpoint);
    if(!pgw) {
        return;
    }
    pgw->lastPing = gwp;
    
    mapSeenGatewayPing.insert(std::make_pair(gwp.GetHash(), gwp));

    CGatewayBroadcast gwb(*pgw);
    uint256 hash = gwb.GetHash();
    if(mapSeenGatewayBroadcast.count(hash)) {
        mapSeenGatewayBroadcast[hash].second.lastPing = gwp;
    }
}

void CGatewayMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    nCachedBlockHeight = pindex->nHeight;
    LogPrint("gateway", "CGatewayMan::UpdatedBlockTip -- nCachedBlockHeight=%d\n", nCachedBlockHeight);

    CheckSameAddr();

    if(fGateWay) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid(pindex);
    }
}

