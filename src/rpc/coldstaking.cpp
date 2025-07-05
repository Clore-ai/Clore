// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpc/server.h"
#include "rpc/client.h"
#include "rpc/protocol.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "wallet/rpcwallet.h"
#include "script/standard.h"
#include "chainparams.h"
#include "init.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "validation.h"
#include "sync.h"
#include "core_io.h"
#include "base58.h"
#include "primitives/transaction.h"
#include "timedata.h"
#include "amount.h"

#include <univalue.h>
#include <boost/assign/list_of.hpp>

using namespace std;

/** 
 * delegateforstaking "validator_address" amount
 * 
 * Delegate coins to an AUTHORIZED VALIDATOR for cold staking while retaining spending control.
 * SECURITY: Delegation is only allowed to validators in the authorized validators list.
 * The validator will be able to stake with these coins but cannot spend them.
 * You retain full spending control of the coins.
 * 
 * Arguments:
 * 1. "validator_address"  (string, required) The authorized validator address to delegate to
 * 2. "amount"             (numeric, required) The amount to delegate (minimum 1.0 CLORE)
 * 
 * Result:
 * "txid"                  (string) The transaction id of the delegation
 * 
 * Notes:
 * - Use 'listauthorizedvalidators' to see available validators
 * - Use 'checkvalidatorauth address <address>' to verify a validator is authorized
 * - Only authorized validators can participate in cold staking
 * 
 * Examples:
 * + HelpExampleCli("delegateforstaking", "\"AN8h7T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE\" 100.0")
 * + HelpExampleRpc("delegateforstaking", "\"AN8h7T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE\", 100.0")
 */
UniValue delegateforstaking(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "delegateforstaking \"validator_address\" amount\n"
            "\nDelegate coins to an AUTHORIZED VALIDATOR for cold staking while retaining spending control.\n"
            "SECURITY: Delegation is only allowed to validators in the authorized validators list.\n"
            "The validator will be able to stake with these coins but cannot spend them.\n"
            "You retain full spending control of the coins.\n"
            "\nArguments:\n"
            "1. \"validator_address\"  (string, required) The authorized validator address to delegate to\n"
            "2. \"amount\"             (numeric, required) The amount to delegate (minimum 1.0 CLORE)\n"
            "\nResult:\n"
            "\"txid\"                  (string) The transaction id of the delegation\n"
            "\nNotes:\n"
            "- Use 'listauthorizedvalidators' to see available validators\n"
            "- Use 'checkvalidatorauth address <address>' to verify a validator is authorized\n"
            "- Only authorized validators can participate in cold staking\n"
            "\nExamples:\n"
            + HelpExampleCli("delegateforstaking", "\"AN8h7T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE\" 100.0")
            + HelpExampleRpc("delegateforstaking", "\"AN8h7T3WeZ6aFgNp7YZJJrn2xwKzAUFAcE\", 100.0")
        );

    if (!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Parse parameters
    string strValidatorAddress = request.params[0].get_str();
    CAmount nAmount = AmountFromValue(request.params[1]);

    // Validate amount
    if (nAmount < MIN_COLDSTAKING_AMOUNT) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Amount must be at least %s CLORE", FormatMoney(MIN_COLDSTAKING_AMOUNT)));
    }

    // Validate validator address
    CTxDestination validatorDest = DecodeDestination(strValidatorAddress);
    if (!IsValidDestination(validatorDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid validator address");
    }

    // SECURITY: Verify the validator address is an authorized validator
    const CChainParams& chainparams = GetParams();
    bool isAuthorizedValidator = chainparams.IsAuthorizedValidatorAddress(strValidatorAddress);
    
    if (!isAuthorizedValidator) {
        // Get list of authorized validators for helpful error message
        const auto& authorizedValidators = chainparams.GetAuthorizedValidators();
        std::string validatorList = "";
        for (size_t i = 0; i < authorizedValidators.size() && i < 5; ++i) {
            if (i > 0) validatorList += ", ";
            validatorList += authorizedValidators[i].alias + " (" + authorizedValidators[i].pubkeyAddress + ")";
        }
        if (authorizedValidators.size() > 5) {
            validatorList += "...";
        }
        
        std::string errorMsg = strprintf(
            "SECURITY: Cold staking delegation only allowed to authorized validators. "
            "Address '%s' is not in the authorized validators list for %s network. "
            "Use 'listauthorizedvalidators' to see valid validators. Examples: %s", 
            strValidatorAddress, chainparams.NetworkIDString(), validatorList);
            
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errorMsg);
    }

    // Get validator alias for logging
    std::string validatorAlias = chainparams.GetAuthorizedValidatorAlias(strValidatorAddress);
    if (validatorAlias.empty()) {
        validatorAlias = "Unknown";
    }

    CKeyID validatorKeyID = boost::get<CKeyID>(validatorDest);
    
    // Get a new address for spending control
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }
    CKeyID spendingKeyID = newKey.GetID();

    // Create cold staking script
    CScript scriptPubKey = GetScriptForColdStaking(validatorKeyID, spendingKeyID);

    // Create transaction
    CTransactionRef tx;
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    
    vector<CRecipient> vecSend;
    CRecipient recipient = {scriptPubKey, nAmount, false};
    vecSend.push_back(recipient);

    CCoinControl coinControl;
    if (!pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, strError, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(tx, {}, {}, {}, state)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Transaction commit failed");
    }

    return tx->GetHash().GetHex();
}

/** 
 * getcoldstakinginfo
 * 
 * Get information about cold staking delegation
 */
