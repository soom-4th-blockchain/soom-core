// Copyright (c) 2014-2017 The Dash Core developers
// Copyright (c) 2017-2018 The Soom Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SRC_GATEWAYCONFIG_H_
#define SRC_GATEWAYCONFIG_H_

class CGatewayConfig;
extern CGatewayConfig gatewayConfig;

class CGatewayConfig
{

public:

    class CGatewayEntry {

    private:
        std::string alias;
        std::string ip;
        std::string privKey;
        std::string txHash;
        std::string outputIndex;
    public:

        CGatewayEntry(const std::string& alias, const std::string& ip, const std::string& privKey, const std::string& txHash, const std::string& outputIndex) {
            this->alias = alias;
            this->ip = ip;
            this->privKey = privKey;
            this->txHash = txHash;
            this->outputIndex = outputIndex;
        }

        const std::string& getAlias() const {
            return alias;
        }

        const std::string& getOutputIndex() const {
            return outputIndex;
        }

        const std::string& getPrivKey() const {
            return privKey;
        }

        const std::string& getTxHash() const {
            return txHash;
        }

        const std::string& getIp() const {
            return ip;
        }
    };

    CGatewayConfig() {
        entries = std::vector<CGatewayEntry>();
    }

    void clear();
    bool read(std::string& strErrRet);
    void add(const std::string& alias, const std::string& ip, const std::string& privKey, const std::string& txHash, const std::string& outputIndex);

    std::vector<CGatewayEntry>& getEntries() {
        return entries;
    }

    int getCount() {
        return (int)entries.size();
    }

private:
    std::vector<CGatewayEntry> entries;


};


#endif /* SRC_GATEWAYCONFIG_H_ */
