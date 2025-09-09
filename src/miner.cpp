// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core Developers
// Copyright (c) 2020-2021 Hive Coin Developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "hash.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/policy.h"
#include "pow.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "sync.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validation.h"
#include "validationinterface.h"

// POS includes
#include "blocksignature.h"
#include "kernel.h"
#include "stakeconsensus.h"
#include "stakeinput.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
// #include "wallet/rpcwallet.h"
#endif


#include <algorithm>
#include <boost/thread.hpp>
#include <queue>
#include <utility>


extern std::vector<CWalletRef> vpwallets;

// Global mining variables
bool fGenerateBitcoins = false;
bool fStakeableCoins = false;

// Add these declarations at the top of the file after includes
uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
uint64_t nLastBlockWeight = 0;
uint64_t nBlockWeight = 0;
uint64_t nBlockSigOpsCost = 0;
CAmount nFees = 0;
CTxMemPool::setEntries inBlock;
bool fIncludeWitness = false;
int nHeight = 0;
int64_t nLockTimeCutoff = 0;
CFeeRate blockMinFeeRate;

// Add these declarations at the top of the file after includes
extern CTxMemPool mempool;
extern CChainState g_chainstate;

// Forward declarations

void ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams);
void BitcoinMiner(const CChainParams& chainparams);

//////////////////////////////////////////////////////////////////////////////
//
// CloreMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest fee rate of a transaction combined with all
// its ancestors.

uint64_t nMiningTimeStart = 0;
uint64_t nHashesPerSec = 0;
uint64_t nHashesDone = 0;


int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev)
{
    int64_t nOldTime = pblock->nTime;
    int64_t nNewTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

    if (nOldTime < nNewTime)
        pblock->nTime = nNewTime;

    // Updating time can change work required on testnet:
    if (consensusParams.fPowAllowMinDifficultyBlocks)
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, consensusParams);

    return nNewTime - nOldTime;
}

BlockAssembler::Options::Options()
    : nBlockMaxWeight(MAX_BLOCK_WEIGHT), nBlockMaxSize(MAX_BLOCK_SERIALIZED_SIZE), fIncludeWitness(true), blockMinFeeRate(CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE))
{
    blockMinFeeRate = CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE);
    nBlockMaxWeight = GetMaxBlockWeight() - 4000;
}

BlockAssembler::BlockAssembler(const CChainParams& params, const CTxMemPool& mempool)
    : chainparams(params), m_mempool(mempool), nBlockMaxWeight(GetMaxBlockWeight() - 4000), blockMinFeeRate(CFeeRate(DEFAULT_BLOCK_MIN_TX_FEE)), nBlockMaxSize(GetMaxBlockWeight() - 4000), fIncludeWitness(true)
{
}

BlockAssembler::BlockAssembler(const CChainParams& params, const Options& options)
    : chainparams(params), m_mempool(::mempool), nBlockMaxWeight(std::max<size_t>(4000, std::min<size_t>(GetMaxBlockWeight() - 4000, options.nBlockMaxWeight))), blockMinFeeRate(options.blockMinFeeRate), nBlockMaxSize(options.nBlockMaxSize), fIncludeWitness(options.fIncludeWitness)
{
}


void BlockAssembler::resetBlock()
{
    inBlock.clear();

    // Reserve space for coinbase tx
    nBlockWeight = 4000;
    nBlockSigOpsCost = 400;
    fIncludeWitness = false;

    // These counters do not include coinbase tx
    nBlockTx = 0;
    nFees = 0;
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx)
{
    return CreateNewBlock(scriptPubKeyIn, fMineWitnessTx, true);
}

