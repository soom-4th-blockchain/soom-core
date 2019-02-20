// Copyright (c) 2018 The Dash Core developers
// Copyright (c) 2019 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SOOM_DETERMINISTICGWS_H
#define SOOM_DETERMINISTICGWS_H

#include "arith_uint256.h"
#include "bls/bls.h"
#include "dbwrapper.h"
#include "evodb.h"
#include "providertx.h"
#include "simplifiedgws.h"
#include "sync.h"

#include "immer/map.hpp"
#include "immer/map_transient.hpp"

#include <map>

class CBlock;
class CBlockIndex;
class CValidationState;

namespace llgq
{
    class CFinalCommitment;
}

class CDeterministicGWState
{
public:
    int nRegisteredHeight{-1};
    int nLastPaidHeight{0};
    int nPoSePenalty{0};
    int nPoSeRevivedHeight{-1};
    int nPoSeBanHeight{-1};
    uint16_t nRevocationReason{CProUpRevTx::REASON_NOT_SPECIFIED};

    // the block hash X blocks after registration, used in quorum calculations
    uint256 confirmedHash;
    // sha256(proTxHash, confirmedHash) to speed up quorum calculations
    // please note that this is NOT a double-sha256 hash
    uint256 confirmedHashWithProRegTxHash;

    CKeyID keyIDOwner;
    CBLSPublicKey pubKeyOperator;
    CKeyID keyIDVoting;
    CService addr;
    CScript scriptPayout;
    CScript scriptOperatorPayout;

public:
    CDeterministicGWState() {}
    CDeterministicGWState(const CProRegTx& proTx)
    {
        keyIDOwner = proTx.keyIDOwner;
        pubKeyOperator = proTx.pubKeyOperator;
        keyIDVoting = proTx.keyIDVoting;
        addr = proTx.addr;
        scriptPayout = proTx.scriptPayout;
    }
    template <typename Stream>
    CDeterministicGWState(deserialize_type, Stream& s)
    {
        s >> *this;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nRegisteredHeight);
        READWRITE(nLastPaidHeight);
        READWRITE(nPoSePenalty);
        READWRITE(nPoSeRevivedHeight);
        READWRITE(nPoSeBanHeight);
        READWRITE(nRevocationReason);
        READWRITE(confirmedHash);
        READWRITE(confirmedHashWithProRegTxHash);
        READWRITE(keyIDOwner);
        READWRITE(pubKeyOperator);
        READWRITE(keyIDVoting);
        READWRITE(addr);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(*(CScriptBase*)(&scriptOperatorPayout));
    }

    void ResetOperatorFields()
    {
        pubKeyOperator = CBLSPublicKey();
        addr = CService();
        scriptOperatorPayout = CScript();
        nRevocationReason = CProUpRevTx::REASON_NOT_SPECIFIED;
    }
    void BanIfNotBanned(int height)
    {
        if (nPoSeBanHeight == -1) {
            nPoSeBanHeight = height;
        }
    }
    void UpdateConfirmedHash(const uint256& _proTxHash, const uint256& _confirmedHash)
    {
        confirmedHash = _confirmedHash;
        CSHA256 h;
        h.Write(_proTxHash.begin(), _proTxHash.size());
        h.Write(_confirmedHash.begin(), _confirmedHash.size());
        h.Finalize(confirmedHashWithProRegTxHash.begin());
    }

    bool operator==(const CDeterministicGWState& rhs) const
    {
        return nRegisteredHeight == rhs.nRegisteredHeight &&
               nLastPaidHeight == rhs.nLastPaidHeight &&
               nPoSePenalty == rhs.nPoSePenalty &&
               nPoSeRevivedHeight == rhs.nPoSeRevivedHeight &&
               nPoSeBanHeight == rhs.nPoSeBanHeight &&
               nRevocationReason == rhs.nRevocationReason &&
               confirmedHash == rhs.confirmedHash &&
               confirmedHashWithProRegTxHash == rhs.confirmedHashWithProRegTxHash &&
               keyIDOwner == rhs.keyIDOwner &&
               pubKeyOperator == rhs.pubKeyOperator &&
               keyIDVoting == rhs.keyIDVoting &&
               addr == rhs.addr &&
               scriptPayout == rhs.scriptPayout &&
               scriptOperatorPayout == rhs.scriptOperatorPayout;
    }

    bool operator!=(const CDeterministicGWState& rhs) const
    {
        return !(rhs == *this);
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};
typedef std::shared_ptr<CDeterministicGWState> CDeterministicGWStatePtr;
typedef std::shared_ptr<const CDeterministicGWState> CDeterministicGWStateCPtr;

