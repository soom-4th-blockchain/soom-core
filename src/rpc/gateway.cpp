// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "netbase.h"
#include "validation.h"
#include "gateway-payments.h"
#include "gateway-sync.h"
#include "gatewayconfig.h"
#include "gatewayman.h"
#include "wallet/wallet.h"
#include "rpc/server.h"
#include "tinyformat.h"
#include "util.h"
#include "utilmoneystr.h"
#include "txmempool.h"

#include "evo/specialtx.h"
#include "evo/deterministicgws.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>

UniValue gatewaylist(const JSONRPCRequest& request);

bool EnsureWalletIsAvailable(bool avoidException);
#ifdef ENABLE_WALLET
void EnsureWalletIsUnlocked();
#endif // ENABLE_WALLET

void gateway_list_help()
{
    throw std::runtime_error(
            "gateway list ( \"mode\" \"filter\" )\n"
            "Get a list of gateways in different modes. This call is identical to gatewaylist call.\n"
            "\nArguments:\n"
            "1. \"mode\"      (string, optional/required to use filter, defaults = json) The mode to run list in\n"
            "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
            "                                    additional matches in some modes are also available\n"
            "\nAvailable modes:\n"
            "  activeseconds  - Print number of seconds gateway recognized by the network as enabled\n"
            "                   (since latest issued \"gateway start/start-many/start-alias\")\n"
            "  addr           - Print ip address associated with a gateway (can be additionally filtered, partial match)\n"
            "  daemon         - Print daemon version of a gateway (can be additionally filtered, exact match)\n"
            "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
            "                   (can be additionally filtered, partial match)\n"
            "  info           - Print info in format 'status protocol payee lastseen activeseconds sentinelversion sentinelstate IP'\n"
            "                   (can be additionally filtered, partial match)\n"
            "  json           - Print info in JSON format (can be additionally filtered, partial match)\n"
            "  lastpaidblock  - Print the last block height a node was paid on the network\n"
            "  lastpaidtime   - Print the last time a node was paid on the network\n"
            "  lastseen       - Print timestamp of when a gateway was last seen on the network\n"
            "  owneraddress   - Print the gateway owner Soom address\n"
            "  payee          - Print the gateway payout Soom address (can be additionally filtered,\n"
            "                   partial match)\n"
            "  protocol       - Print protocol of a gateway (can be additionally filtered, exact match)\n"
            "  keyid          - Print the gateway (not collateral) key id\n"
            "  rank           - Print rank of a gateway based on current block\n"
            "  sentinel       - Print sentinel version of a gateway (can be additionally filtered, exact match)\n"
            "  status         - Print gateway status: PRE_ENABLED / ENABLED / EXPIRED / SENTINEL_PING_EXPIRED / NEW_START_REQUIRED /\n"
            "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
            "  votingaddress  - Print the gateway voting Soom address\n"
        );
}

UniValue gateway_list(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_list_help();
    JSONRPCRequest newRequest = request;
    newRequest.params.setArray();
    // forward params but skip "list"
    for (unsigned int i = 1; i < request.params.size(); i++) {
        newRequest.params.push_back(request.params[i]);
    }
    return gatewaylist(newRequest);
}

void gateway_connect_help()
{
    throw std::runtime_error(
            "gateway connect \"address\"\n"
            "Connect to given gateway\n"
            "\nArguments:\n"
            "1. \"address\"      (string, required) The address of the gateway to connect\n"
        );
}

UniValue gateway_connect(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        gateway_connect_help();

    std::string strAddress = request.params[1].get_str();

    CService addr;
    if (!Lookup(strAddress.c_str(), addr, 0, false))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect gateway address %s", strAddress));

    // TODO: Pass CConnman instance somehow and don't use global variable.
    g_connman->OpenGatewayConnection(CAddress(addr, NODE_NETWORK));
    if (!g_connman->IsConnected(CAddress(addr, NODE_NETWORK), CConnman::AllNodes))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to gateway %s", strAddress));

    return "successfully connected";
}

void gateway_count_help()
{
    throw std::runtime_error(
            "gateway count (\"mode\")\n"
            "  Get information about number of gateways. Mode\n"
            "  usage is depricated, call without mode params returns\n"
            "  all values in JSON format.\n"
            "\nArguments:\n"
            "1. \"mode\"      (string, optional, DEPRICATED) Option to get number of gateways in different states\n"
            "\nAvailable modes:\n"
            "  total         - total number of gateways"
            "  enabled       - number of enabled gateways"
            "  qualify       - number of qualified gateways"
            "  all           - all above in one string"
        );
}

UniValue gateway_count(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        gateway_count_help();

    int nCount;
    int total = gwnodeman.CountGateways(0);
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        nCount = gwnodeman.CountEnabled();
    } else {
        gateway_info_t gwInfo;
        gwnodeman.GetNextGatewayInQueueForPayment(true, nCount, gwInfo);
    }

    int enabled = gwnodeman.CountEnabled();

    if (request.params.size() == 1) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("total", total));
        obj.push_back(Pair("enabled", enabled));
        obj.push_back(Pair("qualify", nCount));

        return obj;
    }

    std::string strMode = request.params[1].get_str();

    if (strMode == "total")
        return total;

    if (strMode == "enabled")
        return enabled;

    if (strMode == "qualify")
        return nCount;

    if (strMode == "all")
        return strprintf("Total: %d (Enabled: %d / Qualify: %d)",
            total, enabled, nCount);

    throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown mode value");
}

