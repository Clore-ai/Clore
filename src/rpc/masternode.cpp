// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"
#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "init.h"
#include "key.h"
#include "masternodeconfig.h"
#include "net.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "wallet/wallet.h"

#include <univalue.h>

UniValue createmasternodekey(const JSONRPCRequest& request)
{
    if (request.fHelp || !request.params.empty())
        throw std::runtime_error(
            "createmasternodekey\n"
            "\nCreate a new masternode private key\n"

            "\nResult:\n"
            "\"key\"                (string) Masternode private key\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodekey", "") + HelpExampleRpc("createmasternodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    CCloreSecret cloreSecret(secret);
    return cloreSecret.ToString();
}

UniValue listmasternodeconf(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listmasternodeconf ( \"filter\" )\n"
            "\nPrint masternode.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by alias, address, txHash.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) masternode alias\n"
            "    \"address\": \"xxxx\",      (string) masternode IP address\n"
            "    \"privateKey\": \"xxxx\",   (string) masternode private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,         (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) masternode status\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodeconf", "") + HelpExampleRpc("listmasternodeconf", ""));

    std::string strFilter = "";
    if (request.params.size() >= 1) {
        strFilter = request.params[0].get_str();
    }

    std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
    mnEntries = masternodeConfig.getEntries();

    const CChainParams& chainparams = GetParams();
    UniValue ret(UniValue::VARR);

    for (const auto& mne : mnEntries) {
        // Apply filter if specified
        if (!strFilter.empty()) {
            if (mne.getAlias().find(strFilter) == std::string::npos &&
                mne.getIp().find(strFilter) == std::string::npos &&
                mne.getTxHash().find(strFilter) == std::string::npos) {
                continue;
            }
        }

        // Determine status based on authorization
        std::string status = "UNKNOWN";
        bool isAuthorized = chainparams.IsAuthorizedMasternodeAlias(mne.getAlias());

        if (chainparams.NetworkIDString() == "regtest") {
            status = "AUTHORIZED_REGTEST";
        } else if (isAuthorized) {
            status = "AUTHORIZED";
        } else {
            status = "UNAUTHORIZED";
        }

        // Convert output index
        int outputIndex = 0;
        mne.castOutputIndex(outputIndex);

        UniValue mnObj(UniValue::VOBJ);
        mnObj.pushKV("alias", mne.getAlias());
        mnObj.pushKV("address", mne.getIp());
        mnObj.pushKV("txHash", mne.getTxHash());
        mnObj.pushKV("outputIndex", outputIndex);
        mnObj.pushKV("status", status);
        mnObj.pushKV("authorized", isAuthorized || chainparams.NetworkIDString() == "regtest");
        ret.push_back(mnObj);
    }

    return ret;
}

UniValue getmasternodeoutputs(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmasternodeoutputs\n"
            "\nPrint all masternode transaction outputs\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n         (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodeoutputs", "") + HelpExampleRpc("getmasternodeoutputs", ""));

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    // Find collateral output candidates
    std::vector<COutput> vPossibleCoins;
    pwallet->AvailableCoins(vPossibleCoins, true, nullptr, 1, MAX_MONEY, MAX_MONEY, 0, 0, 9999999);

    UniValue ret(UniValue::VARR);
    for (const COutput& out : vPossibleCoins) {
        if (out.tx->tx->vout[out.i].nValue == 1000 * COIN) { // 1000 CLORE collateral
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("txhash", out.tx->GetHash().ToString());
            obj.pushKV("outputidx", out.i);
            ret.push_back(obj);
        }
    }

    return ret;
#else
    throw JSONRPCError(RPC_WALLET_ERROR, "Wallet functionality not available");
#endif
}

