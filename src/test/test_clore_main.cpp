// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Test Framework Main - EXTREMELY CRITICAL
 * 
 * Core test framework initialization and shutdown handlers providing foundation for all CLORE unit testing infrastructure.
 * Essential testing infrastructure enabling comprehensive validation of all blockchain components, consensus rules, and security features.
 * 
 * IMPACT SUMMARY:
 * - Security: Provides clean test environment preventing cross-test contamination and ensuring reliable security validation
 * - Performance: Enables efficient test execution and proper resource management for comprehensive testing coverage
 * - Users: Supports development quality assurance ensuring reliable wallet, node, and network functionality
 * - Business: Critical foundation for validator testing, exchange integration validation, and maintaining code quality standards
 */

#define BOOST_TEST_MODULE Clore Test Suite

#include "net.h"

#include <boost/test/unit_test.hpp>

std::unique_ptr<CConnman> g_connman;

[[noreturn]] void Shutdown(void *parg)
{
    std::exit(EXIT_SUCCESS);
}

[[noreturn]] void StartShutdown()
{
    std::exit(EXIT_SUCCESS);
}

bool ShutdownRequested()
{
    return false;
}