std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx, bool /*fIncludeWitness*/)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());

    if (!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1);       // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : pblock->GetBlockTime();

    // Decide whether to include witness transactions
    // This is only needed in case the witness softfork activation is reverted
    // (which would require a very deep reorganization) or when
    // -promiscuousmempoolflags is used.
    // TODO: replace this with a call to main to assess validity of a mempool
    // transaction (which in most cases can be a no-op).
    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus()) && fMineWitnessTx;

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    int64_t nTime1 = GetTimeMicros();

    nLastBlockTx = nBlockTx;
    nLastBlockWeight = nBlockWeight;

    // CLORE START
    //  Coinbase TX is created
    CMutableTransaction coinbaseTx;
    coinbaseTx.vin.resize(1);
    coinbaseTx.vin[0].prevout.SetNull();
    // vout
    CAmount nSubsidy = GetBlockSubsidy(nHeight, chainparams.GetConsensus());
    CAmount nCommunityAutonomousAmount = GetParams().CommunityAutonomousAmount();

    coinbaseTx.vout.resize(2);
    coinbaseTx.vout[0].scriptPubKey = scriptPubKeyIn;
    coinbaseTx.vout[0].nValue = nFees + ((100 - nCommunityAutonomousAmount) * nSubsidy / 100);

    // Assign the set % in chainparams.cpp to the TX
    std::string GetCommunityAutonomousAddress = GetParams().CommunityAutonomousAddress();
    CTxDestination destCommunityAutonomous = DecodeDestination(GetCommunityAutonomousAddress);
    if (!IsValidDestination(destCommunityAutonomous)) {
        LogPrintf("IsValidDestination: Invalid Clore address %s \n", GetCommunityAutonomousAddress);
    }
    // We need to parse the address ready to send to it
    CScript scriptPubKeyCommunityAutonomous = GetScriptForDestination(destCommunityAutonomous);

    coinbaseTx.vout[1].scriptPubKey = scriptPubKeyCommunityAutonomous;
    coinbaseTx.vout[1].nValue = nSubsidy * nCommunityAutonomousAmount / 100;
    LogPrintf("nSubsidy: ====================================================\n");
    LogPrintf("Miner: %ld \n", coinbaseTx.vout[0].nValue);
    LogPrintf("scriptPubKeyIn: %s \n", HexStr(scriptPubKeyIn));

    LogPrintf("GetCommunityAutonomousAddress: %s \n", GetCommunityAutonomousAddress);
    LogPrintf("scriptPubKeyCommunityAutonomous: %s \n", HexStr(scriptPubKeyCommunityAutonomous));
    LogPrintf("nCommunityAutonomousAmount: %ld \n", coinbaseTx.vout[1].nValue);
    coinbaseTx.vin[0].scriptSig = CScript() << nHeight << OP_0;

    pblock->vtx[0] = MakeTransactionRef(std::move(coinbaseTx));
    pblocktemplate->vchCoinbaseCommitment = GenerateCoinbaseCommitment(*pblock, pindexPrev, chainparams.GetConsensus());
    pblocktemplate->vTxFees[0] = -nFees;

    LogPrintf("CreateNewBlock(): block weight: %u txs: %u fees: %ld sigops %d\n", GetBlockWeight(*pblock), nBlockTx, nFees, nBlockSigOpsCost);
    // CLORE END

    LogPrintf("DEBUG: Starting block header fill\n");
    // Fill in header
    pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    LogPrintf("DEBUG: Set hashPrevBlock\n");
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    LogPrintf("DEBUG: Updated time\n");
    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
    LogPrintf("DEBUG: Set nBits\n");
    pblock->nNonce = 0;
    pblock->nNonce64 = 0;
    pblock->nHeight = nHeight;
    LogPrintf("DEBUG: Set nonce and height\n");
    pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);
    LogPrintf("DEBUG: Set sigop cost\n");

    LogPrintf("DEBUG: About to call TestBlockValidity\n");
    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        if (state.IsTransactionError()) {
            if (gArgs.GetBoolArg("-autofixmempool", false)) {
                {
                    TRY_LOCK(mempool.cs, fLockMempool);
                    if (fLockMempool) {
                        LogPrintf("%s failed because of a transaction %s. -autofixmempool is set to true. Clearing the mempool\n", __func__,
                            state.GetFailedTransaction().GetHex());
                        mempool.clear();
                    }
                }
            } else {
                {
                    TRY_LOCK(mempool.cs, fLockMempool);
                    if (fLockMempool) {
                        auto mempoolTx = mempool.get(state.GetFailedTransaction());
                        if (mempoolTx) {
                            LogPrintf("%s : Failed because of a transaction %s. Trying to remove the transaction from the mempool\n", __func__, state.GetFailedTransaction().GetHex());
                            mempool.removeRecursive(*mempoolTx, MemPoolRemovalReason::CONFLICT);
                        }
                    }
                }
            }
        }
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }
    int64_t nTime2 = GetTimeMicros();

    LogPrint(BCLog::BENCH, "CreateNewBlock() packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n", 0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated, 0.001 * (nTime2 - nTime1), 0.001 * (nTime2 - nTimeStart));

    return std::move(pblocktemplate);
}

