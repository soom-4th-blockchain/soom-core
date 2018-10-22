// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"
//#/ start hcdo add include for MineGenesis 180420
#include "arith_uint256.h"
//@/ end hcdo add include for MineGenesis

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=00000ffd590b14, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=e0028e, nTime=1390095618, nBits=1e0ffff0, nNonce=28917698, vtx=1)
 *   CTransaction(hash=e0028e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d01044c5957697265642030392f4a616e2f3230313420546865204772616e64204578706572696d656e7420476f6573204c6976653a204f76657273746f636b2e636f6d204973204e6f7720416363657074696e6720426974636f696e73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0xA9037BAC7050C479B121CF)
 *   vMerkleTree: e0028e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "On 17/Jul/2018 Larry Fink confirmed to Reuters that BlackRock have a bitcoin working group ";
    const CScript genesisOutputScript = CScript() << ParseHex("04195a6bd56beda45d2a2cd5bc0ca329cb05362664a6ad81f3ce683db09596e1f58f33e7da0e265b4f569179fe6eb1d5ec9dc246514e911f5d7c917fb9a31eb30e") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 * + Contains no strange transactions
 */


class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 525600; // Note: actual number of blocks per calendar year with DGW v3 is ~200700 (for example 449750 - 249050)
        consensus.nGatewayPaymentsStartBlock = 100000; // not true, but it's ok as long as it's less then nGatewayPaymentsIncreaseBlock
        consensus.nGatewayPaymentsIncreaseBlock = 158000; // actual historical value
        consensus.nGatewayPaymentsIncreasePeriod = 576*30; // 17280 - actual historical value
        consensus.nInstantSendKeepLock = 24;
        consensus.nGatewayMinimumConfirmations = 15;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x000000005bf291276cd33777ea78dde7b5303cea5aba4af0345469fc5f9684f5");
		consensus.BIP65Height = 1; 
        consensus.BIP66Height = 1; 
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Soom: 1 day
        consensus.nPowTargetSpacing =  60; // Soom: 1 minutes
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 100;
        consensus.nPowDGWHeight = 100;
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
     	consensus.nFoundationPaymentsStartBlock = 101;
	 
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1462060800; // May 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017


	 
        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");
       

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xc3;
        pchMessageStart[1] = 0x0f;
        pchMessageStart[2] = 0x6f;
        pchMessageStart[3] = 0xc1;

        vAlertPubKey = ParseHex("046469bb798dd1976883e84ded53b492af512e201102bc0e7a1ae5198df03df9919f8cdff2af92ec2a9fb3779e86e92df0c5e53e4b191cc768d0745b0442c77ac0");
        nDefaultPort = 16099;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDelayGetHeadersTime = 24 * 60 * 60;
        nPruneAfterHeight = 100000;

        genesis = CreateGenesisBlock(1533447343, 8362638, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();		
		
        assert(consensus.hashGenesisBlock == uint256S("0x0000044a4be97131d4d115f8db2307ab17c39239b9db2d87f3b2921afab7b001"));
        assert(genesis.hashMerkleRoot == uint256S("0x47eec0209382963a4f867534b242cd795688b38471f3b85236de04f4e77d19f4"));

//#/ start jhhong add dns seed 180731
		vSeeds.push_back(CDNSSeedData("soompay.org", "seed11.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "seed12.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "seed13.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "seed14.soompay.org"));
	 	vSeeds.push_back(CDNSSeedData("soompay.org", "seed15.soompay.org"));
		vSeeds.push_back(CDNSSeedData("soompay.org", "seed16.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "seed17.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "seed18.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "seed19.soompay.org"));
	 	vSeeds.push_back(CDNSSeedData("soompay.org", "seed20.soompay.org"));
//@/ end jhhong add dns seed
//#/ start jhhong modify prefix 180803
        // Soom addresses start with 'M'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,50);
        // Soom script addresses start with '7'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,16);
        // Soom private keys start with '7' or 'S'
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,170);
//@/ end jhhong modify prefix
        // Soom BIP32 pubkeys start with 'xpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        // Soom BIP32 prvkeys start with 'xprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();

        // Soom BIP44 coin type is '250'
        nExtCoinType = 250;

//#/ start jhhong add fixed-seed 180731
		vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));
		//vFixedSeeds.clear(); //doesn't have any fixed seeds. 
//@/ end jhhong add fixed-seed
//#/ start jhhong add dns seed 180731
		//vSeeds.clear();  // doesn't have any DNS seeds. 
//@/ end jhhong add dns seed
        
        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = false;

        nFulfilledRequestExpireTime = 60*60; // fulfilled requests expire in 1 hour
        strSporkPubKey = "04a3d66fd1d48e1857d7fa2c1c1af0a1310c05f10efbd33b7e4afb44496c7a6a8473771f377e85c1dadf12c74bec9ff094617bd4f737b8b4d9764fc002caca6fd8";
		strFoundationAddress = "M8iNkrKZ1deRmzHc6wrqyRKhamEsKpQgcd";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (  2000, uint256S("0x000000000002cc28d7e340b9e6253d23dbf7fa811817833f1242987797248469"))
            (  4000, uint256S("0x000000000000de97cda98fc8329329c2c5da6f496aaad45b841c8f17355f9884"))
            (  8000, uint256S("0x0000000000000dc94a4a8917eba2865d3c52961e2fc0e94f56c98f70fde1b2f0")),
            1533447343, // * UNIX timestamp of last checkpoint block
            0,    // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0        // * estimated number of transactions per day after checkpoint
        };
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 525600;
        consensus.nGatewayPaymentsStartBlock = 4010; // not true, but it's ok as long as it's less then nGatewayPaymentsIncreaseBlock
        consensus.nGatewayPaymentsIncreaseBlock = 4030;
        consensus.nGatewayPaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nGatewayMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 51;
        consensus.nMajorityRejectBlockOutdated = 75;
        consensus.nMajorityWindow = 100;
        consensus.BIP34Height = 1;
        consensus.BIP34Hash = uint256S("0x000004ca73d9b8b2a7e7aa9b5799fb788550f2a1fa07cf1c470926af15c7b0ba");
		consensus.BIP65Height = 1; 
        consensus.BIP66Height = 1; 
        consensus.powLimit = uint256S("00000fffff000000000000000000000000000000000000000000000000000000");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Soom: 1 day
        consensus.nPowTargetSpacing =  60; // Soom: 1 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = false;
        consensus.nPowKGWHeight = 4001; // nPowKGWHeight >= nPowDGWHeight means "no KGW"
        consensus.nPowDGWHeight = 4001;
        consensus.nRuleChangeActivationThreshold = 1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 2016; // nPowTargetTimespan / nPowTargetSpacing
     	consensus.nFoundationPaymentsStartBlock = 101;

        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // Deployment of BIP68, BIP112, and BIP113.
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 1456790400; // March 1st, 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 1493596800; // May 1st, 2017



        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00"); 

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00"); 

        pchMessageStart[0] = 0xde;
        pchMessageStart[1] = 0xf2;
        pchMessageStart[2] = 0xda;
        pchMessageStart[3] = 0xcf;
        vAlertPubKey = ParseHex("04517d8a699cb43d3938d7b24faaff7cda448ca4ea267723ba614784de661949bf632d6304316b244646dea079735b9a6fc4af804efb4752075b9fe2245e14e412");
        nDefaultPort = 16999;
        nMaxTipAge = 0x7fffffff; // allow mining on top of old blocks for testnet
        nDelayGetHeadersTime = 24 * 60 * 60;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1533447343, 4430630, 0x1e0ffff0, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
		
        assert(consensus.hashGenesisBlock == uint256S("0x0000059f3300f9caa0d4c8697abf1281c25099d9bc0fc1a8e996c5b714a49c49"));
        assert(genesis.hashMerkleRoot == uint256S("0x47eec0209382963a4f867534b242cd795688b38471f3b85236de04f4e77d19f4"));

        // Testnet Soom addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Testnet Soom script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Testnet Soom BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Testnet Soom BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        // Testnet Soom BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;
//#/ start jhhong add dns seed in test-net 180803
		//vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));
        vFixedSeeds.clear();
        //vSeeds.clear();
        vSeeds.push_back(CDNSSeedData("soompay.org",  "test01.soompay.org"));
        vSeeds.push_back(CDNSSeedData("soompay.org", "test02.soompay.org"));
//@/ end jhhong add dns seed in test-net

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes
        strSporkPubKey = "0485bfd33b09d2b7e416216d82554c7872cc269559af49be1ae22fc2f31dbe19d6094066c8f519cc9f73abf4f1bd4d0eec09e83252696c4f5492468e4a5ce9df23";
		strFoundationAddress = "yQJVxpZiMRe5GAj6f9mtnxoPhifiHgwXLm";

        checkpointData = (CCheckpointData) {
            boost::assign::map_list_of
            (      0, uint256S("0x0000059f3300f9caa0d4c8697abf1281c25099d9bc0fc1a8e996c5b714a49c49")),
            1533447343, // * UNIX timestamp of last checkpoint block
            0,       // * total number of transactions between genesis and last checkpoint
                        //   (the tx=... number in the SetBestChain debug.log lines)
            0         // * estimated number of transactions per day after checkpoint
        };

    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
        consensus.nSubsidyHalvingInterval = 150;
        consensus.nGatewayPaymentsStartBlock = 240;
        consensus.nGatewayPaymentsIncreaseBlock = 350;
        consensus.nGatewayPaymentsIncreasePeriod = 10;
        consensus.nInstantSendKeepLock = 6;
        consensus.nGatewayMinimumConfirmations = 1;
        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.BIP34Height = -1; // BIP34 has not necessarily activated on regtest
        consensus.BIP34Hash = uint256();
		consensus.BIP65Height = 1; 
		consensus.BIP66Height = 1; 
		consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 24 * 60 * 60; // Soom: 1 day
        consensus.nPowTargetSpacing = 60; // Soom: 2.5 minutes
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nPowKGWHeight = 100; // same as mainnet
        consensus.nPowDGWHeight = 100; // same as mainnet
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
     	consensus.nFoundationPaymentsStartBlock = 101;
		
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].bit = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_CSV].nTimeout = 999999999999ULL;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0xdc;
        pchMessageStart[1] = 0xd1;
        pchMessageStart[2] = 0xc7;
        pchMessageStart[3] = 0xec;
        nMaxTipAge = 6 * 60 * 60; // ~144 blocks behind -> 2 x fork detection time, was 24 * 60 * 60 in bitcoin
        nDelayGetHeadersTime = 0; // never delay GETHEADERS in regtests
        nDefaultPort = 16994;
        nPruneAfterHeight = 1000;

        genesis = CreateGenesisBlock(1533447343, 58114, 0x207fffff, 1, 50 * COIN);
        consensus.hashGenesisBlock = genesis.GetHash();
		
        assert(consensus.hashGenesisBlock == uint256S("0x2851bcc4e828d21ad4291244d5154227a9b6f9f179a5a0e076cc6d6817cb9ed9"));
        assert(genesis.hashMerkleRoot == uint256S("0x47eec0209382963a4f867534b242cd795688b38471f3b85236de04f4e77d19f4"));

        vFixedSeeds.clear(); //! Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();  //! Regtest mode doesn't have any DNS seeds.

        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
		strFoundationAddress = "yj17EQ3CcimHaE6ffNpm1unkeidbZqgVzK";

        nFulfilledRequestExpireTime = 5*60; // fulfilled requests expire in 5 minutes

        checkpointData = (CCheckpointData){
            boost::assign::map_list_of
            ( 0, uint256S("0x2851bcc4e828d21ad4291244d5154227a9b6f9f179a5a0e076cc6d6817cb9ed9")),
            0,
            0,
            0
        };
        // Regtest Soom addresses start with 'y'
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,140);
        // Regtest Soom script addresses start with '8' or '9'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,19);
        // Regtest private keys start with '9' or 'c' (Bitcoin defaults)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        // Regtest Soom BIP32 pubkeys start with 'tpub' (Bitcoin defaults)
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Regtest Soom BIP32 prvkeys start with 'tprv' (Bitcoin defaults)
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();

        // Regtest Soom BIP44 coin type is '1' (All coin's testnet default)
        nExtCoinType = 1;
   }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}
