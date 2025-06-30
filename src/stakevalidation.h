// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_STAKEVALIDATION_H
#define CLORE_STAKEVALIDATION_H

#include "consensus/params.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "stakeinput.h"
#include "validation.h"

class CStakeValidator
{
public:
    // Check if a block is a valid proof of stake block
    static bool CheckStake(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a transaction is a valid stake transaction
    static bool CheckStakeTx(const CTransaction& tx, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake input is valid
    static bool CheckStakeInput(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake output is valid
    static bool CheckStakeOutput(const CTxOut& txout, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake modifier is valid
    static bool CheckStakeModifier(const uint256& stakeModifier, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake kernel is valid
    static bool CheckStakeKernel(const CStakeInput& stakeInput, const uint256& stakeModifier, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake age is valid
    static bool CheckStakeAge(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake value is valid
    static bool CheckStakeValue(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake time is valid
    static bool CheckStakeTime(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake block is valid
    static bool CheckStakeBlock(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake transaction is valid
    static bool CheckStakeTransaction(const CTransaction& tx, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake input is valid
    static bool CheckStakeInput(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake output is valid
    static bool CheckStakeOutput(const CTxOut& txout, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake modifier is valid
    static bool CheckStakeModifier(const uint256& stakeModifier, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake kernel is valid
    static bool CheckStakeKernel(const CStakeInput& stakeInput, const uint256& stakeModifier, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake age is valid
    static bool CheckStakeAge(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake value is valid
    static bool CheckStakeValue(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);

    // Check if a stake time is valid
    static bool CheckStakeTime(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams);
};

#endif // CLORE_STAKEVALIDATION_H