// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_STAKEREWARD_H
#define CLORE_STAKEREWARD_H

#include "consensus/amount.h"
#include "consensus/params.h"
#include "primitives/transaction.h"
#include "validation.h"

// Fixed block reward in satoshis (approximately 2.27 CLORE)
static const CAmount FIXED_BLOCK_REWARD = 227184770;

// Developer fund script (initialized based on network)
extern CScript DEVELOPER_FUND_SCRIPT;

// Initialize the developer fund script based on network
void InitializeDeveloperFundScript();

class CStakeReward
{
public:
    // Get the block reward for a given height
    static CAmount GetBlockReward(int nHeight);

    // Get the developer fund reward (50% of block reward)
    static CAmount GetDeveloperReward(int nHeight);

    // Get the staker reward (50% of block reward)
    static CAmount GetStakerReward(int nHeight);

    // Create the developer fund output
    static CTxOut CreateDeveloperOutput(int nHeight);

    // Create the staker output
    static CTxOut CreateStakerOutput(int nHeight, const CScript& scriptPubKey);

    // Create all reward outputs for a block
    static bool CreateRewardOutputs(int nHeight, CMutableTransaction& txNew, const CScript& scriptPubKey);

    // Check if a transaction has valid reward outputs
    static bool CheckRewardOutputs(const CTransaction& tx, int nHeight, CValidationState& state);

    // Get the total reward for a block
    static CAmount GetTotalReward(int nHeight);

    // Check if a block has valid rewards
    static bool CheckBlockRewards(const CBlock& block, int nHeight, CValidationState& state);
};

#endif // CLORE_STAKEREWARD_H