// Copyright (c) 2024 The CLORE Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Minimal logging utilities for consensus library

#include <string>
#include <iostream>

// Minimal LogPrintStr implementation for consensus library
void LogPrintStr(const std::string& str)
{
    // For consensus library, just output to stderr
    std::cerr << str << std::flush;
} 