// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "init.h"
#include "key.h"
#include "net.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "validator.h"
#include "validatorconfig.h"
#include "wallet/wallet.h"

#include <univalue.h>

UniValue createvalidatorkey(const JSONRPCRequest& request)
{
    if (request.fHelp || !request.params.empty())
        throw std::runtime_error(
            "createvalidatorkey\n"
            "\nCreate a new validator private key\n"

            "\nResult:\n"
            "\"key\"                (string) Validator private key\n"

            "\nExamples:\n" +
            HelpExampleCli("createvalidatorkey", "") + HelpExampleRpc("createvalidatorkey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    CCloreSecret cloreSecret(secret);
    return cloreSecret.ToString();
}

UniValue listvalidatorconf(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listvalidatorconf ( \"filter\" )\n"
            "\nPrint validator.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by alias, address, txHash.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) validator alias\n"
            "    \"address\": \"xxxx\",      (string) validator IP address\n"
            "    \"privateKey\": \"xxxx\",   (string) validator private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,         (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) validator status\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listvalidatorconf", "") + HelpExampleRpc("listvalidatorconf", ""));

    std::string strFilter = "";
    if (request.params.size() >= 1) {
        strFilter = request.params[0].get_str();
    }

    std::vector<CValidatorConfig::CValidatorEntry> validatorEntries;
    validatorEntries = validatorConfig.getEntries();

    const CChainParams& chainparams = GetParams();
    UniValue ret(UniValue::VARR);

    for (const auto& validatorEntry : validatorEntries) {
        // Apply filter if specified
        if (!strFilter.empty()) {
            if (validatorEntry.getAlias().find(strFilter) == std::string::npos &&
                validatorEntry.getIp().find(strFilter) == std::string::npos &&
                validatorEntry.getTxHash().find(strFilter) == std::string::npos) {
                continue;
            }
        }

        // Determine status based on authorization
        std::string status = "UNKNOWN";
        bool isAuthorized = chainparams.IsAuthorizedValidatorAlias(validatorEntry.getAlias());

        if (chainparams.NetworkIDString() == "regtest") {
            status = "AUTHORIZED_REGTEST";
        } else if (isAuthorized) {
            status = "AUTHORIZED";
        } else {
            status = "UNAUTHORIZED";
        }

        // Convert output index
        int outputIndex = 0;
        validatorEntry.castOutputIndex(outputIndex);

        UniValue validatorObj(UniValue::VOBJ);
        validatorObj.pushKV("alias", validatorEntry.getAlias());
        validatorObj.pushKV("address", validatorEntry.getIp());
        validatorObj.pushKV("txHash", validatorEntry.getTxHash());
        validatorObj.pushKV("outputIndex", outputIndex);
        validatorObj.pushKV("status", status);
        validatorObj.pushKV("authorized", isAuthorized || chainparams.NetworkIDString() == "regtest");
        ret.push_back(validatorObj);
    }

    return ret;
}

UniValue getvalidatoroutputs(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getvalidatoroutputs\n"
            "\nPrint all validator transaction outputs\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n         (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getvalidatoroutputs", "") + HelpExampleRpc("getvalidatoroutputs", ""));

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
        if (out.tx->tx->vout[out.i].nValue == GetParams().GetConsensus().nValidatorCollateralAmt) { // Validator collateral
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

UniValue getvalidatorcount(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 0))
        throw std::runtime_error(
            "getvalidatorcount\n"
            "\nGet validator count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total validators\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"enabled\": n,      (numeric) Enabled validators\n"
            "  \"inqueue\": n       (numeric) Validators in queue\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getvalidatorcount", "") + HelpExampleRpc("getvalidatorcount", ""));

    UniValue obj(UniValue::VOBJ);

    // TODO: Implement proper validator counting when infrastructure is ready
    // For now, return zero counts
    obj.pushKV("total", 0);
    obj.pushKV("stable", 0);
    obj.pushKV("enabled", 0);
    obj.pushKV("inqueue", 0);

    return obj;
}

