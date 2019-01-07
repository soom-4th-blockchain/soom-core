// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef GATEWAY_PAYMENTS_H
#define GATEWAY_PAYMENTS_H

#include "util.h"
#include "core_io.h"
#include "key.h"
#include "gateway.h"
#include "net_processing.h"
#include "utilstrencodings.h"

class CGatewayPayments;
class CGatewayPaymentVote;
class CGatewayBlockPayees;

static const int GWPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int GWPAYMENTS_SIGNATURES_TOTAL            = 10;

//! minimum peer version that can receive and send gateway payment messages,
//  vote for gateway and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_GATEWAY_PAYMENT_PROTO_VERSION_1 = 70206;
static const int MIN_GATEWAY_PAYMENT_PROTO_VERSION_2 = 70210;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapGatewayBlocks;
extern CCriticalSection cs_mapGatewayPayeeVotes;

extern CGatewayPayments gwpayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGatewayRet, CTxOut& txoutFoundationRet);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsFoundationTxValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward);

class CGatewayPayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;

public:
    CGatewayPayee() :
        scriptPubKey(),
        vecVoteHashes()
        {}

    CGatewayPayee(CScript payee, uint256 hashIn) :
        scriptPubKey(payee),
        vecVoteHashes()
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() const { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() const { return vecVoteHashes; }
    int GetVoteCount() const { return vecVoteHashes.size(); }
};

// Keep track of votes for payees from gateways
class CGatewayBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CGatewayPayee> vecPayees;

    CGatewayBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CGatewayBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CGatewayPaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet) const;
    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq) const;

    bool IsTransactionValid(const CTransaction& txNew) const;

    std::string GetRequiredPaymentsString() const;
};

// vote for the winning payment
class CGatewayPaymentVote
{
public:
    COutPoint gatewayOutpoint;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CGatewayPaymentVote() :
        gatewayOutpoint(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CGatewayPaymentVote(COutPoint outpoint, int nBlockHeight, CScript payee) :
        gatewayOutpoint(outpoint),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (nVersion == 70209 && (s.GetType() & SER_NETWORK)) {
            // converting from/to old format
            CTxIn txin{};
            if (ser_action.ForRead()) {
                READWRITE(txin);
                gatewayOutpoint = txin.prevout;
            } else {
                txin = CTxIn(gatewayOutpoint);
                READWRITE(txin);
            }
        } else {
            // using new format directly
            READWRITE(gatewayOutpoint);
        }
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(vchSig);
        }
    }

    uint256 GetHash() const;
    uint256 GetSignatureHash() const;

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyGateway, int nValidationHeight, int &nDos) const;

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman) const;
    void Relay(CConnman& connman) const;

    bool IsVerified() const { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Gateway Payments Class
// Keeps track of who should get paid for which blocks
//

class CGatewayPayments
{
private:
    // gateway count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block height
    int nCachedBlockHeight;

public:
    std::map<uint256, CGatewayPaymentVote> mapGatewayPaymentVotes;
    std::map<int, CGatewayBlockPayees> mapGatewayBlocks;
    std::map<COutPoint, int> mapGatewaysLastVote;
    std::map<COutPoint, int> mapGatewaysDidNotVote;

    CGatewayPayments() : nStorageCoeff(1.25), nMinBlocksToStore(6000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(mapGatewayPaymentVotes);
        READWRITE(mapGatewayBlocks);
    }

    void Clear();

    bool AddOrUpdatePaymentVote(const CGatewayPaymentVote& vote);
    bool HasVerifiedPaymentVote(const uint256& hashIn) const;
    bool ProcessBlock(int nBlockHeight, CConnman& connman);
    void CheckBlockVotes(int nBlockHeight);

    void Sync(CNode* node, CConnman& connman) const;
    void RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman) const;
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payeeRet) const;
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const;
    bool IsScheduled(const gateway_info_t& gwInfo, int nNotBlockHeight) const;

    bool UpdateLastVote(const CGatewayPaymentVote& vote);

    int GetMinGatewayPaymentsProto() const;
    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);
    std::string GetRequiredPaymentsString(int nBlockHeight) const;
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGatewayRet, CTxOut& txoutFoundationRet) const;
    std::string ToString() const;

    int GetBlockCount() const { return mapGatewayBlocks.size(); }
    int GetVoteCount() const { return mapGatewayPaymentVotes.size(); }

    bool IsEnoughData() const;
    int GetStorageLimit() const;

    void UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman);
};

#endif