UniValue getmasternodecount(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 0))
        throw std::runtime_error(
            "getmasternodecount\n"
            "\nGet masternode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total masternodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"enabled\": n,      (numeric) Enabled masternodes\n"
            "  \"inqueue\": n       (numeric) Masternodes in queue\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));

    UniValue obj(UniValue::VOBJ);

    // TODO: Implement proper masternode counting when infrastructure is ready
    // For now, return zero counts
    obj.pushKV("total", 0);
    obj.pushKV("stable", 0);
    obj.pushKV("enabled", 0);
    obj.pushKV("inqueue", 0);

    return obj;
}

UniValue getmasternodestatus(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getmasternodestatus\n"
            "\nPrint masternode status\n"

            "\nResult:\n"
            "{\n"
            "  \"outpoint\": \"xxxx\",      (string) Collateral transaction output\n"
            "  \"service\": \"xxxx\",       (string) Masternode network address\n"
            "  \"pubkey\": \"xxxx\",        (string) Masternode public key\n"
            "  \"status\": \"xxxx\",        (string) Masternode status\n"
            "  \"message\": \"xxxx\"        (string) Masternode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

    if (!fMasternodeMode)
        throw JSONRPCError(RPC_MISC_ERROR, "This is not a masternode");

    // TODO: Implement proper masternode status when infrastructure is ready
    UniValue mnObj(UniValue::VOBJ);
    mnObj.pushKV("outpoint", "Not implemented");
    mnObj.pushKV("service", "Not implemented");
    mnObj.pushKV("pubkey", "Not implemented");
    mnObj.pushKV("status", "Not implemented");
    mnObj.pushKV("message", "Masternode infrastructure not yet implemented");

    return mnObj;
}

UniValue masternodecurrent(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "masternodecurrent\n"
            "\nGet current masternode winner (scheduled to be paid next).\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"outpoint\": \"xxxx\",    (string) Collateral transaction output\n"
            "  \"pubkey\": \"xxxx\",      (string) MN Public key\n"
            "  \"status\": \"xxxx\"       (string) Masternode status\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodecurrent", "") + HelpExampleRpc("masternodecurrent", ""));

    // TODO: Implement proper payment queue logic when infrastructure is ready
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("protocol", PROTOCOL_VERSION);
    obj.pushKV("outpoint", "Not implemented");
    obj.pushKV("pubkey", "Not implemented");
    obj.pushKV("status", "Payment queue not implemented");

    return obj;
}

// Enhanced masternodelist to replace the basic stub
static UniValue listmasternodes(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listmasternodes ( \"filter\" )\n"
            "\nGet a ranked list of masternodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,                             (numeric) Masternode Rank (or 0 if not enabled)\n"
            "    \"outpoint\": \"xxxx\",                  (string) Collateral transaction output\n"
            "    \"pubkey\": \"xxxx\",                    (string) Masternode public key\n"
            "    \"status\": \"xxxx\",                    (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"xxxx\",                      (string) Masternode CLORE address\n"
            "    \"version\": v,                          (numeric) Masternode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodes", "") + HelpExampleRpc("listmasternodes", ""));

    // TODO: Implement proper masternode listing when infrastructure is ready
    // For now, return empty array
    UniValue ret(UniValue::VARR);
    return ret;
}