class CDeterministicGW
{
public:
    CDeterministicGW() {}
    template <typename Stream>
    CDeterministicGW(deserialize_type, Stream& s)
    {
        s >> *this;
    }

    uint256 proTxHash;
    COutPoint collateralOutpoint;
    uint16_t nOperatorReward;
    CDeterministicGWStateCPtr pdgwState;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(proTxHash);
        READWRITE(collateralOutpoint);
        READWRITE(nOperatorReward);
        READWRITE(pdgwState);
    }

public:
    std::string ToString() const;
    void ToJson(UniValue& obj) const;
};
typedef std::shared_ptr<CDeterministicGW> CDeterministicGWPtr;
typedef std::shared_ptr<const CDeterministicGW> CDeterministicGWCPtr;

class CDeterministicGWListDiff;

template <typename Stream, typename K, typename T, typename Hash, typename Equal>
void SerializeImmerMap(Stream& os, const immer::map<K, T, Hash, Equal>& m)
{
    WriteCompactSize(os, m.size());
    for (typename immer::map<K, T, Hash, Equal>::const_iterator mi = m.begin(); mi != m.end(); ++mi)
        Serialize(os, (*mi));
}

template <typename Stream, typename K, typename T, typename Hash, typename Equal>
void UnserializeImmerMap(Stream& is, immer::map<K, T, Hash, Equal>& m)
{
    m = immer::map<K, T, Hash, Equal>();
    unsigned int nSize = ReadCompactSize(is);
    for (unsigned int i = 0; i < nSize; i++) {
        std::pair<K, T> item;
        Unserialize(is, item);
        m = m.set(item.first, item.second);
    }
}

class CDeterministicGWList
{
public:
    typedef immer::map<uint256, CDeterministicGWCPtr> GwMap;
    typedef immer::map<uint256, std::pair<uint256, uint32_t> > GwUniquePropertyMap;

private:
    uint256 blockHash;
    int nHeight{-1};
    GwMap gwMap;

    // map of unique properties like address and keys
    // we keep track of this as checking for duplicates would otherwise be painfully slow
    // the entries in the map are ref counted as some properties might appear multiple times per GW (e.g. operator/owner keys)
    GwUniquePropertyMap gwUniquePropertyMap;

public:
    CDeterministicGWList() {}
    explicit CDeterministicGWList(const uint256& _blockHash, int _height) :
        blockHash(_blockHash),
        nHeight(_height)
    {
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(blockHash);
        READWRITE(nHeight);
        if (ser_action.ForRead()) {
            UnserializeImmerMap(s, gwMap);
            UnserializeImmerMap(s, gwUniquePropertyMap);
        } else {
            SerializeImmerMap(s, gwMap);
            SerializeImmerMap(s, gwUniquePropertyMap);
        }
    }

public:
    size_t GetAllGWsCount() const
    {
        return gwMap.size();
    }

    size_t GetValidGWsCount() const
    {
        size_t count = 0;
        for (const auto& p : gwMap) {
            if (IsGWValid(p.second)) {
                count++;
            }
        }
        return count;
    }

    template <typename Callback>
    void ForEachGW(bool onlyValid, Callback&& cb) const
    {
        for (const auto& p : gwMap) {
            if (!onlyValid || IsGWValid(p.second)) {
                cb(p.second);
            }
        }
    }

public:
    const uint256& GetBlockHash() const
    {
        return blockHash;
    }
    void SetBlockHash(const uint256& _blockHash)
    {
        blockHash = _blockHash;
    }
    int GetHeight() const
    {
        return nHeight;
    }
    void SetHeight(int _height)
    {
        nHeight = _height;
    }