#ifdef ENABLE_WALLET
// POS-specific block creation
std::unique_ptr<CBlockTemplate> BlockAssembler::CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake, std::vector<CStakeableOutput>* availableCoins)
{
    int64_t nTimeStart = GetTimeMicros();

    resetBlock();

    pblocktemplate.reset(new CBlockTemplate());
    if (!pblocktemplate.get())
        return nullptr;
    pblock = &pblocktemplate->block; // pointer for convenience

    // Add dummy coinbase tx as first transaction
    pblock->vtx.emplace_back();
    pblocktemplate->vTxFees.push_back(-1);       // updated at end
    pblocktemplate->vTxSigOpsCost.push_back(-1); // updated at end

    LOCK2(cs_main, mempool.cs);
    CBlockIndex* pindexPrev = chainActive.Tip();
    assert(pindexPrev != nullptr);
    nHeight = pindexPrev->nHeight + 1;

    pblock->nVersion = ComputeBlockVersion(pindexPrev, chainparams.GetConsensus());
    if (chainparams.MineBlocksOnDemand())
        pblock->nVersion = gArgs.GetArg("-blockversion", pblock->nVersion);

    pblock->nTime = GetAdjustedTime();
    const int64_t nMedianTimePast = pindexPrev->GetMedianTimePast();

    nLockTimeCutoff = (STANDARD_LOCKTIME_VERIFY_FLAGS & LOCKTIME_MEDIAN_TIME_PAST) ? nMedianTimePast : pblock->GetBlockTime();

    // For POS blocks, we need to solve proof of stake first
    if (fProofOfStake) {
#ifdef ENABLE_WALLET
        if (!SolveProofOfStake(pblock, pindexPrev, pwallet, availableCoins)) {
            return nullptr;
        }
        // POS blocks already have coinbase and coinstake transactions
        pblocktemplate->vTxFees[0] = 0;
        pblocktemplate->vTxSigOpsCost[0] = WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[0]);
#else
        return nullptr; // Wallet required for staking
#endif
        if (pblock->vtx.size() > 1) {
            pblocktemplate->vTxFees.push_back(0);
            pblocktemplate->vTxSigOpsCost.push_back(WITNESS_SCALE_FACTOR * GetLegacySigOpCount(*pblock->vtx[1]));
        }
    } else {
        // Regular POW block creation - should not happen in POS phase
        return CreateNewBlock(scriptPubKeyIn, true);
    }

    // Decide whether to include witness transactions
    fIncludeWitness = IsWitnessEnabled(pindexPrev, chainparams.GetConsensus());

    int nPackagesSelected = 0;
    int nDescendantsUpdated = 0;
    addPackageTxs(nPackagesSelected, nDescendantsUpdated);

    // Fill in header
    pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    UpdateTime(pblock, chainparams.GetConsensus(), pindexPrev);
    if (!fProofOfStake) {
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, chainparams.GetConsensus());
        pblock->nNonce = 0;
        pblock->nNonce64 = 0;
    }
    pblock->nHeight = nHeight;

    // Sign the block if it's POS
    if (fProofOfStake) {
        if (!SignBlock(*pblock, *pwallet)) {
            return error("CreateNewBlock(): Failed to sign POS block"), nullptr;
        }
    }

    CValidationState state;
    if (!TestBlockValidity(state, chainparams, *pblock, pindexPrev, false, false)) {
        throw std::runtime_error(strprintf("%s: TestBlockValidity failed: %s", __func__, FormatStateMessage(state)));
    }

    int64_t nTime1 = GetTimeMicros();
    LogPrint(BCLog::BENCH, "CreateNewBlock() POS packages: %.2fms (%d packages, %d updated descendants), validity: %.2fms (total %.2fms)\n",
        0.001 * (nTime1 - nTimeStart), nPackagesSelected, nDescendantsUpdated,
        0.001 * (GetTimeMicros() - nTime1), 0.001 * (GetTimeMicros() - nTimeStart));

    return std::move(pblocktemplate);
}
#endif // ENABLE_WALLET

