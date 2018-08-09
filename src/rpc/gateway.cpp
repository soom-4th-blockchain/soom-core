// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activegateway.h"
#include "base58.h"
#include "init.h"
#include "netbase.h"
#include "validation.h"
#include "gateway-payments.h"
#include "gateway-sync.h"
#include "gatewayconfig.h"
#include "gatewayman.h"
#include "wallet/wallet.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>

#ifdef ENABLE_WALLET
void EnsureWalletIsUnlocked();
#endif // ENABLE_WALLET


UniValue gateway(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");
#endif // ENABLE_WALLET

    if (fHelp  ||
        (
#ifdef ENABLE_WALLET
            strCommand != "start-alias" && strCommand != "start-all" && strCommand != "start-missing" &&
         strCommand != "start-disabled" && strCommand != "outputs" &&
#endif // ENABLE_WALLET
         strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
         strCommand != "genconf" &&/* hong: */
         strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" && strCommand != "genkey" &&
         strCommand != "connect" && strCommand != "status"))
            throw std::runtime_error(
                "gateway \"command\"...\n"
                "Set of commands to execute gateway related actions\n"
                "\nArguments:\n"
                "1. \"command\"        (string or set of strings, required) The command to execute\n"
                "\nAvailable commands:\n"
                "  count        - Print number of all known gateways (optional: 'enabled', 'all', 'qualify')\n"
                "  current      - Print info on current gateway winner to be paid the next block (calculated locally)\n"
                "  genkey       - Generate new gatewayprivkey\n"
#ifdef ENABLE_WALLET
                "  outputs      - Print gateway compatible outputs\n"
                "  start-alias  - Start single remote gateway by assigned alias configured in gateway.conf\n"
                "  start-<mode> - Start remote gateways configured in gateway.conf (<mode>: 'all', 'missing', 'disabled')\n"
#endif // ENABLE_WALLET
                "  status       - Print gateway status information\n"
                "  list         - Print list of all known gateways (see gatewaylist for more info)\n"
                "  list-conf    - Print gateway.conf in JSON format\n"
                "  winner       - Print info on next gateway winner to vote for\n"
                "  winners      - Print list of gateway winners\n"
                );

    if (strCommand == "list")
    {
        UniValue newParams(UniValue::VARR);
        // forward params but skip "list"
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return gatewaylist(newParams, fHelp);
    }

    if(strCommand == "connect")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Gateway address required");

        std::string strAddress = params[1].get_str();

        CService addr;
        if (!Lookup(strAddress.c_str(), addr, 0, false))
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Incorrect gateway address %s", strAddress));

        // TODO: Pass CConnman instance somehow and don't use global variable.
        CNode *pnode = g_connman->ConnectNode(CAddress(addr, NODE_NETWORK), NULL);
        if(!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to gateway %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count")
    {
        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (params.size() == 1)
            return gwnodeman.size();

        std::string strMode = params[1].get_str();

        if (strMode == "enabled")
            return gwnodeman.CountEnabled();

        int nCount;
        gateway_info_t gwInfo;
        gwnodeman.GetNextGatewayInQueueForPayment(true, nCount, gwInfo);

        if (strMode == "qualify")
            return nCount;

        if (strMode == "all")
            return strprintf("Total: %d ( Enabled: %d / Qualify: %d)",
                gwnodeman.size(), gwnodeman.CountEnabled(), nCount);
    }

    if (strCommand == "current" || strCommand == "winner")
    {
        int nCount;
        int nHeight;
        gateway_info_t gwInfo;
        CBlockIndex* pindex = NULL;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        nHeight = pindex->nHeight + (strCommand == "current" ? 1 : 10);
        gwnodeman.UpdateLastPaid(pindex);

        if(!gwnodeman.GetNextGatewayInQueueForPayment(nHeight, true, nCount, gwInfo))
            return "unknown";

        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("height",        nHeight));
        obj.push_back(Pair("IP:port",       gwInfo.addr.ToString()));
        obj.push_back(Pair("protocol",      (int64_t)gwInfo.nProtocolVersion));
        obj.push_back(Pair("outpoint",      gwInfo.vin.prevout.ToStringShort()));
        obj.push_back(Pair("payee",         CBitcoinAddress(gwInfo.pubKeyCollateralAddress.GetID()).ToString()));
        obj.push_back(Pair("lastseen",      gwInfo.nTimeLastPing));
        obj.push_back(Pair("activeseconds", gwInfo.nTimeLastPing - gwInfo.sigTime));
        return obj;
    }

#ifdef ENABLE_WALLET
    if (strCommand == "start-alias")
    {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::string strAlias = params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
            if(gwe.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CGatewayBroadcast gwb;

                bool fResult = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if(fResult) {
                    gwnodeman.UpdateGatewayList(gwb, *g_connman);
                    gwb.Relay(*g_connman);
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
             
                break;
            }
        }

        if(!fFound) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled")
    {
        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if((strCommand == "start-missing" || strCommand == "start-disabled") && !gatewaySync.IsGatewayListSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "You can't use this command until gateway list is synced");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
            std::string strError;

            COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), uint32_t(atoi(gwe.getOutputIndex().c_str())));
            CGateway gw;
            bool fFound = gwnodeman.Get(outpoint, gw);
            CGatewayBroadcast gwb;

            if(strCommand == "start-missing" && fFound) continue;
            if(strCommand == "start-disabled" && fFound && gw.IsEnabled()) continue;

            bool fResult = CGatewayBroadcast::Create(gwe.getIp(), gwe.getPrivKey(), gwe.getTxHash(), gwe.getOutputIndex(), strError, gwb);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", gwe.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if (fResult) {
                nSuccessful++;
                gwnodeman.UpdateGatewayList(gwb, *g_connman);
                gwb.Relay(*g_connman);
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
    

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d gateways, failed to start %d, total %d", nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "genkey")
    {
        CKey secret;
        secret.MakeNewKey(false);

        return CBitcoinSecret(secret).ToString();
    }

    if (strCommand == "list-conf")
    {
        UniValue resultObj(UniValue::VOBJ);

        BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
            COutPoint outpoint = COutPoint(uint256S(gwe.getTxHash()), uint32_t(atoi(gwe.getOutputIndex().c_str())));
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

#ifdef ENABLE_WALLET
    if (strCommand == "outputs") {
        // Find possible candidates
        std::vector<COutput> vPossibleCoins;
        pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_5000);

        UniValue obj(UniValue::VOBJ);
        BOOST_FOREACH(COutput& out, vPossibleCoins) {
            obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
        }

        return obj;
    }
#endif // ENABLE_WALLET

    if (strCommand == "status")
    {
        if (!fGateWay)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a gateway");

        UniValue gwObj(UniValue::VOBJ);

        gwObj.push_back(Pair("outpoint", activeGateway.outpoint.ToStringShort()));
        gwObj.push_back(Pair("service", activeGateway.service.ToString()));

        CGateway gw;
        if(gwnodeman.Get(activeGateway.outpoint, gw)) {
            gwObj.push_back(Pair("payee", CBitcoinAddress(gw.pubKeyCollateralAddress.GetID()).ToString()));
        }

        gwObj.push_back(Pair("status", activeGateway.GetStatus()));
        return gwObj;
    }

    if (strCommand == "winners")
    {
        int nHeight;
        {
            LOCK(cs_main);
            CBlockIndex* pindex = chainActive.Tip();
            if(!pindex) return NullUniValue;

            nHeight = pindex->nHeight;
        }

        int nLast = 10;
        std::string strFilter = "";

        if (params.size() >= 2) {
            nLast = atoi(params[1].get_str());
        }

        if (params.size() == 3) {
            strFilter = params[2].get_str();
        }

        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gateway winners ( \"count\" \"filter\" )'");

        UniValue obj(UniValue::VOBJ);

        for(int i = nHeight - nLast; i < nHeight + 20; i++) {
            std::string strPayment = GetRequiredPaymentsString(i);
            if (strFilter !="" && strPayment.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strprintf("%d", i), strPayment));
        }

        return obj;
    }
	// hong: 
	if (strCommand == "genconf")
	{
		if (params.size() < 2){
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Gateway account required");
		}
		std::string account_name = params[1].get_str();
		map<CTxDestination, CAddressBookData>::iterator mi;
		for(mi = pwalletMain->mapAddressBook.begin(); mi != pwalletMain->mapAddressBook.end(); mi++){
			if((*mi).second.name == account_name){
				break;
			}
		}
		if(mi == pwalletMain->mapAddressBook.end()){
			throw JSONRPCError(RPC_INVALID_PARAMETER, "Gateway account is invalid!");
		}
		// step1: check balance
		if(pwalletMain->GetBalance() < (SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT*COIN)){ // 가용한 balance가 없을 경우 fail
			throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds (SPORK_5_INSTANTSEND_MAX_VALUE_DEFAULT)");
		}
		// step2: execute \"genkey\" --> get privkey
		CKey secret;
        secret.MakeNewKey(false);
        std::string genkey;
		genkey = CBitcoinSecret(secret).ToString();
		if(genkey.length() <= 0){ // 가용한 privkey가 아닐 경우 fail
			throw JSONRPCError(RPC_WALLET_ERROR, "Unavailable PrivKey");
		}
		// step3: execute \"outputs\" --> get collateral id & index
        std::vector<COutput> vPossibleCoins;
        pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_5000);
		if(!vPossibleCoins.size()){ // 가용한 COIN이 없을 경우 fail
			throw JSONRPCError(RPC_WALLET_ERROR, "Unavailable Coins");
		}
		char z_temp[512] = {0};
		COutput& out = *(vPossibleCoins.begin());
		snprintf(z_temp, 512, "%s %d", out.tx->GetHash().ToString().c_str(), out.i);
		std::string collateral_str = z_temp;
		// step4: get localaddr & port (가용한 IP/PORT가 없을 경우 fail)
		std::string netaddr = "";
	    {
	        LOCK(cs_mapLocalHost);
	        BOOST_FOREACH(const PAIRTYPE(CNetAddr, LocalServiceInfo) &item, mapLocalHost){
				if(item.first.IsRoutable()){
					snprintf(z_temp, 512, "%s:%d", item.first.ToString().c_str(), item.second.nPort);
					netaddr = z_temp;
					break;
				}
	        }
	    }
		if(!netaddr.length()){
			throw JSONRPCError(RPC_CLIENT_INVALID_IP_OR_SUBNET, "No Variable Ip/Port");
		}
		// step5: file modify (soom.conf)
	{
		boost::filesystem::path pathConfigFile(GetArg("-conf", BITCOIN_CONF_FILENAME));
	    if (!pathConfigFile.is_complete()){
	        pathConfigFile = GetDataDir(false) / pathConfigFile;
	    }
		boost::filesystem::path pathCopyFile(GetArg("-conf", "soom.conf_bak"));
	    if (!pathCopyFile.is_complete()){
	        pathCopyFile = GetDataDir(false) / pathCopyFile;
	    }
		ifstream fin;
		ofstream fout;
		fin.open(pathConfigFile.string());
		fout.open(pathCopyFile.string());
		std::string read_str;
		std::string except_str1 = "gateway=";
		std::string except_str2 = "gatewayprivkey=";
		char buf[1024] = {0};
		while(!fin.eof()){
			fin.getline(buf, 1024);
			read_str = buf;
			memset(buf, 0x00, strlen(buf));
			if((read_str.find(except_str1) == std::string::npos)&&(read_str.find(except_str2) == std::string::npos)){
				fout << read_str << std::endl;
			}
		}
		std::string gw = "gateway=1";
		fout << gw << std::endl;
		std::string gwprivkey = "gatewayprivkey=";
		gwprivkey += genkey;
		fout << gwprivkey << std::endl;
		fin.close();
		fout.close();
		std::string rm_str = "rm -rf ";
		rm_str += pathConfigFile.string();
		(void)system(rm_str.c_str());
		std::string mv_str = "mv ";
		mv_str += pathCopyFile.string();
		mv_str += " ";
		mv_str += pathConfigFile.string();
		(void)system(mv_str.c_str());
	}
		// step6: file modify (gateway.conf)
	{
		std::string gwinfo_str = account_name;
		gwinfo_str += " ";
		gwinfo_str += netaddr;
		gwinfo_str += " ";
		gwinfo_str += genkey;
		gwinfo_str += " ";
		gwinfo_str += collateral_str;
		boost::filesystem::path pathConfigFile(GetArg("-gwconf", "gateway.conf"));
		if (!pathConfigFile.is_complete()){
    		pathConfigFile = GetDataDir() / pathConfigFile;
		}
		boost::filesystem::path pathCopyFile(GetArg("-gwconf", "gateway.conf_bak"));
		if (!pathCopyFile.is_complete()){
    		pathCopyFile = GetDataDir() / pathCopyFile;
		}
		ifstream fin;
		ofstream fout;
		fin.open(pathConfigFile.string());
		fout.open(pathCopyFile.string());
		std::string read_str;
		std::string except_str = "# ";
		char buf[1024] = {0};
		while(!fin.eof())
		{
			fin.getline(buf, 1024);
			read_str = buf;
			memset(buf, 0x00, strlen(buf));
			if(read_str.find(except_str) != std::string::npos){
				fout << read_str << std::endl;
			}
		}
		fout << gwinfo_str << std::endl;
		fin.close();
		fout.close();
		std::string rm_str = "rm -rf ";
		rm_str += pathConfigFile.string();
		(void)system(rm_str.c_str());
		std::string mv_str = "mv ";
		mv_str += pathCopyFile.string();
		mv_str += " ";
		mv_str += pathConfigFile.string();
		(void)system(mv_str.c_str());
	}
		UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully Generate Gateway conf. %d", 1)));
		return returnObj;
	}

    return NullUniValue;
}