    bool IsGWValid(const uint256& proTxHash) const;
    bool IsGWPoSeBanned(const uint256& proTxHash) const;
    bool IsGWValid(const CDeterministicGWCPtr& dgw) const;
    bool IsGWPoSeBanned(const CDeterministicGWCPtr& dgw) const;

    bool HasGW(const uint256& proTxHash) const
    {
        return GetGW(proTxHash) != nullptr;
    }
    CDeterministicGWCPtr GetGW(const uint256& proTxHash) const;
    CDeterministicGWCPtr GetValidGW(const uint256& proTxHash) const;
    CDeterministicGWCPtr GetGWByOperatorKey(const CBLSPublicKey& pubKey);
    CDeterministicGWCPtr GetGWByCollateral(const COutPoint& collateralOutpoint) const;
    CDeterministicGWCPtr GetGWPayee() const;

    /**
     * Calculates the projected GW payees for the next *count* blocks. The result is not guaranteed to be correct
     * as PoSe banning might occur later
     * @param count
     * @return
     */
    std::vector<CDeterministicGWCPtr> GetProjectedGWPayees(int nCount) const;

    /**
     * Calculate a quorum based on the modifier. The resulting list is deterministically sorted by score
     * @param maxSize
     * @param modifier
     * @return
     */
    std::vector<CDeterministicGWCPtr> CalculateQuorum(size_t maxSize, const uint256& modifier) const;
    std::vector<std::pair<arith_uint256, CDeterministicGWCPtr>> CalculateScores(const uint256& modifier) const;

    /**
     * Calculates the maximum penalty which is allowed at the height of this GW list. It is dynamic and might change
     * for every block.
     * @return
     */
    int CalcMaxPoSePenalty() const;

    /**
     * Returns a the given percentage from the max penalty for this GW list. Always use this method to calculate the
     * value later passed to PoSePunish. The percentage should be high enough to take per-block penalty decreasing for GWs
     * into account. This means, if you want to accept 2 failures per payment cycle, you should choose a percentage that
     * is higher then 50%, e.g. 66%.
     * @param percent
     * @return
     */
    int CalcPenalty(int percent) const;

    /**
     * Punishes a GW for misbehavior. If the resulting penalty score of the GW reaches the max penalty, it is banned.
     * Penalty scores are only increased when the GW is not already banned, which means that after banning the penalty
     * might appear lower then the current max penalty, while the GW is still banned.
     * @param proTxHash
     * @param penalty
     */
    void PoSePunish(const uint256& proTxHash, int penalty, bool debugLogs);

    /**
     * Decrease penalty score of GW by 1.
     * Only allowed on non-banned GWs.
     * @param proTxHash
     */
    void PoSeDecrease(const uint256& proTxHash);

    CDeterministicGWListDiff BuildDiff(const CDeterministicGWList& to) const;
    CSimplifiedGWListDiff BuildSimplifiedDiff(const CDeterministicGWList& to) const;
    CDeterministicGWList ApplyDiff(const CDeterministicGWListDiff& diff) const;

    void AddGW(const CDeterministicGWCPtr& dgw);
    void UpdateGW(const uint256& proTxHash, const CDeterministicGWStateCPtr& pdgwState);
    void RemoveGW(const uint256& proTxHash);