UniValue getvalidatorstatus(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getvalidatorstatus\n"
            "\nPrint validator status\n"

            "\nResult:\n"
            "{\n"
            "  \"outpoint\": \"xxxx\",      (string) Collateral transaction output\n"
            "  \"service\": \"xxxx\",       (string) Validator network address\n"
            "  \"pubkey\": \"xxxx\",        (string) Validator public key\n"
            "  \"status\": \"xxxx\",        (string) Validator status\n"
            "  \"message\": \"xxxx\"        (string) Validator status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getvalidatorstatus", "") + HelpExampleRpc("getvalidatorstatus", ""));

    if (!fValidatorMode)
        throw JSONRPCError(RPC_MISC_ERROR, "This is not a validator");

    // TODO: Implement proper validator status when infrastructure is ready
    UniValue validatorObj(UniValue::VOBJ);
    validatorObj.pushKV("outpoint", "Not implemented");
    validatorObj.pushKV("service", "Not implemented");
    validatorObj.pushKV("pubkey", "Not implemented");
    validatorObj.pushKV("status", "Not implemented");
    validatorObj.pushKV("message", "Validator infrastructure not yet implemented");

    return validatorObj;
}

UniValue validatorcurrent(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "validatorcurrent\n"
            "\nGet current validator winner (scheduled to be paid next).\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"outpoint\": \"xxxx\",    (string) Collateral transaction output\n"
            "  \"pubkey\": \"xxxx\",      (string) Validator Public key\n"
            "  \"status\": \"xxxx\"       (string) Validator status\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("validatorcurrent", "") + HelpExampleRpc("validatorcurrent", ""));

    // TODO: Implement proper payment queue logic when infrastructure is ready
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("protocol", PROTOCOL_VERSION);
    obj.pushKV("outpoint", "Not implemented");
    obj.pushKV("pubkey", "Not implemented");
    obj.pushKV("status", "Payment queue not implemented");

    return obj;
}

// Enhanced validatorlist to replace the basic stub
static UniValue listvalidators(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listvalidators ( \"filter\" )\n"
            "\nGet a ranked list of validators\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,                             (numeric) Validator Rank (or 0 if not enabled)\n"
            "    \"outpoint\": \"xxxx\",                  (string) Collateral transaction output\n"
            "    \"pubkey\": \"xxxx\",                    (string) Validator public key\n"
            "    \"status\": \"xxxx\",                    (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"xxxx\",                      (string) Validator CLORE address\n"
            "    \"version\": v,                          (numeric) Validator protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) validator has been active\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listvalidators", "") + HelpExampleRpc("listvalidators", ""));

    // TODO: Implement proper validator listing when infrastructure is ready
    // For now, return empty array
    UniValue ret(UniValue::VARR);
    return ret;
}

