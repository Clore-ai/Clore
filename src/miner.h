// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Copyright (c) 2015-2022 The PIVX Core developers
// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef CLORE_MINER_H
#define CLORE_MINER_H

#include "chainstate.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "txmempool.h"
#include "validation.h"
#include "wallet/wallet.h"
#include <sync.h>

#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/optional.hpp>
#include <boost/thread.hpp>
#include <memory>
#include <stdint.h>

#include "consensus/consensus.h"
#include "consensus/tx_verify.h"
#include "policy/policy.h"

class CBlockIndex;
class CChainParams;
class CReserveKey;
class CScript;
class CWallet;
class CStakeableOutput;
class CTxMemPool;
class CChainState;

namespace Consensus
{
struct Params;
};

static const bool DEFAULT_PRINTPRIORITY = false;

struct CBlockTemplate {
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOpsCost;
    std::vector<unsigned char> vchCoinbaseCommitment;
};

// Container for tracking updates to ancestor feerate as we include (parent)
// transactions in a block
struct CTxMemPoolModifiedEntry {
    explicit CTxMemPoolModifiedEntry(CTxMemPool::txiter entry)
    {
        iter = entry;
        nSizeWithAncestors = entry->GetSizeWithAncestors();
        nModFeesWithAncestors = entry->GetModFeesWithAncestors();
        nSigOpCostWithAncestors = entry->GetSigOpCostWithAncestors();
    }

    int64_t GetModifiedFee() const { return iter->GetModifiedFee(); }
    uint64_t GetSizeWithAncestors() const { return nSizeWithAncestors; }
    CAmount GetModFeesWithAncestors() const { return nModFeesWithAncestors; }
    size_t GetTxSize() const { return iter->GetTxSize(); }
    const CTransaction& GetTx() const { return iter->GetTx(); }

    CTxMemPool::txiter iter;
    uint64_t nSizeWithAncestors;
    CAmount nModFeesWithAncestors;
    int64_t nSigOpCostWithAncestors;
};

/** Modified CTxMemPool::setEntries to keep track of cached txiter's */
struct modifiedentry_set {
    typedef CTxMemPoolModifiedEntry value_type;
    struct cmp {
        bool operator()(const CTxMemPoolModifiedEntry& a, const CTxMemPoolModifiedEntry& b) const
        {
            return a.iter->GetTx().GetHash() < b.iter->GetTx().GetHash();
        }
    };
    typedef std::set<CTxMemPoolModifiedEntry, cmp> setEntries;
    setEntries entries;

    typedef setEntries::iterator iterator;
    typedef setEntries::const_iterator const_iterator;
    typedef setEntries::size_type size_type;

    modifiedentry_set() {}
    modifiedentry_set(const CTxMemPool::setEntries& entries)
    {
        for (CTxMemPool::setEntries::iterator it = entries.begin(); it != entries.end(); ++it) {
            this->entries.insert(CTxMemPoolModifiedEntry(*it));
        }
    }
    size_type size() const { return entries.size(); }
    bool empty() const { return entries.empty(); }
    iterator begin() { return entries.begin(); }
    iterator end() { return entries.end(); }
    const_iterator begin() const { return entries.begin(); }
    const_iterator end() const { return entries.end(); }
    iterator find(const CTxMemPoolModifiedEntry& entry) { return entries.find(entry); }
    const_iterator find(const CTxMemPoolModifiedEntry& entry) const { return entries.find(entry); }
    void insert(const CTxMemPoolModifiedEntry& entry) { entries.insert(entry); }
    void erase(const CTxMemPoolModifiedEntry& entry) { entries.erase(entry); }
    void clear() { entries.clear(); }
};

/** Comparator for CTxMemPool::txiter objects.
 *  It simply compares the internal memory address of the CTxMemPoolEntry object
 *  pointed to. This means it has no meaning, and is only useful for using them
 *  as key in other indexes.
 */