UniValue gatewaylist(const UniValue& params, bool fHelp)
{
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    if (fHelp || (
                strMode != "activeseconds" && strMode != "addr" && strMode != "full" && strMode != "info" &&
                strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
                strMode != "protocol" && strMode != "payee" && strMode != "pubkey" &&
                strMode != "rank" && strMode != "status"))
    {
        throw std::runtime_error(
                "gatewaylist ( \"mode\" \"filter\" )\n"
                "Get a list of gateways in different modes\n"
                "\nArguments:\n"
                "1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
                "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
                "                                    additional matches in some modes are also available\n"
                "\nAvailable modes:\n"
                "  activeseconds  - Print number of seconds gateway recognized by the network as enabled\n"
                "                   (since latest issued \"gateway start/start-many/start-alias\")\n"
                "  addr           - Print ip address associated with a gateway (can be additionally filtered, partial match)\n"
                "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  info           - Print info in format 'status protocol payee lastseen activeseconds IP'\n"
                "                   (can be additionally filtered, partial match)\n"
                "  lastpaidblock  - Print the last block height a node was paid on the network\n"
                "  lastpaidtime   - Print the last time a node was paid on the network\n"
                "  lastseen       - Print timestamp of when a gateway was last seen on the network\n"
                "  payee          - Print Soom address associated with a gateway (can be additionally filtered,\n"
                "                   partial match)\n"
                "  protocol       - Print protocol of a gateway (can be additionally filtered, exact match)\n"
                "  pubkey         - Print the gateway (not collateral) public key\n"
                "  rank           - Print rank of a gateway based on current block\n"
                "  status         - Print gateway status: PRE_ENABLED / ENABLED / EXPIRED / WATCHDOG_EXPIRED / NEW_START_REQUIRED /\n"
                "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
                );
    }

    if (strMode == "full" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
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
        BOOST_FOREACH(PAIRTYPE(int, CGateway)& s, vGatewayRanks) {
            std::string strOutpoint = s.second.vin.prevout.ToStringShort();
            if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, s.first));
        }
    } else {
        std::map<COutPoint, CGateway> mapGateways = gwnodeman.GetFullGatewayMap();
        for (auto& gwpair : mapGateways) {
            CGateway gw = gwpair.second;
            std::string strOutpoint = gwpair.first.ToStringShort();
            if (strMode == "activeseconds") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)(gw.lastPing.sigTime - gw.sigTime)));
            } else if (strMode == "addr") {
                std::string strAddress = gw.addr.ToString();
                if (strFilter !="" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strAddress));
            } else if (strMode == "full") {
                std::ostringstream streamFull;
                streamFull << std::setw(18) <<
                               gw.GetStatus() << " " <<
                               gw.nProtocolVersion << " " <<
                               CBitcoinAddress(gw.pubKeyCollateralAddress.GetID()).ToString() << " " <<
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
                               CBitcoinAddress(gw.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                               (int64_t)gw.lastPing.sigTime << " " << std::setw(8) <<
                               (int64_t)(gw.lastPing.sigTime - gw.sigTime) << " " <<                               
                               gw.addr.ToString();
                std::string strInfo = streamInfo.str();
                if (strFilter !="" && strInfo.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strInfo));
            } else if (strMode == "lastpaidblock") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, gw.GetLastPaidBlock()));
            } else if (strMode == "lastpaidtime") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, gw.GetLastPaidTime()));
            } else if (strMode == "lastseen") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)gw.lastPing.sigTime));
            } else if (strMode == "payee") {
                CBitcoinAddress address(gw.pubKeyCollateralAddress.GetID());
                std::string strPayee = address.ToString();
                if (strFilter !="" && strPayee.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strPayee));
            } else if (strMode == "protocol") {
                if (strFilter !="" && strFilter != strprintf("%d", gw.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)gw.nProtocolVersion));
            } else if (strMode == "pubkey") {
                if (strFilter !="" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, HexStr(gw.pubKeyGateway)));
            } else if (strMode == "status") {
                std::string strStatus = gw.GetStatus();
                if (strFilter !="" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, strStatus));
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
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

