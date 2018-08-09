Protocol Documentation - 1.0.0
=====================================

This document describes the protocol extensions for all additional functionality build into the Soom protocol. This doesn't include any of the Bitcoin protocol, which has been left intact in the Soom project. For more information about the core protocol, please see https://en.bitcoin.it/w/index.php?title#Protocol_documentation&action#edit

## Common Structures

### Simple types

uint256  => char[32]

CScript => uchar[]

### COutPoint

Bitcoin Outpoint https://bitcoin.org/en/glossary/outpoint

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 32 | hash | uint256 | Hash of transactional output which is being referenced
| 4 | n | uint32_t | Index of transaction which is being referenced


### CTxIn

Bitcoin Input https://bitcoin.org/en/glossary/input

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 36 | prevout | [COutPoint](#coutpoint) | The previous output from an existing transaction, in the form of an unspent output
| 1+ | script length | var_int | The length of the signature script
| ? | script | CScript | The script which is validated for this input to be spent
| 4 | nSequence | uint_32t | Transaction version as defined by the sender. Intended for "replacement" of transactions when information is updated before inclusion into a block.

### CTxOut

Bitcoin Output https://bitcoin.org/en/glossary/output

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 8 | nValue | int64_t | Transfered value
| ? | scriptPubKey | CScript | The script for indicating what conditions must be fulfilled for this output to be further spent

### CTransaction

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nVersion | int32_t | Transaction data format version
| 1+ | tx_in count | var_int | Number of Transaction inputs
| 41+ | vin | [CTxIn](#ctxin) | A list of 1 or more transaction inputs
| 1+ | tx_out count | var_int | Number of Transaction outputs
| 9+ | vout | [CTxOut](#ctxout) | A list of 1 or more transaction outputs
| 4 | nLockTime | uint32_t | The block number or timestamp at which this transaction is unlocked

### CPubKey

Bitcoin Public Key https://bitcoin.org/en/glossary/public-key

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 33-65 | vch | char[] | The public portion of a keypair which can be used to verify signatures made with the private portion of the keypair.

### CService

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 16 | IP | CNetAddr | IP Address
| 2 | Port | uint16 | IP Port

## Message Types

### GWANNOUNCE - "gwb"

CGatewayBroadcast

Whenever a gateway comes online or a client is syncing, they will send this message which describes the gateway entry and how to validate messages from it.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 41 | vin | [CTxIn](#ctxin) | The unspent output which is holding 5000 SOOM
| # | addr | [CService](#cservice) | IPv4 address of the gateway
| 33-65 | pubKeyCollateralAddress | [CPubKey](#cpubkey) | CPubKey of the main 5000 SOOM unspent output
| 33-65 | pubKeyGateway | [CPubKey](#cpubkey) | CPubKey of the secondary signing key (For all other messaging other than announce message)
| 71-73 | sig | char[] | Signature of this message (verifiable via pubKeyCollateralAddress)
| 8 | sigTime | int64_t | Time which the signature was created
| 4 | nProtocolVersion | int | The protocol version of the gateway
| # | lastPing | CGatewayPing | The last known ping of the gateway

### GWPING - "gwp"

CGatewayPing

Every few minutes, gateways ping the network with a message that propagates the whole network.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 41 | vin | [CTxIn](#ctxin) | The unspent output of the gateway which is signing the message
| 32 | blockHash | uint256 | Current chaintip blockhash minus 12
| 8 | sigTime | int64_t | Signature time for this ping
| 71-73 | vchSig | char[] | Signature of this message by gateway (verifiable via pubKeyGateway)

### GATEWAYPAYMENTVOTE - "gww"

CGatewayPaymentVote

When a new block is found on the network, a gateway quorum will be determined and those 10 selected gateways will issue a gateway payment vote message to pick the next winning node.

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 41 | vinGateway | [CTxIn](#ctxin) | The unspent output of the gateway which is signing the message
| 4 | nBlockHeight | int | The blockheight which the payee should be paid
| ? | payeeAddress | CScript | The address to pay to
| 71-73 | sig | char[] | Signature of the gateway which is signing the message


### TXLOCKREQUEST - "ix"

CTxLockRequest

Transaction Lock Request, serialization is the same as for [CTransaction](#ctransaction).

### TXLOCKVOTE - "txlvote"

CTxLockVote

Transaction Lock Vote

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 32 | txHash | uint256 | txid of the transaction to lock
| 36 | outpoint | [COutPoint](#coutpoint) | The utxo to lock in this transaction
| 36 | outpointGateway | [COutPoint](#coutpoint) | The utxo of the gateway which is signing the vote
| 71-73 | vchGatewaySignature | char[] | Signature of this message by gateway (verifiable via pubKeyGateway)


### SPORK - "spork"

Spork

Spork

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nSporkID | int | 
| 8 | nValue | int64_t | 
| 8 | nTimeSigned | int64_t | 
| 66* | vchSig | char[] | Unclear if 66 is the correct size, but this is what it appears to be in most cases

#### Defined Sporks (per src/sporks.h)
 
| Spork ID | Number | Name | Description | 
| ---------- | ---------- | ----------- | ----------- |
| 10001 | 2 | INSTANTSEND_ENABLED | Turns on and off InstantSend network wide
| 10002 | 3 | INSTANTSEND_BLOCK_FILTERING | Turns on and off InstantSend block filtering
| 10004 | 5 | INSTANTSEND_MAX_VALUE | Controls the max value for an InstantSend transaction (currently 2000 soom)
| 10007 | 8 | GATEWAY_PAYMENT_ENFORCEMENT | Requires gateways to be paid by miners when blocks are processed
| 10009 | 10 | GATEWAY_PAY_UPDATED_NODES | Only current protocol version gateway's will be paid (not older nodes)
| 10011 | 12 | RECONSIDER_BLOCKS |

## Undocumented messages

### GATEWAYPAYMENTBLOCK - "gwwb"

Gateway Payment Block

*NOTE: Per src/protocol.cpp, there is no message for this (only inventory)*

### GWVERIFY - "gwv"

Gateway Verify

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 41 | vin1 | [CTxIn](#ctxin) | The unspent output which is holding 5000 SOOM for gateway 1
| 41 | vin2 | [CTxIn](#ctxin) | The unspent output which is holding 5000 SOOM for gateway 2
| # | addr | [CService](#cservice) | IPv4 address / port of the gateway
| 4 | nonce | int | Nonce
| 4 | nBlockHeight | int | The blockheight
| 66* | vchSig1 | char[] | Signature of by gateway 1 (unclear if 66 is the correct size, but this is what it appears to be in most cases)
| 66* | vchSig2 | char[] | Signature of by gateway 2 (unclear if 66 is the correct size, but this is what it appears to be in most cases)

### GWEG - "gweg"

Gateway List/Entry Sync

Get Gateway list or specific entry

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 41 | vin | [CTxIn](#ctxin) | The unspent output which is holding 5000 SOOM

### SYNCSTATUSCOUNT - "ssc"

Sync Status Count

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nItemID | int | Gateway Sync Item ID
| 4 | nCount | int | Gateway Sync Count

#### Defined Sync Item IDs (per src/gateway-sync.h)

| Item ID | Name | Description |
| ---------- | ---------- | ----------- |
| 2 | GATEWAY_SYNC_LIST |
| 3 | GATEWAY_SYNC_GWW |

### GATEWAYPAYMENTSYNC - "gwget"

Gateway Payment Sync

| Field Size | Field Name | Data type | Description |
| ---------- | ----------- | --------- | -------- |
| 4 | nGwCount | int |