struct CompareCTxMemPoolIter {
    bool operator()(const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) const
    {
        return &(*a) < &(*b);
    }
};

struct modifiedentry_iter {
    typedef CTxMemPool::txiter result_type;
    result_type operator()(const CTxMemPoolModifiedEntry& entry) const
    {
        return entry.iter;
    }
};

// This matches the calculation in CompareTxMemPoolEntryByAncestorFee,
// except operating on CTxMemPoolModifiedEntry.
// TODO: refactor to avoid duplication of this logic.
struct CompareModifiedEntry {
    bool operator()(const CTxMemPoolModifiedEntry& a, const CTxMemPoolModifiedEntry& b) const
    {
        double f1 = (double)a.nModFeesWithAncestors * b.nSizeWithAncestors;
        double f2 = (double)b.nModFeesWithAncestors * a.nSizeWithAncestors;
        if (f1 == f2) {
            return CTxMemPool::CompareIteratorByHash()(a.iter, b.iter);
        }
        return f1 > f2;
    }
};

// A comparator that sorts transactions based on number of ancestors.
// This is sufficient to sort an ancestor package in an order that is valid
// to appear in a block.
struct CompareTxIterByAncestorCount {
    bool operator()(const CTxMemPool::txiter& a, const CTxMemPool::txiter& b) const
    {
        if ((*a).GetCountWithAncestors() != (*b).GetCountWithAncestors())
            return (*a).GetCountWithAncestors() < (*b).GetCountWithAncestors();
        return CTxMemPool::CompareIteratorByHash()(a, b);
    }

    // Overload for comparing with integers
    bool operator()(const CTxMemPool::txiter& a, const int& b) const
    {
        return (*a).GetCountWithAncestors() < static_cast<uint64_t>(b);
    }

    bool operator()(const int& a, const CTxMemPool::txiter& b) const
    {
        return static_cast<uint64_t>(a) < (*b).GetCountWithAncestors();
    }

    // Template overload for generic iterators
    template <typename Iterator>
    bool operator()(const Iterator& a, const Iterator& b) const
    {
        return (*(*a)).GetCountWithAncestors() < (*(*b)).GetCountWithAncestors();
    }
};

typedef boost::multi_index_container<
    CTxMemPoolModifiedEntry,
    boost::multi_index::indexed_by<
        boost::multi_index::ordered_unique<
            modifiedentry_iter,
            CompareCTxMemPoolIter>,
        // sorted by modified ancestor fee rate
        boost::multi_index::ordered_non_unique<
            // Reuse same tag from CTxMemPool's similar index
            boost::multi_index::tag<ancestor_score>,
            boost::multi_index::identity<CTxMemPoolModifiedEntry>,
            CompareModifiedEntry>>>
    indexed_modified_transaction_set;

typedef indexed_modified_transaction_set::nth_index<0>::type::iterator modtxiter;
typedef indexed_modified_transaction_set::index<ancestor_score>::type::iterator modtxscoreiter;

struct update_for_parent_inclusion {
    explicit update_for_parent_inclusion(CTxMemPool::txiter it) : iter(it) {}

    void operator()(CTxMemPoolModifiedEntry& e)
    {
        e.nModFeesWithAncestors -= iter->GetFee();
        e.nSizeWithAncestors -= iter->GetTxSize();
        e.nSigOpCostWithAncestors -= iter->GetSigOpCost();
    }

    CTxMemPool::txiter iter;
};

/** Generate a new block, without valid proof-of-work */
class BlockAssembler
{
private:
    // Chain context for the block
    const CChainParams& chainparams;
    const CTxMemPool& m_mempool;

    // Variables used for addScoreTxs and addPriorityTxs
    uint64_t nConsecutiveFailed{0};
    int64_t nLastBlockTx{0};
    int64_t nLastBlockSize{0};
    int64_t nLastBlockWeight{0};
    int64_t medianTimePast{0};

