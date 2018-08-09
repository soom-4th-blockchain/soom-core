// Copyright (c) 2014-2017 The Dash Core developers 
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "netbase.h"
#include "gatewayconfig.h"
#include "util.h"
#include "chainparams.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

CGatewayConfig gatewayConfig;

void CGatewayConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex) {
    CGatewayEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CGatewayConfig::read(std::string& strErr) {
    int linenumber = 1;
    boost::filesystem::path pathGatewayConfigFile = GetGatewayConfigFile();
    boost::filesystem::ifstream streamConfig(pathGatewayConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathGatewayConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Gateway config file\n"
                          "# Format: alias IP:port gatewayprivkey collateral_output_txid collateral_output_index\n"
                          "# Example: gw1 127.0.0.2:16999 94HbYCVUCYjEMeeK1Y3sCDLBMDZE1Cd1E46xhpgX12tGCDWL8Xg 2afd3d74f84f87aeg36b4a53567c91235a04f6e1832g3s0b84e0f0162343e64c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for(std::string line; std::getline(streamConfig, line); linenumber++)
    {
        if(line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if(comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse gateway.conf") + "\n" +
                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        int port = 0;
        std::string hostname = "";
        SplitHostPort(ip, port, hostname);
        if(port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string") + "\n"+
                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }
		
      	if(!fLocalGateWay)
    	{
		    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
	        if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
	            if(port != mainnetDefaultPort) {
	                strErr = _("Invalid port detected in gateway.conf") + "\n" +
	                        strprintf(_("Port: %d"), port) + "\n" +
	                        strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
	                        strprintf(_("(must be %d for mainnet)"), mainnetDefaultPort);
	                streamConfig.close();
	                return false;
	            }
	        } else if(port == mainnetDefaultPort) {
	            strErr = _("Invalid port detected in gateway.conf") + "\n" +
	                    strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
	                    strprintf(_("(%d could be used only on mainnet)"), mainnetDefaultPort);
	            streamConfig.close();
	            return false;
	        }
		}

        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}