UniValue createmasternodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 5))
        throw std::runtime_error(
            "createmasternodebroadcast \"alias\" \"service\" \"keyCollateral\" \"txHash\" outputIndex\n"
            "\nCreates a masternode broadcast message for the given masternode.\n"

            "\nArguments:\n"
            "1. \"alias\"         (string, required) Alias name for the masternode\n"
            "2. \"service\"       (string, required) '<ip>:<port>' The masternode service location\n"
            "3. \"keyCollateral\" (string, required) The collateral address private key\n"
            "4. \"txHash\"        (string, required) Transaction hash for the collateral\n"
            "5. outputIndex       (numeric, required) Output index for the collateral\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",       (string) Alias name\n"
            "  \"result\": \"xxxx\",      (string) Result message\n"
            "  \"hex\": \"xxxx\"          (string) Hex encoded broadcast (if successful)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodebroadcast", "\"mn1\" \"127.0.0.1:9999\" \"privkey\" \"txhash\" 0") +
            HelpExampleRpc("createmasternodebroadcast", "\"mn1\", \"127.0.0.1:9999\", \"privkey\", \"txhash\", 0"));

    std::string alias = request.params[0].get_str();
    std::string service = request.params[1].get_str();
    std::string keyCollateral = request.params[2].get_str(); // Currently unused - for future implementation
    std::string txHash = request.params[3].get_str();
    /*int outputIndex = */ request.params[4].get_int(); // Currently unused

    // Check if this masternode is authorized via collateral address
    // Extract address from private key to check authorization
    std::string collateralAddress = ""; // TODO: Extract from keyCollateral when implemented

    const CChainParams& chainparams = GetParams();
    bool isAuthorized = false;

    // For now, check by alias since we don't have key->address conversion implemented
    isAuthorized = chainparams.IsAuthorizedMasternodeAlias(alias);

    UniValue statusObj(UniValue::VOBJ);
    statusObj.pushKV("alias", alias);
    statusObj.pushKV("service", service);
    statusObj.pushKV("authorized", isAuthorized);

    if (!isAuthorized && chainparams.NetworkIDString() != "regtest") {
        statusObj.pushKV("result", "ERROR: Masternode not authorized on this network");
        statusObj.pushKV("error", "This masternode alias is not in the authorized list for " + chainparams.NetworkIDString());
    } else {
        statusObj.pushKV("result", "Masternode broadcast creation not yet implemented (but authorization passed)");
    }

    statusObj.pushKV("hex", "");
    return statusObj;
}

UniValue decodemasternodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodemasternodebroadcast \"hexstring\"\n"
            "\nDecode masternode broadcast message\n"

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The hex encoded masternode broadcast\n"

            "\nResult:\n"
            "{\n"
            "  \"outpoint\": \"xxxx\",    (string) The masternode outpoint\n"
            "  \"addr\": \"xxxx\",        (string) The masternode address\n"
            "  \"pubkey\": \"xxxx\",      (string) The masternode public key\n"
            "  \"vchSig\": \"xxxx\",      (string) The signature\n"
            "  \"sigTime\": nnn,          (numeric) The signature time\n"
            "  \"protocolVersion\": nnn   (numeric) The protocol version\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodemasternodebroadcast", "\"hexstring\"") +
            HelpExampleRpc("decodemasternodebroadcast", "\"hexstring\""));

    // TODO: Implement proper broadcast decoding when infrastructure is ready
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("outpoint", "Decoding not implemented");
    obj.pushKV("addr", "");
    obj.pushKV("pubkey", "");
    obj.pushKV("vchSig", "");
    obj.pushKV("sigTime", 0);
    obj.pushKV("protocolVersion", PROTOCOL_VERSION);

    return obj;
}

UniValue getmasternodescores(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getmasternodescores ( blocks )\n"
            "\nPrint list of winning masternode by score\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

            "\nResult:\n"
            "{\n"
            "  \"nHeight\": n,           (numeric) Current block height\n"
            "  \"scores\": [             (array) Masternode scores\n"
            "    {\n"
            "      \"outpoint\": \"xxxx\",   (string) Masternode outpoint\n"
            "      \"score\": \"xxxx\",      (string) Masternode score\n"
            "      \"addr\": \"xxxx\"        (string) Masternode address\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodescores", "") + HelpExampleRpc("getmasternodescores", ""));

    /*int nBlocks = 10;
    if (request.params.size() > 0) {
        nBlocks = request.params[0].get_int();
    }*/

    // TODO: Implement proper masternode scoring when infrastructure is ready
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("nHeight", chainActive.Height());

    UniValue scoresArray(UniValue::VARR);
    obj.pushKV("scores", scoresArray);

    return obj;
}