void BlockAssembler::onlyUnconfirmed(CTxMemPool::setEntries& testSet)
{
    for (CTxMemPool::setEntries::iterator iit = testSet.begin(); iit != testSet.end();) {
        // Only test txs not already in the block
        if (inBlock.count(*iit)) {
            testSet.erase(iit++);
        } else {
            iit++;
        }
    }
}

bool BlockAssembler::TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const
{
    // TODO: switch to weight-based accounting for packages instead of vsize-based accounting.
    if (nBlockWeight + WITNESS_SCALE_FACTOR * packageSize >= nBlockMaxWeight)
        return false;
    if (nBlockSigOpsCost + packageSigOpsCost >= MAX_BLOCK_SIGOPS_COST)
        return false;
    return true;
}

// Perform transaction-level checks before adding to block:
// - transaction finality (locktime)
// - premature witness (in case segwit transactions are added to mempool before
//   segwit activation)
bool BlockAssembler::TestPackageTransactions(const CTxMemPool::setEntries& package)
{
    for (const CTxMemPool::txiter it : package) {
        if (!IsFinalTx(it->GetTx(), nHeight, nLockTimeCutoff))
            return false;
        if (!fIncludeWitness && it->GetTx().HasWitness())
            return false;
    }
    return true;
}

void BlockAssembler::AddToBlock(CTxMemPool::txiter iter)
{
    pblock->vtx.emplace_back(iter->GetSharedTx());
    pblocktemplate->vTxFees.push_back(iter->GetFee());
    pblocktemplate->vTxSigOpsCost.push_back(iter->GetSigOpCost());
    nBlockWeight += iter->GetTxWeight();
    ++nBlockTx;
    nBlockSigOpsCost += iter->GetSigOpCost();
    nFees += iter->GetFee();
    inBlock.insert(iter);

    bool fPrintPriority = gArgs.GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);
    if (fPrintPriority) {
        LogPrintf("fee %s txid %s\n",
            CFeeRate(iter->GetModifiedFee(), iter->GetTxSize()).ToString(),
            iter->GetTx().GetHash().ToString());
    }
}

int BlockAssembler::UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded,
    indexed_modified_transaction_set& mapModifiedTx)
{
    int nDescendantsUpdated = 0;
    for (const CTxMemPool::txiter it : alreadyAdded) {
        CTxMemPool::setEntries descendants;
        mempool.CalculateDescendants(it, descendants);
        // Insert all descendants (not yet in block) into the modified set
        for (CTxMemPool::txiter desc : descendants) {
            if (alreadyAdded.count(desc))
                continue;
            ++nDescendantsUpdated;
            modtxiter mit = mapModifiedTx.find(desc);
            if (mit == mapModifiedTx.end()) {
                CTxMemPoolModifiedEntry modEntry(desc);
                modEntry.nSizeWithAncestors -= it->GetTxSize();
                modEntry.nModFeesWithAncestors -= it->GetModifiedFee();
                modEntry.nSigOpCostWithAncestors -= it->GetSigOpCost();
                mapModifiedTx.insert(modEntry);
            } else {
                mapModifiedTx.modify(mit, update_for_parent_inclusion(it));
            }
        }
    }
    return nDescendantsUpdated;
}