UniValue gatewaybroadcast(const UniValue& params, bool fHelp)
{
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp  ||
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
    if (strCommand == "create-alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        bool fFound = false;
        std::string strAlias = params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        std::vector<CGatewayBroadcast> vecGwb;

        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
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
                    statusObj.push_back(Pair("hex", HexStr(ssVecGwb.begin(), ssVecGwb.end())));
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

    if (strCommand == "create-all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        // hcdo 180625 remove not used 
        //std::vector<CGatewayConfig::CGatewayEntry> gwEntries;
        //gwEntries = gatewayConfig.getEntries();

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        std::vector<CGatewayBroadcast> vecGwb;

        BOOST_FOREACH(CGatewayConfig::CGatewayEntry gwe, gatewayConfig.getEntries()) {
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

    if (strCommand == "decode")
    {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'gatewaybroadcast decode \"hexstring\"'");

        std::vector<CGatewayBroadcast> vecGwb;

        if (!DecodeHexVecGwb(vecGwb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Gateway broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);

        BOOST_FOREACH(CGatewayBroadcast& gwb, vecGwb) {
            UniValue resultObj(UniValue::VOBJ);

            if(gwb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.push_back(Pair("outpoint", gwb.vin.prevout.ToStringShort()));
                resultObj.push_back(Pair("addr", gwb.addr.ToString()));
                resultObj.push_back(Pair("pubKeyCollateralAddress", CBitcoinAddress(gwb.pubKeyCollateralAddress.GetID()).ToString()));
                resultObj.push_back(Pair("pubKeyGateway", CBitcoinAddress(gwb.pubKeyGateway.GetID()).ToString()));
                resultObj.push_back(Pair("vchSig", EncodeBase64(&gwb.vchSig[0], gwb.vchSig.size())));
                resultObj.push_back(Pair("sigTime", gwb.sigTime));
                resultObj.push_back(Pair("protocolVersion", gwb.nProtocolVersion));

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.push_back(Pair("outpoint", gwb.lastPing.vin.prevout.ToStringShort()));
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

    if (strCommand == "relay")
    {
        if (params.size() < 2 || params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER,   "gatewaybroadcast relay \"hexstring\" ( fast )\n"
                                                        "\nArguments:\n"
                                                        "1. \"hex\"      (string, required) Broadcast messages hex string\n"
                                                        "2. fast       (string, optional) If none, using safe method\n");

        std::vector<CGatewayBroadcast> vecGwb;

        if (!DecodeHexVecGwb(vecGwb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Gateway broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        bool fSafe = params.size() == 2;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        BOOST_FOREACH(CGatewayBroadcast& gwb, vecGwb) {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("outpoint", gwb.vin.prevout.ToStringShort()));
            resultObj.push_back(Pair("addr", gwb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (gwb.CheckSignature(nDos)) {
                if (fSafe) {
                    fResult = gwnodeman.CheckGwbAndUpdateGatewayList(NULL, gwb, nDos, *g_connman);
                } else {
                    gwnodeman.UpdateGatewayList(gwb, *g_connman);
                    gwb.Relay(*g_connman);
                    fResult = true;
                }
          
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