UniValue getmasternodewinners(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getmasternodewinners ( blocks \"filter\" )\n"
            "\nPrint the masternode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"
            "2. \"filter\"    (string, optional) Search filter matching MN address\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"outpoint\": \"xxxx\",   (string) masternode outpoint\n"
            "      \"score\": \"xxxx\",      (string) masternode score\n"
            "      \"addr\": \"xxxx\"        (string) masternode address\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodewinners", "") + HelpExampleRpc("getmasternodewinners", ""));

    /*int nBlocks = 10;
    std::string strFilter = "";

    if (request.params.size() >= 1) {
        nBlocks = request.params[0].get_int();
    }
    if (request.params.size() >= 2) {
        strFilter = request.params[1].get_str();
    }*/

    // TODO: Implement proper payment winner logic when infrastructure is ready
    UniValue ret(UniValue::VARR);
    return ret;
}

UniValue initmasternode(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 2))
        throw std::runtime_error(
            "initmasternode \"privkey\" \"address\"\n"
            "\nInitialize masternode on this wallet\n"

            "\nArguments:\n"
            "1. \"privkey\"     (string, required) The masternode private key\n"
            "2. \"address\"     (string, required) The IP:port of the masternode\n"

            "\nResult:\n"
            "\"status\"         (string) Masternode initialization status\n"

            "\nExamples:\n" +
            HelpExampleCli("initmasternode", "\"privkey\" \"addr:port\"") +
            HelpExampleRpc("initmasternode", "\"privkey\", \"addr:port\""));

    // TODO: Implement proper masternode initialization when infrastructure is ready
    return "Masternode initialization not yet implemented";
}

UniValue relaymasternodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "relaymasternodebroadcast \"hexstring\"\n"
            "\nCommand to relay masternode broadcast messages\n"

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The hex encoded masternode broadcast message\n"

            "\nResult:\n"
            "\"status\"           (string) Relay status\n"

            "\nExamples:\n" +
            HelpExampleCli("relaymasternodebroadcast", "\"hexstring\"") +
            HelpExampleRpc("relaymasternodebroadcast", "\"hexstring\""));

    // TODO: Implement proper broadcast relay when infrastructure is ready
    return "Masternode broadcast relay not yet implemented";
}

UniValue listauthorizedmasternodes(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "listauthorizedmasternodes ( \"filter\" )\n"
            "\nList all authorized masternodes for this network\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by alias, address, or description.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) masternode alias\n"
            "    \"pubkeyAddress\": \"xxxx\", (string) authorized CLORE address\n"
            "    \"description\": \"xxxx\",   (string) description/notes\n"
            "    \"network\": \"xxxx\"       (string) network (main/test/regtest)\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listauthorizedmasternodes", "") + HelpExampleRpc("listauthorizedmasternodes", ""));

    std::string strFilter = "";
    if (request.params.size() >= 1) {
        strFilter = request.params[0].get_str();
    }

    const CChainParams& chainparams = GetParams();
    const std::vector<CChainParams::AuthorizedMasternode>& authorizedMNs = chainparams.GetAuthorizedMasternodes();

    UniValue ret(UniValue::VARR);

    for (const auto& mn : authorizedMNs) {
        // Apply filter if specified
        if (!strFilter.empty()) {
            if (mn.alias.find(strFilter) == std::string::npos &&
                mn.pubkeyAddress.find(strFilter) == std::string::npos &&
                mn.description.find(strFilter) == std::string::npos) {
                continue;
            }
        }

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("alias", mn.alias);
        obj.pushKV("pubkeyAddress", mn.pubkeyAddress);
        obj.pushKV("description", mn.description);
        obj.pushKV("network", chainparams.NetworkIDString());
        ret.push_back(obj);
    }

    return ret;
}