// Skip entries in mapTx that are already in a block or are present
// in mapModifiedTx (which implies that the mapTx ancestor state is
// stale due to ancestor inclusion in the block)
// Also skip transactions that we've already failed to add. This can happen if
// we consider a transaction in mapModifiedTx and it fails: we can then
// potentially consider it again while walking mapTx.  It's currently
// guaranteed to fail again, but as a belt-and-suspenders check we put it in
// failedTx and avoid re-evaluation, since the re-evaluation would be using
// cached size/sigops/fee values that are not actually correct.
bool BlockAssembler::SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set& mapModifiedTx, CTxMemPool::setEntries& failedTx)
{
    assert(it != mempool.mapTx.end());
    return mapModifiedTx.count(it) || inBlock.count(it) || failedTx.count(it);
}

void BlockAssembler::SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries)
{
    // Sort package by ancestor count
    // If a transaction A depends on transaction B, then A's ancestor count
    // must be greater than B's.  So this is sufficient to validly order the
    // transactions for block inclusion.
    sortedEntries.clear();
    sortedEntries.insert(sortedEntries.begin(), package.begin(), package.end());
    std::sort(sortedEntries.begin(), sortedEntries.end(), CompareTxIterByAncestorCount());
}

// This transaction selection algorithm orders the mempool based
// on feerate of a transaction including all unconfirmed ancestors.
// Since we don't remove transactions from the mempool as we select them
// for block inclusion, we need an alternate method of updating the feerate
// of a transaction with its not-yet-selected ancestors as we go.
// This is accomplished by walking the in-mempool descendants of selected
// transactions and storing a temporary modified state in mapModifiedTxs.
// Each time through the loop, we compare the best transaction in
// mapModifiedTxs with the next transaction in the mempool to decide what
// transaction package to work on next.
void BlockAssembler::addPackageTxs(int& nPackagesSelected, int& nDescendantsUpdated)
{
    // mapModifiedTx will store sorted packages after they are modified
    // because some of their txs are already in the block
    indexed_modified_transaction_set mapModifiedTx;
    // Keep track of entries that failed inclusion, to avoid duplicate work
    CTxMemPool::setEntries failedTx;

    // Start by adding all descendants of previously added txs to mapModifiedTx
    // and modifying them for their already included ancestors
    UpdatePackagesForAdded(inBlock, mapModifiedTx);

    CTxMemPool::indexed_transaction_set::index<ancestor_score>::type::iterator mi = mempool.mapTx.get<ancestor_score>().begin();
    CTxMemPool::txiter iter;

    // Limit the number of attempts to add transactions to the block when it is
    // close to full; this is just a simple heuristic to finish quickly if the
    // mempool has a lot of entries.
    const int64_t MAX_CONSECUTIVE_FAILURES = 1000;
    int64_t nConsecutiveFailed = 0;

    while (mi != mempool.mapTx.get<ancestor_score>().end() || !mapModifiedTx.empty()) {
        // First try to find a new transaction in mapTx to evaluate.
        if (mi != mempool.mapTx.get<ancestor_score>().end() &&
            SkipMapTxEntry(mempool.mapTx.project<0>(mi), mapModifiedTx, failedTx)) {
            ++mi;
            continue;
        }

        // Now that mi is not stale, determine which transaction to evaluate:
        // the next entry from mapTx, or the best from mapModifiedTx?
        bool fUsingModified = false;

        modtxscoreiter modit = mapModifiedTx.get<ancestor_score>().begin();
        if (mi == mempool.mapTx.get<ancestor_score>().end()) {
            // We're out of entries in mapTx; use the entry from mapModifiedTx
            iter = modit->iter;
            fUsingModified = true;
        } else {
            // Try to compare the mapTx entry to the mapModifiedTx entry
            iter = mempool.mapTx.project<0>(mi);
            if (modit != mapModifiedTx.get<ancestor_score>().end() &&
                CompareModifiedEntry()(*modit, CTxMemPoolModifiedEntry(iter))) {
                // The best entry in mapModifiedTx has higher score
                // than the one from mapTx.
                // Switch which transaction (package) to consider
                iter = modit->iter;
                fUsingModified = true;
            } else {
                // Either no entry in mapModifiedTx, or it's worse than mapTx.
                // Increment mi for the next loop iteration.
                ++mi;
            }
        }

        // We skip mapTx entries that are inBlock, and mapModifiedTx shouldn't
        // contain anything that is inBlock.
        assert(!inBlock.count(iter));

        uint64_t packageSize = iter->GetSizeWithAncestors();
        CAmount packageFees = iter->GetModFeesWithAncestors();
        int64_t packageSigOpsCost = iter->GetSigOpCostWithAncestors();
        if (fUsingModified) {
            packageSize = modit->nSizeWithAncestors;
            packageFees = modit->nModFeesWithAncestors;
            packageSigOpsCost = modit->nSigOpCostWithAncestors;
        }

        if (packageFees < blockMinFeeRate.GetFee(packageSize)) {
            // Everything else we might consider has a lower fee rate
            return;
        }

        if (!TestPackage(packageSize, packageSigOpsCost)) {
            if (fUsingModified) {
                // Since we always look at the best entry in mapModifiedTx,
                // we must erase failed entries so that we can consider the
                // next best entry on the next loop iteration
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }

            ++nConsecutiveFailed;

            if (nConsecutiveFailed > MAX_CONSECUTIVE_FAILURES && nBlockWeight >
                                                                     nBlockMaxWeight - 4000) {
                // Give up if we're close to full and haven't succeeded in a while
                break;
            }
            continue;
        }

        CTxMemPool::setEntries ancestors;
        uint64_t nNoLimit = std::numeric_limits<uint64_t>::max();
        std::string dummy;
        mempool.CalculateMemPoolAncestors(*iter, ancestors, nNoLimit, nNoLimit, nNoLimit, nNoLimit, dummy, false);

        onlyUnconfirmed(ancestors);
        ancestors.insert(iter);

        // Test if all tx's are Final
        if (!TestPackageTransactions(ancestors)) {
            if (fUsingModified) {
                mapModifiedTx.get<ancestor_score>().erase(modit);
                failedTx.insert(iter);
            }
            continue;
        }

        // This transaction will make it in; reset the failed counter.
        nConsecutiveFailed = 0;

        // Package can be added. Sort the entries in a valid order.
        std::vector<CTxMemPool::txiter> sortedEntries;
        SortForBlock(ancestors, iter, sortedEntries);

        for (size_t i = 0; i < sortedEntries.size(); ++i) {
            AddToBlock(sortedEntries[i]);
            // Erase from the modified set, if present
            mapModifiedTx.erase(sortedEntries[i]);
        }

        ++nPackagesSelected;

        // Update transactions that depend on each of these
        nDescendantsUpdated += UpdatePackagesForAdded(ancestors, mapModifiedTx);
    }
}