UniValue createvalidatorbroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 5))
        throw std::runtime_error(
            "createvalidatorbroadcast \"alias\" \"service\" \"keyCollateral\" \"txHash\" outputIndex\n"
            "\nCreates a validator broadcast message for the given validator.\n"
            "\nSECURITY: Authorization is based on the collateral private key, not the alias.\n"
            "\nOnly holders of authorized collateral private keys can create broadcasts.\n"

            "\nArguments:\n"
            "1. \"alias\"         (string, required) Alias name for the validator (informational only)\n"
            "2. \"service\"       (string, required) '<ip>:<port>' The validator service location\n"
            "3. \"keyCollateral\" (string, required) The collateral address private key\n"
            "4. \"txHash\"        (string, required) Transaction hash for the collateral\n"
            "5. outputIndex       (numeric, required) Output index for the collateral\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",           (string) Alias name (informational)\n"
            "  \"service\": \"xxxx\",         (string) Service address\n"
            "  \"collateralAddress\": \"xxxx\", (string) Derived collateral address\n"
            "  \"authorized\": true|false,    (boolean) Whether the collateral address is authorized\n"
            "  \"result\": \"xxxx\",          (string) Result message\n"
            "  \"hex\": \"xxxx\"              (string) Hex encoded broadcast (if successful)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createvalidatorbroadcast", "\"validator1\" \"127.0.0.1:9999\" \"privkey\" \"txhash\" 0") +
            HelpExampleRpc("createvalidatorbroadcast", "\"validator1\", \"127.0.0.1:9999\", \"privkey\", \"txhash\", 0"));

    std::string alias = request.params[0].get_str();
    std::string service = request.params[1].get_str();
    std::string keyCollateral = request.params[2].get_str();
    std::string txHash = request.params[3].get_str();
    int outputIndex = request.params[4].get_int();

    const CChainParams& chainparams = GetParams();
    UniValue statusObj(UniValue::VOBJ);
    statusObj.pushKV("alias", alias);
    statusObj.pushKV("service", service);

    // SECURITY: Extract and validate collateral address from private key
    std::string collateralAddress = "";
    bool addressExtractionSuccess = false;
    
    try {
        // Parse the private key
        CCloreSecret cloreSecret;
        if (!cloreSecret.SetString(keyCollateral)) {
            statusObj.pushKV("authorized", false);
            statusObj.pushKV("result", "ERROR: Invalid private key format");
            statusObj.pushKV("error", "The provided keyCollateral is not a valid private key");
            statusObj.pushKV("hex", "");
            return statusObj;
        }

        // Get the CKey from CCloreSecret
        CKey key = cloreSecret.GetKey();
        if (!key.IsValid()) {
            statusObj.pushKV("authorized", false);
            statusObj.pushKV("result", "ERROR: Invalid private key");
            statusObj.pushKV("error", "The private key is not valid");
            statusObj.pushKV("hex", "");
            return statusObj;
        }

        // Get public key and derive address
        CPubKey pubkey = key.GetPubKey();
        if (!pubkey.IsValid()) {
            statusObj.pushKV("authorized", false);
            statusObj.pushKV("result", "ERROR: Cannot derive public key");
            statusObj.pushKV("error", "Failed to derive public key from private key");
            statusObj.pushKV("hex", "");
            return statusObj;
        }

        // Convert public key to address
        CKeyID keyID = pubkey.GetID();
        CTxDestination dest = keyID;
        collateralAddress = EncodeDestination(dest);
        addressExtractionSuccess = true;

        statusObj.pushKV("collateralAddress", collateralAddress);

    } catch (const std::exception& e) {
        statusObj.pushKV("authorized", false);
        statusObj.pushKV("result", "ERROR: Failed to process private key");
        statusObj.pushKV("error", std::string("Exception: ") + e.what());
        statusObj.pushKV("hex", "");
        return statusObj;
    }

    // SECURITY: Check authorization by collateral address (not alias!)
    bool isAuthorized = false;
    if (addressExtractionSuccess) {
        if (chainparams.NetworkIDString() == "regtest") {
            // In regtest, all validators are authorized for testing
            isAuthorized = true;
        } else {
            // Use the secure address-based authorization check
            isAuthorized = chainparams.IsAuthorizedValidatorAddress(collateralAddress);
        }
    }

    statusObj.pushKV("authorized", isAuthorized);

    if (!isAuthorized) {
        statusObj.pushKV("result", "ERROR: Validator not authorized on this network");
        statusObj.pushKV("error", "The collateral address " + collateralAddress + 
                                   " is not in the authorized validators list for " + 
                                   chainparams.NetworkIDString() + " network");
        statusObj.pushKV("hex", "");
    } else {
        // TODO: Implement actual broadcast creation when infrastructure is ready
        statusObj.pushKV("result", "SUCCESS: Authorization verified (broadcast creation not yet implemented)");
        statusObj.pushKV("note", "Collateral address authorization confirmed");
        statusObj.pushKV("hex", "");
    }

    return statusObj;
}

UniValue decodevalidatorbroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodevalidatorbroadcast \"hexstring\"\n"
            "\nDecode validator broadcast message\n"

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The hex encoded validator broadcast\n"

            "\nResult:\n"
            "{\n"
            "  \"outpoint\": \"xxxx\",    (string) The validator outpoint\n"
            "  \"addr\": \"xxxx\",        (string) The validator address\n"
            "  \"pubkey\": \"xxxx\",      (string) The validator public key\n"
            "  \"vchSig\": \"xxxx\",      (string) The signature\n"
            "  \"sigTime\": nnn,          (numeric) The signature time\n"
            "  \"protocolVersion\": nnn   (numeric) The protocol version\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodevalidatorbroadcast", "\"hexstring\"") +
            HelpExampleRpc("decodevalidatorbroadcast", "\"hexstring\""));

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

