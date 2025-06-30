// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakereward.h"
#include "chainparams.h"
#include "consensus/params.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "util.h"
#include "validation.h"

// Developer fund addresses for different networks
const std::string MAINNET_DEV_FUND = "ARZ8joQPE1NQ7KqzAivypNFGFTBYrr2t4s";
const std::string TESTNET_DEV_FUND = "J4ybSs4riSB3eZVddhG32kRDD35UPvjfDj";
const std::string REGTEST_DEV_FUND = "J4ybSs4riSB3eZVddhG32kRDD35UPvjfDj";

// Developer fund script (initialized based on network)
CScript DEVELOPER_FUND_SCRIPT;

void InitializeDeveloperFundScript()
{
    const CChainParams& chainparams = Params();
    std::string devFundAddress;

    if (chainparams.NetworkIDString() == "main") {
        devFundAddress = MAINNET_DEV_FUND;
    } else if (chainparams.NetworkIDString() == "test") {
        devFundAddress = TESTNET_DEV_FUND;
    } else {
        devFundAddress = REGTEST_DEV_FUND;
    }

    CBitcoinAddress address(devFundAddress);
    if (!address.IsValid()) {
        LogPrintf("Invalid developer fund address: %s\n", devFundAddress);
        return;
    }

    DEVELOPER_FUND_SCRIPT = GetScriptForDestination(address.Get());
}

CAmount CStakeReward::GetBlockReward(int nHeight)
{
    return FIXED_BLOCK_REWARD;
}

CAmount CStakeReward::GetDeveloperReward(int nHeight)
{
    return GetBlockReward(nHeight) / 2;
}

CAmount CStakeReward::GetStakerReward(int nHeight)
{
    return GetBlockReward(nHeight) / 2;
}

CTxOut CStakeReward::CreateDeveloperOutput(int nHeight)
{
    return CTxOut(GetDeveloperReward(nHeight), DEVELOPER_FUND_SCRIPT);
}

CTxOut CStakeReward::CreateStakerOutput(int nHeight, const CScript& scriptPubKey)
{
    return CTxOut(GetStakerReward(nHeight), scriptPubKey);
}

bool CStakeReward::CreateRewardOutputs(int nHeight, CMutableTransaction& txNew, const CScript& scriptPubKey)
{
    // Add developer fund output
    txNew.vout.push_back(CreateDeveloperOutput(nHeight));

    // Add staker output
    txNew.vout.push_back(CreateStakerOutput(nHeight, scriptPubKey));

    return true;
}

bool CStakeReward::CheckRewardOutputs(const CTransaction& tx, int nHeight, CValidationState& state)
{
    // Check if transaction has at least 2 outputs
    if (tx.vout.size() < 2) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-reward-outputs");
    }

    // Check developer fund output
    if (tx.vout[0].nValue != GetDeveloperReward(nHeight) ||
        tx.vout[0].scriptPubKey != DEVELOPER_FUND_SCRIPT) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-developer-reward");
    }

    // Check staker output
    if (tx.vout[1].nValue != GetStakerReward(nHeight)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-txns-staker-reward");
    }

    return true;
}

CAmount CStakeReward::GetTotalReward(int nHeight)
{
    return GetBlockReward(nHeight);
}

bool CStakeReward::CheckBlockRewards(const CBlock& block, int nHeight, CValidationState& state)
{
    // Check if block has at least one transaction
    if (block.vtx.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-reward-tx");
    }

    // Check reward outputs in the first transaction
    return CheckRewardOutputs(*block.vtx[0], nHeight, state);
}