// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2015-2020 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_CONSENSUS_PARAMS_H
#define CLORE_CONSENSUS_PARAMS_H

#include "amount.h"
#include "uint256.h"
#include <boost/optional.hpp>
#include <map>
#include <string>

namespace Consensus
{

enum DeploymentPos {
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_ASSETS,          // Deployment of HIP2
    DEPLOYMENT_MSG_REST_ASSETS, // Deployment of HIP5 and Restricted assets
    DEPLOYMENT_TRANSFER_SCRIPT_SIZE,
    DEPLOYMENT_ENFORCE_VALUE,
    DEPLOYMENT_COINBASE_ASSETS,
    DEPLOYMENT_POS, // Deployment of Proof of Stake consensus
    // DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
    // DEPLOYMENT_SEGWIT, // Deployment of BIP141, BIP143, and BIP147.
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Index into Params.vUpgrades and NetworkUpgradeInfo
 *
 * Being array indices, these MUST be numbered consecutively.
 *
 * The order of these indices MUST match the order of the upgrades on-chain, as
 * several functions depend on the enum being sorted.
 */
enum UpgradeIndex : uint32_t {
    BASE_NETWORK,
    ENABLE_POS_STAKING,       // Proof of Stake preparation phase (hybrid PoW/PoS)
    UPGRADE_POS_V2,           // Proof of Stake v2 (improved stake modifier)
    ENABLE_POS_REWARDS,       // Proof of Stake completion phase (PoW disabled completely)
    UPGRADE_BIP65,            // BIP65 CLTV activation
    ENABLE_POS_VALIDATORS,    // Stake modifier v2
    ENABLE_POS_TIME_PROTO_v2, // Time protocol v2
    UPGRADE_TESTDUMMY,        // Test dummy upgrade
    // NOTE: Also add new upgrades to NetworkUpgradeInfo in upgrades.cpp
    MAX_NETWORK_UPGRADES
};

struct NetworkUpgrade {
    /**
     * The first protocol version which will understand the new consensus rules
     */
    int nProtocolVersion;

    /**
     * Height of the first block for which the new consensus rules will be active
     */
    int nActivationHeight;

    /**
     * Special value for nActivationHeight indicating that the upgrade is always active.
     */
    static constexpr int ALWAYS_ACTIVE = 0;

    /**
     * Special value for nActivationHeight indicating that the upgrade will never activate.
     */
    static constexpr int NO_ACTIVATION_HEIGHT = -1;

    /**
     * The hash of the block at height nActivationHeight, if known.
     */
    boost::optional<uint256> hashActivationBlock;
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
    /** Use to override the confirmation window on a specific BIP */
    uint32_t nOverrideMinerConfirmationWindow;
    /** Use to override the the activation threshold on a specific BIP */
    uint32_t nOverrideRuleChangeActivationThreshold;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height and hash at which BIP34 becomes active */
    bool nBIP34Enabled;
    bool nBIP65Enabled;
    bool nBIP66Enabled;
    int BIP34LockedIn;
    // uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    // int BIP65Height;
    /** Block height at which BIP66 becomes active */
    // int BIP66Height;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    uint256 kawpowLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t nPowTargetSpacing;
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval() const { return nPowTargetTimespan / nPowTargetSpacing; }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;
    bool nSegwitEnabled;
    bool nCSVEnabled;

    // Proof of Stake parameters
    uint256 posLimitV1;
    uint256 posLimitV2;
    int nStakeMinAge;
    int nStakeMinDepth;
    int64_t nTargetTimespan;
    int64_t nTargetTimespanV2;
    int nTimeSlotLength;
    int nFutureTimeDriftPoW;
    int nFutureTimeDriftPoS;
    int nStakeTimestampMask; // Mask for stake timestamps

    // Validator parameters
    int nBudgetCycleBlocks;
    int nBudgetFeeConfirmations;
    int nCoinbaseMaturity;
    CAmount nMaxMoneyOut;
    int64_t nProposalEstablishmentTime;
    int nMaxProposalPayments;

    // spork keys
    std::string strSporkPubKey;
    std::string strSporkPubKeyOld;
    int64_t nTime_EnforceNewSporkKey;
    int64_t nTime_RejectOldSporkKey;

    // height-based activations
    int height_last_invalid_UTXO;

    // validation by-pass
    int64_t nCloreBadBlockTime;
    unsigned int nCloreBadBlockBits;

    // Map with network updates
    NetworkUpgrade vUpgrades[MAX_NETWORK_UPGRADES];

    int64_t TargetTimespan(const bool fV2 = true) const { return fV2 ? nTargetTimespanV2 : nTargetTimespan; }
    uint256 ProofOfStakeLimit(const bool fV2) const { return fV2 ? posLimitV2 : posLimitV1; }
    bool MoneyRange(const CAmount& nValue) const { return (nValue >= 0 && nValue <= nMaxMoneyOut); }
    bool IsTimeProtocolV2(const int nHeight) const { return NetworkUpgradeActive(nHeight, ENABLE_POS_TIME_PROTO_v2); }
    bool IsPurePosActive(const int nHeight) const { return NetworkUpgradeActive(nHeight, ENABLE_POS_REWARDS); }

    int FutureBlockTimeDrift(const int nHeight) const
    {
        // PoS (TimeV2): 14 seconds
        if (IsTimeProtocolV2(nHeight)) return nTimeSlotLength - 1;
        // PoS (TimeV1): 3 minutes - PoW: 2 hours
        return (NetworkUpgradeActive(nHeight, ENABLE_POS_STAKING) ? nFutureTimeDriftPoS : nFutureTimeDriftPoW);
    }

    bool IsValidBlockTimeStamp(const int64_t nTime, const int nHeight) const
    {
        // Before time protocol V2, blocks can have arbitrary timestamps
        if (!IsTimeProtocolV2(nHeight)) return true;
        // Time protocol v2 requires time in slots
        return (nTime % nTimeSlotLength) == 0;
    }

    bool HasStakeMinAgeOrDepth(const int contextHeight, const uint32_t contextTime, const int utxoFromBlockHeight, const uint32_t utxoFromBlockTime) const
    {
        // before stake modifier V2, we require the utxo to be nStakeMinAge old
        if (!NetworkUpgradeActive(contextHeight, Consensus::ENABLE_POS_VALIDATORS))
            return (utxoFromBlockTime + nStakeMinAge <= contextTime);
        // with stake modifier V2+, we require the utxo to be nStakeMinDepth deep in the chain
        return (contextHeight - utxoFromBlockHeight >= nStakeMinDepth);
    }

    bool NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const;
};
} // namespace Consensus

#endif // CLORE_CONSENSUS_PARAMS_H