UniValue getvalidatorscores(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getvalidatorscores ( blocks )\n"
            "\nPrint list of winning validator by score\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

            "\nResult:\n"
            "{\n"
            "  \"nHeight\": n,           (numeric) Current block height\n"
            "  \"scores\": [             (array) Validator scores\n"
            "    {\n"
            "      \"outpoint\": \"xxxx\",   (string) Validator outpoint\n"
            "      \"score\": \"xxxx\",      (string) Validator score\n"
            "      \"addr\": \"xxxx\"        (string) Validator address\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getvalidatorscores", "") + HelpExampleRpc("getvalidatorscores", ""));

    /*int nBlocks = 10;
    if (request.params.size() > 0) {
        nBlocks = request.params[0].get_int();
    }*/

    // TODO: Implement proper validator scoring when infrastructure is ready
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("nHeight", chainActive.Height());

    UniValue scoresArray(UniValue::VARR);
    obj.pushKV("scores", scoresArray);

    return obj;
}

UniValue getvalidatorwinners(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getvalidatorwinners ( blocks \"filter\" )\n"
            "\nPrint the validator winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"
            "2. \"filter\"    (string, optional) Search filter matching Validator address\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"outpoint\": \"xxxx\",   (string) validator outpoint\n"
            "      \"score\": \"xxxx\",      (string) validator score\n"
            "      \"addr\": \"xxxx\"        (string) validator address\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getvalidatorwinners", "") + HelpExampleRpc("getvalidatorwinners", ""));

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

UniValue initvalidator(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 2))
        throw std::runtime_error(
            "initvalidator \"privkey\" \"address\"\n"
            "\nInitialize validator on this wallet\n"

            "\nArguments:\n"
            "1. \"privkey\"     (string, required) The validator private key\n"
            "2. \"address\"     (string, required) The IP:port of the validator\n"

            "\nResult:\n"
            "\"status\"         (string) Validator initialization status\n"

            "\nExamples:\n" +
            HelpExampleCli("initvalidator", "\"privkey\" \"addr:port\"") +
            HelpExampleRpc("initvalidator", "\"privkey\", \"addr:port\""));

    // TODO: Implement proper validator initialization when infrastructure is ready
    return "Validator initialization not yet implemented";
}

UniValue relayvalidatorbroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "relayvalidatorbroadcast \"hexstring\"\n"
            "\nCommand to relay validator broadcast messages\n"

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The hex encoded validator broadcast message\n"

            "\nResult:\n"
            "\"status\"           (string) Relay status\n"

            "\nExamples:\n" +
            HelpExampleCli("relayvalidatorbroadcast", "\"hexstring\"") +
            HelpExampleRpc("relayvalidatorbroadcast", "\"hexstring\""));

    // TODO: Implement proper broadcast relay when infrastructure is ready
    return "Validator broadcast relay not yet implemented";
}

UniValue listauthorizedvalidators(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "listauthorizedvalidators ( \"filter\" )\n"
            "\nList all authorized validators for this network\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by alias, address, or description.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) validator alias\n"
            "    \"pubkeyAddress\": \"xxxx\", (string) authorized CLORE address\n"
            "    \"description\": \"xxxx\",   (string) description/notes\n"
            "    \"network\": \"xxxx\"       (string) network (main/test/regtest)\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listauthorizedvalidators", "") + HelpExampleRpc("listauthorizedvalidators", ""));

    std::string strFilter = "";
    if (request.params.size() >= 1) {
        strFilter = request.params[0].get_str();
    }

    const CChainParams& chainparams = GetParams();
    const std::vector<CChainParams::AuthorizedValidator>& authorizedValidators = chainparams.GetAuthorizedValidators();

    UniValue ret(UniValue::VARR);

    for (const auto& validator : authorizedValidators) {
        // Apply filter if specified
        if (!strFilter.empty()) {
            if (validator.alias.find(strFilter) == std::string::npos &&
                validator.pubkeyAddress.find(strFilter) == std::string::npos &&
                validator.description.find(strFilter) == std::string::npos) {
                continue;
            }
        }

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("alias", validator.alias);
        obj.pushKV("pubkeyAddress", validator.pubkeyAddress);
        obj.pushKV("description", validator.description);
        obj.pushKV("network", chainparams.NetworkIDString());
        ret.push_back(obj);
    }

    return ret;
}

