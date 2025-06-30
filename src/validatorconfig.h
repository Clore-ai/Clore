// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_VALIDATORCONFIG_H
#define CLORE_VALIDATORCONFIG_H

#include "sync.h"
#include <string>
#include <vector>

class CValidatorConfig;
extern CValidatorConfig validatorConfig;

class CValidatorConfig
{
public:
    class CValidatorEntry
    {
    private:
        std::string alias;
        std::string ip;
        std::string txHash;
        std::string outputIndex;

    public:
        CValidatorEntry(std::string& _alias, std::string& _ip, std::string& _txHash, std::string& _outputIndex) : alias(_alias), ip(_ip), txHash(_txHash), outputIndex(_outputIndex) {}

        const std::string& getAlias() const { return alias; }
        const std::string& getOutputIndex() const { return outputIndex; }
        bool castOutputIndex(int& n) const;
        const std::string& getTxHash() const { return txHash; }
        const std::string& getIp() const { return ip; }
    };

    CValidatorConfig() { entries = std::vector<CValidatorEntry>(); }

    void clear()
    {
        LOCK(cs_entries);
        entries.clear();
    }
    bool read(std::string& strErr);
    CValidatorConfig::CValidatorEntry* add(std::string alias, std::string ip, std::string txHash, std::string outputIndex);
    void remove(std::string alias);

    std::vector<CValidatorEntry> getEntries()
    {
        LOCK(cs_entries);
        return entries;
    }

    int getCount()
    {
        LOCK(cs_entries);
        int c = -1;
        for (const auto& e : entries) {
            if (!e.getAlias().empty()) c++;
        }
        return c;
    }

private:
    std::vector<CValidatorEntry> entries;
    CCriticalSection cs_entries;
};


#endif // CLORE_VALIDATORCONFIG_H