void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(*pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = MakeTransactionRef(std::move(txCoinbase));
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

CWallet* GetFirstWallet()
{
#ifdef ENABLE_WALLET
    while (vpwallets.size() == 0) {
        MilliSleep(100);
    }
    if (vpwallets.size() == 0)
        return (NULL);
    return (vpwallets[0]);
#endif
    return (NULL);
}

void ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0]->vout[0].nValue));

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash()) {
            LogPrintf("ProcessBlockFound: generated block is stale\n");
            return;
        }
    }

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    bool fNewBlock = false;
    if (!ProcessNewBlock(chainparams, std::make_shared<const CBlock>(*pblock), true, &fNewBlock) || !fNewBlock) {
        LogPrintf("ProcessBlockFound: ProcessNewBlock, block not accepted\n");
        return;
    }
}

// GetNumCores() function moved to util.cpp to avoid duplicate definition

#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
double dHashesPerSec = 0.0;
int64_t nHPSTimerStart = 0;

std::unique_ptr<CBlockTemplate> CreateNewBlockWithKey(std::shared_ptr<CReserveScript>& reservekey, CWallet* pwallet)
{
    return CreateNewBlockWithScript(reservekey->reserveScript, pwallet);
}

std::unique_ptr<CBlockTemplate> CreateNewBlockWithScript(const CScript& coinbaseScript, CWallet* pwallet)
{
    const int nHeightNext = chainActive.Tip()->nHeight + 1;

    // PoS-only phase: Completely disable PoW mining after PoS infrastructure is ready
    if (GetParams().GetConsensus().IsPurePosActive(nHeightNext)) {
        LogPrintf("%s: PoS-only phase active - PoW mining permanently disabled\n", __func__);
        return nullptr;
    }

    // Infrastructure phases: PoW continues to secure network while PoS builds up
    // - Phase 1: Validators can be created (PoW secure)
    // - Phase 2: Staking enabled (PoW still secure)
    return BlockAssembler(GetParams(), mempool).CreateNewBlock(coinbaseScript);
}