UniValue GetNextGatewayForPayment(int heightShift)
{
    int nCount;
    int nHeight;
    gateway_info_t gwInfo;
    CBlockIndex* pindex = NULL;
    {
        LOCK(cs_main);
        pindex = chainActive.Tip();
    }

    nHeight = pindex->nHeight + heightShift;
    gwnodeman.UpdateLastPaid(pindex);

    CScript payeeScript;
    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        auto payee = deterministicGWManager->GetListAtChainTip().GetGWPayee();
        if (!payee || !gwnodeman.GetGatewayInfo(payee->proTxHash, gwInfo))
            return "unknown";
        payeeScript = payee->pdgwState->scriptPayout;
    } else {
        if (!gwnodeman.GetNextGatewayInQueueForPayment(nHeight, true, nCount, gwInfo))
            return "unknown";
        payeeScript = GetScriptForDestination(gwInfo.keyIDCollateralAddress);
    }

    CTxDestination payeeDest;
    CBitcoinAddress payeeAddr;
    if (ExtractDestination(payeeScript, payeeDest)) {
        payeeAddr = CBitcoinAddress(payeeDest);
    }

    UniValue obj(UniValue::VOBJ);

    obj.push_back(Pair("height",        nHeight));
    obj.push_back(Pair("IP:port",       gwInfo.addr.ToString()));
    obj.push_back(Pair("protocol",      gwInfo.nProtocolVersion));
    obj.push_back(Pair("outpoint",      gwInfo.outpoint.ToStringShort()));
    obj.push_back(Pair("payee",         payeeAddr.IsValid() ? payeeAddr.ToString() : "UNKNOWN"));
    obj.push_back(Pair("lastseen",      gwInfo.nTimeLastPing));
    obj.push_back(Pair("activeseconds", gwInfo.nTimeLastPing - gwInfo.sigTime));
    return obj;
}

void gateway_winner_help()
{
    throw std::runtime_error(
            "gateway winner\n"
            "Print info on next gateway winner to vote for\n"
        );
}

UniValue gateway_winner(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_winner_help();

    return GetNextGatewayForPayment(10);
}

void gateway_current_help()
{
    throw std::runtime_error(
            "gateway current\n"
            "Print info on current gateway winner to be paid the next block (calculated locally)\n"
        );
}

UniValue gateway_current(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_current_help();

    return GetNextGatewayForPayment(1);
}

#ifdef ENABLE_WALLET
void gateway_start_alias_help()
{
    throw std::runtime_error(
            "gateway start-alias \"alias\"\n"
            "Start single remote gateway by assigned alias\n"
            "\nArguments:\n"
            "1. \"alias\"      (string, required) The alias of the remote gateway configured in gateway.conf\n"
        );
}

