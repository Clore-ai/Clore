// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2022 The CLORE.AI
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"

#include "init.h"
#include "primitives/block.h"
#include "scheduler.h"
#include "sync.h"
#include "util.h"

#include <list>
#include <atomic>
#include <functional>

#include <boost/signals2/signal.hpp>

// Fixing Boost 1.73 compile errors
#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

struct MainSignalsInstance {
    boost::signals2::signal<void (const CBlockIndex *, const CBlockIndex *, bool fInitialDownload)> UpdatedBlockTip;
    boost::signals2::signal<void (const CTransactionRef &)> TransactionAddedToMempool;
    boost::signals2::signal<void (const std::shared_ptr<const CBlock> &, const CBlockIndex *pindex, const std::vector<CTransactionRef>&)> BlockConnected;
    boost::signals2::signal<void (const std::shared_ptr<const CBlock> &)> BlockDisconnected;
    boost::signals2::signal<void (const CBlockLocator &)> SetBestChain;
    boost::signals2::signal<void (const uint256 &)> Inventory;
    boost::signals2::signal<void (int64_t nBestBlockTime, CConnman* connman)> Broadcast;
    boost::signals2::signal<void (const CBlock&, const CValidationState&)> BlockChecked;
    boost::signals2::signal<void (const CBlockIndex *, const std::shared_ptr<const CBlock>&)> NewPoWValidBlock;
    boost::signals2::signal<void (const uint256 &)> BlockFound;
    boost::signals2::signal<void (const CMessage &)> NewAssetMessage;
//    boost::signals2::signal<void (std::shared_ptr<CReserveScript>&)> ScriptForMining;
    
    // We are not allowed to assume the scheduler only runs in one thread,
    // but must ensure all callbacks happen in-order, so we end up creating
    // our own queue here :(
    SingleThreadedSchedulerClient m_schedulerClient;

    explicit MainSignalsInstance(CScheduler *pscheduler) : m_schedulerClient(pscheduler) {}
};

static CMainSignals g_signals;

void CMainSignals::RegisterBackgroundSignalScheduler(CScheduler& scheduler) {
    assert(!m_internals);
    m_internals.reset(new MainSignalsInstance(&scheduler));
}

void CMainSignals::UnregisterBackgroundSignalScheduler() {
    m_internals.reset(nullptr);
}

void CMainSignals::FlushBackgroundCallbacks() {
    m_internals->m_schedulerClient.EmptyQueue();
}

CMainSignals& GetMainSignals()
{
    return g_signals;
}

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
    if (!g_signals.m_internals) return;
    
    // Store the connection objects when registering
    static std::map<CValidationInterface*, std::vector<boost::signals2::connection>> connections;
    
    // Clear any existing connections
    auto it = connections.find(pwalletIn);
    if (it != connections.end()) {
        for (auto& conn : it->second) {
            conn.disconnect();
        }
        connections.erase(it);
    }
    
    // Create new connections using lambda functions instead of std::bind
    std::vector<boost::signals2::connection> new_connections;
    
    new_connections.push_back(g_signals.m_internals->UpdatedBlockTip.connect(
        [pwalletIn](const CBlockIndex* p1, const CBlockIndex* p2, bool f3) {
            pwalletIn->UpdatedBlockTip(p1, p2, f3);
        }));
    new_connections.push_back(g_signals.m_internals->TransactionAddedToMempool.connect(
        [pwalletIn](const CTransactionRef& tx) {
            pwalletIn->TransactionAddedToMempool(tx);
        }));
    new_connections.push_back(g_signals.m_internals->BlockConnected.connect(
        [pwalletIn](const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex, const std::vector<CTransactionRef>& vtxConflicted) {
            pwalletIn->BlockConnected(block, pindex, vtxConflicted);
        }));
    new_connections.push_back(g_signals.m_internals->BlockDisconnected.connect(
        [pwalletIn](const std::shared_ptr<const CBlock>& block) {
            pwalletIn->BlockDisconnected(block);
        }));
    new_connections.push_back(g_signals.m_internals->SetBestChain.connect(
        [pwalletIn](const CBlockLocator& locator) {
            pwalletIn->SetBestChain(locator);
        }));
    new_connections.push_back(g_signals.m_internals->Inventory.connect(
        [pwalletIn](const uint256& hash) {
            pwalletIn->Inventory(hash);
        }));
    new_connections.push_back(g_signals.m_internals->Broadcast.connect(
        [pwalletIn](int64_t nBestBlockTime, CConnman* connman) {
            pwalletIn->ResendWalletTransactions(nBestBlockTime, connman);
        }));
    new_connections.push_back(g_signals.m_internals->BlockChecked.connect(
        [pwalletIn](const CBlock& block, const CValidationState& state) {
            pwalletIn->BlockChecked(block, state);
        }));
    new_connections.push_back(g_signals.m_internals->NewPoWValidBlock.connect(
        [pwalletIn](const CBlockIndex* pindex, const std::shared_ptr<const CBlock>& block) {
            pwalletIn->NewPoWValidBlock(pindex, block);
        }));
    new_connections.push_back(g_signals.m_internals->BlockFound.connect(
        [pwalletIn](const uint256& hash) {
            pwalletIn->BlockFound(hash);
        }));
    new_connections.push_back(g_signals.m_internals->NewAssetMessage.connect(
        [pwalletIn](const CMessage& message) {
            pwalletIn->NewAssetMessage(message);
        }));
    
    // Store the new connections
    connections[pwalletIn] = std::move(new_connections);
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {
    if (!g_signals.m_internals) return;
    
    // Store the connection objects when registering
    static std::map<CValidationInterface*, std::vector<boost::signals2::connection>> connections;
    
    // Disconnect all connections for this interface
    auto it = connections.find(pwalletIn);
    if (it != connections.end()) {
        for (auto& conn : it->second) {
            conn.disconnect();
        }
        connections.erase(it);
    }
}