// Create coinstake transaction
static CMutableTransaction NewCoinbase(const int nHeight, const CScript* pScriptPubKey = nullptr)
{
    CMutableTransaction txCoinbase;
    txCoinbase.vout.emplace_back();
    txCoinbase.vout[0].SetNull();
    if (pScriptPubKey) txCoinbase.vout[0].scriptPubKey = *pScriptPubKey;
    txCoinbase.vin.emplace_back();
    txCoinbase.vin[0].scriptSig = CScript() << nHeight << OP_0;
    return txCoinbase;
}

#ifdef ENABLE_WALLET
bool SolveProofOfStake(CBlock* pblock, CBlockIndex* pindexPrev, CWallet* pwallet, std::vector<CStakeableOutput>* availableCoins)
{
    boost::this_thread::interruption_point();

    assert(pindexPrev);
    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock, GetParams().GetConsensus());

    // Sync wallet before create coinstake
    // TODO: Add wallet sync check when available in CLORE

    CMutableTransaction txCoinStake;
    unsigned int nTxNewTime = 0;
    if (!pwallet->CreateCoinStake(*pwallet,
            pblock->nBits,
            60, // search interval in seconds
            txCoinStake,
            nTxNewTime)) {
        LogPrint(BCLog::STAKING, "%s : stake not found\n", __func__);
        return false;
    }
    // Stake found

    // Create coinbase tx
    CMutableTransaction txCoinbase = NewCoinbase(pindexPrev->nHeight + 1);

    // Sign coinstake
    if (!pwallet->SignCoinStake(*pwallet, uint256(), txCoinStake)) {
        const COutPoint& stakeIn = txCoinStake.vin[0].prevout;
        return error("Unable to sign coinstake with input %s-%d", stakeIn.hash.ToString(), stakeIn.n);
    }

    pblock->vtx.emplace_back(MakeTransactionRef(txCoinbase));
    pblock->vtx.emplace_back(MakeTransactionRef(txCoinStake));
    pblock->nTime = nTxNewTime;
    return true;
}
#endif

#ifdef ENABLE_WALLET
void CheckForCoins(CWallet* pwallet, std::vector<CStakeableOutput>* availableCoins)
{
    if (pwallet && availableCoins) {
        LOCK2(cs_main, pwallet->cs_wallet);
        fStakeableCoins = pwallet->StakeableCoins(availableCoins);
    }
}
#endif

