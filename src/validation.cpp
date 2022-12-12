// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core Developers
// Copyright (c) 2020-2021 Hive Coin Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validation.h"

#include "arith_uint256.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "cuckoocache.h"
#include "fs.h"
#include "hash.h"
#include "init.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "random.h"
#include "reverse_iterator.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "timedata.h"
#include "tinyformat.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "versionbits.h"
#include "warnings.h"
#include "net.h"

#include <atomic>
#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/thread.hpp>
#include <script/ismine.h>
#include <wallet/wallet.h>

#include "assets/assets.h"
#include "assets/assetdb.h"
#include "base58.h"

#include "assets/snapshotrequestdb.h"
#include "assets/assetsnapshotdb.h"

// Fixing Boost 1.73 compile errors
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

#if defined(NDEBUG)
# error "Clore cannot be compiled without assertions."
#endif

#define MICRO 0.000001
#define MILLI 0.001

#define CHECK_DUPLICATE_TRANSACTION_TRUE true
#define CHECK_DUPLICATE_TRANSACTION_FALSE false
#define CHECK_MEMPOOL_TRANSACTION_TRUE true
#define CHECK_MEMPOOL_TRANSACTION_FALSE false
#define CHECK_BLOCK_TRANSACTION_TRUE true
#define CHECK_BLOCK_TRANSACTION_FALSE false

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
bool is_windows=true;
#else
bool is_windows=false;
#endif

/**
 * Global state
 */


CCriticalSection cs_main;

BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex *pindexBestHeader = nullptr;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
std::atomic_bool fImporting(false);
std::atomic_bool fReindex(false);
bool fMessaging = true;
bool fTxIndex = false;
bool fAssetIndex = false;
bool fAddressIndex = false;
bool fTimestampIndex = false;
bool fSpentIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = DEFAULT_PERMIT_BAREMULTISIG;
bool fRequireStandard = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = DEFAULT_CHECKPOINTS_ENABLED;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;
bool fEnableReplacement = DEFAULT_ENABLE_REPLACEMENT;

bool fUnitTest = false;
bool log_all=false;

uint256 hashAssumeValid;
arith_uint256 nMinimumChainWork;

CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;

CBlockPolicyEstimator feeEstimator;
CTxMemPool mempool(&feeEstimator);

static void CheckBlockIndex(const Consensus::Params& consensusParams);

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "Clore Signed Message:\n";

// Internal stuff
namespace {

    struct CBlockIndexWorkComparator
    {
        bool operator()(const CBlockIndex *pa, const CBlockIndex *pb) const {
            // First sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */
    std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
    /** All pairs A->B, where A (or one of its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    std::multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile;
    int nLastBlockFile = 0;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    int32_t nBlockSequenceId = 1;
    /** Decreasing counter (used by subsequent preciousblock calls). */
    int32_t nBlockReverseSequenceId = -1;
    /** chainwork for the last block that preciousblock has been applied to. */
    arith_uint256 nLastPreciousChainwork = 0;

    /** In order to efficiently track invalidity of headers, we keep the set of
      * blocks which we tried to connect and found to be invalid here (ie which
      * were set to BLOCK_FAILED_VALID since the last restart). We can then
      * walk this set and check if a new header is a descendant of something in
      * this set, preventing us from having to walk mapBlockIndex when we try
      * to connect a bad block and fail.
      *
      * While this is more complicated than marking everything which descends
      * from an invalid block as invalid at the time we discover it to be
      * invalid, doing so would require walking all of mapBlockIndex to find all
      * descendants. Since this case should be very rare, keeping track of all
      * BLOCK_FAILED_VALID blocks in a set should be just fine and work just as
      * well.
      *
      * Because we alreardy walk mapBlockIndex in height-order at startup, we go
      * ahead and mark descendants of invalid blocks as FAILED_CHILD at that time,
      * instead of putting things in this set.
      */
    std::set<CBlockIndex*> g_failed_blocks;

    /** Dirty block index entries. */
    std::set<CBlockIndex*> setDirtyBlockIndex;

    /** Dirty block file entries. */
    std::set<int> setDirtyFileInfo;
} // anon namespace

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for (const uint256& hash : locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
            if (pindex->GetAncestor(chain.Height()) == chain.Tip()) {
                return chain.Tip();
            }
        }
    }
    return chain.Genesis();
}

CCoinsViewDB *pcoinsdbview = nullptr;
CCoinsViewCache *pcoinsTip = nullptr;
CBlockTreeDB *pblocktree = nullptr;

CAssetsDB *passetsdb = nullptr;
CAssetsCache *passets = nullptr;
CLRUCache<std::string, CDatabasedAssetData> *passetsCache = nullptr;
CLRUCache<std::string, CMessage> *pMessagesCache = nullptr;
CLRUCache<std::string, int> *pMessageSubscribedChannelsCache = nullptr;
CLRUCache<std::string, int> *pMessagesSeenAddressCache = nullptr;
CMessageDB *pmessagedb = nullptr;
CMessageChannelDB *pmessagechanneldb = nullptr;
CMyRestrictedDB *pmyrestricteddb = nullptr;
CSnapshotRequestDB *pSnapshotRequestDb = nullptr;
CAssetSnapshotDB *pAssetSnapshotDb = nullptr;
CDistributeSnapshotRequestDB *pDistributeSnapshotDb = nullptr;

CLRUCache<std::string, CNullAssetTxVerifierString> *passetsVerifierCache = nullptr;
CLRUCache<std::string, int8_t> *passetsQualifierCache = nullptr;
CLRUCache<std::string, int8_t> *passetsRestrictionCache = nullptr;
CLRUCache<std::string, int8_t> *passetsGlobalRestrictionCache = nullptr;
CRestrictedDB *prestricteddb = nullptr;

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

// See definition for documentation
static bool FlushStateToDisk(const CChainParams& chainParams, CValidationState &state, FlushStateMode mode, int nManualPruneHeight=0);
static void FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight);
static void FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight);
bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, std::vector<CScriptCheck> *pvChecks = nullptr);
static FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly = false);

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 requires that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = ((flags & LOCKTIME_MEDIAN_TIME_PAST) && chainActive.Tip())
                               ? chainActive.Tip()->GetMedianTimePast()
                               : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

bool TestLockPointValidity(const LockPoints* lp)
{
    AssertLockHeld(cs_main);
    assert(lp);
    // If there are relative lock times then the maxInputBlock will be set
    // If there are no relative lock times, the LockPoints don't depend on the chain
    if (lp->maxInputBlock) {
        // Check whether chainActive is an extension of the block at which the LockPoints
        // calculation was valid.  If not LockPoints are no longer valid
        if (!chainActive.Contains(lp->maxInputBlock)) {
            return false;
        }
    }

    // LockPoints still valid
    return true;
}

bool CheckSequenceLocks(const CTransaction &tx, int flags, LockPoints* lp, bool useExistingLockPoints)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(mempool.cs);

    CBlockIndex* tip = chainActive.Tip();
    assert(tip != nullptr);

    CBlockIndex index;
    index.pprev = tip;
    // CheckSequenceLocks() uses chainActive.Height()+1 to evaluate
    // height based locks because when SequenceLocks() is called within
    // ConnectBlock(), the height of the block *being*
    // evaluated is what is used.
    // Thus if we want to know if a transaction can be part of the
    // *next* block, we need to use one more than chainActive.Height()
    index.nHeight = tip->nHeight + 1;

    std::pair<int, int64_t> lockPair;
    if (useExistingLockPoints) {
        assert(lp);
        lockPair.first = lp->height;
        lockPair.second = lp->time;
    }
    else {
        // pcoinsTip contains the UTXO set for chainActive.Tip()
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);
        std::vector<int> prevheights;
        prevheights.resize(tx.vin.size());
        for (size_t txinIndex = 0; txinIndex < tx.vin.size(); txinIndex++) {
            const CTxIn& txin = tx.vin[txinIndex];
            Coin coin;
            if (!viewMemPool.GetCoin(txin.prevout, coin)) {
                return error("%s: Missing input", __func__);
            }
            if (coin.nHeight == MEMPOOL_HEIGHT) {
                // Assume all mempool transaction confirm in the next block
                prevheights[txinIndex] = tip->nHeight + 1;
            } else {
                prevheights[txinIndex] = coin.nHeight;
            }
        }
        lockPair = CalculateSequenceLocks(tx, flags, &prevheights, index);
        if (lp) {
            lp->height = lockPair.first;
            lp->time = lockPair.second;
            // Also store the hash of the block with the highest height of
            // all the blocks which have sequence locked prevouts.
            // This hash needs to still be on the chain
            // for these LockPoint calculations to be valid
            // Note: It is impossible to correctly calculate a maxInputBlock
            // if any of the sequence locked inputs depend on unconfirmed txs,
            // except in the special case where the relative lock time/height
            // is 0, which is equivalent to no sequence lock. Since we assume
            // input height of tip+1 for mempool txs and test the resulting
            // lockPair from CalculateSequenceLocks against tip+1.  We know
            // EvaluateSequenceLocks will fail if there was a non-zero sequence
            // lock on a mempool input, so we can use the return value of
            // CheckSequenceLocks to indicate the LockPoints validity
            int maxInputHeight = 0;
            for (int height : prevheights) {
                // Can ignore mempool inputs since we'll fail if they had non-zero locks
                if (height != tip->nHeight+1) {
                    maxInputHeight = std::max(maxInputHeight, height);
                }
            }
            lp->maxInputBlock = tip->GetAncestor(maxInputHeight);
        }
    }
    return EvaluateSequenceLocks(index, lockPair);
}

// Returns the script flags which should be checked for a given block
static unsigned int GetBlockScriptFlags(const CBlockIndex* pindex, const Consensus::Params& chainparams);

static void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age) {
    int expired = pool.Expire(GetTime() - age);
    if (expired != 0) {
        LogPrint(BCLog::MEMPOOL, "Expired %i transactions from the memory pool\n", expired);
    }

    std::vector<COutPoint> vNoSpendsRemaining;
    pool.TrimToSize(limit, &vNoSpendsRemaining);
    for (const COutPoint& removed : vNoSpendsRemaining)
        pcoinsTip->Uncache(removed);
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
        state.GetRejectReason(),
        state.GetDebugMessage().empty() ? "" : ", "+state.GetDebugMessage(),
        state.GetRejectCode());
}

static bool IsCurrentForFeeEstimation()
{
    AssertLockHeld(cs_main);
    if (IsInitialBlockDownload())
        return false;
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - MAX_FEE_ESTIMATION_TIP_AGE))
        return false;
    if (chainActive.Height() < pindexBestHeader->nHeight - 1)
        return false;
    return true;
}

/* Make mempool consistent after a reorg, by re-adding or recursively erasing
 * disconnected block transactions from the mempool, and also removing any
 * other transactions from the mempool that are no longer valid given the new
 * tip/height.
 *
 * Note: we assume that disconnectpool only contains transactions that are NOT
 * confirmed in the current chain nor already in the mempool (otherwise,
 * in-mempool descendants of such transactions would be removed).
 *
 * Passing fAddToMempool=false will skip trying to add the transactions back,
 * and instead just erase from the mempool as needed.
 */

void UpdateMempoolForReorg(DisconnectedBlockTransactions &disconnectpool, bool fAddToMempool)
{
    AssertLockHeld(cs_main);
    std::vector<uint256> vHashUpdate;
    // disconnectpool's insertion_order index sorts the entries from
    // oldest to newest, but the oldest entry will be the last tx from the
    // latest mined block that was disconnected.
    // Iterate disconnectpool in reverse, so that we add transactions
    // back to the mempool starting with the earliest transaction that had
    // been previously seen in a block.
    auto it = disconnectpool.queuedTx.get<insertion_order>().rbegin();
    while (it != disconnectpool.queuedTx.get<insertion_order>().rend()) {
        // ignore validation errors in resurrected transactions
        CValidationState stateDummy;
        if (!fAddToMempool || (*it)->IsCoinBase() ||
            !AcceptToMemoryPool(mempool, stateDummy, *it, nullptr /* pfMissingInputs */,
                                nullptr /* plTxnReplaced */, true /* bypass_limits */, 0 /* nAbsurdFee */)) {
            // If the transaction doesn't make it in to the mempool, remove any
            // transactions that depend on it (which would now be orphans).
            mempool.removeRecursive(**it, MemPoolRemovalReason::REORG);
        } else if (mempool.exists((*it)->GetHash())) {
            vHashUpdate.push_back((*it)->GetHash());
        }
        ++it;
    }
    disconnectpool.queuedTx.clear();
    // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in
    // the disconnectpool that were added back and cleans up the mempool state.
    mempool.UpdateTransactionsFromBlock(vHashUpdate);

    // We also need to remove any now-immature transactions
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    // Re-limit mempool size, in case we added any transactions
    LimitMempoolSize(mempool, gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
}

// Used to avoid mempool polluting consensus critical paths if CCoinsViewMempool
// were somehow broken and returning the wrong scriptPubKeys
static bool CheckInputsFromMempoolAndCache(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &view, CTxMemPool& pool,
                 unsigned int flags, bool cacheSigStore, PrecomputedTransactionData& txdata) {
    AssertLockHeld(cs_main);

    // pool.cs should be locked already, but go ahead and re-take the lock here
    // to enforce that mempool doesn't change between when we check the view
    // and when we actually call through to CheckInputs
    LOCK(pool.cs);

    assert(!tx.IsCoinBase());
    for (const CTxIn& txin : tx.vin) {
        const Coin& coin = view.AccessCoin(txin.prevout);

        // At this point we haven't actually checked if the coins are all
        // available (or shouldn't assume we have, since CheckInputs does).
        // So we just return failure if the inputs are not available here,
        // and then only have to check equivalence for available inputs.
        if (coin.IsSpent()) return false;

        const CTransactionRef& txFrom = pool.get(txin.prevout.hash);
        if (txFrom) {
            assert(txFrom->GetHash() == txin.prevout.hash);
            assert(txFrom->vout.size() > txin.prevout.n);
            assert(txFrom->vout[txin.prevout.n] == coin.out);
        } else {
            const Coin& coinFromDisk = pcoinsTip->AccessCoin(txin.prevout);
            assert(!coinFromDisk.IsSpent());
            assert(coinFromDisk.out == coin.out);
        }
    }

    return CheckInputs(tx, state, view, true, flags, cacheSigStore, true, txdata);
}

static bool AcceptToMemoryPoolWorker(const CChainParams& chainparams, CTxMemPool& pool, CValidationState& state, const CTransactionRef& ptx,
                              bool* pfMissingInputs, int64_t nAcceptTime, std::list<CTransactionRef>* plTxnReplaced,
                              bool bypass_limits, const CAmount& nAbsurdFee, std::vector<COutPoint>& coins_to_uncache, bool test_accept)
{
    const CTransaction& tx = *ptx;
    const uint256 hash = tx.GetHash();

    /** CLORE START */
    std::vector<std::pair<std::string, uint256>> vReissueAssets;
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    bool fCheckDuplicates = true;
    bool fCheckMempool = true;
    if (!CheckTransaction(tx, state, fCheckDuplicates, fCheckMempool))
        return false; // state filled in by CheckTransaction

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    // Reject transactions with witness before segregated witness activates (override with -prematurewitness)
    bool witnessEnabled = IsWitnessEnabled(chainActive.Tip(), chainparams.GetConsensus());
    if (!gArgs.GetBoolArg("-prematurewitness", false) && tx.HasWitness() && !witnessEnabled) {
        return state.DoS(0, false, REJECT_NONSTANDARD, "no-witness-yet", true);
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    std::string reason;
    if (fRequireStandard && !IsStandardTx(tx, reason, witnessEnabled))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // is it already in the memory pool?
    if (pool.exists(hash)) {
        return state.Invalid(false, REJECT_DUPLICATE, "txn-already-in-mempool");
    }

    // Check for conflicts with in-memory transactions
    std::set<uint256> setConflicts;
    {
    LOCK(pool.cs); // protect pool.mapNextTx
    for (const CTxIn &txin : tx.vin)
    {
        auto itConflicting = pool.mapNextTx.find(txin.prevout);
        if (itConflicting != pool.mapNextTx.end())
        {
            const CTransaction *ptxConflicting = itConflicting->second;
            if (!setConflicts.count(ptxConflicting->GetHash()))
            {
                // Allow opt-out of transaction replacement by setting
                // nSequence > MAX_BIP125_RBF_SEQUENCE (SEQUENCE_FINAL-2) on all inputs.
                //
                // SEQUENCE_FINAL-1 is picked to still allow use of nLockTime by
                // non-replaceable transactions. All inputs rather than just one
                // is for the sake of multi-party protocols, where we don't
                // want a single party to be able to disable replacement.
                //
                // The opt-out ignores descendants as anyone relying on
                // first-seen mempool behavior should be checking all
                // unconfirmed ancestors anyway; doing otherwise is hopelessly
                // insecure.
                bool fReplacementOptOut = true;
                if (fEnableReplacement)
                {
                    for (const CTxIn &_txin : ptxConflicting->vin)
                    {
                        if (_txin.nSequence <= MAX_BIP125_RBF_SEQUENCE)
                        {
                            fReplacementOptOut = false;
                            break;
                        }
                    }
                }
                if (fReplacementOptOut) {
                    return state.Invalid(false, REJECT_DUPLICATE, "txn-mempool-conflict");
                }

                setConflicts.insert(ptxConflicting->GetHash());
            }
        }
    }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        LockPoints lp;
        {
        LOCK(pool.cs);
        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        view.SetBackend(viewMemPool);

        // do all inputs exist?
        for (const CTxIn txin : tx.vin) {
            if (!pcoinsTip->HaveCoinInCache(txin.prevout)) {
                coins_to_uncache.push_back(txin.prevout);
            }
            if (!view.HaveCoin(txin.prevout)) {
                // Are inputs missing because we already have the tx?
                for (size_t out = 0; out < tx.vout.size(); out++) {
                    // Optimistically just do efficient check of cache for outputs
                    if (pcoinsTip->HaveCoinInCache(COutPoint(hash, out))) {
                        return state.Invalid(false, REJECT_DUPLICATE, "txn-already-known");
                    }
                }
                // Otherwise assume this might be an orphan tx for which we just haven't seen parents yet
                if (pfMissingInputs) {
                    *pfMissingInputs = true;
                }
                return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
            }
        }

        // Bring the best block into scope
        view.GetBestBlock();

        // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend(dummy);

        // Only accept BIP68 sequence locked transactions that can be mined in the next
        // block; we don't want our mempool filled up with transactions that can't
        // be mined yet.
        // Must keep pool.cs for this unless we change CheckSequenceLocks to take a
        // CoinsViewCache instead of create its own
        if (!CheckSequenceLocks(tx, STANDARD_LOCKTIME_VERIFY_FLAGS, &lp))
            return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");

        } // end LOCK(pool.cs)

        CAmount nFees = 0;
        if (!Consensus::CheckTxInputs(tx, state, view, GetSpendHeight(view), nFees)) {
            return error("%s: Consensus::CheckTxInputs: %s, %s", __func__, tx.GetHash().ToString(), FormatStateMessage(state));
        }

        /** CLORE START */
        if (!AreAssetsDeployed()) {
            for (auto out : tx.vout) {
                if (out.scriptPubKey.IsAssetScript())
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-contained-asset-when-not-active");
            }
        }

        if (AreAssetsDeployed()) {
            if (!Consensus::CheckTxAssets(tx, state, view, GetCurrentAssetCache(), true, vReissueAssets))
                return error("%s: Consensus::CheckTxAssets: %s, %s", __func__, tx.GetHash().ToString(),
                             FormatStateMessage(state));
        }
        /** CLORE END */

        // Check for non-standard pay-to-script-hash in inputs
        if (fRequireStandard && !AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        // Check for non-standard witness in P2WSH
        if (tx.HasWitness() && fRequireStandard && !IsWitnessStandard(tx, view))
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-witness-nonstandard", true);

        int64_t nSigOpsCost = GetTransactionSigOpCost(tx, view, STANDARD_SCRIPT_VERIFY_FLAGS);

        // nModifiedFees includes any fee deltas from PrioritiseTransaction
        CAmount nModifiedFees = nFees;
        pool.ApplyDelta(hash, nModifiedFees);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const CTxIn &txin : tx.vin) {
            const Coin &coin = view.AccessCoin(txin.prevout);
            if (coin.IsCoinBase()) {
                fSpendsCoinbase = true;
                break;
            }
        }

        CTxMemPoolEntry entry(ptx, nFees, nAcceptTime, chainActive.Height(),
                              fSpendsCoinbase, nSigOpsCost, lp);
        unsigned int nSize = entry.GetTxSize();

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        if (nSigOpsCost > MAX_STANDARD_TX_SIGOPS_COST)
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false,
                strprintf("%d", nSigOpsCost));

        CAmount mempoolRejectFee = pool.GetMinFee(gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
        if (!bypass_limits && mempoolRejectFee > 0 && nModifiedFees < mempoolRejectFee) {
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool min fee not met", false, strprintf("%d < %d", nFees, mempoolRejectFee));
        }

        // No transactions are allowed below minRelayTxFee except from disconnected blocks
        if (!bypass_limits && nModifiedFees < ::minRelayTxFee.GetFee(nSize)) {
            LogPrintf("Modifed fees: %u, minrelayfee: %u\n", nModifiedFees, ::minRelayTxFee.GetFee(nSize));
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "min relay fee not met");
        }

        if (nAbsurdFee && nFees > nAbsurdFee)
            return state.Invalid(false,
                REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, nAbsurdFee));

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = gArgs.GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = gArgs.GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = gArgs.GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = gArgs.GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            LogPrintf("%s - %s\n", __func__, errString);
            return state.DoS(0, false, REJECT_NONSTANDARD, "too-long-mempool-chain", false, errString);
        }

        // A transaction that spends outputs that would be replaced by it is invalid. Now
        // that we have the set of all ancestors we can detect this
        // pathological case by making sure setConflicts and setAncestors don't
        // intersect.
        for (CTxMemPool::txiter ancestorIt : setAncestors)
        {
            const uint256 &hashAncestor = ancestorIt->GetTx().GetHash();
            if (setConflicts.count(hashAncestor))
            {
                return state.DoS(10, false,
                                 REJECT_INVALID, "bad-txns-spends-conflicting-tx", false,
                                 strprintf("%s spends conflicting transaction %s",
                                           hash.ToString(),
                                           hashAncestor.ToString()));
            }
        }

        // Check if it's economically rational to mine this transaction rather
        // than the ones it replaces.
        CAmount nConflictingFees = 0;
        size_t nConflictingSize = 0;
        uint64_t nConflictingCount = 0;
        CTxMemPool::setEntries allConflicting;

        // If we don't hold the lock allConflicting might be incomplete; the
        // subsequent RemoveStaged() and addUnchecked() calls don't guarantee
        // mempool consistency for us.
        LOCK(pool.cs);
        const bool fReplacementTransaction = setConflicts.size();
        if (fReplacementTransaction)
        {
            CFeeRate newFeeRate(nModifiedFees, nSize);
            std::set<uint256> setConflictsParents;
            const int maxDescendantsToVisit = 100;
            CTxMemPool::setEntries setIterConflicting;
            for (const uint256 &hashConflicting : setConflicts)
            {
                CTxMemPool::txiter mi = pool.mapTx.find(hashConflicting);
                if (mi == pool.mapTx.end())
                    continue;

                // Save these to avoid repeated lookups
                setIterConflicting.insert(mi);

                // Don't allow the replacement to reduce the feerate of the
                // mempool.
                //
                // We usually don't want to accept replacements with lower
                // feerates than what they replaced as that would lower the
                // feerate of the next block. Requiring that the feerate always
                // be increased is also an easy-to-reason about way to prevent
                // DoS attacks via replacements.
                //
                // The mining code doesn't (currently) take children into
                // account (CPFP) so we only consider the feerates of
                // transactions being directly replaced, not their indirect
                // descendants. While that does mean high feerate children are
                // ignored when deciding whether or not to replace, we do
                // require the replacement to pay more overall fees too,
                // mitigating most cases.
                CFeeRate oldFeeRate(mi->GetModifiedFee(), mi->GetTxSize());
                if (newFeeRate <= oldFeeRate)
                {
                    return state.DoS(0, false,
                            REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                            strprintf("rejecting replacement %s; new feerate %s <= old feerate %s",
                                  hash.ToString(),
                                  newFeeRate.ToString(),
                                  oldFeeRate.ToString()));
                }

                for (const CTxIn &txin : mi->GetTx().vin)
                {
                    setConflictsParents.insert(txin.prevout.hash);
                }

                nConflictingCount += mi->GetCountWithDescendants();
            }
            // This potentially overestimates the number of actual descendants
            // but we just want to be conservative to avoid doing too much
            // work.
            if (nConflictingCount <= maxDescendantsToVisit) {
                // If not too many to replace, then calculate the set of
                // transactions that would have to be evicted
                for (CTxMemPool::txiter it : setIterConflicting) {
                    pool.CalculateDescendants(it, allConflicting);
                }
                for (CTxMemPool::txiter it : allConflicting) {
                    nConflictingFees += it->GetModifiedFee();
                    nConflictingSize += it->GetTxSize();
                }
            } else {
                return state.DoS(0, false,
                        REJECT_NONSTANDARD, "too many potential replacements", false,
                        strprintf("rejecting replacement %s; too many potential replacements (%d > %d)\n",
                            hash.ToString(),
                            nConflictingCount,
                            maxDescendantsToVisit));
            }

            for (unsigned int j = 0; j < tx.vin.size(); j++)
            {
                // We don't want to accept replacements that require low
                // feerate junk to be mined first. Ideally we'd keep track of
                // the ancestor feerates and make the decision based on that,
                // but for now requiring all new inputs to be confirmed works.
                if (!setConflictsParents.count(tx.vin[j].prevout.hash))
                {
                    // Rather than check the UTXO set - potentially expensive -
                    // it's cheaper to just check if the new input refers to a
                    // tx that's in the mempool.
                    if (pool.mapTx.find(tx.vin[j].prevout.hash) != pool.mapTx.end())
                        return state.DoS(0, false,
                                         REJECT_NONSTANDARD, "replacement-adds-unconfirmed", false,
                                         strprintf("replacement %s adds unconfirmed input, idx %d",
                                                  hash.ToString(), j));
                }
            }

            // The replacement must pay greater fees than the transactions it
            // replaces - if we did the bandwidth used by those conflicting
            // transactions would not be paid for.
            if (nModifiedFees < nConflictingFees)
            {
                return state.DoS(0, false,
                                 REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                                 strprintf("rejecting replacement %s, less fees than conflicting txs; %s < %s",
                                          hash.ToString(), FormatMoney(nModifiedFees), FormatMoney(nConflictingFees)));
            }

            // Finally in addition to paying more fees than the conflicts the
            // new transaction must pay for its own bandwidth.
            CAmount nDeltaFees = nModifiedFees - nConflictingFees;
            if (nDeltaFees < ::incrementalRelayFee.GetFee(nSize))
            {
                return state.DoS(0, false,
                        REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                        strprintf("rejecting replacement %s, not enough additional fees to relay; %s < %s",
                              hash.ToString(),
                              FormatMoney(nDeltaFees),
                              FormatMoney(::incrementalRelayFee.GetFee(nSize))));
            }
        }

        unsigned int scriptVerifyFlags = STANDARD_SCRIPT_VERIFY_FLAGS;
        if (!chainparams.RequireStandard()) {
            scriptVerifyFlags = gArgs.GetArg("-promiscuousmempoolflags", scriptVerifyFlags);
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        PrecomputedTransactionData txdata(tx);
        if (!CheckInputs(tx, state, view, true, scriptVerifyFlags, true, false, txdata)) {
            // SCRIPT_VERIFY_CLEANSTACK requires SCRIPT_VERIFY_WITNESS, so we
            // need to turn both off, and compare against just turning off CLEANSTACK
            // to see if the failure is specifically due to witness validation.
            CValidationState stateDummy; // Want reported failures to be from first CheckInputs
            if (!tx.HasWitness() && CheckInputs(tx, stateDummy, view, true, scriptVerifyFlags & ~(SCRIPT_VERIFY_WITNESS | SCRIPT_VERIFY_CLEANSTACK), true, false, txdata) &&
                !CheckInputs(tx, stateDummy, view, true, scriptVerifyFlags & ~SCRIPT_VERIFY_CLEANSTACK, true, false, txdata)) {
                // Only the witness is missing, so the transaction itself may be fine.
                state.SetCorruptionPossible();
            }
            return false; // state filled in by CheckInputs
        }

        // Check again against the current block tip's script verification
        // flags to cache our script execution flags. This is, of course,
        // useless if the next block has different script flags from the
        // previous one, but because the cache tracks script flags for us it
        // will auto-invalidate and we'll just have a few blocks of extra
        // misses on soft-fork activation.
        //
        // This is also useful in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks (using TestBlockValidity), however allowing such
        // transactions into the mempool can be exploited as a DoS attack.
        unsigned int currentBlockScriptVerifyFlags = GetBlockScriptFlags(chainActive.Tip(), GetParams().GetConsensus());
        if (!CheckInputsFromMempoolAndCache(tx, state, view, pool, currentBlockScriptVerifyFlags, true, txdata))
        {
            // If we're using promiscuousmempoolflags, we may hit this normally
            // Check if current block has some flags that scriptVerifyFlags
            // does not before printing an ominous warning
            if (!(~scriptVerifyFlags & currentBlockScriptVerifyFlags)) {
                return error("%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against latest-block but not STANDARD flags %s, %s",
                    __func__, hash.ToString(), FormatStateMessage(state));
            } else {
                if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, false, txdata)) {
                    return error("%s: ConnectInputs failed against MANDATORY but not STANDARD flags due to promiscuous mempool %s, %s",
                        __func__, hash.ToString(), FormatStateMessage(state));
                } else {
                    LogPrintf("Warning: -promiscuousmempool flags set to not include currently enforced soft forks, this may break mining or otherwise cause instability!\n");
                }
            }
        }

        if (test_accept) {
            // Tx was accepted, but not added
            return true;
        }

        // Remove conflicting transactions from the mempool
        for (const CTxMemPool::txiter it : allConflicting)
        {
            LogPrint(BCLog::MEMPOOL, "replacing tx %s with %s for %s CLORE additional fees, %d delta bytes\n",
                    it->GetTx().GetHash().ToString(),
                    hash.ToString(),
                    FormatMoney(nModifiedFees - nConflictingFees),
                    (int)nSize - (int)nConflictingSize);
            if (plTxnReplaced)
                plTxnReplaced->push_back(it->GetSharedTx());
        }
        pool.RemoveStaged(allConflicting, false, MemPoolRemovalReason::REPLACED);

        // This transaction should only count for fee estimation if:
        // - it isn't a BIP 125 replacement transaction (may not be widely supported)
        // - it's not being readded during a reorg which bypasses typical mempool fee limits
        // - the node is not behind
        // - the transaction is not dependent on any other transactions in the mempool
        bool validForFeeEstimation = !fReplacementTransaction && !bypass_limits && IsCurrentForFeeEstimation() && pool.HasNoInputsOf(tx);

        // Store transaction in memory
        pool.addUnchecked(hash, entry, setAncestors, validForFeeEstimation);

        // Add memory address index
        if (fAddressIndex) {
            pool.addAddressIndex(entry, view);
        }

        // Add memory spent index
        if (fSpentIndex) {
            pool.addSpentIndex(entry, view);
        }

        // trim mempool and check if tx was trimmed
        if (!bypass_limits) {
            LimitMempoolSize(pool, gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
            if (!pool.exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }

        for (auto out : vReissueAssets) {
            mapReissuedAssets.insert(out);
            mapReissuedTx.insert(std::make_pair(out.second, out.first));
        }

        if (AreAssetsDeployed()) {
            for (auto out : tx.vout) {
                if (out.scriptPubKey.IsAssetScript()) {
                    CAssetOutputEntry data;
                    if (!GetAssetData(out.scriptPubKey, data))
                        continue;
                    if (data.type == TX_NEW_ASSET && !IsAssetNameAnOwner(data.assetName)) {
                        pool.mapAssetToHash[data.assetName] = hash;
                        pool.mapHashToAsset[hash] = data.assetName;
                    }

                    // Keep track of all restricted assets tx that can become invalid if qualifier or verifiers are changed
                    if (AreRestrictedAssetsDeployed()) {
                        if (IsAssetNameAnRestricted(data.assetName)) {
                            std::string address = EncodeDestination(data.destination);
                            pool.mapAddressesQualifiersChanged[address].insert(hash);
                            pool.mapHashQualifiersChanged[hash].insert(address);

                            pool.mapAssetVerifierChanged[data.assetName].insert(hash);
                            pool.mapHashVerifierChanged[hash].insert(data.assetName);
                        }
                    }
                } else if (out.scriptPubKey.IsNullGlobalRestrictionAssetTxDataScript()) {
                    CNullAssetTxData globalNullData;
                    if (GlobalAssetNullDataFromScript(out.scriptPubKey, globalNullData)) {
                        if (globalNullData.flag == 1) {
                            if (pool.mapGlobalFreezingAssetTransactions.count(globalNullData.asset_name)) {
                                return state.DoS(0, false, REJECT_INVALID, "bad-txns-global-freeze-already-in-mempool");
                            } else {
                                pool.mapGlobalFreezingAssetTransactions[globalNullData.asset_name].insert(tx.GetHash());
                                pool.mapHashGlobalFreezingAssetTransactions[tx.GetHash()].insert(globalNullData.asset_name);
                            }
                        } else if (globalNullData.flag == 0) {
                            if (pool.mapGlobalUnFreezingAssetTransactions.count(globalNullData.asset_name)) {
                                return state.DoS(0, false, REJECT_INVALID, "bad-txns-global-unfreeze-already-in-mempool");
                            } else {
                                pool.mapGlobalUnFreezingAssetTransactions[globalNullData.asset_name].insert(tx.GetHash());
                                pool.mapHashGlobalUnFreezingAssetTransactions[tx.GetHash()].insert(globalNullData.asset_name);
                            }
                        }
                    }
                } else if (out.scriptPubKey.IsNullAssetTxDataScript()) {
                    // We need to track all tags that are being adding to address, that live in the mempool
                    // This will allow us to keep the mempool clean, and only allow one tag per address at a time into the mempool
                    CNullAssetTxData addressNullData;
                    std::string address;
                    if (AssetNullDataFromScript(out.scriptPubKey, addressNullData, address)) {
                        if (IsAssetNameAQualifier(addressNullData.asset_name)) {
                            if (addressNullData.flag == (int) QualifierType::ADD_QUALIFIER) {
                                if (pool.mapAddressAddedTag.count(std::make_pair(address, addressNullData.asset_name))) {
                                    return state.DoS(0, false, REJECT_INVALID,
                                                     "bad-txns-adding-tag-already-in-mempool");
                                }
                                // Adding a qualifier to an address
                                pool.mapAddressAddedTag[std::make_pair(address, addressNullData.asset_name)].insert(tx.GetHash());
                                pool.mapHashToAddressAddedTag[tx.GetHash()].insert(std::make_pair(address, addressNullData.asset_name));
                            } else {
                                    if (pool.mapAddressRemoveTag.count(std::make_pair(address, addressNullData.asset_name))) {
                                        return state.DoS(0, false, REJECT_INVALID,
                                                         "bad-txns-remove-tag-already-in-mempool");
                                    }

                                pool.mapAddressRemoveTag[std::make_pair(address, addressNullData.asset_name)].insert(tx.GetHash());
                                pool.mapHashToAddressRemoveTag[tx.GetHash()].insert(std::make_pair(address, addressNullData.asset_name));
                            }
                        }
                    }
                }
            }
        }

        // Keep track of all restricted assets tx that can become invalid if address or assets are marked as frozen
        if (AreRestrictedAssetsDeployed()) {
            for (auto in : tx.vin) {
                const Coin coin = pcoinsTip->AccessCoin(in.prevout);

                if (!coin.IsAsset())
                    continue;

                CAssetOutputEntry data;
                if (GetAssetData(coin.out.scriptPubKey, data)) {

                    if (IsAssetNameAnRestricted(data.assetName)) {
                        pool.mapAssetMarkedGlobalFrozen[data.assetName].insert(hash);
                        pool.mapHashMarkedGlobalFrozen[hash].insert(data.assetName);

                        auto pair = std::make_pair(EncodeDestination(data.destination), data.assetName);
                        pool.mapAddressesMarkedFrozen[pair].insert(hash);
                        pool.mapHashToAddressMarkedFrozen[hash].insert(pair);
                    }
                }
            }
        }
    }

    GetMainSignals().TransactionAddedToMempool(ptx);

    return true;
}

/** (try to) add transaction to memory pool with a specified acceptance time **/
static bool AcceptToMemoryPoolWithTime(const CChainParams& chainparams, CTxMemPool& pool, CValidationState &state, const CTransactionRef &tx,
                        bool* pfMissingInputs, int64_t nAcceptTime, std::list<CTransactionRef>* plTxnReplaced,
                        bool bypass_limits, const CAmount nAbsurdFee, bool test_accept)
{
    std::vector<COutPoint> coins_to_uncache;
    bool res = AcceptToMemoryPoolWorker(chainparams, pool, state, tx, pfMissingInputs, nAcceptTime, plTxnReplaced, bypass_limits, nAbsurdFee, coins_to_uncache, test_accept);
    if (!res) {
        for (const COutPoint& hashTx : coins_to_uncache)
            pcoinsTip->Uncache(hashTx);
    }
    // After we've (potentially) uncached entries, ensure our coins cache is still within its size limits
    CValidationState stateDummy;
    FlushStateToDisk(chainparams, stateDummy, FLUSH_STATE_PERIODIC);
    return res;
}

bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransactionRef &tx,
                        bool* pfMissingInputs, std::list<CTransactionRef>* plTxnReplaced,
                        bool bypass_limits, const CAmount nAbsurdFee, bool test_accept)
{
    const CChainParams& chainparams = GetParams();
    return AcceptToMemoryPoolWithTime(chainparams, pool, state, tx, pfMissingInputs, GetTime(), plTxnReplaced, bypass_limits, nAbsurdFee, test_accept);
}

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes)
{
    if (!fTimestampIndex)
        return error("Timestamp index not enabled");

    if (!pblocktree->ReadTimestampIndex(high, low, fActiveOnly, hashes))
        return error("Unable to get hashes for timestamps");

    return true;
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}

bool HashOnchainActive(const uint256 &hash)
{
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!chainActive.Contains(pblockindex)) {
        return false;
    }

    return true;
}

bool GetAddressIndex(uint160 addressHash, int type, std::string assetName,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, int start, int end)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, assetName, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressIndex(uint160 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, int start, int end)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressUnspent(uint160 addressHash, int type, std::string assetName,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, assetName, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

/** Return transaction in txOut, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256 &hash, CTransactionRef &txOut, const Consensus::Params& consensusParams, uint256 &hashBlock, bool fAllowSlow)
{
    CBlockIndex *pindexSlow = nullptr;

    LOCK(cs_main);

    CTransactionRef ptx = mempool.get(hash);
    if (ptx)
    {
        txOut = ptx;
        return true;
    }

    if (fTxIndex) {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx)) {
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception& e) {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut->GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }

        // transaction not found in index, nothing more can be done
        return false;
    }

    if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
        const Coin& coin = AccessByTxid(*pcoinsTip, hash);
        if (!coin.IsSpent()) pindexSlow = chainActive[coin.nHeight];
    }

    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow, consensusParams)) {
            for (const auto& tx : block.vtx) {
                if (tx->GetHash() == hash) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}






//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

static bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk: OpenBlockFile failed");

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, block);
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk: ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s", pos.ToString());

    // Read block
    try {
        filein >> block;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
    }

    // Check the header
    if (!CheckProofOfWork(block.GetHash(), block.nBits, consensusParams))
        return error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());

    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos(), consensusParams))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
    /*if(!log_all){
        log_all=true;
        CAmount lr=1;
        int height=1;
        while (lr!=0){
            lr = 54193019856*pow(1-0.00000041686938347033551682078457954749861613663597381673753261566162109375,height);
            error("block_:%i,%i",height,lr);
            height++;
        }
    }*/
    if(is_windows){
        if(nHeight==76084){
            return 52501147075;
        }else if(nHeight==78768){
            return 52442437565;
        }else if(nHeight==86055){
            return 52283373432;
        }else if(nHeight==88693){
            return 52225908922;
        }else if(nHeight==105643){
            return 51858184601;
        }else if(nHeight==106421){
            return 51841368451;
        }else if(nHeight==120307){
            return 51542143830;
        }else if(nHeight==124643){
            return 51449063182;
        }else if(nHeight==124856){
            return 51444495058;
        }else if(nHeight==157709){
            return 50744744093;
        }else if(nHeight==163015){
            return 50632625360;
        }else if(nHeight==170119){
            return 50482901650;
        }else if(nHeight==172782){
            return 50426890495;
        }else if(nHeight==198661){
            return 49885800809;
        }else if(nHeight==198914){
            return 49880539732;
        }else if(nHeight==223917){
            return 49363335585;
        }else if(nHeight==229332){
            return 49252031023;
        }else if(nHeight==239473){
            return 49044258862;
        }else if(nHeight==251887){
            return 48791109550;
        }else if(nHeight==254712){
            return 48733684215;
        }else if(nHeight==260229){
            return 48621731919;
        }else if(nHeight==261080){
            return 48604486131;
        }else if(nHeight==282695){
            return 48168496138;
        }else if(nHeight==283411){
            return 48154121021;
        }else if(nHeight==286020){
            return 48101776470;
        }else if(nHeight==287044){
            return 48081247438;
        }else if(nHeight==287466){
            return 48072789781;
        }else if(nHeight==289274){
            return 48036570970;
        }else if(nHeight==292195){
            return 47978113602;
        }else if(nHeight==293404){
            return 47953938956;
        }else if(nHeight==293830){
            return 47945423745;
        }else if(nHeight==306443){
            return 47693989520;
        }else if(nHeight==315587){
            return 47512533037;
        }else if(nHeight==317554){
            return 47473589572;
        }else if(nHeight==334849){
            return 47132547397;
        }else if(nHeight==347902){
            return 46876776991;
        }else if(nHeight==348005){
            return 46874764260;
        }else if(nHeight==352551){
            return 46786016547;
        }else if(nHeight==357691){
            return 46685875050;
        }else if(nHeight==360268){
            return 46635748622;
        }else if(nHeight==379825){
            return 46257086258;
        }else if(nHeight==381041){
            return 46233643869;
        }else if(nHeight==390650){
            return 46048816249;
        }else if(nHeight==405950){
            return 45756046810;
        }else if(nHeight==406706){
            return 45741628912;
        }else if(nHeight==414648){
            return 45590438980;
        }else if(nHeight==421511){
            return 45460192271;
        }else if(nHeight==423328){
            return 45425771403;
        }else if(nHeight==427163){
            return 45353207495;
        }else if(nHeight==432702){
            return 45248605936;
        }else if(nHeight==440313){
            return 45105268961;
        }else if(nHeight==449826){
            return 44926750139;
        }else if(nHeight==450670){
            return 44910945989;
        }else if(nHeight==452443){
            return 44877764143;
        }else if(nHeight==452992){
            return 44867494533;
        }else if(nHeight==457958){
            return 44774707098;
        }else if(nHeight==468652){
            return 44575545620;
        }else if(nHeight==470361){
            return 44543799977;
        }else if(nHeight==488422){
            return 44209685531;
        }else if(nHeight==489106){
            return 44197081435;
        }else if(nHeight==494131){
            return 44104595657;
        }else if(nHeight==503927){
            return 43924855027;
        }else if(nHeight==504896){
            return 43907115318;
        }else if(nHeight==505350){
            return 43898806299;
        }else if(nHeight==510536){
            return 43804004637;
        }else if(nHeight==514236){
            return 43736492673;
        }else if(nHeight==515174){
            return 43719394017;
        }else if(nHeight==515204){
            return 43718847262;
        }else if(nHeight==516781){
            return 43690115799;
        }else if(nHeight==517906){
            return 43669630893;
        }else if(nHeight==518707){
            return 43655051494;
        }else if(nHeight==519291){
            return 43644424888;
        }else if(nHeight==521034){
            return 43612724215;
        }else if(nHeight==532618){
            return 43402625368;
        }else if(nHeight==533012){
            return 43395497221;
        }else if(nHeight==533583){
            return 43385168913;
        }else if(nHeight==535539){
            return 43349807209;
        }else if(nHeight==541466){
            return 43242831352;
        }else if(nHeight==544328){
            return 43191269941;
        }else if(nHeight==550333){
            return 43083284401;
        }else if(nHeight==553076){
            return 43034047986;
        }else if(nHeight==555760){
            return 42985925078;
        }else if(nHeight==561150){
            return 42889447296;
        }else if(nHeight==563037){
            return 42855722321;
        }else if(nHeight==565035){
            return 42820042428;
        }else if(nHeight==566823){
            return 42788137861;
        }else if(nHeight==567230){
            return 42780878790;
        }else if(nHeight==579476){
            return 42563039614;
        }else if(nHeight==581475){
            return 42527585668;
        }else if(nHeight==581829){
            return 42521310259;
        }else if(nHeight==583209){
            return 42496855640;
        }else if(nHeight==584182){
            return 42479621816;
        }else if(nHeight==587078){
            return 42428369067;
        }else if(nHeight==589432){
            return 42386754075;
        }else if(nHeight==589564){
            return 42384421733;
        }else if(nHeight==590855){
            return 42361617486;
        }else if(nHeight==594928){
            return 42289752327;
        }else if(nHeight==596498){
            return 42262083371;
        }else if(nHeight==598044){
            return 42234855070;
        }else if(nHeight==601431){
            return 42175264199;
        }else if(nHeight==603049){
            return 42146826794;
        }else if(nHeight==605302){
            return 42107260786;
        }else if(nHeight==606122){
            return 42092869596;
        }else if(nHeight==607207){
            return 42073835154;
        }else if(nHeight==616620){
            return 41909061245;
        }else if(nHeight==620036){
            return 41849424120;
        }else if(nHeight==620104){
            return 41848237826;
        }else if(nHeight==621620){
            return 41821799178;
        }else if(nHeight==623482){
            return 41789349235;
        }else if(nHeight==624051){
            return 41779438030;
        }else if(nHeight==624477){
            return 41772019229;
        }else if(nHeight==625471){
            return 41754713816;
        }else if(nHeight==625592){
            return 41752607711;
        }else if(nHeight==636030){
            return 41571324969;
        }else if(nHeight==656145){
            return 41224193157;
        }else if(nHeight==667828){
            return 41023907665;
        }else if(nHeight==669120){
            return 41001818328;
        }else if(nHeight==671165){
            return 40966879252;
        }else if(nHeight==675034){
            return 40900858340;
        }else if(nHeight==676461){
            return 40876534770;
        }else if(nHeight==678292){
            return 40845346106;
        }else if(nHeight==679531){
            return 40824254880;
        }else if(nHeight==682622){
            return 40771684927;
        }else if(nHeight==685250){
            return 40727042660;
        }else if(nHeight==686523){
            return 40705435577;
        }else if(nHeight==689076){
            return 40662137139;
        }else if(nHeight==690480){
            return 40638345174;
        }else if(nHeight==698465){
            return 40503297096;
        }else if(nHeight==701991){
            return 40443805772;
        }else if(nHeight==707394){
            return 40352814848;
        }else if(nHeight==710900){
            return 40293880497;
        }else if(nHeight==711768){
            return 40279303088;
        }else if(nHeight==719144){
            return 40155641327;
        }else if(nHeight==719862){
            return 40143624049;
        }else if(nHeight==721111){
            return 40122727910;
        }else if(nHeight==721133){
            return 40122359941;
        }else if(nHeight==722134){
            return 40105620921;
        }else if(nHeight==725574){
            return 40048149436;
        }else if(nHeight==726849){
            return 40026869157;
        }else if(nHeight==730258){
            return 39970027051;
        }else if(nHeight==730867){
            return 39959881008;
        }else if(nHeight==732427){
            return 39933902891;
        }else if(nHeight==736549){
            return 39865341952;
        }else if(nHeight==738692){
            return 39829744101;
        }else if(nHeight==738960){
            return 39825294530;
        }else if(nHeight==742041){
            return 39774176758;
        }else if(nHeight==745309){
            return 39720028119;
        }else if(nHeight==748129){
            return 39673361805;
        }else if(nHeight==749288){
            return 39654198182;
        }else if(nHeight==750972){
            return 39626370379;
        }else if(nHeight==753242){
            return 39588889931;
        }else if(nHeight==754188){
            return 39573280793;
        }else if(nHeight==756106){
            return 39541652399;
        }else if(nHeight==756324){
            return 39538059114;
        }else if(nHeight==757925){
            return 39511679900;
        }else if(nHeight==767925){
            return 39347310610;
        }else if(nHeight==769409){
            return 39322976542;
        }else if(nHeight==769488){
            return 39321681552;
        }else if(nHeight==771317){
            return 39291711995;
        }else if(nHeight==773780){
            return 39251389953;
        }else if(nHeight==775282){
            return 39226820861;
        }else if(nHeight==776637){
            return 39204669529;
        }else if(nHeight==777067){
            return 39197642570;
        }else if(nHeight==778180){
            return 39179460034;
        }else if(nHeight==782847){
            return 39103309327;
        }else if(nHeight==789277){
            return 38998634404;
        }else if(nHeight==797417){
            return 38866523929;
        }else if(nHeight==798043){
            return 38856382633;
        }else if(nHeight==800138){
            return 38822462554;
        }else if(nHeight==802387){
            return 38786082021;
        }else if(nHeight==802606){
            return 38782541230;
        }else if(nHeight==803842){
            return 38762563647;
        }else if(nHeight==803920){
            return 38761303271;
        }else if(nHeight==804208){
            return 38756649930;
        }else if(nHeight==812383){
            return 38624795637;
        }else if(nHeight==812425){
            return 38624119380;
        }else if(nHeight==813300){
            return 38610033385;
        }else if(nHeight==815183){
            return 38579737744;
        }else if(nHeight==815888){
            return 38568401096;
        }else if(nHeight==820895){
            return 38487982562;
        }else if(nHeight==822996){
            return 38454287899;
        }else if(nHeight==829323){
            return 38352997078;
        }else if(nHeight==829880){
            return 38344092688;
        }else if(nHeight==829912){
            return 38343581188;
        }else if(nHeight==832380){
            return 38304152300;
        }else if(nHeight==838703){
            return 38203320648;
        }else if(nHeight==840827){
            return 38169509224;
        }else if(nHeight==849517){
            return 38031486675;
        }else if(nHeight==850743){
            return 38012054434;
        }else if(nHeight==869617){
            return 37714149298;
        }else if(nHeight==871031){
            return 37691925114;
        }else if(nHeight==873289){
            return 37656462727;
        }else if(nHeight==874640){
            return 37635260930;
        }else if(nHeight==879690){
            return 37556114862;
        }else if(nHeight==880128){
            return 37549258161;
        }else if(nHeight==887284){
            return 37437411205;
        }else if(nHeight==887608){
            return 37432355036;
        }else if(nHeight==890556){
            return 37386381502;
        }else if(nHeight==891182){
            return 37376626414;
        }else if(nHeight==891416){
            return 37372980597;
        }else if(nHeight==892716){
            return 37352732533;
        }else if(nHeight==893197){
            return 37345243530;
        }else if(nHeight==895435){
            return 37310418388;
        }else if(nHeight==903580){
            return 37183949353;
        }else if(nHeight==904813){
            return 37164841712;
        }else if(nHeight==906777){
            return 37134426133;
        }else if(nHeight==906957){
            return 37131639800;
        }else if(nHeight==907865){
            return 37117587485;
        }else if(nHeight==910864){
            return 37071212386;
        }else if(nHeight==911864){
            return 37055761750;
        }else if(nHeight==913377){
            return 37032397179;
        }else if(nHeight==915001){
            return 37007334878;
        }else if(nHeight==919906){
            return 36931741635;
        }else if(nHeight==923816){
            return 36871593420;
        }else if(nHeight==929306){
            return 36787305086;
        }else if(nHeight==932161){
            return 36743548265;
        }else if(nHeight==934534){
            return 36707218371;
        }else if(nHeight==934659){
            return 36705305656;
        }else if(nHeight==936858){
            return 36671673468;
        }else if(nHeight==942611){
            return 36583831001;
        }else if(nHeight==944688){
            return 36552169043;
        }else if(nHeight==950661){
            return 36461268771;
        }else if(nHeight==953606){
            return 36416533445;
        }else if(nHeight==955972){
            return 36380633046;
        }else if(nHeight==962309){
            return 36284653092;
        }else if(nHeight==963494){
            return 36266733251;
        }else if(nHeight==967425){
            return 36207351120;
        }else if(nHeight==968027){
            return 36198265829;
        }else if(nHeight==970753){
            return 36157153984;
        }else if(nHeight==970938){
            return 36154365621;
        }else if(nHeight==973901){
            return 36109735887;
        }else if(nHeight==974960){
            return 36093798229;
        }else if(nHeight==975137){
            return 36091135114;
        }else if(nHeight==975245){
            return 36089510259;
        }else if(nHeight==975336){
            return 36088141225;
        }else if(nHeight==976393){
            return 36072243173;
        }else if(nHeight==976796){
            return 36066183603;
        }else if(nHeight==977152){
            return 36060831579;
        }else if(nHeight==978633){
            return 36038575081;
        }else if(nHeight==980434){
            return 36011528125;
        }else if(nHeight==983192){
            return 35970148527;
        }else if(nHeight==991090){
            return 35851913895;
        }else if(nHeight==991164){
            return 35850807940;
        }else if(nHeight==992007){
            return 35838211428;
        }else if(nHeight==993566){
            return 35814927759;
        }else if(nHeight==993742){
            return 35812300149;
        }else if(nHeight==994458){
            return 35801612541;
        }else if(nHeight==999057){
            return 35733040063;
        }else if(nHeight==1002454){
            return 35682474117;
        }else if(nHeight==1003169){
            return 35671840124;
        }else if(nHeight==1009978){
            return 35570730448;
        }else if(nHeight==1013009){
            return 35525814097;
        }else if(nHeight==1016291){
            return 35477242135;
        }else if(nHeight==1018358){
            return 35446685655;
        }else if(nHeight==1018994){
            return 35437288957;
        }else if(nHeight==1020059){
            return 35421559498;
        }else if(nHeight==1022371){
            return 35387436567;
        }else if(nHeight==1025027){
            return 35348277092;
        }else if(nHeight==1026346){
            return 35328846155;
        }else if(nHeight==1027076){
            return 35318096703;
        }else if(nHeight==1028113){
            return 35302832214;
        }else if(nHeight==1028810){
            return 35292576183;
        }else if(nHeight==1029283){
            return 35285617905;
        }else if(nHeight==1033998){
            return 35216330743;
        }else if(nHeight==1041618){
            return 35104641957;
        }else if(nHeight==1041908){
            return 35100398338;
        }else if(nHeight==1044766){
            return 35058604171;
        }else if(nHeight==1045916){
            return 35041801108;
        }else if(nHeight==1048753){
            return 35000383114;
        }else if(nHeight==1049909){
            return 34983520454;
        }else if(nHeight==1050315){
            return 34977600029;
        }else if(nHeight==1052195){
            return 34950198312;
        }else if(nHeight==1052337){
            return 34948129480;
        }else if(nHeight==1056108){
            return 34893233664;
        }else if(nHeight==1063622){
            return 34784106594;
        }else if(nHeight==1067263){
            return 34731350568;
        }else if(nHeight==1067648){
            return 34725776816;
        }else if(nHeight==1067962){
            return 34721231613;
        }else if(nHeight==1069225){
            return 34702955483;
        }else if(nHeight==1073537){
            return 34640631524;
        }else if(nHeight==1075595){
            return 34610925469;
        }else if(nHeight==1076773){
            return 34593933177;
        }else if(nHeight==1077158){
            return 34588381478;
        }else if(nHeight==1079005){
            return 34561760130;
        }else if(nHeight==1079576){
            return 34553534288;
        }else if(nHeight==1082551){
            return 34510708017;
        }else if(nHeight==1083944){
            return 34490673495;
        }else if(nHeight==1088972){
            return 34418456075;
        }else if(nHeight==1089623){
            return 34409116792;
        }else if(nHeight==1095360){
            return 34326922957;
        }else if(nHeight==1096031){
            return 34317322393;
        }else if(nHeight==1100757){
            return 34249779530;
        }else if(nHeight==1106740){
            return 34164462566;
        }else if(nHeight==1107289){
            return 34156644536;
        }else if(nHeight==1110277){
            return 34114125302;
        }else if(nHeight==1110463){
            return 34111480273;
        }else if(nHeight==1111811){
            return 34092317051;
        }else if(nHeight==1117620){
            return 34009859155;
        }else if(nHeight==1130895){
            return 33822170368;
        }else if(nHeight==1134200){
            return 33775603837;
        }else if(nHeight==1137887){
            return 33723730685;
        }else if(nHeight==1137921){
            return 33723252703;
        }else if(nHeight==1137963){
            return 33722662264;
        }else if(nHeight==1138893){
            return 33709590906;
        }else if(nHeight==1138988){
            return 33708255945;
        }else if(nHeight==1139769){
            return 33697283164;
        }else if(nHeight==1143043){
            return 33651323450;
        }else if(nHeight==1144100){
            return 33636498899;
        }else if(nHeight==1145735){
            return 33613580692;
        }else if(nHeight==1150219){
            return 33550807439;
        }else if(nHeight==1150669){
            return 33544514191;
        }else if(nHeight==1153447){
            return 33505690002;
        }else if(nHeight==1153960){
            return 33498525441;
        }else if(nHeight==1154652){
            return 33488863392;
        }else if(nHeight==1154785){
            return 33487006699;
        }else if(nHeight==1155844){
            return 33472226628;
        }else if(nHeight==1156454){
            return 33463716045;
        }else if(nHeight==1157706){
            return 33446255200;
        }else if(nHeight==1158920){
            return 33429333017;
        }else if(nHeight==1161101){
            return 33398953137;
        }else if(nHeight==1161844){
            return 33388609947;
        }else if(nHeight==1162803){
            return 33375264589;
        }else if(nHeight==1163908){
            return 33359894122;
        }else if(nHeight==1169442){
            return 33283023029;
        }else if(nHeight==1169984){
            return 33275503804;
        }else if(nHeight==1173936){
            return 33220728604;
        }else if(nHeight==1175298){
            return 33201872018;
        }else if(nHeight==1176887){
            return 33179886195;
        }else if(nHeight==1177087){
            return 33177119974;
        }else if(nHeight==1177491){
            return 33171532911;
        }else if(nHeight==1177896){
            return 33165932963;
        }else if(nHeight==1179918){
            return 33137988843;
        }else if(nHeight==1180704){
            return 33127132648;
        }else if(nHeight==1181707){
            return 33113284424;
        }else if(nHeight==1183631){
            return 33086736335;
        }else if(nHeight==1186213){
            return 33051142355;
        }else if(nHeight==1188550){
            return 33018958820;
        }else if(nHeight==1190637){
            return 32990244601;
        }else if(nHeight==1192578){
            return 32963561551;
        }else if(nHeight==1192960){
            return 32958312715;
        }else if(nHeight==1194162){
            return 32941802196;
        }else if(nHeight==1194858){
            return 32932245810;
        }else if(nHeight==1195445){
            return 32924188197;
        }else if(nHeight==1195716){
            return 32920468908;
        }else if(nHeight==1195750){
            return 32920002311;
        }else if(nHeight==1199621){
            return 32866922086;
        }else if(nHeight==1201721){
            return 32838162122;
        }else if(nHeight==1201880){
            return 32835985607;
        }else if(nHeight==1204716){
            return 32797188470;
        }else if(nHeight==1206982){
            return 32766222014;
        }else if(nHeight==1208684){
            return 32742982237;
        }else if(nHeight==1209828){
            return 32727370875;
        }else if(nHeight==1212293){
            return 32693758050;
        }else if(nHeight==1214349){
            return 32665748770;
        }else if(nHeight==1214411){
            return 32664904505;
        }else if(nHeight==1217460){
            return 32623412642;
        }else if(nHeight==1217909){
            return 32617306946;
        }else if(nHeight==1219505){
            return 32595613097;
        }else if(nHeight==1220061){
            return 32588058980;
        }else if(nHeight==1225577){
            return 32513210391;
        }else if(nHeight==1227852){
            return 32482390193;
        }else if(nHeight==1229614){
            return 32458539858;
        }else if(nHeight==1230376){
            return 32448230893;
        }else if(nHeight==1231301){
            return 32435721129;
        }else if(nHeight==1232027){
            return 32425906033;
        }else if(nHeight==1233776){
            return 32402272769;
        }else if(nHeight==1234649){
            return 32390482851;
        }else if(nHeight==1236721){
            return 32362517536;
        }else if(nHeight==1238553){
            return 32337811559;
        }else if(nHeight==1239834){
            return 32320547461;
        }else if(nHeight==1240042){
            return 32317745105;
        }else if(nHeight==1240317){
            return 32314040440;
        }else if(nHeight==1241423){
            return 32299145239;
        }else if(nHeight==1241721){
            return 32295133059;
        }else if(nHeight==1242239){
            return 32288160053;
        }else if(nHeight==1252300){
            return 32153023103;
        }else if(nHeight==1253960){
            return 32130780801;
        }else if(nHeight==1253977){
            return 32130553098;
        }else if(nHeight==1258472){
            return 32070402333;
        }else if(nHeight==1259555){
            return 32055926788;
        }else if(nHeight==1260246){
            return 32046694190;
        }else if(nHeight==1260818){
            return 32039053588;
        }else if(nHeight==1260947){
            return 32037330697;
        }else if(nHeight==1266403){
            return 31964546519;
        }else if(nHeight==1266849){
            return 31958604102;
        }else if(nHeight==1268605){
            return 31935218236;
        }else if(nHeight==1269877){
            return 31918288821;
        }else if(nHeight==1270824){
            return 31905690753;
        }else if(nHeight==1271552){
            return 31896009452;
        }else if(nHeight==1271968){
            return 31890478599;
        }else if(nHeight==1272310){
            return 31885932318;
        }else if(nHeight==1278727){
            return 31800749795;
        }else if(nHeight==1278959){
            return 31797674375;
        }else if(nHeight==1279830){
            return 31786130948;
        }else if(nHeight==1282009){
            return 31757270853;
        }else if(nHeight==1284445){
            return 31725037903;
        }else if(nHeight==1284457){
            return 31724879201;
        }else if(nHeight==1285557){
            return 31710334889;
        }else if(nHeight==1285993){
            return 31704571898;
        }else if(nHeight==1287561){
            return 31683854934;
        }else if(nHeight==1287845){
            return 31680104075;
        }else if(nHeight==1289307){
            return 31660802101;
        }else if(nHeight==1296587){
            return 31564863242;
        }else if(nHeight==1298380){
            return 31541278996;
        }else if(nHeight==1300809){
            return 31509357220;
        }else if(nHeight==1302777){
            return 31483517572;
        }else if(nHeight==1303598){
            return 31472744187;
        }else if(nHeight==1304329){
            return 31463154909;
        }else if(nHeight==1306782){
            return 31430997735;
        }else if(nHeight==1308910){
            return 31403127716;
        }else if(nHeight==1309210){
            return 31399200660;
        }else if(nHeight==1309903){
            return 31390131038;
        }else if(nHeight==1310634){
            return 31380566931;
        }else if(nHeight==1310834){
            return 31377950720;
        }else if(nHeight==1311235){
            return 31372705874;
        }else if(nHeight==1312039){
            return 31362192664;
        }else if(nHeight==1316191){
            return 31307956613;
        }else if(nHeight==1316342){
            return 31305985924;
        }else if(nHeight==1316702){
            return 31301288093;
        }else if(nHeight==1322811){
            return 31221675908;
        }else if(nHeight==1323598){
            return 31211434497;
        }else if(nHeight==1324568){
            return 31198816287;
        }else if(nHeight==1326598){
            return 31172425612;
        }else if(nHeight==1327569){
            return 31159810183;
        }else if(nHeight==1332920){
            return 31090380441;
        }else if(nHeight==1338803){
            return 31014226472;
        }else if(nHeight==1338981){
            return 31011925216;
        }else if(nHeight==1340540){
            return 30991777129;
        }else if(nHeight==1342460){
            return 30966981564;
        }else if(nHeight==1343255){
            return 30956720459;
        }else if(nHeight==1345059){
            return 30933448750;
        }else if(nHeight==1345168){
            return 30932043204;
        }else if(nHeight==1346684){
            return 30912501129;
        }else if(nHeight==1347734){
            return 30898973288;
        }else if(nHeight==1350606){
            return 30862001656;
        }else if(nHeight==1351316){
            return 30852868555;
        }else if(nHeight==1353194){
            return 30828723887;
        }else if(nHeight==1353221){
            return 30828376897;
        }else if(nHeight==1355024){
            return 30805214512;
        }else if(nHeight==1357422){
            return 30774435374;
        }else if(nHeight==1358045){
            return 30766443993;
        }else if(nHeight==1361587){
            return 30721049271;
        }else if(nHeight==1362408){
            return 30710536796;
        }else if(nHeight==1366851){
            return 30653708886;
        }else if(nHeight==1371941){
            return 30588734793;
        }else if(nHeight==1372714){
            return 30578879464;
        }else if(nHeight==1378191){
            return 30509141590;
        }else if(nHeight==1380483){
            return 30480005100;
        }else if(nHeight==1380716){
            return 30477044703;
        }else if(nHeight==1385695){
            return 30413852363;
        }else if(nHeight==1386007){
            return 30409896895;
        }else if(nHeight==1387590){
            return 30389835891;
        }else if(nHeight==1388045){
            return 30384072227;
        }else if(nHeight==1388701){
            return 30375764341;
        }else if(nHeight==1391663){
            return 30338280485;
        }else if(nHeight==1397702){
            return 30262000687;
        }else if(nHeight==1398296){
            return 30254508124;
        }else if(nHeight==1401743){
            return 30211065157;
        }else if(nHeight==1402334){
            return 30203622978;
        }else if(nHeight==1402823){
            return 30197466622;
        }else if(nHeight==1403602){
            return 30187661849;
        }else if(nHeight==1405647){
            return 30161937892;
        }else if(nHeight==1406578){
            return 30150234150;
        }else if(nHeight==1409632){
            return 30111873727;
        }else if(nHeight==1414084){
            return 30056040840;
        }else if(nHeight==1415721){
            return 30035537134;
        }else if(nHeight==1422724){
            return 29947981147;
        }else if(nHeight==1424386){
            return 29927239262;
        }else if(nHeight==1427727){
            return 29885586786;
        }else if(nHeight==1431177){
            return 29842636238;
        }else if(nHeight==1432106){
            return 29831081266;
        }else if(nHeight==1432432){
            return 29827027514;
        }else if(nHeight==1433939){
            return 29808295395;
        }else if(nHeight==1436830){
            return 29772392981;
        }else if(nHeight==1439697){
            return 29736831321;
        }else if(nHeight==1441662){
            return 29712482414;
        }else if(nHeight==1442726){
            return 29699306391;
        }else if(nHeight==1443422){
            return 29690690650;
        }else if(nHeight==1444490){
            return 29677474804;
        }else if(nHeight==1447261){
            return 29643212801;
        }else if(nHeight==1450796){
            return 29599561738;
        }else if(nHeight==1452678){
            return 29576348558;
        }else if(nHeight==1452857){
            return 29574141664;
        }else if(nHeight==1453431){
            return 29567065919;
        }else if(nHeight==1453952){
            return 29560644975;
        }else if(nHeight==1457130){
            return 29521508632;
        }else if(nHeight==1460266){
            return 29482940301;
        }else if(nHeight==1463613){
            return 29441832556;
        }else if(nHeight==1463633){
            return 29441587089;
        }else if(nHeight==1463839){
            return 29439058898;
        }else if(nHeight==1468205){
            return 29385527007;
        }else if(nHeight==1468835){
            return 29377810565;
        }else if(nHeight==1470104){
            return 29362273597;
        }else if(nHeight==1470938){
            return 29352067015;
        }else if(nHeight==1471358){
            return 29346928353;
        }else if(nHeight==1471677){
            return 29343026018;
        }else if(nHeight==1473312){
            return 29323033166;
        }else if(nHeight==1474083){
            return 29313610071;
        }else if(nHeight==1476046){
            return 29289632123;
        }else if(nHeight==1477650){
            return 29270053904;
        }else if(nHeight==1479661){
            return 29245526383;
        }else if(nHeight==1480590){
            return 29234202610;
        }else if(nHeight==1486032){
            return 29167956962;
        }else if(nHeight==1486622){
            return 29160783898;
        }else if(nHeight==1490271){
            return 29116459497;
        }else if(nHeight==1492442){
            return 29090120334;
        }else if(nHeight==1496403){
            return 29042125782;
        }else if(nHeight==1498506){
            return 29016676390;
        }else if(nHeight==1499427){
            return 29005537959;
        }else if(nHeight==1499943){
            return 28999299404;
        }else if(nHeight==1500748){
            return 28989569454;
        }else if(nHeight==1503258){
            return 28959252303;
        }else if(nHeight==1505232){
            return 28935431527;
        }else if(nHeight==1506937){
            return 28914872616;
        }else if(nHeight==1512023){
            return 28853632301;
        }else if(nHeight==1513869){
            return 28831436788;
        }else if(nHeight==1515219){
            return 28815215776;
        }else if(nHeight==1517385){
            return 28789209129;
        }else if(nHeight==1520153){
            return 28756008572;
        }else if(nHeight==1522744){
            return 28724965722;
        }else if(nHeight==1522775){
            return 28724594513;
        }else if(nHeight==1523957){
            return 28710444251;
        }else if(nHeight==1524077){
            return 28709008066;
        }else if(nHeight==1526234){
            return 28683204889;
        }else if(nHeight==1526511){
            return 28679892949;
        }else if(nHeight==1526886){
            return 28675409885;
        }else if(nHeight==1530159){
            return 28636311440;
        }else if(nHeight==1531649){
            return 28618529933;
        }else if(nHeight==1531783){
            return 28616931332;
        }else if(nHeight==1532108){
            return 28613054499;
        }else if(nHeight==1534488){
            return 28584680154;
        }else if(nHeight==1535718){
            return 28570027132;
        }else if(nHeight==1539382){
            return 28526422304;
        }else if(nHeight==1541646){
            return 28499511982;
        }else if(nHeight==1545072){
            return 28458838179;
        }else if(nHeight==1545673){
            return 28451709036;
        }else if(nHeight==1545713){
            return 28451234614;
        }else if(nHeight==1547049){
            return 28435393463;
        }else if(nHeight==1549912){
            return 28401476142;
        }else if(nHeight==1550593){
            return 28393414445;
        }else if(nHeight==1550862){
            return 28390230646;
        }else if(nHeight==1553515){
            return 28358849693;
        }else if(nHeight==1554409){
            return 28348282849;
        }else if(nHeight==1555859){
            return 28331152603;
        }else if(nHeight==1559429){
            return 28289020860;
        }else if(nHeight==1559531){
            return 28287818017;
        }else if(nHeight==1567768){
            return 28190851189;
        }else if(nHeight==1568604){
            return 28181028308;
        }else if(nHeight==1569096){
            return 28175248978;
        }else if(nHeight==1569414){
            return 28171514188;
        }else if(nHeight==1570242){
            return 28161791963;
        }else if(nHeight==1571127){
            return 28151404164;
        }else if(nHeight==1571396){
            return 28148247502;
        }else if(nHeight==1575223){
            return 28103376731;
        }else if(nHeight==1576121){
            return 28092858235;
        }else if(nHeight==1576702){
            return 28086054936;
        }else if(nHeight==1580325){
            return 28043668076;
        }else if(nHeight==1580892){
            return 28037040318;
        }else if(nHeight==1582769){
            return 28015110924;
        }else if(nHeight==1582777){
            return 28015017495;
        }else if(nHeight==1583221){
            return 28009832674;
        }else if(nHeight==1583335){
            return 28008501591;
        }else if(nHeight==1583508){
            return 28006481735;
        }else if(nHeight==1585139){
            return 27987446205;
        }else if(nHeight==1585961){
            return 27977857482;
        }else if(nHeight==1588041){
            return 27953608718;
        }else if(nHeight==1588365){
            return 27949833399;
        }else if(nHeight==1589881){
            return 27932175408;
        }else if(nHeight==1590470){
            return 27925317892;
        }else if(nHeight==1591045){
            return 27918624997;
        }else if(nHeight==1591156){
            return 27917333162;
        }else if(nHeight==1594434){
            return 27879210232;
        }else if(nHeight==1595085){
            return 27871645342;
        }else if(nHeight==1595124){
            return 27871192211;
        }else if(nHeight==1597707){
            return 27841197392;
        }else if(nHeight==1600145){
            return 27812915984;
        }else if(nHeight==1605885){
            return 27746443943;
        }else if(nHeight==1612950){
            return 27664845813;
        }else if(nHeight==1613682){
            return 27656405216;
        }else if(nHeight==1614265){
            return 27649684561;
        }else if(nHeight==1619680){
            return 27587339989;
        }else if(nHeight==1622164){
            return 27558787980;
        }else if(nHeight==1624465){
            return 27532365806;
        }else if(nHeight==1626581){
            return 27508090330;
        }else if(nHeight==1627133){
            return 27501761118;
        }else if(nHeight==1629789){
            return 27471327873;
        }else if(nHeight==1630174){
            return 27466919223;
        }else if(nHeight==1634101){
            return 27421991386;
        }else if(nHeight==1636143){
            return 27398658418;
        }else if(nHeight==1637647){
            return 27381485619;
        }else if(nHeight==1640337){
            return 27350797809;
        }else if(nHeight==1640799){
            return 27345530725;
        }else if(nHeight==1645547){
            return 27291459348;
        }else if(nHeight==1648544){
            return 27257383841;
        }else if(nHeight==1651152){
            return 27227765837;
        }else if(nHeight==1651866){
            return 27219662840;
        }else if(nHeight==1653073){
            return 27205970400;
        }else if(nHeight==1656897){
            return 27162635671;
        }else if(nHeight==1658004){
            return 27150103699;
        }else if(nHeight==1664756){
            return 27073791678;
        }else if(nHeight==1669320){
            return 27022330262;
        }else if(nHeight==1672426){
            return 26987364483;
        }else if(nHeight==1674571){
            return 26963243572;
        }else if(nHeight==1675949){
            return 26947759089;
        }else if(nHeight==1677107){
            return 26934753606;
        }else if(nHeight==1681486){
            return 26885629834;
        }else if(nHeight==1682305){
            return 26876452214;
        }else if(nHeight==1686487){
            return 26829638020;
        }else if(nHeight==1689203){
            return 26799278225;
        }else if(nHeight==1690481){
            return 26785004466;
        }else if(nHeight==1691035){
            return 26778819299;
        }else if(nHeight==1692296){
            return 26764746112;
        }else if(nHeight==1703374){
            return 26641429336;
        }else if(nHeight==1703959){
            return 26634933119;
        }else if(nHeight==1706341){
            return 26608498208;
        }else if(nHeight==1706465){
            return 26607122802;
        }else if(nHeight==1710782){
            return 26559283005;
        }else if(nHeight==1710838){
            return 26558662994;
        }else if(nHeight==1711095){
            return 26555817772;
        }else if(nHeight==1715076){
            return 26511783418;
        }else if(nHeight==1715502){
            return 26507075704;
        }else if(nHeight==1716644){
            return 26494459618;
        }else if(nHeight==1716971){
            return 26490848237;
        }else if(nHeight==1717993){
            return 26479564464;
        }else if(nHeight==1720717){
            return 26449512596;
        }else if(nHeight==1726668){
            return 26383978226;
        }else if(nHeight==1727852){
            return 26370959008;
        }else if(nHeight==1729941){
            return 26348004110;
        }else if(nHeight==1731116){
            return 26335101448;
        }else if(nHeight==1734968){
            return 26292846972;
        }else if(nHeight==1735848){
            return 26283203338;
        }else if(nHeight==1737887){
            return 26260872190;
        }else if(nHeight==1738274){
            return 26256635905;
        }else if(nHeight==1738381){
            return 26255464753;
        }else if(nHeight==1738553){
            return 26253582263;
        }else if(nHeight==1740935){
            return 26227525839;
        }else if(nHeight==1741842){
            return 26217611070;
        }else if(nHeight==1743855){
            return 26195619574;
        }else if(nHeight==1745168){
            return 26181285335;
        }else if(nHeight==1745387){
            return 26178895239;
        }else if(nHeight==1747158){
            return 26159575126;
        }else if(nHeight==1749303){
            return 26136194081;
        }else if(nHeight==1753619){
            return 26089211893;
        }else if(nHeight==1754676){
            return 26077718709;
        }else if(nHeight==1754830){
            return 26076044628;
        }else if(nHeight==1755999){
            return 26063340335;
        }else if(nHeight==1761911){
            return 25999185479;
        }else if(nHeight==1763236){
            return 25984828741;
        }else if(nHeight==1764173){
            return 25974680875;
        }else if(nHeight==1764834){
            return 25967524519;
        }else if(nHeight==1765862){
            return 25956398733;
        }else if(nHeight==1770251){
            return 25908951284;
        }else if(nHeight==1770914){
            return 25901791442;
        }else if(nHeight==1775955){
            return 25847417559;
        }else if(nHeight==1778584){
            return 25819105603;
        }else if(nHeight==1778685){
            return 25818018543;
        }else if(nHeight==1780319){
            return 25800438208;
        }else if(nHeight==1780907){
            return 25794114799;
        }else if(nHeight==1784059){
            return 25760244297;
        }else if(nHeight==1786483){
            return 25734226934;
        }else if(nHeight==1789577){
            return 25701056475;
        }else if(nHeight==1789887){
            return 25697735354;
        }else if(nHeight==1798029){
            return 25610661209;
        }else if(nHeight==1798721){
            return 25603274273;
        }else if(nHeight==1799367){
            return 25596380299;
        }else if(nHeight==1800631){
            return 25582896530;
        }else if(nHeight==1800889){
            return 25580145178;
        }else if(nHeight==1802882){
            return 25558901486;
        }else if(nHeight==1806935){
            return 25515754343;
        }else if(nHeight==1807324){
            return 25511616987;
        }else if(nHeight==1807583){
            return 25508862667;
        }else if(nHeight==1811237){
            return 25470036099;
        }else if(nHeight==1813415){
            return 25446921286;
        }else if(nHeight==1815458){
            return 25425258277;
        }else if(nHeight==1815561){
            return 25424166602;
        }else if(nHeight==1817634){
            return 25402205280;
        }else if(nHeight==1820750){
            return 25369230119;
        }else if(nHeight==1821390){
            return 25362462601;
        }else if(nHeight==1823956){
            return 25335347208;
        }else if(nHeight==1825013){
            return 25324186127;
        }else if(nHeight==1828276){
            return 25289762445;
        }else if(nHeight==1830073){
            return 25270824613;
        }else if(nHeight==1832756){
            return 25242575987;
        }else if(nHeight==1834025){
            return 25229226010;
        }else if(nHeight==1834846){
            return 25220592789;
        }else if(nHeight==1835053){
            return 25218416548;
        }else if(nHeight==1838262){
            return 25184703566;
        }else if(nHeight==1839719){
            return 25169411555;
        }else if(nHeight==1844021){
            return 25124313876;
        }else if(nHeight==1844328){
            return 25121098699;
        }else if(nHeight==1844511){
            return 25119182356;
        }else if(nHeight==1846321){
            return 25100236234;
        }else if(nHeight==1848590){
            return 25076505727;
        }else if(nHeight==1851550){
            return 25045582066;
        }else if(nHeight==1852469){
            return 25035988865;
        }else if(nHeight==1852897){
            return 25031522339;
        }else if(nHeight==1853125){
            return 25029143300;
        }else if(nHeight==1855286){
            return 25006605826;
        }else if(nHeight==1857780){
            return 24980620657;
        }else if(nHeight==1859237){
            return 24965452564;
        }else if(nHeight==1859578){
            return 24961903915;
        }else if(nHeight==1860070){
            return 24956784759;
        }else if(nHeight==1860080){
            return 24956680722;
        }else if(nHeight==1862323){
            return 24933356178;
        }else if(nHeight==1863775){
            return 24918268722;
        }else if(nHeight==1865250){
            return 24902951625;
        }else if(nHeight==1865652){
            return 24898778700;
        }else if(nHeight==1866490){
            return 24890082164;
        }else if(nHeight==1869527){
            return 24858590448;
        }else if(nHeight==1871615){
            return 24836962362;
        }else if(nHeight==1881791){
            return 24731825541;
        }else if(nHeight==1882850){
            return 24720909721;
        }else if(nHeight==1884396){
            return 24704982717;
        }else if(nHeight==1886366){
            return 24684702502;
        }else if(nHeight==1886381){
            return 24684548148;
        }else if(nHeight==1888973){
            return 24657890265;
        }else if(nHeight==1890062){
            return 24646698842;
        }else if(nHeight==1895164){
            return 24594334272;
        }else if(nHeight==1895309){
            return 24592847686;
        }else if(nHeight==1896322){
            return 24582464595;
        }else if(nHeight==1898336){
            return 24561834431;
        }else if(nHeight==1898835){
            return 24556725662;
        }else if(nHeight==1905003){
            return 24493665266;
        }else if(nHeight==1908031){
            return 24462766889;
        }else if(nHeight==1908116){
            return 24461900093;
        }else if(nHeight==1908353){
            return 24459483424;
        }else if(nHeight==1913795){
            return 24404057444;
        }else if(nHeight==1915503){
            return 24386687621;
        }else if(nHeight==1916875){
            return 24372743767;
        }else if(nHeight==1920084){
            return 24340161314;
        }else if(nHeight==1920308){
            return 24337888566;
        }else if(nHeight==1921259){
            return 24328241896;
        }else if(nHeight==1926578){
            return 24274357948;
        }else if(nHeight==1929473){
            return 24245080422;
        }else if(nHeight==1929535){
            return 24244453794;
        }else if(nHeight==1929861){
            return 24241159210;
        }else if(nHeight==1932652){
            return 24212971442;
        }else if(nHeight==1934138){
            return 24197976925;
        }else if(nHeight==1937129){
            return 24167824320;
        }else if(nHeight==1938145){
            return 24157590462;
        }else if(nHeight==1942566){
            return 24113109509;
        }else if(nHeight==1942988){
            return 24108867930;
        }else if(nHeight==1945286){
            return 24085783512;
        }else if(nHeight==1946086){
            return 24077752349;
        }else if(nHeight==1946166){
            return 24076949380;
        }else if(nHeight==1946991){
            return 24068670324;
        }else if(nHeight==1947267){
            return 24065901239;
        }else if(nHeight==1947319){
            return 24065379563;
        }else if(nHeight==1951813){
            return 24020337411;
        }else if(nHeight==1958496){
            return 23953511354;
        }else if(nHeight==1963455){
            return 23904044469;
        }else if(nHeight==1965955){
            return 23879145280;
        }else if(nHeight==1968154){
            return 23857265394;
        }else if(nHeight==1968376){
            return 23855057625;
        }else if(nHeight==1973557){
            return 23803591053;
        }else if(nHeight==1976844){
            return 23770996520;
        }else if(nHeight==1977396){
            return 23765527159;
        }else if(nHeight==1979667){
            return 23743038730;
        }else if(nHeight==1980079){
            return 23738961208;
        }else if(nHeight==1980732){
            return 23732499968;
        }else if(nHeight==1981430){
            return 23725595411;
        }else if(nHeight==1981885){
            return 23721095671;
        }else if(nHeight==1983422){
            return 23705901760;
        }else if(nHeight==1985203){
            return 23688307975;
        }else if(nHeight==1987374){
            return 23666879195;
        }else if(nHeight==1991018){
            return 23630954786;
        }else if(nHeight==1993949){
            return 23602099068;
        }else if(nHeight==1994386){
            return 23597799819;
        }else if(nHeight==1997342){
            return 23568738958;
        }else if(nHeight==1998308){
            return 23559249834;
        }else if(nHeight==1999449){
            return 23548046587;
        }else if(nHeight==2001290){
            return 23529981414;
        }else if(nHeight==2001741){
            return 23525558002;
        }else if(nHeight==2002891){
            return 23514282555;
        }else if(nHeight==2004663){
            return 23496919140;
        }else if(nHeight==2005076){
            return 23492874092;
        }else if(nHeight==2006254){
            return 23481340226;
        }else if(nHeight==2007406){
            return 23470066404;
        }else if(nHeight==2013190){
            return 23413544183;
        }else if(nHeight==2013774){
            return 23407844808;
        }else if(nHeight==2013911){
            return 23406507998;
        }else if(nHeight==2014645){
            return 23399347119;
        }else if(nHeight==2015266){
            return 23393290375;
        }else if(nHeight==2019215){
            return 23354811611;
        }else if(nHeight==2019911){
            return 23348036402;
        }else if(nHeight==2021637){
            return 23331243142;
        }else if(nHeight==2022144){
            return 23326312539;
        }else if(nHeight==2022637){
            return 23321519086;
        }else if(nHeight==2023522){
            return 23312916677;
        }else if(nHeight==2024484){
            return 23303569409;
        }else if(nHeight==2033008){
            return 23220909562;
        }else if(nHeight==2036087){
            return 23191123690;
        }else if(nHeight==2040932){
            return 23144331092;
        }else if(nHeight==2043047){
            return 23123934216;
        }else if(nHeight==2043498){
            return 23119587137;
        }else if(nHeight==2045453){
            return 23100752816;
        }else if(nHeight==2046036){
            return 23095139209;
        }else if(nHeight==2046047){
            return 23095033305;
        }else if(nHeight==2046401){
            return 23091625381;
        }else if(nHeight==2046515){
            return 23090528021;
        }else if(nHeight==2051869){
            return 23039049299;
        }else if(nHeight==2052805){
            return 23030061450;
        }else if(nHeight==2052960){
            return 23028573416;
        }else if(nHeight==2053221){
            return 23026067976;
        }else if(nHeight==2053464){
            return 23023735570;
        }else if(nHeight==2056777){
            return 22991959700;
        }else if(nHeight==2057473){
            return 22985289754;
        }else if(nHeight==2059055){
            return 22970136240;
        }else if(nHeight==2063305){
            return 22929476188;
        }else if(nHeight==2063612){
            return 22926541886;
        }else if(nHeight==2066311){
            return 22900761036;
        }else if(nHeight==2067502){
            return 22889393824;
        }else if(nHeight==2069855){
            return 22866952766;
        }else if(nHeight==2069951){
            return 22866037661;
        }else if(nHeight==2070685){
            return 22859042131;
        }else if(nHeight==2072411){
            return 22842600584;
        }else if(nHeight==2075150){
            return 22816533662;
        }else if(nHeight==2077808){
            return 22791266053;
        }else if(nHeight==2078924){
            return 22780665422;
        }else if(nHeight==2078943){
            return 22780484988;
        }else if(nHeight==2080475){
            return 22765941012;
        }else if(nHeight==2081051){
            return 22760475183;
        }else if(nHeight==2082290){
            return 22748722404;
        }else if(nHeight==2084047){
            return 22732066438;
        }else if(nHeight==2085385){
            return 22719390678;
        }else if(nHeight==2090206){
            return 22673776740;
        }else if(nHeight==2090560){
            return 22670430977;
        }else if(nHeight==2090819){
            return 22667983401;
        }else if(nHeight==2095092){
            return 22627641243;
        }else if(nHeight==2095320){
            return 22625490673;
        }else if(nHeight==2096488){
            return 22614476923;
        }else if(nHeight==2096595){
            return 22613468226;
        }else if(nHeight==2099714){
            return 22584084942;
        }else if(nHeight==2101378){
            return 22568424454;
        }else if(nHeight==2105055){
            return 22533857417;
        }else if(nHeight==2106623){
            return 22519132944;
        }else if(nHeight==2116354){
            return 22427967835;
        }else if(nHeight==2116372){
            return 22427799544;
        }else if(nHeight==2117768){
            return 22414751488;
        }else if(nHeight==2122435){
            return 22371185314;
        }else if(nHeight==2122970){
            return 22366196533;
        }else if(nHeight==2123729){
            return 22359120900;
        }else if(nHeight==2126891){
            return 22329667836;
        }else if(nHeight==2129648){
            return 22304018887;
        }else if(nHeight==2129791){
            return 22302689332;
        }else if(nHeight==2130895){
            return 22292427463;
        }else if(nHeight==2131069){
            return 22290810534;
        }else if(nHeight==2132854){
            return 22274229844;
        }else if(nHeight==2135396){
            return 22250638741;
        }else if(nHeight==2136305){
            return 22242208807;
        }else if(nHeight==2139383){
            return 22213687592;
        }else if(nHeight==2140004){
            return 22207937747;
        }else if(nHeight==2142923){
            return 22180930631;
        }else if(nHeight==2144114){
            return 22169920720;
        }else if(nHeight==2144459){
            return 22166732472;
        }else if(nHeight==2145618){
            return 22156025164;
        }else if(nHeight==2146996){
            return 22143301376;
        }else if(nHeight==2147947){
            return 22134524562;
        }else if(nHeight==2148765){
            return 22126977993;
        }else if(nHeight==2149058){
            return 22124275508;
        }else if(nHeight==2153423){
            return 22084054002;
        }else if(nHeight==2155320){
            return 22066596805;
        }else if(nHeight==2156866){
            return 22052379902;
        }else if(nHeight==2157518){
            return 22046386904;
        }else if(nHeight==2157887){
            return 22042995883;
        }else if(nHeight==2160795){
            return 22016290310;
        }else if(nHeight==2161128){
            return 22013234275;
        }else if(nHeight==2161374){
            return 22010976936;
        }else if(nHeight==2165497){
            return 21973178000;
        }else if(nHeight==2166311){
            return 21965723068;
        }else if(nHeight==2169936){
            return 21932554593;
        }else if(nHeight==2171388){
            return 21919282956;
        }else if(nHeight==2173197){
            return 21902759486;
        }else if(nHeight==2175376){
            return 21882872960;
        }else if(nHeight==2176899){
            return 21868984104;
        }else if(nHeight==2176956){
            return 21868464469;
        }else if(nHeight==2178116){
            return 21857892123;
        }else if(nHeight==2178464){
            return 21854721416;
        }else if(nHeight==2179571){
            return 21844638346;
        }else if(nHeight==2180208){
            return 21838838363;
        }else if(nHeight==2184125){
            return 21803207309;
        }else if(nHeight==2184840){
            return 21796709577;
        }else if(nHeight==2186191){
            return 21784437330;
        }else if(nHeight==2186952){
            return 21777527582;
        }else if(nHeight==2188311){
            return 21765193549;
        }else if(nHeight==2188495){
            return 21763524136;
        }else if(nHeight==2188913){
            return 21759732141;
        }else if(nHeight==2190071){
            return 21749230495;
        }else if(nHeight==2192096){
            return 21730878397;
        }else if(nHeight==2195796){
            return 21697386156;
        }else if(nHeight==2197042){
            return 21686119040;
        }else if(nHeight==2197344){
            return 21683389047;
        }else if(nHeight==2199884){
            return 21660441775;
        }else if(nHeight==2200153){
            return 21658012955;
        }else if(nHeight==2200499){
            return 21654889297;
        }else if(nHeight==2200649){
            return 21653535250;
        }else if(nHeight==2201375){
            return 21646982859;
        }else if(nHeight==2202100){
            return 21640441472;
        }else if(nHeight==2202855){
            return 21633631508;
        }else if(nHeight==2203024){
            return 21632107452;
        }else if(nHeight==2204637){
            return 21617566686;
        }else if(nHeight==2206088){
            return 21604494658;
        }else if(nHeight==2206193){
            return 21603549022;
        }else if(nHeight==2206757){
            return 21598470314;
        }else if(nHeight==2207004){
            return 21596246504;
        }else if(nHeight==2212828){
            return 21543877702;
        }else if(nHeight==2215005){
            return 21524334967;
        }else if(nHeight==2215669){
            return 21518377827;
        }else if(nHeight==2216564){
            return 21510350857;
        }else if(nHeight==2218088){
            return 21496689476;
        }else if(nHeight==2223279){
            return 21450221593;
        }else if(nHeight==2225848){
            return 21427262039;
        }else if(nHeight==2227703){
            return 21410698895;
        }else if(nHeight==2232590){
            return 21367124540;
        }else if(nHeight==2232799){
            return 21365262995;
        }else if(nHeight==2233573){
            return 21358370456;
        }else if(nHeight==2236286){
            return 21334228501;
        }else if(nHeight==2239684){
            return 21304029481;
        }else if(nHeight==2242217){
            return 21281545782;
        }else if(nHeight==2244894){
            return 21257809684;
        }else if(nHeight==2246307){
            return 21245291744;
        }else if(nHeight==2248168){
            return 21228816164;
        }else if(nHeight==2250651){
            return 21206853863;
        }else if(nHeight==2250887){
            return 21204767610;
        }else if(nHeight==2252464){
            return 21190832110;
        }else if(nHeight==2255451){
            return 21164461938;
        }else if(nHeight==2256179){
            return 21158039901;
        }else if(nHeight==2258386){
            return 21138582802;
        }else if(nHeight==2259944){
            return 21124858117;
        }else if(nHeight==2263920){
            return 21089873236;
        }else if(nHeight==2264100){
            return 21088290785;
        }else if(nHeight==2272347){
            return 21015915357;
        }else if(nHeight==2274117){
            return 21000414295;
        }else if(nHeight==2278668){
            return 20960610646;
        }else if(nHeight==2280600){
            return 20943735938;
        }else if(nHeight==2282589){
            return 20926377566;
        }else if(nHeight==2283981){
            return 20914237882;
        }else if(nHeight==2283985){
            return 20914203008;
        }else if(nHeight==2286684){
            return 20890685029;
        }else if(nHeight==2288663){
            return 20873457641;
        }else if(nHeight==2289807){
            return 20863505490;
        }else if(nHeight==2294777){
            return 20820324366;
        }else if(nHeight==2297370){
            return 20797830951;
        }else if(nHeight==2298028){
            return 20792126886;
        }else if(nHeight==2298168){
            return 20790913457;
        }else if(nHeight==2300704){
            return 20768945313;
        }else if(nHeight==2305853){
            return 20724413394;
        }else if(nHeight==2307583){
            return 20709472663;
        }else if(nHeight==2308459){
            return 20701911407;
        }else if(nHeight==2310531){
            return 20684037778;
        }else if(nHeight==2311362){
            return 20676873685;
        }else if(nHeight==2312342){
            return 20668428244;
        }else if(nHeight==2313269){
            return 20660442721;
        }else if(nHeight==2315277){
            return 20643155640;
        }else if(nHeight==2317394){
            return 20624945830;
        }else if(nHeight==2318843){
            return 20612491220;
        }else if(nHeight==2319047){
            return 20610738380;
        }else if(nHeight==2320549){
            return 20597837254;
        }else if(nHeight==2324612){
            return 20562979388;
        }else if(nHeight==2326023){
            return 20550887742;
        }else if(nHeight==2327919){
            return 20534651056;
        }else if(nHeight==2332022){
            return 20499558292;
        }else if(nHeight==2335500){
            return 20469858092;
        }else if(nHeight==2337195){
            return 20455399327;
        }else if(nHeight==2339035){
            return 20439715237;
        }else if(nHeight==2339394){
            return 20436656537;
        }else if(nHeight==2340347){
            return 20428539144;
        }else if(nHeight==2340455){
            return 20427619433;
        }else if(nHeight==2344690){
            return 20391587467;
        }else if(nHeight==2345126){
            return 20387881529;
        }else if(nHeight==2345229){
            return 20387006142;
        }else if(nHeight==2345936){
            return 20380998432;
        }else if(nHeight==2346385){
            return 20377183988;
        }else if(nHeight==2347689){
            return 20366110006;
        }else if(nHeight==2348559){
            return 20358725037;
        }else if(nHeight==2351515){
            return 20333653120;
        }else if(nHeight==2351782){
            return 20331390026;
        }else if(nHeight==2352353){
            return 20326551071;
        }else if(nHeight==2357759){
            return 20280794807;
        }else if(nHeight==2359120){
            return 20269291572;
        }else if(nHeight==2359325){
            return 20267559468;
        }else if(nHeight==2360220){
            return 20259999089;
        }else if(nHeight==2360252){
            return 20259728826;
        }else if(nHeight==2362061){
            return 20244456382;
        }else if(nHeight==2363965){
            return 20228394338;
        }else if(nHeight==2369105){
            return 20185097177;
        }else if(nHeight==2369815){
            return 20179123730;
        }else if(nHeight==2370315){
            return 20174918138;
        }else if(nHeight==2374049){
            return 20143538479;
        }else if(nHeight==2374885){
            return 20136519621;
        }else if(nHeight==2374938){
            return 20136074728;
        }else if(nHeight==2376512){
            return 20122866725;
        }else if(nHeight==2376517){
            return 20122824782;
        }else if(nHeight==2378325){
            return 20107663923;
        }else if(nHeight==2382964){
            return 20068816142;
        }else if(nHeight==2383400){
            return 20065168864;
        }else if(nHeight==2383504){
            return 20064298969;
        }else if(nHeight==2384941){
            return 20052283222;
        }else if(nHeight==2385047){
            return 20051397168;
        }else if(nHeight==2387145){
            return 20033868040;
        }else if(nHeight==2387249){
            return 20032999502;
        }else if(nHeight==2387781){
            return 20028557185;
        }else if(nHeight==2387881){
            return 20027722273;
        }else if(nHeight==2400058){
            return 19926314759;
        }else if(nHeight==2400714){
            return 19920866327;
        }else if(nHeight==2400873){
            return 19919545971;
        }else if(nHeight==2405260){
            return 19883150269;
        }else if(nHeight==2406282){
            return 19874681044;
        }else if(nHeight==2407345){
            return 19865875883;
        }else if(nHeight==2409262){
            return 19850006633;
        }else if(nHeight==2414181){
            return 19809344293;
        }else if(nHeight==2415591){
            return 19797704060;
        }else if(nHeight==2416609){
            return 19789304229;
        }else if(nHeight==2419690){
            return 19763903660;
        }else if(nHeight==2421529){
            return 19748758004;
        }else if(nHeight==2422232){
            return 19742971296;
        }else if(nHeight==2422677){
            return 19739309178;
        }else if(nHeight==2423516){
            return 19732406493;
        }else if(nHeight==2426623){
            return 19706865359;
        }else if(nHeight==2427138){
            return 19702634990;
        }else if(nHeight==2428690){
            return 19689891874;
        }else if(nHeight==2428858){
            return 19688512959;
        }else if(nHeight==2429347){
            return 19684499881;
        }else if(nHeight==2430805){
            return 19672539362;
        }else if(nHeight==2431045){
            return 19670571249;
        }else if(nHeight==2432684){
            return 19657135940;
        }else if(nHeight==2432693){
            return 19657062190;
        }else if(nHeight==2435410){
            return 19634810530;
        }else if(nHeight==2436795){
            return 19623477365;
        }else if(nHeight==2440092){
            return 19596525018;
        }else if(nHeight==2443526){
            return 19568492079;
        }else if(nHeight==2445437){
            return 19552909291;
        }else if(nHeight==2447158){
            return 19538886432;
        }else if(nHeight==2447934){
            return 19532566806;
        }else if(nHeight==2449320){
            return 19521284518;
        }else if(nHeight==2449872){
            return 19516792954;
        }else if(nHeight==2453215){
            return 19489613399;
        }else if(nHeight==2454829){
            return 19476504665;
        }else if(nHeight==2455645){
            return 19469880557;
        }else if(nHeight==2457840){
            return 19452073210;
        }else if(nHeight==2457910){
            return 19451505590;
        }else if(nHeight==2458831){
            return 19444038875;
        }else if(nHeight==2459363){
            return 19439727160;
        }else if(nHeight==2460230){
            return 19432702410;
        }else if(nHeight==2461672){
            return 19421024422;
        }else if(nHeight==2462241){
            return 19416418326;
        }else if(nHeight==2462818){
            return 19411748585;
        }else if(nHeight==2462892){
            return 19411149774;
        }else if(nHeight==2464475){
            return 19398344497;
        }else if(nHeight==2470107){
            return 19352854314;
        }else if(nHeight==2470807){
            return 19347207808;
        }else if(nHeight==2471868){
            return 19338652459;
        }else if(nHeight==2473849){
            return 19322688836;
        }else if(nHeight==2475808){
            return 19306915456;
        }else if(nHeight==2477071){
            return 19296752922;
        }else if(nHeight==2479345){
            return 19278469017;
        }else if(nHeight==2481460){
            return 19261479088;
        }else if(nHeight==2484709){
            return 19235408828;
        }else if(nHeight==2487717){
            return 19211303831;
        }else if(nHeight==2487896){
            return 19209870344;
        }else if(nHeight==2488909){
            return 19201759944;
        }else if(nHeight==2489096){
            return 19200263137;
        }else if(nHeight==2489678){
            return 19195605372;
        }else if(nHeight==2490671){
            return 19187660969;
        }else if(nHeight==2491964){
            return 19177321372;
        }else if(nHeight==2492686){
            return 19171550255;
        }else if(nHeight==2493491){
            return 19165117747;
        }else if(nHeight==2493550){
            return 19164646381;
        }else if(nHeight==2493739){
            return 19163136490;
        }else if(nHeight==2494885){
            return 19153983825;
        }else if(nHeight==2495176){
            return 19151660415;
        }else if(nHeight==2497470){
            return 19133354464;
        }else if(nHeight==2498879){
            return 19122119423;
        }else if(nHeight==2499867){
            return 19114245274;
        }else if(nHeight==2500960){
            return 19105538075;
        }else if(nHeight==2501712){
            return 19099549698;
        }else if(nHeight==2507092){
            return 19056762034;
        }else if(nHeight==2507257){
            return 19055451289;
        }else if(nHeight==2507821){
            return 19050971605;
        }else if(nHeight==2508174){
            return 19048168367;
        }else if(nHeight==2508543){
            return 19045238511;
        }else if(nHeight==2509757){
            return 19035602544;
        }else if(nHeight==2514197){
            return 19000402125;
        }else if(nHeight==2518917){
            return 18963053236;
        }else if(nHeight==2519042){
            return 18962065122;
        }else if(nHeight==2519162){
            return 18961116581;
        }else if(nHeight==2520224){
            return 18952724061;
        }else if(nHeight==2520250){
            return 18952518641;
        }else if(nHeight==2521212){
            return 18944919666;
        }else if(nHeight==2522377){
            return 18935721244;
        }else if(nHeight==2523652){
            return 18925659420;
        }else if(nHeight==2523670){
            return 18925517409;
        }else if(nHeight==2525414){
            return 18911763173;
        }else if(nHeight==2525544){
            return 18910738315;
        }else if(nHeight==2527125){
            return 18898278909;
        }else if(nHeight==2527863){
            return 18892465754;
        }else if(nHeight==2528591){
            return 18886733120;
        }else if(nHeight==2528707){
            return 18885819839;
        }else if(nHeight==2529678){
            return 18878176779;
        }else if(nHeight==2529720){
            return 18877846253;
        }else if(nHeight==2529919){
            return 18876280268;
        }else if(nHeight==2530677){
            return 18870316550;
        }else if(nHeight==2531476){
            return 18864032296;
        }else if(nHeight==2531819){
            return 18861335192;
        }else if(nHeight==2532115){
            return 18859007972;
        }else if(nHeight==2533590){
            return 18847415463;
        }else if(nHeight==2534147){
            return 18843039671;
        }else if(nHeight==2535282){
            return 18834126255;
        }else if(nHeight==2535581){
            return 18831778841;
        }else if(nHeight==2536782){
            return 18822352878;
        }else if(nHeight==2537117){
            return 18819724496;
        }else if(nHeight==2537307){
            return 18818233935;
        }else if(nHeight==2538153){
            return 18811598449;
        }else if(nHeight==2538745){
            return 18806956569;
        }else if(nHeight==2540617){
            return 18792285728;
        }else if(nHeight==2541835){
            return 18782746423;
        }else if(nHeight==2544653){
            return 18760694569;
        }else if(nHeight==2546173){
            return 18748810778;
        }else if(nHeight==2546614){
            return 18745364324;
        }else if(nHeight==2549181){
            return 18725315565;
        }else if(nHeight==2550406){
            return 18715755641;
        }else if(nHeight==2550901){
            return 18711894036;
        }else if(nHeight==2554105){
            return 18686918182;
        }else if(nHeight==2554817){
            return 18681372521;
        }else if(nHeight==2557223){
            return 18662644723;
        }else if(nHeight==2557484){
            return 18660614283;
        }else if(nHeight==2560986){
            return 18633391959;
        }else if(nHeight==2561175){
            return 18631923923;
        }else if(nHeight==2563617){
            return 18612966364;
        }else if(nHeight==2563695){
            return 18612361158;
        }else if(nHeight==2566227){
            return 18592725924;
        }else if(nHeight==2568765){
            return 18573064949;
        }else if(nHeight==2569771){
            return 18565277583;
        }else if(nHeight==2571845){
            return 18549233217;
        }else if(nHeight==2572267){
            return 18545970343;
        }else if(nHeight==2572599){
            return 18543403746;
        }else if(nHeight==2572679){
            return 18542785342;
        }else if(nHeight==2573190){
            return 18538835773;
        }else if(nHeight==2574627){
            return 18527733568;
        }else if(nHeight==2576636){
            return 18512223258;
        }else if(nHeight==2581682){
            return 18473323292;
        }else if(nHeight==2582406){
            return 18467748635;
        }else if(nHeight==2583437){
            return 18459813042;
        }else if(nHeight==2586157){
            return 18438893600;
        }else if(nHeight==2587414){
            return 18429234060;
        }else if(nHeight==2590248){
            return 18407474470;
        }else if(nHeight==2597968){
            return 18348330162;
        }else if(nHeight==2602271){
            return 18315446625;
        }else if(nHeight==2603006){
            return 18309835649;
        }else if(nHeight==2607203){
            return 18277828747;
        }else if(nHeight==2608987){
            return 18264240668;
        }else if(nHeight==2611659){
            return 18243907909;
        }else if(nHeight==2613702){
            return 18228376838;
        }else if(nHeight==2614771){
            return 18220255473;
        }else if(nHeight==2617510){
            return 18199463358;
        }else if(nHeight==2617695){
            return 18198059854;
        }else if(nHeight==2618991){
            return 18188230774;
        }else if(nHeight==2620741){
            return 18174966906;
        }else if(nHeight==2621773){
            return 18167149548;
        }else if(nHeight==2622184){
            return 18164037176;
        }else if(nHeight==2622414){
            return 18162295692;
        }else if(nHeight==2627669){
            return 18122552024;
        }else if(nHeight==2627814){
            return 18121456620;
        }else if(nHeight==2628103){
            return 18119273564;
        }else if(nHeight==2629271){
            return 18110453373;
        }else if(nHeight==2630519){
            return 18101033804;
        }else if(nHeight==2630729){
            return 18099449262;
        }else if(nHeight==2631414){
            return 18094281601;
        }else if(nHeight==2631920){
            return 18090465269;
        }else if(nHeight==2633706){
            return 18077001408;
        }else if(nHeight==2633976){
            return 18074966870;
        }else if(nHeight==2634174){
            return 18073475021;
        }else if(nHeight==2635558){
            return 18063050585;
        }else if(nHeight==2636815){
            return 18053587937;
        }else if(nHeight==2638730){
            return 18039181418;
        }else if(nHeight==2639232){
            return 18035406781;
        }else if(nHeight==2642066){
            return 18014112187;
        }else if(nHeight==2642751){
            return 18008968891;
        }else if(nHeight==2643284){
            return 18004967897;
        }else if(nHeight==2644793){
            return 17993645325;
        }else if(nHeight==2644794){
            return 17993637824;
        }else if(nHeight==2644850){
            return 17993217773;
        }else if(nHeight==2645002){
            return 17992077684;
        }else if(nHeight==2647646){
            return 17972257689;
        }else if(nHeight==2652543){
            return 17935606369;
        }else if(nHeight==2653364){
            return 17929468961;
        }else if(nHeight==2653619){
            return 17927563129;
        }else if(nHeight==2656883){
            return 17903186364;
        }else if(nHeight==2657423){
            return 17899156640;
        }else if(nHeight==2659692){
            return 17882234247;
        }else if(nHeight==2660262){
            return 17877985654;
        }else if(nHeight==2662201){
            return 17863540540;
        }else if(nHeight==2662873){
            return 17858537015;
        }else if(nHeight==2667091){
            return 17827162951;
        }else if(nHeight==2670118){
            return 17804681685;
        }else if(nHeight==2673923){
            return 17776462493;
        }else if(nHeight==2675529){
            return 17764565270;
        }else if(nHeight==2675848){
            return 17762203071;
        }else if(nHeight==2676127){
            return 17760137330;
        }else if(nHeight==2677849){
            return 17747392804;
        }else if(nHeight==2680237){
            return 17729734344;
        }else if(nHeight==2685026){
            return 17694374225;
        }else if(nHeight==2688630){
            return 17667810200;
        }else if(nHeight==2690979){
            return 17650517882;
        }else if(nHeight==2690995){
            return 17650400155;
        }else if(nHeight==2691221){
            return 17648737345;
        }else if(nHeight==2693134){
            return 17634668594;
        }else if(nHeight==2694723){
            return 17622991159;
        }else if(nHeight==2696688){
            return 17608561223;
        }else if(nHeight==2700320){
            return 17581920803;
        }else if(nHeight==2701934){
            return 17570095185;
        }else if(nHeight==2706386){
            return 17537517035;
        }else if(nHeight==2708067){
            return 17525231792;
        }else if(nHeight==2708391){
            return 17522864894;
        }else if(nHeight==2709169){
            return 17517182722;
        }else if(nHeight==2712407){
            return 17493553571;
        }else if(nHeight==2717437){
            return 17456910584;
        }else if(nHeight==2720927){
            return 17431531437;
        }else if(nHeight==2723344){
            return 17413976733;
        }else if(nHeight==2724298){
            return 17407052685;
        }else if(nHeight==2726395){
            return 17391842519;
        }else if(nHeight==2730306){
            return 17363510370;
        }else if(nHeight==2730962){
            return 17358762683;
        }else if(nHeight==2731526){
            return 17354681868;
        }else if(nHeight==2731534){
            return 17354623991;
        }else if(nHeight==2734457){
            return 17333490096;
        }else if(nHeight==2734805){
            return 17330975699;
        }else if(nHeight==2736587){
            return 17318105967;
        }else if(nHeight==2737875){
            return 17308809889;
        }else if(nHeight==2738457){
            return 17304610969;
        }else if(nHeight==2738525){
            return 17304120440;
        }else if(nHeight==2738705){
            return 17302822048;
        }else if(nHeight==2738930){
            return 17301199195;
        }else if(nHeight==2740565){
            return 17289411034;
        }else if(nHeight==2741689){
            return 17281311783;
        }else if(nHeight==2742414){
            return 17276089635;
        }else if(nHeight==2744391){
            return 17261857395;
        }else if(nHeight==2745482){
            return 17254008408;
        }else if(nHeight==2745504){
            return 17253850170;
        }else if(nHeight==2746412){
            return 17247320522;
        }else if(nHeight==2751942){
            return 17207606272;
        }else if(nHeight==2752220){
            return 17205612203;
        }else if(nHeight==2752665){
            return 17202420739;
        }else if(nHeight==2754480){
            return 17189409999;
        }else if(nHeight==2756546){
            return 17174611953;
        }else if(nHeight==2763939){
            return 17121762722;
        }else if(nHeight==2765012){
            return 17114105854;
        }else if(nHeight==2769293){
            return 17083590946;
        }else if(nHeight==2771062){
            return 17070997431;
        }else if(nHeight==2772891){
            return 17057986537;
        }else if(nHeight==2773741){
            return 17051943297;
        }else if(nHeight==2776360){
            return 17033336466;
        }else if(nHeight==2781935){
            return 16993796151;
        }else if(nHeight==2781984){
            return 16993449029;
        }else if(nHeight==2787969){
            return 16951103836;
        }else if(nHeight==2788969){
            return 16944038911;
        }else if(nHeight==2789687){
            return 16938968111;
        }else if(nHeight==2793403){
            return 16912748490;
        }else if(nHeight==2794109){
            return 16907771634;
        }else if(nHeight==2795666){
            return 16896800939;
        }else if(nHeight==2799517){
            return 16869697179;
        }else if(nHeight==2800047){
            return 16865970386;
        }else if(nHeight==2801771){
            return 16853853455;
        }else if(nHeight==2804957){
            return 16831483933;
        }else if(nHeight==2806365){
            return 16821607555;
        }else if(nHeight==2806375){
            return 16821537431;
        }else if(nHeight==2811358){
            return 16786630982;
        }else if(nHeight==2813241){
            return 16773459231;
        }else if(nHeight==2813851){
            return 16769194444;
        }else if(nHeight==2816138){
            return 16753214640;
        }else if(nHeight==2816665){
            return 16749534527;
        }else if(nHeight==2817866){
            return 16741150800;
        }else if(nHeight==2818185){
            return 16738924687;
        }else if(nHeight==2819033){
            return 16733008434;
        }else if(nHeight==2823632){
            return 16700958932;
        }else if(nHeight==2824904){
            return 16692105463;
        }else if(nHeight==2827913){
            return 16671180676;
        }else if(nHeight==2829239){
            return 16661967912;
        }else if(nHeight==2829683){
            return 16658884233;
        }else if(nHeight==2833360){
            return 16633368572;
        }else if(nHeight==2833588){
            return 16631787708;
        }else if(nHeight==2834129){
            return 16628037224;
        }else if(nHeight==2834549){
            return 16625126156;
        }else if(nHeight==2835387){
            return 16619319405;
        }else if(nHeight==2835891){
            return 16615828016;
        }else if(nHeight==2836045){
            return 16614761349;
        }else if(nHeight==2836543){
            return 16611312466;
        }else if(nHeight==2837442){
            return 16605088283;
        }else if(nHeight==2840889){
            return 16581244752;
        }else if(nHeight==2843442){
            return 16563607255;
        }else if(nHeight==2851185){
            return 16510229101;
        }else if(nHeight==2858662){
            return 16458847940;
        }else if(nHeight==2860051){
            return 16449320504;
        }else if(nHeight==2862987){
            return 16429200023;
        }else if(nHeight==2863231){
            return 16427528993;
        }else if(nHeight==2863497){
            return 16425707490;
        }else if(nHeight==2863821){
            return 16423489090;
        }else if(nHeight==2866163){
            return 16407462526;
        }else if(nHeight==2870002){
            return 16381225648;
        }else if(nHeight==2870049){
            return 16380904696;
        }else if(nHeight==2870476){
            return 16377989101;
        }else if(nHeight==2873900){
            return 16354628473;
        }else if(nHeight==2874236){
            return 16352337871;
        }else if(nHeight==2876837){
            return 16334617008;
        }else if(nHeight==2876936){
            return 16333942891;
        }else if(nHeight==2876958){
            return 16333793091;
        }else if(nHeight==2880835){
            return 16307415688;
        }else if(nHeight==2883966){
            return 16286144835;
        }else if(nHeight==2885862){
            return 16273277604;
        }else if(nHeight==2891176){
            return 16237268217;
        }else if(nHeight==2894947){
            return 16211763044;
        }else if(nHeight==2899664){
            return 16179915988;
        }else if(nHeight==2903792){
            return 16152096930;
        }else if(nHeight==2907875){
            return 16124628184;
        }else if(nHeight==2909327){
            return 16114870989;
        }else if(nHeight==2911018){
            return 16103515196;
        }else if(nHeight==2917116){
            return 16062630920;
        }else if(nHeight==2917439){
            return 16060468251;
        }else if(nHeight==2918039){
            return 16056451682;
        }else if(nHeight==2921240){
            return 16035040255;
        }else if(nHeight==2921359){
            return 16034244817;
        }else if(nHeight==2923797){
            return 16017957047;
        }else if(nHeight==2923813){
            return 16017850209;
        }else if(nHeight==2933450){
            return 15953629646;
        }else if(nHeight==2933457){
            return 15953583092;
        }else if(nHeight==2933634){
            return 15952405986;
        }else if(nHeight==2941538){
            return 15899930324;
        }else if(nHeight==2942734){
            return 15892004978;
        }else if(nHeight==2943161){
            return 15889176401;
        }else if(nHeight==2945924){
            return 15870885619;
        }else if(nHeight==2946116){
            return 15869615381;
        }else if(nHeight==2946595){
            return 15866446845;
        }else if(nHeight==2950198){
            return 15842633636;
        }else if(nHeight==2950537){
            return 15840394933;
        }else if(nHeight==2951223){
            return 15835865664;
        }else if(nHeight==2954740){
            return 15812665239;
        }else if(nHeight==2954785){
            return 15812368610;
        }else if(nHeight==2954954){
            return 15811254653;
        }else if(nHeight==2955816){
            return 15805574034;
        }else if(nHeight==2956684){
            return 15799855937;
        }else if(nHeight==2957799){
            return 15792513721;
        }else if(nHeight==2958665){
            return 15786813511;
        }else if(nHeight==2959990){
            return 15778096040;
        }else if(nHeight==2961123){
            return 15770645598;
        }else if(nHeight==2963810){
            return 15752990342;
        }else if(nHeight==2966294){
            return 15736686504;
        }else if(nHeight==2968915){
            return 15719501756;
        }else if(nHeight==2972298){
            return 15697348648;
        }else if(nHeight==2972956){
            return 15693043454;
        }else if(nHeight==2976807){
            return 15667870613;
        }else if(nHeight==2987143){
            return 15600506706;
        }else if(nHeight==2988510){
            return 15591619125;
        }else if(nHeight==2988913){
            return 15588999978;
        }else if(nHeight==2989253){
            return 15586790618;
        }else if(nHeight==2990732){
            return 15577183545;
        }else if(nHeight==2992762){
            return 15564007007;
        }else if(nHeight==2993043){
            return 15562183941;
        }else if(nHeight==2994849){
            return 15550472107;
        }else if(nHeight==2998408){
            return 15527417935;
        }else if(nHeight==2998785){
            return 15524977841;
        }else if(nHeight==2999506){
            return 15520312310;
        }else if(nHeight==2999692){
            return 15519108947;
        }else if(nHeight==3001506){
            return 15507377814;
        }else if(nHeight==3001935){
            return 15504604769;
        }else if(nHeight==3002779){
            return 15499150622;
        }else if(nHeight==3003732){
            return 15492994395;
        }else if(nHeight==3004065){
            return 15490843845;
        }else if(nHeight==3004718){
            return 15486627567;
        }else if(nHeight==3005788){
            return 15479721292;
        }else if(nHeight==3006203){
            return 15477043519;
        }else if(nHeight==3006269){
            return 15476617699;
        }else if(nHeight==3007668){
            return 15467594361;
        }else if(nHeight==3015887){
            return 15414689198;
        }else if(nHeight==3018050){
            return 15400796212;
        }else if(nHeight==3019925){
            return 15388763187;
        }else if(nHeight==3020382){
            return 15385831763;
        }else if(nHeight==3021393){
            return 15379348693;
        }else if(nHeight==3023175){
            return 15367928211;
        }else if(nHeight==3024291){
            return 15360780309;
        }else if(nHeight==3025548){
            return 15352733293;
        }else if(nHeight==3028770){
            return 15332126059;
        }else if(nHeight==3029777){
            return 15325691174;
        }else if(nHeight==3029911){
            return 15324835097;
        }else if(nHeight==3031186){
            return 15316691980;
        }else if(nHeight==3031348){
            return 15315657635;
        }else if(nHeight==3033244){
            return 15303557159;
        }else if(nHeight==3035516){
            return 15289069602;
        }else if(nHeight==3036538){
            return 15282557225;
        }else if(nHeight==3037657){
            return 15275429927;
        }else if(nHeight==3038056){
            return 15272889362;
        }else if(nHeight==3038849){
            return 15267841323;
        }else if(nHeight==3040466){
            return 15257553076;
        }else if(nHeight==3041729){
            return 15249521995;
        }else if(nHeight==3042629){
            return 15243801714;
        }else if(nHeight==3047775){
            return 15211135604;
        }else if(nHeight==3049905){
            return 15197635145;
        }else if(nHeight==3059149){
            return 15139183125;
        }else if(nHeight==3062410){
            return 15118616730;
        }else if(nHeight==3062561){
            return 15117665084;
        }else if(nHeight==3063812){
            return 15109783221;
        }else if(nHeight==3069055){
            return 15076794638;
        }else if(nHeight==3069909){
            return 15071428156;
        }else if(nHeight==3071150){
            return 15063633195;
        }else if(nHeight==3076238){
            return 15031716609;
        }else if(nHeight==3078965){
            return 15014638217;
        }else if(nHeight==3082913){
            return 14989947439;
        }else if(nHeight==3084843){
            return 14977892006;
        }else if(nHeight==3085112){
            return 14976212511;
        }else if(nHeight==3085525){
            return 14973634322;
        }else if(nHeight==3088499){
            return 14955081965;
        }else if(nHeight==3090926){
            return 14939958929;
        }else if(nHeight==3091052){
            return 14939174220;
        }else if(nHeight==3095119){
            return 14913867681;
        }else if(nHeight==3098246){
            return 14894439362;
        }else if(nHeight==3100067){
            return 14883136996;
        }else if(nHeight==3106351){
            return 14844200037;
        }else if(nHeight==3108386){
            return 14831612606;
        }else if(nHeight==3111411){
            return 14812921283;
        }else if(nHeight==3115582){
            return 14787187509;
        }else if(nHeight==3115851){
            return 14785529398;
        }else if(nHeight==3116982){
            return 14778559969;
        }else if(nHeight==3120606){
            return 14756250338;
        }else if(nHeight==3120955){
            return 14754103645;
        }else if(nHeight==3122576){
            return 14744136995;
        }else if(nHeight==3127609){
            return 14713234691;
        }else if(nHeight==3131095){
            return 14691868844;
        }else if(nHeight==3132039){
            return 14686088367;
        }else if(nHeight==3132243){
            return 14684839495;
        }else if(nHeight==3132816){
            return 14681332202;
        }else if(nHeight==3135954){
            return 14662139573;
        }else if(nHeight==3136346){
            return 14659743787;
        }else if(nHeight==3138027){
            return 14649474459;
        }else if(nHeight==3141323){
            return 14629359877;
        }else if(nHeight==3143266){
            return 14617515224;
        }else if(nHeight==3144510){
            return 14609936756;
        }else if(nHeight==3146410){
            return 14598369508;
        }else if(nHeight==3149798){
            return 14577765999;
        }else if(nHeight==3149893){
            return 14577188693;
        }else if(nHeight==3150986){
            return 14570548280;
        }else if(nHeight==3152288){
            return 14562642056;
        }else if(nHeight==3153130){
            return 14557531406;
        }else if(nHeight==3153624){
            return 14554533831;
        }else if(nHeight==3155904){
            return 14540706866;
        }else if(nHeight==3156663){
            return 14536106857;
        }else if(nHeight==3156861){
            return 14534907094;
        }else if(nHeight==3157040){
            return 14533822545;
        }else if(nHeight==3160204){
            return 14514665433;
        }else if(nHeight==3160751){
            return 14511356066;
        }else if(nHeight==3161917){
            return 14504304248;
        }else if(nHeight==3162243){
            return 14502333255;
        }else if(nHeight==3162436){
            return 14501166505;
        }else if(nHeight==3167134){
            return 14472794447;
        }else if(nHeight==3171059){
            return 14449133240;
        }else if(nHeight==3172177){
            return 14442400645;
        }else if(nHeight==3175699){
            return 14421211665;
        }else if(nHeight==3177148){
            return 14412503251;
        }else if(nHeight==3178063){
            return 14407006858;
        }else if(nHeight==3183348){
            return 14375300926;
        }else if(nHeight==3184381){
            return 14369111878;
        }else if(nHeight==3190842){
            return 14330462276;
        }else if(nHeight==3191013){
            return 14329440770;
        }else if(nHeight==3192788){
            return 14318841718;
        }else if(nHeight==3194312){
            return 14309747717;
        }else if(nHeight==3195299){
            return 14303861180;
        }else if(nHeight==3195895){
            return 14300307767;
        }else if(nHeight==3198273){
            return 14286138673;
        }else if(nHeight==3200100){
            return 14275262199;
        }else if(nHeight==3200208){
            return 14274619514;
        }else if(nHeight==3201001){
            return 14269901426;
        }else if(nHeight==3202755){
            return 14259471244;
        }else if(nHeight==3204668){
            return 14248104258;
        }else if(nHeight==3204706){
            return 14247878555;
        }else if(nHeight==3205464){
            return 14243377121;
        }else if(nHeight==3208251){
            return 14226838558;
        }else if(nHeight==3208449){
            return 14225664321;
        }else if(nHeight==3213491){
            return 14195795426;
        }else if(nHeight==3216328){
            return 14179016569;
        }else if(nHeight==3216802){
            return 14176215127;
        }else if(nHeight==3216919){
            return 14175523717;
        }else if(nHeight==3218783){
            return 14164512980;
        }else if(nHeight==3219120){
            return 14162523218;
        }else if(nHeight==3219547){
            return 14160002467;
        }else if(nHeight==3220590){
            return 14153847109;
        }else if(nHeight==3220745){
            return 14152932591;
        }else if(nHeight==3224973){
            return 14128009676;
        }else if(nHeight==3226091){
            return 14121426709;
        }else if(nHeight==3227767){
            return 14111563892;
        }else if(nHeight==3228733){
            return 14105882367;
        }else if(nHeight==3229952){
            return 14098716088;
        }else if(nHeight==3230024){
            return 14098292927;
        }else if(nHeight==3233238){
            return 14079416422;
        }else if(nHeight==3236622){
            return 14059568785;
        }else if(nHeight==3236626){
            return 14059545341;
        }else if(nHeight==3240660){
            return 14035921955;
        }else if(nHeight==3242647){
            return 14024300539;
        }else if(nHeight==3244676){
            return 14012443406;
        }else if(nHeight==3247960){
            return 13993273505;
        }else if(nHeight==3255943){
            return 13946783124;
        }else if(nHeight==3258587){
            return 13931419408;
        }else if(nHeight==3260627){
            return 13919576974;
        }else if(nHeight==3265377){
            return 13892041673;
        }else if(nHeight==3265510){
            return 13891271469;
        }else if(nHeight==3269021){
            return 13870954677;
        }else if(nHeight==3270447){
            return 13862711457;
        }else if(nHeight==3271161){
            return 13858585907;
        }else if(nHeight==3272659){
            return 13849934331;
        }else if(nHeight==3276271){
            return 13829095727;
        }else if(nHeight==3278931){
            return 13813769518;
        }else if(nHeight==3279681){
            return 13809451289;
        }else if(nHeight==3279965){
            return 13807816472;
        }else if(nHeight==3280850){
            return 13802723301;
        }else if(nHeight==3284046){
            return 13784345973;
        }else if(nHeight==3286155){
            return 13772232409;
        }else if(nHeight==3289959){
            return 13750410103;
        }else if(nHeight==3294076){
            return 13726831179;
        }else if(nHeight==3295482){
            return 13718787987;
        }else if(nHeight==3298465){
            return 13701738980;
        }else if(nHeight==3299810){
            return 13694058713;
        }else if(nHeight==3306168){
            return 13657811269;
        }else if(nHeight==3307302){
            return 13651356338;
        }else if(nHeight==3307555){
            return 13649916633;
        }else if(nHeight==3308675){
            return 13643545059;
        }else if(nHeight==3309284){
            return 13640081764;
        }else if(nHeight==3309850){
            return 13636863792;
        }else if(nHeight==3309970){
            return 13636181634;
        }else if(nHeight==3310369){
            return 13633913704;
        }else if(nHeight==3313118){
            return 13618298540;
        }else if(nHeight==3313343){
            return 13617021263;
        }else if(nHeight==3313887){
            return 13613933586;
        }else if(nHeight==3315090){
            return 13607107992;
        }else if(nHeight==3315366){
            return 13605542503;
        }else if(nHeight==3315490){
            return 13604839226;
        }else if(nHeight==3316488){
            return 13599180304;
        }else if(nHeight==3318225){
            return 13589336671;
        }else if(nHeight==3318282){
            return 13589013771;
        }else if(nHeight==3319486){
            return 13582195009;
        }else if(nHeight==3319488){
            return 13582183685;
        }else if(nHeight==3321022){
            return 13573500957;
        }else if(nHeight==3323954){
            return 13556920727;
        }else if(nHeight==3326962){
            return 13539931770;
        }else if(nHeight==3330604){
            return 13519390520;
        }else if(nHeight==3330651){
            return 13519125639;
        }else if(nHeight==3331092){
            return 13516640519;
        }else if(nHeight==3331165){
            return 13516229194;
        }else if(nHeight==3333552){
            return 13502786324;
        }else if(nHeight==3334009){
            return 13500214162;
        }else if(nHeight==3334766){
            return 13495954569;
        }else if(nHeight==3336546){
            return 13485943912;
        }else if(nHeight==3336884){
            return 13484043851;
        }else if(nHeight==3340175){
            return 13465557540;
        }else if(nHeight==3341048){
            return 13460657951;
        }else if(nHeight==3342759){
            return 13451060376;
        }else if(nHeight==3343017){
            return 13449613761;
        }else if(nHeight==3349715){
            return 13412112241;
        }else if(nHeight==3353460){
            return 13391189907;
        }else if(nHeight==3353876){
            return 13388867839;
        }else if(nHeight==3355745){
            return 13378440246;
        }else if(nHeight==3356441){
            return 13374559173;
        }else if(nHeight==3358878){
            return 13360978712;
        }else if(nHeight==3363523){
            return 13335132097;
        }else if(nHeight==3365716){
            return 13322946760;
        }else if(nHeight==3366335){
            return 13319509321;
        }else if(nHeight==3366981){
            return 13315922891;
        }else if(nHeight==3367283){
            return 13314246594;
        }else if(nHeight==3370877){
            return 13294313741;
        }else if(nHeight==3374751){
            return 13272861385;
        }else if(nHeight==3374941){
            return 13271810147;
        }else if(nHeight==3375477){
            return 13268844998;
        }else if(nHeight==3375879){
            return 13266621571;
        }else if(nHeight==3377722){
            return 13256432867;
        }else if(nHeight==3378024){
            return 13254764059;
        }else if(nHeight==3378791){
            return 13250526673;
        }else if(nHeight==3380857){
            return 13239119539;
        }else if(nHeight==3381012){
            return 13238264124;
        }else if(nHeight==3382612){
            return 13229437263;
        }else if(nHeight==3384190){
            return 13220737536;
        }else if(nHeight==3392108){
            return 13177170831;
        }else if(nHeight==3394198){
            return 13165695126;
        }else if(nHeight==3394878){
            return 13161963559;
        }else if(nHeight==3399248){
            return 13138007979;
        }else if(nHeight==3400744){
            return 13129817189;
        }else if(nHeight==3400841){
            return 13129286278;
        }else if(nHeight==3402671){
            return 13119274144;
        }else if(nHeight==3403075){
            return 13117064844;
        }else if(nHeight==3408388){
            return 13088044957;
        }else if(nHeight==3408583){
            return 13086981079;
        }else if(nHeight==3409642){
            return 13081204913;
        }else if(nHeight==3414216){
            return 13056285947;
        }else if(nHeight==3414578){
            return 13054315814;
        }else if(nHeight==3414713){
            return 13053581172;
        }else if(nHeight==3415979){
            return 13046693874;
        }else if(nHeight==3416163){
            return 13045693179;
        }else if(nHeight==3420570){
            return 13021748367;
        }else if(nHeight==3420722){
            return 13020923281;
        }else if(nHeight==3420765){
            return 13020689878;
        }else if(nHeight==3421476){
            return 13016831193;
        }else if(nHeight==3422602){
            return 13010722591;
        }else if(nHeight==3424213){
            return 13001987826;
        }else if(nHeight==3424831){
            return 12998638616;
        }else if(nHeight==3425358){
            return 12995783256;
        }else if(nHeight==3427041){
            return 12986668725;
        }else if(nHeight==3431098){
            return 12964723721;
        }else if(nHeight==3431259){
            return 12963853610;
        }else if(nHeight==3431726){
            return 12961330078;
        }else if(nHeight==3433107){
            return 12953870430;
        }else if(nHeight==3434346){
            return 12947181467;
        }else if(nHeight==3435190){
            return 12942626960;
        }else if(nHeight==3435763){
            return 12939535773;
        }else if(nHeight==3437496){
            return 12930191178;
        }else if(nHeight==3437797){
            return 12928568829;
        }else if(nHeight==3443615){
            return 12897250563;
        }else if(nHeight==3447540){
            return 12876165173;
        }else if(nHeight==3448978){
            return 12868448762;
        }else if(nHeight==3449560){
            return 12865327023;
        }else if(nHeight==3451903){
            return 12852767269;
        }else if(nHeight==3453985){
            return 12841616906;
        }else if(nHeight==3454144){
            return 12840765763;
        }else if(nHeight==3455595){
            return 12833001020;
        }else if(nHeight==3456175){
            return 12829898577;
        }else if(nHeight==3458774){
            return 12816005631;
        }else if(nHeight==3463147){
            return 12792663717;
        }else if(nHeight==3463232){
            return 12792210431;
        }else if(nHeight==3464035){
            return 12787929004;
        }else if(nHeight==3466654){
            return 12773975003;
        }else if(nHeight==3468467){
            return 12764324280;
        }else if(nHeight==3468840){
            return 12762339680;
        }else if(nHeight==3470711){
            return 12752389411;
        }else if(nHeight==3472814){
            return 12741214590;
        }else if(nHeight==3473305){
            return 12738606948;
        }else if(nHeight==3475759){
            return 12725582046;
        }else if(nHeight==3477777){
            return 12714881246;
        }else if(nHeight==3480486){
            return 12700530443;
        }else if(nHeight==3481182){
            return 12696846031;
        }else if(nHeight==3481347){
            return 12695972728;
        }else if(nHeight==3483515){
            return 12684503634;
        }else if(nHeight==3485372){
            return 12674688022;
        }else if(nHeight==3491056){
            return 12644691078;
        }else if(nHeight==3491536){
            return 12642161162;
        }else if(nHeight==3491976){
            return 12639842517;
        }else if(nHeight==3492277){
            return 12638256598;
        }else if(nHeight==3496269){
            return 12617242223;
        }else if(nHeight==3500131){
            return 12596945438;
        }else if(nHeight==3500894){
            return 12592939347;
        }else if(nHeight==3501925){
            return 12587528160;
        }else if(nHeight==3502089){
            return 12586667623;
        }else if(nHeight==3504506){
            return 12573992017;
        }else if(nHeight==3504812){
            return 12572388155;
        }else if(nHeight==3505718){
            return 12567640665;
        }else if(nHeight==3516539){
            return 12511076410;
        }else if(nHeight==3516971){
            return 12508823523;
        }else if(nHeight==3516992){
            return 12508714018;
        }else if(nHeight==3519293){
            return 12496721204;
        }else if(nHeight==3524817){
            return 12467977026;
        }else if(nHeight==3527747){
            return 12452757592;
        }else if(nHeight==3530774){
            return 12437053817;
        }else if(nHeight==3533591){
            return 12422457292;
        }else if(nHeight==3537842){
            return 12400462799;
        }else if(nHeight==3538346){
            return 12397857708;
        }else if(nHeight==3538746){
            return 12395790565;
        }else if(nHeight==3541027){
            return 12384009267;
        }else if(nHeight==3541492){
            return 12381608930;
        }else if(nHeight==3543539){
            return 12371047816;
        }else if(nHeight==3546193){
            return 12357368409;
        }else if(nHeight==3548442){
            return 12345788318;
        }else if(nHeight==3548716){
            return 12344378235;
        }else if(nHeight==3549108){
            return 12342361170;
        }else if(nHeight==3550751){
            return 12333910577;
        }else if(nHeight==3551663){
            return 12329222301;
        }else if(nHeight==3569056){
            return 12240151210;
        }else if(nHeight==3571165){
            return 12229394671;
        }else if(nHeight==3574100){
            return 12214441011;
        }else if(nHeight==3577341){
            return 12197949541;
        }else if(nHeight==3577423){
            return 12197532582;
        }else if(nHeight==3581566){
            return 12176484524;
        }else if(nHeight==3582315){
            return 12172683190;
        }else if(nHeight==3588080){
            return 12143464283;
        }else if(nHeight==3588454){
            return 12141571153;
        }else if(nHeight==3591427){
            return 12126532782;
        }else if(nHeight==3599849){
            return 12084032695;
        }else if(nHeight==3600596){
            return 12080270295;
        }else if(nHeight==3603172){
            return 12067304790;
        }else if(nHeight==3605727){
            return 12054458728;
        }else if(nHeight==3607817){
            return 12043960768;
        }else if(nHeight==3610799){
            return 12028998165;
        }else if(nHeight==3611840){
            return 12023779180;
        }else if(nHeight==3613499){
            return 12015466572;
        }else if(nHeight==3615364){
            return 12006128639;
        }else if(nHeight==3618445){
            return 11990718168;
        }else if(nHeight==3618920){
            return 11988344085;
        }else if(nHeight==3623460){
            return 11965676553;
        }else if(nHeight==3624256){
            return 11961706664;
        }else if(nHeight==3625597){
            return 11955021676;
        }else if(nHeight==3626179){
            return 11952121524;
        }else if(nHeight==3628046){
            return 11942822863;
        }else if(nHeight==3628349){
            return 11941314443;
        }else if(nHeight==3631102){
            return 11927617954;
        }else if(nHeight==3632057){
            return 11922870391;
        }else if(nHeight==3633554){
            return 11915432202;
        }else if(nHeight==3636091){
            return 11902837128;
        }else if(nHeight==3639817){
            return 11884363330;
        }else if(nHeight==3642800){
            return 11869594052;
        }else if(nHeight==3644918){
            return 11859118662;
        }else if(nHeight==3645453){
            return 11856474075;
        }else if(nHeight==3645904){
            return 11854245171;
        }else if(nHeight==3646698){
            return 11850322132;
        }else if(nHeight==3647048){
            return 11848593245;
        }else if(nHeight==3648134){
            return 11843230361;
        }else if(nHeight==3649346){
            return 11837248130;
        }else if(nHeight==3650335){
            return 11832368829;
        }else if(nHeight==3651692){
            return 11825677247;
        }else if(nHeight==3654026){
            return 11814176774;
        }else if(nHeight==3656272){
            return 11803120469;
        }else if(nHeight==3660069){
            return 11784452638;
        }else if(nHeight==3660076){
            return 11784418250;
        }else if(nHeight==3664864){
            return 11760920351;
        }else if(nHeight==3667129){
            return 11749820821;
        }else if(nHeight==3669716){
            return 11737156159;
        }else if(nHeight==3671306){
            return 11729379086;
        }else if(nHeight==3672154){
            return 11725233421;
        }else if(nHeight==3674645){
            return 11713064002;
        }else if(nHeight==3676666){
            return 11703199981;
        }else if(nHeight==3680216){
            return 11685893381;
        }else if(nHeight==3682506){
            return 11674742987;
        }else if(nHeight==3683569){
            return 11669570678;
        }else if(nHeight==3683933){
            return 11667800066;
        }else if(nHeight==3684237){
            return 11666321519;
        }else if(nHeight==3686194){
            return 11656807857;
        }else if(nHeight==3687838){
            return 11648821794;
        }else if(nHeight==3689009){
            return 11643136761;
        }else if(nHeight==3692922){
            return 11624159839;
        }else if(nHeight==3695411){
            return 11612105004;
        }else if(nHeight==3696446){
            return 11607095927;
        }else if(nHeight==3696951){
            return 11604652669;
        }else if(nHeight==3703379){
            return 11573598039;
        }else if(nHeight==3704394){
            return 11568702025;
        }else if(nHeight==3706759){
            return 11557302105;
        }else if(nHeight==3706873){
            return 11556752879;
        }else if(nHeight==3708895){
            return 11547015680;
        }else if(nHeight==3710213){
            return 11540673100;
        }else if(nHeight==3710862){
            return 11537551213;
        }else if(nHeight==3712494){
            return 11529704529;
        }else if(nHeight==3713364){
            return 11525523735;
        }else if(nHeight==3714234){
            return 11521344457;
        }else if(nHeight==3715627){
            return 11514655964;
        }else if(nHeight==3718229){
            return 11502172853;
        }else if(nHeight==3718515){
            return 11500801592;
        }else if(nHeight==3718957){
            return 11498682692;
        }else if(nHeight==3721825){
            return 11484943293;
        }else if(nHeight==3726365){
            return 11463227590;
        }else if(nHeight==3726368){
            return 11463213254;
        }else if(nHeight==3727398){
            return 11458292287;
        }else if(nHeight==3728277){
            return 11454094414;
        }else if(nHeight==3729140){
            return 11449974449;
        }else if(nHeight==3731047){
            return 11440875679;
        }else if(nHeight==3734270){
            return 11425514380;
        }else if(nHeight==3737769){
            return 11408860973;
        }else if(nHeight==3738289){
            return 11406388118;
        }else if(nHeight==3741741){
            return 11389985749;
        }else if(nHeight==3744470){
            return 11377035450;
        }else if(nHeight==3744971){
            return 11374659586;
        }else if(nHeight==3750535){
            return 11348307072;
        }else if(nHeight==3750581){
            return 11348089459;
        }else if(nHeight==3750666){
            return 11347687359;
        }else if(nHeight==3750961){
            return 11346291946;
        }else if(nHeight==3752066){
            return 11341066585;
        }else if(nHeight==3757386){
            return 11315942854;
        }else if(nHeight==3758328){
            return 11311500057;
        }else if(nHeight==3760432){
            return 11301583165;
        }else if(nHeight==3763057){
            return 11289222806;
        }else if(nHeight==3766003){
            return 11275367050;
        }else if(nHeight==3767527){
            return 11268205982;
        }else if(nHeight==3768233){
            return 11264890126;
        }else if(nHeight==3768363){
            return 11264279664;
        }else if(nHeight==3769654){
            return 11258219102;
        }else if(nHeight==3769917){
            return 11256984856;
        }else if(nHeight==3770171){
            return 11255792975;
        }else if(nHeight==3772361){
            return 11245521754;
        }else if(nHeight==3772757){
            return 11243665493;
        }else if(nHeight==3773420){
            return 11240558348;
        }else if(nHeight==3774301){
            return 11236430876;
        }else if(nHeight==3774333){
            return 11236280985;
        }else if(nHeight==3783659){
            return 11192682223;
        }else if(nHeight==3785434){
            return 11184403336;
        }else if(nHeight==3795906){
            return 11135684720;
        }else if(nHeight==3796653){
            return 11132217591;
        }else if(nHeight==3799189){
            return 11120455041;
        }else if(nHeight==3806469){
            return 11086757734;
        }else if(nHeight==3812019){
            return 11061136778;
        }else if(nHeight==3815305){
            return 11045995240;
        }else if(nHeight==3819546){
            return 11026483798;
        }else if(nHeight==3822029){
            return 11015076334;
        }else if(nHeight==3823382){
            return 11008865314;
        }else if(nHeight==3824262){
            return 11004827506;
        }else if(nHeight==3825530){
            return 10999011996;
        }else if(nHeight==3826911){
            return 10992681723;
        }else if(nHeight==3829128){
            return 10982526984;
        }else if(nHeight==3829705){
            return 10979885634;
        }else if(nHeight==3831665){
            return 10970918027;
        }else if(nHeight==3833701){
            return 10961610452;
        }else if(nHeight==3836136){
            return 10950489217;
        }else if(nHeight==3836224){
            return 10950087511;
        }else if(nHeight==3840187){
            return 10932012313;
        }else if(nHeight==3841147){
            return 10927638255;
        }else if(nHeight==3843500){
            return 10916924657;
        }else if(nHeight==3850501){
            return 10885110026;
        }else if(nHeight==3853479){
            return 10871605229;
        }else if(nHeight==3854676){
            return 10866181730;
        }else if(nHeight==3858226){
            return 10850112906;
        }else if(nHeight==3858241){
            return 10850045060;
        }else if(nHeight==3859101){
            return 10846155932;
        }else if(nHeight==3859620){
            return 10843809563;
        }else if(nHeight==3860501){
            return 10839827775;
        }else if(nHeight==3861594){
            return 10834889859;
        }else if(nHeight==3865694){
            return 10816387063;
        }else if(nHeight==3865753){
            return 10816121034;
        }else if(nHeight==3866568){
            return 10812446896;
        }else if(nHeight==3871569){
            return 10789928974;
        }else if(nHeight==3876693){
            return 10766905861;
        }else if(nHeight==3877010){
            return 10765483134;
        }else if(nHeight==3878858){
            return 10757192871;
        }else if(nHeight==3883704){
            return 10735483669;
        }else if(nHeight==3883842){
            return 10734866096;
        }else if(nHeight==3885109){
            return 10729197720;
        }else if(nHeight==3887553){
            return 10718272069;
        }else if(nHeight==3888384){
            return 10714559704;
        }else if(nHeight==3890332){
            return 10705862352;
        }else if(nHeight==3890727){
            return 10704099633;
        }else if(nHeight==3893784){
            return 10690467338;
        }else if(nHeight==3894581){
            return 10686916074;
        }else if(nHeight==3897004){
            return 10676126940;
        }else if(nHeight==3897333){
            return 10674662809;
        }else if(nHeight==3898611){
            return 10668977299;
        }else if(nHeight==3898694){
            return 10668608157;
        }else if(nHeight==3899807){
            return 10663659330;
        }else if(nHeight==3902676){
            return 10650913233;
        }else if(nHeight==3903906){
            return 10645453383;
        }else if(nHeight==3905847){
            return 10636843166;
        }else if(nHeight==3909113){
            return 10622371004;
        }else if(nHeight==3909230){
            return 10621852924;
        }else if(nHeight==3910574){
            return 10615903458;
        }else if(nHeight==3911223){
            return 10613031732;
        }else if(nHeight==3911802){
            return 10610470401;
        }else if(nHeight==3911869){
            return 10610174052;
        }else if(nHeight==3912009){
            return 10609554842;
        }else if(nHeight==3913061){
            return 10604903077;
        }else if(nHeight==3913923){
            return 10601092980;
        }else if(nHeight==3914371){
            return 10599113331;
        }else if(nHeight==3916188){
            return 10591088053;
        }else if(nHeight==3916590){
            return 10589313331;
        }else if(nHeight==3917303){
            return 10586166359;
        }else if(nHeight==3917714){
            return 10584352751;
        }else if(nHeight==3921080){
            return 10569511386;
        }else if(nHeight==3925325){
            return 10550824003;
        }else if(nHeight==3930592){
            return 10527683484;
        }else if(nHeight==3931097){
            return 10525467439;
        }else if(nHeight==3931746){
            return 10522620177;
        }else if(nHeight==3932788){
            return 10518050375;
        }else if(nHeight==3937283){
            return 10498359809;
        }else if(nHeight==3937710){
            return 10496491233;
        }else if(nHeight==3939144){
            return 10490218402;
        }else if(nHeight==3944101){
            return 10468563566;
        }else if(nHeight==3944128){
            return 10468445738;
        }else if(nHeight==3946524){
            return 10457994873;
        }else if(nHeight==3946903){
            return 10456342708;
        }else if(nHeight==3947274){
            return 10454725670;
        }else if(nHeight==3948635){
            return 10448795766;
        }else if(nHeight==3949007){
            return 10447175540;
        }else if(nHeight==3949942){
            return 10443104307;
        }else if(nHeight==3953435){
            return 10427908907;
        }else if(nHeight==3955512){
            return 10418883936;
        }else if(nHeight==3955755){
            return 10417828564;
        }else if(nHeight==3956561){
            return 10414328795;
        }else if(nHeight==3960031){
            return 10399274973;
        }else if(nHeight==3965446){
            return 10375826664;
        }else if(nHeight==3968271){
            return 10363614699;
        }else if(nHeight==3969632){
            return 10357736473;
        }else if(nHeight==3970026){
            return 10356035390;
        }else if(nHeight==3970519){
            return 10353907271;
        }else if(nHeight==3970807){
            return 10352664272;
        }else if(nHeight==3972665){
            return 10344648788;
        }else if(nHeight==3973866){
            return 10339470930;
        }else if(nHeight==3974521){
            return 10336648128;
        }else if(nHeight==3978693){
            return 10318686466;
        }else if(nHeight==3980736){
            return 10309902150;
        }else if(nHeight==3980984){
            return 10308836330;
        }else if(nHeight==3981184){
            return 10307976878;
        }else if(nHeight==3982810){
            return 10300992192;
        }else if(nHeight==3983300){
            return 10298888264;
        }else if(nHeight==3983692){
            return 10297205431;
        }else if(nHeight==3986745){
            return 10284108488;
        }else if(nHeight==3988532){
            return 10276450238;
        }else if(nHeight==3988759){
            return 10275477830;
        }else if(nHeight==3995507){
            return 10246613167;
        }else if(nHeight==3997993){
            return 10235999718;
        }else if(nHeight==3999566){
            return 10229289808;
        }else if(nHeight==4000619){
            return 10224800508;
        }else if(nHeight==4001742){
            return 10220014945;
        }else if(nHeight==4007267){
            return 10196503254;
        }else if(nHeight==4010514){
            return 10182710857;
        }else if(nHeight==4011865){
            return 10176977664;
        }else if(nHeight==4012836){
            return 10172859058;
        }else if(nHeight==4015503){
            return 10161555251;
        }else if(nHeight==4016655){
            return 10156676502;
        }else if(nHeight==4020530){
            return 10140282964;
        }else if(nHeight==4025127){
            return 10120869251;
        }else if(nHeight==4026686){
            return 10114293840;
        }else if(nHeight==4028057){
            return 10108514889;
        }else if(nHeight==4029186){
            return 10103758480;
        }else if(nHeight==4034708){
            return 10080526850;
        }else if(nHeight==4036427){
            return 10073305746;
        }else if(nHeight==4037931){
            return 10066992048;
        }else if(nHeight==4038106){
            return 10066257666;
        }else if(nHeight==4038677){
            return 10063861855;
        }else if(nHeight==4039173){
            return 10061781193;
        }else if(nHeight==4040886){
            return 10054598666;
        }else if(nHeight==4042139){
            return 10049348144;
        }else if(nHeight==4042915){
            return 10046097799;
        }else if(nHeight==4044728){
            return 10038507984;
        }else if(nHeight==4049302){
            return 10019385186;
        }else if(nHeight==4051106){
            return 10011853115;
        }else if(nHeight==4051728){
            return 10009257450;
        }else if(nHeight==4052032){
            return 10007989074;
        }else if(nHeight==4053730){
            return 10000907482;
        }else if(nHeight==4053826){
            return 10000507259;
        }else if(nHeight==4054824){
            return 9996347556;
        }else if(nHeight==4055169){
            return 9994909985;
        }else if(nHeight==4059004){
            return 9978943944;
        }else if(nHeight==4060652){
            return 9972090755;
        }else if(nHeight==4062095){
            return 9966093921;
        }else if(nHeight==4066296){
            return 9948655887;
        }else if(nHeight==4067774){
            return 9942528079;
        }else if(nHeight==4069777){
            return 9934229637;
        }else if(nHeight==4071337){
            return 9927771345;
        }else if(nHeight==4074342){
            return 9915342684;
        }else if(nHeight==4076670){
            return 9905724788;
        }else if(nHeight==4079545){
            return 9893859891;
        }else if(nHeight==4080038){
            return 9891826747;
        }else if(nHeight==4083555){
            return 9877334670;
        }else if(nHeight==4091032){
            return 9846595610;
        }else if(nHeight==4092394){
            return 9841006534;
        }else if(nHeight==4096075){
            return 9825917124;
        }else if(nHeight==4097339){
            return 9820740986;
        }else if(nHeight==4097871){
            return 9818563237;
        }else if(nHeight==4098888){
            return 9814401478;
        }else if(nHeight==4100737){
            return 9806839534;
        }else if(nHeight==4101311){
            return 9804493204;
        }else if(nHeight==4101356){
            return 9804309282;
        }else if(nHeight==4102098){
            return 9801277110;
        }else if(nHeight==4103501){
            return 9795546334;
        }else if(nHeight==4103755){
            return 9794509189;
        }else if(nHeight==4104525){
            return 9791365759;
        }else if(nHeight==4108874){
            return 9773630434;
        }else if(nHeight==4112513){
            return 9758815194;
        }else if(nHeight==4113171){
            return 9756138717;
        }else if(nHeight==4114386){
            return 9751198519;
        }else if(nHeight==4114897){
            return 9749121537;
        }else if(nHeight==4114935){
            return 9748967102;
        }else if(nHeight==4115963){
            return 9744790157;
        }else if(nHeight==4119491){
            return 9730468877;
        }else if(nHeight==4120209){
            return 9727556864;
        }else if(nHeight==4125194){
            return 9707363073;
        }else if(nHeight==4125978){
            return 9704190976;
        }else if(nHeight==4128425){
            return 9694296976;
        }else if(nHeight==4128854){
            return 9692563432;
        }else if(nHeight==4129961){
            return 9688091593;
        }else if(nHeight==4130308){
            return 9686690276;
        }else if(nHeight==4136424){
            return 9662024802;
        }else if(nHeight==4144707){
            return 9628720041;
        }else if(nHeight==4147766){
            return 9616449287;
        }else if(nHeight==4149352){
            return 9610093425;
        }else if(nHeight==4149366){
            return 9610037339;
        }else if(nHeight==4153104){
            return 9595074082;
        }else if(nHeight==4155987){
            return 9583549316;
        }else if(nHeight==4156386){
            return 9581955408;
        }else if(nHeight==4157124){
            return 9579007976;
        }else if(nHeight==4157701){
            return 9576704179;
        }else if(nHeight==4157966){
            return 9575646295;
        }else if(nHeight==4158398){
            return 9573921995;
        }else if(nHeight==4159781){
            return 9568403928;
        }else if(nHeight==4161997){
            return 9559568883;
        }else if(nHeight==4166443){
            return 9541867571;
        }else if(nHeight==4169475){
            return 9529814763;
        }else if(nHeight==4171933){
            return 9520054895;
        }else if(nHeight==4173045){
            return 9515642812;
        }else if(nHeight==4175544){
            return 9505734988;
        }else if(nHeight==4175876){
            return 9504419479;
        }else if(nHeight==4176009){
            return 9503892534;
        }else if(nHeight==4176511){
            return 9501903877;
        }else if(nHeight==4177773){
            return 9496906342;
        }else if(nHeight==4177930){
            return 9496284804;
        }else if(nHeight==4180127){
            return 9487591497;
        }else if(nHeight==4184156){
            return 9471669825;
        }else if(nHeight==4184654){
            return 9469703701;
        }else if(nHeight==4185188){
            return 9467595901;
        }else if(nHeight==4186665){
            return 9461768343;
        }else if(nHeight==4187779){
            return 9457375388;
        }else if(nHeight==4190488){
            return 9446701208;
        }else if(nHeight==4191210){
            return 9443858370;
        }else if(nHeight==4197150){
            return 9420502373;
        }else if(nHeight==4200239){
            return 9408379307;
        }else if(nHeight==4200671){
            return 9406685127;
        }else if(nHeight==4202212){
            return 9400644252;
        }else if(nHeight==4203508){
            return 9395566805;
        }else if(nHeight==4205416){
            return 9388096665;
        }else if(nHeight==4205769){
            return 9386715262;
        }else if(nHeight==4208808){
            return 9374831078;
        }else if(nHeight==4212406){
            return 9360780343;
        }else if(nHeight==4214320){
            return 9353314466;
        }else if(nHeight==4216607){
            return 9344401448;
        }else if(nHeight==4218317){
            return 9337742695;
        }else if(nHeight==4219657){
            return 9332528041;
        }else if(nHeight==4220098){
            return 9330812512;
        }else if(nHeight==4220887){
            return 9327744019;
        }else if(nHeight==4221900){
            return 9323805849;
        }else if(nHeight==4223005){
            return 9319511913;
        }else if(nHeight==4225687){
            return 9309098112;
        }else if(nHeight==4225702){
            return 9309039902;
        }else if(nHeight==4227034){
            return 9303872305;
        }else if(nHeight==4230054){
            return 9292166604;
        }else if(nHeight==4231284){
            return 9287403272;
        }else if(nHeight==4233382){
            return 9279284133;
        }else if(nHeight==4239448){
            return 9255848970;
        }else if(nHeight==4239883){
            return 9254170683;
        }else if(nHeight==4241210){
            return 9249052823;
        }else if(nHeight==4241375){
            return 9248416663;
        }else if(nHeight==4241510){
            return 9247896201;
        }else if(nHeight==4243899){
            return 9238690795;
        }else if(nHeight==4245929){
            return 9230875906;
        }else if(nHeight==4246168){
            return 9229956263;
        }else if(nHeight==4250931){
            return 9211647912;
        }else if(nHeight==4252883){
            return 9204155174;
        }else if(nHeight==4257436){
            return 9186702194;
        }else if(nHeight==4260335){
            return 9175606728;
        }else if(nHeight==4262289){
            return 9168135662;
        }else if(nHeight==4263735){
            return 9162610837;
        }else if(nHeight==4264575){
            return 9159402924;
        }else if(nHeight==4265413){
            return 9156203768;
        }else if(nHeight==4266597){
            return 9151685624;
        }else if(nHeight==4267301){
            return 9149000217;
        }else if(nHeight==4267987){
            return 9146384229;
        }else if(nHeight==4271271){
            return 9133871402;
        }else if(nHeight==4271511){
            return 9132957616;
        }else if(nHeight==4272066){
            return 9130844836;
        }else if(nHeight==4274259){
            return 9122501280;
        }else if(nHeight==4275300){
            return 9118543328;
        }else if(nHeight==4276396){
            return 9114378118;
        }else if(nHeight==4279096){
            return 9104125223;
        }else if(nHeight==4280065){
            return 9100448386;
        }else if(nHeight==4280656){
            return 9098206586;
        }else if(nHeight==4281324){
            return 9095673372;
        }else if(nHeight==4281610){
            return 9094589008;
        }else if(nHeight==4282252){
            return 9092155347;
        }else if(nHeight==4284007){
            return 9085505905;
        }else if(nHeight==4286227){
            return 9077101611;
        }else if(nHeight==4287071){
            return 9073908505;
        }else if(nHeight==4288683){
            return 9067812945;
        }else if(nHeight==4291119){
            return 9058609309;
        }else if(nHeight==4291715){
            return 9056358939;
        }else if(nHeight==4292656){
            return 9052807060;
        }else if(nHeight==4296526){
            return 9038214078;
        }else if(nHeight==4296785){
            return 9037238282;
        }else if(nHeight==4304659){
            return 9007622810;
        }else if(nHeight==4311895){
            return 8980492548;
        }else if(nHeight==4313441){
            return 8974706663;
        }else if(nHeight==4316206){
            return 8964367980;
        }else if(nHeight==4319191){
            return 8953220058;
        }else if(nHeight==4320954){
            return 8946642388;
        }else if(nHeight==4321080){
            return 8946172473;
        }else if(nHeight==4325814){
            return 8928534968;
        }else if(nHeight==4327410){
            return 8922596578;
        }else if(nHeight==4327794){
            return 8921168382;
        }else if(nHeight==4330571){
            return 8910846798;
        }else if(nHeight==4331324){
            return 8908050098;
        }else if(nHeight==4332020){
            return 8905465881;
        }else if(nHeight==4336861){
            return 8887512193;
        }else if(nHeight==4337918){
            return 8883596942;
        }else if(nHeight==4340446){
            return 8874239930;
        }else if(nHeight==4340635){
            return 8873540771;
        }else if(nHeight==4341086){
            return 8871872630;
        }else if(nHeight==4345342){
            return 8856146140;
        }else if(nHeight==4345442){
            return 8855776962;
        }else if(nHeight==4346815){
            return 8850709704;
        }else if(nHeight==4348204){
            return 8845586346;
        }else if(nHeight==4353461){
            return 8826222621;
        }else if(nHeight==4355818){
            return 8817554575;
        }else if(nHeight==4356097){
            return 8816529095;
        }else if(nHeight==4357445){
            return 8811576126;
        }else if(nHeight==4358681){
            return 8807037125;
        }else if(nHeight==4362593){
            return 8792686372;
        }else if(nHeight==4367748){
            return 8773811510;
        }else if(nHeight==4368989){
            return 8769273684;
        }else if(nHeight==4370520){
            return 8763678681;
        }else if(nHeight==4371560){
            return 8759880062;
        }else if(nHeight==4373276){
            return 8753615940;
        }else if(nHeight==4373474){
            return 8752893445;
        }else if(nHeight==4380048){
            return 8728938980;
        }else if(nHeight==4380290){
            return 8728058428;
        }else if(nHeight==4385058){
            return 8710727475;
        }else if(nHeight==4386231){
            return 8706469076;
        }else if(nHeight==4389081){
            return 8696131254;
        }else if(nHeight==4391209){
            return 8688420352;
        }else if(nHeight==4392347){
            return 8684299565;
        }else if(nHeight==4394733){
            return 8675666016;
        }else if(nHeight==4397434){
            return 8665903022;
        }else if(nHeight==4401999){
            return 8649427411;
        }else if(nHeight==4403659){
            return 8643444049;
        }else if(nHeight==4406549){
            return 8633037106;
        }else if(nHeight==4407420){
            return 8629903077;
        }else if(nHeight==4410603){
            return 8618459691;
        }else if(nHeight==4411317){
            return 8615894833;
        }else if(nHeight==4413146){
            return 8609328111;
        }else if(nHeight==4414524){
            return 8604383936;
        }else if(nHeight==4415095){
            return 8602336057;
        }else if(nHeight==4417075){
            return 8595238605;
        }else if(nHeight==4418529){
            return 8590030367;
        }else if(nHeight==4419502){
            return 8586546837;
        }else if(nHeight==4421519){
            return 8579330082;
        }else if(nHeight==4426460){
            return 8561676976;
        }else if(nHeight==4427841){
            return 8556749465;
        }else if(nHeight==4429370){
            return 8551297187;
        }else if(nHeight==4434527){
            return 8532933390;
        }else if(nHeight==4435427){
            return 8529732583;
        }else if(nHeight==4437601){
            return 8522005808;
        }else if(nHeight==4437701){
            return 8521650559;
        }else if(nHeight==4439100){
            return 8516682178;
        }else if(nHeight==4443136){
            return 8502365034;
        }else if(nHeight==4444301){
            return 8498236838;
        }else if(nHeight==4444331){
            return 8498130559;
        }else if(nHeight==4446094){
            return 8491887230;
        }else if(nHeight==4447468){
            return 8487024651;
        }else if(nHeight==4449146){
            return 8481089994;
        }else if(nHeight==4449513){
            return 8479792562;
        }else if(nHeight==4450222){
            return 8477286641;
        }else if(nHeight==4450875){
            return 8474979304;
        }else if(nHeight==4455381){
            return 8459074728;
        }else if(nHeight==4458018){
            return 8449780905;
        }else if(nHeight==4458488){
            return 8448125513;
        }else if(nHeight==4461540){
            return 8437383919;
        }else if(nHeight==4461681){
            return 8436887996;
        }else if(nHeight==4467664){
            return 8415871520;
        }else if(nHeight==4470987){
            return 8404221444;
        }else if(nHeight==4473129){
            return 8396720375;
        }else if(nHeight==4474847){
            return 8390708950;
        }else if(nHeight==4481871){
            return 8366176124;
        }else if(nHeight==4486711){
            return 8349313141;
        }else if(nHeight==4487736){
            return 8345746315;
        }else if(nHeight==4490814){
            return 8335044553;
        }else if(nHeight==4493077){
            return 8327185183;
        }else if(nHeight==4494445){
            return 8322437731;
        }else if(nHeight==4494496){
            return 8322260795;
        }else if(nHeight==4496851){
            return 8314094611;
        }else if(nHeight==4500756){
            return 8300571312;
        }else if(nHeight==4503868){
            return 8289809981;
        }else if(nHeight==4506047){
            return 8282283280;
        }else if(nHeight==4506267){
            return 8281523736;
        }else if(nHeight==4508007){
            return 8275518887;
        }else if(nHeight==4508371){
            return 8274263251;
        }else if(nHeight==4510530){
            return 8266819589;
        }else if(nHeight==4513294){
            return 8257299820;
        }else if(nHeight==4514943){
            return 8251625556;
        }else if(nHeight==4516669){
            return 8245690509;
        }else if(nHeight==4519786){
            return 8234983164;
        }else if(nHeight==4526091){
            return 8213367067;
        }else if(nHeight==4531564){
            return 8194649412;
        }else if(nHeight==4533271){
            return 8188820205;
        }else if(nHeight==4533362){
            return 8188509567;
        }else if(nHeight==4535956){
            return 8179659631;
        }else if(nHeight==4536853){
            return 8176601567;
        }else if(nHeight==4540844){
            return 8163009252;
        }else if(nHeight==4541459){
            return 8160916731;
        }else if(nHeight==4541919){
            return 8159351944;
        }else if(nHeight==4543528){
            return 8153880951;
        }else if(nHeight==4546008){
            return 8145455529;
        }else if(nHeight==4546641){
            return 8143306403;
        }else if(nHeight==4547048){
            return 8141924879;
        }else if(nHeight==4547349){
            return 8140903313;
        }else if(nHeight==4553602){
            return 8119710178;
        }else if(nHeight==4554997){
            return 8114989672;
        }else if(nHeight==4556514){
            return 8109859448;
        }else if(nHeight==4556518){
            return 8109845925;
        }else if(nHeight==4557551){
            return 8106354365;
        }else if(nHeight==4558025){
            return 8104752739;
        }else if(nHeight==4558321){
            return 8103752728;
        }else if(nHeight==4564804){
            return 8081881379;
        }else if(nHeight==4568503){
            return 8069428720;
        }else if(nHeight==4569901){
            return 8064727360;
        }else if(nHeight==4572031){
            return 8057569609;
        }else if(nHeight==4573591){
            return 8052331343;
        }else if(nHeight==4574464){
            return 8049401415;
        }else if(nHeight==4576266){
            return 8043356985;
        }else if(nHeight==4586367){
            return 8009559237;
        }else if(nHeight==4590427){
            return 7996014603;
        }else if(nHeight==4598976){
            return 7967568987;
        }else if(nHeight==4600811){
            return 7961476482;
        }else if(nHeight==4602190){
            return 7956901039;
        }else if(nHeight==4603553){
            return 7952381267;
        }else if(nHeight==4603743){
            return 7951751422;
        }else if(nHeight==4604932){
            return 7947811051;
        }else if(nHeight==4606751){
            return 7941786625;
        }else if(nHeight==4609758){
            return 7931837622;
        }else if(nHeight==4615557){
            return 7912686149;
        }else if(nHeight==4621068){
            return 7894528665;
        }else if(nHeight==4625029){
            return 7881503818;
        }else if(nHeight==4625488){
            return 7879995891;
        }else if(nHeight==4628645){
            return 7869632189;
        }else if(nHeight==4631456){
            return 7860415797;
        }else if(nHeight==4632689){
            return 7856376581;
        }else if(nHeight==4633397){
            return 7854058164;
        }else if(nHeight==4634571){
            return 7850215291;
        }else if(nHeight==4639023){
            return 7835659565;
        }else if(nHeight==4640355){
            return 7831309865;
        }else if(nHeight==4640386){
            return 7831208662;
        }else if(nHeight==4645225){
            return 7815427225;
        }else if(nHeight==4645639){
            return 7814078524;
        }else if(nHeight==4651557){
            return 7794824690;
        }else if(nHeight==4653837){
            return 7787419522;
        }else if(nHeight==4653843){
            return 7787400044;
        }else if(nHeight==4654031){
            return 7786789758;
        }else if(nHeight==4656764){
            return 7777923287;
        }else if(nHeight==4657034){
            return 7777047894;
        }else if(nHeight==4660159){
            return 7766923197;
        }else if(nHeight==4662383){
            return 7759725682;
        }else if(nHeight==4662542){
            return 7759211367;
        }else if(nHeight==4663798){
            return 7755149800;
        }else if(nHeight==4665672){
            return 7749093739;
        }else if(nHeight==4666463){
            return 7746538945;
        }else if(nHeight==4669169){
            return 7737805398;
        }else if(nHeight==4673335){
            return 7724378982;
        }else if(nHeight==4673359){
            return 7724301701;
        }else if(nHeight==4673787){
            return 7722923653;
        }else if(nHeight==4673859){
            return 7722691856;
        }else if(nHeight==4674737){
            return 7719865780;
        }else if(nHeight==4678326){
            return 7708324381;
        }else if(nHeight==4679987){
            return 7702988829;
        }else if(nHeight==4680770){
            return 7700474916;
        }else if(nHeight==4681289){
            return 7698809058;
        }else if(nHeight==4683128){
            return 7692909236;
        }else if(nHeight==4685170){
            return 7686363453;
        }else if(nHeight==4687224){
            return 7679784822;
        }else if(nHeight==4687573){
            return 7678667591;
        }else if(nHeight==4687576){
            return 7678657988;
        }else if(nHeight==4693472){
            return 7659808078;
        }else if(nHeight==4702502){
            return 7631028225;
        }else if(nHeight==4703729){
            return 7627125961;
        }else if(nHeight==4705349){
            return 7621976884;
        }else if(nHeight==4705952){
            return 7620061171;
        }else if(nHeight==4706087){
            return 7619632346;
        }else if(nHeight==4707875){
            return 7613955073;
        }else if(nHeight==4708531){
            return 7611873197;
        }else if(nHeight==4711451){
            return 7602613214;
        }else if(nHeight==4718427){
            return 7580536312;
        }else if(nHeight==4718544){
            return 7580166590;
        }else if(nHeight==4722048){
            return 7569102243;
        }else if(nHeight==4722115){
            return 7568890839;
        }else if(nHeight==4722776){
            return 7566805513;
        }else if(nHeight==4726356){
            return 7555521290;
        }else if(nHeight==4732796){
            return 7535264643;
        }else if(nHeight==4733218){
            return 7533939164;
        }else if(nHeight==4735878){
            return 7525589614;
        }else if(nHeight==4736242){
            return 7524447764;
        }else if(nHeight==4736384){
            return 7524002364;
        }else if(nHeight==4737980){
            return 7518998132;
        }else if(nHeight==4740341){
            return 7511601358;
        }else if(nHeight==4742299){
            return 7505472662;
        }else if(nHeight==4742413){
            return 7505115987;
        }else if(nHeight==4743905){
            return 7500449487;
        }else if(nHeight==4746381){
            return 7492711751;
        }else if(nHeight==4746835){
            return 7491293824;
        }else if(nHeight==4749614){
            return 7482620333;
        }else if(nHeight==4750363){
            return 7480284360;
        }else if(nHeight==4755528){
            return 7464195656;
        }else if(nHeight==4755601){
            return 7463968513;
        }else if(nHeight==4755657){
            return 7463794271;
        }else if(nHeight==4757682){
            return 7457496288;
        }else if(nHeight==4760054){
            return 7450125853;
        }else if(nHeight==4765004){
            return 7434768340;
        }else if(nHeight==4765423){
            return 7433469835;
        }else if(nHeight==4766253){
            return 7430898287;
        }else if(nHeight==4771553){
            return 7414498523;
        }else if(nHeight==4771694){
            return 7414062722;
        }else if(nHeight==4775394){
            return 7402635960;
        }else if(nHeight==4778345){
            return 7393534971;
        }else if(nHeight==4779580){
            return 7389729509;
        }else if(nHeight==4779651){
            return 7389510793;
        }else if(nHeight==4779943){
            return 7388611353;
        }else if(nHeight==4781157){
            return 7384873074;
        }else if(nHeight==4781232){
            return 7384642188;
        }else if(nHeight==4783712){
            return 7377011622;
        }else if(nHeight==4784416){
            return 7374846963;
        }else if(nHeight==4784843){
            return 7373534333;
        }else if(nHeight==4787664){
            return 7364868236;
        }else if(nHeight==4788120){
            return 7363468363;
        }else if(nHeight==4789471){
            return 7359322494;
        }else if(nHeight==4791485){
            return 7353146383;
        }else if(nHeight==4794097){
            return 7345144171;
        }else if(nHeight==4797824){
            return 7333741083;
        }else if(nHeight==4798663){
            return 7331176530;
        }else if(nHeight==4798706){
            return 7331045117;
        }else if(nHeight==4799446){
            return 7328783960;
        }else if(nHeight==4806785){
            return 7306396505;
        }else if(nHeight==4810375){
            return 7295470212;
        }else if(nHeight==4814373){
            return 7283321386;
        }else if(nHeight==4816320){
            return 7277412314;
        }else if(nHeight==4816728){
            return 7276174657;
        }else if(nHeight==4818323){
            return 7271338287;
        }else if(nHeight==4820206){
            return 7265632779;
        }else if(nHeight==4820643){
            return 7264309305;
        }else if(nHeight==4826268){
            return 7247295249;
        }else if(nHeight==4829383){
            return 7237890393;
        }else if(nHeight==4830046){
            return 7235890229;
        }else if(nHeight==4833365){
            return 7225885648;
        }else if(nHeight==4833784){
            return 7224623625;
        }else if(nHeight==4837462){
            return 7213554988;
        }else if(nHeight==4839514){
            return 7207387035;
        }else if(nHeight==4844479){
            return 7192484923;
        }else if(nHeight==4850816){
            return 7173509597;
        }else if(nHeight==4852126){
            return 7169593220;
        }else if(nHeight==4854009){
            return 7163967547;
        }else if(nHeight==4855997){
            return 7158032965;
        }else if(nHeight==4857658){
            return 7153078314;
        }else if(nHeight==4859773){
            return 7146774375;
        }else if(nHeight==4859880){
            return 7146455600;
        }else if(nHeight==4860458){
            return 7144733865;
        }else if(nHeight==4865923){
            return 7128475319;
        }else if(nHeight==4870911){
            return 7113668160;
        }else if(nHeight==4872075){
            return 7110217189;
        }else if(nHeight==4874896){
            return 7101860568;
        }else if(nHeight==4876607){
            return 7096796875;
        }else if(nHeight==4876824){
            return 7096154923;
        }else if(nHeight==4876830){
            return 7096137174;
        }else if(nHeight==4878538){
            return 7091086430;
        }else if(nHeight==4882452){
            return 7079525855;
        }else if(nHeight==4884463){
            return 7073593402;
        }else if(nHeight==4886927){
            return 7066331375;
        }else if(nHeight==4887561){
            return 7064464024;
        }else if(nHeight==4889579){
            return 7058523595;
        }else if(nHeight==4893674){
            return 7046484406;
        }else if(nHeight==4893748){
            return 7046267037;
        }else if(nHeight==4893996){
            return 7045538606;
        }else if(nHeight==4895297){
            return 7041718514;
        }else if(nHeight==4899171){
            return 7030355652;
        }else if(nHeight==4906674){
            return 7008400658;
        }else if(nHeight==4908273){
            return 7003730595;
        }else if(nHeight==4909869){
            return 6999072397;
        }else if(nHeight==4911042){
            return 6995650772;
        }else if(nHeight==4916061){
            return 6981029298;
        }else if(nHeight==4916434){
            return 6979943886;
        }else if(nHeight==4918723){
            return 6973286701;
        }else if(nHeight==4920796){
            return 6967263196;
        }else if(nHeight==4920906){
            return 6966943715;
        }else if(nHeight==4924701){
            return 6955930587;
        }else if(nHeight==4927552){
            return 6947668410;
        }else if(nHeight==4929847){
            return 6941024647;
        }else if(nHeight==4930778){
            return 6938331320;
        }else if(nHeight==4933084){
            return 6931664700;
        }else if(nHeight==4933314){
            return 6931000124;
        }else if(nHeight==4933769){
            return 6929685607;
        }else if(nHeight==4937841){
            return 6917932496;
        }else if(nHeight==4941430){
            return 6907590008;
        }else if(nHeight==4943329){
            return 6902123881;
        }else if(nHeight==4948245){
            return 6887993633;
        }else if(nHeight==4950558){
            return 6881355299;
        }else if(nHeight==4952119){
            return 6876878829;
        }else if(nHeight==4955223){
            return 6867986158;
        }else if(nHeight==4956255){
            return 6865032122;
        }else if(nHeight==4956742){
            return 6863638556;
        }else if(nHeight==4956971){
            return 6862983363;
        }else if(nHeight==4957772){
            return 6860692110;
        }else if(nHeight==4958259){
            return 6859299425;
        }else if(nHeight==4960247){
            return 6853617228;
        }else if(nHeight==4961155){
            return 6851023505;
        }else if(nHeight==4965772){
            return 6837850115;
        }else if(nHeight==4965877){
            return 6837550820;
        }else if(nHeight==4968060){
            return 6831331301;
        }else if(nHeight==4972958){
            return 6817397137;
        }else if(nHeight==4973936){
            return 6814618262;
        }else if(nHeight==4974784){
            return 6812209684;
        }else if(nHeight==4982819){
            return 6789430045;
        }else if(nHeight==4983817){
            return 6786605987;
        }else if(nHeight==4985193){
            return 6782714222;
        }else if(nHeight==4987859){
            return 6775180277;
        }else if(nHeight==4988538){
            return 6773262804;
        }else if(nHeight==4990037){
            return 6769031600;
        }else if(nHeight==4993364){
            return 6759649970;
        }else if(nHeight==4994557){
            return 6756289061;
        }else if(nHeight==4995603){
            return 6753343654;
        }else if(nHeight==4996708){
            return 6750233505;
        }else if(nHeight==4998849){
            return 6744211491;
        }else if(nHeight==5002891){
            return 6732857155;
        }else if(nHeight==5003002){
            return 6732545616;
        }else if(nHeight==5004889){
            return 6727251658;
        }else if(nHeight==5005253){
            return 6726230939;
        }else if(nHeight==5007215){
            return 6720731818;
        }else if(nHeight==5008164){
            return 6718073561;
        }else if(nHeight==5013108){
            return 6704241852;
        }else if(nHeight==5016689){
            return 6694241162;
        }else if(nHeight==5017481){
            return 6692031352;
        }else if(nHeight==5018048){
            return 6690449777;
        }else if(nHeight==5019412){
            return 6686646602;
        }else if(nHeight==5022351){
            return 6678459277;
        }else if(nHeight==5023085){
            return 6676416100;
        }else if(nHeight==5025499){
            return 6669700849;
        }else if(nHeight==5027889){
            return 6663059015;
        }else if(nHeight==5032485){
            return 6650305268;
        }else if(nHeight==5032829){
            return 6649351662;
        }else if(nHeight==5035798){
            return 6641126947;
        }else if(nHeight==5037656){
            return 6635985097;
        }else if(nHeight==5037948){
            return 6635177375;
        }else if(nHeight==5037953){
            return 6635163545;
        }else if(nHeight==5041396){
            return 6625647048;
        }else if(nHeight==5042255){
            return 6623274889;
        }else if(nHeight==5047805){
            return 6607968824;
        }else if(nHeight==5049595){
            return 6603039821;
        }else if(nHeight==5051896){
            return 6596709112;
        }else if(nHeight==5058057){
            return 6579788306;
        }else if(nHeight==5060228){
            return 6573836136;
        }else if(nHeight==5061832){
            return 6569441953;
        }else if(nHeight==5063248){
            return 6565565240;
        }else if(nHeight==5065245){
            return 6560101758;
        }else if(nHeight==5065900){
            return 6558310770;
        }else if(nHeight==5071739){
            return 6542366593;
        }else if(nHeight==5084269){
            return 6508282457;
        }else if(nHeight==5086126){
            return 6503246172;
        }else if(nHeight==5091636){
            return 6488325678;
        }else if(nHeight==5092872){
            return 6484983425;
        }else if(nHeight==5093458){
            return 6483399431;
        }else if(nHeight==5097056){
            return 6473682293;
        }else if(nHeight==5097732){
            return 6471858242;
        }else if(nHeight==5098266){
            return 6470417713;
        }else if(nHeight==5105470){
            return 6451015371;
        }else if(nHeight==5105748){
            return 6450267808;
        }else if(nHeight==5110356){
            return 6437889159;
        }else if(nHeight==5111415){
            return 6435047685;
        }else if(nHeight==5114408){
            return 6427023745;
        }else if(nHeight==5120731){
            return 6410105281;
        }else if(nHeight==5120903){
            return 6409645683;
        }else if(nHeight==5124470){
            return 6400121793;
        }else if(nHeight==5132959){
            return 6377513038;
        }else if(nHeight==5137227){
            return 6366176262;
        }else if(nHeight==5141096){
            return 6355916736;
        }else if(nHeight==5142172){
            return 6353066419;
        }else if(nHeight==5142333){
            return 6352640041;
        }else if(nHeight==5144057){
            return 6348076147;
        }else if(nHeight==5146211){
            return 6342378534;
        }else if(nHeight==5146862){
            return 6340657560;
        }else if(nHeight==5147739){
            return 6338339874;
        }else if(nHeight==5148013){
            return 6337615936;
        }else if(nHeight==5148807){
            return 6335518568;
        }else if(nHeight==5150727){
            return 6330449715;
        }else if(nHeight==5152610){
            return 6325482482;
        }else if(nHeight==5152945){
            return 6324599182;
        }else if(nHeight==5153302){
            return 6323658010;
        }else if(nHeight==5154868){
            return 6319531162;
        }else if(nHeight==5156319){
            return 6315709775;
        }else if(nHeight==5163676){
            return 6296369742;
        }else if(nHeight==5175441){
            return 6265564992;
        }else if(nHeight==5179945){
            return 6253811929;
        }else if(nHeight==5180990){
            return 6251088183;
        }else if(nHeight==5185478){
            return 6239403892;
        }else if(nHeight==5190604){
            return 6226085314;
        }else if(nHeight==5192145){
            return 6222086987;
        }else if(nHeight==5199293){
            return 6203574114;
        }else if(nHeight==5203604){
            return 6192435532;
        }else if(nHeight==5203875){
            return 6191736002;
        }else if(nHeight==5209396){
            return 6177501883;
        }else if(nHeight==5209552){
            return 6177100163;
        }else if(nHeight==5210907){
            return 6173611963;
        }else if(nHeight==5212110){
            return 6170516710;
        }else if(nHeight==5212886){
            return 6168520928;
        }else if(nHeight==5216833){
            return 6158379689;
        }else if(nHeight==5222274){
            return 6144427163;
        }else if(nHeight==5224435){
            return 6138894418;
        }else if(nHeight==5228895){
            return 6127491357;
        }else if(nHeight==5229630){
            return 6125614187;
        }else if(nHeight==5232723){
            return 6117721049;
        }else if(nHeight==5233126){
            return 6116693368;
        }else if(nHeight==5233688){
            return 6115260513;
        }else if(nHeight==5234507){
            return 6113173021;
        }else if(nHeight==5237289){
            return 6106087495;
        }else if(nHeight==5237848){
            return 6104664759;
        }else if(nHeight==5241846){
            return 6094498929;
        }else if(nHeight==5249110){
            return 6076071848;
        }else if(nHeight==5250957){
            return 6071395329;
        }else if(nHeight==5251622){
            return 6069712461;
        }else if(nHeight==5255191){
            return 6060688614;
        }else if(nHeight==5257188){
            return 6055645261;
        }else if(nHeight==5258674){
            return 6051895144;
        }else if(nHeight==5264757){
            return 6036568087;
        }else if(nHeight==5265487){
            return 6034731350;
        }else if(nHeight==5268163){
            return 6028003103;
        }else if(nHeight==5270620){
            return 6021832092;
        }else if(nHeight==5270948){
            return 6021008764;
        }else if(nHeight==5271679){
            return 6019174252;
        }else if(nHeight==5272401){
            return 6017362875;
        }else if(nHeight==5274861){
            return 6011195239;
        }else if(nHeight==5275647){
            return 6009225937;
        }else if(nHeight==5279596){
            return 5999341582;
        }else if(nHeight==5279929){
            return 5998508826;
        }else if(nHeight==5281597){
            return 5994339283;
        }else if(nHeight==5285429){
            return 5984771307;
        }else if(nHeight==5294586){
            return 5961969345;
        }else if(nHeight==5298992){
            return 5951028886;
        }else if(nHeight==5299266){
            return 5950349185;
        }else if(nHeight==5303577){
            return 5939665271;
        }else if(nHeight==5303757){
            return 5939219596;
        }else if(nHeight==5305467){
            return 5934987351;
        }else if(nHeight==5316348){
            return 5908127469;
        }else if(nHeight==5317058){
            return 5906379056;
        }else if(nHeight==5317845){
            return 5904441631;
        }else if(nHeight==5329091){
            return 5876825719;
        }else if(nHeight==5330355){
            return 5873729900;
        }else if(nHeight==5331912){
            return 5869918700;
        }else if(nHeight==5332790){
            return 5867770636;
        }else if(nHeight==5333892){
            return 5865075659;
        }else if(nHeight==5336198){
            return 5859440265;
        }else if(nHeight==5339899){
            return 5850407092;
        }else if(nHeight==5341111){
            return 5847451945;
        }else if(nHeight==5342582){
            return 5843867299;
        }else if(nHeight==5348445){
            return 5829601710;
        }else if(nHeight==5349939){
            return 5825972147;
        }else if(nHeight==5351031){
            return 5823320643;
        }else if(nHeight==5354220){
            return 5815584283;
        }else if(nHeight==5356404){
            return 5810291935;
        }else if(nHeight==5361105){
            return 5798916636;
        }else if(nHeight==5361716){
            return 5797439798;
        }else if(nHeight==5361952){
            return 5796869467;
        }else if(nHeight==5363279){
            return 5793663608;
        }else if(nHeight==5368127){
            return 5781966535;
        }else if(nHeight==5368146){
            return 5781920739;
        }else if(nHeight==5370180){
            return 5777020254;
        }else if(nHeight==5371503){
            return 5773835000;
        }else if(nHeight==5372092){
            return 5772417489;
        }else if(nHeight==5376793){
            return 5761116340;
        }else if(nHeight==5387384){
            return 5735736707;
        }else if(nHeight==5394886){
            return 5717827043;
        }else if(nHeight==5401851){
            return 5701249434;
        }else if(nHeight==5403720){
            return 5696809155;
        }else if(nHeight==5403779){
            return 5696669042;
        }else if(nHeight==5404901){
            return 5694005176;
        }else if(nHeight==5408643){
            return 5685129876;
        }else if(nHeight==5410262){
            return 5681294210;
        }else if(nHeight==5413885){
            return 5672720125;
        }else if(nHeight==5419249){
            return 5660049596;
        }else if(nHeight==5427994){
            return 5639453317;
        }else if(nHeight==5428379){
            return 5638548287;
        }else if(nHeight==5433940){
            return 5625492081;
        }else if(nHeight==5437522){
            return 5617098216;
        }else if(nHeight==5439317){
            return 5612896622;
        }else if(nHeight==5440868){
            return 5609268695;
        }else if(nHeight==5441384){
            return 5608062245;
        }else if(nHeight==5443482){
            return 5603159622;
        }else if(nHeight==5443603){
            return 5602876999;
        }else if(nHeight==5443674){
            return 5602711169;
        }else if(nHeight==5444095){
            return 5601727968;
        }else if(nHeight==5445560){
            return 5598307960;
        }else if(nHeight==5446035){
            return 5597199532;
        }else if(nHeight==5449437){
            return 5589267266;
        }else if(nHeight==5454789){
            return 5576811034;
        }else if(nHeight==5455840){
            return 5574368202;
        }else if(nHeight==5456041){
            return 5573901141;
        }else if(nHeight==5457164){
            return 5571292361;
        }else if(nHeight==5458287){
            return 5568684802;
        }else if(nHeight==5462052){
            return 5559951531;
        }else if(nHeight==5463388){
            return 5556855847;
        }else if(nHeight==5463469){
            return 5556668215;
        }else if(nHeight==5466544){
            return 5549549832;
        }else if(nHeight==5466764){
            return 5549040899;
        }else if(nHeight==5470481){
            return 5540449297;
        }else if(nHeight==5477216){
            return 5524915660;
        }else if(nHeight==5478100){
            return 5522880034;
        }else if(nHeight==5478162){
            return 5522737292;
        }else if(nHeight==5481813){
            return 5514338132;
        }else if(nHeight==5489467){
            return 5496771469;
        }else if(nHeight==5494564){
            return 5485104418;
        }else if(nHeight==5504518){
            return 5462391032;
        }else if(nHeight==5507513){
            return 5455575361;
        }else if(nHeight==5508540){
            return 5453240193;
        }else if(nHeight==5509953){
            return 5450028981;
        }else if(nHeight==5513785){
            return 5441329816;
        }else if(nHeight==5515060){
            return 5438438471;
        }else if(nHeight==5517745){
            return 5432354662;
        }else if(nHeight==5517816){
            return 5432193879;
        }else if(nHeight==5521040){
            return 5424897984;
        }else if(nHeight==5522275){
            return 5422105782;
        }else if(nHeight==5523140){
            return 5420150966;
        }else if(nHeight==5523640){
            return 5419021336;
        }else if(nHeight==5524212){
            return 5417729328;
        }else if(nHeight==5527806){
            return 5409618407;
        }else if(nHeight==5528820){
            return 5407332214;
        }else if(nHeight==5536806){
            return 5389360490;
        }else if(nHeight==5539615){
            return 5383053316;
        }else if(nHeight==5545737){
            return 5369332876;
        }else if(nHeight==5548778){
            return 5362530485;
        }else if(nHeight==5553723){
            return 5351487446;
        }else if(nHeight==5558657){
            return 5340491637;
        }else if(nHeight==5558867){
            return 5340024137;
        }else if(nHeight==5562204){
            return 5332600829;
        }else if(nHeight==5567975){
            return 5319787324;
        }else if(nHeight==5568906){
            return 5317723086;
        }else if(nHeight==5573076){
            return 5308487075;
        }else if(nHeight==5574128){
            return 5306159566;
        }else if(nHeight==5576610){
            return 5300672281;
        }else if(nHeight==5594497){
            return 5261294577;
        }else if(nHeight==5594898){
            return 5260415148;
        }else if(nHeight==5594942){
            return 5260318661;
        }else if(nHeight==5603530){
            return 5241519996;
        }else if(nHeight==5603927){
            return 5240652611;
        }else if(nHeight==5603930){
            return 5240646057;
        }else if(nHeight==5604408){
            return 5239601891;
        }else if(nHeight==5607950){
            return 5231871057;
        }else if(nHeight==5609073){
            return 5229422359;
        }else if(nHeight==5614261){
            return 5218124810;
        }else if(nHeight==5618690){
            return 5208499397;
        }else if(nHeight==5619360){
            return 5207044853;
        }else if(nHeight==5619812){
            return 5206063808;
        }else if(nHeight==5620506){
            return 5204557873;
        }else if(nHeight==5622027){
            return 5201258925;
        }else if(nHeight==5622697){
            return 5199806403;
        }else if(nHeight==5623700){
            return 5197632714;
        }else if(nHeight==5624266){
            return 5196406487;
        }else if(nHeight==5625921){
            return 5192822624;
        }else if(nHeight==5626537){
            return 5191489322;
        }else if(nHeight==5635324){
            return 5172507517;
        }else if(nHeight==5635818){
            return 5171442434;
        }else if(nHeight==5638286){
            return 5166124615;
        }else if(nHeight==5638389){
            return 5165902799;
        }else if(nHeight==5640891){
            return 5160517533;
        }else if(nHeight==5641984){
            return 5158166739;
        }else if(nHeight==5645929){
            return 5149690847;
        }else if(nHeight==5652837){
            return 5134882438;
        }else if(nHeight==5654813){
            return 5130654402;
        }else if(nHeight==5656796){
            return 5126414888;
        }else if(nHeight==5658769){
            return 5122200230;
        }else if(nHeight==5662159){
            return 5114966713;
        }else if(nHeight==5664749){
            return 5109447105;
        }else if(nHeight==5666522){
            return 5105672059;
        }else if(nHeight==5669026){
            return 5100345329;
        }else if(nHeight==5673575){
            return 5090682509;
        }else if(nHeight==5676561){
            return 5084349711;
        }else if(nHeight==5677603){
            return 5082141661;
        }else if(nHeight==5678962){
            return 5079263313;
        }else if(nHeight==5683908){
            return 5068801492;
        }else if(nHeight==5684711){
            return 5067105014;
        }else if(nHeight==5687665){
            return 5060869057;
        }else if(nHeight==5694752){
            return 5045939523;
        }else if(nHeight==5699647){
            return 5035653398;
        }else if(nHeight==5700285){
            return 5034314280;
        }else if(nHeight==5701606){
            return 5031542724;
        }else if(nHeight==5702171){
            return 5030357778;
        }else if(nHeight==5703211){
            return 5028177368;
        }else if(nHeight==5705617){
            return 5023136695;
        }else if(nHeight==5707008){
            return 5020224796;
        }else if(nHeight==5709355){
            return 5015315447;
        }else if(nHeight==5711753){
            return 5010304377;
        }else if(nHeight==5715410){
            return 5002672029;
        }else if(nHeight==5721538){
            return 4989908632;
        }else if(nHeight==5721917){
            return 4989120321;
        }else if(nHeight==5722405){
            return 4988105476;
        }else if(nHeight==5723788){
            return 4985230510;
        }else if(nHeight==5736728){
            return 4958411127;
        }else if(nHeight==5742253){
            return 4947004037;
        }else if(nHeight==5743694){
            return 4944033220;
        }else if(nHeight==5744406){
            return 4942565994;
        }else if(nHeight==5750930){
            return 4929142175;
        }else if(nHeight==5751161){
            return 4928667537;
        }else if(nHeight==5755952){
            return 4918833719;
        }else if(nHeight==5756567){
            return 4917572816;
        }else if(nHeight==5756660){
            return 4917382171;
        }else if(nHeight==5760011){
            return 4910517730;
        }else if(nHeight==5761794){
            return 4906869205;
        }else if(nHeight==5763413){
            return 4903558619;
        }else if(nHeight==5764342){
            return 4901659977;
        }else if(nHeight==5764863){
            return 4900595506;
        }else if(nHeight==5770750){
            return 4888583648;
        }else if(nHeight==5773333){
            return 4883322582;
        }else if(nHeight==5773387){
            return 4883212655;
        }else if(nHeight==5775636){
            return 4878636596;
        }else if(nHeight==5777523){
            return 4874800410;
        }else if(nHeight==5780473){
            return 4868809236;
        }else if(nHeight==5781926){
            return 4865861036;
        }else if(nHeight==5787687){
            return 4854189278;
        }else if(nHeight==5788406){
            return 4852734554;
        }else if(nHeight==5791583){
            return 4846311874;
        }else if(nHeight==5796580){
            return 4836227045;
        }else if(nHeight==5796968){
            return 4835444871;
        }else if(nHeight==5798195){
            return 4832972179;
        }else if(nHeight==5798408){
            return 4832543063;
        }else if(nHeight==5801298){
            return 4826724549;
        }else if(nHeight==5805466){
            return 4818345339;
        }else if(nHeight==5805751){
            return 4817772916;
        }else if(nHeight==5810010){
            return 4809226804;
        }else if(nHeight==5816862){
            return 4795509379;
        }else if(nHeight==5820659){
            return 4787924795;
        }else if(nHeight==5823886){
            return 4781488228;
        }else if(nHeight==5826828){
            return 4775627662;
        }else if(nHeight==5834210){
            return 4760954067;
        }else if(nHeight==5834649){
            return 4760082865;
        }else if(nHeight==5836341){
            return 4756726557;
        }else if(nHeight==5837302){
            return 4754821339;
        }else if(nHeight==5840341){
            return 4748801430;
        }else if(nHeight==5841495){
            return 4746517486;
        }else if(nHeight==5843166){
            return 4743212266;
        }else if(nHeight==5844074){
            return 4741417217;
        }else if(nHeight==5846267){
            return 4737084619;
        }else if(nHeight==5847956){
            return 4733750447;
        }else if(nHeight==5854192){
            return 4721460580;
        }else if(nHeight==5858679){
            return 4712637374;
        }else if(nHeight==5864822){
            return 4700584554;
        }else if(nHeight==5865485){
            return 4699285565;
        }else if(nHeight==5870918){
            return 4688654423;
        }else if(nHeight==5873462){
            return 4683684666;
        }else if(nHeight==5875019){
            return 4680645633;
        }else if(nHeight==5879799){
            return 4671328096;
        }else if(nHeight==5885001){
            return 4661209040;
        }else if(nHeight==5891539){
            return 4648522246;
        }else if(nHeight==5895943){
            return 4639995885;
        }else if(nHeight==5896447){
            return 4639021114;
        }else if(nHeight==5898894){
            return 4634291356;
        }else if(nHeight==5899152){
            return 4633792954;
        }else if(nHeight==5899794){
            return 4632552977;
        }else if(nHeight==5905267){
            return 4621995732;
        }else if(nHeight==5906838){
            return 4618969769;
        }else if(nHeight==5907660){
            return 4617387273;
        }else if(nHeight==5907712){
            return 4617287182;
        }else if(nHeight==5913680){
            return 4605814217;
        }else if(nHeight==5921120){
            return 4591551373;
        }else if(nHeight==5927152){
            return 4580020161;
        }else if(nHeight==5935754){
            return 4563626027;
        }else if(nHeight==5937514){
            return 4560278967;
        }else if(nHeight==5938221){
            return 4558935129;
        }else if(nHeight==5940060){
            return 4555441484;
        }else if(nHeight==5942427){
            return 4550948710;
        }else if(nHeight==5942852){
            return 4550142492;
        }else if(nHeight==5942958){
            return 4549941434;
        }else if(nHeight==5943044){
            return 4549778318;
        }else if(nHeight==5948026){
            return 4540338945;
        }else if(nHeight==5951551){
            return 4533671976;
        }else if(nHeight==5954146){
            return 4528770209;
        }else if(nHeight==5958941){
            return 4519726741;
        }else if(nHeight==5960546){
            return 4516703714;
        }else if(nHeight==5964731){
            return 4508830748;
        }else if(nHeight==5976580){
            return 4486614354;
        }else if(nHeight==5985198){
            return 4470524747;
        }else if(nHeight==5985306){
            return 4470323480;
        }else if(nHeight==5986255){
            return 4468555329;
        }else if(nHeight==5992905){
            return 4456184835;
        }else if(nHeight==5993922){
            return 4454296008;
        }else if(nHeight==5994751){
            return 4452756937;
        }else if(nHeight==5996291){
            return 4449899278;
        }else if(nHeight==6007466){
            return 4429217560;
        }else if(nHeight==6008701){
            return 4426937836;
        }else if(nHeight==6021201){
            return 4403929644;
        }else if(nHeight==6021519){
            return 4403345878;
        }else if(nHeight==6023766){
            return 4399223170;
        }else if(nHeight==6029223){
            return 4389226942;
        }else if(nHeight==6030138){
            return 4387553054;
        }else if(nHeight==6041753){
            return 4366360139;
        }else if(nHeight==6043707){
            return 4362804912;
        }else if(nHeight==6044642){
            return 4361104740;
        }else if(nHeight==6055822){
            return 4340826663;
        }else if(nHeight==6056827){
            return 4339008438;
        }else if(nHeight==6059279){
            return 4334575526;
        }else if(nHeight==6060286){
            return 4332756307;
        }else if(nHeight==6063280){
            return 4327351936;
        }else if(nHeight==6063640){
            return 4326702566;
        }else if(nHeight==6067586){
            return 4319591134;
        }else if(nHeight==6068993){
            return 4317058284;
        }else if(nHeight==6071671){
            return 4312241511;
        }else if(nHeight==6072046){
            return 4311567448;
        }else if(nHeight==6074182){
            return 4307729994;
        }else if(nHeight==6076118){
            return 4304254803;
        }else if(nHeight==6076947){
            return 4302767575;
        }else if(nHeight==6082696){
            return 4292467984;
        }else if(nHeight==6084949){
            return 4288438361;
        }else if(nHeight==6087150){
            return 4284505396;
        }else if(nHeight==6091299){
            return 4277101357;
        }else if(nHeight==6099734){
            return 4262088222;
        }else if(nHeight==6110584){
            return 4242854184;
        }else if(nHeight==6112945){
            return 4238680299;
        }else if(nHeight==6113022){
            return 4238544244;
        }else if(nHeight==6113667){
            return 4237404734;
        }else if(nHeight==6122871){
            return 4221177528;
        }else if(nHeight==6124117){
            return 4218985536;
        }else if(nHeight==6127972){
            return 4212210937;
        }else if(nHeight==6129946){
            return 4208746133;
        }else if(nHeight==6139648){
            return 4191758372;
        }else if(nHeight==6142423){
            return 4186912096;
        }else if(nHeight==6142502){
            return 4186774212;
        }else if(nHeight==6145745){
            return 4181117904;
        }else if(nHeight==6146191){
            return 4180340607;
        }else if(nHeight==6148105){
            return 4177006493;
        }else if(nHeight==6149906){
            return 4173871649;
        }else if(nHeight==6150148){
            return 4173450600;
        }else if(nHeight==6151139){
            return 4171726830;
        }else if(nHeight==6151334){
            return 4171387726;
        }else if(nHeight==6151531){
            return 4171045172;
        }else if(nHeight==6161353){
            return 4154001777;
        }else if(nHeight==6163711){
            return 4149920490;
        }else if(nHeight==6168084){
            return 4142362200;
        }else if(nHeight==6171543){
            return 4136393419;
        }else if(nHeight==6174923){
            return 4130569267;
        }else if(nHeight==6176325){
            return 4128155857;
        }else if(nHeight==6176369){
            return 4128080138;
        }else if(nHeight==6177644){
            return 4125886611;
        }else if(nHeight==6180068){
            return 4121719543;
        }else if(nHeight==6183712){
            return 4115463106;
        }else if(nHeight==6186467){
            return 4110739311;
        }else if(nHeight==6188476){
            return 4107298046;
        }else if(nHeight==6197287){
            return 4092239461;
        }else if(nHeight==6198777){
            return 4089698415;
        }else if(nHeight==6200133){
            return 4087387264;
        }else if(nHeight==6206739){
            return 4076146739;
        }else if(nHeight==6208768){
            return 4072700477;
        }else if(nHeight==6212075){
            return 4067089772;
        }else if(nHeight==6215353){
            return 4061535897;
        }else if(nHeight==6218090){
            return 4056904442;
        }else if(nHeight==6218277){
            return 4056588200;
        }else if(nHeight==6219675){
            return 4054224776;
        }else if(nHeight==6225003){
            return 4045230009;
        }else if(nHeight==6238336){
            return 4022808501;
        }else if(nHeight==6241658){
            return 4017241409;
        }else if(nHeight==6241801){
            return 4017001939;
        }else if(nHeight==6242950){
            return 4015078324;
        }else if(nHeight==6243767){
            return 4013711092;
        }else if(nHeight==6244150){
            return 4013070310;
        }else if(nHeight==6251701){
            return 4000457903;
        }else if(nHeight==6251707){
            return 4000447897;
        }else if(nHeight==6258287){
            return 3989489700;
        }else if(nHeight==6261126){
            return 3984770962;
        }else if(nHeight==6263879){
            return 3980200496;
        }else if(nHeight==6266273){
            return 3976230295;
        }else if(nHeight==6275551){
            return 3960881072;
        }else if(nHeight==6280174){
            return 3953255062;
        }else if(nHeight==6283048){
            return 3948521571;
        }else if(nHeight==6283561){
            return 3947677254;
        }else if(nHeight==6285681){
            return 3944189983;
        }else if(nHeight==6288969){
            return 3938787516;
        }else if(nHeight==6295446){
            return 3928166884;
        }else if(nHeight==6296303){
            return 3926763769;
        }else if(nHeight==6300213){
            return 3920368516;
        }else if(nHeight==6303464){
            return 3915059064;
        }else if(nHeight==6311468){
            return 3902017756;
        }else if(nHeight==6312331){
            return 3900614225;
        }else if(nHeight==6313923){
            return 3898026417;
        }else if(nHeight==6315891){
            return 3894829791;
        }else if(nHeight==6322062){
            return 3884823212;
        }else if(nHeight==6323999){
            return 3881687576;
        }else if(nHeight==6326824){
            return 3877118973;
        }else if(nHeight==6328852){
            return 3873842598;
        }else if(nHeight==6329442){
            return 3872889932;
        }else if(nHeight==6329723){
            return 3872436287;
        }else if(nHeight==6330563){
            return 3871080512;
        }else if(nHeight==6335724){
            return 3862760977;
        }else if(nHeight==6337852){
            return 3859335848;
        }else if(nHeight==6338852){
            return 3857727344;
        }else if(nHeight==6340187){
            return 3855581036;
        }else if(nHeight==6341238){
            return 3853892161;
        }else if(nHeight==6343535){
            return 3850203636;
        }else if(nHeight==6349837){
            return 3840101997;
        }else if(nHeight==6354403){
            return 3832799599;
        }else if(nHeight==6363954){
            return 3817569569;
        }else if(nHeight==6364199){
            return 3817179689;
        }else if(nHeight==6372699){
            return 3803677866;
        }else if(nHeight==6377284){
            return 3796414663;
        }else if(nHeight==6381230){
            return 3790174820;
        }else if(nHeight==6382950){
            return 3787458180;
        }else if(nHeight==6387110){
            return 3780895749;
        }else if(nHeight==6389401){
            return 3777286536;
        }else if(nHeight==6390101){
            return 3776184452;
        }else if(nHeight==6392403){
            return 3772562437;
        }else if(nHeight==6397265){
            return 3764923878;
        }else if(nHeight==6399442){
            return 3761508666;
        }else if(nHeight==6404273){
            return 3753941000;
        }else if(nHeight==6406679){
            return 3750177730;
        }else if(nHeight==6418119){
            return 3732335760;
        }else if(nHeight==6423920){
            return 3723320907;
        }else if(nHeight==6425278){
            return 3721213699;
        }else if(nHeight==6431118){
            return 3712165357;
        }else if(nHeight==6432830){
            return 3709517002;
        }else if(nHeight==6432876){
            return 3709445869;
        }else if(nHeight==6434763){
            return 3706529045;
        }else if(nHeight==6440721){
            return 3697334531;
        }else if(nHeight==6441614){
            return 3695958401;
        }else if(nHeight==6444908){
            return 3690886712;
        }else if(nHeight==6448916){
            return 3684725080;
        }else if(nHeight==6449140){
            return 3684381021;
        }else if(nHeight==6454786){
            return 3675719493;
        }else if(nHeight==6457942){
            return 3670886749;
        }else if(nHeight==6458866){
            return 3669473042;
        }else if(nHeight==6464521){
            return 3660832826;
        }else if(nHeight==6465086){
            return 3659970687;
        }else if(nHeight==6466928){
            return 3657161371;
        }else if(nHeight==6482428){
            return 3633606888;
        }else if(nHeight==6485001){
            return 3629711552;
        }else if(nHeight==6489620){
            return 3622729194;
        }else if(nHeight==6493807){
            return 3616411480;
        }else if(nHeight==6494147){
            return 3615898942;
        }else if(nHeight==6494813){
            return 3614895181;
        }else if(nHeight==6495157){
            return 3614376831;
        }else if(nHeight==6496517){
            return 3612328268;
        }else if(nHeight==6501634){
            return 3604630947;
        }else if(nHeight==6504138){
            return 3600870248;
        }else if(nHeight==6505341){
            return 3599064886;
        }else if(nHeight==6507747){
            return 3595456877;
        }else if(nHeight==6512374){
            return 3588528446;
        }else if(nHeight==6514203){
            return 3585793400;
        }else if(nHeight==6514228){
            return 3585756030;
        }else if(nHeight==6524549){
            return 3570361421;
        }else if(nHeight==6531542){
            return 3559968373;
        }else if(nHeight==6534853){
            return 3555058099;
        }else if(nHeight==6538834){
            return 3549163169;
        }else if(nHeight==6539192){
            return 3548633534;
        }else if(nHeight==6542873){
            return 3543192344;
        }else if(nHeight==6543007){
            return 3542994425;
        }else if(nHeight==6552942){
            return 3528351110;
        }else if(nHeight==6558106){
            return 3520763749;
        }else if(nHeight==6563597){
            return 3512713831;
        }else if(nHeight==6565484){
            return 3509951702;
        }else if(nHeight==6585763){
            return 3480404704;
        }else if(nHeight==6586420){
            return 3479451610;
        }else if(nHeight==6588577){
            return 3476324337;
        }else if(nHeight==6589515){
            return 3474965278;
        }else if(nHeight==6589911){
            return 3474391677;
        }else if(nHeight==6590297){
            return 3473832652;
        }else if(nHeight==6590357){
            return 3473745765;
        }else if(nHeight==6590736){
            return 3473196979;
        }else if(nHeight==6596780){
            return 3464457069;
        }else if(nHeight==6600091){
            return 3459678534;
        }else if(nHeight==6609254){
            return 3446488548;
        }else if(nHeight==6610463){
            return 3444751972;
        }else if(nHeight==6612600){
            return 3441684581;
        }else if(nHeight==6616145){
            return 3436602208;
        }else if(nHeight==6622854){
            return 3427004225;
        }else if(nHeight==6635095){
            return 3409561111;
        }else if(nHeight==6638447){
            return 3404800100;
        }else if(nHeight==6642066){
            return 3399667319;
        }else if(nHeight==6644773){
            return 3395833075;
        }else if(nHeight==6646365){
            return 3393580157;
        }else if(nHeight==6650039){
            return 3388386601;
        }else if(nHeight==6656267){
            return 3379600868;
        }else if(nHeight==6663490){
            return 3369440032;
        }else if(nHeight==6675958){
            return 3351972704;
        }else if(nHeight==6675964){
            return 3351964320;
        }else if(nHeight==6687377){
            return 3336054452;
        }else if(nHeight==6697141){
            return 3322503262;
        }else if(nHeight==6706160){
            return 3310034948;
        }else if(nHeight==6706940){
            return 3308958838;
        }else if(nHeight==6707477){
            return 3308218181;
        }else if(nHeight==6711617){
            return 3302513651;
        }else if(nHeight==6720085){
            return 3290876163;
        }else if(nHeight==6721952){
            return 3288315886;
        }else if(nHeight==6733941){
            return 3271922383;
        }else if(nHeight==6734659){
            return 3270943203;
        }else if(nHeight==6738573){
            return 3265610595;
        }else if(nHeight==6744504){
            return 3257546500;
        }else if(nHeight==6746542){
            return 3254780129;
        }else if(nHeight==6746822){
            return 3254400242;
        }else if(nHeight==6748996){
            return 3251452199;
        }else if(nHeight==6755722){
            return 3242348338;
        }else if(nHeight==6759754){
            return 3236903119;
        }else if(nHeight==6761532){
            return 3234504835;
        }else if(nHeight==6762466){
            return 3233245706;
        }else if(nHeight==6767626){
            return 3226298319;
        }else if(nHeight==6770566){
            return 3222346602;
        }else if(nHeight==6771775){
            return 3220722964;
        }else if(nHeight==6772447){
            return 3219820849;
        }else if(nHeight==6773027){
            return 3219042441;
        }else if(nHeight==6776823){
            return 3213952539;
        }else if(nHeight==6777874){
            return 3212544719;
        }else if(nHeight==6787877){
            return 3199176475;
        }else if(nHeight==6788731){
            return 3198037750;
        }else if(nHeight==6792770){
            return 3192657630;
        }else if(nHeight==6801282){
            return 3181348902;
        }else if(nHeight==6803835){
            return 3177964896;
        }else if(nHeight==6808763){
            return 3171443000;
        }else if(nHeight==6811412){
            return 3167942749;
        }else if(nHeight==6824322){
            return 3150939358;
        }else if(nHeight==6827324){
            return 3146998606;
        }else if(nHeight==6828608){
            return 3145314593;
        }else if(nHeight==6829939){
            return 3143569889;
        }else if(nHeight==6832085){
            return 3140758903;
        }else if(nHeight==6837843){
            return 3133229072;
        }else if(nHeight==6840191){
            return 3130163738;
        }else if(nHeight==6843043){
            return 3126444461;
        }else if(nHeight==6844088){
            return 3125082789;
        }else if(nHeight==6845806){
            return 3122845463;
        }else if(nHeight==6846494){
            return 3121949940;
        }else if(nHeight==6847690){
            return 3120393799;
        }else if(nHeight==6852826){
            return 3113720053;
        }else if(nHeight==6856789){
            return 3108580267;
        }else if(nHeight==6861800){
            return 3102093429;
        }else if(nHeight==6862333){
            return 3101404247;
        }else if(nHeight==6872191){
            return 3088685181;
        }else if(nHeight==6876158){
            return 3083581578;
        }else if(nHeight==6878835){
            return 3080142345;
        }else if(nHeight==6879758){
            return 3078957425;
        }else if(nHeight==6882264){
            return 3075742595;
        }else if(nHeight==6883357){
            return 3074341488;
        }else if(nHeight==6888268){
            return 3068053993;
        }else if(nHeight==6890309){
            return 3065444709;
        }else if(nHeight==6891569){
            return 3063834990;
        }else if(nHeight==6893187){
            return 3061769146;
        }else if(nHeight==6894817){
            return 3059689389;
        }else if(nHeight==6908008){
            return 3042910561;
        }else if(nHeight==6908196){
            return 3042672093;
        }else if(nHeight==6928034){
            return 3017613390;
        }else if(nHeight==6929961){
            return 3015190292;
        }else if(nHeight==6933248){
            return 3011061557;
        }else if(nHeight==6951791){
            return 2987875748;
        }else if(nHeight==6961867){
            return 2975351865;
        }else if(nHeight==6964621){
            return 2971937947;
        }else if(nHeight==6965111){
            return 2971330943;
        }else if(nHeight==6966415){
            return 2969716173;
        }else if(nHeight==6968760){
            return 2966814519;
        }else if(nHeight==6968790){
            return 2966777416;
        }else if(nHeight==6974987){
            return 2959123112;
        }else if(nHeight==6975182){
            return 2958882576;
        }else if(nHeight==6975365){
            return 2958656860;
        }else if(nHeight==6979554){
            return 2953494766;
        }else if(nHeight==6982120){
            return 2950337140;
        }else if(nHeight==6984482){
            return 2947433533;
        }else if(nHeight==6990436){
            return 2940126954;
        }else if(nHeight==6991631){
            return 2938662668;
        }else if(nHeight==6992438){
            return 2937674228;
        }else if(nHeight==6997211){
            return 2931834896;
        }else if(nHeight==6997626){
            return 2931327730;
        }else if(nHeight==7001061){
            return 2927133229;
        }else if(nHeight==7013417){
            return 2912094800;
        }else if(nHeight==7014454){
            return 2910836192;
        }else if(nHeight==7019758){
            return 2904407223;
        }else if(nHeight==7020140){
            return 2903944750;
        }else if(nHeight==7022299){
            return 2901332314;
        }else if(nHeight==7039616){
            return 2880463220;
        }else if(nHeight==7052439){
            return 2865106735;
        }else if(nHeight==7052580){
            return 2864938333;
        }else if(nHeight==7053143){
            return 2864266018;
        }else if(nHeight==7054422){
            return 2862739267;
        }else if(nHeight==7056887){
            return 2859799075;
        }else if(nHeight==7068909){
            return 2845502746;
        }else if(nHeight==7076738){
            return 2836231099;
        }else if(nHeight==7077417){
            return 2835428405;
        }else if(nHeight==7080816){
            return 2831413620;
        }else if(nHeight==7084065){
            return 2827581324;
        }else if(nHeight==7087070){
            return 2824041451;
        }else if(nHeight==7096674){
            return 2812757681;
        }else if(nHeight==7097341){
            return 2811975697;
        }else if(nHeight==7098725){
            return 2810353803;
        }else if(nHeight==7100829){
            return 2807889941;
        }else if(nHeight==7104278){
            return 2803855706;
        }else if(nHeight==7104663){
            return 2803405738;
        }else if(nHeight==7109519){
            return 2797736493;
        }else if(nHeight==7116873){
            return 2789172723;
        }else if(nHeight==7117103){
            return 2788905310;
        }else if(nHeight==7119480){
            return 2786143156;
        }else if(nHeight==7121572){
            return 2783714445;
        }else if(nHeight==7122958){
            return 2782106532;
        }else if(nHeight==7131726){
            return 2771956184;
        }else if(nHeight==7134772){
            return 2768438631;
        }else if(nHeight==7143420){
            return 2758476137;
        }else if(nHeight==7153113){
            return 2747352408;
        }else if(nHeight==7155321){
            return 2744824777;
        }else if(nHeight==7155449){
            return 2744678319;
        }else if(nHeight==7157009){
            return 2742893990;
        }else if(nHeight==7162891){
            return 2736176581;
        }else if(nHeight==7164860){
            return 2733931605;
        }else if(nHeight==7166147){
            return 2732465214;
        }else if(nHeight==7168727){
            return 2729527964;
        }else if(nHeight==7182926){
            return 2713419256;
        }else if(nHeight==7183300){
            return 2712996242;
        }else if(nHeight==7185763){
            return 2710212104;
        }else if(nHeight==7188365){
            return 2707273946;
        }else if(nHeight==7191051){
            return 2704244277;
        }else if(nHeight==7204043){
            return 2689637766;
        }else if(nHeight==7208480){
            return 2684667476;
        }else if(nHeight==7210243){
            return 2682695129;
        }else if(nHeight==7212699){
            return 2679949907;
        }else if(nHeight==7213857){
            return 2678656514;
        }else if(nHeight==7215272){
            return 2677076920;
        }else if(nHeight==7215890){
            return 2676387326;
        }else if(nHeight==7216748){
            return 2675430223;
        }else if(nHeight==7219021){
            return 2672896335;
        }else if(nHeight==7227449){
            return 2663521923;
        }else if(nHeight==7229755){
            return 2660962707;
        }else if(nHeight==7232958){
            return 2657412073;
        }else if(nHeight==7233122){
            return 2657230401;
        }else if(nHeight==7242754){
            return 2646582251;
        }else if(nHeight==7249785){
            return 2638836451;
        }else if(nHeight==7250949){
            return 2637556303;
        }else if(nHeight==7260275){
            return 2627322117;
        }else if(nHeight==7262706){
            return 2624660912;
        }else if(nHeight==7263625){
            return 2623655589;
        }else if(nHeight==7267253){
            return 2619690565;
        }else if(nHeight==7269772){
            return 2616941087;
        }else if(nHeight==7275664){
            return 2610521257;
        }else if(nHeight==7279066){
            return 2606821666;
        }else if(nHeight==7282593){
            return 2602991676;
        }else if(nHeight==7283815){
            return 2601666012;
        }else if(nHeight==7285579){
            return 2599753560;
        }else if(nHeight==7291198){
            return 2593671051;
        }else if(nHeight==7295347){
            return 2589188937;
        }else if(nHeight==7296269){
            return 2588193964;
        }else if(nHeight==7300945){
            return 2583153759;
        }else if(nHeight==7301729){
            return 2582309656;
        }else if(nHeight==7301936){
            return 2582086833;
        }else if(nHeight==7308897){
            return 2574604921;
        }else if(nHeight==7315009){
            return 2568053419;
        }else if(nHeight==7318776){
            return 2564023848;
        }else if(nHeight==7318848){
            return 2563946891;
        }else if(nHeight==7322865){
            return 2559656989;
        }else if(nHeight==7324589){
            return 2557818068;
        }else if(nHeight==7325517){
            return 2556828755;
        }else if(nHeight==7325571){
            return 2556771199;
        }else if(nHeight==7326877){
            return 2555379591;
        }else if(nHeight==7327261){
            return 2554970564;
        }else if(nHeight==7332932){
            return 2548937577;
        }else if(nHeight==7341548){
            return 2539798859;
        }else if(nHeight==7352059){
            return 2528694530;
        }else if(nHeight==7355694){
            return 2524865649;
        }else if(nHeight==7360864){
            return 2519429880;
        }else if(nHeight==7367911){
            return 2512039464;
        }else if(nHeight==7377303){
            return 2502223460;
        }else if(nHeight==7384885){
            return 2494327157;
        }else if(nHeight==7387241){
            return 2491878570;
        }else if(nHeight==7391488){
            return 2487470740;
        }else if(nHeight==7397228){
            return 2481525759;
        }else if(nHeight==7403887){
            return 2474646760;
        }else if(nHeight==7414451){
            return 2463772849;
        }else if(nHeight==7425573){
            return 2452376198;
        }else if(nHeight==7430333){
            return 2447514776;
        }else if(nHeight==7445514){
            return 2432074598;
        }else if(nHeight==7445584){
            return 2432003629;
        }else if(nHeight==7455409){
            return 2422063139;
        }else if(nHeight==7458320){
            return 2419125731;
        }else if(nHeight==7460115){
            return 2417316223;
        }else if(nHeight==7466247){
            return 2411144865;
        }else if(nHeight==7471966){
            return 2405403358;
        }else if(nHeight==7495562){
            return 2381858711;
        }else if(nHeight==7503145){
            return 2374341255;
        }else if(nHeight==7509125){
            return 2368429680;
        }else if(nHeight==7520601){
            return 2357126186;
        }else if(nHeight==7531300){
            return 2346636609;
        }else if(nHeight==7535536){
            return 2342496436;
        }else if(nHeight==7536776){
            return 2341285870;
        }else if(nHeight==7537014){
            return 2341053591;
        }else if(nHeight==7538976){
            return 2339139631;
        }else if(nHeight==7551040){
            return 2327405364;
        }else if(nHeight==7561021){
            return 2317741674;
        }else if(nHeight==7561939){
            return 2316854876;
        }else if(nHeight==7565948){
            return 2312986113;
        }else if(nHeight==7567622){
            return 2311372583;
        }else if(nHeight==7571892){
            return 2307261924;
        }else if(nHeight==7573850){
            return 2305379435;
        }else if(nHeight==7577810){
            return 2301576847;
        }else if(nHeight==7580185){
            return 2299299264;
        }else if(nHeight==7582656){
            return 2296932011;
        }else if(nHeight==7585255){
            return 2294444762;
        }else if(nHeight==7588354){
            return 2291482532;
        }else if(nHeight==7596753){
            return 2283473424;
        }else if(nHeight==7597004){
            return 2283234507;
        }else if(nHeight==7597755){
            return 2282519809;
        }else if(nHeight==7599162){
            return 2281181423;
        }else if(nHeight==7606651){
            return 2274070827;
        }else if(nHeight==7608955){
            return 2271887705;
        }else if(nHeight==7611741){
            return 2269250670;
        }else if(nHeight==7613334){
            return 2267744222;
        }else if(nHeight==7614120){
            return 2267001296;
        }else if(nHeight==7630397){
            return 2251670891;
        }else if(nHeight==7631593){
            return 2250548542;
        }else if(nHeight==7643316){
            return 2239577030;
        }else if(nHeight==7653342){
            return 2230236177;
        }else if(nHeight==7654646){
            return 2229024155;
        }else if(nHeight==7659493){
            return 2224524811;
        }else if(nHeight==7665957){
            return 2218538577;
        }else if(nHeight==7668229){
            return 2216438333;
        }else if(nHeight==7669113){
            return 2215621698;
        }else if(nHeight==7669782){
            return 2215003879;
        }else if(nHeight==7675713){
            return 2209534151;
        }else if(nHeight==7677530){
            return 2207861169;
        }else if(nHeight==7683924){
            return 2201984032;
        }else if(nHeight==7687636){
            return 2198579274;
        }else if(nHeight==7687673){
            return 2198545363;
        }else if(nHeight==7689718){
            return 2196671906;
        }else if(nHeight==7692808){
            return 2193844136;
        }else if(nHeight==7693710){
            return 2193019370;
        }else if(nHeight==7696045){
            return 2190885745;
        }else if(nHeight==7715618){
            return 2173082194;
        }else if(nHeight==7720941){
            return 2168265479;
        }else if(nHeight==7728525){
            return 2161421250;
        }else if(nHeight==7738756){
            return 2152222437;
        }else if(nHeight==7740927){
            return 2150275506;
        }else if(nHeight==7742936){
            return 2148475424;
        }else if(nHeight==7745575){
            return 2146113146;
        }else if(nHeight==7749613){
            return 2142503592;
        }else if(nHeight==7754617){
            return 2138038956;
        }else if(nHeight==7755309){
            return 2137422277;
        }else if(nHeight==7760196){
            return 2133072265;
        }else if(nHeight==7760783){
            return 2132550361;
        }else if(nHeight==7762814){
            return 2130745576;
        }else if(nHeight==7772194){
            return 2122430127;
        }else if(nHeight==7774042){
            return 2120795690;
        }else if(nHeight==7776530){
            return 2118597202;
        }else if(nHeight==7779336){
            return 2116120452;
        }else if(nHeight==7780421){
            return 2115163540;
        }else if(nHeight==7813166){
            return 2086486900;
        }else if(nHeight==7814928){
            return 2084954888;
        }else if(nHeight==7816950){
            return 2083198199;
        }else if(nHeight==7817923){
            return 2082353396;
        }else if(nHeight==7823406){
            return 2077599206;
        }else if(nHeight==7828219){
            return 2073434905;
        }else if(nHeight==7829731){
            return 2072128417;
        }else if(nHeight==7835842){
            return 2066856410;
        }else if(nHeight==7842770){
            return 2060895792;
        }else if(nHeight==7851985){
            return 2052994146;
        }else if(nHeight==7853213){
            return 2051943455;
        }else if(nHeight==7853483){
            return 2051712512;
        }else if(nHeight==7853719){
            return 2051510672;
        }else if(nHeight==7860474){
            return 2045741840;
        }else if(nHeight==7868895){
            return 2038572940;
        }else if(nHeight==7869743){
            return 2037852421;
        }else if(nHeight==7872033){
            return 2035907952;
        }else if(nHeight==7873422){
            return 2034729438;
        }else if(nHeight==7877905){
            return 2030930434;
        }else if(nHeight==7882896){
            return 2026709282;
        }else if(nHeight==7890276){
            return 2020483699;
        }else if(nHeight==7905709){
            return 2007526548;
        }else if(nHeight==7906362){
            return 2006980142;
        }else if(nHeight==7916208){
            return 1998759381;
        }else if(nHeight==7918873){
            return 1996540078;
        }else if(nHeight==7922385){
            return 1993619191;
        }else if(nHeight==7926634){
            return 1990091062;
        }else if(nHeight==7927785){
            return 1989136412;
        }else if(nHeight==7938823){
            return 1980004615;
        }else if(nHeight==7941046){
            return 1978170593;
        }else if(nHeight==7943439){
            return 1976198216;
        }else if(nHeight==7952472){
            return 1968770673;
        }else if(nHeight==7964552){
            return 1958881292;
        }else if(nHeight==7966294){
            return 1957459295;
        }else if(nHeight==7983634){
            return 1943360785;
        }else if(nHeight==7986788){
            return 1940807321;
        }else if(nHeight==7987520){
            return 1940215177;
        }else if(nHeight==7991480){
            return 1937014906;
        }else if(nHeight==8003855){
            return 1927048042;
        }else if(nHeight==8009806){
            return 1922273365;
        }else if(nHeight==8018945){
            return 1914963878;
        }else if(nHeight==8020012){
            return 1914112292;
        }else if(nHeight==8020892){
            return 1913410238;
        }else if(nHeight==8035237){
            return 1902002203;
        }else if(nHeight==8039040){
            return 1898989244;
        }else if(nHeight==8041588){
            return 1896973240;
        }else if(nHeight==8042058){
            return 1896601605;
        }else if(nHeight==8049215){
            return 1890951461;
        }else if(nHeight==8064712){
            return 1878774861;
        }else if(nHeight==8068274){
            return 1875987159;
        }else if(nHeight==8069848){
            return 1874756629;
        }else if(nHeight==8071384){
            return 1873556585;
        }else if(nHeight==8074000){
            return 1871514528;
        }else if(nHeight==8084789){
            return 1863116096;
        }else if(nHeight==8090986){
            return 1858309245;
        }else if(nHeight==8091530){
            return 1857887871;
        }else if(nHeight==8096500){
            return 1854042607;
        }else if(nHeight==8097753){
            return 1853074424;
        }else if(nHeight==8104716){
            return 1847703374;
        }else if(nHeight==8121471){
            return 1834842782;
        }else if(nHeight==8123540){
            return 1833260907;
        }else if(nHeight==8123834){
            return 1833036237;
        }else if(nHeight==8126263){
            return 1831181088;
        }else if(nHeight==8138222){
            return 1822074742;
        }else if(nHeight==8142359){
            return 1818935120;
        }else if(nHeight==8146170){
            return 1816047691;
        }else if(nHeight==8147285){
            return 1815203771;
        }else if(nHeight==8150766){
            return 1812571598;
        }else if(nHeight==8151428){
            return 1812071456;
        }else if(nHeight==8152134){
            return 1811538224;
        }else if(nHeight==8153924){
            return 1810186965;
        }else if(nHeight==8164064){
            return 1802551352;
        }else if(nHeight==8165858){
            return 1801203793;
        }else if(nHeight==8169258){
            return 1798652654;
        }else if(nHeight==8186352){
            return 1785881074;
        }else if(nHeight==8192843){
            return 1781055191;
        }else if(nHeight==8197836){
            return 1777351906;
        }else if(nHeight==8203136){
            return 1773429345;
        }else if(nHeight==8209657){
            return 1768614991;
        }else if(nHeight==8211461){
            return 1767285435;
        }else if(nHeight==8214717){
            return 1764888278;
        }else if(nHeight==8219388){
            return 1761455036;
        }else if(nHeight==8227361){
            return 1755610206;
        }else if(nHeight==8227968){
            return 1755166023;
        }else if(nHeight==8232658){
            return 1751737819;
        }else if(nHeight==8236337){
            return 1749053303;
        }else if(nHeight==8239533){
            return 1746724565;
        }else if(nHeight==8240249){
            return 1746203283;
        }else if(nHeight==8240880){
            return 1745744014;
        }else if(nHeight==8251406){
            return 1738100527;
        }else if(nHeight==8254721){
            return 1735700266;
        }else if(nHeight==8254941){
            return 1735541090;
        }else if(nHeight==8255210){
            return 1735346481;
        }else if(nHeight==8260219){
            return 1731726686;
        }else if(nHeight==8271226){
            return 1723798891;
        }else if(nHeight==8274598){
            return 1721377477;
        }else if(nHeight==8276134){
            return 1720275612;
        }else if(nHeight==8278519){
            return 1718566106;
        }else if(nHeight==8302302){
            return 1701611728;
        }else if(nHeight==8308488){
            return 1697229342;
        }else if(nHeight==8313714){
            return 1693535851;
        }else if(nHeight==8333024){
            return 1679958034;
        }else if(nHeight==8334867){
            return 1678667834;
        }else if(nHeight==8336311){
            return 1677657648;
        }else if(nHeight==8338048){
            return 1676443292;
        }else if(nHeight==8340342){
            return 1674840878;
        }else if(nHeight==8340775){
            return 1674538589;
        }else if(nHeight==8344747){
            return 1671768173;
        }else if(nHeight==8354434){
            return 1665030827;
        }else if(nHeight==8359469){
            return 1661539696;
        }else if(nHeight==8365110){
            return 1657637075;
        }else if(nHeight==8371412){
            return 1653287993;
        }else if(nHeight==8374222){
            return 1651352460;
        }else if(nHeight==8384893){
            return 1644022875;
        }else if(nHeight==8386208){
            return 1643121896;
        }else if(nHeight==8395308){
            return 1636900501;
        }else if(nHeight==8397614){
            return 1635327703;
        }else if(nHeight==8400883){
            return 1633100684;
        }else if(nHeight==8405787){
            return 1629765501;
        }else if(nHeight==8418224){
            return 1621337676;
        }else if(nHeight==8420701){
            return 1619664370;
        }else if(nHeight==8425198){
            return 1616630891;
        }else if(nHeight==8428810){
            return 1614198509;
        }else if(nHeight==8431643){
            return 1612293280;
        }else if(nHeight==8442779){
            return 1604825944;
        }else if(nHeight==8470655){
            return 1586284757;
        }else if(nHeight==8474430){
            return 1583790412;
        }else if(nHeight==8480265){
            return 1579942629;
        }else if(nHeight==8484113){
            return 1577410253;
        }else if(nHeight==8494260){
            return 1570751940;
        }else if(nHeight==8501196){
            return 1566216817;
        }else if(nHeight==8518839){
            return 1554739819;
        }else if(nHeight==8527953){
            return 1548844028;
        }else if(nHeight==8532400){
            return 1545975412;
        }else if(nHeight==8534565){
            return 1544580764;
        }else if(nHeight==8540598){
            return 1540701065;
        }else if(nHeight==8547891){
            return 1536024094;
        }else if(nHeight==8549943){
            return 1534710716;
        }else if(nHeight==8555704){
            return 1531029400;
        }else if(nHeight==8570031){
            return 1521912596;
        }else if(nHeight==8575510){
            return 1518440472;
        }else if(nHeight==8582300){
            return 1514148537;
        }else if(nHeight==8582526){
            return 1514005892;
        }else if(nHeight==8582842){
            return 1513806464;
        }else if(nHeight==8591869){
            return 1508120593;
        }else if(nHeight==8592992){
            return 1507414740;
        }else if(nHeight==8611412){
            return 1495884028;
        }else if(nHeight==8611738){
            return 1495680752;
        }else if(nHeight==8622848){
            return 1488769643;
        }else if(nHeight==8625151){
            return 1487341035;
        }else if(nHeight==8626209){
            return 1486685191;
        }else if(nHeight==8637688){
            return 1479588033;
        }else if(nHeight==8641175){
            return 1477438831;
        }else if(nHeight==8645951){
            return 1474500223;
        }else if(nHeight==8664752){
            return 1462988904;
        }else if(nHeight==8664798){
            return 1462960850;
        }else if(nHeight==8667689){
            return 1461198796;
        }else if(nHeight==8675855){
            return 1456233104;
        }else if(nHeight==8689087){
            return 1448222611;
        }else if(nHeight==8696272){
            return 1443891374;
        }else if(nHeight==8714645){
            return 1432874647;
        }else if(nHeight==8727157){
            return 1425420415;
        }else if(nHeight==8746405){
            return 1414028743;
        }else if(nHeight==8748702){
            return 1412675389;
        }else if(nHeight==8752280){
            return 1410569871;
        }else if(nHeight==8759800){
            return 1406154858;
        }else if(nHeight==8760181){
            return 1405931540;
        }else if(nHeight==8765297){
            return 1402936299;
        }else if(nHeight==8798755){
            return 1383504507;
        }else if(nHeight==8800117){
            return 1382719209;
        }else if(nHeight==8804426){
            return 1380237673;
        }else if(nHeight==8806655){
            return 1378955749;
        }else if(nHeight==8808489){
            return 1377901887;
        }else if(nHeight==8833184){
            return 1363789714;
        }else if(nHeight==8834453){
            return 1363068450;
        }else if(nHeight==8836980){
            return 1361633310;
        }else if(nHeight==8843255){
            return 1358076128;
        }else if(nHeight==8844580){
            return 1357326199;
        }else if(nHeight==8845343){
            return 1356894541;
        }else if(nHeight==8858337){
            return 1349564383;
        }else if(nHeight==8859886){
            return 1348693209;
        }else if(nHeight==8862529){
            return 1347208056;
        }else if(nHeight==8868082){
            return 1344093043;
        }else if(nHeight==8884156){
            return 1335116706;
        }else if(nHeight==8899572){
            return 1326564143;
        }else if(nHeight==8901305){
            return 1325606133;
        }else if(nHeight==8903313){
            return 1324496967;
        }else if(nHeight==8908058){
            return 1321879641;
        }else if(nHeight==8908614){
            return 1321573292;
        }else if(nHeight==8914199){
            return 1318499963;
        }else if(nHeight==8916169){
            return 1317417612;
        }else if(nHeight==8929774){
            return 1309967014;
        }else if(nHeight==8935838){
            return 1306659735;
        }else if(nHeight==8947000){
            return 1300593844;
        }else if(nHeight==8951961){
            return 1297906879;
        }else if(nHeight==8957717){
            return 1294796284;
        }else if(nHeight==8963098){
            return 1291895085;
        }else if(nHeight==8963441){
            return 1291710375;
        }else if(nHeight==8965168){
            return 1290780764;
        }else if(nHeight==8973698){
            return 1286199032;
        }else if(nHeight==8974110){
            return 1285978146;
        }else if(nHeight==8975154){
            return 1285418595;
        }else if(nHeight==8977960){
            return 1283915874;
        }else if(nHeight==8990149){
            return 1277408559;
        }else if(nHeight==8990472){
            return 1277236569;
        }else if(nHeight==9005785){
            return 1269109269;
        }else if(nHeight==9010660){
            return 1266532755;
        }else if(nHeight==9013703){
            return 1264927134;
        }else if(nHeight==9014444){
            return 1264536458;
        }else if(nHeight==9018186){
            return 1262565413;
        }else if(nHeight==9023130){
            return 1259965942;
        }else if(nHeight==9044813){
            return 1248628451;
        }else if(nHeight==9047007){
            return 1247486963;
        }else if(nHeight==9082866){
            return 1228977565;
        }else if(nHeight==9084898){
            return 1227936965;
        }else if(nHeight==9095757){
            return 1222390920;
        }else if(nHeight==9096263){
            return 1222133101;
        }else if(nHeight==9107906){
            return 1216215714;
        }else if(nHeight==9111353){
            return 1214469329;
        }else if(nHeight==9116982){
            return 1211622847;
        }else if(nHeight==9122718){
            return 1208729120;
        }else if(nHeight==9129003){
            return 1205566365;
        }else if(nHeight==9133229){
            return 1203444400;
        }else if(nHeight==9133389){
            return 1203364134;
        }else if(nHeight==9136599){
            return 1201754928;
        }else if(nHeight==9138269){
            return 1200918591;
        }else if(nHeight==9142262){
            return 1198921253;
        }else if(nHeight==9143177){
            return 1198464029;
        }else if(nHeight==9143666){
            return 1198219748;
        }else if(nHeight==9151666){
            return 1194230394;
        }else if(nHeight==9159185){
            return 1190493009;
        }else if(nHeight==9166839){
            return 1186700534;
        }else if(nHeight==9176207){
            return 1182075229;
        }else if(nHeight==9184647){
            return 1177923549;
        }else if(nHeight==9186953){
            return 1176791754;
        }else if(nHeight==9189224){
            return 1175678200;
        }else if(nHeight==9195177){
            return 1172764226;
        }else if(nHeight==9196868){
            return 1171937805;
        }else if(nHeight==9200580){
            return 1170125728;
        }else if(nHeight==9211381){
            return 1164868955;
        }else if(nHeight==9212724){
            return 1164216979;
        }else if(nHeight==9214180){
            return 1163510558;
        }else if(nHeight==9228210){
            return 1156725420;
        }else if(nHeight==9230919){
            return 1155419868;
        }else if(nHeight==9244505){
            return 1148894541;
        }else if(nHeight==9246312){
            return 1148029424;
        }else if(nHeight==9247589){
            return 1147418442;
        }else if(nHeight==9254040){
            return 1144336921;
        }else if(nHeight==9257955){
            return 1142470836;
        }else if(nHeight==9261815){
            return 1140633946;
        }else if(nHeight==9264852){
            return 1139190780;
        }else if(nHeight==9267339){
            return 1138010331;
        }else if(nHeight==9268301){
            return 1137554048;
        }else if(nHeight==9269066){
            return 1137191334;
        }else if(nHeight==9271135){
            return 1136210926;
        }else if(nHeight==9305560){
            return 1120021908;
        }else if(nHeight==9326492){
            return 1110291212;
        }else if(nHeight==9328795){
            return 1109225788;
        }else if(nHeight==9332865){
            return 1107345406;
        }else if(nHeight==9382997){
            return 1084443687;
        }else if(nHeight==9405550){
            return 1074295897;
        }else if(nHeight==9420489){
            return 1067626387;
        }else if(nHeight==9424739){
            return 1065736553;
        }else if(nHeight==9429006){
            return 1063842525;
        }else if(nHeight==9437264){
            return 1060186535;
        }else if(nHeight==9438347){
            return 1059708001;
        }else if(nHeight==9440714){
            return 1058662871;
        }else if(nHeight==9451071){
            return 1054101929;
        }else if(nHeight==9453541){
            return 1053017113;
        }else if(nHeight==9459879){
            return 1050238589;
        }else if(nHeight==9467017){
            return 1047118129;
        }else if(nHeight==9475411){
            return 1043460454;
        }else if(nHeight==9481268){
            return 1040915844;
        }else if(nHeight==9486576){
            return 1038615111;
        }else if(nHeight==9487979){
            return 1038007836;
        }else if(nHeight==9488812){
            return 1037647448;
        }else if(nHeight==9489985){
            return 1037140175;
        }else if(nHeight==9496777){
            return 1034207793;
        }else if(nHeight==9497729){
            return 1033797439;
        }else if(nHeight==9498265){
            return 1033566471;
        }else if(nHeight==9501265){
            return 1032274692;
        }else if(nHeight==9506553){
            return 1030001646;
        }else if(nHeight==9512192){
            return 1027583237;
        }else if(nHeight==9519678){
            return 1024381472;
        }else if(nHeight==9536829){
            return 1017083543;
        }else if(nHeight==9546556){
            return 1012967732;
        }else if(nHeight==9550766){
            return 1011191512;
        }else if(nHeight==9553616){
            return 1009990851;
        }else if(nHeight==9567547){
            return 1004142420;
        }else if(nHeight==9571312){
            return 1002567641;
        }else if(nHeight==9574322){
            return 1001310431;
        }else if(nHeight==9578570){
            return 999538818;
        }else if(nHeight==9580682){
            return 998659183;
        }else if(nHeight==9581175){
            return 998453963;
        }else if(nHeight==9583303){
            return 997568629;
        }else if(nHeight==9585786){
            return 996536593;
        }else if(nHeight==9591973){
            return 993969666;
        }else if(nHeight==9597185){
            return 991812389;
        }else if(nHeight==9601006){
            return 990233830;
        }else if(nHeight==9609980){
            return 986536299;
        }else if(nHeight==9623149){
            return 981135296;
        }else if(nHeight==9631395){
            return 977768428;
        }else if(nHeight==9637009){
            return 975482827;
        }else if(nHeight==9646375){
            return 971681578;
        }else if(nHeight==9677052){
            return 959334535;
        }else if(nHeight==9677643){
            return 959098213;
        }else if(nHeight==9710735){
            return 945958252;
        }else if(nHeight==9714185){
            return 944598753;
        }else if(nHeight==9714492){
            return 944477872;
        }else if(nHeight==9719638){
            return 942453940;
        }else if(nHeight==9731964){
            return 937623718;
        }else if(nHeight==9734273){
            return 936721641;
        }else if(nHeight==9734919){
            return 936469418;
        }else if(nHeight==9763710){
            return 925297009;
        }else if(nHeight==9773974){
            return 921346354;
        }else if(nHeight==9794562){
            return 913472727;
        }else if(nHeight==9797119){
            return 912499543;
        }else if(nHeight==9828052){
            return 900808381;
        }else if(nHeight==9828081){
            return 900797491;
        }else if(nHeight==9829995){
            return 900079042;
        }else if(nHeight==9832501){
            return 899139243;
        }else if(nHeight==9844654){
            return 894595530;
        }else if(nHeight==9856098){
            return 890337888;
        }else if(nHeight==9867035){
            return 886287809;
        }else if(nHeight==9867405){
            return 886151117;
        }else if(nHeight==9872618){
            return 884227477;
        }else if(nHeight==9891286){
            return 877373019;
        }else if(nHeight==9896373){
            return 875514420;
        }else if(nHeight==9932200){
            return 862535614;
        }else if(nHeight==9937955){
            return 860468799;
        }else if(nHeight==9948829){
            return 856577088;
        }else if(nHeight==9965954){
            return 850483854;
        }else if(nHeight==9967765){
            return 849842023;
        }else if(nHeight==10020472){
            return 831372989;
        }else if(nHeight==10041096){
            return 824255885;
        }else if(nHeight==10048878){
            return 821586267;
        }else if(nHeight==10053935){
            return 819856098;
        }else if(nHeight==10060130){
            return 817741546;
        }else if(nHeight==10068289){
            return 814964937;
        }else if(nHeight==10078053){
            return 811654516;
        }else if(nHeight==10100278){
            return 804169327;
        }else if(nHeight==10116015){
            return 798911022;
        }else if(nHeight==10123611){
            return 796385239;
        }else if(nHeight==10130803){
            return 794001152;
        }else if(nHeight==10140315){
            return 790858963;
        }else if(nHeight==10147183){
            return 788597925;
        }else if(nHeight==10180461){
            return 777733568;
        }else if(nHeight==10183951){
            return 776602886;
        }else if(nHeight==10191338){
            return 774215082;
        }else if(nHeight==10200313){
            return 771323843;
        }else if(nHeight==10216772){
            return 766049708;
        }else if(nHeight==10225436){
            return 763287913;
        }else if(nHeight==10229088){
            return 762126762;
        }else if(nHeight==10231041){
            return 761506532;
        }else if(nHeight==10237061){
            return 759597886;
        }else if(nHeight==10237964){
            return 759312002;
        }else if(nHeight==10260383){
            return 752248684;
        }else if(nHeight==10267336){
            return 750071453;
        }else if(nHeight==10318121){
            return 734358814;
        }else if(nHeight==10319129){
            return 734050298;
        }else if(nHeight==10328494){
            return 731190165;
        }else if(nHeight==10328515){
            return 731183764;
        }else if(nHeight==10347356){
            return 725463367;
        }else if(nHeight==10352940){
            return 723776598;
        }else if(nHeight==10360720){
            return 721433016;
        }else if(nHeight==10378208){
            return 716192740;
        }else if(nHeight==10380456){
            return 715521894;
        }else if(nHeight==10384321){
            return 714369973;
        }else if(nHeight==10387049){
            return 713558039;
        }else if(nHeight==10394891){
            return 711229162;
        }else if(nHeight==10411470){
            return 706330606;
        }else if(nHeight==10421601){
            return 703353847;
        }else if(nHeight==10425862){
            return 702105602;
        }else if(nHeight==10426472){
            return 701927086;
        }else if(nHeight==10443312){
            return 697016756;
        }else if(nHeight==10444661){
            return 696624894;
        }else if(nHeight==10445921){
            return 696259084;
        }else if(nHeight==10491901){
            return 683040517;
        }else if(nHeight==10492857){
            return 682768361;
        }else if(nHeight==10518890){
            return 675398772;
        }else if(nHeight==10520238){
            return 675019345;
        }else if(nHeight==10527279){
            return 673040948;
        }else if(nHeight==10533876){
            return 671192569;
        }else if(nHeight==10546442){
            return 667685799;
        }else if(nHeight==10547540){
            return 667380254;
        }else if(nHeight==10547959){
            return 667263694;
        }else if(nHeight==10557317){
            return 664665726;
        }else if(nHeight==10588549){
            return 656068090;
        }else if(nHeight==10612893){
            return 649443803;
        }else if(nHeight==10635053){
            return 643471979;
        }else if(nHeight==10644217){
            return 641018482;
        }else if(nHeight==10649171){
            return 639696035;
        }else if(nHeight==10653275){
            return 638602558;
        }else if(nHeight==10669268){
            return 634359160;
        }else if(nHeight==10691602){
            return 628480455;
        }else if(nHeight==10709471){
            return 623816272;
        }else if(nHeight==10720063){
            return 621067895;
        }else if(nHeight==10769940){
            return 608287851;
        }else if(nHeight==10774335){
            return 607174402;
        }else if(nHeight==10778007){
            return 606245684;
        }else if(nHeight==10793844){
            return 602256456;
        }else if(nHeight==10795413){
            return 601862668;
        }else if(nHeight==10801507){
            return 600335635;
        }else if(nHeight==10806420){
            return 599107358;
        }else if(nHeight==10819494){
            return 595851014;
        }else if(nHeight==10833296){
            return 592432550;
        }else if(nHeight==10834102){
            return 592233528;
        }else if(nHeight==10867833){
            return 583964157;
        }else if(nHeight==10879367){
            return 581163096;
        }else if(nHeight==10914698){
            return 572666211;
        }else if(nHeight==10922431){
            return 570823107;
        }else if(nHeight==10969638){
            return 559699599;
        }else if(nHeight==10977266){
            return 557922648;
        }else if(nHeight==11052469){
            return 540703189;
        }else if(nHeight==11133783){
            return 522681959;
        }else if(nHeight==11153321){
            return 518442111;
        }else if(nHeight==11166796){
            return 515538022;
        }else if(nHeight==11195332){
            return 509441624;
        }else if(nHeight==11230896){
            return 501944585;
        }else if(nHeight==11239245){
            return 500200632;
        }else if(nHeight==11269764){
            return 493877170;
        }else if(nHeight==11273232){
            return 493163686;
        }else if(nHeight==11280857){
            return 491598590;
        }else if(nHeight==11287522){
            return 490234611;
        }else if(nHeight==11293263){
            return 489062761;
        }else if(nHeight==11316044){
            return 484440261;
        }else if(nHeight==11361321){
            return 475382396;
        }else if(nHeight==11367478){
            return 474163813;
        }else if(nHeight==11374290){
            return 472819233;
        }else if(nHeight==11376282){
            return 472426765;
        }else if(nHeight==11392041){
            return 469333355;
        }else if(nHeight==11415633){
            return 464740186;
        }else if(nHeight==11429085){
            return 462141343;
        }else if(nHeight==11454756){
            return 457222126;
        }else if(nHeight==11477800){
            return 452850924;
        }else if(nHeight==11498536){
            return 448953258;
        }else if(nHeight==11516531){
            return 445598006;
        }else if(nHeight==11519087){
            return 445123466;
        }else if(nHeight==11530667){
            return 442979878;
        }else if(nHeight==11561291){
            return 437360648;
        }else if(nHeight==11570157){
            return 435747162;
        }else if(nHeight==11571608){
            return 435483668;
        }else if(nHeight==11603835){
            return 429672307;
        }else if(nHeight==11649661){
            return 421541985;
        }else if(nHeight==11650212){
            return 421445170;
        }else if(nHeight==11700252){
            return 412744822;
        }else if(nHeight==11706504){
            return 411670499;
        }else if(nHeight==11707552){
            return 411490688;
        }else if(nHeight==11720224){
            return 409322691;
        }else if(nHeight==11743152){
            return 405429029;
        }else if(nHeight==11756463){
            return 403185554;
        }else if(nHeight==11771091){
            return 400734423;
        }else if(nHeight==11801692){
            return 395654873;
        }else if(nHeight==11820509){
            return 392563405;
        }else if(nHeight==11837750){
            return 389752070;
        }else if(nHeight==11903065){
            return 379283137;
        }else if(nHeight==11903338){
            return 379239975;
        }else if(nHeight==11909989){
            return 378189951;
        }else if(nHeight==11939652){
            return 373542201;
        }else if(nHeight==11989152){
            return 365913127;
        }else if(nHeight==11992289){
            return 365434928;
        }else if(nHeight==12010035){
            return 362741501;
        }else if(nHeight==12031855){
            return 359456932;
        }else if(nHeight==12043489){
            return 357717837;
        }else if(nHeight==12044463){
            return 357572622;
        }else if(nHeight==12060963){
            return 355121553;
        }else if(nHeight==12073279){
            return 353302973;
        }else if(nHeight==12101063){
            return 349234518;
        }else if(nHeight==12135416){
            return 344268870;
        }else if(nHeight==12184259){
            return 337330039;
        }else if(nHeight==12185172){
            return 337201675;
        }else if(nHeight==12210812){
            return 333616677;
        }else if(nHeight==12231311){
            return 330777933;
        }else if(nHeight==12242361){
            return 329257739;
        }else if(nHeight==12275068){
            return 324798924;
        }else if(nHeight==12388697){
            return 309772400;
        }else if(nHeight==12469160){
            return 299554169;
        }else if(nHeight==12494412){
            return 296417365;
        }else if(nHeight==12503369){
            return 295312636;
        }else if(nHeight==12507264){
            return 294833524;
        }else if(nHeight==12535434){
            return 291391481;
        }else if(nHeight==12562908){
            return 288073192;
        }else if(nHeight==12572287){
            return 286949077;
        }else if(nHeight==12596898){
            return 284020152;
        }else if(nHeight==12597065){
            return 284000380;
        }else if(nHeight==12654637){
            return 277265510;
        }else if(nHeight==12655385){
            return 277179067;
        }else if(nHeight==12662067){
            return 276408053;
        }else if(nHeight==12694318){
            return 272716766;
        }else if(nHeight==12694897){
            return 272650949;
        }else if(nHeight==12705975){
            return 271394728;
        }else if(nHeight==12717466){
            return 270097789;
        }else if(nHeight==12735180){
            return 268110618;
        }else if(nHeight==12745534){
            return 266955875;
        }else if(nHeight==12765774){
            return 264712927;
        }else if(nHeight==12772098){
            return 264015988;
        }else if(nHeight==12837911){
            return 256871056;
        }else if(nHeight==12847025){
            return 255896965;
        }else if(nHeight==12870909){
            return 253361766;
        }else if(nHeight==12901962){
            return 250103123;
        }else if(nHeight==12909671){
            return 249300670;
        }else if(nHeight==12917036){
            return 248536430;
        }else if(nHeight==12920225){
            return 248206246;
        }else if(nHeight==12939075){
            return 246263487;
        }else if(nHeight==12949850){
            return 245159809;
        }else if(nHeight==13040125){
            return 236105181;
        }else if(nHeight==13043250){
            return 235797803;
        }else if(nHeight==13081544){
            return 232063507;
        }else if(nHeight==13095036){
            return 230761952;
        }else if(nHeight==13132513){
            return 227184770;
        }else if(nHeight==13137003){
            return 226759936;
        }else if(nHeight==13155155){
            return 225050516;
        }else if(nHeight==13211556){
            return 219820881;
        }else if(nHeight==13222464){
            return 218823578;
        }else if(nHeight==13239232){
            return 217299320;
        }else if(nHeight==13248419){
            return 216468703;
        }else if(nHeight==13296450){
            return 212177528;
        }else if(nHeight==13333403){
            return 208934069;
        }else if(nHeight==13349018){
            return 207578447;
        }else if(nHeight==13393847){
            return 203735276;
        }else if(nHeight==13411247){
            return 202262823;
        }else if(nHeight==13417391){
            return 201745441;
        }else if(nHeight==13498750){
            return 195017759;
        }else if(nHeight==13502539){
            return 194709968;
        }else if(nHeight==13518210){
            return 193442120;
        }else if(nHeight==13562448){
            return 189907455;
        }else if(nHeight==13565627){
            return 189655951;
        }else if(nHeight==13625573){
            return 184975242;
        }else if(nHeight==13657206){
            return 182552017;
        }else if(nHeight==13677836){
            return 180988798;
        }else if(nHeight==13722073){
            return 177681760;
        }else if(nHeight==13722446){
            return 177654134;
        }else if(nHeight==13765566){
            return 174489258;
        }else if(nHeight==13776571){
            return 173690596;
        }else if(nHeight==13794118){
            return 172424718;
        }else if(nHeight==13797643){
            return 172171532;
        }else if(nHeight==13807240){
            return 171484102;
        }else if(nHeight==13809891){
            return 171294696;
        }else if(nHeight==13827388){
            return 170049824;
        }else if(nHeight==13891849){
            return 165541125;
        }else if(nHeight==13914018){
            return 164018311;
        }else if(nHeight==13960772){
            return 160852494;
        }else if(nHeight==13975028){
            return 159899400;
        }else if(nHeight==13987610){
            return 159062915;
        }else if(nHeight==14038635){
            return 155715255;
        }else if(nHeight==14050611){
            return 154939795;
        }else if(nHeight==14082561){
            return 152889837;
        }else if(nHeight==14128769){
            return 149972949;
        }else if(nHeight==14224500){
            return 144105779;
        }else if(nHeight==14248266){
            return 142685126;
        }else if(nHeight==14265441){
            return 141667187;
        }else if(nHeight==14285177){
            return 140506425;
        }else if(nHeight==14308211){
            return 139163715;
        }else if(nHeight==14319783){
            return 138494004;
        }else if(nHeight==14321008){
            return 138423298;
        }else if(nHeight==14326374){
            return 138114002;
        }else if(nHeight==14371111){
            return 135562116;
        }else if(nHeight==14511218){
            return 127871215;
        }else if(nHeight==14513628){
            return 127742813;
        }else if(nHeight==14575788){
            return 124475183;
        }else if(nHeight==14577940){
            return 124363566;
        }else if(nHeight==14596893){
            return 123384850;
        }else if(nHeight==14611279){
            return 122647115;
        }else if(nHeight==14612093){
            return 122605504;
        }else if(nHeight==14849243){
            return 111064523;
        }else if(nHeight==14864132){
            return 110377306;
        }else if(nHeight==14935738){
            return 107131194;
        }else if(nHeight==15050681){
            return 102118915;
        }else if(nHeight==15057282){
            return 101838295;
        }else if(nHeight==15097016){
            return 100165350;
        }else if(nHeight==15168953){
            return 97206150;
        }else if(nHeight==15257267){
            return 93692540;
        }else if(nHeight==15323997){
            return 91122146;
        }else if(nHeight==15336937){
            return 90631930;
        }else if(nHeight==15360423){
            return 89748919;
        }else if(nHeight==15452085){
            return 86384209;
        }else if(nHeight==15479011){
            return 85420000;
        }else if(nHeight==15490921){
            return 84996948;
        }else if(nHeight==15529046){
            return 83656757;
        }else if(nHeight==15585002){
            return 81727934;
        }else if(nHeight==15602538){
            return 81132663;
        }else if(nHeight==15794188){
            return 74902899;
        }else if(nHeight==15987728){
            return 69097025;
        }else if(nHeight==15994149){
            return 68912319;
        }else if(nHeight==16061053){
            return 67016893;
        }else if(nHeight==16180368){
            return 63765095;
        }else if(nHeight==16244185){
            return 62091095;
        }else if(nHeight==16317256){
            return 60228250;
        }else if(nHeight==16394792){
            return 58312654;
        }else if(nHeight==16425515){
            return 57570578;
        }else if(nHeight==16479518){
            return 56289017;
        }else if(nHeight==16537804){
            return 54937808;
        }else if(nHeight==16571833){
            return 54163981;
        }else if(nHeight==16648663){
            return 52456699;
        }else if(nHeight==16703241){
            return 51276684;
        }else if(nHeight==16862259){
            return 47987780;
        }else if(nHeight==16883817){
            return 47558452;
        }else if(nHeight==17049372){
            return 44386914;
        }else if(nHeight==17320113){
            return 39649607;
        }else if(nHeight==17362072){
            return 38962109;
        }else if(nHeight==17495132){
            return 36859779;
        }else if(nHeight==17597216){
            return 35324093;
        }else if(nHeight==17766795){
            return 32913172;
        }else if(nHeight==18006400){
            return 29784524;
        }else if(nHeight==18260769){
            return 26787899;
        }else if(nHeight==18260937){
            return 26786023;
        }else if(nHeight==18441021){
            return 24848781;
        }else if(nHeight==18504846){
            return 24196355;
        }else if(nHeight==18550382){
            return 23741378;
        }else if(nHeight==18693580){
            return 22365611;
        }else if(nHeight==18871014){
            return 20770999;
        }else if(nHeight==19118976){
            return 18731188;
        }else if(nHeight==19465539){
            return 16211460;
        }else if(nHeight==19482328){
            return 16098395;
        }else if(nHeight==19861632){
            return 13743958;
        }else if(nHeight==20797544){
            return 9304016;
        }else if(nHeight==20873569){
            return 9013772;
        }else if(nHeight==21226337){
            return 7781082;
        }else if(nHeight==21911847){
            return 5846991;
        }else if(nHeight==25932669){
            return 1093921;
        }else{
            long double br=54193019856*powl(1-0.00000041686938347033551682078457954749861613663597381673753261566162109375,nHeight);
            CAmount nSubsidy = floor (br);
            return nSubsidy;
        }
    }else{
        CAmount nSubsidy = 54193019856*pow(1-0.00000041686938347033551682078457954749861613663597381673753261566162109375,nHeight);
        return nSubsidy;
    }
}

bool IsInitialBlockDownload()
{
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
    {
//        LogPrintf("IsInitialBlockDownload (importing or reindex)\n");
        return true;
    }
    if (chainActive.Tip() == nullptr)
    {
//        LogPrintf("IsInitialBlockDownload (tip is null)");
        return true;
    }
    if (chainActive.Tip()->nChainWork < nMinimumChainWork)
    {
//    		LogPrintf("IsInitialBlockDownload (min chain work)");
//    		LogPrintf("Work found: %s", chainActive.Tip()->nChainWork.GetHex());
//    		LogPrintf("Work needed: %s", nMinimumChainWork.GetHex());
        return true;
    }
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
    {
//        LogPrintf("%s: (tip age): %d\n", __func__, nMaxTipAge);
        return true;
    }
//    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

bool IsInitialSyncSpeedUp()
{
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> syncLatchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (syncLatchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (syncLatchToFalse.load(std::memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
    {
//        LogPrintf("IsInitialBlockDownload (importing or reindex)\n");
        return true;
    }
    if (chainActive.Tip() == nullptr)
    {
//        LogPrintf("IsInitialBlockDownload (tip is null)");
        return true;
    }
    if (chainActive.Tip()->nChainWork < nMinimumChainWork)
    {
//    		LogPrintf("IsInitialBlockDownload (min chain work)");
//    		LogPrintf("Work found: %s", chainActive.Tip()->nChainWork.GetHex());
//    		LogPrintf("Work needed: %s", nMinimumChainWork.GetHex());
        return true;
    }
    if (chainActive.Tip()->GetBlockTime() < (GetTime() - (60 * 60 * 72))) // 3 Days
    {
//        LogPrintf("%s: (tip age): %d\n", __func__, nMaxTipAge);
        return true;
    }
//    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    syncLatchToFalse.store(true, std::memory_order_relaxed);
    return false;
}

CBlockIndex *pindexBestForkTip = nullptr, *pindexBestForkBase = nullptr;

static void AlertNotify(const std::string& strMessage)
{
    uiInterface.NotifyAlertChanged();
    std::string strCmd = gArgs.GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    boost::thread t(runCommand, strCmd); // thread runs free
}

static void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 72 blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = nullptr;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6)))
    {
        if (!GetfLargeWorkForkFound() && pindexBestForkBase)
        {
            std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                pindexBestForkBase->phashBlock->ToString() + std::string("'");
            AlertNotify(warning);
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                   pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                   pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
            SetfLargeWorkForkFound(true);
        }
        else
        {
            LogPrintf("%s: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n", __func__);
            SetfLargeWorkInvalidChainFound(true);
        }
    }
    else
    {
        SetfLargeWorkForkFound(false);
        SetfLargeWorkInvalidChainFound(false);
    }
}

static void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 12 hours if no one mines it) of ours
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || pindexNewForkTip->nHeight > pindexBestForkTip->nHeight) &&
            pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
            chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
      log(pindexNew->nChainWork.getdouble())/log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
      pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
      tip->GetBlockHash().ToString(), chainActive.Height(), log(tip->nChainWork.getdouble())/log(2.0),
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state) {
    if (!state.CorruptionPossible()) {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        g_failed_blocks.insert(pindex);
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight, uint256 blockHash, CAssetsCache* assetCache, std::pair<std::string, CBlockAssetUndo>* undoAssetData)
{
    // mark inputs spent
    if (!tx.IsCoinBase()) {
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn &txin : tx.vin) {
            txundo.vprevout.emplace_back();
            bool is_spent = inputs.SpendCoin(txin.prevout, &txundo.vprevout.back(), assetCache); /** CLORE START */ /* Pass assetCache into function */ /** CLORE END */
            assert(is_spent);
        }
    }
    // add outputs
    AddCoins(inputs, tx, nHeight, blockHash, false, assetCache, undoAssetData); /** CLORE START */ /* Pass assetCache into function */ /** CLORE END */
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight, uint256());
}

bool CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    const CScriptWitness *witness = &ptxTo->vin[nIn].scriptWitness;
    return VerifyScript(scriptSig, m_tx_out.scriptPubKey, witness, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, m_tx_out.nValue, cacheStore, *txdata), &error);
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    LOCK(cs_main);
    CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return pindexPrev->nHeight + 1;
}


static CuckooCache::cache<uint256, SignatureCacheHasher> scriptExecutionCache;
static uint256 scriptExecutionCacheNonce(GetRandHash());

void InitScriptExecutionCache() {
    // nMaxCacheSize is unsigned. If -maxsigcachesize is set to zero,
    // setup_bytes creates the minimum possible cache (2 elements).
    size_t nMaxCacheSize = std::min(std::max((int64_t)0, gArgs.GetArg("-maxsigcachesize", DEFAULT_MAX_SIG_CACHE_SIZE) / 2), MAX_MAX_SIG_CACHE_SIZE) * ((size_t) 1 << 20);
    size_t nElems = scriptExecutionCache.setup_bytes(nMaxCacheSize);
    LogPrintf("Using %zu MiB out of %zu/2 requested for script execution cache, able to store %zu elements\n",
            (nElems*sizeof(uint256)) >>20, (nMaxCacheSize*2)>>20, nElems);
}

/**
 * Check whether all inputs of this transaction are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set.
 *
 * If pvChecks is not nullptr, script checks are pushed onto it instead of being performed inline. Any
 * script checks which are not necessary (eg due to script execution cache hits) are, obviously,
 * not pushed onto pvChecks/run.
 *
 * Setting cacheSigStore/cacheFullScriptStore to false will remove elements from the corresponding cache
 * which are matched. This is useful for checking blocks where we will likely never need the cache
 * entry again.
 *
 * Non-static (and re-declared) in src/test/txvalidationcache_tests.cpp
 */
bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheSigStore, bool cacheFullScriptStore, PrecomputedTransactionData& txdata, std::vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsCoinBase())
    {
        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip script verification when connecting blocks under the
        // assumevalid block. Assuming the assumevalid block is valid this
        // is safe because block merkle hashes are still computed and checked,
        // Of course, if an assumed valid block is invalid due to false scriptSigs
        // this optimization would allow an invalid chain to be accepted.
        if (fScriptChecks) {
            // First check if script executions have been cached with the same
            // flags. Note that this assumes that the inputs provided are
            // correct (ie that the transaction hash which is in tx's prevouts
            // properly commits to the scriptPubKey in the inputs view of that
            // transaction).
            uint256 hashCacheEntry;
            // We only use the first 19 bytes of nonce to avoid a second SHA
            // round - giving us 19 + 32 + 4 = 55 bytes (+ 8 + 1 = 64)
            static_assert(55 - sizeof(flags) - 32 >= 128/8, "Want at least 128 bits of nonce for script execution cache");
            CSHA256().Write(scriptExecutionCacheNonce.begin(), 55 - sizeof(flags) - 32).Write(tx.GetWitnessHash().begin(), 32).Write((unsigned char*)&flags, sizeof(flags)).Finalize(hashCacheEntry.begin());
            AssertLockHeld(cs_main); //TODO: Remove this requirement by making CuckooCache not require external locks
            if (scriptExecutionCache.contains(hashCacheEntry, !cacheFullScriptStore)) {
                return true;
            }

            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;
                const Coin& coin = inputs.AccessCoin(prevout);
                assert(!coin.IsSpent());

                // We very carefully only pass in things to CScriptCheck which
                // are clearly committed to by tx' witness hash. This provides
                // a sanity check that our caching is not introducing consensus
                // failures through additional data in, eg, the coins being
                // spent being checked as a part of CScriptCheck.

                // Verify signature
                CScriptCheck check(coin.out, tx, i, flags, cacheSigStore, &txdata);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(coin.out, tx, i,
                                flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheSigStore, &txdata);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. an invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after soft-fork
                    // super-majority signaling has occurred.

                    return state.DoS(100,false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }

            if (cacheFullScriptStore && !pvChecks) {
                // We executed all of the provided scripts, and were told to
                // cache the result. Do so now.
                scriptExecutionCache.insert(hashCacheEntry);
            }
        }
    }

    return true;
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, blockundo);
    fileout << FLATDATA(messageStart) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s: ftell failed", __func__);
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: OpenUndoFile failed", __func__);

    // Read block
    uint256 hashChecksum;
    CHashVerifier<CAutoFile> verifier(&filein); // We need a CHashVerifier as reserializing may lose data
    try {
        verifier << hashBlock;
        verifier >> blockundo;
        filein >> hashChecksum;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    if (hashChecksum != verifier.GetHash())
        return error("%s: Checksum mismatch", __func__);

    return true;
}

/** Abort with a message */
bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    SetMiscWarning(strMessage);
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);

    StartShutdown();
    return false;
}

bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

} // namespace

enum DisconnectResult
{
    DISCONNECT_OK,      // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED   // Something else went wrong.
};

/**
 * Restore the UTXO in a Coin at a given COutPoint
 * @param undo The Coin to be restored.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return A DisconnectResult as an int
 */
int ApplyTxInUndo(Coin&& undo, CCoinsViewCache& view, const COutPoint& out, CAssetsCache* assetCache = nullptr)
{
    bool fClean = true;

    /** CLORE START */
    // This is needed because undo, is going to be cleared and moved when AddCoin is called. We need this for undo assets
    Coin tempCoin;
    bool fIsAsset = false;
    if (undo.IsAsset()) {
        fIsAsset = true;
        tempCoin = undo;
    }
    /** CLORE END */

    if (view.HaveCoin(out)) fClean = false; // overwriting transaction output

    if (undo.nHeight == 0) {
        // Missing undo metadata (height and coinbase). Older versions included this
        // information only in undo records for the last spend of a transactions'
        // outputs. This implies that it must be present for some other output of the same tx.
        const Coin& alternate = AccessByTxid(view, out.hash);
        if (!alternate.IsSpent()) {
            undo.nHeight = alternate.nHeight;
            undo.fCoinBase = alternate.fCoinBase;
        } else {
            return DISCONNECT_FAILED; // adding output for transaction without known metadata
        }
    }
    // The potential_overwrite parameter to AddCoin is only allowed to be false if we know for
    // sure that the coin did not already exist in the cache. As we have queried for that above
    // using HaveCoin, we don't need to guess. When fClean is false, a coin already existed and
    // it is an overwrite.
    view.AddCoin(out, std::move(undo), !fClean);

    /** CLORE START */
    if (AreAssetsDeployed()) {
        if (assetCache && fIsAsset) {
            if (!assetCache->UndoAssetCoin(tempCoin, out))
                fClean = false;
        }
    }
    /** CLORE END */

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When FAILED is returned, view is left in an indeterminate state. */
static DisconnectResult DisconnectBlock(const CBlock& block, const CBlockIndex* pindex, CCoinsViewCache& view, CAssetsCache* assetsCache = nullptr, bool ignoreAddressIndex = false, bool databaseMessaging = true)
{
    bool fClean = true;

    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull()) {
        error("DisconnectBlock(): no undo data available");
        return DISCONNECT_FAILED;
    }
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash())) {
        error("DisconnectBlock(): failure reading undo data");
        return DISCONNECT_FAILED;
    }

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size()) {
        error("DisconnectBlock(): block and undo data inconsistent");
        return DISCONNECT_FAILED;
    }

    std::vector<std::pair<std::string, CBlockAssetUndo> > vUndoData;
    if (!passetsdb->ReadBlockUndoAssetData(block.GetHash(), vUndoData)) {
        error("DisconnectBlock(): block asset undo data inconsistent");
        return DISCONNECT_FAILED;
    }
    
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    // undo transactions in reverse order
    CAssetsCache tempCache(*assetsCache);
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = *(block.vtx[i]);
        uint256 hash = tx.GetHash();
        bool is_coinbase = tx.IsCoinBase();

        std::vector<int> vAssetTxIndex;
        std::vector<int> vNullAssetTxIndex;
        if (fAddressIndex) {
            for (unsigned int k = tx.vout.size(); k-- > 0;) {
                const CTxOut &out = tx.vout[k];

                if (out.scriptPubKey.IsPayToScriptHash()) {
                    std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                    // undo receiving activity
                    addressIndex.push_back(std::make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), hash, k), CAddressUnspentValue()));

                } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                    std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                    // undo receiving activity
                    addressIndex.push_back(std::make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, hash, k, false), out.nValue));

                    // undo unspent index
                    addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), hash, k), CAddressUnspentValue()));

                } else if (out.scriptPubKey.IsPayToPublicKey()) {
                    uint160 hashBytes(Hash160(out.scriptPubKey.begin()+1, out.scriptPubKey.end()-1));
                    addressIndex.push_back(std::make_pair(CAddressIndexKey(1, hashBytes, pindex->nHeight, i, hash, k, false), out.nValue));
                    addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, hashBytes, hash, k), CAddressUnspentValue()));
                } else {
                    /** CLORE START */
                    if (AreAssetsDeployed()) {
                        std::string assetName;
                        CAmount assetAmount;
                        uint160 hashBytes;

                        if (ParseAssetScript(out.scriptPubKey, hashBytes, assetName, assetAmount)) {
//                            std::cout << "ConnectBlock(): pushing assets onto addressIndex: " << "1" << ", " << hashBytes.GetHex() << ", " << assetName << ", " << pindex->nHeight
//                                      << ", " << i << ", " << hash.GetHex() << ", " << k << ", " << "true" << ", " << assetAmount << std::endl;

                            // undo receiving activity
                            addressIndex.push_back(std::make_pair(
                                    CAddressIndexKey(1, uint160(hashBytes), assetName, pindex->nHeight, i, hash, k,
                                                     false), assetAmount));

                            // undo unspent index
                            addressUnspentIndex.push_back(
                                    std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), assetName, hash, k),
                                                   CAddressUnspentValue()));
                        } else {
                            continue;
                        }
                    }
                    /** CLORE END */
                }
            }
        }

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        int indexOfRestrictedAssetVerifierString = -1;
        for (size_t o = 0; o < tx.vout.size(); o++) {
            if (!tx.vout[o].scriptPubKey.IsUnspendable()) {
                COutPoint out(hash, o);
                Coin coin;
                bool is_spent = view.SpendCoin(out, &coin, &tempCache); /** CLORE START */ /* Pass assetsCache into the SpendCoin function */ /** CLORE END */
                if (!is_spent || tx.vout[o] != coin.out || pindex->nHeight != coin.nHeight || is_coinbase != coin.fCoinBase) {
                    fClean = false; // transaction output mismatch
                }

                /** CLORE START */
                if (AreAssetsDeployed()) {
                    if (assetsCache) {
                        if (IsScriptTransferAsset(tx.vout[o].scriptPubKey))
                            vAssetTxIndex.emplace_back(o);
                    }
                }
                /** CLORE START */
            } else {
                if(AreRestrictedAssetsDeployed()) {
                    if (assetsCache) {
                        if (tx.vout[o].scriptPubKey.IsNullAsset()) {
                            if (tx.vout[o].scriptPubKey.IsNullAssetVerifierTxDataScript()) {
                                indexOfRestrictedAssetVerifierString = o;
                            } else {
                                vNullAssetTxIndex.emplace_back(o);
                            }
                        }
                    }
                }
            }
        }

        /** CLORE START */
        if (AreAssetsDeployed()) {
            if (assetsCache) {
                if (tx.IsNewAsset()) {
                    // Remove the newly created asset
                    CNewAsset asset;
                    std::string strAddress;
                    if (!AssetFromTransaction(tx, asset, strAddress)) {
                        error("%s : Failed to get asset from transaction. TXID : %s", __func__, tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }
                    if (assetsCache->ContainsAsset(asset)) {
                        if (!assetsCache->RemoveNewAsset(asset, strAddress)) {
                            error("%s : Failed to Remove Asset. Asset Name : %s", __func__, asset.strName);
                            return DISCONNECT_FAILED;
                        }
                    }

                    // Get the owner from the transaction and remove it
                    std::string ownerName;
                    std::string ownerAddress;
                    if (!OwnerFromTransaction(tx, ownerName, ownerAddress)) {
                        error("%s : Failed to get owner from transaction. TXID : %s", __func__, tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    if (!assetsCache->RemoveOwnerAsset(ownerName, ownerAddress)) {
                        error("%s : Failed to Remove Owner from transaction. TXID : %s", __func__, tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }
                } else if (tx.IsReissueAsset()) {
                    CReissueAsset reissue;
                    std::string strAddress;

                    if (!ReissueAssetFromTransaction(tx, reissue, strAddress)) {
                        error("%s : Failed to get reissue asset from transaction. TXID : %s", __func__,
                              tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    if (assetsCache->ContainsAsset(reissue.strName)) {
                        if (!assetsCache->RemoveReissueAsset(reissue, strAddress,
                                                             COutPoint(tx.GetHash(), tx.vout.size() - 1),
                                                             vUndoData)) {
                            error("%s : Failed to Undo Reissue Asset. Asset Name : %s", __func__, reissue.strName);
                            return DISCONNECT_FAILED;
                        }
                    }
                } else if (tx.IsNewUniqueAsset()) {
                    for (int n = 0; n < (int)tx.vout.size(); n++) {
                        auto out = tx.vout[n];
                        CNewAsset asset;
                        std::string strAddress;

                        if (IsScriptNewUniqueAsset(out.scriptPubKey)) {
                            if (!AssetFromScript(out.scriptPubKey, asset, strAddress)) {
                                error("%s : Failed to get unique asset from transaction. TXID : %s, vout: %s", __func__,
                                      tx.GetHash().GetHex(), n);
                                return DISCONNECT_FAILED;
                            }

                            if (assetsCache->ContainsAsset(asset.strName)) {
                                if (!assetsCache->RemoveNewAsset(asset, strAddress)) {
                                    error("%s : Failed to Undo Unique Asset. Asset Name : %s", __func__, asset.strName);
                                    return DISCONNECT_FAILED;
                                }
                            }
                        }
                    }
                } else if (tx.IsNewMsgChannelAsset()) {
                    CNewAsset asset;
                    std::string strAddress;

                    if (!MsgChannelAssetFromTransaction(tx, asset, strAddress)) {
                        error("%s : Failed to get msgchannel asset from transaction. TXID : %s", __func__,
                              tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    if (assetsCache->ContainsAsset(asset.strName)) {
                        if (!assetsCache->RemoveNewAsset(asset, strAddress)) {
                            error("%s : Failed to Undo Msg Channel Asset. Asset Name : %s", __func__, asset.strName);
                            return DISCONNECT_FAILED;
                        }
                    }
                } else if (tx.IsNewQualifierAsset()) {
                    CNewAsset asset;
                    std::string strAddress;

                    if (!QualifierAssetFromTransaction(tx, asset, strAddress)) {
                        error("%s : Failed to get qualifier asset from transaction. TXID : %s", __func__,
                              tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    if (assetsCache->ContainsAsset(asset.strName)) {
                        if (!assetsCache->RemoveNewAsset(asset, strAddress)) {
                            error("%s : Failed to Undo Qualifier Asset. Asset Name : %s", __func__, asset.strName);
                            return DISCONNECT_FAILED;
                        }
                    }
                } else if (tx.IsNewRestrictedAsset()) {
                    CNewAsset asset;
                    std::string strAddress;

                    if (!RestrictedAssetFromTransaction(tx, asset, strAddress)) {
                        error("%s : Failed to get restricted asset from transaction. TXID : %s", __func__,
                              tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    if (assetsCache->ContainsAsset(asset.strName)) {
                        if (!assetsCache->RemoveNewAsset(asset, strAddress)) {
                            error("%s : Failed to Undo Restricted Asset. Asset Name : %s", __func__, asset.strName);
                            return DISCONNECT_FAILED;
                        }
                    }

                    if (indexOfRestrictedAssetVerifierString < 0) {
                        error("%s : Failed to find the restricted asset verifier string index from trasaction. TxID : %s", __func__, tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    CNullAssetTxVerifierString verifier;
                    if (!AssetNullVerifierDataFromScript(tx.vout[indexOfRestrictedAssetVerifierString].scriptPubKey, verifier)) {
                        error("%s : Failed to get the restricted asset verifier string from trasaction. TxID : %s", __func__, tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }

                    if (!assetsCache->RemoveRestrictedVerifier(asset.strName, verifier.verifier_string)){
                        error("%s : Failed to Remove Restricted Verifier from transaction. TXID : %s", __func__, tx.GetHash().GetHex());
                        return DISCONNECT_FAILED;
                    }
                }

                for (auto index : vAssetTxIndex) {
                    CAssetTransfer transfer;
                    std::string strAddress;
                    if (!TransferAssetFromScript(tx.vout[index].scriptPubKey, transfer, strAddress)) {
                        error("%s : Failed to get transfer asset from transaction. CTxOut : %s", __func__,
                              tx.vout[index].ToString());
                        return DISCONNECT_FAILED;
                    }

                    COutPoint out(hash, index);
                    if (!assetsCache->RemoveTransfer(transfer, strAddress, out)) {
                        error("%s : Failed to Remove the transfer of an asset. Asset Name : %s, COutPoint : %s",
                              __func__,
                              transfer.strName, out.ToString());
                        return DISCONNECT_FAILED;
                    }

                    // Undo messages
                    if (AreMessagesDeployed() && fMessaging && databaseMessaging && !transfer.message.empty() &&
                        (IsAssetNameAnOwner(transfer.strName) || IsAssetNameAnMsgChannel(transfer.strName))) {

                        LOCK(cs_messaging);
                        if (IsChannelSubscribed(transfer.strName)) {
                            OrphanMessage(COutPoint(hash, index));
                        }
                    }
                }

                if (AreRestrictedAssetsDeployed()) {
                    // Because of the strict rules for allowing the null asset tx types into a transaction.
                    // We know that if these are in a transaction, that they are valid null asset tx, and can be reversed
                    for (auto index: vNullAssetTxIndex) {
                        CScript script = tx.vout[index].scriptPubKey;

                        if (script.IsNullAssetTxDataScript()) {
                            CNullAssetTxData data;
                            std::string address;
                            if (!AssetNullDataFromScript(script, data, address)) {
                                error("%s : Failed to get null asset data from transaction. CTxOut : %s", __func__,
                                      tx.vout[index].ToString());
                                return DISCONNECT_FAILED;
                            }

                            AssetType type;
                            IsAssetNameValid(data.asset_name, type);

                            // Handle adding qualifiers to addresses
                            if (type == AssetType::QUALIFIER || type == AssetType::SUB_QUALIFIER) {
                                if (!assetsCache->RemoveQualifierAddress(data.asset_name, address, data.flag ? QualifierType::ADD_QUALIFIER : QualifierType::REMOVE_QUALIFIER)) {
                                    error("%s : Failed to remove qualifier from address, Qualifier : %s, Flag Removing : %d, Address : %s",
                                          __func__, data.asset_name, data.flag, address);
                                    return DISCONNECT_FAILED;
                                }
                            // Handle adding restrictions to addresses
                            } else if (type == AssetType::RESTRICTED) {
                                if (!assetsCache->RemoveRestrictedAddress(data.asset_name, address, data.flag ? RestrictedType::FREEZE_ADDRESS : RestrictedType::UNFREEZE_ADDRESS)) {
                                    error("%s : Failed to remove restriction from address, Restriction : %s, Flag Removing : %d, Address : %s",
                                          __func__, data.asset_name, data.flag, address);
                                    return DISCONNECT_FAILED;
                                }
                            }
                        } else if (script.IsNullGlobalRestrictionAssetTxDataScript()) {
                            CNullAssetTxData data;
                            std::string address;
                            if (!GlobalAssetNullDataFromScript(script, data)) {
                                error("%s : Failed to get global null asset data from transaction. CTxOut : %s", __func__,
                                      tx.vout[index].ToString());
                                return DISCONNECT_FAILED;
                            }

                            if (!assetsCache->RemoveGlobalRestricted(data.asset_name, data.flag ? RestrictedType::GLOBAL_FREEZE : RestrictedType::GLOBAL_UNFREEZE)) {
                                error("%s : Failed to remove global restriction from cache. Asset Name: %s, Flag Removing %d", __func__, data.asset_name, data.flag);
                                return DISCONNECT_FAILED;
                            }
                        } else if (script.IsNullAssetVerifierTxDataScript()) {
                            // These are handled in the undo restricted asset issuance, and restricted asset reissuance
                            continue;
                        }
                    }
                }
            }
        }
        /** CLORE END */

        // restore inputs
        if (i > 0) { // not coinbases
            CTxUndo &txundo = blockUndo.vtxundo[i-1];
            if (txundo.vprevout.size() != tx.vin.size()) {
                error("DisconnectBlock(): transaction and undo data inconsistent");
                return DISCONNECT_FAILED;
            }
            for (unsigned int j = tx.vin.size(); j-- > 0;) {
                const COutPoint &out = tx.vin[j].prevout;
                Coin &undo = txundo.vprevout[j];
                int res = ApplyTxInUndo(std::move(undo), view, out, assetsCache); /** CLORE START */ /* Pass assetsCache into ApplyTxInUndo function */ /** CLORE END */
                if (res == DISCONNECT_FAILED) return DISCONNECT_FAILED;
                fClean = fClean && res != DISCONNECT_UNCLEAN;

                const CTxIn input = tx.vin[j];

                if (fSpentIndex) {
                    // undo and delete the spent index
                    spentIndex.push_back(std::make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue()));
                }

                if (fAddressIndex) {
                    const CTxOut &prevout = view.AccessCoin(tx.vin[j].prevout).out;
                    if (prevout.scriptPubKey.IsPayToScriptHash()) {
                        std::vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22);

                        // undo spending activity
                        addressIndex.push_back(std::make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));


                    } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                        std::vector<unsigned char> hashBytes(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23);

                        // undo spending activity
                        addressIndex.push_back(std::make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, hash, j, true), prevout.nValue * -1));

                        // restore unspent index
                        addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));

                    } else if (prevout.scriptPubKey.IsPayToPublicKey()) {
                        uint160 hashBytes(Hash160(prevout.scriptPubKey.begin()+1, prevout.scriptPubKey.end()-1));
                        addressIndex.push_back(std::make_pair(CAddressIndexKey(1, hashBytes, pindex->nHeight, i, hash, j, false), prevout.nValue));
                        addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, hashBytes, hash, j), CAddressUnspentValue()));
                    } else {
                        /** CLORE START */
                        if (AreAssetsDeployed()) {
                            std::string assetName;
                            CAmount assetAmount;
                            uint160 hashBytes;

                            if (ParseAssetScript(prevout.scriptPubKey, hashBytes, assetName, assetAmount)) {
//                                std::cout << "ConnectBlock(): pushing assets onto addressIndex: " << "1" << ", " << hashBytes.GetHex() << ", " << assetName << ", " << pindex->nHeight
//                                          << ", " << i << ", " << hash.GetHex() << ", " << j << ", " << "true" << ", " << assetAmount * -1 << std::endl;

                                // undo spending activity
                                addressIndex.push_back(std::make_pair(
                                        CAddressIndexKey(1, uint160(hashBytes), assetName, pindex->nHeight, i, hash, j,
                                                         true), assetAmount * -1));

                                // restore unspent index
                                addressUnspentIndex.push_back(std::make_pair(
                                        CAddressUnspentKey(1, uint160(hashBytes), assetName, input.prevout.hash,
                                                           input.prevout.n),
                                        CAddressUnspentValue(assetAmount, prevout.scriptPubKey, undo.nHeight)));
                            } else {
                                continue;
                            }
                        }
                        /** CLORE END */
                    }
                }
            }
            // At this point, all of txundo.vprevout should have been moved out.
        }
    }


    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (!ignoreAddressIndex && fAddressIndex) {
        if (!pblocktree->EraseAddressIndex(addressIndex)) {
            error("Failed to delete address index");
            return DISCONNECT_FAILED;
        }
        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            error("Failed to write address unspent index");
            return DISCONNECT_FAILED;
        }
    }

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

static bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck() {
    RenameThread("clore-scriptch");
    scriptcheckqueue.Thread();
}

// Protected by cs_main
VersionBitsCache versionbitscache;

int32_t ComputeBlockVersion(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    LOCK(cs_main);
    int32_t nVersion = VERSIONBITS_TOP_BITS;

    /** If the assets are deployed now. We need to use the correct block version */
    if (AreAssetsDeployed())
        nVersion = VERSIONBITS_TOP_BITS_ASSETS;

    for (int i = 0; i < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; i++) {
        ThresholdState state = VersionBitsState(pindexPrev, params, (Consensus::DeploymentPos)i, versionbitscache);
        if (state == THRESHOLD_LOCKED_IN || state == THRESHOLD_STARTED) {
            nVersion |= VersionBitsMask(params, (Consensus::DeploymentPos)i);
        }
    }

    return nVersion;
}

/**
 * Threshold condition checker that triggers when unknown versionbits are seen on the network.
 */
class WarningBitsConditionChecker : public AbstractThresholdConditionChecker
{
private:
    int bit;

public:
    explicit WarningBitsConditionChecker(int bitIn) : bit(bitIn) {}

    int64_t BeginTime(const Consensus::Params& params) const override { return 0; }
    int64_t EndTime(const Consensus::Params& params) const override { return std::numeric_limits<int64_t>::max(); }
    int Period(const Consensus::Params& params) const override { return params.nMinerConfirmationWindow; }
    int Threshold(const Consensus::Params& params) const override { return params.nRuleChangeActivationThreshold; }

    bool Condition(const CBlockIndex* pindex, const Consensus::Params& params) const override
    {
        return ((pindex->nVersion & VERSIONBITS_TOP_MASK) == VERSIONBITS_TOP_BITS) &&
               ((pindex->nVersion >> bit) & 1) != 0 &&
               ((ComputeBlockVersion(pindex->pprev, params) >> bit) & 1) == 0;
    }
};

// Protected by cs_main
static ThresholdConditionCache warningcache[VERSIONBITS_NUM_BITS];

static unsigned int GetBlockScriptFlags(const CBlockIndex* pindex, const Consensus::Params& consensusparams) {
    AssertLockHeld(cs_main);

    // BIP16 didn't become active until Apr 1 2012
    int64_t nBIP16SwitchTime = 1333238400;
    bool fStrictPayToScriptHash = (pindex->GetBlockTime() >= nBIP16SwitchTime);

    unsigned int flags = fStrictPayToScriptHash ? SCRIPT_VERIFY_P2SH : SCRIPT_VERIFY_NONE;

    if(consensusparams.nBIP66Enabled) {
    // Start enforcing the DERSIG (BIP66) rule
    		flags |= SCRIPT_VERIFY_DERSIG;
    }

    if(consensusparams.nBIP65Enabled) {
    // Start enforcing CHECKLOCKTIMEVERIFY (BIP65) rule
    		flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    if(consensusparams.nCSVEnabled) {
    		// Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    		flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    // Start enforcing WITNESS rules using versionbits logic.
    if (IsWitnessEnabled(pindex->pprev, consensusparams)) {
    		flags |= SCRIPT_VERIFY_WITNESS;
    		flags |= SCRIPT_VERIFY_NULLDUMMY;
    }

    return flags;
}



static int64_t nTimeCheck = 0;
static int64_t nTimeForks = 0;
static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;
static int64_t nBlocksTotal = 0;

/** Apply the effects of this block (with given index) on the UTXO set represented by coins.
 *  Validity checks that depend on the UTXO set are also done; ConnectBlock()
 *  can fail if those validity checks fail (among other reasons). */
static bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex,
                  CCoinsViewCache& view, const CChainParams& chainparams, CAssetsCache* assetsCache = nullptr, bool fJustCheck = false, bool ignoreAddressIndex = false)
{

    AssertLockHeld(cs_main);
    assert(pindex);
    // pindex->phashBlock can be null if called by CreateNewBlock/TestBlockValidity
    assert((pindex->phashBlock == nullptr) ||
           (*pindex->phashBlock == block.GetHash()));
    int64_t nTimeStart = GetTimeMicros();

    // Check it again in case a previous version let a bad block in
    if (!CheckBlock(block, state, chainparams.GetConsensus(), !fJustCheck, !fJustCheck)) // Force the check of asset duplicates when connecting the block
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == nullptr ? uint256() : pindex->pprev->GetBlockHash();
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock) {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    nBlocksTotal++;

    bool fScriptChecks = true;
    if (!hashAssumeValid.IsNull()) {
        // We've been configured with the hash of a block which has been externally verified to have a valid history.
        // A suitable default value is included with the software and updated from time to time.  Because validity
        //  relative to a piece of software is an objective fact these defaults can be easily reviewed.
        // This setting doesn't force the selection of any particular chain but makes validating some faster by
        //  effectively caching the result of part of the verification.
        BlockMap::const_iterator  it = mapBlockIndex.find(hashAssumeValid);
        if (it != mapBlockIndex.end()) {
            if (it->second->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->GetAncestor(pindex->nHeight) == pindex &&
                pindexBestHeader->nChainWork >= nMinimumChainWork) {
                // This block is a member of the assumed verified chain and an ancestor of the best header.
                // The equivalent time check discourages hash power from extorting the network via DOS attack
                //  into accepting an invalid block through telling users they must manually set assumevalid.
                //  Requiring a software change or burying the invalid block, regardless of the setting, makes
                //  it hard to hide the implication of the demand.  This also avoids having release candidates
                //  that are hardly doing any signature verification at all in testing without having to
                //  artificially set the default assumed verified block further back.
                // The test against nMinimumChainWork prevents the skipping when denied access to any chain at
                //  least as good as the expected chain.
                fScriptChecks = (GetBlockProofEquivalentTime(*pindexBestHeader, *pindex, *pindexBestHeader, chainparams.GetConsensus()) <= 60 * 60 * 24 * 7 * 2);
            }
        }
    }

    int64_t nTime1 = GetTimeMicros(); nTimeCheck += nTime1 - nTimeStart;
    LogPrint(BCLog::BENCH, "    - Sanity checks: %.2fms [%.2fs (%.2fms/blk)]\n", MILLI * (nTime1 - nTimeStart), nTimeCheck * MICRO, nTimeCheck * MILLI / nBlocksTotal);

    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    // If such overwrites are allowed, coinbases and transactions depending upon those
    // can be duplicated to remove the ability to spend the first instance -- even after
    // being sent to another address.
    // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
    // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
    // already refuses previously-known transaction ids entirely.
    // This rule was originally applied to all blocks with a timestamp after March 15, 2012, 0:00 UTC.
    // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
    // two in the chain that violate it. This prevents exploiting the issue against nodes during their
    // initial block download.
    // bool fEnforceBIP30 = (!pindex->phashBlock) || // Enforce on CreateNewBlock invocations which don't have a hash.
    //                       !((pindex->nHeight==91842 && pindex->GetBlockHash() == uint256S("0x00000000000a4d0a398161ffc163c503763b1f4360639393e0e4c8e300e0caec")) ||
    //                        (pindex->nHeight==91880 && pindex->GetBlockHash() == uint256S("0x00000000000743f190a18c5577a3c2d2a1f610ae9601ac046a38084ccb7cd721")));

    // Once BIP34 activated it was not possible to create new duplicate coinbases and thus other than starting
    // with the 2 existing duplicate coinbase pairs, not possible to create overwriting txs.  But by the
    // time BIP34 activated, in each of the existing pairs the duplicate coinbase had overwritten the first
    // before the first had been spent.  Since those coinbases are sufficiently buried its no longer possible to create further
    // duplicate transactions descending from the known pairs either.
    // If we're on the known chain at height greater than where BIP34 activated, we can save the db accesses needed for the BIP30 check.
    // assert(pindex->pprev);
    // CBlockIndex *pindexBIP34height = pindex->pprev->GetAncestor(chainparams.GetConsensus().BIP34Height);
    // //Only continue to enforce if we're below BIP34 activation height or the block hash at that height doesn't correspond.
    // fEnforceBIP30 = fEnforceBIP30 && (!pindexBIP34height || !(pindexBIP34height->GetBlockHash() == chainparams.GetConsensus().BIP34Hash));

    // if (fEnforceBIP30) {
    //     for (const auto& tx : block.vtx) {
    //         for (size_t o = 0; o < tx->vout.size(); o++) {
    //             if (view.HaveCoin(COutPoint(tx->GetHash(), o))) {
    //                 return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
    //                                  REJECT_INVALID, "bad-txns-BIP30");
    //             }
    //         }
    //     }
    // }

    // Start enforcing BIP68 (sequence locks) and BIP112 (CHECKSEQUENCEVERIFY) using versionbits logic.
    int nLockTimeFlags = 0;
    if(chainparams.CSVEnabled()) {
    		nLockTimeFlags |= LOCKTIME_VERIFY_SEQUENCE;
    }

    // Get the script flags for this block
    unsigned int flags = GetBlockScriptFlags(pindex, chainparams.GetConsensus());

    int64_t nTime2 = GetTimeMicros(); nTimeForks += nTime2 - nTime1;
    LogPrint(BCLog::BENCH, "    - Fork checks: %.2fms [%.2fs (%.2fms/blk)]\n", MILLI * (nTime2 - nTime1), nTimeForks * MICRO, nTimeForks * MILLI / nBlocksTotal);

    CBlockUndo blockundo;
    std::vector<std::pair<std::string, CBlockAssetUndo> > vUndoAssetData;

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : nullptr);

    std::vector<int> prevheights;
    CAmount nFees = 0;
    int nInputs = 0;
    int64_t nSigOpsCost = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<PrecomputedTransactionData> txdata;
    txdata.reserve(block.vtx.size()); // Required so that pointers to individual PrecomputedTransactionData don't get invalidated

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    std::set<CMessage> setMessages;
    std::vector<std::pair<std::string, CNullAssetTxData>> myNullAssetData;
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = *(block.vtx[i]);
        const uint256 txhash = tx.GetHash();

        nInputs += tx.vin.size();

        if (!tx.IsCoinBase())
        {
            CAmount txfee = 0;
            if (!Consensus::CheckTxInputs(tx, state, view, pindex->nHeight, txfee)) {
                return error("%s: Consensus::CheckTxInputs: %s, %s", __func__, tx.GetHash().ToString(), FormatStateMessage(state));
            }
            nFees += txfee;
            if (!MoneyRange(nFees)) {
                return state.DoS(100, error("%s: accumulated fee in the block out of range.", __func__),
                                 REJECT_INVALID, "bad-txns-accumulated-fee-outofrange");
            }

            /** CLORE START */
            if (!AreAssetsDeployed()) {
                for (auto out : tx.vout)
                    if (out.scriptPubKey.IsAssetScript())
                        return state.DoS(100, error("%s : Received Block with tx that contained an asset when assets wasn't active", __func__), REJECT_INVALID, "bad-txns-assets-not-active");
                    else if (out.scriptPubKey.IsNullAsset())
                        return state.DoS(100, error("%s : Received Block with tx that contained an null asset data tx when assets wasn't active", __func__), REJECT_INVALID, "bad-txns-null-data-assets-not-active");
            }

            if (AreAssetsDeployed()) {
                std::vector<std::pair<std::string, uint256>> vReissueAssets;
                if (!Consensus::CheckTxAssets(tx, state, view, assetsCache, false, vReissueAssets, false, &setMessages, block.nTime, &myNullAssetData)) {
                    state.SetFailedTransaction(tx.GetHash());
                    return error("%s: Consensus::CheckTxAssets: %s, %s", __func__, tx.GetHash().ToString(),
                                 FormatStateMessage(state));
                }
            }

            /** CLORE END */

            // Check that transaction is BIP68 final
            // BIP68 lock checks (as opposed to nLockTime checks) must
            // be in ConnectBlock because they require the UTXO set
            prevheights.resize(tx.vin.size());
            for (size_t j = 0; j < tx.vin.size(); j++) {
                prevheights[j] = view.AccessCoin(tx.vin[j].prevout).nHeight;
            }

            if (!SequenceLocks(tx, nLockTimeFlags, &prevheights, *pindex)) {
                return state.DoS(100, error("%s: contains a non-BIP68-final transaction", __func__),
                                 REJECT_INVALID, "bad-txns-nonfinal");
            }

            if (fAddressIndex || fSpentIndex)
            {
                for (size_t j = 0; j < tx.vin.size(); j++) {

                    const CTxIn input = tx.vin[j];
                    const CTxOut &prevout = view.AccessCoin(tx.vin[j].prevout).out;
                    uint160 hashBytes;
                    int addressType = 0;
                    bool isAsset = false;
                    std::string assetName;
                    CAmount assetAmount;

                    if (prevout.scriptPubKey.IsPayToScriptHash()) {
                        hashBytes = uint160(std::vector <unsigned char>(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22));
                        addressType = 2;
                    } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                        hashBytes = uint160(std::vector <unsigned char>(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23));
                        addressType = 1;
                    } else if (prevout.scriptPubKey.IsPayToPublicKey()) {
                        hashBytes = Hash160(prevout.scriptPubKey.begin() + 1, prevout.scriptPubKey.end() - 1);
                        addressType = 1;
                    } else {
                        /** CLORE START */
                        if (AreAssetsDeployed()) {
                            hashBytes.SetNull();
                            addressType = 0;

                            if (ParseAssetScript(prevout.scriptPubKey, hashBytes, assetName, assetAmount)) {
                                addressType = 1;
                                isAsset = true;
                            }
                        }
                        /** CLORE END */
                    }

                    if (fAddressIndex && addressType > 0) {
                        /** CLORE START */
                        if (isAsset) {
//                            std::cout << "ConnectBlock(): pushing assets onto addressIndex: " << "1" << ", " << hashBytes.GetHex() << ", " << assetName << ", " << pindex->nHeight
//                                      << ", " << i << ", " << txhash.GetHex() << ", " << j << ", " << "true" << ", " << assetAmount * -1 << std::endl;

                            // record spending activity
                            addressIndex.push_back(std::make_pair(CAddressIndexKey(addressType, hashBytes, assetName, pindex->nHeight, i, txhash, j, true), assetAmount * -1));

                            // remove address from unspent index
                            addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(addressType, hashBytes, assetName, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
                        /** CLORE END */
                        } else {
                            // record spending activity
                            addressIndex.push_back(std::make_pair(CAddressIndexKey(addressType, hashBytes, pindex->nHeight, i, txhash, j, true), prevout.nValue * -1));

                            // remove address from unspent index
                            addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(addressType, hashBytes, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
                        }
                    }
                    /** CLORE END */

                    if (fSpentIndex) {
                        // add the spent index to determine the txid and input that spent an output
                        // and to find the amount and address from an input
                        spentIndex.push_back(std::make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue(txhash, j, pindex->nHeight, prevout.nValue, addressType, hashBytes)));
                    }
                }

            }
        }

        // GetTransactionSigOpCost counts 3 types of sigops:
        // * legacy (always)
        // * p2sh (when P2SH enabled in flags and excludes coinbase)
        // * witness (when witness enabled in flags and excludes coinbase)
        nSigOpsCost += GetTransactionSigOpCost(tx, view, flags);
        if (nSigOpsCost > MAX_BLOCK_SIGOPS_COST)
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        txdata.emplace_back(tx);
        if (!tx.IsCoinBase())
        {
            std::vector<CScriptCheck> vChecks;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, fCacheResults, txdata[i], nScriptCheckThreads ? &vChecks : nullptr))
                return error("ConnectBlock(): CheckInputs on %s failed with %s",
                    tx.GetHash().ToString(), FormatStateMessage(state));
            control.Add(vChecks);
        }

        if (fAddressIndex) {
            for (unsigned int k = 0; k < tx.vout.size(); k++) {
                const CTxOut &out = tx.vout[k];

                if (out.scriptPubKey.IsPayToScriptHash()) {
                    std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                    // record receiving activity
                    addressIndex.push_back(std::make_pair(CAddressIndexKey(2, uint160(hashBytes), pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));

                } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                    std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                    // record receiving activity
                    addressIndex.push_back(std::make_pair(CAddressIndexKey(1, uint160(hashBytes), pindex->nHeight, i, txhash, k, false), out.nValue));

                    // record unspent output
                    addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));

                } else if (out.scriptPubKey.IsPayToPublicKey()) {
                    uint160 hashBytes(Hash160(out.scriptPubKey.begin() + 1, out.scriptPubKey.end() - 1));
                    addressIndex.push_back(
                            std::make_pair(CAddressIndexKey(1, hashBytes, pindex->nHeight, i, txhash, k, false),
                                           out.nValue));
                    addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(1, hashBytes, txhash, k),
                                                                 CAddressUnspentValue(out.nValue, out.scriptPubKey,
                                                                                      pindex->nHeight)));
                } else {
                    /** CLORE START */
                    if (AreAssetsDeployed()) {
                        std::string assetName;
                        CAmount assetAmount;
                        uint160 hashBytes;

                        if (ParseAssetScript(out.scriptPubKey, hashBytes, assetName, assetAmount)) {
//                            std::cout << "ConnectBlock(): pushing assets onto addressIndex: " << "1" << ", " << hashBytes.GetHex() << ", " << assetName << ", " << pindex->nHeight
//                                      << ", " << i << ", " << txhash.GetHex() << ", " << k << ", " << "true" << ", " << assetAmount << std::endl;

                            // record receiving activity
                            addressIndex.push_back(std::make_pair(
                                    CAddressIndexKey(1, hashBytes, assetName, pindex->nHeight, i, txhash, k, false),
                                    assetAmount));

                            // record unspent output
                            addressUnspentIndex.push_back(
                                    std::make_pair(CAddressUnspentKey(1, hashBytes, assetName, txhash, k),
                                                   CAddressUnspentValue(assetAmount, out.scriptPubKey,
                                                                        pindex->nHeight)));
                        }
                    } else {
                        continue;
                    }
                    /** CLORE END */
                }
            }
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        /** CLORE START */
        // Create the basic empty string pair for the undoblock
        std::pair<std::string, CBlockAssetUndo> undoPair = std::make_pair("", CBlockAssetUndo());
        std::pair<std::string, CBlockAssetUndo>* undoAssetData = &undoPair;
        /** CLORE END */

        UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight, block.GetHash(), assetsCache, undoAssetData);

        /** CLORE START */
        if (!undoAssetData->first.empty()) {
            vUndoAssetData.emplace_back(*undoAssetData);
        }
        /** CLORE END */

        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }
    int64_t nTime3 = GetTimeMicros(); nTimeConnect += nTime3 - nTime2;
    LogPrint(BCLog::BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs (%.2fms/blk)]\n", (unsigned)block.vtx.size(), MILLI * (nTime3 - nTime2), MILLI * (nTime3 - nTime2) / block.vtx.size(), nInputs <= 1 ? 0 : MILLI * (nTime3 - nTime2) / (nInputs-1), nTimeConnect * MICRO, nTimeConnect * MILLI / nBlocksTotal);

    CAmount blockReward = nFees + GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    if (block.vtx[0]->GetValueOut(AreEnforcedValuesDeployed()) > blockReward)
        return state.DoS(100,
                         error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",block.vtx[0]->GetValueOut(AreEnforcedValuesDeployed()), blockReward),
                         REJECT_INVALID, "bad-cb-amount");
	
    /** CLORE START */
	//CommunityAutonomousAddress Assign 10%
	std::string  GetCommunityAutonomousAddress 	= GetParams().CommunityAutonomousAddress();
	CTxDestination destCommunityAutonomous 		= DecodeDestination(GetCommunityAutonomousAddress);
    if (!IsValidDestination(destCommunityAutonomous)) {
		LogPrintf("IsValidDestination: Invalid Clore address %s \n", GetCommunityAutonomousAddress);
    }
	// Parse Clore address
    CScript scriptPubKeyCommunityAutonomous 	= GetScriptForDestination(destCommunityAutonomous);
	
	CAmount nCommunityAutonomousAmount 			= GetParams().CommunityAutonomousAmount();
	CAmount nSubsidy 							= GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
	CAmount nCommunityAutonomousAmountValue		= nSubsidy*nCommunityAutonomousAmount/100;
	/* Remove Log to console
	LogPrintf("==>block.vtx[0]->vout[1].nValue:    %ld \n", block.vtx[0]->vout[1].nValue);
	LogPrintf("==>nCommunityAutonomousAmountValue: %ld \n", nCommunityAutonomousAmountValue);
	LogPrintf("==>block.vtx[0]->vout[1].scriptPubKey: %s \n", block.vtx[0]->vout[1].scriptPubKey[3]);
	LogPrintf("==>GetCommunityAutonomousAddress:   %s \n", GetCommunityAutonomousAddress);
	LogPrintf("==>scriptPubKeyCommunityAutonomous    Actual: %s \n", HexStr(block.vtx[0]->vout[1].scriptPubKey));
	LogPrintf("==>scriptPubKeyCommunityAutonomous Should Be: %s \n", HexStr(scriptPubKeyCommunityAutonomous));
	*/
	//Check 10% Amount
	if(block.vtx[0]->vout[1].nValue != nCommunityAutonomousAmountValue )		{
		return state.DoS(100,
                         error("ConnectBlock(): coinbase Community Autonomous Amount Is Invalid. Actual: %ld Should be:%ld ",block.vtx[0]->vout[1].nValue, nCommunityAutonomousAmountValue),
                         REJECT_INVALID, "bad-cb-community-autonomous-amount");
	}
	//Check 10% Address
	if( HexStr(block.vtx[0]->vout[1].scriptPubKey) != HexStr(scriptPubKeyCommunityAutonomous) )		{
		return state.DoS(100,
                         error("ConnectBlock(): coinbase Community Autonomous Address Is Invalid. Actual: %s Should Be: %s \n",HexStr(block.vtx[0]->vout[1].scriptPubKey), HexStr(scriptPubKeyCommunityAutonomous)),
                         REJECT_INVALID, "bad-cb-community-autonomous-address");
	}
	/** CLORE END */
	
    if (!control.Wait())
        return state.DoS(100, error("%s: CheckQueue failed", __func__), REJECT_INVALID, "block-validation-failed");
    int64_t nTime4 = GetTimeMicros(); nTimeVerify += nTime4 - nTime2;
    LogPrint(BCLog::BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs (%.2fms/blk)]\n", nInputs - 1, MILLI * (nTime4 - nTime2), nInputs <= 1 ? 0 : MILLI * (nTime4 - nTime2) / (nInputs-1), nTimeVerify * MICRO, nTimeVerify * MILLI / nBlocksTotal);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos _pos;
            if (!FindUndoPos(state, pindex->nFile, _pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock(): FindUndoPos failed");
            if (!UndoWriteToDisk(blockundo, _pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = _pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        if (vUndoAssetData.size()) {
            if (!passetsdb->WriteBlockUndoAssetData(block.GetHash(), vUndoAssetData))
                return AbortNode(state, "Failed to write asset undo data");
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    if (!ignoreAddressIndex && fAddressIndex) {
        if (!pblocktree->WriteAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to write address index");
        }

        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    if (!ignoreAddressIndex && fSpentIndex)
        if (!pblocktree->UpdateSpentIndex(spentIndex))
            return AbortNode(state, "Failed to write transaction index");

    if (!ignoreAddressIndex && fTimestampIndex) {
        unsigned int logicalTS = pindex->nTime;
        unsigned int prevLogicalTS = 0;

        // retrieve logical timestamp of the previous block
        if (pindex->pprev)
            if (!pblocktree->ReadTimestampBlockIndex(pindex->pprev->GetBlockHash(), prevLogicalTS))
                LogPrintf("%s: Failed to read previous block's logical timestamp\n", __func__);

        if (logicalTS <= prevLogicalTS) {
            logicalTS = prevLogicalTS + 1;
            LogPrintf("%s: Previous logical timestamp is newer Actual[%d] prevLogical[%d] Logical[%d]\n", __func__, pindex->nTime, prevLogicalTS, logicalTS);
        }

        if (!pblocktree->WriteTimestampIndex(CTimestampIndexKey(logicalTS, pindex->GetBlockHash())))
            return AbortNode(state, "Failed to write timestamp index");

        if (!pblocktree->WriteTimestampBlockIndex(CTimestampBlockIndexKey(pindex->GetBlockHash()), CTimestampBlockIndexValue(logicalTS)))
            return AbortNode(state, "Failed to write blockhash index");
    }

    if (AreMessagesDeployed() && fMessaging && setMessages.size()) {
        LOCK(cs_messaging);
        for (auto message : setMessages) {
            int nHeight = 0;
            if (pindex)
                nHeight = pindex->nHeight;
            message.nBlockHeight = nHeight;

            if (message.nExpiredTime == 0 || GetTime() < message.nExpiredTime)
                GetMainSignals().NewAssetMessage(message);

            if (IsChannelSubscribed(message.strName)) {
                AddMessage(message);
            }
        }
    }
#ifdef ENABLE_WALLET
    if (AreRestrictedAssetsDeployed() && myNullAssetData.size() && pmyrestricteddb) {
        for (auto item : myNullAssetData) {
            if (IsAssetNameAQualifier(item.second.asset_name)) {
                // TODO we can add block height to this data also, and use it to pull more info on when this was tagged/untagged
                pmyrestricteddb->WriteTaggedAddress(item.first, item.second.asset_name, item.second.flag ? true : false, block.nTime);
            } else if (IsAssetNameAnRestricted(item.second.asset_name)) {
                pmyrestricteddb->WriteRestrictedAddress(item.first, item.second.asset_name, item.second.flag ? true : false, block.nTime);
            }


            if (vpwallets.size())
                vpwallets[0]->UpdateMyRestrictedAssets(item.first, item.second.asset_name, item.second.flag, block.nTime);

        }
    }
#endif

    assert(pindex->phashBlock);
    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime5 = GetTimeMicros(); nTimeIndex += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "    - Index writing: %.2fms [%.2fs (%.2fms/blk)]\n", MILLI * (nTime5 - nTime4), nTimeIndex * MICRO, nTimeIndex * MILLI / nBlocksTotal);

    int64_t nTime6 = GetTimeMicros(); nTimeCallbacks += nTime6 - nTime5;
    LogPrint(BCLog::BENCH, "    - Callbacks: %.2fms [%.2fs (%.2fms/blk)]\n", MILLI * (nTime6 - nTime5), nTimeCallbacks * MICRO, nTimeCallbacks * MILLI / nBlocksTotal);

    return true;
}

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool static FlushStateToDisk(const CChainParams& chainparams, CValidationState &state, FlushStateMode mode, int nManualPruneHeight) {
    int64_t nMempoolUsage = mempool.DynamicMemoryUsage();
    LOCK(cs_main);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    bool fDoFullFlush = false;
    int64_t nNow = 0;

    try {
    {
        LOCK(cs_LastBlockFile);
        if (fPruneMode && (fCheckForPruning || nManualPruneHeight > 0) && !fReindex) {
            if (nManualPruneHeight > 0) {
                FindFilesToPruneManual(setFilesToPrune, nManualPruneHeight);
            } else {
                FindFilesToPrune(setFilesToPrune, chainparams.PruneAfterHeight());
                fCheckForPruning = false;
            }
            if (!setFilesToPrune.empty()) {
                fFlushForPrune = true;
                if (!fHavePruned) {
                    pblocktree->WriteFlag("prunedblockfiles", true);
                    fHavePruned = true;
                }
            }
        }
        nNow = GetTimeMicros();
        // Avoid writing/flushing immediately after startup.
        if (nLastWrite == 0) {
            nLastWrite = nNow;
        }
        if (nLastFlush == 0) {
            nLastFlush = nNow;
        }
        if (nLastSetChain == 0) {
            nLastSetChain = nNow;
        }

        // Get the size of the memory used by the asset cache.
        int64_t assetDynamicSize = 0;
        int64_t assetDirtyCacheSize = 0;
        size_t assetMapAmountSize = 0;
        if (AreAssetsDeployed()) {
            auto currentActiveAssetCache = GetCurrentAssetCache();
            if (currentActiveAssetCache) {
                assetDynamicSize = currentActiveAssetCache->DynamicMemoryUsage();
                assetDirtyCacheSize = currentActiveAssetCache->GetCacheSizeV2();
                assetMapAmountSize = currentActiveAssetCache->mapAssetsAddressAmount.size();
            }
        }

        int messageCacheSize = 0;

        if (fMessaging) {
                messageCacheSize = GetMessageDirtyCacheSize();
        }

        int64_t nMempoolSizeMax = gArgs.GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
        int64_t cacheSize = pcoinsTip->DynamicMemoryUsage() + assetDynamicSize + assetDirtyCacheSize + messageCacheSize;
        int64_t nTotalSpace = nCoinCacheUsage + std::max<int64_t>(nMempoolSizeMax - nMempoolUsage, 0);
        // The cache is large and we're within 10% and 10 MiB of the limit, but we have time now (not in the middle of a block processing).
        bool fCacheLarge = mode == FLUSH_STATE_PERIODIC && cacheSize > std::max((9 * nTotalSpace) / 10, nTotalSpace - MAX_BLOCK_COINSDB_USAGE * 1024 * 1024);
        // The cache is over the limit, we have to write now.
        bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && (cacheSize > nTotalSpace || assetMapAmountSize > 1000000);
        // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
        bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
        // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
        bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;

        // Combine all conditions that result in a full cache flush.
        fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;

        if (!fDoFullFlush && IsInitialSyncSpeedUp() && nNow > nLastFlush + (int64_t) DATABASE_FLUSH_INTERVAL_SPEEDY * 1000000) {
            LogPrintf("Flushing to database sooner for speedy sync\n");
            fDoFullFlush = true;
        }

        // Write blocks and block index to disk.
        if (fDoFullFlush || fPeriodicWrite) {
            // Depend on nMinDiskSpace to ensure we can write block index
            if (!CheckDiskSpace(0))
                return state.Error("out of disk space");
            // First make sure all block and undo data is flushed to disk.
            FlushBlockFile();
            // Then update all block file information (which may refer to block and undo files).
            {
                std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
                vFiles.reserve(setDirtyFileInfo.size());
                for (std::set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                    vFiles.push_back(std::make_pair(*it, &vinfoBlockFile[*it]));
                    setDirtyFileInfo.erase(it++);
                }
                std::vector<const CBlockIndex*> vBlocks;
                vBlocks.reserve(setDirtyBlockIndex.size());
                for (std::set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                    vBlocks.push_back(*it);
                    setDirtyBlockIndex.erase(it++);
                }
                if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                    return AbortNode(state, "Failed to write to block index database");
                }
            }
            // Finally remove any pruned files
            if (fFlushForPrune)
                UnlinkPrunedFiles(setFilesToPrune);
            nLastWrite = nNow;
        }
        // Flush best chain related state. This can only be done if the blocks / block index write was also done.
        if (fDoFullFlush) {


            // Typical Coin structures on disk are around 48 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!CheckDiskSpace((48 * 2 * 2 * pcoinsTip->GetCacheSize()) + assetDirtyCacheSize * 2)) /** CLORE START */ /** CLORE END */
                return state.Error("out of disk space");

            // Flush the chainstate (which may refer to block index entries).
            if (!pcoinsTip->Flush())
                return AbortNode(state, "Failed to write to coin database");

            /** CLORE START */
            // Flush the assetstate
            if (AreAssetsDeployed()) {
                // Flush the assetstate
                auto currentActiveAssetCache = GetCurrentAssetCache();
                if (currentActiveAssetCache) {
                    if (!currentActiveAssetCache->DumpCacheToDatabase())
                        return AbortNode(state, "Failed to write to asset database");
                }
            }

            // Write the reissue mempool data to database
            if (passetsdb)
                passetsdb->WriteReissuedMempoolState();

            if (fMessaging) {
                if (pmessagedb) {
                    LOCK(cs_messaging);
                    if (!pmessagedb->Flush())
                        return AbortNode(state, "Failed to Flush the message database");
                }

                if (pmessagechanneldb) {
                    LOCK(cs_messaging);
                    if (!pmessagechanneldb->Flush())
                        return AbortNode(state, "Failed to Flush the message channel database");
                }
            }
            /** CLORE END */

            nLastFlush = nNow;
        }
    }
    if (fDoFullFlush || ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000)) {
        // Update best block in wallet (so we can detect restored wallets).
        GetMainSignals().SetBestChain(chainActive.GetLocator());
        nLastSetChain = nNow;
    }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    const CChainParams& chainparams = GetParams();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_ALWAYS);
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    const CChainParams& chainparams = GetParams();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE);
}

static void DoWarning(const std::string& strWarning)
{
    static bool fWarned = false;
    SetMiscWarning(strWarning);
    if (!fWarned) {
        AlertNotify(strWarning);
        fWarned = true;
    }
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex *pindexNew, const CChainParams& chainParams) {
    chainActive.SetTip(pindexNew);

    // New best block
    mempool.AddTransactionsUpdated(1);

    cvBlockChange.notify_all();

    std::vector<std::string> warningMessages;
    if (!IsInitialBlockDownload())
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int bit = 0; bit < VERSIONBITS_NUM_BITS; bit++) {
            WarningBitsConditionChecker checker(bit);
            ThresholdState state = checker.GetStateFor(pindex, chainParams.GetConsensus(), warningcache[bit]);
            if (state == THRESHOLD_ACTIVE || state == THRESHOLD_LOCKED_IN) {
                const std::string strWarning = strprintf(_("Warning: unknown new rules activated (versionbit %i)"), bit);
                if (bit == 28 || bit == 25) // DUMMY TEST BIT
                    continue;
                if (state == THRESHOLD_ACTIVE) {
                    DoWarning(strWarning);
                } else {
                    warningMessages.push_back(strWarning);
                }
            }
        }
        // Check the version of the last 100 blocks to see if we need to upgrade:
        for (int i = 0; i < 100 && pindex != nullptr; i++)
        {
            int32_t nExpectedVersion = ComputeBlockVersion(pindex->pprev, chainParams.GetConsensus());
            if (pindex->nVersion > nExpectedVersion)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            warningMessages.push_back(strprintf(_("%d of last 100 blocks have unexpected version"), nUpgraded));
        if (nUpgraded > 100/2)
        {
            std::string strWarning = _("Warning: Unknown block versions being mined! It's possible unknown rules are in effect");
            // notify GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            DoWarning(strWarning);
        }
    }
    LogPrintf("%s: new best=%s height=%d version=0x%08x log2_work=%.8g tx=%lu date='%s' progress=%f cache=%.1fMiB(%utxo)", __func__,
      chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(), chainActive.Tip()->nVersion,
      log(chainActive.Tip()->nChainWork.getdouble())/log(2.0), (unsigned long)chainActive.Tip()->nChainTx,
      DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
      GuessVerificationProgress(chainParams.TxData(), chainActive.Tip()), pcoinsTip->DynamicMemoryUsage() * (1.0 / (1<<20)), pcoinsTip->GetCacheSize());
    if (!warningMessages.empty())
        LogPrintf(" warning='%s'", boost::algorithm::join(warningMessages, ", "));
    LogPrintf("\n");

}

/** Disconnect chainActive's tip.
  * After calling, the mempool will be in an inconsistent state, with
  * transactions from disconnected blocks being added to disconnectpool.  You
  * should make the mempool consistent again by calling UpdateMempoolForReorg.
  * with cs_main held.
  *
  * If disconnectpool is nullptr, then no disconnected transactions are added to
  * disconnectpool (note that the caller is responsible for mempool consistency
  * in any case).
  */
bool static DisconnectTip(CValidationState& state, const CChainParams& chainparams, DisconnectedBlockTransactions *disconnectpool)
{
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
    CBlock& block = *pblock;
    if (!ReadBlockFromDisk(block, pindexDelete, chainparams.GetConsensus()))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        CAssetsCache assetCache;

        assert(view.GetBestBlock() == pindexDelete->GetBlockHash());
        if (DisconnectBlock(block, pindexDelete, view, &assetCache) != DISCONNECT_OK)
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        bool flushed = view.Flush();
        assert(flushed);

        bool assetsFlushed = assetCache.Flush();
        assert(assetsFlushed);
    }
    LogPrint(BCLog::BENCH, "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * MILLI);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_IF_NEEDED))
        return false;

    if (disconnectpool) {
        // Save transactions to re-add to mempool at end of reorg
        for (auto it = block.vtx.rbegin(); it != block.vtx.rend(); ++it) {
            disconnectpool->addTransaction(*it);
        }
        while (disconnectpool->DynamicMemoryUsage() > MAX_DISCONNECTED_TX_POOL_SIZE * 1000) {
            // Drop the earliest entry, and remove its children from the mempool.
            auto it = disconnectpool->queuedTx.get<insertion_order>().begin();
            mempool.removeRecursive(**it, MemPoolRemovalReason::REORG);
            disconnectpool->removeEntry(it);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev, chainparams);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    GetMainSignals().BlockDisconnected(pblock);
    return true;
}

static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeAssetFlush = 0;
static int64_t nTimeAssetTasks = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

struct PerBlockConnectTrace {
    CBlockIndex* pindex = nullptr;
    std::shared_ptr<const CBlock> pblock;
    std::shared_ptr<std::vector<CTransactionRef>> conflictedTxs;
    PerBlockConnectTrace() : conflictedTxs(std::make_shared<std::vector<CTransactionRef>>()) {}
};
/**
 * Used to track blocks whose transactions were applied to the UTXO state as a
 * part of a single ActivateBestChainStep call.
 *
 * This class also tracks transactions that are removed from the mempool as
 * conflicts (per block) and can be used to pass all those transactions
 * through SyncTransaction.
 *
 * This class assumes (and asserts) that the conflicted transactions for a given
 * block are added via mempool callbacks prior to the BlockConnected() associated
 * with those transactions. If any transactions are marked conflicted, it is
 * assumed that an associated block will always be added.
 *
 * This class is single-use, once you call GetBlocksConnected() you have to throw
 * it away and make a new one.
 */
class ConnectTrace {
private:
    std::vector<PerBlockConnectTrace> blocksConnected;
    CTxMemPool &pool;

public:
    explicit ConnectTrace(CTxMemPool &_pool) : blocksConnected(1), pool(_pool) {
        pool.NotifyEntryRemoved.connect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    ~ConnectTrace() {
        pool.NotifyEntryRemoved.disconnect(boost::bind(&ConnectTrace::NotifyEntryRemoved, this, _1, _2));
    }

    void BlockConnected(CBlockIndex* pindex, std::shared_ptr<const CBlock> pblock) {
        assert(!blocksConnected.back().pindex);
        assert(pindex);
        assert(pblock);
        blocksConnected.back().pindex = pindex;
        blocksConnected.back().pblock = std::move(pblock);
        blocksConnected.emplace_back();
    }

    std::vector<PerBlockConnectTrace>& GetBlocksConnected() {
        // We always keep one extra block at the end of our list because
        // blocks are added after all the conflicted transactions have
        // been filled in. Thus, the last entry should always be an empty
        // one waiting for the transactions from the next block. We pop
        // the last entry here to make sure the list we return is sane.
        assert(!blocksConnected.back().pindex);
        assert(blocksConnected.back().conflictedTxs->empty());
        blocksConnected.pop_back();
        return blocksConnected;
    }

    void NotifyEntryRemoved(CTransactionRef txRemoved, MemPoolRemovalReason reason) {
        assert(!blocksConnected.back().pindex);
        if (reason == MemPoolRemovalReason::CONFLICT) {
            blocksConnected.back().conflictedTxs->emplace_back(std::move(txRemoved));
        }
    }
};

/**
 * Connect a new block to chainActive. pblock is either nullptr or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 *
 * The block is added to connectTrace if connection succeeds.
 */
bool static ConnectTip(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexNew, const std::shared_ptr<const CBlock>& pblock, ConnectTrace& connectTrace, DisconnectedBlockTransactions &disconnectpool)
{
    assert(pindexNew->pprev == chainActive.Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    std::shared_ptr<const CBlock> pthisBlock;
    if (!pblock) {
        std::shared_ptr<CBlock> pblockNew = std::make_shared<CBlock>();
        if (!ReadBlockFromDisk(*pblockNew, pindexNew, chainparams.GetConsensus()))
            return AbortNode(state, "Failed to read block");
        pthisBlock = pblockNew;
    } else {
        pthisBlock = pblock;
    }
    const CBlock& blockConnecting = *pthisBlock;
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros(); nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    int64_t nTime4;
    int64_t nTimeAssetsFlush;
    LogPrint(BCLog::BENCH, "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * MILLI, nTimeReadFromDisk * MICRO);

    /** CLORE START */
    // Initialize sets used from removing asset entries from the mempool
    ConnectedBlockAssetData assetDataFromBlock;
    /** CLORE END */

    {
        CCoinsViewCache view(pcoinsTip);
        /** CLORE START */
        // Create the empty asset cache, that will be sent into the connect block
        // All new data will be added to the cache, and will be flushed back into passets after a successful
        // Connect Block cycle
        CAssetsCache assetCache;
        std::vector<std::pair<std::string, CNullAssetTxData>> myNullAssetData;
        /** CLORE END */

        int64_t nTimeConnectStart = GetTimeMicros();

        bool rv = ConnectBlock(blockConnecting, state, pindexNew, view, chainparams, &assetCache);
        GetMainSignals().BlockChecked(blockConnecting, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        int64_t nTimeConnectDone = GetTimeMicros();
        LogPrint(BCLog::BENCH, "  - Connect Block only time: %.2fms [%.2fs (%.2fms/blk)]\n", (nTimeConnectDone - nTimeConnectStart) * MILLI, nTimeConnectTotal * MICRO, nTimeConnectTotal * MILLI / nBlocksTotal);

        int64_t nTimeAssetsStart = GetTimeMicros();
        /** CLORE START */
        // Get the newly created assets, from the connectblock assetCache so we can remove the correct assets from the mempool
        assetDataFromBlock = {assetCache.setNewAssetsToAdd, assetCache.setNewRestrictedVerifierToAdd, assetCache.setNewRestrictedAddressToAdd, assetCache.setNewRestrictedGlobalToAdd, assetCache.setNewQualifierAddressToAdd};

        // Remove all tx hashes, that were marked as reissued script from the mapReissuedTx.
        // Without this check, you wouldn't be able to reissue for those assets again, as this maps block it
        for (auto tx : blockConnecting.vtx) {
            uint256 txHash = tx->GetHash();
            if (mapReissuedTx.count(txHash)) {
                mapReissuedAssets.erase(mapReissuedTx.at(txHash));
                mapReissuedTx.erase(txHash);
            }
        }
        int64_t nTimeAssetsEnd = GetTimeMicros(); nTimeAssetTasks += nTimeAssetsEnd - nTimeAssetsStart;
        LogPrint(BCLog::BENCH, "  - Compute Asset Tasks total: %.2fms [%.2fs (%.2fms/blk)]\n", (nTimeAssetsEnd - nTimeAssetsStart) * MILLI, nTimeAssetsEnd * MICRO, nTimeAssetsEnd * MILLI / nBlocksTotal);
        /** CLORE END */

        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint(BCLog::BENCH, "  - Connect total: %.2fms [%.2fs (%.2fms/blk)]\n", (nTime3 - nTime2) * MILLI, nTimeConnectTotal * MICRO, nTimeConnectTotal * MILLI / nBlocksTotal);
        bool flushed = view.Flush();
        assert(flushed);
        nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
        LogPrint(BCLog::BENCH, "  - Flush CLORE: %.2fms [%.2fs (%.2fms/blk)]\n", (nTime4 - nTime3) * MILLI, nTimeFlush * MICRO, nTimeFlush * MILLI / nBlocksTotal);

        /** CLORE START */
        nTimeAssetsFlush = GetTimeMicros();
        bool assetFlushed = assetCache.Flush();
        assert(assetFlushed);
        int64_t nTimeAssetFlushFinished = GetTimeMicros(); nTimeAssetFlush += nTimeAssetFlushFinished - nTimeAssetsFlush;
        LogPrint(BCLog::BENCH, "  - Flush Assets: %.2fms [%.2fs (%.2fms/blk)]\n", (nTimeAssetFlushFinished - nTimeAssetsFlush) * MILLI, nTimeAssetFlush * MICRO, nTimeAssetFlush * MILLI / nBlocksTotal);
        /** CLORE END */
    }

    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_IF_NEEDED))
        return false;
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "  - Writing chainstate: %.2fms [%.2fs (%.2fms/blk)]\n", (nTime5 - nTime4) * MILLI, nTimeChainState * MICRO, nTimeChainState * MILLI / nBlocksTotal);
    // Remove conflicting transactions from the mempool.;
    mempool.removeForBlock(blockConnecting.vtx, pindexNew->nHeight, assetDataFromBlock);
    disconnectpool.removeForBlock(blockConnecting.vtx);
    // Update chainActive & related variables.
    UpdateTip(pindexNew, chainparams);

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint(BCLog::BENCH, "  - Connect postprocess: %.2fms [%.2fs (%.2fms/blk)]\n", (nTime6 - nTime5) * MILLI, nTimePostConnect * MICRO, nTimePostConnect * MILLI / nBlocksTotal);
    LogPrint(BCLog::BENCH, "- Connect block: %.2fms [%.2fs (%.2fms/blk)]\n", (nTime6 - nTime1) * MILLI, nTimeTotal * MICRO, nTimeTotal * MILLI / nBlocksTotal);

    connectTrace.BlockConnected(pindexNew, std::move(pthisBlock));

    /** CLORE START */

    //  Determine if the new block height has any pending snapshot requests,
    //      and if so, capture a snapshot of the relevant target assets.
    if (pSnapshotRequestDb != nullptr) {
        //  Retrieve the scheduled snapshot requests
        std::set<CSnapshotRequestDBEntry> assetsToSnapshot;
        if (pSnapshotRequestDb->RetrieveSnapshotRequestsForHeight("", pindexNew->nHeight, assetsToSnapshot)) {
            //  Loop through them
            for (auto const & assetEntry : assetsToSnapshot) {
                //  Add a snapshot entry for the target asset ownership
                if (!pAssetSnapshotDb->AddAssetOwnershipSnapshot(assetEntry.assetName, pindexNew->nHeight)) {
                   LogPrint(BCLog::REWARDS, "ConnectTip: Failed to snapshot owners for '%s' at height %d!\n",
                       assetEntry.assetName.c_str(), pindexNew->nHeight);
                }
            }
        }
        else {
            LogPrint(BCLog::REWARDS, "ConnectTip: Failed to load payable Snapshot Requests at height %d!\n", pindexNew->nHeight);
        }
    }

#ifdef ENABLE_WALLET
    if (vpwallets.size()) {
        CheckRewardDistributions(vpwallets[0]);
    }
#endif
    /** CLORE END */

    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain() {
    do {
        CBlockIndex *pindexNew = nullptr;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return nullptr;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == nullptr || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex *pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either nullptr or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState& state, const CChainParams& chainparams, CBlockIndex* pindexMostWork, const std::shared_ptr<const CBlock>& pblock, bool& fInvalidFound, ConnectTrace& connectTrace)
{
    AssertLockHeld(cs_main);
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    DisconnectedBlockTransactions disconnectpool;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state, chainparams, &disconnectpool)) {
            // This is likely a fatal error, but keep the mempool consistent,
            // just in case. Only remove from the mempool in this case.
            UpdateMempoolForReorg(disconnectpool, false);
            return false;
        }
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        for (CBlockIndex *pindexConnect : reverse_iterate(vpindexToConnect)) {
            if (!ConnectTip(state, chainparams, pindexConnect, pindexConnect == pindexMostWork ? pblock : std::shared_ptr<const CBlock>(), connectTrace, disconnectpool)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible()) {
                        InvalidChainFound(vpindexToConnect.back());
                    }
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    // Make the mempool consistent with the current tip, just in case
                    // any observers try to use it before shutdown.
                    UpdateMempoolForReorg(disconnectpool, false);
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    if (fBlocksDisconnected) {
        // If any blocks were disconnected, disconnectpool may be non empty.  Add
        // any disconnected transactions back to the mempool.
        UpdateMempoolForReorg(disconnectpool, true);
    }
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

static void NotifyHeaderTip() {
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex* pindexHeaderOld = nullptr;
    CBlockIndex* pindexHeader = nullptr;
    {
        LOCK(cs_main);
        pindexHeader = pindexBestHeader;

        if (pindexHeader != pindexHeaderOld) {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify) {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
    }
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either nullptr or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState &state, const CChainParams& chainparams, std::shared_ptr<const CBlock> pblock) {
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!

    CBlockIndex *pindexMostWork = nullptr;
    CBlockIndex *pindexNewTip = nullptr;
    int nStopAtHeight = gArgs.GetArg("-stopatheight", DEFAULT_STOPATHEIGHT);
    do {
        boost::this_thread::interruption_point();
        if (ShutdownRequested())
            break;

        const CBlockIndex *pindexFork;
        bool fInitialDownload;
        {
            LOCK(cs_main);
            ConnectTrace connectTrace(mempool); // Destructed before cs_main is unlocked

            CBlockIndex *pindexOldTip = chainActive.Tip();
            if (pindexMostWork == nullptr) {
                pindexMostWork = FindMostWorkChain();
            }

            // Whether we have anything to do at all.
            if (pindexMostWork == nullptr || pindexMostWork == chainActive.Tip())
                return true;

            bool fInvalidFound = false;
            std::shared_ptr<const CBlock> nullBlockPtr;
            if (!ActivateBestChainStep(state, chainparams, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : nullBlockPtr, fInvalidFound, connectTrace))
                return false;

            if (fInvalidFound) {
                // Wipe cache, we may need another branch now.
                pindexMostWork = nullptr;
            }
            pindexNewTip = chainActive.Tip();
            pindexFork = chainActive.FindFork(pindexOldTip);
            fInitialDownload = IsInitialBlockDownload();

            for (const PerBlockConnectTrace& trace : connectTrace.GetBlocksConnected()) {
                assert(trace.pblock && trace.pindex);
                GetMainSignals().BlockConnected(trace.pblock, trace.pindex, *trace.conflictedTxs);
            }
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main

        // Notify external listeners about the new tip.
        GetMainSignals().UpdatedBlockTip(pindexNewTip, pindexFork, fInitialDownload);

        // Always notify the UI if a new block tip was connected
        if (pindexFork != pindexNewTip) {
            uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);
        }

        if (nStopAtHeight && pindexNewTip && pindexNewTip->nHeight >= nStopAtHeight) StartShutdown();
    } while (pindexNewTip != pindexMostWork);
    CheckBlockIndex(chainparams.GetConsensus());

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(chainparams, state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}


bool PreciousBlock(CValidationState& state, const CChainParams& params, CBlockIndex *pindex)
{
    {
        LOCK(cs_main);
        if (pindex->nChainWork < chainActive.Tip()->nChainWork) {
            // Nothing to do, this block is not at the tip.
            return true;
        }
        if (chainActive.Tip()->nChainWork > nLastPreciousChainwork) {
            // The chain has been extended since the last call, reset the counter.
            nBlockReverseSequenceId = -1;
        }
        nLastPreciousChainwork = chainActive.Tip()->nChainWork;
        setBlockIndexCandidates.erase(pindex);
        pindex->nSequenceId = nBlockReverseSequenceId;
        if (nBlockReverseSequenceId > std::numeric_limits<int32_t>::min()) {
            // We can't keep reducing the counter if somebody really wants to
            // call preciousblock 2**31-1 times on the same set of tips...
            nBlockReverseSequenceId--;
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && pindex->nChainTx) {
            setBlockIndexCandidates.insert(pindex);
            PruneBlockIndexCandidates();
        }
    }

    return ActivateBestChain(state, params);
}

bool InvalidateBlock(CValidationState& state, const CChainParams& chainparams, CBlockIndex *pindex)
{
    AssertLockHeld(cs_main);

    // We first disconnect backwards and then mark the blocks as invalid.
    // This prevents a case where pruned nodes may fail to invalidateblock
    // and be left unable to start as they have no tip candidates (as there
    // are no blocks that meet the "have data and are not invalid per
    // nStatus" criteria for inclusion in setBlockIndexCandidates).

    bool pindex_was_in_chain = false;
    CBlockIndex *invalid_walk_tip = chainActive.Tip();

    DisconnectedBlockTransactions disconnectpool;
    while (chainActive.Contains(pindex)) {
        pindex_was_in_chain = true;
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state, chainparams, &disconnectpool)) {
            // It's probably hopeless to try to make the mempool consistent
            // here if DisconnectTip failed, but we can try.
            UpdateMempoolForReorg(disconnectpool, false);
            return false;
        }
    }

    // Now mark the blocks we just disconnected as descendants invalid
    // (note this may not be all descendants).
    while (pindex_was_in_chain && invalid_walk_tip != pindex) {
        invalid_walk_tip->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(invalid_walk_tip);
        setBlockIndexCandidates.erase(invalid_walk_tip);
        invalid_walk_tip = invalid_walk_tip->pprev;
    }

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);
    g_failed_blocks.insert(pindex);

    // DisconnectTip will add transactions to disconnectpool; try to add these
    // back to the mempool.
    UpdateMempoolForReorg(disconnectpool, true);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    uiInterface.NotifyBlockTip(IsInitialBlockDownload(), pindex->pprev);
    return true;
}

bool ResetBlockFailureFlags(CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = nullptr;
            }
            g_failed_blocks.erase(it->second);
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != nullptr) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

static CBlockIndex* AddToBlockIndex(const CBlockHeader& block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();
    }
    pindexNew->nTimeMax = (pindexNew->pprev ? std::max(pindexNew->pprev->nTimeMax, pindexNew->nTime) : pindexNew->nTime);
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == nullptr || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
static bool ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos, const Consensus::Params& consensusParams)
{
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    if (IsWitnessEnabled(pindexNew->pprev, consensusParams)) {
        pindexNew->nStatus |= BLOCK_OPT_WITNESS;
    }
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == nullptr || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == nullptr || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

static bool FindBlockPos(CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    if ((int)nFile != nLastBlockFile) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nLastBlockFile, vinfoBlockFile[nLastBlockFile].ToString());
        }
        FlushBlockFile(!fKnown);
        nLastBlockFile = nFile;
    }

    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}

static bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

static bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, const Consensus::Params& consensusParams, bool fCheckPOW = true)
{
    // If we are checking a KAWPOW block below a know checkpoint height. We can validate the proof of work using the mix_hash
    if (fCheckPOW && block.nTime >= nKAWPOWActivationTime) {
        CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(GetParams().Checkpoints());
        if (fCheckPOW && pcheckpoint && block.nHeight <= (uint32_t)pcheckpoint->nHeight) {
           if (!CheckProofOfWork(block.GetHash(), block.nBits, consensusParams)) {
               return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed with mix_hash only check");
           }

           return true;
        }
    }

    uint256 mix_hash;
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHashFull(mix_hash), block.nBits, consensusParams)) {
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");
    }

    if (fCheckPOW && block.nTime >= nKAWPOWActivationTime) {
        if (mix_hash != block.mix_hash) {
            return state.DoS(50, false, REJECT_INVALID, "invalid-mix-hash", false, "mix_hash validity failed");
        }
    }

    return true;
}

bool CheckBlock(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams, bool fCheckPOW, bool fCheckMerkleRoot, bool fDBCheck)
{
    // These are checks that are independent of context.

    if (block.fChecked)
        return true;

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, consensusParams, fCheckPOW))
        return error("%s: Consensus::CheckBlockHeader: %s", __func__, FormatStateMessage(state));

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch");

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction");
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.
    // Note that witness malleability is checked in ContextualCheckBlock, so no
    // checks that use witness data may be performed here.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() * WITNESS_SCALE_FACTOR > GetMaxBlockWeight() || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION | SERIALIZE_TRANSACTION_NO_WITNESS) * WITNESS_SCALE_FACTOR > GetMaxBlockWeight())
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase");

    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i]->IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // Check transactions
    bool fCheckBlock = CHECK_BLOCK_TRANSACTION_TRUE;
    bool fCheckDuplicates = CHECK_DUPLICATE_TRANSACTION_TRUE;
    bool fCheckMempool = CHECK_MEMPOOL_TRANSACTION_FALSE;
    for (const auto& tx : block.vtx) {
        // We only want to check the blocks when they are added to our chain
        // We want to make sure when nodes shutdown and restart that they still
        // verify the blocks in the database correctly even if Enforce Value BIP is active
        fCheckBlock = CHECK_BLOCK_TRANSACTION_TRUE;
        if (fDBCheck){
            fCheckBlock = CHECK_BLOCK_TRANSACTION_FALSE;
        }

        if (!CheckTransaction(*tx, state, fCheckDuplicates, fCheckMempool, fCheckBlock))
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                 strprintf("Transaction check failed (tx hash %s) %s %s", tx->GetHash().ToString(),
                                           state.GetDebugMessage(), state.GetRejectReason()));
    }

    unsigned int nSigOps = 0;
    for (const auto& tx : block.vtx)
    {
        nSigOps += GetLegacySigOpCount(*tx);
    }
    if (nSigOps * WITNESS_SCALE_FACTOR > MAX_BLOCK_SIGOPS_COST)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-sigops", false, "out-of-bounds SigOpCount");

    if (fCheckPOW && fCheckMerkleRoot)
        block.fChecked = true;

    return true;
}

bool IsWitnessEnabled(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{
    return params.nSegwitEnabled;
}

bool IsWitnessEnabled(const Consensus::Params& params) {
	return params.nSegwitEnabled;
}
// Compute at which vout of the block's coinbase transaction the witness
// commitment occurs, or -1 if not found.
static int GetWitnessCommitmentIndex(const CBlock& block)
{
    int commitpos = -1;
    if (!block.vtx.empty()) {
        for (size_t o = 0; o < block.vtx[0]->vout.size(); o++) {
            if (block.vtx[0]->vout[o].scriptPubKey.size() >= 38 && block.vtx[0]->vout[o].scriptPubKey[0] == OP_RETURN && block.vtx[0]->vout[o].scriptPubKey[1] == 0x24 && block.vtx[0]->vout[o].scriptPubKey[2] == 0xaa && block.vtx[0]->vout[o].scriptPubKey[3] == 0x21 && block.vtx[0]->vout[o].scriptPubKey[4] == 0xa9 && block.vtx[0]->vout[o].scriptPubKey[5] == 0xed) {
                commitpos = o;
            }
        }
    }
    return commitpos;
}

void UpdateUncommittedBlockStructures(CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& consensusParams)
{
    int commitpos = GetWitnessCommitmentIndex(block);
    static const std::vector<unsigned char> nonce(32, 0x00);
    if (commitpos != -1 && IsWitnessEnabled(pindexPrev, consensusParams) && !block.vtx[0]->HasWitness()) {
        CMutableTransaction tx(*block.vtx[0]);
        tx.vin[0].scriptWitness.stack.resize(1);
        tx.vin[0].scriptWitness.stack[0] = nonce;
        block.vtx[0] = MakeTransactionRef(std::move(tx));
    }
}

std::vector<unsigned char> GenerateCoinbaseCommitment(CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& consensusParams)
{
    std::vector<unsigned char> commitment;
    int commitpos = GetWitnessCommitmentIndex(block);
    std::vector<unsigned char> ret(32, 0x00);
    if(consensusParams.nSegwitEnabled) { // if (consensusParams.vDeployments[Consensus::DEPLOYMENT_SEGWIT].nTimeout != 0) {
		if (commitpos == -1) {
			uint256 witnessroot = BlockWitnessMerkleRoot(block, nullptr);
			CHash256().Write(witnessroot.begin(), 32).Write(ret.data(), 32).Finalize(witnessroot.begin());
			CTxOut out;
			out.nValue = 0;
			out.scriptPubKey.resize(38);
			out.scriptPubKey[0] = OP_RETURN;
			out.scriptPubKey[1] = 0x24;
			out.scriptPubKey[2] = 0xaa;
			out.scriptPubKey[3] = 0x21;
			out.scriptPubKey[4] = 0xa9;
			out.scriptPubKey[5] = 0xed;
			memcpy(&out.scriptPubKey[6], witnessroot.begin(), 32);
			commitment = std::vector<unsigned char>(out.scriptPubKey.begin(), out.scriptPubKey.end());
			CMutableTransaction tx(*block.vtx[0]);
			tx.vout.push_back(out);
			block.vtx[0] = MakeTransactionRef(std::move(tx));
		}
    }
	UpdateUncommittedBlockStructures(block, pindexPrev, consensusParams);
    return commitment;
}

/** Context-dependent validity checks.
 *  By "context", we mean only the previous block headers, but not the UTXO
 *  set; UTXO-related validity checks are done in ConnectBlock(). */
static bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, const CChainParams& params, const CBlockIndex* pindexPrev, int64_t nAdjustedTime)
{
    assert(pindexPrev != nullptr);
    const int nHeight = pindexPrev->nHeight + 1;

    //If this is a reorg, check that it is not too deep
    int nMaxReorgDepth = gArgs.GetArg("-maxreorg", GetParams().MaxReorganizationDepth());
    int nMinReorgPeers = gArgs.GetArg("-minreorgpeers", GetParams().MinReorganizationPeers());
    int nMinReorgAge = gArgs.GetArg("-minreorgage", GetParams().MinReorganizationAge());
    bool fGreaterThanMaxReorg = (chainActive.Height() - (nHeight - 1)) >= nMaxReorgDepth;
    if (fGreaterThanMaxReorg && g_connman) {
        int nCurrentNodeCount = g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
        bool bIsCurrentChainCaughtUp = (GetTime() - chainActive.Tip()->nTime) <= nMinReorgAge;
        if ((nCurrentNodeCount >= nMinReorgPeers) && bIsCurrentChainCaughtUp)
            return state.DoS(10,
                             error("%s: forked chain older than max reorganization depth (height %d), with connections (count %d), and caught up with active chain (%s)",
                                   __func__, nHeight, nCurrentNodeCount, bIsCurrentChainCaughtUp ? "true" : "false"),
                             REJECT_MAXREORGDEPTH, "bad-fork-prior-to-maxreorgdepth");
    }

    // Check proof of work
    const Consensus::Params& consensusParams = params.GetConsensus();
    if (block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
        return state.DoS(100, false, REJECT_INVALID, "bad-diffbits", false, "incorrect proof of work");

    // Check against checkpoints
    if (fCheckpointsEnabled) {
        // Don't accept any forks from the main chain prior to last checkpoint.
        // GetLastCheckpoint finds the last checkpoint in MapCheckpoints that's in our
        // MapBlockIndex.
        CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(params.Checkpoints());
        if (pcheckpoint && nHeight < pcheckpoint->nHeight)
            return state.DoS(100, error("%s: forked chain older than last checkpoint (height %d)", __func__, nHeight), REJECT_CHECKPOINT, "bad-fork-prior-to-checkpoint");
    }

    // Check timestamp against prev
    if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast())
        return state.Invalid(false, REJECT_INVALID, "time-too-old", "block's timestamp is too early");

    // Check timestamp
    if (IsDGWActive(pindexPrev->nHeight+1))
    {
        if (block.GetBlockTime() > nAdjustedTime + MAX_FUTURE_BLOCK_TIME_DGW)
            return state.Invalid(false, REJECT_INVALID, "time-too-new", "block timestamp too far in the future");
    }
    else
    {
        if (block.GetBlockTime() > nAdjustedTime + MAX_FUTURE_BLOCK_TIME)
            return state.Invalid(false, REJECT_INVALID, "time-too-new", "block timestamp too far in the future");
    }

    // Reject outdated version blocks when 95% (75% on testnet) of the network has upgraded:
    // check for version 2, 3 and 4 upgrades
    // if((block.nVersion < 2 && nHeight >= consensusParams.BIP34Height) ||
    //    (block.nVersion < 3 && nHeight >= consensusParams.BIP66Height) ||
    //    (block.nVersion < 4 && nHeight >= consensusParams.BIP65Height))
    //         return state.Invalid(false, REJECT_OBSOLETE, strprintf("bad-version(0x%08x)", block.nVersion),
    //                              strprintf("rejected nVersion=0x%08x block", block.nVersion));

    // Reject outdated version blocks once assets are active.
    if (AreAssetsDeployed() && block.nVersion < VERSIONBITS_TOP_BITS_ASSETS)
        return state.Invalid(false, REJECT_OBSOLETE, strprintf("bad-version(0x%08x)", block.nVersion), strprintf("rejected nVersion=0x%08x block", block.nVersion));

    return true;
}

static bool ContextualCheckBlock(const CBlock& block, CValidationState& state, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev, CAssetsCache* assetCache)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;

    // Start enforcing BIP113 (Median Time Past) using versionbits logic.
    int nLockTimeFlags = 0;
    if(consensusParams.nCSVEnabled == true) {
    		nLockTimeFlags |= LOCKTIME_MEDIAN_TIME_PAST;
    }

    int64_t nLockTimeCutoff = ((nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST) && pindexPrev)
                              ? pindexPrev->GetMedianTimePast()
                              : block.GetBlockTime();

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx) {
        if (!IsFinalTx(*tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false, "non-final transaction");
        }
    }

    // Enforce rule that the coinbase starts with serialized block height
    CScript expect = CScript() << nHeight;

    if (nHeight >= consensusParams.BIP34LockedIn)
    {
		if (block.vtx[0]->vin[0].scriptSig.size() < expect.size() ||
			!std::equal(expect.begin(), expect.end(), block.vtx[0]->vin[0].scriptSig.begin())) {
			return state.DoS(100, false, REJECT_INVALID, "bad-cb-height", false, "block height mismatch in coinbase");
		}
    }
    // Validation for witness commitments.
    // * We compute the witness hash (which is the hash including witnesses) of all the block's transactions, except the
    //   coinbase (where 0x0000....0000 is used instead).
    // * The coinbase scriptWitness is a stack of a single 32-byte vector, containing a witness nonce (unconstrained).
    // * We build a merkle tree with all those witness hashes as leaves (similar to the hashMerkleRoot in the block header).
    // * There must be at least one output whose scriptPubKey is a single 36-byte push, the first 4 bytes of which are
    //   {0xaa, 0x21, 0xa9, 0xed}, and the following 32 bytes are SHA256^2(witness root, witness nonce). In case there are
    //   multiple, the last one is used.
    bool fHaveWitness = false;
    if(IsWitnessEnabled(consensusParams)) {
		int commitpos = GetWitnessCommitmentIndex(block);
		if (commitpos != -1) {
			bool malleated = false;
			uint256 hashWitness = BlockWitnessMerkleRoot(block, &malleated);
			// The malleation check is ignored; as the transaction tree itself
			// already does not permit it, it is impossible to trigger in the
			// witness tree.
			if (block.vtx[0]->vin[0].scriptWitness.stack.size() != 1 || block.vtx[0]->vin[0].scriptWitness.stack[0].size() != 32) {
				return state.DoS(100, false, REJECT_INVALID, "bad-witness-nonce-size", true, strprintf("%s : invalid witness nonce size", __func__));
			}
			CHash256().Write(hashWitness.begin(), 32).Write(&block.vtx[0]->vin[0].scriptWitness.stack[0][0], 32).Finalize(hashWitness.begin());
			if (memcmp(hashWitness.begin(), &block.vtx[0]->vout[commitpos].scriptPubKey[6], 32)) {
				return state.DoS(100, false, REJECT_INVALID, "bad-witness-merkle-match", true, strprintf("%s : witness merkle commitment mismatch", __func__));
			}
			fHaveWitness = true;
		}
    }
    // No witness data is allowed in blocks that don't commit to witness data, as this would otherwise leave room for spam
    if (!fHaveWitness) {
      for (const auto& tx : block.vtx) {
            if (tx->HasWitness()) {
                return state.DoS(100, false, REJECT_INVALID, "unexpected-witness", true, strprintf("%s : unexpected witness data found", __func__));
            }
        }
    }

    // After the coinbase witness nonce and commitment are verified,
    // we can check if the block weight passes (before we've checked the
    // coinbase witness, it would be possible for the weight to be too
    // large by filling up the coinbase witness, which doesn't change
    // the block hash, so we couldn't mark the block as permanently
    // failed).
    if (GetBlockWeight(block) > GetMaxBlockWeight()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-weight", false, strprintf("%s : weight limit failed", __func__));
    }

    return true;
}

static bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex *pindex = nullptr;
    if (hash != chainparams.GetConsensus().hashGenesisBlock) {

        if (miSelf != mapBlockIndex.end()) {
            // Block header is already known.
            pindex = miSelf->second;
            if (ppindex)
                *ppindex = pindex;
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return state.Invalid(error("%s: block %s is marked invalid", __func__, hash.ToString()), 0, "duplicate");
            return true;
        }

        if (!CheckBlockHeader(block, state, chainparams.GetConsensus()))
            return error("%s: Consensus::CheckBlockHeader: %s, %s", __func__, hash.ToString(), FormatStateMessage(state));

        // Get prev block index
        CBlockIndex* pindexPrev = nullptr;
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
            return state.DoS(10, error("%s: prev block not found", __func__), 0, "prev-blk-not-found");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK)
            return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
        if (!ContextualCheckBlockHeader(block, state, chainparams, pindexPrev, GetAdjustedTime()))
            return error("%s: Consensus::ContextualCheckBlockHeader: %s, %s", __func__, hash.ToString(), FormatStateMessage(state));

        if (!pindexPrev->IsValid(BLOCK_VALID_SCRIPTS)) {
            for (const CBlockIndex* failedit : g_failed_blocks) {
                if (pindexPrev->GetAncestor(failedit->nHeight) == failedit) {
                    assert(failedit->nStatus & BLOCK_FAILED_VALID);
                    CBlockIndex* invalid_walk = pindexPrev;
                    while (invalid_walk != failedit) {
                        invalid_walk->nStatus |= BLOCK_FAILED_CHILD;
                        setDirtyBlockIndex.insert(invalid_walk);
                        invalid_walk = invalid_walk->pprev;
                    }
                    return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
                }
            }
        }
    }
    if (pindex == nullptr)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    CheckBlockIndex(chainparams.GetConsensus());

    return true;
}

// Exposed wrapper for AcceptBlockHeader
bool ProcessNewBlockHeaders(const std::vector<CBlockHeader>& headers, CValidationState& state, const CChainParams& chainparams, const CBlockIndex** ppindex, CBlockHeader *first_invalid)
{
    if (first_invalid != nullptr) first_invalid->SetNull();
    {
        LOCK(cs_main);
        for (const CBlockHeader& header : headers) {
            CBlockIndex *pindex = nullptr; // Use a temp pindex instead of ppindex to avoid a const_cast
            if (!AcceptBlockHeader(header, state, chainparams, &pindex)) {
                if (first_invalid) *first_invalid = header;
                return false;
            }
            if (ppindex) {
                *ppindex = pindex;
            }
        }
    }
    NotifyHeaderTip();
    return true;
}

/** Store block on disk. If dbp is non-nullptr, the file is known to already reside on disk */
static bool AcceptBlock(const std::shared_ptr<const CBlock>& pblock, CValidationState& state, const CChainParams& chainparams, CBlockIndex** ppindex, bool fRequested, const CDiskBlockPos* dbp, bool* fNewBlock, bool fFromLoad = false)
{
    const CBlock& block = *pblock;

    if (fNewBlock) *fNewBlock = false;
    AssertLockHeld(cs_main);

    CBlockIndex *pindexDummy = nullptr;
    CBlockIndex *&pindex = ppindex ? *ppindex : pindexDummy;

    if (!AcceptBlockHeader(block, state, chainparams, &pindex))
        return false;

    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + MIN_BLOCKS_TO_KEEP));
    // TODO: Decouple this function from the block download logic by removing fRequested
    // This requires some new chain data structure to efficiently look up if a
    // block is in a chain leading to a candidate for best tip, despite not
    // being such a candidate itself.

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    if (fAlreadyHave) return true;
    if (!fRequested) {  // If we didn't ask for it:
        if (pindex->nTx != 0) return true;  // This is a previously-processed block that was pruned
        if (!fHasMoreWork) return true;     // Don't process less-work chains
        if (fTooFarAhead) return true;      // Block height is too high

        // Protect against DoS attacks from low-work chains.
        // If our tip is behind, a peer could try to send us
        // low-work blocks on a fake chain that we would never
        // request; don't process these.
        if (pindex->nChainWork < nMinimumChainWork) return true;
    }

    if (fNewBlock) *fNewBlock = true;

    auto currentActiveAssetCache = GetCurrentAssetCache();
    // Dont force the CheckBlock asset duplciates when checking from this state
    if (!CheckBlock(block, state, chainparams.GetConsensus(), true, true) ||
        !ContextualCheckBlock(block, state, chainparams.GetConsensus(), pindex->pprev, currentActiveAssetCache)) {
        if (fFromLoad && state.GetRejectReason() == "bad-txns-transfer-asset-bad-deserialize") {
            // keep going, we are only loading blocks from database
            CValidationState new_state;
            state = new_state;
        } else {
            if (state.IsInvalid() && !state.CorruptionPossible()) {
                pindex->nStatus |= BLOCK_FAILED_VALID;
                setDirtyBlockIndex.insert(pindex);
            }
            return error("%s: %s", __func__, FormatStateMessage(state));
        }
    }

    // Header is valid/has work, merkle tree and segwit merkle tree are good...RELAY NOW
    // (but if it does not build on our best tip, let the SendMessages loop relay it)
    if (!IsInitialBlockDownload() && chainActive.Tip() == pindex->pprev)
        GetMainSignals().NewPoWValidBlock(pindex, pblock);

    int nHeight = pindex->nHeight;

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != nullptr)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, nHeight, block.GetBlockTime(), dbp != nullptr))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == nullptr)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE); // we just allocated more disk space for block files

    return true;
}

bool ProcessNewBlock(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool *fNewBlock)
{
    {
        CBlockIndex *pindex = nullptr;
        if (fNewBlock) *fNewBlock = false;
        CValidationState state;

        // Ensure that CheckBlock() passes before calling AcceptBlock, as
        // belt-and-suspenders.
        bool ret = CheckBlock(*pblock, state, chainparams.GetConsensus(), true, true);

        LOCK(cs_main);

        if (ret) {
            // Store to disk
            ret = AcceptBlock(pblock, state, chainparams, &pindex, fForceProcessing, nullptr, fNewBlock);
        }

        CheckBlockIndex(chainparams.GetConsensus());
        if (!ret) {
            GetMainSignals().BlockChecked(*pblock, state);
            return error("%s: AcceptBlock FAILED (%s)", __func__, state.GetDebugMessage());
        }
    }
    NotifyHeaderTip();

    CValidationState state; // Only used to report errors, not invalidity - ignore it
    if (!ActivateBestChain(state, chainparams, pblock))
        return error("%s: ActivateBestChain failed", __func__);

    return true;
}

bool TestBlockValidity(CValidationState& state, const CChainParams& chainparams, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev && pindexPrev == chainActive.Tip());
    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    /** CLORE START */
    CAssetsCache assetCache = *GetCurrentAssetCache();
    /** CLORE END */

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, chainparams, pindexPrev, GetAdjustedTime()))
        return error("%s: Consensus::ContextualCheckBlockHeader: %s", __func__, FormatStateMessage(state));
    if (!CheckBlock(block, state, chainparams.GetConsensus(), fCheckPOW, fCheckMerkleRoot))
        return error("%s: Consensus::CheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ContextualCheckBlock(block, state, chainparams.GetConsensus(), pindexPrev, &assetCache))
        return error("%s: Consensus::ContextualCheckBlock: %s", __func__, FormatStateMessage(state));
    if (!ConnectBlock(block, state, &indexDummy, viewNew, chainparams, &assetCache, true)) /** CLORE START */ /*Add asset to function */ /** CLORE END*/
        return error("%s: Consensus::ConnectBlock: %s", __func__, FormatStateMessage(state));
    assert(state.IsValid());

    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    LOCK(cs_LastBlockFile);

    uint64_t retval = 0;
    for (const CBlockFileInfo &file : vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

/* Prune a block file (modify associated database entries)*/
void PruneOneBlockFile(const int fileNumber)
{
    LOCK(cs_LastBlockFile);

    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it) {
        CBlockIndex* pindex = it->second;
        if (pindex->nFile == fileNumber) {
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);

            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second) {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator _it = range.first;
                range.first++;
                if (_it->second == pindex) {
                    mapBlocksUnlinked.erase(_it);
                }
            }
        }
    }

    vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
}


void UnlinkPrunedFiles(const std::set<int>& setFilesToPrune)
{
    for (std::set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        fs::remove(GetBlockPosFilename(pos, "blk"));
        fs::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files to delete based on height specified by user with RPC command pruneblockchain */
static void FindFilesToPruneManual(std::set<int>& setFilesToPrune, int nManualPruneHeight)
{
    assert(fPruneMode && nManualPruneHeight > 0);

    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == nullptr)
        return;

    // last block to prune is the lesser of (user-specified height, MIN_BLOCKS_TO_KEEP from the tip)
    unsigned int nLastBlockWeCanPrune = std::min((unsigned)nManualPruneHeight, chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP);
    int count=0;
    for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
        if (vinfoBlockFile[fileNumber].nSize == 0 || vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
            continue;
        PruneOneBlockFile(fileNumber);
        setFilesToPrune.insert(fileNumber);
        count++;
    }
    LogPrintf("Prune (Manual): prune_height=%d removed %d blk/rev pairs\n", nLastBlockWeCanPrune, count);
}

/* This function is called from the RPC code for pruneblockchain */
void PruneBlockFilesManual(int nManualPruneHeight)
{
    CValidationState state;
    const CChainParams& chainparams = GetParams();
    FlushStateToDisk(chainparams, state, FLUSH_STATE_NONE, nManualPruneHeight);
}

/**
 * Prune block and undo files (blk???.dat and undo???.dat) so that the disk space used is less than a user-defined target.
 * The user sets the target (in MB) on the command line or in config file.  This will be run on startup and whenever new
 * space is allocated in a block or undo file, staying below the target. Changing back to unpruned requires a reindex
 * (which in this case means the blockchain must be re-downloaded.)
 *
 * Pruning functions are called from FlushStateToDisk when the global fCheckForPruning flag has been set.
 * Block and undo files are deleted in lock-step (when blk00003.dat is deleted, so is rev00003.dat.)
 * Pruning cannot take place until the longest chain is at least a certain length (100000 on mainnet, 1000 on testnet, 1000 on regtest).
 * Pruning will never delete a block within a defined distance (currently 288) from the active chain's tip.
 * The block index is updated by unsetting HAVE_DATA and HAVE_UNDO for any blocks that were stored in the deleted files.
 * A db flag records the fact that at least some block files have been pruned.
 *
 * @param[out]   setFilesToPrune   The set of file indices that can be unlinked will be returned
 */
static void FindFilesToPrune(std::set<int>& setFilesToPrune, uint64_t nPruneAfterHeight)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == nullptr || nPruneTarget == 0) {
        return;
    }
    if ((uint64_t)chainActive.Tip()->nHeight <= nPruneAfterHeight) {
        return;
    }

    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint(BCLog::PRUNE, "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
           nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
           ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
           nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

static FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return nullptr;
    fs::path path = GetBlockPosFilename(pos, prefix);
    fs::create_directories(path.parent_path());
    FILE* file = fsbridge::fopen(path, "rb+");
    if (!file && !fReadOnly)
        file = fsbridge::fopen(path, "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return nullptr;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

/** Open an undo file (rev?????.dat) */
static FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

fs::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return nullptr;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool static LoadBlockIndexDB(const CChainParams& chainparams)
{
    if (!pblocktree->LoadBlockIndexGuts(chainparams.GetConsensus(), InsertBlockIndex))
        return false;

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    std::vector<std::pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for (const std::pair<uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const std::pair<int, CBlockIndex*>& item : vSortedByHeight)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        pindex->nTimeMax = (pindex->pprev ? std::max(pindex->pprev->nTimeMax, pindex->nTime) : pindex->nTime);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (!(pindex->nStatus & BLOCK_FAILED_MASK) && pindex->pprev && (pindex->pprev->nStatus & BLOCK_FAILED_MASK)) {
            pindex->nStatus |= BLOCK_FAILED_CHILD;
            setDirtyBlockIndex.insert(pindex);
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == nullptr))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == nullptr || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const std::pair<uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    if(fReindexing) fReindex = true;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

    pblocktree->ReadFlag("assetindex", fAssetIndex);
    LogPrintf("%s: asset index %s\n", __func__, fAssetIndex ? "enabled" : "disabled");

    // Check whether we have an address index
    pblocktree->ReadFlag("addressindex", fAddressIndex);
    LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

    // Check whether we have a timestamp index
    pblocktree->ReadFlag("timestampindex", fTimestampIndex);
    LogPrintf("%s: timestamp index %s\n", __func__, fTimestampIndex ? "enabled" : "disabled");

    // Check whether we have a spent index
    pblocktree->ReadFlag("spentindex", fSpentIndex);
    LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");
    return true;
}

bool LoadChainTip(const CChainParams& chainparams)
{
    if (chainActive.Tip() && chainActive.Tip()->GetBlockHash() == pcoinsTip->GetBestBlock()) return true;

    if (pcoinsTip->GetBestBlock().IsNull() && mapBlockIndex.size() == 1) {
        // In case we just added the genesis block, connect it now, so
        // that we always have a chainActive.Tip() when we return.
        LogPrintf("%s: Connecting genesis block...\n", __func__);
        CValidationState state;
        if (!ActivateBestChain(state, chainparams)) {
            return false;
        }
    }

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return false;
    chainActive.SetTip(it->second);

    PruneBlockIndexCandidates();

    LogPrintf("Loaded best chain: hashBestChain=%s height=%d date=%s progress=%f\n",
        chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
        GuessVerificationProgress(chainparams.TxData(), chainActive.Tip()));
    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0, false);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100, false);
}

bool CVerifyDB::VerifyDB(const CChainParams& chainparams, CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == nullptr || chainActive.Tip()->pprev == nullptr)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0 || nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = nullptr;
    int nGoodTransactions = 0;
    CValidationState state;
    int reportDone = 0;

    auto currentActiveAssetCache = GetCurrentAssetCache();
    CAssetsCache assetCache(*currentActiveAssetCache);
    LogPrintf("[0%%]...");
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        int percentageDone = std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100))));
        if (reportDone < percentageDone/10) {
            // report every 10% step
            LogPrintf("[%d%%]...", percentageDone);
            reportDone = percentageDone/10;
        }
        uiInterface.ShowProgress(_("Verifying blocks..."), percentageDone, false);
        if (pindex->nHeight < chainActive.Height()-nCheckDepth)
            break;
        if (fPruneMode && !(pindex->nStatus & BLOCK_HAVE_DATA)) {
            // If pruning, only go back as far as we have data.
            LogPrintf("VerifyDB(): block verification stopping at height %d (pruning, no data)\n", pindex->nHeight);
            break;
        }
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        bool fCheckPoW = true;
        bool fCheckMerkleRoot = true;
        bool fDBCheck = true;
        if (nCheckLevel >= 1 && !CheckBlock(block, state, chainparams.GetConsensus(), fCheckPoW, fCheckMerkleRoot, fDBCheck)) // fCheckAssetDuplicate set to false, because we don't want to fail because the asset exists in our database, when loading blocks from our asset databse
            return error("%s: *** found bad block at %d, hash=%s (%s)\n", __func__,
                         pindex->nHeight, pindex->GetBlockHash().ToString(), FormatStateMessage(state));
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            assert(coins.GetBestBlock() == pindex->GetBlockHash());
            DisconnectResult res = DisconnectBlock(block, pindex, coins, &assetCache, true, false);
            if (res == DISCONNECT_FAILED) {
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (res == DISCONNECT_UNCLEAN) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else {
                nGoodTransactions += block.vtx.size();
            }
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))), false);
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, chainparams.GetConsensus()))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins, chainparams, &assetCache, false, true))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("[DONE].\n");
    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

/** Apply the effects of a block on the utxo cache, ignoring that it may already have been applied. */
static bool RollforwardBlock(const CBlockIndex* pindex, CCoinsViewCache& inputs, const CChainParams& params, CAssetsCache* assetsCache = nullptr)
{
    // TODO: merge with ConnectBlock
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, params.GetConsensus())) {
        return error("ReplayBlock(): ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
    }

    for (const CTransactionRef& tx : block.vtx) {
        if (!tx->IsCoinBase()) {
            for (const CTxIn &txin : tx->vin) {
                inputs.SpendCoin(txin.prevout, nullptr, assetsCache);
            }
        }
        // Pass check = true as every addition may be an overwrite.
        AddCoins(inputs, *tx, pindex->nHeight, pindex->GetBlockHash(), true, assetsCache);
    }
    return true;
}

bool ReplayBlocks(const CChainParams& params, CCoinsView* view)
{
    LOCK(cs_main);

    CCoinsViewCache cache(view);
    auto currentActiveAssetCache = GetCurrentAssetCache();
    CAssetsCache assetsCache(*currentActiveAssetCache);

    std::vector<uint256> hashHeads = view->GetHeadBlocks();
    if (hashHeads.empty()) return true; // We're already in a consistent state.
    if (hashHeads.size() != 2) return error("ReplayBlocks(): unknown inconsistent state");

    uiInterface.ShowProgress(_("Replaying blocks..."), 0, false);
    LogPrintf("Replaying blocks\n");

    const CBlockIndex* pindexOld = nullptr;  // Old tip during the interrupted flush.
    const CBlockIndex* pindexNew;            // New tip during the interrupted flush.
    const CBlockIndex* pindexFork = nullptr; // Latest block common to both the old and the new tip.

    if (mapBlockIndex.count(hashHeads[0]) == 0) {
        return error("ReplayBlocks(): reorganization to unknown block requested");
    }
    pindexNew = mapBlockIndex[hashHeads[0]];

    if (!hashHeads[1].IsNull()) { // The old tip is allowed to be 0, indicating it's the first flush.
        if (mapBlockIndex.count(hashHeads[1]) == 0) {
            return error("ReplayBlocks(): reorganization from unknown block requested");
        }
        pindexOld = mapBlockIndex[hashHeads[1]];
        pindexFork = LastCommonAncestor(pindexOld, pindexNew);
        assert(pindexFork != nullptr);
    }

    // Rollback along the old branch.
    while (pindexOld != pindexFork) {
        if (pindexOld->nHeight > 0) { // Never disconnect the genesis block.
            CBlock block;
            if (!ReadBlockFromDisk(block, pindexOld, params.GetConsensus())) {
                return error("RollbackBlock(): ReadBlockFromDisk() failed at %d, hash=%s", pindexOld->nHeight, pindexOld->GetBlockHash().ToString());
            }
            LogPrintf("Rolling back %s (%i)\n", pindexOld->GetBlockHash().ToString(), pindexOld->nHeight);
            DisconnectResult res = DisconnectBlock(block, pindexOld, cache, &assetsCache);
            if (res == DISCONNECT_FAILED) {
                return error("RollbackBlock(): DisconnectBlock failed at %d, hash=%s", pindexOld->nHeight, pindexOld->GetBlockHash().ToString());
            }
            // If DISCONNECT_UNCLEAN is returned, it means a non-existing UTXO was deleted, or an existing UTXO was
            // overwritten. It corresponds to cases where the block-to-be-disconnect never had all its operations
            // applied to the UTXO set. However, as both writing a UTXO and deleting a UTXO are idempotent operations,
            // the result is still a version of the UTXO set with the effects of that block undone.
        }
        pindexOld = pindexOld->pprev;
    }

    // Roll forward from the forking point to the new tip.
    int nForkHeight = pindexFork ? pindexFork->nHeight : 0;
    for (int nHeight = nForkHeight + 1; nHeight <= pindexNew->nHeight; ++nHeight) {
        const CBlockIndex* pindex = pindexNew->GetAncestor(nHeight);
        LogPrintf("Rolling forward %s (%i)\n", pindex->GetBlockHash().ToString(), nHeight);
        if (!RollforwardBlock(pindex, cache, params)) return false;
    }

    cache.SetBestBlock(pindexNew->GetBlockHash());
    cache.Flush();
    assetsCache.Flush();
    uiInterface.ShowProgress("", 100, false);
    return true;
}

bool RewindBlockIndex(const CChainParams& params)
{
    LOCK(cs_main);

    // Note that during -reindex-chainstate we are called with an empty chainActive!

    int nHeight = 1;
    while (nHeight <= chainActive.Height()) {
        if (IsWitnessEnabled(chainActive[nHeight - 1], params.GetConsensus()) && !(chainActive[nHeight]->nStatus & BLOCK_OPT_WITNESS)) {
            break;
        }
        nHeight++;
    }

    // nHeight is now the height of the first insufficiently-validated block, or tipheight + 1
    CValidationState state;
    CBlockIndex* pindex = chainActive.Tip();
    while (chainActive.Height() >= nHeight) {
        if (fPruneMode && !(chainActive.Tip()->nStatus & BLOCK_HAVE_DATA)) {
            // If pruning, don't try rewinding past the HAVE_DATA point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain).
            break;
        }
        if (!DisconnectTip(state, params, nullptr)) {
            return error("RewindBlockIndex: unable to disconnect block at height %i", pindex->nHeight);
        }
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(params, state, FLUSH_STATE_PERIODIC))
            return false;
    }

    // Reduce validity flag and have-data flags.
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted.
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        CBlockIndex* pindexIter = it->second;

        // Note: If we encounter an insufficiently validated block that
        // is on chainActive, it must be because we are a pruning node, and
        // this block or some successor doesn't HAVE_DATA, so we were unable to
        // rewind all the way.  Blocks remaining on chainActive at this point
        // must not have their validity reduced.
        if (IsWitnessEnabled(pindexIter->pprev, params.GetConsensus()) && !(pindexIter->nStatus & BLOCK_OPT_WITNESS) && !chainActive.Contains(pindexIter)) {
            // Reduce validity
            pindexIter->nStatus = std::min<unsigned int>(pindexIter->nStatus & BLOCK_VALID_MASK, BLOCK_VALID_TREE) | (pindexIter->nStatus & ~BLOCK_VALID_MASK);
            // Remove have-data flags.
            pindexIter->nStatus &= ~(BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);
            // Remove storage location.
            pindexIter->nFile = 0;
            pindexIter->nDataPos = 0;
            pindexIter->nUndoPos = 0;
            // Remove various other things
            pindexIter->nTx = 0;
            pindexIter->nChainTx = 0;
            pindexIter->nSequenceId = 0;
            // Make sure it gets written.
            setDirtyBlockIndex.insert(pindexIter);
            // Update indexes
            setBlockIndexCandidates.erase(pindexIter);
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> ret = mapBlocksUnlinked.equal_range(pindexIter->pprev);
            while (ret.first != ret.second) {
                if (ret.first->second == pindexIter) {
                    mapBlocksUnlinked.erase(ret.first++);
                } else {
                    ++ret.first;
                }
            }
        } else if (pindexIter->IsValid(BLOCK_VALID_TRANSACTIONS) && pindexIter->nChainTx) {
            setBlockIndexCandidates.insert(pindexIter);
        }
    }

    if (chainActive.Tip() != nullptr) {
        // We can't prune block index candidates based on our tip if we have
        // no tip due to chainActive being empty!
        PruneBlockIndexCandidates();

        CheckBlockIndex(params.GetConsensus());

        // FlushStateToDisk can possibly read chainActive. Be conservative
        // and skip it here, we're about to -reindex-chainstate anyway, so
        // it'll get called a bunch real soon.
        if (!FlushStateToDisk(params, state, FLUSH_STATE_ALWAYS)) {
            return false;
        }
    }

    return true;
}

// May NOT be used after any connections are up as much
// of the peer-processing logic assumes a consistent
// block index state
void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(nullptr);
    pindexBestInvalid = nullptr;
    pindexBestHeader = nullptr;
    mempool.clear();
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    setDirtyBlockIndex.clear();
    g_failed_blocks.clear();
    setDirtyFileInfo.clear();
    versionbitscache.Clear();
    for (int b = 0; b < VERSIONBITS_NUM_BITS; b++) {
        warningcache[b].clear();
    }

    for (BlockMap::value_type& entry : mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;
}

bool LoadBlockIndex(const CChainParams& chainparams)
{
    // Load block index from databases
    bool needs_init = fReindex;
    if (!fReindex) {
        bool ret = LoadBlockIndexDB(chainparams);
        if (!ret) return false;
        needs_init = mapBlockIndex.empty();
    }

    if (needs_init) {
        // Everything here is for *new* reindex/DBs. Thus, though
        // LoadBlockIndexDB may have set fReindex if we shut down
        // mid-reindex previously, we don't check fReindex and
        // instead only check it prior to LoadBlockIndexDB to set
        // needs_init.

        LogPrintf("Initializing databases...\n");

        // Use the provided setting for -txindex in the new database
        fTxIndex = gArgs.GetBoolArg("-txindex", DEFAULT_TXINDEX);
        pblocktree->WriteFlag("txindex", fTxIndex);
        LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");

        // Use the provided setting for -assetindex in the new database
        fAssetIndex = gArgs.GetBoolArg("-assetindex", DEFAULT_ASSETINDEX);
        pblocktree->WriteFlag("assetindex", fAssetIndex);
        LogPrintf("%s: asset index %s\n", __func__, fAssetIndex ? "enabled" : "disabled");

        // Use the provided setting for -addressindex in the new database
        fAddressIndex = gArgs.GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
        pblocktree->WriteFlag("addressindex", fAddressIndex);
        LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

        // Use the provided setting for -timestampindex in the new database
        fTimestampIndex = gArgs.GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);
        pblocktree->WriteFlag("timestampindex", fTimestampIndex);
        LogPrintf("%s: timestamp index %s\n", __func__, fTimestampIndex ? "enabled" : "disabled");

        // Use the provided setting for -spentindex in the new database
        fSpentIndex = gArgs.GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
        pblocktree->WriteFlag("spentindex", fSpentIndex);
        LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");

    }
    return true;
}

bool LoadGenesisBlock(const CChainParams& chainparams)
{
    LOCK(cs_main);

    // Check whether we're already initialized by checking for genesis in
    // mapBlockIndex. Note that we can't use chainActive here, since it is
    // set based on the coins db, not the block index db, which is the only
    // thing loaded at this point.
    if (mapBlockIndex.count(chainparams.GenesisBlock().GetHash()))
        return true;

    try {
        CBlock &block = const_cast<CBlock&>(chainparams.GenesisBlock());
        // Start new block file
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        CValidationState state;
        if (!FindBlockPos(state, blockPos, nBlockSize+8, 0, block.GetBlockTime()))
            return error("%s: FindBlockPos failed", __func__);
        if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            return error("%s: writing genesis block to disk failed", __func__);
        CBlockIndex *pindex = AddToBlockIndex(block);
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos, chainparams.GetConsensus()))
            return error("%s: genesis block not accepted", __func__);
    } catch (const std::runtime_error& e) {
        return error("%s: failed to write genesis block: %s", __func__, e.what());
    }

    return true;
}

bool LoadExternalBlockFile(const CChainParams& chainparams, FILE* fileIn, CDiskBlockPos *dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2*GetMaxBlockSerializedSize(), GetMaxBlockSerializedSize()+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[CMessageHeader::MESSAGE_START_SIZE];
                blkdat.FindByte(chainparams.MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, chainparams.MessageStart(), CMessageHeader::MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > GetMaxBlockSerializedSize())
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>();
                CBlock& block = *pblock;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrint(BCLog::REINDEX, "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                            block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0) {
                    LOCK(cs_main);
                    CValidationState state;
                    if (AcceptBlock(pblock, state, chainparams, nullptr, true, dbp, nullptr, true)) {
                        nLoaded++;
                    }
                    if (state.IsError()) {
                        break;
                    }
                } else if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrint(BCLog::REINDEX, "Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Activate the genesis block so normal node progress can continue
                if (hash == chainparams.GetConsensus().hashGenesisBlock) {
                    CValidationState state;
                    if (!ActivateBestChain(state, chainparams)) {
                        break;
                    }
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        std::shared_ptr<CBlock> pblockrecursive = std::make_shared<CBlock>();
                        if (ReadBlockFromDisk(*pblockrecursive, it->second, chainparams.GetConsensus()))
                        {
                            LogPrint(BCLog::REINDEX, "%s: Processing out of order child %s of %s\n", __func__, pblockrecursive->GetHash().ToString(),
                                    head.ToString());
                            LOCK(cs_main);
                            CValidationState dummy;
                            if (AcceptBlock(pblockrecursive, dummy, chainparams, nullptr, true, &it->second, nullptr, true))
                            {
                                nLoaded++;
                                queue.push_back(pblockrecursive->GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void static CheckBlockIndex(const Consensus::Params& consensusParams)
{
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*,CBlockIndex*> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(nullptr);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent nullptr.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = nullptr; // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = nullptr; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNeverProcessed = nullptr; // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex* pindexFirstNotTreeValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotTransactionsValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = nullptr; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != nullptr) {
        nNodes++;
        if (pindexFirstInvalid == nullptr && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == nullptr && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == nullptr && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTreeValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotTransactionsValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotChainValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != nullptr && pindexFirstNotScriptsValid == nullptr && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == nullptr) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId <= 0);  // nSequenceId can't be set positive for blocks that aren't linked (negative is used for preciousblock)
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA) assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO) assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != nullptr) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != nullptr) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == nullptr || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == nullptr); // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == nullptr); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == nullptr); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == nullptr); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == nullptr) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == nullptr) {
            if (pindexFirstInvalid == nullptr) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (pindexFirstMissing == nullptr || pindex == chainActive.Tip()) {
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != nullptr && pindexFirstInvalid == nullptr) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA)) assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (pindexFirstMissing == nullptr) assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == nullptr && pindexFirstMissing != nullptr) {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0) {
                if (pindexFirstInvalid == nullptr) {
                    assert(foundInUnlinked);
                }
            }
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = nullptr;
            if (pindex == pindexFirstMissing) pindexFirstMissing = nullptr;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = nullptr;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = nullptr;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = nullptr;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = nullptr;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = nullptr;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

std::string CBlockFileInfo::ToString() const
{
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}

CBlockFileInfo* GetBlockFileInfo(size_t n)
{
    LOCK(cs_LastBlockFile);

    return &vinfoBlockFile.at(n);
}

ThresholdState VersionBitsTipState(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsState(chainActive.Tip(), params, pos, versionbitscache);
}

BIP9Stats VersionBitsTipStatistics(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsStatistics(chainActive.Tip(), params, pos);
}

int VersionBitsTipStateSinceHeight(const Consensus::Params& params, Consensus::DeploymentPos pos)
{
    LOCK(cs_main);
    return VersionBitsStateSinceHeight(chainActive.Tip(), params, pos, versionbitscache);
}

static const uint64_t MEMPOOL_DUMP_VERSION = 1;

bool LoadMempool(void)
{
    const CChainParams& chainparams = GetParams();
    int64_t nExpiryTimeout = gArgs.GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60;
    FILE* filestr = fsbridge::fopen(GetDataDir() / "mempool.dat", "rb");
    CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
    if (file.IsNull()) {
        LogPrintf("Failed to open mempool file from disk. Continuing anyway.\n");
        return false;
    }

    int64_t count = 0;
    int64_t expired = 0;
    int64_t failed = 0;
    int64_t already_there = 0;
    int64_t nNow = GetTime();

    try {
        uint64_t version;
        file >> version;
        if (version != MEMPOOL_DUMP_VERSION) {
            return false;
        }
        uint64_t num;
        file >> num;
        while (num--) {
            CTransactionRef tx;
            int64_t nTime;
            int64_t nFeeDelta;
            file >> tx;
            file >> nTime;
            file >> nFeeDelta;

            CAmount amountdelta = nFeeDelta;
            if (amountdelta) {
                mempool.PrioritiseTransaction(tx->GetHash(), amountdelta);
            }
            CValidationState state;
            if (nTime + nExpiryTimeout > nNow) {
                LOCK(cs_main);
                AcceptToMemoryPoolWithTime(chainparams, mempool, state, tx, nullptr /* pfMissingInputs */, nTime,
                                           nullptr /* plTxnReplaced */, false /* bypass_limits */, 0 /* nAbsurdFee */,
                                           false /* test_accept */);
                if (state.IsValid()) {
                    ++count;
                } else {
                    // mempool may contain the transaction already, e.g. from
                    // wallet(s) having loaded it while we were processing
                    // mempool transactions; consider these as valid, instead of
                    // failed, but mark them as 'already there'
                    if (mempool.exists(tx->GetHash())) {
                        ++already_there;
                    } else {
                        ++failed;
                    }
                }
            } else {
                ++expired;
            }
            if (ShutdownRequested())
                return false;
        }
        std::map<uint256, CAmount> mapDeltas;
        file >> mapDeltas;

        for (const auto& i : mapDeltas) {
            mempool.PrioritiseTransaction(i.first, i.second);
        }
    } catch (const std::exception& e) {
        LogPrintf("Failed to deserialize mempool data on disk: %s. Continuing anyway.\n", e.what());
        return false;
    }

    LogPrintf("Imported mempool transactions from disk: %i succeeded, %i failed, %i expired, %i already there\n", count, failed, expired, already_there);
    return true;
}

bool DumpMempool(void)
{
    int64_t start = GetTimeMicros();

    std::map<uint256, CAmount> mapDeltas;
    std::vector<TxMempoolInfo> vinfo;

    {
        LOCK(mempool.cs);
        for (const auto &i : mempool.mapDeltas) {
            mapDeltas[i.first] = i.second;
        }
        vinfo = mempool.infoAll();
    }

    int64_t mid = GetTimeMicros();

    try {
        FILE* filestr = fsbridge::fopen(GetDataDir() / "mempool.dat.new", "wb");
        if (!filestr) {
            return false;
        }

        CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);

        uint64_t version = MEMPOOL_DUMP_VERSION;
        file << version;

        file << (uint64_t)vinfo.size();
        for (const auto& i : vinfo) {
            file << *(i.tx);
            file << (int64_t)i.nTime;
            file << (int64_t)i.nFeeDelta;
            mapDeltas.erase(i.tx->GetHash());
        }

        file << mapDeltas;
        FileCommit(file.Get());
        file.fclose();
        RenameOver(GetDataDir() / "mempool.dat.new", GetDataDir() / "mempool.dat");
        int64_t last = GetTimeMicros();
        LogPrintf("Dumped mempool: %gs to copy, %gs to dump\n", (mid-start)*MICRO, (last-mid)*MICRO);
    } catch (const std::exception& e) {
        LogPrintf("Failed to dump mempool: %s. Continuing anyway.\n", e.what());
        return false;
    }
    return true;
}

//! Guess how far we are in the verification process at the given block index
double GuessVerificationProgress(const ChainTxData& data, CBlockIndex *pindex) {
    if (pindex == nullptr)
        return 0.0;

    int64_t nNow = time(nullptr);

    double fTxTotal;

    if (pindex->nChainTx <= data.nTxCount) {
        fTxTotal = data.nTxCount + (nNow - data.nTime) * data.dTxRate;
    } else {
        fTxTotal = pindex->nChainTx + (nNow - pindex->GetBlockTime()) * data.dTxRate;
    }

    return pindex->nChainTx / fTxTotal;
}

/** CLORE START */

// Only used by test framework
void SetEnforcedValues(bool value) {
    fEnforcedValuesIsActive = value;
}

bool AreEnforcedValuesDeployed()
{
    if (fEnforcedValuesIsActive)
        return true;

    const ThresholdState thresholdState = VersionBitsTipState(GetParams().GetConsensus(), Consensus::DEPLOYMENT_ENFORCE_VALUE);
    if (thresholdState == THRESHOLD_ACTIVE || thresholdState == THRESHOLD_LOCKED_IN)
        fEnforcedValuesIsActive = true;

    return fEnforcedValuesIsActive;
}

bool AreCoinbaseCheckAssetsDeployed()
{
    if (fCheckCoinbaseAssetsIsActive)
        return true;

    const ThresholdState thresholdState = VersionBitsTipState(GetParams().GetConsensus(), Consensus::DEPLOYMENT_COINBASE_ASSETS);
    if (thresholdState == THRESHOLD_ACTIVE)
        fCheckCoinbaseAssetsIsActive = true;

    return fCheckCoinbaseAssetsIsActive;
}

bool AreAssetsDeployed()
{

    if (fAssetsIsActive)
        return true;

    const ThresholdState thresholdState = VersionBitsTipState(GetParams().GetConsensus(), Consensus::DEPLOYMENT_ASSETS);
    if (thresholdState == THRESHOLD_ACTIVE)
        fAssetsIsActive = true;

    return fAssetsIsActive;
}

bool IsRip5Active()
{
    if (fRip5IsActive)
        return true;

    const ThresholdState thresholdState = VersionBitsTipState(GetParams().GetConsensus(), Consensus::DEPLOYMENT_MSG_REST_ASSETS);
    if (thresholdState == THRESHOLD_ACTIVE)
        fRip5IsActive = true;

    return fRip5IsActive;
}

bool AreMessagesDeployed() {

    return IsRip5Active();
}

bool AreTransferScriptsSizeDeployed() {

    if (fTransferScriptIsActive)
        return true;

    const ThresholdState thresholdState = VersionBitsTipState(GetParams().GetConsensus(), Consensus::DEPLOYMENT_TRANSFER_SCRIPT_SIZE);
    if (thresholdState == THRESHOLD_ACTIVE)
        fTransferScriptIsActive = true;

    return fTransferScriptIsActive;
}

bool AreRestrictedAssetsDeployed() {

    return IsRip5Active();
}

bool IsDGWActive(unsigned int nBlockNumber) {
    return nBlockNumber >= GetParams().DGWActivationBlock();
}

bool IsMessagingActive(unsigned int nBlockNumber) {
    if (GetParams().MessagingActivationBlock()) {
        return nBlockNumber > GetParams().MessagingActivationBlock();
    } else {
        return AreMessagesDeployed();
    }
}

bool IsRestrictedActive(unsigned int nBlockNumber)
{
    if (GetParams().RestrictedActivationBlock()) {
        return nBlockNumber > GetParams().RestrictedActivationBlock();
    } else {
        return AreRestrictedAssetsDeployed();
    }
}

CAssetsCache* GetCurrentAssetCache()
{
    return passets;
}
/** CLORE END */

class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();
    }
} instance_of_cmaincleanup;