UniValue checkmasternodeauth(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "checkmasternodeauth \"type\" \"value\"\n"
            "\nCheck if a masternode is authorized on this network\n"

            "\nArguments:\n"
            "1. \"type\"      (string, required) Type of check: \"alias\" or \"address\"\n"
            "2. \"value\"     (string, required) Value to check (alias or CLORE address)\n"

            "\nResult:\n"
            "{\n"
            "  \"authorized\": true|false,    (boolean) Whether the masternode is authorized\n"
            "  \"type\": \"xxxx\",            (string) Type of check performed\n"
            "  \"value\": \"xxxx\",           (string) Value that was checked\n"
            "  \"network\": \"xxxx\"          (string) Current network\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("checkmasternodeauth", "\"alias\" \"clore-mn-01\"") +
            HelpExampleCli("checkmasternodeauth", "\"address\" \"ATsQHm7qbMSe4gJnx8W5SnLNP9bKLmgD52\"") +
            HelpExampleRpc("checkmasternodeauth", "\"address\", \"ATsQHm7qbMSe4gJnx8W5SnLNP9bKLmgD52\""));

    std::string strType = request.params[0].get_str();
    std::string strValue = request.params[1].get_str();

    const CChainParams& chainparams = GetParams();
    bool fAuthorized = false;

    if (strType == "alias") {
        fAuthorized = chainparams.IsAuthorizedMasternodeAlias(strValue);
    } else if (strType == "address") {
        fAuthorized = chainparams.IsAuthorizedMasternodeAddress(strValue);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid type. Use 'alias' or 'address'");
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("authorized", fAuthorized);
    ret.pushKV("type", strType);
    ret.pushKV("value", strValue);
    ret.pushKV("network", chainparams.NetworkIDString());

    return ret;
}

UniValue createmasternodeconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "createmasternodeconfig \"alias\" \"address\" \"collateraltxid\" ( outputindex )\n"
            "\nCreate a complete masternode configuration entry\n"

            "\nArguments:\n"
            "1. \"alias\"         (string, required) Unique alias for the masternode\n"
            "2. \"address\"       (string, required) IP:port for the masternode service\n"
            "3. \"collateraltxid\" (string, required) Transaction ID of the 1000 CLORE collateral\n"
            "4. outputindex       (numeric, optional) Output index of the collateral (default: 0)\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",           (string) Masternode alias\n"
            "  \"address\": \"xxxx\",         (string) Masternode IP:port\n"
            "  \"privateKey\": \"xxxx\",      (string) Generated masternode private key\n"
            "  \"collateralTxId\": \"xxxx\",  (string) Collateral transaction ID\n"
            "  \"outputIndex\": n,            (numeric) Collateral output index\n"
            "  \"authorized\": true|false,    (boolean) Whether this masternode is authorized\n"
            "  \"configLine\": \"xxxx\",      (string) Line to add to masternode.conf\n"
            "  \"status\": \"xxxx\"           (string) Creation status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodeconfig", "\"mn1\" \"127.0.0.1:8788\" \"abc123def456...\" 0") +
            HelpExampleRpc("createmasternodeconfig", "\"mn1\", \"127.0.0.1:8788\", \"abc123def456...\", 0"));

    std::string alias = request.params[0].get_str();
    std::string address = request.params[1].get_str();
    std::string collateralTxId = request.params[2].get_str();
    int outputIndex = (request.params.size() > 3) ? request.params[3].get_int() : 0;

    const CChainParams& chainparams = GetParams();
    UniValue result(UniValue::VOBJ);

    // 1. Validate alias uniqueness
    std::vector<CMasternodeConfig::CMasternodeEntry> existingEntries = masternodeConfig.getEntries();
    for (const auto& entry : existingEntries) {
        if (entry.getAlias() == alias) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode alias '" + alias + "' already exists in configuration");
        }
    }

    // 2. Validate address format (IP:port)
    size_t colonPos = address.find_last_of(":");
    if (colonPos == std::string::npos) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Address must be in format IP:port");
    }

    std::string hostname = address.substr(0, colonPos);
    int port = 0;
    try {
        port = std::stoi(address.substr(colonPos + 1));
    } catch (const std::exception& e) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid port number in address");
    }

    // 3. Validate port for network
    int nDefaultPort = chainparams.GetDefaultPort();
    if (chainparams.NetworkIDString() != "regtest" && port != nDefaultPort) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Port must be %d for %s network", nDefaultPort, chainparams.NetworkIDString()));
    }

    // 4. Check authorization
    bool isAuthorized = chainparams.IsAuthorizedMasternodeAlias(alias);
    if (chainparams.NetworkIDString() != "regtest" && !isAuthorized) {
        result.pushKV("authorized", false);
        result.pushKV("status", "ERROR: Masternode alias not authorized for " + chainparams.NetworkIDString() + " network");
        result.pushKV("alias", alias);
        return result;
    }

    // 5. Generate new masternode private key
    CKey secret;
    secret.MakeNewKey(false);
    CCloreSecret cloreSecret(secret);
    std::string privateKey = cloreSecret.ToString();

