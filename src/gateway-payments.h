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
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() { return vecVoteHashes.size(); }
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
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CGatewayPaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet);
    bool HasPayeeWithVotes(const CScript& payeeIn, int nVotesReq);

    bool IsTransactionValid(const CTransaction& txNew);

    std::string GetRequiredPaymentsString();
};

// vote for the winning payment
class CGatewayPaymentVote
{
public:
    CTxIn vinGateway;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CGatewayPaymentVote() :
        vinGateway(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CGatewayPaymentVote(COutPoint outpointGateway, int nBlockHeight, CScript payee) :
        vinGateway(outpointGateway),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinGateway);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinGateway.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyGateway, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError, CConnman& connman);
    void Relay(CConnman& connman);

    bool IsVerified() { return !vchSig.empty(); }
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

    CGatewayPayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapGatewayPaymentVotes);
        READWRITE(mapGatewayBlocks);
    }

    void Clear();

    bool AddPaymentVote(const CGatewayPaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
    bool ProcessBlock(int nBlockHeight, CConnman& connman);
    void CheckPreviousBlockVotes(int nPrevBlockHeight);

    void Sync(CNode* node, CConnman& connman);
    void RequestLowDataPaymentBlocks(CNode* pnode, CConnman& connman);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CGateway& gw, int nNotBlockHeight);

    bool CanVote(COutPoint outGateway, int nBlockHeight);

    int GetMinGatewayPaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutGatewayRet, CTxOut& txoutFoundationRet);
    std::string ToString() const;

    int GetBlockCount() { return mapGatewayBlocks.size(); }
    int GetVoteCount() { return mapGatewayPaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();

    void UpdatedBlockTip(const CBlockIndex *pindex, CConnman& connman);
};

#endif