UniValue getcoldstakinginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getcoldstakinginfo\n"
            "\nReturns information about cold staking delegation.\n"
            "\nResult:\n"
            "{\n"
            "  \"enabled\": true|false,          (boolean) Whether cold staking is enabled\n"
            "  \"cold_staking_balance\": x.xxx,  (numeric) Amount delegated for cold staking\n"
            "  \"currently_staking\": x.xxx,     (numeric) Amount currently staking\n"
            "  \"delegations\": [                (array) List of cold staking delegations\n"
            "    {\n"
            "      \"txid\": \"hash\",           (string) Transaction id\n"
            "      \"vout\": n,                  (numeric) Output number\n"
            "      \"amount\": x.xxx,            (numeric) Amount delegated\n"
            "      \"cold_staking_address\": \"address\", (string) Cold staking address\n"
            "      \"spending_address\": \"address\"      (string) Spending address\n"
            "    }\n"
            "  ]\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getcoldstakinginfo", "")
            + HelpExampleRpc("getcoldstakinginfo", "")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("enabled", true);  // Cold staking is enabled by default
    
    CAmount nColdStakingBalance = 0;
    CAmount nCurrentlyStaking = 0;
    UniValue delegations(UniValue::VARR);

    // Iterate through unspent outputs to find cold staking delegations
    vector<COutput> vCoins;
    pwallet->AvailableCoins(vCoins);

    for (const auto& coin : vCoins) {
        const CWalletTx* pcoin = coin.tx;
        const CTxOut& out = pcoin->tx->vout[coin.i];
        
        if (IsColdStakeScript(out.scriptPubKey)) {
            nColdStakingBalance += out.nValue;
            
            // Extract addresses from cold staking script
            CKeyID stakingKey, spendingKey;
            if (ExtractColdStakeAddresses(out.scriptPubKey, stakingKey, spendingKey)) {
                UniValue delegation(UniValue::VOBJ);
                delegation.pushKV("txid", pcoin->GetHash().GetHex());
                delegation.pushKV("vout", coin.i);
                delegation.pushKV("amount", ValueFromAmount(out.nValue));
                delegation.pushKV("cold_staking_address", EncodeDestination(stakingKey));
                delegation.pushKV("spending_address", EncodeDestination(spendingKey));
                delegations.push_back(delegation);
            }
        }
    }

    obj.pushKV("cold_staking_balance", ValueFromAmount(nColdStakingBalance));
    obj.pushKV("currently_staking", ValueFromAmount(nCurrentlyStaking));
    obj.pushKV("delegations", delegations);

    return obj;
}

/** 
 * undelegatefromstaking "txid" vout
 * 
 * Undelegate (spend) coins from cold staking
 */
UniValue undelegatefromstaking(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "undelegatefromstaking \"txid\" vout\n"
            "\nUndelegate (spend) coins from cold staking.\n"
            "This will move the coins back to a regular address under your control.\n"
            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id of the delegation\n"
            "2. \"vout\"      (numeric, required) The output number of the delegation\n"
            "\nResult:\n"
            "\"txid\"         (string) The transaction id of the undelegation\n"
            "\nExamples:\n"
            + HelpExampleCli("undelegatefromstaking", "\"abc123...\" 0")
            + HelpExampleRpc("undelegatefromstaking", "\"abc123...\", 0")
        );

    CWallet * const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    // Parse parameters
    uint256 txid = ParseHashV(request.params[0], "txid");
    int vout = request.params[1].get_int();

    // Find the cold staking UTXO
    COutPoint outPoint(txid, vout);
    auto it = pwallet->mapWallet.find(txid);
    if (it == pwallet->mapWallet.end()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction not found in wallet");
    }

    const CWalletTx& wtx = it->second;
    if (vout >= (int)wtx.tx->vout.size()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid output number");
    }

    const CTxOut& out = wtx.tx->vout[vout];
    if (!IsColdStakeScript(out.scriptPubKey)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output is not a cold staking delegation");
    }

    // Check if UTXO is spendable
    if (pwallet->IsSpent(outPoint)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output is already spent");
    }

    // Get new address for the undelegated coins
    CTxDestination dest;
    if (!pwallet->GetNewDestination(OutputType::LEGACY, "", dest)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Unable to generate a new address");
    }

    CScript scriptPubKey = GetScriptForDestination(dest);
    CAmount nAmount = out.nValue;

    // Create transaction to undelegate
    CTransactionRef tx;
    CAmount nFeeRequired;
    int nChangePosRet = -1;
    string strError;
    
    vector<CRecipient> vecSend;
    CRecipient recipient = {scriptPubKey, nAmount, false};
    vecSend.push_back(recipient);

    CCoinControl coinControl;
    coinControl.Select(outPoint);
    
    if (!pwallet->CreateTransaction(vecSend, tx, nFeeRequired, nChangePosRet, strError, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }

    CValidationState state;
    if (!pwallet->CommitTransaction(tx, {}, {}, {}, state)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Transaction commit failed");
    }

    return tx->GetHash().GetHex();
}

static const CRPCCommand commands[] = {
    //  category            name                      actor (function)         argNames
    //  -----------------   ------------------------  -----------------------  ----------
    { "coldstaking",        "delegateforstaking",     &delegateforstaking,     {"validator_address", "amount"} },
    { "coldstaking",        "getcoldstakinginfo",     &getcoldstakinginfo,     {} },
    { "coldstaking",        "undelegatefromstaking",  &undelegatefromstaking,  {"txid", "vout"} },
};

void RegisterColdStakingRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
} 