UniValue checkvalidatorauth(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "checkvalidatorauth \"type\" \"value\"\n"
            "\nCheck if a validator is authorized on this network\n"

            "\nArguments:\n"
            "1. \"type\"      (string, required) Type of check: \"alias\" or \"address\"\n"
            "2. \"value\"     (string, required) Value to check (alias or CLORE address)\n"

            "\nResult:\n"
            "{\n"
            "  \"authorized\": true|false,    (boolean) Whether the validator is authorized\n"
            "  \"type\": \"xxxx\",            (string) Type of check performed\n"
            "  \"value\": \"xxxx\",           (string) Value that was checked\n"
            "  \"network\": \"xxxx\"          (string) Current network\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("checkvalidatorauth", "\"alias\" \"clore-validator-01\"") +
            HelpExampleCli("checkvalidatorauth", "\"address\" \"ATsQHm7qbMSe4gJnx8W5SnLNP9bKLmgD52\"") +
            HelpExampleRpc("checkvalidatorauth", "\"address\", \"ATsQHm7qbMSe4gJnx8W5SnLNP9bKLmgD52\""));

    std::string strType = request.params[0].get_str();
    std::string strValue = request.params[1].get_str();

    const CChainParams& chainparams = GetParams();
    bool fAuthorized = false;

    if (strType == "alias") {
        fAuthorized = chainparams.IsAuthorizedValidatorAlias(strValue);
    } else if (strType == "address") {
        fAuthorized = chainparams.IsAuthorizedValidatorAddress(strValue);
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

UniValue createvalidatorconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 4)
        throw std::runtime_error(
            "createvalidatorconfig \"alias\" \"address\" \"collateraltxid\" ( outputindex )\n"
            "\nCreate a complete validator configuration entry\n"

            "\nArguments:\n"
            "1. \"alias\"         (string, required) Unique alias for the validator\n"
            "2. \"address\"       (string, required) IP:port for the validator service\n"
            "3. \"collateraltxid\" (string, required) Transaction ID of the 1000 CLORE collateral\n"
            "4. outputindex       (numeric, optional) Output index of the collateral (default: 0)\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",           (string) Validator alias\n"
            "  \"address\": \"xxxx\",         (string) Validator IP:port\n"
            "  \"privateKey\": \"xxxx\",      (string) Generated validator private key\n"
            "  \"collateralTxId\": \"xxxx\",  (string) Collateral transaction ID\n"
            "  \"outputIndex\": n,            (numeric) Collateral output index\n"
            "  \"authorized\": true|false,    (boolean) Whether this validator is authorized\n"
            "  \"configLine\": \"xxxx\",      (string) Line to add to validator.conf\n"
            "  \"status\": \"xxxx\"           (string) Creation status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createvalidatorconfig", "\"validator1\" \"127.0.0.1:8788\" \"abc123def456...\" 0") +
            HelpExampleRpc("createvalidatorconfig", "\"validator1\", \"127.0.0.1:8788\", \"abc123def456...\", 0"));

    std::string alias = request.params[0].get_str();
    std::string address = request.params[1].get_str();
    std::string collateralTxId = request.params[2].get_str();
    int outputIndex = (request.params.size() > 3) ? request.params[3].get_int() : 0;

    const CChainParams& chainparams = GetParams();
    UniValue result(UniValue::VOBJ);

    // 1. Validate alias uniqueness
    std::vector<CValidatorConfig::CValidatorEntry> existingEntries = validatorConfig.getEntries();
    for (const auto& entry : existingEntries) {
        if (entry.getAlias() == alias) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Validator alias '" + alias + "' already exists in configuration");
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
    bool isAuthorized = chainparams.IsAuthorizedValidatorAlias(alias);
    if (chainparams.NetworkIDString() != "regtest" && !isAuthorized) {
        result.pushKV("authorized", false);
        result.pushKV("status", "ERROR: Validator alias not authorized for " + chainparams.NetworkIDString() + " network");
        result.pushKV("alias", alias);
        return result;
    }

    // 5. Generate new validator private key
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
                if (nValue == GetParams().GetConsensus().nValidatorCollateralAmt) {
                    result.pushKV("collateralValid", true);
                } else {
                    result.pushKV("collateralValid", false);
                    result.pushKV("collateralAmount", (double)nValue / COIN);
                    result.pushKV("warning", strprintf("Collateral amount is not exactly %d CLORE", GetParams().GetConsensus().nValidatorCollateralAmt / COIN));
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

    // 8. Add to validator configuration (in memory)
    try {
        validatorConfig.add(alias, address, collateralTxId, std::to_string(outputIndex));
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
    result.pushKV("status", "Validator configuration created successfully");
    result.pushKV("network", chainparams.NetworkIDString());

    return result;
}

UniValue addvalidatorconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 5)
        throw std::runtime_error(
            "addvalidatorconfig \"alias\" \"address\" \"privatekey\" \"collateraltxid\" outputindex\n"
            "\nAdd a validator configuration entry manually\n"

            "\nArguments:\n"
            "1. \"alias\"         (string, required) Validator alias\n"
            "2. \"address\"       (string, required) IP:port for the validator\n"
            "3. \"privatekey\"    (string, required) Validator private key\n"
            "4. \"collateraltxid\" (string, required) Collateral transaction ID\n"
            "5. outputindex       (numeric, required) Collateral output index\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",           (string) Validator alias\n"
            "  \"added\": true|false,         (boolean) Whether the entry was added\n"
            "  \"authorized\": true|false,    (boolean) Whether this validator is authorized\n"
            "  \"status\": \"xxxx\"           (string) Status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("addvalidatorconfig", "\"validator1\" \"127.0.0.1:8788\" \"93HaYBV...\" \"abc123...\" 0") +
            HelpExampleRpc("addvalidatorconfig", "\"validator1\", \"127.0.0.1:8788\", \"93HaYBV...\", \"abc123...\", 0"));

    std::string alias = request.params[0].get_str();
    std::string address = request.params[1].get_str();
    std::string privateKey = request.params[2].get_str();
    std::string collateralTxId = request.params[3].get_str();
    std::string outputIndexStr = std::to_string(request.params[4].get_int());

    const CChainParams& chainparams = GetParams();
    UniValue result(UniValue::VOBJ);

    // Check authorization
    bool isAuthorized = chainparams.IsAuthorizedValidatorAlias(alias);

    if (chainparams.NetworkIDString() != "regtest" && !isAuthorized) {
        result.pushKV("alias", alias);
        result.pushKV("added", false);
        result.pushKV("authorized", false);
        result.pushKV("status", "ERROR: Validator alias not authorized for " + chainparams.NetworkIDString() + " network");
        return result;
    }

    // Add to configuration (note: privateKey is no longer stored in configuration)
    try {
        validatorConfig.add(alias, address, collateralTxId, outputIndexStr);
        result.pushKV("alias", alias);
        result.pushKV("added", true);
        result.pushKV("authorized", isAuthorized || chainparams.NetworkIDString() == "regtest");
        result.pushKV("status", "Validator configuration added successfully");
    } catch (const std::exception& e) {
        result.pushKV("alias", alias);
        result.pushKV("added", false);
        result.pushKV("authorized", isAuthorized);
        result.pushKV("status", "ERROR: " + std::string(e.what()));
    }

    return result;
}

