// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "chainparams.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "rpc/blockchain.h"
#include "rpc/server.h"
#include "stakeinput.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "wallet/coincontrol.h"
#include "wallet/wallet.h"

#include <univalue.h>

#ifdef ENABLE_WALLET
#include "wallet/rpcwallet.h"
#endif

// Global staking variables
static bool fStakingEnabled = true;
static bool fStakingActive = false;
static CAmount nStakingWeight = 0;
static uint64_t nExpectedStakingTime = 0;

// Calculate staking weight from wallet UTXOs
static CAmount CalculateStakingWeight(CWallet* pwallet)
{
#ifdef ENABLE_WALLET
    if (!pwallet) {
        return 0;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    CAmount nWeight = 0;
    const Consensus::Params& params = GetParams().GetConsensus();
    int64_t nTime = GetTime();

    // Iterate through wallet UTXOs
    std::map<uint256, CWalletTx> mapWallet = pwallet->mapWallet;
    for (const auto& item : mapWallet) {
        const CWalletTx& wtx = item.second;

        // Skip invalid transactions
        if (!wtx.IsTrusted() || wtx.GetDepthInMainChain() < 1) {
            continue;
        }

        // Check each output
        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            const CTxOut& txout = wtx.tx->vout[i];

            // Skip if not ours or already spent
            if (!pwallet->IsMine(txout) || pwallet->IsSpent(wtx.GetHash(), i)) {
                continue;
            }

            // Check minimum staking amount (1 CLORE minimum)
            if (txout.nValue < COIN) {
                continue;
            }

            // Check minimum age
            int64_t nCoinAge = nTime - wtx.GetTxTime();
            if (nCoinAge < params.nStakeMinAge) {
                continue;
            }

            // Add to weight
            nWeight += txout.nValue;
        }
    }

    return nWeight;
#else
    return 0;
#endif
}

// Calculate expected staking time based on weight and difficulty
static uint64_t CalculateExpectedStakingTime(CAmount stakingWeight)
{
    if (stakingWeight <= 0) {
        return 0;
    }

    double difficulty = GetDifficulty();
    double expectedTime = (difficulty * 1000000000000.0) / (double)stakingWeight;

    return (uint64_t)expectedTime;
}

// Enhanced staking status with real calculations
static UniValue getstakinginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getstakinginfo\n"
            "\nReturns comprehensive staking status information.\n"
            "\nResult:\n"
            "{\n"
            "  \"enabled\": true|false,           (boolean) true if staking is enabled globally\n"
            "  \"staking\": true|false,           (boolean) true if wallet is currently staking\n"
            "  \"errors\": \"...\",                (string) any staking errors\n"
            "  \"currentblocksize\": n,           (numeric) size of last staked block\n"
            "  \"currentblocktx\": n,             (numeric) number of transactions in last staked block\n"
            "  \"pooledtx\": n,                   (numeric) number of transactions in mempool\n"
            "  \"difficulty\": n,                 (numeric) current staking difficulty\n"
            "  \"weight\": n,                     (numeric) current staking weight\n"
            "  \"expectedtime\": n,               (numeric) expected time until next stake (seconds)\n"
            "  \"mininput\": n,                   (numeric) minimum input value for staking\n"
            "  \"minage\": n,                     (numeric) minimum age for staking (seconds)\n"
            "  \"stakingsupply\": n,              (numeric) total supply participating in staking\n"
            "  \"percentstaking\": n.n            (numeric) percentage of total supply staking\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getstakinginfo", "") + HelpExampleRpc("getstakinginfo", ""));

    LOCK(cs_main);

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    // Calculate current staking weight
    nStakingWeight = CalculateStakingWeight(pwallet);
    nExpectedStakingTime = CalculateExpectedStakingTime(nStakingWeight);

    // Check if actively staking
    fStakingActive = fStakingEnabled && nStakingWeight > 0 && !IsInitialBlockDownload();
#else
    nStakingWeight = 0;
    nExpectedStakingTime = 0;
    fStakingActive = false;
#endif

    const Consensus::Params& params = GetParams().GetConsensus();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("enabled", fStakingEnabled);
    obj.pushKV("staking", fStakingActive);
    obj.pushKV("errors", "");
    obj.pushKV("currentblocksize", 0); // TODO: Track last staked block size
    obj.pushKV("currentblocktx", 0);   // TODO: Track last staked block tx count
    obj.pushKV("pooledtx", (int64_t)mempool.size());
    obj.pushKV("difficulty", GetDifficulty());
    obj.pushKV("weight", ValueFromAmount(nStakingWeight));
    obj.pushKV("expectedtime", nExpectedStakingTime);
    obj.pushKV("mininput", ValueFromAmount(COIN));
    obj.pushKV("minage", params.nStakeMinAge);

    // Calculate staking supply estimates
    // TODO: Implement total supply calculation when nMoneySupply is available
    obj.pushKV("stakingsupply", ValueFromAmount(nStakingWeight));
    obj.pushKV("percentstaking", 0.0); // TODO: Calculate actual percentage

    return obj;
}

