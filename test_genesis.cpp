#include <iostream>
#include <string>
#include "chainparams.h"
#include "primitives/block.h"

int main()
{
    // Create the same genesis block as regtest
    CBlock genesis = CreateGenesisBlock(1524179366, 1, 0x207fffff, 4, 5000 * COIN);
    uint256 actualHash = genesis.GetX16RHash();
    std::string expectedHash = "0x0b2c703dc93bb63a36c4e33b85be4855ddbca2ac951a7a0a29b8de0408200a3c";

    std::cout << "Expected: " << expectedHash << std::endl;
    std::cout << "Actual:   " << actualHash.GetHex() << std::endl;

    return 0;
}