UniValue removevalidatorconfig(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "removevalidatorconfig \"alias\"\n"
            "\nRemove a validator configuration entry\n"

            "\nArguments:\n"
            "1. \"alias\"    (string, required) Validator alias to remove\n"

            "\nResult:\n"
            "{\n"
            "  \"alias\": \"xxxx\",      (string) Validator alias\n"
            "  \"removed\": true|false,  (boolean) Whether the entry was removed\n"
            "  \"status\": \"xxxx\"      (string) Status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("removevalidatorconfig", "\"validator1\"") +
            HelpExampleRpc("removevalidatorconfig", "\"validator1\""));

    std::string alias = request.params[0].get_str();
    UniValue result(UniValue::VOBJ);

    try {
        validatorConfig.remove(alias);
        result.pushKV("alias", alias);
        result.pushKV("removed", true);
        result.pushKV("status", "Validator configuration removed successfully");
    } catch (const std::exception& e) {
        result.pushKV("alias", alias);
        result.pushKV("removed", false);
        result.pushKV("status", "ERROR: " + std::string(e.what()));
    }

    return result;
}

UniValue startvalidator(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "startvalidator \"set\" ( \"lockWallet\" ) ( \"alias\" )\n"
            "\nAttempts to start one or more validator(s)\n"

            "\nArguments:\n"
            "1. \"set\"         (string, required) Specify which set of validator(s) to start.\n"
            "2. lockWallet      (boolean, optional) Lock wallet after completion.\n"
            "3. \"alias\"       (string, optional) Validator alias. Required if set is \"alias\"\n"

            "\nResult: (for set = \"all\", \"missing\" or \"disabled\"):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [              (array) Details about each started validator\n"
            "    {\n"
            "      \"alias\": \"xxxx\",      (string) Alias of the validator\n"
            "      \"result\": \"xxxx\",     (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"       (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"
            "Result: (for set = \"alias\"):\n"
            "{\n"
            "  \"alias\": \"xxxx\",       (string) Alias of the validator\n"
            "  \"result\": \"xxxx\",      (string) 'success' or 'failed'\n"
            "  \"error\": \"xxxx\"        (string) Error message, if failed\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("startvalidator", "\"alias\" false \"my_validator\"") +
            HelpExampleRpc("startvalidator", "\"alias\", false, \"my_validator\""));

    std::string strCommand = request.params[0].get_str();

    if (strCommand == "alias") {
        if (request.params.size() < 3) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");
        }

        std::string strAlias = request.params[2].get_str();

        UniValue statusObj(UniValue::VOBJ);
        statusObj.pushKV("alias", strAlias);
        statusObj.pushKV("result", "failed");
        statusObj.pushKV("error", "Validator starting not yet implemented");

        return statusObj;
    } else if (strCommand == "all" || strCommand == "missing" || strCommand == "disabled") {
        UniValue resultsObj(UniValue::VOBJ);
        resultsObj.pushKV("overall", "Failed to start any validators. Implementation not ready.");

        UniValue detailsArray(UniValue::VARR);
        resultsObj.pushKV("detail", detailsArray);

        return resultsObj;
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid set specified, please use one of the following: 'all', 'missing', 'disabled' or 'alias'");
    }
}