// Enhanced staking control
static UniValue setstaking(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "setstaking enabled\n"
            "\nEnable or disable staking.\n"
            "\nArguments:\n"
            "1. enabled     (boolean, required) true to enable staking, false to disable\n"
            "\nResult:\n"
            "{\n"
            "  \"enabled\": true|false,     (boolean) new staking status\n"
            "  \"message\": \"...\"          (string) status message\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("setstaking", "true") + HelpExampleRpc("setstaking", "true"));

    bool fEnable = request.params[0].get_bool();

    fStakingEnabled = fEnable;

    LogPrintf("Staking %s via RPC\n", fEnable ? "enabled" : "disabled");

    UniValue result(UniValue::VOBJ);
    result.pushKV("enabled", fStakingEnabled);
    result.pushKV("message", fEnable ? "Staking enabled" : "Staking disabled");

    return result;
}

// Get staking rewards information
static UniValue getstakingrewards(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getstakingrewards ( \"address\" )\n"
            "\nReturns staking rewards information.\n"
            "\nArguments:\n"
            "1. \"address\"       (string, optional) specific address to check\n"
            "\nResult:\n"
            "{\n"
            "  \"totalrewards\": n,           (numeric) total staking rewards earned\n"
            "  \"lastreward\": n,             (numeric) last staking reward amount\n"
            "  \"lastrewardtime\": n,         (numeric) timestamp of last reward\n"
            "  \"averagereward\": n,          (numeric) average reward per stake\n"
            "  \"stakingblocks\": n           (numeric) number of blocks staked\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getstakingrewards", "") +
            HelpExampleCli("getstakingrewards", "\"CloreAddressHere\"") +
            HelpExampleRpc("getstakingrewards", ""));

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strAddress;
    if (request.params.size() > 0) {
        strAddress = request.params[0].get_str();
    }

    CAmount totalRewards = 0;
    CAmount lastReward = 0;
    int64_t lastRewardTime = 0;
    int stakingBlocks = 0;

    // Scan wallet for staking transactions
    for (const auto& item : pwallet->mapWallet) {
        const CWalletTx& wtx = item.second;

        // Check if this is a staking transaction (coinstake)
        if (wtx.tx->IsCoinStake() && wtx.GetDepthInMainChain() > 0) {
            stakingBlocks++;

            // Calculate reward (output value minus input value)
            CAmount inputValue = 0;
            CAmount outputValue = 0;

            // Get input value
            if (!wtx.tx->vin.empty()) {
                const CTxIn& txin = wtx.tx->vin[0];
                const CWalletTx* wtxPrev = pwallet->GetWalletTx(txin.prevout.hash);
                if (wtxPrev && txin.prevout.n < wtxPrev->tx->vout.size()) {
                    inputValue = wtxPrev->tx->vout[txin.prevout.n].nValue;
                }
            }

            // Get output value (first output is usually the reward + stake)
            if (!wtx.tx->vout.empty()) {
                outputValue = wtx.tx->vout[0].nValue;
            }

            CAmount reward = outputValue - inputValue;
            if (reward > 0) {
                totalRewards += reward;

                // Update last reward info
                if (wtx.GetTxTime() > lastRewardTime) {
                    lastReward = reward;
                    lastRewardTime = wtx.GetTxTime();
                }
            }
        }
    }

    CAmount averageReward = stakingBlocks > 0 ? totalRewards / stakingBlocks : 0;

    UniValue result(UniValue::VOBJ);
    result.pushKV("totalrewards", ValueFromAmount(totalRewards));
    result.pushKV("lastreward", ValueFromAmount(lastReward));
    result.pushKV("lastrewardtime", lastRewardTime);
    result.pushKV("averagereward", ValueFromAmount(averageReward));
    result.pushKV("stakingblocks", stakingBlocks);

    return result;
#else
    throw JSONRPCError(RPC_WALLET_ERROR, "Error: staking rewards require wallet support");
#endif
}