    template <typename T>
    bool HasUniqueProperty(const T& v) const
    {
        return gwUniquePropertyMap.count(::SerializeHash(v)) != 0;
    }
    template <typename T>
    CDeterministicGWCPtr GetUniquePropertyGW(const T& v) const
    {
        auto p = gwUniquePropertyMap.find(::SerializeHash(v));
        if (!p) {
            return nullptr;
        }
        return GetGW(p->first);
    }

private:
    template <typename T>
    void AddUniqueProperty(const CDeterministicGWCPtr& dgw, const T& v)
    {
        static const T nullValue;
        assert(v != nullValue);

        auto hash = ::SerializeHash(v);
        auto oldEntry = gwUniquePropertyMap.find(hash);
        assert(!oldEntry || oldEntry->first == dgw->proTxHash);
        std::pair<uint256, uint32_t> newEntry(dgw->proTxHash, 1);
        if (oldEntry) {
            newEntry.second = oldEntry->second + 1;
        }
        gwUniquePropertyMap = gwUniquePropertyMap.set(hash, newEntry);
    }
    template <typename T>
    void DeleteUniqueProperty(const CDeterministicGWCPtr& dgw, const T& oldValue)
    {
        static const T nullValue;
        assert(oldValue != nullValue);

        auto oldHash = ::SerializeHash(oldValue);
        auto p = gwUniquePropertyMap.find(oldHash);
        assert(p && p->first == dgw->proTxHash);
        if (p->second == 1) {
            gwUniquePropertyMap = gwUniquePropertyMap.erase(oldHash);
        } else {
            gwUniquePropertyMap = gwUniquePropertyMap.set(oldHash, std::make_pair(dgw->proTxHash, p->second - 1));
        }
    }
    template <typename T>
    void UpdateUniqueProperty(const CDeterministicGWCPtr& dgw, const T& oldValue, const T& newValue)
    {
        if (oldValue == newValue) {
            return;
        }
        static const T nullValue;

        if (oldValue != nullValue) {
            DeleteUniqueProperty(dgw, oldValue);
        }

        if (newValue != nullValue) {
            AddUniqueProperty(dgw, newValue);
        }
    }
};

class CDeterministicGWListDiff
{
public:
    uint256 prevBlockHash;
    uint256 blockHash;
    int nHeight{-1};
    std::map<uint256, CDeterministicGWCPtr> addedGWs;
    std::map<uint256, CDeterministicGWStateCPtr> updatedGWs;
    std::set<uint256> removedGws;

public:
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(prevBlockHash);
        READWRITE(blockHash);
        READWRITE(nHeight);
        READWRITE(addedGWs);
        READWRITE(updatedGWs);
        READWRITE(removedGws);
    }

public:
    bool HasChanges() const
    {
        return !addedGWs.empty() || !updatedGWs.empty() || !removedGws.empty();
    }
};

class CDeterministicGWManager
{
    static const int SNAPSHOT_LIST_PERIOD = 576; // once per day
    static const int LISTS_CACHE_SIZE = 576;

public:
    CCriticalSection cs;

private:
    CEvoDB& evoDb;

    std::map<uint256, CDeterministicGWList> gwListsCache;
    int tipHeight{-1};
    uint256 tipBlockHash;

public:
    CDeterministicGWManager(CEvoDB& _evoDb);

    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex);

    void UpdatedBlockTip(const CBlockIndex* pindex);

    // the returned list will not contain the correct block hash (we can't know it yet as the coinbase TX is not updated yet)
    bool BuildNewListFromBlock(const CBlock& block, const CBlockIndex* pindexPrev, CValidationState& state, CDeterministicGWList& gwListRet, bool debugLogs);
    void HandleQuorumCommitment(llgq::CFinalCommitment& qc, CDeterministicGWList& gwList, bool debugLogs);
    void DecreasePoSePenalties(CDeterministicGWList& gwList);

    CDeterministicGWList GetListForBlock(const uint256& blockHash);
    CDeterministicGWList GetListAtChainTip();

    // TODO remove after removal of old non-deterministic lists
    bool HasValidGWCollateralAtChainTip(const COutPoint& outpoint);
    bool HasGWCollateralAtChainTip(const COutPoint& outpoint);

    // Test if given TX is a ProRegTx which also contains the collateral at index n
    bool IsProTxWithCollateral(const CTransactionRef& tx, uint32_t n);

    bool IsDeterministicGWsSporkActive(int nHeight = -1);

private:
    int64_t GetSpork15Value();
    void CleanupCache(int nHeight);
};

extern CDeterministicGWManager* deterministicGWManager;

#endif //SOOM_DETERMINISTICGWS_H
