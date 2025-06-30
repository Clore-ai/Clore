// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core Developers
// Copyright (c) 2020-2021 Hive Coin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"

#include "arith_uint256.h"
#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"

#include "chainparamsseeds.h"
#include <assert.h>

// TODO: Take these out
extern double algoHashTotal[16];
extern int algoHashHits[16];


static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << CScriptNum(0) << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    const char* pszTimestamp = "The Times 03/30/2021 Bitcoin is name of the game for new generation of firms";
    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

void CChainParams::UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    consensus.vDeployments[d].nStartTime = nStartTime;
    consensus.vDeployments[d].nTimeout = nTimeout;
}

void CChainParams::TurnOffSegwit()
{
    consensus.nSegwitEnabled = false;
}

void CChainParams::TurnOffCSV()
{
    consensus.nCSVEnabled = false;
}

void CChainParams::TurnOffBIP34()
{
    consensus.nBIP34Enabled = false;
}

void CChainParams::TurnOffBIP65()
{
    consensus.nBIP65Enabled = false;
}

void CChainParams::TurnOffBIP66()
{
    consensus.nBIP66Enabled = false;
}

bool CChainParams::BIP34()
{
    return consensus.nBIP34Enabled;
}

bool CChainParams::BIP65()
{
    return consensus.nBIP34Enabled;
}

bool CChainParams::BIP66()
{
    return consensus.nBIP34Enabled;
}

bool CChainParams::CSVEnabled() const
{
    return consensus.nCSVEnabled;
}


/**
 * Main network
 */
/**
 * What makes a good checkpoint block?
 * + Is surrounded by blocks with reasonable timestamps
 *   (no blocks before with a timestamp after, none after with
 *    timestamp before)
 */