UniValue gateway_start_alias(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        gateway_start_alias_help();
    if (deterministicGWManager->IsDeterministicGWsSporkActive())
        throw JSONRPCError(RPC_MISC_ERROR, "start-alias is not supported when deterministic gateway list is active (DIP3)");

    if (!EnsureWalletIsAvailable(request.fHelp))
        return NullUniValue;

    {
        LOCK(pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();
    }

    std::string strAlias = request.params[1].get_str();

    bool fFound = false;

    UniValue statusObj(UniValue::VOBJ);
    statusObj.push_back(Pair("alias", strAlias));

    for (const auto& gwe : gatewayConfig.getEntries()) {
        if (gwe.getAlias() == strAlias) {
            fFound = true;
            std::string strError;
            CGatewayBroadcast gwb;

            bool fResult = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

            int nDoS;
            if (fResult && !gwnodeman.CheckGwbAndUpdateGatewayList(NULL, gwb, nDoS, *g_connman)) {
                strError = "Failed to verify GWB";
                fResult = false;
            }

            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
            if (!fResult) {
                statusObj.push_back(Pair("errorMessage", strError));
            }
            gwnodeman.NotifyGatewayUpdates(*g_connman);
            break;
        }
    }

    if (!fFound) {
        statusObj.push_back(Pair("result", "failed"));
        statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
    }

    return statusObj;
}

void gateway_start_all_help()
{
    throw std::runtime_error(
            "gateway start-all\n"
            "Start remote gateways configured in gateway.conf\n"
        );
}

UniValue StartGatewayList(const std::vector<CGatewayConfig::CGatewayEntry>& entries)
{
    int nSuccessful = 0;
    int nFailed = 0;

    UniValue resultsObj(UniValue::VOBJ);

    for (const auto& gwe : entries) {
        std::string strError;
        CGatewayBroadcast gwb;

        bool fResult = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

        int nDoS;
        if (fResult && !gwnodeman.CheckGwbAndUpdateGatewayList(NULL, gwb, nDoS, *g_connman)) {
            strError = "Failed to verify GWB";
            fResult = false;
        }

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", gwe.getAlias()));
        statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

        if (fResult) {
            nSuccessful++;
        } else {
            nFailed++;
            statusObj.push_back(Pair("errorMessage", strError));
        }

        resultsObj.push_back(Pair("status", statusObj));
    }
    gwnodeman.NotifyGatewayUpdates(*g_connman);

    UniValue returnObj(UniValue::VOBJ);
    returnObj.push_back(Pair("overall", strprintf("Successfully started %d gateways, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
    returnObj.push_back(Pair("detail", resultsObj));

    return returnObj;
}

UniValue gateway_start_all(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_start_all_help();
    if (deterministicGWManager->IsDeterministicGWsSporkActive())
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("start-all is not supported when deterministic gateway list is active (DIP3)"));

    if (!EnsureWalletIsAvailable(request.fHelp))
        return NullUniValue;

    {
        LOCK(pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();
    }

    return StartGatewayList(gatewayConfig.getEntries());
}

void gateway_start_missing_help()
{
    throw std::runtime_error(
            "gateway start-missing\n"
            "Start not started remote gateways configured in gateway.conf\n"
        );
}

UniValue gateway_start_missing(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_start_missing_help();

    if (!EnsureWalletIsAvailable(request.fHelp))
        return NullUniValue;

    {
        LOCK(pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();
    }

    if (!gatewaySync.IsGatewayListSynced()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until gateway list is synced");
    }

    std::vector<CGatewayConfig::CGatewayEntry> entries;
    for (const auto& gwe : gatewayConfig.getEntries()) {
        COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), (uint32_t)atoi(gwe.getOutputIndex()));
        CGateway gw;
        bool fFound = gwnodeman.Get(outpoint, gw);
        if (fFound)
            entries.push_back(gwe);
    }
    return StartGatewayList(entries);
}

void gateway_start_disabled_help()
{
    throw std::runtime_error(
            "gateway start-disabled\n"
            "Start not started and disabled remote gateways configured in gateway.conf\n"
        );
}

UniValue gateway_start_disabled(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_start_disabled_help();

    if (!EnsureWalletIsAvailable(request.fHelp))
        return NullUniValue;

    {
        LOCK(pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();
    }

    if (!gatewaySync.IsGatewayListSynced()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until gateway list is synced");
    }

    std::vector<CGatewayConfig::CGatewayEntry> entries;
    for (const auto& gwe : gatewayConfig.getEntries()) {
        COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), (uint32_t)atoi(gwe.getOutputIndex()));
        CGateway gw;
        bool fFound = gwnodeman.Get(outpoint, gw);

        if (fFound && gw.IsEnabled()) continue;

        entries.push_back(gwe);
    }
    return StartGatewayList(entries);
}

void gateway_outputs_help()
{
    throw std::runtime_error(
            "gateway outputs\n"
            "Print gateway compatible outputs\n"
        );
}

UniValue gateway_outputs(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_outputs_help();

    if (!EnsureWalletIsAvailable(request.fHelp))
        return NullUniValue;

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
    pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_5000);

    UniValue obj(UniValue::VOBJ);
    for (const auto& out : vPossibleCoins) {
        obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
    }

    return obj;
}

#endif // ENABLE_WALLET

void gateway_genkey_help()
{
    throw std::runtime_error(
            "gateway genkey (compressed)\n"
            "Generate new gatewayprivkey\n"
            "\nArguments:\n"
            "1. compressed        (boolean, optional, default=false) generate compressed privkey\n"
        );
}

UniValue gateway_genkey(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_genkey_help();

    bool fCompressed = false;
    if (request.params.size() > 1) {
        fCompressed = ParseBoolV(request.params[1], "compressed");
    }

    CKey secret;
    secret.MakeNewKey(fCompressed);

    return CBitcoinSecret(secret).ToString();
}

void gateway_list_conf_help()
{
    throw std::runtime_error(
            "gateway list-conf\n"
            "Print gateway.conf in JSON format\n"
        );
}

UniValue gateway_list_conf(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_list_conf_help();

    UniValue resultObj(UniValue::VOBJ);

    for (const auto& gwe : gatewayConfig.getEntries()) {
        COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), (uint32_t)atoi(gwe.getOutputIndex()));
        CGateway gw;
        bool fFound = gwnodeman.Get(outpoint, gw);

        std::string strStatus = fFound ? gw.GetStatus() : "MISSING";

        UniValue gwObj(UniValue::VOBJ);
        gwObj.push_back(Pair("alias", gwe.getAlias()));
        gwObj.push_back(Pair("address", gwe.getIp()));
        gwObj.push_back(Pair("privateKey", gwe.getPrivKey()));
        gwObj.push_back(Pair("txHash", gwe.getTxHash()));
        gwObj.push_back(Pair("outputIndex", gwe.getOutputIndex()));
        gwObj.push_back(Pair("status", strStatus));
        resultObj.push_back(Pair("gateway", gwObj));
    }

    return resultObj;
}

void gateway_gen_conf_help()
{
    throw std::runtime_error(
            "gateway gen-conf\n"
            "Generate gateway.conf\n"
            "\nArguments:\n"
            "1. \"Gateway account\"      (string, required) The Gateway account in gateway.conf\n"
        );
}

