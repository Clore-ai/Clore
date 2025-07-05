// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Hash Algorithm Tests - EXTREMELY CRITICAL
 * 
 * Tests X16R and X16RV2 mining hash algorithms ensuring correct implementation for proof-of-work validation and mining operations.
 * Essential for mining infrastructure, block validation, and maintaining network security during the PoW phase before validator transition.
 * 
 * IMPACT SUMMARY:
 * - Security: Validates mining hash algorithms preventing consensus failures and ensuring network security during PoW mining phase
 * - Performance: Ensures efficient hash computation for mining operations and block validation critical for network timing
 * - Users: Enables mining functionality and block validation ensuring proper network participation and rewards
 * - Business: Critical for mining pools, validator transition timing, and maintaining network integrity during consensus evolution
 */

#include <arith_uint256.h>
#include <hash.h>
#include <utilstrencodings.h>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc == 3)
    {
        std::vector<unsigned char> rawHeader = ParseHex(argv[1]);
        int whichalgo = strtol(argv[2], nullptr, 10);

        std::vector<unsigned char> rawHashPrevBlock(rawHeader.begin() + 4, rawHeader.begin() + 36);
        uint256 hashPrevBlock(rawHashPrevBlock);

        if (whichalgo == 1)
            std::cout << HashX16R(rawHeader.data(), rawHeader.data() + 80, hashPrevBlock).GetHex();
        else if (whichalgo == 2)
            std::cout << HashX16RV2(rawHeader.data(), rawHeader.data() + 80, hashPrevBlock).GetHex();
        else
        {
            std::cerr << "Usage: test_clore_hash blockHex algorithm (1=x16r, 2=x16rv2)" << std::endl;
            return 1;
        }
    }

    else
    {
        std::cerr << "Usage: test_clore_hash blockHex algorithm (1=x16r, 2=x16rv2)" << std::endl;
        return 1;
    }

    return 0;
}