class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 2100000; //~ 4 yrs at 1 min block time
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true; //
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = true;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.kawpowLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // Estimated starting diff for first 180 kawpow blocks
        consensus.nPowTargetTimespan = 2016 * 60;                                                             // 1.4 days
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1613; // Approx 80% of 2016
        consensus.nMinerConfirmationWindow = 2016;       // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1653004800; // Friday, 20 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1653264000;   // Monday, 23 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideRuleChangeActivationThreshold = 1814;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].bit = 6;                 // Assets (HIP2)
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nStartTime = 1653004800; // Friday, 20 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nTimeout = 1653264000;   // Monday, 23 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nOverrideRuleChangeActivationThreshold = 1814;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].bit = 7;                                       // Assets (HIP5)
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nStartTime = 1653004800;                       // Friday, 20 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nTimeout = 1653264000;                         // Monday, 23 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nOverrideRuleChangeActivationThreshold = 1714; // Approx 85% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nStartTime = 1653004800;                       // Friday, 20 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nTimeout = 1653264000;                         // Monday, 23 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nOverrideRuleChangeActivationThreshold = 1714; // Approx 85% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nStartTime = 1653004800;                       // Friday, 20 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nTimeout = 1653264000;                         // Monday, 23 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nOverrideRuleChangeActivationThreshold = 1411; // Approx 70% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nStartTime = 1653004800;                       // Friday, 20 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nTimeout = 1653264000;                         // Monday, 23 May 2022 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nOverrideRuleChangeActivationThreshold = 1411; // Approx 70% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nOverrideMinerConfirmationWindow = 2016;

        // PoS deployment - initially set to very high activation threshold to prevent activation until network is ready
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].bit = 11;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nStartTime = 1719072000;                            // Sunday, 23 June 2024 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nTimeout = 1750608000;                              // Sunday, 23 June 2025 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideRuleChangeActivationThreshold = 999999999; // Effectively disabled until lowered
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideMinerConfirmationWindow = 2016;

        // POS consensus parameters
        consensus.posLimitV1 = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimitV2 = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nStakeMinAge = 60 * 60;              // 1 hour minimum stake age
        consensus.nStakeMinDepth = 100;                // 100 blocks minimum depth
        consensus.nTargetTimespan = 14 * 24 * 60 * 60; // 2 weeks
        consensus.nTargetTimespanV2 = 30 * 60;         // 30 minutes
        consensus.nTimeSlotLength = 15;                // 15 seconds per slot
        consensus.nFutureTimeDriftPoW = 2 * 60 * 60;   // 2 hours
        consensus.nFutureTimeDriftPoS = 3 * 60;        // 3 minutes
        consensus.nStakeTimestampMask = 15;            // 15 second timestamp mask

        // Validator and budget parameters
        consensus.nBudgetCycleBlocks = 30240; // 21 days at 1 minute blocks
        consensus.nBudgetFeeConfirmations = 6;
        consensus.nCoinbaseMaturity = 100;                   // 100 blocks maturity
        consensus.nMaxMoneyOut = 25000000 * COIN;            // 25 million max money
        consensus.nProposalEstablishmentTime = 60 * 60 * 24; // 24 hours
        consensus.nMaxProposalPayments = 6;

        // Network upgrades
        consensus.vUpgrades[Consensus::BASE_NETWORK] = {0, 0, {}};
        consensus.vUpgrades[Consensus::ENABLE_POS_VALIDATORS] = {70002, 1000001439, {}};    // Validator activation (VALIDATOR) at block 1,000,001,439 (+1440 after DEPLOYMENT_POS)
        consensus.vUpgrades[Consensus::ENABLE_POS_STAKING] = {70002, 1000002879, {}};       // Staking logic activation (STAKING) at block 1,000,002,879 (+1440 blocks)
        consensus.vUpgrades[Consensus::ENABLE_POS_TIME_PROTO_v2] = {70002, 1000002879, {}}; // Time protocol v2 (merged with STAKING phase)
        consensus.vUpgrades[Consensus::ENABLE_POS_REWARDS] = {70002, 1000004319, {}};       // PoS enabled, PoW disabled (REWARDS) at block 1,000,004,319 (+1440 blocks)

        consensus.BIP34LockedIn = 6048; // Locked_in at height 6048


        // The best chain should have at least this much work
        // consensus.nMinimumChainWork = uint256S("0000000000000000000000000000000000000000000000001395dd8e70866177"); // Block 157581

        // By default assume that the signatures in ancestors of this block are valid. Block#
        // consensus.defaultAssumeValid = uint256S("0x0000000000008ea299bed393aaeedcdac66baf26c7228c60636fa432addc4777"); // Block 157581

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0x41; // A
        pchMessageStart[1] = 0x49; // I
        pchMessageStart[2] = 0x41; // A
        pchMessageStart[3] = 0x49; // I
        nDefaultPort = 8788;
        nPruneAfterHeight = 100000;

        uint32_t nGenesisTime = 1651442858;

        genesis = CreateGenesisBlock(nGenesisTime, 3244753, 0x1e00ffff, 4, 5000 * COIN);
        consensus.hashGenesisBlock = genesis.GetX16RHash();
        assert(consensus.hashGenesisBlock == uint256S("0000000a50fdaaf22f1c98b8c61559e15ab2269249aa1fb20683180703cdbf07"));
        assert(genesis.hashMerkleRoot == uint256S("7c1d71731b98c560a80cee3b88993c8c863342b9661894304fd843bf7e75a41f"));


        vSeeds.emplace_back("seed.clore.ai", false);
        vSeeds.emplace_back("seed1.clore.ai", false);
        vSeeds.emplace_back("seed2.clore.ai", false);

        // Address start with A
        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 23);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 122);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 112);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        // CLORE Blockchain BIP44 cointype in mainnet is '1313'
        nExtCoinType = 1313;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = true;

        checkpointData = (CCheckpointData){
            {{{0, uint256S("0000000a50fdaaf22f1c98b8c61559e15ab2269249aa1fb20683180703cdbf07")},
                {2, uint256S("003714ec51ec4bd78e1b548bf1c198711ef973d248b6bef7b5fd17a091e27e6f")},
                {3960, uint256S("00000000fa933b399211df8adc614d69ab0fd7ed4cce194e1fce0f7045fcc8db")}}}};

        chainTxData = ChainTxData{
            // Update as we know more about the contents of the Clore chain
            1662386772,         // * UNIX timestamp of last known number of transactions 2021-06-18 22:03:06 UTC
            0,                  // * total number of transactions between genesis and that timestamp
                                //   (the tx=... number in the SetBestChain debug.log lines)
            0.05014635153727871 // * estimated number of transactions per second after that timestamp
        };


        // Burn Amounts
        nIssueAssetBurnAmount = 500 * COIN;
        nReissueAssetBurnAmount = 100 * COIN;
        nIssueSubAssetBurnAmount = 100 * COIN;
        nIssueUniqueAssetBurnAmount = 5 * COIN;
        nIssueMsgChannelAssetBurnAmount = 100 * COIN;
        nIssueQualifierAssetBurnAmount = 1000 * COIN;
        nIssueSubQualifierAssetBurnAmount = 100 * COIN;
        nIssueRestrictedAssetBurnAmount = 1500 * COIN;
        nAddNullQualifierTagBurnAmount = .1 * COIN;

        // 50% of mined coins towards rewarding CLORE.AI users
        nCommunityAutonomousAmount = 50;

        // Burn Addresses
        // TODO: Remove these addresses
        strIssueAssetBurnAddress = "AP6RNAdjGgkX2QERU3Gr5VV5hvidu6xgau";
        strReissueAssetBurnAddress = "AKsyQ9K9Kxftcb77Veiv91kA2VugPY45PL";
        strIssueSubAssetBurnAddress = "AbXjGsYEt89DUARDsQoXLAB3t4EpKUd1D8";
        strIssueUniqueAssetBurnAddress = "APZ5XSUwfKXDtscpoPbWfNkeiNu3FFu6ee";
        strIssueMsgChannelAssetBurnAddress = "AVPHkMz1GCxqE85ZuoxsBWY62Fi1ygyBnG";
        strIssueQualifierAssetBurnAddress = "AXEv5tmqu6cnaskJbmrEEPKQGTnCkWBBTk";
        strIssueSubQualifierAssetBurnAddress = "AM2okBkzJb21QyMGepGqmintGNnCJuVoQs";
        strIssueRestrictedAssetBurnAddress = "AMR2ckKABVwQnhdFaQiQaqfoqAQLSZdV2T";
        strAddNullQualifierTagBurnAddress = "AcjqNXmzBpoBCGgfzSMJqwZLnYiF4zoqtL";
        // Global Burn Address
        strGlobalBurnAddress = "AZuJi37imwSjTFBwExtJ12tG1BvSnUctZg";
        // ProofOfGame Address
        strCommunityAutonomousAddress = "AePr762UcuQrGoa3TRQpGMX6byRjuXw97A";

        // DGW Activation
        nDGWActivationBlock = 1;

        nMaxReorganizationDepth = 60; // 60 at 1 minute block timespan is +/- 60 minutes.
        nMinReorganizationPeers = 4;
        nMinReorganizationAge = 60 * 60 * 12; // 12 hours

        nAssetActivationHeight = 1;     // Asset activated block height
        nMessagingActivationBlock = 1;  // Messaging activated block height
        nRestrictedActivationBlock = 1; // Restricted activated block height

        nKAAAWWWPOWActivationTime = 1651444217; // 2021-05-03 06:00:18
        nKAWPOWActivationTime = nKAAAWWWPOWActivationTime;

        /** VALIDATOR AUTHORIZATION - MAINNET **/
        // Initial authorized validators for mainnet (Phase 2 - controlled rollout)
        // Authorization based on collateral addresses, not IP addresses for flexibility
        vAuthorizedValidators = {
            AuthorizedValidator("clore-validator-01", "ATsQHm7qbMSe4gJnx8W5SnLNP9bKLmgD52", "Official Clore Foundation Node 1"),
            AuthorizedValidator("clore-validator-02", "AXLFQxg7Vo8BpMKNp3LWNVfAVrE4KaAPnR", "Official Clore Foundation Node 2"),
            AuthorizedValidator("clore-validator-03", "AUVHqV4yw8qz6x8VYLPM2mEuUqgHMQWGqX", "Official Clore Foundation Node 3"),
            AuthorizedValidator("clore-validator-04", "AYqA6vMLbmtqRQtGKh5XJnVxhYzZo6F8jS", "Clore Partner Node 1"),
            AuthorizedValidator("clore-validator-05", "AZWwYw5LjUuN8k9s3jKqGKTNaRvRKsZyMM", "Clore Partner Node 2")};
    }
};