UniValue gateway_gen_conf(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2)
        gateway_gen_conf_help();

    if (request.params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gateway gen-conf ( \"gw account\" )'");

    UniValue resultObj(UniValue::VOBJ);

    std::string account_name = request.params[1].get_str();
//    std::map<CTxDestination, CAddressBookData>::iterator mi;
//    for(mi = pwalletMain->mapAddressBook.begin(); mi != pwalletMain->mapAddressBook.end(); mi++) {
//
//        if((*mi).second.name == account_name){
//            break;
//        }
//    }
//    if(mi == pwalletMain->mapAddressBook.end()){
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "Gateway account is invalid!");
//    }

    if(std::none_of(pwalletMain->mapAddressBook.begin(), pwalletMain->mapAddressBook.end(),
            [&account_name](std::pair<CTxDestination, CAddressBookData> mi){ return mi.second.name == account_name; }))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Gateway account is invalid!");

    // step1: check balance
    if(pwalletMain->GetBalance() < (COLLATERAL_COINS)){ // if there are not available collateral balance, return fail
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
    }

    // step2: execute \"genkey\" --> get privkey
    CKey secret;
    secret.MakeNewKey(false);
    std::string genkey = CBitcoinSecret(secret).ToString();

    if(genkey.length() <= 0){ // Unavailable PrivKey
        throw JSONRPCError(RPC_WALLET_ERROR, "Unavailable PrivKey");
    }

    // step3: execute \"outputs\" --> get collateral id & index
    std::vector<COutput> vPossibleCoins;
    pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_5000);

    if(!vPossibleCoins.size()){ // Unavailable Coins
        throw JSONRPCError(RPC_WALLET_ERROR, "Unavailable Coins");
    }

    std::string collateral_str = strprintf("%s %d", vPossibleCoins.begin()->tx->GetHash().ToString().c_str(), vPossibleCoins.begin()->i);

    // step4: get localaddr & port
    std::string netaddr;
    {
        LOCK(cs_mapLocalHost);
        for(const auto item : mapLocalHost){
            if(item.first.IsRoutable()){
                netaddr = strprintf("%s:%d", item.first.ToString().c_str(), item.second.nPort);
                break;
            }
        }
    }

    if(!netaddr.length()){
        throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET, "No Variable Ip/Port");
    }

    // step5: file modify (soom.conf)
    boost::filesystem::path pathConfFileOrigin(GetArg("-conf", BITCOIN_CONF_FILENAME));
    if (!pathConfFileOrigin.is_complete()){
        pathConfFileOrigin = GetDataDir(false) / pathConfFileOrigin;
    }

    boost::filesystem::path pathConfFileCopy(GetArg("-conf", BITCOIN_CONF_FILENAME).append("_bak"));

    if (!pathConfFileCopy.is_complete()){
        pathConfFileCopy = GetDataDir(false) / pathConfFileCopy;
    }

    std::ifstream fin_conf;
    std::ofstream fout_conf;

    fin_conf.open(pathConfFileOrigin.string());
    fout_conf.open(pathConfFileCopy.string());

    std::string readline_conf;
    std::string except_str1 = "gateway=";
    std::string except_str2 = "gatewayprivkey=";

    while(!fin_conf.eof()){
        std::getline(fin_conf, readline_conf);
        if(readline_conf.empty()) continue;
        if((readline_conf.find(except_str1) == std::string::npos) && (readline_conf.find(except_str2) == std::string::npos)){
            fout_conf << readline_conf << std::endl;
        }
    }

    std::string gw = "gateway=1";
    fout_conf << gw << std::endl;

    std::string gwprivkey = "gatewayprivkey=" + genkey;
    fout_conf << gwprivkey << std::endl;

    fin_conf.close();
    fout_conf.close();

    try {
        boost::filesystem::remove(pathConfFileOrigin);
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to remove file: %s\n", __func__, e.what());
    }

    try {
        boost::filesystem::rename(pathConfFileCopy, pathConfFileOrigin);
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to rename file: %s\n", __func__, e.what());
    }

    // step6: file modify (gateway.conf)
    std::string gwinfo_str = strprintf("%s %s %s %s", account_name.c_str(), netaddr.c_str(), genkey.c_str(), collateral_str.c_str());

    boost::filesystem::path pathGwFileOrigin(GetArg("-gwconf", "gateway.conf"));

    if (!pathGwFileOrigin.is_complete()){
        pathGwFileOrigin = GetDataDir() / pathGwFileOrigin;
    }

    boost::filesystem::path pathGwFileCopy(GetArg("-gwconf", "gateway.conf").append("_bak"));
    if (!pathGwFileCopy.is_complete()){
        pathGwFileCopy = GetDataDir() / pathGwFileCopy;
    }

    std::ifstream fin_gw;
    std::ofstream fout_gw;

    fin_gw.open(pathGwFileOrigin.string());
    fout_gw.open(pathGwFileCopy.string());

    std::string readline_gw;
    std::string except_str = "# ";

    while(!fin_gw.eof()){
        std::getline(fin_gw, readline_gw);
        if(readline_gw.empty()) continue;
        if(readline_gw.find(except_str) != std::string::npos){
            fout_gw << readline_gw << std::endl;
        }
    }

    fout_gw << gwinfo_str << std::endl;

    fin_gw.close();
    fout_gw.close();

    try {
        boost::filesystem::remove(pathGwFileOrigin);
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to remove file: %s\n", __func__, e.what());
    }

    try {
        boost::filesystem::rename(pathGwFileCopy, pathGwFileOrigin);
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to rename file: %s\n", __func__, e.what());
    }

    resultObj.push_back(Pair("overall", strprintf("Successfully Generate Gateway conf. %d", 1)));

    return resultObj;
}