// List all stakeable UTXOs
static UniValue liststakeableutxos(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "liststakeableutxos ( minconf )\n"
            "\nReturns list of unspent outputs available for staking.\n"
            "\nArguments:\n"
            "1. minconf       (numeric, optional, default=1) minimum confirmations\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"txid\",             (string) transaction id\n"
            "    \"vout\": n,                   (numeric) output number\n"
            "    \"address\": \"address\",       (string) CLORE address\n"
            "    \"scriptPubKey\": \"key\",      (string) script key\n"
            "    \"amount\": x.xxx,             (numeric) transaction amount\n"
            "    \"confirmations\": n,          (numeric) confirmations\n"
            "    \"age\": n,                    (numeric) age in seconds\n"
            "    \"stakeable\": true|false      (boolean) whether UTXO is stakeable\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n" +
            HelpExampleCli("liststakeableutxos", "") +
            HelpExampleCli("liststakeableutxos", "6") +
            HelpExampleRpc("liststakeableutxos", ""));

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // nMinDepth is not currently used in the implementation but kept for future use
    /*int nMinDepth = 1;
    if (request.params.size() > 0) {
        nMinDepth = request.params[0].get_int();
    }*/

    const Consensus::Params& params = GetParams().GetConsensus();
    int64_t nTime = GetTime();

    UniValue results(UniValue::VARR);

    std::vector<COutput> vCoins;
    pwallet->AvailableCoins(vCoins, true, nullptr);

    for (const COutput& out : vCoins) {
        const CWalletTx* pcoin = out.tx;
        int i = out.i;

        const CTxOut& txout = pcoin->tx->vout[i];

        // Get age
        int64_t nAge = nTime - pcoin->GetTxTime();
        bool fStakeable = (nAge >= params.nStakeMinAge && txout.nValue >= COIN);

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", pcoin->GetHash().GetHex());
        entry.pushKV("vout", i);

        CTxDestination dest;
        if (ExtractDestination(txout.scriptPubKey, dest)) {
            entry.pushKV("address", EncodeDestination(dest));
        }

        entry.pushKV("scriptPubKey", HexStr(txout.scriptPubKey.begin(), txout.scriptPubKey.end()));
        entry.pushKV("amount", ValueFromAmount(txout.nValue));
        entry.pushKV("confirmations", out.nDepth);
        entry.pushKV("age", nAge);
        entry.pushKV("stakeable", fStakeable);

        results.push_back(entry);
    }

    return results;
#else
    throw JSONRPCError(RPC_WALLET_ERROR, "Error: listing UTXOs requires wallet support");
#endif
}

// Validator-PoS integration command
static UniValue getvalidatorstakinginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getvalidatorstakinginfo\n"
            "\nReturns information about validator staking integration.\n"
            "\nResult:\n"
            "{\n"
            "  \"validatorcount\": n,           (numeric) total number of validators\n"
            "  \"enabledvalidators\": n,        (numeric) number of enabled validators\n"
            "  \"validatorrewards\": true|false, (boolean) whether validator rewards are enabled\n"
            "  \"nextpayment\": \"...\",          (string) next validator payment info\n"
            "  \"posvalidatorintegration\": true|false (boolean) PoS-Validator integration status\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getvalidatorstakinginfo", "") + HelpExampleRpc("getvalidatorstakinginfo", ""));

    LOCK(cs_main);

    // TODO: Implement actual validator counting and payment logic
    int validatorCount = 0;
    int enabledValidators = 0;

    UniValue result(UniValue::VOBJ);
    result.pushKV("validatorcount", validatorCount);
    result.pushKV("enabledvalidators", enabledValidators);
    result.pushKV("validatorrewards", true); // Always enabled in hybrid system
    result.pushKV("nextpayment", "Not implemented yet");
    result.pushKV("posvalidatorintegration", true); // PoS-Validator integration active

    return result;
}