/**
 * Testnet (v7)
 */
class CTestNetParams : public CChainParams
{
public:
    CTestNetParams()
    {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 2100000; //~ 4 yrs at 1 min block time
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true; //
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = true;
        consensus.nCSVEnabled = true;
        consensus.powLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.kawpowLimit = uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // Estimated starting diff for first 180 kawpow blocks
        consensus.nPowTargetTimespan = 2016 * 60;                                                             // 1.4 days
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = false;
        consensus.fPowNoRetargeting = false;
        consensus.nRuleChangeActivationThreshold = 1613; // Approx 80% of 2016
        consensus.nMinerConfirmationWindow = 2016;       // nPowTargetTimespan / nPowTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1662998400; // Monday, 12 September 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1665590400;   // Wednesday, 12 October 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideRuleChangeActivationThreshold = 1814;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].bit = 6;                 // Assets (HIP2)
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nStartTime = 1662998400; // Monday, 12 September 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nTimeout = 1665590400;   // Wednesday, 12 October 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nOverrideRuleChangeActivationThreshold = 1814;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].bit = 7;                                       // Assets (HIP5)
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nStartTime = 1662998400;                       // Monday, 12 September 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nTimeout = 1665590400;                         // Wednesday, 12 October 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nOverrideRuleChangeActivationThreshold = 1714; // Approx 85% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nStartTime = 1662998400;                       // Monday, 12 September 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nTimeout = 1665590400;                         // Wednesday, 12 October 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nOverrideRuleChangeActivationThreshold = 1714; // Approx 85% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nStartTime = 1662998400;                       // Monday, 12 September 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nTimeout = 1665590400;                         // Wednesday, 12 October 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nOverrideRuleChangeActivationThreshold = 1411; // Approx 70% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nOverrideMinerConfirmationWindow = 2016;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nStartTime = 1662998400;                       // Monday, 12 September 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nTimeout = 1665590400;                         // Wednesday, 12 October 2022 16:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nOverrideRuleChangeActivationThreshold = 1411; // Approx 70% of 2016
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nOverrideMinerConfirmationWindow = 2016;

        // PoS deployment for testnet - easier activation for testing
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].bit = 11;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nStartTime = 1719072000;                       // Sunday, 23 June 2024 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nTimeout = 1750608000;                         // Sunday, 23 June 2025 00:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideRuleChangeActivationThreshold = 1411; // Approx 70% of 2016 for testing
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideMinerConfirmationWindow = 2016;

        // POS consensus parameters (same as mainnet)
        consensus.posLimitV1 = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimitV2 = uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nStakeMinAge = 60 * 60;              // 1 hour minimum stake age
        consensus.nStakeMinDepth = 100;                // 100 blocks minimum depth
        consensus.nTargetTimespan = 14 * 24 * 60 * 60; // 2 weeks
        consensus.nTargetTimespanV2 = 30 * 60;         // 30 minutes
        consensus.nTimeSlotLength = 15;                // 15 seconds per slot
        consensus.nFutureTimeDriftPoW = 2 * 60 * 60;   // 2 hours
        consensus.nFutureTimeDriftPoS = 3 * 60;        // 3 minutes
        consensus.nStakeTimestampMask = 15;            // 15 second timestamp mask

        // Validator and budget parameters
        consensus.nBudgetCycleBlocks = 30240; // 21 days at 1 minute blocks
        consensus.nBudgetFeeConfirmations = 6;
        consensus.nCoinbaseMaturity = 100;                   // 100 blocks maturity
        consensus.nMaxMoneyOut = 25000000 * COIN;            // 25 million max money
        consensus.nProposalEstablishmentTime = 60 * 60 * 24; // 24 hours
        consensus.nMaxProposalPayments = 6;

        // Network upgrades (testnet - 1440 block spacing for proper testing)
        consensus.vUpgrades[Consensus::BASE_NETWORK] = {0, 0, {}};
        consensus.vUpgrades[Consensus::ENABLE_POS_VALIDATORS] = {70002, 1000, {}};    // Validator activation (VALIDATOR phase)
        consensus.vUpgrades[Consensus::ENABLE_POS_STAKING] = {70002, 2440, {}};       // Staking logic activation (STAKING phase) +1440 blocks
        consensus.vUpgrades[Consensus::ENABLE_POS_TIME_PROTO_v2] = {70002, 2440, {}}; // Time protocol v2 (merged with staking)
        consensus.vUpgrades[Consensus::ENABLE_POS_REWARDS] = {70002, 3880, {}};       // PoW disabled (REWARDS phase) +1440 blocks