#ifdef ENABLE_WALLET
    // 6. Validate collateral if wallet is available
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (EnsureWalletIsAvailable(pwallet, false)) {
        uint256 txid;
        txid.SetHex(collateralTxId);

        // Check if transaction exists and has correct output
        const CWalletTx* wtx = pwallet->GetWalletTx(txid);
        if (wtx) {
            if (outputIndex >= 0 && outputIndex < (int)wtx->tx->vout.size()) {
                CAmount nValue = wtx->tx->vout[outputIndex].nValue;
                if (nValue == 1000 * COIN) {
                    result.pushKV("collateralValid", true);
                } else {
                    result.pushKV("collateralValid", false);
                    result.pushKV("collateralAmount", (double)nValue / COIN);
                    result.pushKV("warning", "Collateral amount is not exactly 1000 CLORE");
                }
            } else {
                result.pushKV("collateralValid", false);
                result.pushKV("warning", "Invalid output index for collateral transaction");
            }
        } else {
            result.pushKV("collateralValid", false);
            result.pushKV("warning", "Collateral transaction not found in wallet");
        }
    }
#endif

    // 7. Create the configuration line
    std::string configLine = alias + " " + address + " " + privateKey + " " + collateralTxId + " " + std::to_string(outputIndex);

    // 8. Add to masternode configuration (in memory)
    try {
        masternodeConfig.add(alias, address, collateralTxId, std::to_string(outputIndex));
        result.pushKV("addedToConfig", true);
    } catch (const std::exception& e) {
        result.pushKV("addedToConfig", false);
        result.pushKV("configError", e.what());
    }

    // 9. Build result
    result.pushKV("alias", alias);
    result.pushKV("address", address);
    result.pushKV("privateKey", privateKey);
    result.pushKV("collateralTxId", collateralTxId);
    result.pushKV("outputIndex", outputIndex);
    result.pushKV("authorized", isAuthorized || chainparams.NetworkIDString() == "regtest");
    result.pushKV("configLine", configLine);
    result.pushKV("status", "Masternode configuration created successfully");
    result.pushKV("network", chainparams.NetworkIDString());

    return result;
}