void gateway_status_help()
{
    throw std::runtime_error(
            "gateway status\n"
            "Print gateway status information\n"
        );
}

UniValue gateway_status(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_status_help();

    if (!fGatewayMode)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a gateway");

    UniValue gwObj(UniValue::VOBJ);

    // keep compatibility with legacy status for now (might get deprecated/removed later)
    gwObj.push_back(Pair("outpoint", activeGatewayInfo.outpoint.ToStringShort()));
    gwObj.push_back(Pair("service", activeGatewayInfo.service.ToString()));

    if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
        auto dgw = activeGatewayManager->GetDGW();
        if (dgw) {
            gwObj.push_back(Pair("proTxHash", dgw->proTxHash.ToString()));
            gwObj.push_back(Pair("collateralHash", dgw->collateralOutpoint.hash.ToString()));
            gwObj.push_back(Pair("collateralIndex", (int)dgw->collateralOutpoint.n));
            UniValue stateObj;
            dgw->pdgwState->ToJson(stateObj);
            gwObj.push_back(Pair("dgwState", stateObj));
        }
        gwObj.push_back(Pair("state", activeGatewayManager->GetStateString()));
        gwObj.push_back(Pair("status", activeGatewayManager->GetStatus()));
    } else {
        CGateway gw;
        if (gwnodeman.Get(activeGatewayInfo.outpoint, gw)) {
            gwObj.push_back(Pair("payee", CBitcoinAddress(gw.keyIDCollateralAddress).ToString()));
        }

        gwObj.push_back(Pair("status", legacyActiveGatewayManager.GetStatus()));
    }
    return gwObj;
}

void gateway_winners_help()
{
    throw std::runtime_error(
            "gateway winners ( count \"filter\" )\n"
            "Print list of gateway winners\n"
            "\nArguments:\n"
            "1. count        (numeric, optional) number of last winners to return\n"
            "2. filter       (string, optional) filter for returned winners\n"
        );
}

UniValue gateway_winners(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_winners_help();

    int nHeight;
    {
        LOCK(cs_main);
        CBlockIndex* pindex = chainActive.Tip();
        if (!pindex) return NullUniValue;

        nHeight = pindex->nHeight;
    }

    int nLast = 10;
    std::string strFilter = "";

    if (request.params.size() >= 2) {
        nLast = atoi(request.params[1].get_str());
    }

    if (request.params.size() == 3) {
        strFilter = request.params[2].get_str();
    }

    if (request.params.size() > 3)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gateway winners ( \"count\" \"filter\" )'");

    UniValue obj(UniValue::VOBJ);
    auto mapPayments = GetRequiredPaymentsStrings(nHeight - nLast, nHeight + 20);
    for (const auto &p : mapPayments) {
        obj.push_back(Pair(strprintf("%d", p.first), p.second));
    }

    return obj;
}

void gateway_check_help()
{
    throw std::runtime_error(
            "gateway check\n"
            "Force check all gateways and remove invalid ones\n"
        );
}

UniValue gateway_check(const JSONRPCRequest& request)
{
    if (request.fHelp)
        gateway_check_help();

    int countBeforeCheck = gwnodeman.CountGateways();
    int countEnabledBeforeCheck = gwnodeman.CountEnabled();

    gwnodeman.CheckAndRemove(*g_connman);

    int countAfterCheck = gwnodeman.CountGateways();
    int countEnabledAfterCheck = gwnodeman.CountEnabled();
    int removedCount = std::max(0, countBeforeCheck - countAfterCheck);
    int removedEnabledCount = std::max(0, countEnabledBeforeCheck - countEnabledAfterCheck);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("removedTotalCount", removedCount));
    obj.push_back(Pair("removedEnabledCount", removedEnabledCount));
    obj.push_back(Pair("totalCount", countAfterCheck));
    obj.push_back(Pair("enabledCount", countEnabledAfterCheck));

    return obj;
}

[[ noreturn ]] void gateway_help()
{
    throw std::runtime_error(
        "gateway \"command\"...\n"
        "Set of commands to execute gateway related actions\n"
        "\nArguments:\n"
        "1. \"command\"        (string or set of strings, required) The command to execute\n"
        "\nAvailable commands:\n"
        "  check        - Force check all gateways and remove invalid ones\n"
        "  count        - Get information about number of gateways (DEPRECATED options: 'total', 'enabled', 'qualify', 'all')\n"
        "  current      - Print info on current gateway winner to be paid the next block (calculated locally)\n"
        "  genkey       - Generate new gatewayprivkey, optional param: 'compressed' (boolean, optional, default=false) generate compressed privkey\n"
#ifdef ENABLE_WALLET
        "  outputs      - Print gateway compatible outputs\n"
        "  start-alias  - Start single remote gateway by assigned alias configured in gateway.conf\n"
        "  start-<mode> - Start remote gateways configured in gateway.conf (<mode>: 'all', 'missing', 'disabled')\n"
#endif // ENABLE_WALLET
        "  status       - Print gateway status information\n"
        "  list         - Print list of all known gateways (see gatewaylist for more info)\n"
        "  list-conf    - Print gateway.conf in JSON format\n"
        "  gen-conf     - Generate gateway configuration\n"
        "  winner       - Print info on next gateway winner to vote for\n"
        "  winners      - Print list of gateway winners\n"
        );
}