void UnregisterAllValidationInterfaces() {
    g_signals.m_internals->BlockChecked.disconnect_all_slots();
    g_signals.m_internals->Broadcast.disconnect_all_slots();
    g_signals.m_internals->Inventory.disconnect_all_slots();
    g_signals.m_internals->SetBestChain.disconnect_all_slots();
    g_signals.m_internals->TransactionAddedToMempool.disconnect_all_slots();
    g_signals.m_internals->BlockConnected.disconnect_all_slots();
    g_signals.m_internals->BlockDisconnected.disconnect_all_slots();
    g_signals.m_internals->UpdatedBlockTip.disconnect_all_slots();
    g_signals.m_internals->NewPoWValidBlock.disconnect_all_slots();
    g_signals.m_internals->BlockFound.disconnect_all_slots();
    g_signals.m_internals->NewAssetMessage.disconnect_all_slots();
//    g_signals.m_internals->ScriptForMining.disconnect_all_slots();
}

void CMainSignals::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload) {
    m_internals->UpdatedBlockTip(pindexNew, pindexFork, fInitialDownload);
}

void CMainSignals::TransactionAddedToMempool(const CTransactionRef &ptx) {
    m_internals->TransactionAddedToMempool(ptx);
}

void CMainSignals::BlockConnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindex, const std::vector<CTransactionRef>& vtxConflicted) {
    m_internals->BlockConnected(pblock, pindex, vtxConflicted);
}

void CMainSignals::BlockDisconnected(const std::shared_ptr<const CBlock> &pblock) {
    m_internals->BlockDisconnected(pblock);
}

void CMainSignals::SetBestChain(const CBlockLocator &locator) {
    m_internals->SetBestChain(locator);
}

void CMainSignals::Inventory(const uint256 &hash) {
    m_internals->Inventory(hash);
}

void CMainSignals::Broadcast(int64_t nBestBlockTime, CConnman* connman) {
    m_internals->Broadcast(nBestBlockTime, connman);
}

void CMainSignals::BlockChecked(const CBlock& block, const CValidationState& state) {
    m_internals->BlockChecked(block, state);
}

void CMainSignals::NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &block) {
    m_internals->NewPoWValidBlock(pindex, block);
}

void CMainSignals::BlockFound(const uint256 &hash) {
    m_internals->BlockFound(hash);
}

void CMainSignals::NewAssetMessage(const CMessage& message) {
    m_internals->NewAssetMessage(message);
}
