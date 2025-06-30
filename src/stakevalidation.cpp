// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "stakevalidation.h"
#include "kernel.h"
#include "stakeinput.h"
#include "util.h"
#include "validation.h"

bool CStakeValidator::CheckStake(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if block is a proof of stake block
    if (!block.IsProofOfStake()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-block");
    }

    // Check if block has a valid stake transaction
    if (!CheckStakeTx(*block.vtx[1], state, consensusParams)) {
        return false;
    }

    // Check if block has a valid stake input
    CStakeInput* stakeInput = nullptr;
    if (!LoadStakeInput(block, stakeInput)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-input");
    }

    // Check if stake input is valid
    if (!CheckStakeInput(*stakeInput, state, consensusParams)) {
        delete stakeInput;
        return false;
    }

    // Check if stake modifier is valid
    if (!CheckStakeModifier(stakeInput->GetModifier(), state, consensusParams)) {
        delete stakeInput;
        return false;
    }

    // Check if stake kernel is valid
    if (!CheckStakeKernel(*stakeInput, stakeInput->GetModifier(), state, consensusParams)) {
        delete stakeInput;
        return false;
    }

    delete stakeInput;
    return true;
}

bool CStakeValidator::CheckStakeTx(const CTransaction& tx, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if transaction is a valid stake transaction
    if (!tx.IsCoinStake()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-tx");
    }

    // Check if transaction has a valid stake input
    if (tx.vin.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-tx-input");
    }

    // Check if transaction has a valid stake output
    if (tx.vout.empty()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-tx-output");
    }

    // Check if stake output is valid
    if (!CheckStakeOutput(tx.vout[0], state, consensusParams)) {
        return false;
    }

    return true;
}

bool CStakeValidator::CheckStakeInput(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake input is valid
    if (!stakeInput.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-input");
    }

    // Check if stake age is valid
    if (!CheckStakeAge(stakeInput, state, consensusParams)) {
        return false;
    }

    // Check if stake value is valid
    if (!CheckStakeValue(stakeInput, state, consensusParams)) {
        return false;
    }

    // Check if stake time is valid
    if (!CheckStakeTime(stakeInput, state, consensusParams)) {
        return false;
    }

    return true;
}

bool CStakeValidator::CheckStakeOutput(const CTxOut& txout, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake output is valid
    if (!txout.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-output");
    }

    // Check if stake output has a valid script
    if (!txout.scriptPubKey.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-output-script");
    }

    return true;
}

bool CStakeValidator::CheckStakeModifier(const uint256& stakeModifier, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake modifier is valid
    if (stakeModifier.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-modifier");
    }

    return true;
}

bool CStakeValidator::CheckStakeKernel(const CStakeInput& stakeInput, const uint256& stakeModifier, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake kernel is valid
    if (!CheckKernelHash(stakeInput, stakeModifier, state, consensusParams)) {
        return false;
    }

    return true;
}

bool CStakeValidator::CheckStakeAge(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake age is valid
    if (stakeInput.GetAge() < consensusParams.nStakeMinAge) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-age");
    }

    return true;
}

bool CStakeValidator::CheckStakeValue(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake value is valid
    if (stakeInput.GetValue() < consensusParams.nStakeMinValue) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-value");
    }

    return true;
}

bool CStakeValidator::CheckStakeTime(const CStakeInput& stakeInput, CValidationState& state, const Consensus::Params& consensusParams)
{
    // Check if stake time is valid
    if (stakeInput.GetTime() < consensusParams.nStakeMinTime) {
        return state.DoS(100, false, REJECT_INVALID, "bad-stake-time");
    }

    return true;
}

bool CStakeValidator::CheckStakeBlock(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams)
{
    return CheckStake(block, state, consensusParams);
}

bool CStakeValidator::CheckStakeTransaction(const CTransaction& tx, CValidationState& state, const Consensus::Params& consensusParams)
{
    return CheckStakeTx(tx, state, consensusParams);
}