        consensus.BIP34LockedIn = 8064; // Locked_in at height 8064

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0x60;
        pchMessageStart[1] = 0x63;
        pchMessageStart[2] = 0x56;
        pchMessageStart[3] = 0x65;
        nDefaultPort = 4568;
        nPruneAfterHeight = 1000;

        uint32_t nGenesisTime = 1670019499;

        genesis = CreateGenesisBlock(nGenesisTime, 11903232, 0x1e00ffff, 4, 5000 * COIN);
        consensus.hashGenesisBlock = genesis.GetX16RHash();

        // assert(consensus.hashGenesisBlock == uint256S("00000065c2d5777fe4f059f9ac7579b35c1ad4c7042aebae8c105179cca0f8f0"));
        // assert(genesis.hashMerkleRoot == uint256S("7c1d71731b98c560a80cee3b88993c8c863342b9661894304fd843bf7e75a41f"));

        vFixedSeeds.clear();
        vSeeds.clear();
        vSeeds.emplace_back("testnet.clore.ai", false);

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 42);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 124);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 114);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Clore BIP44 cointype in testnet
        nExtCoinType = 1;

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fMiningRequiresPeers = true;

        checkpointData = (CCheckpointData){
            {}};

        chainTxData = ChainTxData{
            // Update as we know more about the contents of the Clore chain
            0, // * UNIX timestamp of last known number of transactions
            0, // * total number of transactions between genesis and that timestamp
               //   (the tx=... number in the SetBestChain debug.log lines)
            0  // * estimated number of transactions per second after that timestamp
        };

        // Remove asset deployment configuration
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].bit = 7; // Assets (HIP5)
        // Remove asset burn amounts
        nIssueAssetBurnAmount = 500 * COIN;
        nReissueAssetBurnAmount = 100 * COIN;
        nIssueSubAssetBurnAmount = 100 * COIN;
        nIssueUniqueAssetBurnAmount = 5 * COIN;
        nIssueMsgChannelAssetBurnAmount = 100 * COIN;
        nIssueQualifierAssetBurnAmount = 1000 * COIN;
        nIssueSubQualifierAssetBurnAmount = 100 * COIN;
        nIssueRestrictedAssetBurnAmount = 1500 * COIN;
        nAddNullQualifierTagBurnAmount = .1 * COIN;

        // Remove asset burn addresses
        strIssueAssetBurnAddress = "AP6RNAdjGgkX2QERU3Gr5VV5hvidu6xgau";
        strReissueAssetBurnAddress = "AKsyQ9K9Kxftcb77Veiv91kA2VugPY45PL";
        strIssueSubAssetBurnAddress = "AbXjGsYEt89DUARDsQoXLAB3t4EpKUd1D8";
        strIssueUniqueAssetBurnAddress = "APZ5XSUwfKXDtscpoPbWfNkeiNu3FFu6ee";
        strIssueMsgChannelAssetBurnAddress = "AVPHkMz1GCxqE85ZuoxsBWY62Fi1ygyBnG";
        strIssueQualifierAssetBurnAddress = "AXEv5tmqu6cnaskJbmrEEPKQGTnCkWBBTk";
        strIssueSubQualifierAssetBurnAddress = "AM2okBkzJb21QyMGepGqmintGNnCJuVoQs";
        strIssueRestrictedAssetBurnAddress = "AMR2ckKABVwQnhdFaQiQaqfoqAQLSZdV2T";
        strAddNullQualifierTagBurnAddress = "AcjqNXmzBpoBCGgfzSMJqwZLnYiF4zoqtL";
        // Global Burn Address
        strGlobalBurnAddress = "JGYQBki6wWWnJLp2dcgdtNZWs9a2e1nXM3";

        // CommunityAutonomousAddress
        strCommunityAutonomousAddress = "J8db9nuaVL3Jo8hDcfKh77pZnG2J8jvxWH";

        // DGW Activation
        nDGWActivationBlock = 1;

        nMaxReorganizationDepth = 60; // 60 at 1 minute block timespan is +/- 60 minutes.
        nMinReorganizationPeers = 4;
        nMinReorganizationAge = 60 * 60 * 12; // 12 hours

        nAssetActivationHeight = 1;     // Asset activated block height
        nMessagingActivationBlock = 1;  // Messaging activated block height
        nRestrictedActivationBlock = 1; // Restricted activated block height

        nKAAAWWWPOWActivationTime = 1653247613; // 2021-05-03 06:00:18
        nKAWPOWActivationTime = nKAAAWWWPOWActivationTime;

        /** VALIDATOR AUTHORIZATION - TESTNET **/
        // Test validators for testnet (easier testing)
        // Using testnet address format for authorization
        vAuthorizedValidators = {
            AuthorizedValidator("test-validator-01", "JTestNodeAddress1234567890123456789A", "Testnet Node 1"),
            AuthorizedValidator("test-validator-02", "JTestNodeAddress1234567890123456789B", "Testnet Node 2"),
            AuthorizedValidator("test-validator-03", "JTestNodeAddress1234567890123456789C", "Testnet Node 3")};
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams
{
public:
    CRegTestParams()
    {
        strNetworkID = "regtest";
        consensus.nBIP34Enabled = true;
        consensus.nBIP65Enabled = true; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.nBIP66Enabled = true;
        consensus.nSegwitEnabled = true;
        consensus.nCSVEnabled = true;
        consensus.nSubsidyHalvingInterval = 150;
        consensus.powLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.kawpowLimit = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 2016 * 60; // 1.4 days
        consensus.nPowTargetSpacing = 1 * 60;
        consensus.fPowAllowMinDifficultyBlocks = true;
        consensus.fPowNoRetargeting = true;
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144;       // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideRuleChangeActivationThreshold = 108;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nOverrideMinerConfirmationWindow = 144;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].bit = 6;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nOverrideRuleChangeActivationThreshold = 108;
        consensus.vDeployments[Consensus::DEPLOYMENT_ASSETS].nOverrideMinerConfirmationWindow = 144;
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].bit = 7;                    // Assets (HIP5)
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nStartTime = 0;             // GMT: Sun Mar 3, 2019 5:00:00 PM
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nTimeout = 999999999999ULL; // UTC: Wed Dec 25 2019 07:00:00
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nOverrideRuleChangeActivationThreshold = 108;
        consensus.vDeployments[Consensus::DEPLOYMENT_MSG_REST_ASSETS].nOverrideMinerConfirmationWindow = 144;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].bit = 8;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nOverrideRuleChangeActivationThreshold = 208;
        consensus.vDeployments[Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE].nOverrideMinerConfirmationWindow = 288;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].bit = 9;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nOverrideRuleChangeActivationThreshold = 108;
        consensus.vDeployments[Consensus::DEPLOYMENT_ENFORCE_VALUE].nOverrideMinerConfirmationWindow = 144;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].bit = 10;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nOverrideRuleChangeActivationThreshold = 108;
        consensus.vDeployments[Consensus::DEPLOYMENT_COINBASE_ASSETS].nOverrideMinerConfirmationWindow = 144;

        // PoS deployment for regtest - very easy activation for testing
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].bit = 11;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nTimeout = 999999999999ULL;
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideRuleChangeActivationThreshold = 108; // 75% for regtest
        consensus.vDeployments[Consensus::DEPLOYMENT_POS].nOverrideMinerConfirmationWindow = 144;

        // POS consensus parameters (easier for regtest)
        consensus.posLimitV1 = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.posLimitV2 = uint256S("7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nStakeMinAge = 1;                    // 1 second minimum stake age for testing
        consensus.nStakeMinDepth = 1;                  // 1 block minimum depth for testing
        consensus.nTargetTimespan = 14 * 24 * 60 * 60; // 2 weeks (same as others)
        consensus.nTargetTimespanV2 = 30 * 60;         // 30 minutes
        consensus.nTimeSlotLength = 15;                // 15 seconds per slot
        consensus.nFutureTimeDriftPoW = 2 * 60 * 60;   // 2 hours
        consensus.nFutureTimeDriftPoS = 3 * 60;        // 3 minutes
        consensus.nStakeTimestampMask = 15;            // 15 second timestamp mask

        // Validator and budget parameters (testing values)
        consensus.nBudgetCycleBlocks = 1440; // 1 day at 1 minute blocks
        consensus.nBudgetFeeConfirmations = 3;
        consensus.nCoinbaseMaturity = 10;              // 10 blocks maturity for testing
        consensus.nMaxMoneyOut = 25000000 * COIN;      // 25 million max money
        consensus.nProposalEstablishmentTime = 60 * 5; // 5 minutes for testing
        consensus.nMaxProposalPayments = 6;

        // Network upgrades (regtest - 50 block spacing for rapid testing)
        consensus.vUpgrades[Consensus::BASE_NETWORK] = {0, 0, {}};
        consensus.vUpgrades[Consensus::ENABLE_POS_VALIDATORS] = {70002, 100, {}};    // Validator activation (VALIDATOR phase)
        consensus.vUpgrades[Consensus::ENABLE_POS_STAKING] = {70002, 150, {}};       // Staking logic activation (STAKING phase) +50 blocks
        consensus.vUpgrades[Consensus::ENABLE_POS_TIME_PROTO_v2] = {70002, 150, {}}; // Time protocol v2 (merged with staking)
        consensus.vUpgrades[Consensus::ENABLE_POS_REWARDS] = {70002, 200, {}};       // PoW disabled (REWARDS phase) +50 blocks

        consensus.BIP34LockedIn = 0;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        pchMessageStart[0] = 0x44;
        pchMessageStart[1] = 0x52;
        pchMessageStart[2] = 0x4F;
        pchMessageStart[3] = 0x57;
        nDefaultPort = 19444;
        nPruneAfterHeight = 1000;


        genesis = CreateGenesisBlock(1524179366, 1, 0x207fffff, 4, 5000 * COIN);
        consensus.hashGenesisBlock = genesis.GetX16RHash();

        // assert(consensus.hashGenesisBlock == uint256S("0x0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c"));
        // assert(genesis.hashMerkleRoot == uint256S("0x28ff00a867739a352523808d301f504bc4547699398d70faf2266a8bae5f3516"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;

        checkpointData = (CCheckpointData){
            {}};

        chainTxData = ChainTxData{
            0,
            0,
            0};

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 42);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 124);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 114);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        // Clore BIP44 cointype in regtest
        nExtCoinType = 1;

        /** CLORE_BLOCKCHAIN Start **/
        // Burn Amounts
        nIssueAssetBurnAmount = 500 * COIN;
        nReissueAssetBurnAmount = 100 * COIN;
        nIssueSubAssetBurnAmount = 100 * COIN;
        nIssueUniqueAssetBurnAmount = 5 * COIN;
        nIssueMsgChannelAssetBurnAmount = 100 * COIN;
        nIssueQualifierAssetBurnAmount = 1000 * COIN;
        nIssueSubQualifierAssetBurnAmount = 100 * COIN;
        nIssueRestrictedAssetBurnAmount = 1500 * COIN;
        nAddNullQualifierTagBurnAmount = .1 * COIN;

        // 50% of mined coins towards rewarding CLORE.AI users
        nCommunityAutonomousAmount = 50;

        // Burn Addresses
        strIssueAssetBurnAddress = "J1VQJKLSLVZ4syiCAx5hEPq8BrkFaxAXAi";
        strReissueAssetBurnAddress = "J2yh4DiLETuVVDvpvBNSq3QCmHcdMmNEdp";
        strIssueSubAssetBurnAddress = "J3PE3FsHqfszvz7nhwK2Gc32wykrc7pNMA";
        strIssueUniqueAssetBurnAddress = "J4yKRTYF2nRryYEnupsNnQQmRKsQhdspYB";
        strIssueMsgChannelAssetBurnAddress = "J58ndjHjLYKHMszr4ehUg9YMWPAiXNEepa";
        strIssueQualifierAssetBurnAddress = "J68wpmVvdE6bMSkiCEDQWCHCKZs4VVdE2G";
        strIssueSubQualifierAssetBurnAddress = "J7MSidYgNJrPE15ouEsXPYXFYH2AAPXmhr";
        strIssueRestrictedAssetBurnAddress = "J8uX8jfZn14P1VNzh6YjSzLaRTQAdoFSHn";
        strAddNullQualifierTagBurnAddress = "J9CrKy8m548AvSbcv1mcn7tyJQkgcwVfj6";
        // Global Burn Address
        strGlobalBurnAddress = "JGYQBki6wWWnJLp2dcgdtNZWs9a2e1nXM3";

        // CommunityAutonomousAddress
        strCommunityAutonomousAddress = "JCPncGFawSDgP3CmG19MB6cbKP5XuhXY4u";

        // DGW Activation
        nDGWActivationBlock = 200;

        nMaxReorganizationDepth = 60; // 60 at 1 minute block timespan is +/- 60 minutes.
        nMinReorganizationPeers = 4;
        nMinReorganizationAge = 60 * 60 * 12; // 12 hours

        nAssetActivationHeight = 1;     // Asset activated block height
        nMessagingActivationBlock = 1;  // Messaging activated block height
        nRestrictedActivationBlock = 1; // Restricted activated block height

        // TODO, we need to figure out what to do with this for regtest. This effects the unit tests
        // For now we can use a timestamp very far away
        // If you are looking to test the kawpow hashing function in regtest. You will need to change this number
        nKAAAWWWPOWActivationTime = 3582830167;
        nKAWPOWActivationTime = nKAAAWWWPOWActivationTime;
        /** CLORE_BLOCKCHAIN End **/
    }
};