UniValue gateway(const JSONRPCRequest& request)
{
    std::string strCommand;
    if (request.params.size() >= 1) {
        strCommand = request.params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (request.fHelp && strCommand.empty()) {
        gateway_help();
    }

    if (strCommand == "list") {
        return gateway_list(request);
    } else if (strCommand == "connect") {
        return gateway_connect(request);
    } else if (strCommand == "count") {
        return gateway_count(request);
    } else if (strCommand == "current") {
        return gateway_current(request);
    } else if (strCommand == "winner") {
        return gateway_winner(request);
#ifdef ENABLE_WALLET
    } else if (strCommand == "start-alias") {
        return gateway_start_alias(request);
    } else if (strCommand == "start-all") {
        return gateway_start_all(request);
    } else if (strCommand == "start-missing") {
        return gateway_start_missing(request);
    } else if (strCommand == "start-disabled") {
        return gateway_start_disabled(request);
#endif // ENABLE_WALLET
    } else if (strCommand == "genkey") {
        return gateway_genkey(request);
    } else if (strCommand == "list-conf") {
        return gateway_list_conf(request);
    } else if (strCommand == "gen-conf") {
        return gateway_gen_conf(request);
#ifdef ENABLE_WALLET
    } else if (strCommand == "outputs") {
        return gateway_outputs(request);
#endif // ENABLE_WALLET
    } else if (strCommand == "status") {
        return gateway_status(request);
    } else if (strCommand == "winners") {
        return gateway_winners(request);
    } else if (strCommand == "check") {
        return gateway_check(request);
    } else {
        gateway_help();
    }
}


UniValue gatewaylist(const JSONRPCRequest& request)
{
    std::string strMode = "json";
    std::string strFilter = "";

    if (request.params.size() >= 1) strMode = request.params[0].get_str();
    if (request.params.size() == 2) strFilter = request.params[1].get_str();

    if (request.fHelp || (
                strMode != "activeseconds" && strMode != "addr" && strMode != "daemon" && strMode != "full" && strMode != "info" && strMode != "json" &&
                strMode != "owneraddress" && strMode != "votingaddress" && strMode != "keyid" &&
                strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "protocol" && strMode != "payee" && strMode != "pubkey" &&
                strMode != "rank" && strMode != "sentinel" && strMode != "status"))
    {
        gateway_list_help();
    }

    if (strMode == "full" || strMode == "json" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        gwnodeman.UpdateLastPaid(pindex);
    }

    UniValue obj(UniValue::VOBJ);
    if (strMode == "rank") {
        CGatewayMan::rank_pair_vec_t vGatewayRanks;
        gwnodeman.GetGatewayRanks(vGatewayRanks);
        for (const auto& rankpair : vGatewayRanks) {
            std::string strOutpoint = rankpair.second.outpoint.ToStringShort();
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, rankpair.first));
        }
    } else {
        std::map<COutPoint, CGateway> mapGateways = gwnodeman.GetFullGatewayMap();
        for (const auto& gwpair : mapGateways) {
            CGateway gw = gwpair.second;
            std::string strOutpoint = gwpair.first.ToStringShort();

            CScript payeeScript;
            if (deterministicGWManager->IsDeterministicGWsSporkActive()) {
                auto dgw = deterministicGWManager->GetListAtChainTip().GetGWByCollateral(gw.outpoint);
                if (dgw) {
                    payeeScript = dgw->pdgwState->scriptPayout;
                }
            } else {
                payeeScript = GetScriptForDestination(gw.keyIDCollateralAddress);
            }

            CTxDestination payeeDest;
            std::string payeeStr = "UNKOWN";
            if (ExtractDestination(payeeScript, payeeDest)) {
                payeeStr = CBitcoinAddress(payeeDest).ToString();
            }

            if (strMode == "activeseconds") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)(gw.lastPing.sigTime - gw.sigTime)));
            } else if (strMode == "addr") {
                std::string strAddress = gw.addr.ToString();
                if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strAddress));
            } else if (strMode == "daemon") {
                std::string strDaemon =gw.lastPing.GetDaemonString();
                if (strFilter !="" && strDaemon.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strDaemon));
            } else if (strMode == "sentinel") {
                std::string strSentinel = gw.lastPing.GetSentinelString();
                if (strFilter !="" && strSentinel.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strSentinel));
            } else if (strMode == "full") {
                std::ostringstream streamFull;
                streamFull << std::setw(18) <<
                               gw.GetStatus() << " " <<
                               gw.nProtocolVersion << " " <<
                               payeeStr << " " <<
                               (int64_t)gw.lastPing.sigTime << " " << std::setw(8) <<
                               (int64_t)(gw.lastPing.sigTime - gw.sigTime) << " " << std::setw(10) <<
                               gw.GetLastPaidTime() << " "  << std::setw(6) <<
                               gw.GetLastPaidBlock() << " " <<
                               gw.addr.ToString();
                std::string strFull = streamFull.str();
                if (strFilter !="" && strFull.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strFull));
            } else if (strMode == "info") {
                std::ostringstream streamInfo;
                streamInfo << std::setw(18) <<
                               gw.GetStatus() << " " <<
                               gw.nProtocolVersion << " " <<
                               payeeStr << " " <<
                               (int64_t)gw.lastPing.sigTime << " " << std::setw(8) <<
                               (int64_t)(gw.lastPing.sigTime - gw.sigTime) << " " <<
                               gw.lastPing.GetSentinelString() << " "  <<
                               (gw.lastPing.fSentinelIsCurrent ? "current" : "expired") << " " <<
                               gw.addr.ToString();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strInfo));
            } else if (strMode == "json") {
                std::ostringstream streamInfo;
                streamInfo <<  gw.addr.ToString() << " " <<
                               payeeStr << " " <<
                               gw.GetStatus() << " " <<
                               gw.nProtocolVersion << " " <<
                               gw.lastPing.nDaemonVersion << " " <<
                               gw.lastPing.GetSentinelString() << " " <<
                               (gw.lastPing.fSentinelIsCurrent ? "current" : "expired") << " " <<
                               (int64_t)gw.lastPing.sigTime << " " <<
                               (int64_t)(gw.lastPing.sigTime - gw.sigTime) << " " <<
                               gw.GetLastPaidTime() << " " <<
                               gw.GetLastPaidBlock();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                UniValue objGW(UniValue::VOBJ);
                objGW.push_back(Pair("address", gw.addr.ToString()));
                objGW.push_back(Pair("payee", CBitcoinAddress(gw.pubKeyCollateralAddress.GetID()).ToString()));
                objGW.push_back(Pair("status", gw.GetStatus()));
                objGW.push_back(Pair("protocol", gw.nProtocolVersion));
                objGW.push_back(Pair("daemonversion", gw.lastPing.GetDaemonString()));
                objGW.push_back(Pair("sentinelversion", gw.lastPing.GetSentinelString()));
                objGW.push_back(Pair("sentinelstate", (gw.lastPing.fSentinelIsCurrent ? "current" : "expired")));
                objGW.push_back(Pair("lastseen", (int64_t)gw.lastPing.sigTime));
                objGW.push_back(Pair("activeseconds", (int64_t)(gw.lastPing.sigTime - gw.sigTime)));
                objGW.push_back(Pair("lastpaidtime", gw.GetLastPaidTime()));
                objGW.push_back(Pair("lastpaidblock", gw.GetLastPaidBlock()));
                objGW.push_back(Pair("owneraddress", CBitcoinAddress(gw.keyIDOwner).ToString()));
                objGW.push_back(Pair("votingaddress", CBitcoinAddress(gw.keyIDVoting).ToString()));
                obj.push_back(Pair(strOutpoint, objGW));
            } else if (strMode == "keyid") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, HexStr(gw.legacyKeyIDOperator)));
            } else if (strMode == "lastpaidblock") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, gw.GetLastPaidBlock()));
            } else if (strMode == "lastpaidtime") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, gw.GetLastPaidTime()));
            } else if (strMode == "lastseen") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)gw.lastPing.sigTime));
            } else if (strMode == "owneraddress") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, CBitcoinAddress(gw.keyIDOwner).ToString()));
            } else if (strMode == "payee") {
                if (strFilter !="" && payeeStr.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, payeeStr));
            } else if (strMode == "protocol") {
                if (strFilter !="" && strFilter != strprintf("%d", gw.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, gw.nProtocolVersion));
            } else if (strMode == "status") {
                std::string strStatus = gw.GetStatus();
                if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strStatus));
            } else if (strMode == "votingaddress") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, CBitcoinAddress(gw.keyIDVoting).ToString()));
            }
        }
    }
    return obj;
}