// Basic validator command dispatcher (keeping compatibility)
static UniValue validator(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
            "validator \"command\" ...\n"
            "\nValidator control commands.\n"
            "\nArguments:\n"
            "1. command     (string, required) The command to execute\n"
            "\nAvailable commands:\n"
            "  status       - Get validator status\n"
            "  count        - Get validator count\n"
            "\nExamples:\n" +
            HelpExampleCli("validator", "\"status\"") + HelpExampleRpc("validator", "\"status\""));

    std::string strCommand = request.params[0].get_str();

    if (strCommand == "status") {
        return getvalidatorstatus(request);
    } else if (strCommand == "count") {
        return getvalidatorcount(request);
    } else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown command: " + strCommand);
    }
}

static const CRPCCommand commands[] =
    {
        //  category              name                      actor (function)         argNames
        //  --------------------- ------------------------  -----------------------  ----------
        {"validator", "createvalidatorbroadcast", &createvalidatorbroadcast, {"alias", "service", "keyCollateral", "txHash", "outputIndex"}},
        {"validator", "createvalidatorkey", &createvalidatorkey, {}},
        {"validator", "decodevalidatorbroadcast", &decodevalidatorbroadcast, {"hexstring"}},
        {"validator", "getvalidatorcount", &getvalidatorcount, {}},
        {"validator", "getvalidatoroutputs", &getvalidatoroutputs, {}},
        {"validator", "getvalidatorscores", &getvalidatorscores, {"blocks"}},
        {"validator", "getvalidatorstatus", &getvalidatorstatus, {}},
        {"validator", "getvalidatorwinners", &getvalidatorwinners, {"blocks", "filter"}},
        {"validator", "initvalidator", &initvalidator, {"privkey", "address"}},
        {"validator", "listvalidatorconf", &listvalidatorconf, {"filter"}},
        {"validator", "listvalidators", &listvalidators, {"filter"}},
        {"validator", "validator", &validator, {"command"}},
        {"validator", "validatorcurrent", &validatorcurrent, {}},
        {"validator", "relayvalidatorbroadcast", &relayvalidatorbroadcast, {"hexstring"}},
        {"validator", "startvalidator", &startvalidator, {"set", "lockWallet", "alias"}},
        {"validator", "listauthorizedvalidators", &listauthorizedvalidators, {"filter"}},
        {"validator", "checkvalidatorauth", &checkvalidatorauth, {"type", "value"}},
        {"validator", "createvalidatorconfig", &createvalidatorconfig, {"alias", "address", "collateraltxid", "outputindex"}},
        {"validator", "addvalidatorconfig", &addvalidatorconfig, {"alias", "address", "privatekey", "collateraltxid", "outputindex"}},
        {"validator", "removevalidatorconfig", &removevalidatorconfig, {"alias"}},
};

void RegisterValidatorRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}