// Get comprehensive PoS information and transition status
static UniValue getposinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getposinfo\n"
            "\nReturns comprehensive Proof of Stake information and transition status.\n"
            "\nPhases:\n"
            "  Phase 0: Pure PoW - Traditional mining only\n"
            "  Phase 1-2: Infrastructure - PoW continues while validators/staking activate\n"
            "  Phase 3: PoS-Only - PoW disabled, PoS takes over with full infrastructure\n"
            "\nResult:\n"
            "{\n"
            "  \"validators_active\": true|false,   (boolean) Whether validator infrastructure is active\n"
            "  \"pos_active\": true|false,           (boolean) Whether PoS staking infrastructure is active\n"
            "  \"pure_pos_active\": true|false,      (boolean) Whether PoS-only phase is active\n"
            "  \"current_height\": n,               (numeric) Current blockchain height\n"
            "  \"validator_activation_height\": n, (numeric) Height when validators activate\n"
            "  \"pos_activation_height\": n,        (numeric) Height when PoS staking activates\n"
            "  \"pure_pos_activation_height\": n,   (numeric) Height when PoS-only phase activates\n"
            "  \"consensus_phase\": \"string\",        (string) Current consensus phase\n"
            "  \"blocks_until_pure_pos\": n,        (numeric) Blocks remaining until PoS-only phase\n"
            "  \"network\": \"string\",              (string) Network name (mainnet, testnet, regtest)\n"
            "  \"staking_enabled\": true|false,      (boolean) Whether staking is enabled\n"
            "  \"staking_weight\": n,               (numeric) Current staking weight\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getposinfo", "") +
            HelpExampleRpc("getposinfo", ""));

    LOCK(cs_main);

    const CChainParams& params = GetParams();
    const Consensus::Params& consensus = params.GetConsensus();
    const int nCurrentHeight = chainActive.Height();

    // Get upgrade activation heights
    const int nValidatorActivationHeight = consensus.vUpgrades[Consensus::ENABLE_POS_VALIDATORS].nActivationHeight;
    const int nPosActivationHeight = consensus.vUpgrades[Consensus::ENABLE_POS_STAKING].nActivationHeight;
    const int nPurePosActivationHeight = consensus.vUpgrades[Consensus::ENABLE_POS_REWARDS].nActivationHeight;

    // Check activation status
    const bool fValidatorsActive = Consensus::NetworkUpgradeActive(nCurrentHeight, consensus, Consensus::ENABLE_POS_VALIDATORS);
    const bool fPosActive = Consensus::NetworkUpgradeActive(nCurrentHeight, consensus, Consensus::ENABLE_POS_STAKING);
    const bool fPurePosActive = Consensus::NetworkUpgradeActive(nCurrentHeight, consensus, Consensus::ENABLE_POS_REWARDS);

    // Determine consensus phase
    std::string strConsensusPhase;
    if (fPurePosActive) {
        strConsensusPhase = "PoS-Only (Phase 3)";
    } else if (fPosActive) {
        strConsensusPhase = "Staking Infrastructure (Phase 2)";
    } else if (fValidatorsActive) {
        strConsensusPhase = "Validator Infrastructure (Phase 1)";
    } else {
        strConsensusPhase = "Pure PoW (Phase 0)";
    }

    // Calculate blocks until pure PoS
    int nBlocksUntilPurePos = 0;
    if (!fPurePosActive && nPurePosActivationHeight > nCurrentHeight) {
        nBlocksUntilPurePos = nPurePosActivationHeight - nCurrentHeight;
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("validators_active", fValidatorsActive);
    obj.pushKV("pos_active", fPosActive);
    obj.pushKV("pure_pos_active", fPurePosActive);
    obj.pushKV("current_height", nCurrentHeight);
    obj.pushKV("validator_activation_height", nValidatorActivationHeight);
    obj.pushKV("pos_activation_height", nPosActivationHeight);
    obj.pushKV("pure_pos_activation_height", nPurePosActivationHeight);
    obj.pushKV("consensus_phase", strConsensusPhase);
    obj.pushKV("blocks_until_pure_pos", nBlocksUntilPurePos);
    obj.pushKV("network", params.NetworkIDString());
    obj.pushKV("staking_enabled", fStakingEnabled);
    obj.pushKV("staking_weight", ValueFromAmount(nStakingWeight));

    return obj;
}

static const CRPCCommand commands[] =
    {
        //  category              name                         actor (function)              argNames
        //  --------------------- ---------------------------- ----------------------------  ----------
        {"staking", "getstakinginfo", &getstakinginfo, {}},
        {"staking", "setstaking", &setstaking, {"enabled"}},
        {"staking", "getstakingrewards", &getstakingrewards, {"address"}},
        {"staking", "liststakeableutxos", &liststakeableutxos, {"minconf"}},
        {"staking", "getvalidatorstakinginfo", &getvalidatorstakinginfo, {}},
        {"staking", "getposinfo", &getposinfo, {}},
};

void RegisterStakingRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        // Only register wallet-dependent commands if wallet is enabled
        if (commands[vcidx].actor == &getstakingrewards ||
            commands[vcidx].actor == &liststakeableutxos) {
#ifdef ENABLE_WALLET
            t.appendCommand(commands[vcidx].name, &commands[vcidx]);
#endif
        } else {
            t.appendCommand(commands[vcidx].name, &commands[vcidx]);
        }
    }
}