    // Block assembly state
    CTxMemPool::setEntries inBlock;
    std::unique_ptr<CBlockTemplate> pblocktemplate;
    CBlock* pblock{nullptr};

    // Block limits
    uint64_t nBlockMaxWeight;
    CFeeRate blockMinFeeRate;
    uint64_t nBlockMaxSize;
    bool fIncludeWitness;

    // Current block state
    uint64_t nBlockWeight;
    uint64_t nBlockSize;
    int64_t nBlockSigOpsCost;
    uint64_t nBlockTx;
    CAmount nFees;
    int nHeight;
    int64_t nLockTimeCutoff;

    // Helper functions for addToBlock
    bool TestPackage(uint64_t packageSize, int64_t packageSigOpsCost) const;
    bool TestPackageTransactions(const CTxMemPool::setEntries& package);
    bool SkipMapTxEntry(CTxMemPool::txiter it, indexed_modified_transaction_set& mapModifiedTx, CTxMemPool::setEntries& failedTx) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);
    void SortForBlock(const CTxMemPool::setEntries& package, CTxMemPool::txiter entry, std::vector<CTxMemPool::txiter>& sortedEntries);
    void addScoreTxs() EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);
    void addPriorityTxs(bool fPriorityBlock) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);
    void addPackageTxs(int& nPackagesSelected, int& nDescendantsUpdated) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);
    int UpdatePackagesForAdded(const CTxMemPool::setEntries& alreadyAdded, indexed_modified_transaction_set& mapModifiedTx) EXCLUSIVE_LOCKS_REQUIRED(m_mempool.cs);

public:
    struct Options {
        Options();
        size_t nBlockMaxWeight;
        size_t nBlockMaxSize;
        bool fIncludeWitness;
        CFeeRate blockMinFeeRate;
    };

    explicit BlockAssembler(const CChainParams& params, const CTxMemPool& mempool);
    explicit BlockAssembler(const CChainParams& params, const Options& options);
    /** Construct a new block template with coinbase to scriptPubKeyIn */
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx = true);
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, bool fMineWitnessTx, bool fIncludeWitness);
#ifdef ENABLE_WALLET
    std::unique_ptr<CBlockTemplate> CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake, std::vector<CStakeableOutput>* availableCoins);
#endif

private:
    // utility functions
    /** Clear the block's state and prepare for assembling a new block */
    void resetBlock();

    /** Add a tx to the block */
    void AddToBlock(CTxMemPool::txiter iter);


    /** Remove confirmed (inBlock) entries from given set */
    void onlyUnconfirmed(CTxMemPool::setEntries& testSet);
    /** Test if a new package would "fit" in the block */
    bool TestForBlock(CTxMemPool::txiter iter);
};

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, const CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
int64_t UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);

/** Process a found block - submit to network */
void ProcessBlockFound(const CBlock* pblock, const CChainParams& chainparams);

/** GetNumCores() declared in util.h */

#ifdef ENABLE_WALLET
/** Run the miner threads */
void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads);
/** Generate a new PoW block, without valid proof-of-work */
std::unique_ptr<CBlockTemplate> CreateNewBlockWithKey(std::shared_ptr<CReserveScript>& reservekey, CWallet* pwallet);
std::unique_ptr<CBlockTemplate> CreateNewBlockWithScript(const CScript& coinbaseScript, CWallet* pwallet);

void CloreMiner(const CChainParams& chainparams);
void ThreadStakeMinter();

// Proof of Stake functions
bool SolveProofOfStake(CBlock* pblock, CBlockIndex* pindexPrev, CWallet* pwallet, std::vector<CStakeableOutput>* availableCoins = nullptr);
#endif // ENABLE_WALLET

// CLORE-specific mining functions
int GenerateClores(bool fGenerate, int nThreads, const CChainParams& chainparams);

extern double dHashesPerSec;
extern int64_t nHPSTimerStart;
extern bool fGenerateBitcoins;
extern bool fStakeableCoins;


#endif // CLORE_MINER_H