UniValue addmasternodeconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 5)
        throw std::runtime_error(
            "addmasternodeconfig \"alias\" \"address\" \"privatekey\" \"collateraltxid\" outputindex\n"
            "\nAdd a masternode configuration entry manually\n"

            "\nArguments:\n"
            "1. \"alias\"         (string, required) Masternode alias\n"
            "2. \"address\"       (string, required) IP:port for the masternode\n"
            "3. \"privatekey\"    (string, required) Masternode private key\n"
            "4. \"collateraltxid\" (string, required) Collateral transaction ID\n"
            "5. outputindex       (numeric, required) Collateral output index\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",           (string) Masternode alias\n"
            "  \"added\": true|false,         (boolean) Whether the entry was added\n"
            "  \"authorized\": true|false,    (boolean) Whether this masternode is authorized\n"
            "  \"status\": \"xxxx\"           (string) Status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("addmasternodeconfig", "\"mn1\" \"127.0.0.1:8788\" \"93HaYBV...\" \"abc123...\" 0") +
            HelpExampleRpc("addmasternodeconfig", "\"mn1\", \"127.0.0.1:8788\", \"93HaYBV...\", \"abc123...\", 0"));

    std::string alias = request.params[0].get_str();
    std::string address = request.params[1].get_str();
    std::string privateKey = request.params[2].get_str();
    std::string collateralTxId = request.params[3].get_str();
    std::string outputIndexStr = std::to_string(request.params[4].get_int());

    const CChainParams& chainparams = GetParams();
    UniValue result(UniValue::VOBJ);

    // Check authorization
    bool isAuthorized = chainparams.IsAuthorizedMasternodeAlias(alias);

    if (chainparams.NetworkIDString() != "regtest" && !isAuthorized) {
        result.pushKV("alias", alias);
        result.pushKV("added", false);
        result.pushKV("authorized", false);
        result.pushKV("status", "ERROR: Masternode alias not authorized for " + chainparams.NetworkIDString() + " network");
        return result;
    }

    // Add to configuration (note: privateKey is no longer stored in configuration)
    try {
        masternodeConfig.add(alias, address, collateralTxId, outputIndexStr);
        result.pushKV("alias", alias);
        result.pushKV("added", true);
        result.pushKV("authorized", isAuthorized || chainparams.NetworkIDString() == "regtest");
        result.pushKV("status", "Masternode configuration added successfully");
    } catch (const std::exception& e) {
        result.pushKV("alias", alias);
        result.pushKV("added", false);
        result.pushKV("authorized", isAuthorized);
        result.pushKV("status", "ERROR: " + std::string(e.what()));
    }

    return result;
}

UniValue removemasternodeconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "removemasternodeconfig \"alias\"\n"
            "\nRemove a masternode configuration entry\n"

            "\nArguments:\n"
            "1. \"alias\"    (string, required) Masternode alias to remove\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",      (string) Masternode alias\n"
            "  \"removed\": true|false,  (boolean) Whether the entry was removed\n"
            "  \"status\": \"xxxx\"      (string) Status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("removemasternodeconfig", "\"mn1\"") +
            HelpExampleRpc("removemasternodeconfig", "\"mn1\""));

    std::string alias = request.params[0].get_str();
    UniValue result(UniValue::VOBJ);

    try {
        masternodeConfig.remove(alias);
        result.pushKV("alias", alias);
        result.pushKV("removed", true);
        result.pushKV("status", "Masternode configuration removed successfully");
    } catch (const std::exception& e) {
        result.pushKV("alias", alias);
        result.pushKV("removed", false);
        result.pushKV("status", "ERROR: " + std::string(e.what()));
    }

    return result;
}

UniValue startmasternode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "startmasternode \"set\" ( \"lockWallet\" ) ( \"alias\" )\n"
            "\nAttempts to start one or more masternode(s)\n"

            "\nArguments:\n"
            "1. \"set\"         (string, required) Specify which set of masternode(s) to start.\n"
            "2. lockWallet      (boolean, optional) Lock wallet after completion.\n"
            "3. \"alias\"       (string, optional) Masternode alias. Required if set is \"alias\"\n"

            "\nResult: (for set = \"all\", \"missing\" or \"disabled\"):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [              (array) Details about each started masternode\n"
            "    {\n"
            "      \"alias\": \"xxxx\",      (string) Alias of the masternode\n"
            "      \"result\": \"xxxx\",     (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"       (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"
            "Result: (for set = \"alias\"):\n"
            "{\n"
            "  \"alias\": \"xxxx\",       (string) Alias of the masternode\n"
            "  \"result\": \"xxxx\",      (string) 'success' or 'failed'\n"
            "  \"error\": \"xxxx\"        (string) Error message, if failed\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("startmasternode", "\"alias\" false \"my_mn\"") +
            HelpExampleRpc("startmasternode", "\"alias\", false, \"my_mn\""));

    std::string strCommand = request.params[0].get_str();

    if (strCommand == "alias") {
        if (request.params.size() < 3) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");
        }

        std::string strAlias = request.params[2].get_str();

        UniValue statusObj(UniValue::VOBJ);
        statusObj.pushKV("alias", strAlias);
        statusObj.pushKV("result", "failed");
        statusObj.pushKV("error", "Masternode starting not yet implemented");

        return statusObj;
    } else if (strCommand == "all" || strCommand == "missing" || strCommand == "disabled") {
        UniValue resultsObj(UniValue::VOBJ);
        resultsObj.pushKV("overall", "Failed to start any masternodes. Implementation not ready.");

        UniValue detailsArray(UniValue::VARR);
        resultsObj.pushKV("detail", detailsArray);

        return resultsObj;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid set specified, please use one of the following: 'all', 'missing', 'disabled' or 'alias'");
    }
}