void CloreMiner(const CChainParams& chainparams)
{
    static CCriticalSection cs;
    static std::unique_ptr<CBlockTemplate> pblocktemplate;

    // Get wallet and setup variables
    CWallet* pwallet = GetFirstWallet();
    std::shared_ptr<CReserveScript> pReservekey;
    std::vector<CStakeableOutput> availableCoins;

    // Each thread has its own key and counter


    while (true) {
        if (chainparams.MiningRequiresPeers()) {
            // Busy-wait for the network to come online so we don't waste time mining
            // on an obsolete chain. In regtest mode we expect to fly solo.
            do {
                bool fvNodesEmpty;
                {
                    fvNodesEmpty = g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0;
                }
                if (!fvNodesEmpty && !IsInitialBlockDownload())
                    break;
                MilliSleep(1000);
            } while (true);
        }

        //
        // Create new block
        //

        bool fProofOfStake = true; // Always POS in this context
        std::unique_ptr<CBlockTemplate> pblocktemplate((fProofOfStake ?
                                                            BlockAssembler(GetParams(), mempool).CreateNewBlock(CScript(), pwallet, true, &availableCoins) :
                                                            CreateNewBlockWithKey(pReservekey, pwallet)));
        if (!pblocktemplate) continue;
        std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>(pblocktemplate->block);

        // POS - block found: process it
        if (fProofOfStake) {
            LogPrintf("%s : proof-of-stake block was signed %s \n", __func__, pblock->GetHash().ToString().c_str());
            SetThreadPriority(THREAD_PRIORITY_NORMAL);
            ProcessBlockFound(pblock.get(), GetParams());
        }
        SetThreadPriority(THREAD_PRIORITY_LOWEST);
        continue;

        // POW - miner main would go here
        // For now, skip POW mining as we're in POS mode
        MilliSleep(1000); // Sleep to prevent tight loop
    }
}

void static ThreadCloreMiner(void* /*parg*/)
{
    boost::this_thread::interruption_point();
    try {
        CloreMiner(GetParams());
        boost::this_thread::interruption_point();
    } catch (const std::exception& e) {
        LogPrintf("CloreMiner exception\n");
    } catch (...) {
        LogPrintf("CloreMiner exception\n");
    }

    LogPrintf("CloreMiner exiting\n");
}

void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads)
{
    static boost::thread_group* minerThreads = nullptr;
    fGenerateBitcoins = fGenerate;

    if (minerThreads != nullptr) {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = nullptr;
    }

    if (nThreads == 0 || !fGenerate)
        return;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(std::bind(&ThreadCloreMiner, pwallet));
}

void ThreadStakeMinter()
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadStakeMinter started\n");
    try {
        CloreMiner(GetParams());
        boost::this_thread::interruption_point();
    } catch (const std::exception& e) {
        LogPrintf("ThreadStakeMinter() exception \n");
    } catch (...) {
        LogPrintf("ThreadStakeMinter() error \n");
    }
    LogPrintf("ThreadStakeMinter exiting,\n");
}

#endif // ENABLE_WALLET

// CLORE-specific mining function
int GenerateClores(bool fGenerate, int nThreads, const CChainParams& chainparams)
{
    static boost::thread_group* minerThreads = nullptr;

    int numCores = GetNumCores();
    if (nThreads < 0)
        nThreads = numCores;

    if (minerThreads != nullptr) {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = nullptr;
    }

    if (nThreads == 0 || !fGenerate)
        return numCores;

#ifdef ENABLE_WALLET
    minerThreads = new boost::thread_group();

    // Reset metrics
    nHPSTimerStart = GetTimeMillis();
    dHashesPerSec = 0;

    for (int i = 0; i < nThreads; i++) {
        minerThreads->create_thread(boost::bind(&CloreMiner, boost::cref(chainparams)));
    }
#else
    LogPrintf("Mining/staking disabled - wallet support not compiled\n");
#endif // ENABLE_WALLET

    return numCores;
}