static std::unique_ptr<CChainParams> globalChainParams;

const CChainParams& GetParams()
{
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams());
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network, bool fForceBlockNetwork)
{
    SelectBaseParams(network);
    if (fForceBlockNetwork) {
        bNetwork.SetNetwork(network);
    }
    globalChainParams = CreateChainParams(network);
}

void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
{
    globalChainParams->UpdateVersionBitsParameters(d, nStartTime, nTimeout);
}

void TurnOffSegwit()
{
    globalChainParams->TurnOffSegwit();
}

void TurnOffCSV()
{
    globalChainParams->TurnOffCSV();
}

void TurnOffBIP34()
{
    globalChainParams->TurnOffBIP34();
}

void TurnOffBIP65()
{
    globalChainParams->TurnOffBIP65();
}

void TurnOffBIP66()
{
    globalChainParams->TurnOffBIP66();
}

/** VALIDATOR AUTHORIZATION Implementation **/
bool CChainParams::IsAuthorizedValidatorAddress(const std::string& pubkeyAddress) const
{
    // In regtest mode, all validators are authorized for testing
    if (strNetworkID == "regtest") {
        return true;
    }

    for (const auto& validator : vAuthorizedValidators) {
        if (validator.pubkeyAddress == pubkeyAddress) {
            return true;
        }
    }
    return false;
}

bool CChainParams::IsAuthorizedValidatorAlias(const std::string& alias) const
{
    // In regtest mode, all validators are authorized for testing
    if (strNetworkID == "regtest") {
        return true;
    }

    for (const auto& validator : vAuthorizedValidators) {
        if (validator.alias == alias) {
            return true;
        }
    }
    return false;
}

std::string CChainParams::GetAuthorizedValidatorAlias(const std::string& pubkeyAddress) const
{
    for (const auto& validator : vAuthorizedValidators) {
        if (validator.pubkeyAddress == pubkeyAddress) {
            return validator.alias;
        }
    }
    return ""; // Not found
}