bool DecodeHexVecGwb(std::vector<CGatewayBroadcast>& vecGwb, std::string strHexGwb) {

    if (!IsHex(strHexGwb))
        return false;

    std::vector<unsigned char> gwbData(ParseHex(strHexGwb));
    CDataStream ssData(gwbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecGwb;
    } catch (const std::exception&) {
        return false;
    }

    return true;
}

UniValue gatewaybroadcast(const JSONRPCRequest& request)
{
    std::string strCommand;
    if (request.params.size() >= 1)
        strCommand = request.params[0].get_str();

    if (request.fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "create-alias" && strCommand != "create-all" &&
#endif // ENABLE_WALLET
            strCommand != "decode" && strCommand != "relay"))
        throw std::runtime_error(
                "gatewaybroadcast \"command\"...\n"
                "Set of commands to create and relay gateway broadcast messages\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
#ifdef ENABLE_WALLET
                "  create-alias  - Create single remote gateway broadcast message by assigned alias configured in gateway.conf\n"
                "  create-all    - Create remote gateway broadcast messages for all gateways configured in gateway.conf\n"
#endif // ENABLE_WALLET
                "  decode        - Decode gateway broadcast message\n"
                "  relay         - Relay gateway broadcast message to the network\n"
                );

#ifdef ENABLE_WALLET
    if (strCommand == "create-alias") {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (request.params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        bool fFound = false;
        std::string strAlias = request.params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        std::vector<CGatewayBroadcast> vecGwb;

        statusObj.push_back(Pair("alias", strAlias));

        for (const auto& gwe : gatewayConfig.getEntries()) {
            if(gwe.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CGatewayBroadcast gwb;

                bool fResult = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb, true);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    vecGwb.push_back(gwb);
                    CDataStream ssVecGwb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecGwb << vecGwb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecGwb)));
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "not found"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "create-all") {
        if (!EnsureWalletIsAvailable(request.fHelp))
            return NullUniValue;

        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        std::vector<CGatewayBroadcast> vecGwb;

        for (const auto& gwe : gatewayConfig.getEntries()) {
            std::string strError;
            CGatewayBroadcast gwb;

            bool fResult = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", gwe.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if(fResult) {
                nSuccessful++;
                vecGwb.push_back(gwb);
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }

        CDataStream ssVecGwb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecGwb << vecGwb;
        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d gateways, failed to create %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));
        returnObj.push_back(Pair("hex", HexStr(ssVecGwb.begin(), ssVecGwb.end())));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "decode") {
        if (request.params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gatewaybroadcast decode \"hexstring\"'");

        std::vector<CGatewayBroadcast> vecGwb;

        if (!DecodeHexVecGwb(vecGwb, request.params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Gateway broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);

        for (const auto& gwb : vecGwb) {
            UniValue resultObj(UniValue::VOBJ);

            if(gwb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.push_back(Pair("outpoint", gwb.outpoint.ToStringShort()));
                resultObj.push_back(Pair("addr", gwb.addr.ToString()));
                resultObj.push_back(Pair("pubKeyCollateralAddress", CBitcoinAddress(gwb.pubKeyCollateralAddress.GetID()).ToString()));
                resultObj.push_back(Pair("pubKeyGateway", CBitcoinAddress(gwb.pubKeyGateway.GetID()).ToString()));
                resultObj.push_back(Pair("vchSig", EncodeBase64(&gwb.vchSig[0], gwb.vchSig.size())));
                resultObj.push_back(Pair("sigTime", gwb.sigTime));
                resultObj.push_back(Pair("protocolVersion", gwb.nProtocolVersion));

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.push_back(Pair("outpoint", gwb.lastPing.gatewayOutpoint.ToStringShort()));
                lastPingObj.push_back(Pair("blockHash", gwb.lastPing.blockHash.ToString()));
                lastPingObj.push_back(Pair("sigTime", gwb.lastPing.sigTime));
                lastPingObj.push_back(Pair("vchSig", EncodeBase64(&gwb.lastPing.vchSig[0], gwb.lastPing.vchSig.size())));

                resultObj.push_back(Pair("lastPing", lastPingObj));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Gateway broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(gwb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully decoded broadcast messages for %d gateways, failed to decode %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    if (strCommand == "relay") {
        if (request.params.size() < 2 || request.params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,   "gatewaybroadcast relay \"hexstring\"\n"
                                                        "\nArguments:\n"
                                                        "1. \"hex\"      (string, required) Broadcast messages hex string\n");

        std::vector<CGatewayBroadcast> vecGwb;

        if (!DecodeHexVecGwb(vecGwb, request.params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Gateway broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        for (const auto& gwb : vecGwb) {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("outpoint", gwb.outpoint.ToStringShort()));
            resultObj.push_back(Pair("addr", gwb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (gwb.CheckSignature(nDos)) {
                fResult = gwnodeman.CheckGwbAndUpdateGatewayList(NULL, gwb, nDos, *g_connman);
                gwnodeman.NotifyGatewayUpdates(*g_connman);
            } else fResult = false;

            if(fResult) {
                nSuccessful++;
                resultObj.push_back(Pair(gwb.GetHash().ToString(), "successful"));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Gateway broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(gwb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf("Successfully relayed broadcast messages for %d gateways, failed to relay %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    return NullUniValue;
}

UniValue sentinelping(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "sentinelping version\n"
            "\nSentinel ping.\n"
            "\nArguments:\n"
            "1. version           (string, required) Sentinel version in the form \"x.x.x\"\n"
            "\nResult:\n"
            "state                (boolean) Ping result\n"
            "\nExamples:\n"
            + HelpExampleCli("sentinelping", "1.0.2")
            + HelpExampleRpc("sentinelping", "1.0.2")
        );
    }

    legacyActiveGatewayManager.UpdateSentinelPing(StringVersionToInt(request.params[0].get_str()));
    return true;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafe argNames
  //  --------------------- ------------------------  -----------------------  ------ ----------
    { "soom",               "gateway",                &gateway,                true,  {} },
    { "soom",               "gatewaylist",            &gatewaylist,            true,  {} },
    { "soom",               "gatewaybroadcast",       &gatewaybroadcast,       true,  {} },
    { "soom",               "sentinelping",           &sentinelping,           true,  {} },
};

void RegisterGatewayRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