// Basic masternode command dispatcher (keeping compatibility)
static UniValue masternode(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "masternode \"command\" ...\n"
            "\nMasternode control commands.\n"
            "\nArguments:\n"
            "1. command     (string, required) The command to execute\n"
            "\nAvailable commands:\n"
            "  status       - Get masternode status\n"
            "  count        - Get masternode count\n"
            "\nExamples:\n" +
            HelpExampleCli("masternode", "\"status\"") + HelpExampleRpc("masternode", "\"status\""));

    std::string strCommand = request.params[0].get_str();

    if (strCommand == "status") {
        return getmasternodestatus(request);
    } else if (strCommand == "count") {
        return getmasternodecount(request);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown command: " + strCommand);
    }
}

static const CRPCCommand commands[] =
    {
        //  category              name                      actor (function)         argNames
        //  --------------------- ------------------------  -----------------------  ----------
        {"masternode", "createmasternodebroadcast", &createmasternodebroadcast, {"alias", "service", "keyCollateral", "txHash", "outputIndex"}},
        {"masternode", "createmasternodekey", &createmasternodekey, {}},
        {"masternode", "decodemasternodebroadcast", &decodemasternodebroadcast, {"hexstring"}},
        {"masternode", "getmasternodecount", &getmasternodecount, {}},
        {"masternode", "getmasternodeoutputs", &getmasternodeoutputs, {}},
        {"masternode", "getmasternodescores", &getmasternodescores, {"blocks"}},
        {"masternode", "getmasternodestatus", &getmasternodestatus, {}},
        {"masternode", "getmasternodewinners", &getmasternodewinners, {"blocks", "filter"}},
        {"masternode", "initmasternode", &initmasternode, {"privkey", "address"}},
        {"masternode", "listmasternodeconf", &listmasternodeconf, {"filter"}},
        {"masternode", "listmasternodes", &listmasternodes, {"filter"}},
        {"masternode", "masternode", &masternode, {"command"}},
        {"masternode", "masternodecurrent", &masternodecurrent, {}},
        {"masternode", "relaymasternodebroadcast", &relaymasternodebroadcast, {"hexstring"}},
        {"masternode", "startmasternode", &startmasternode, {"set", "lockWallet", "alias"}},
        {"masternode", "listauthorizedmasternodes", &listauthorizedmasternodes, {"filter"}},
        {"masternode", "checkmasternodeauth", &checkmasternodeauth, {"type", "value"}},
        {"masternode", "createmasternodeconfig", &createmasternodeconfig, {"alias", "address", "collateraltxid", "outputindex"}},
        {"masternode", "addmasternodeconfig", &addmasternodeconfig, {"alias", "address", "privatekey", "collateraltxid", "outputindex"}},
        {"masternode", "removemasternodeconfig", &removemasternodeconfig, {"alias"}},
};

void RegisterMasternodeRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}