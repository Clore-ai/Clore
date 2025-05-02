// Copyright (c) 2013-2016 The Bitcoin Core developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>
#include "hash.h"
#include "crypto/common.h"
#include "crypto/hmac_sha512.h"
#include "pubkey.h"
#include "util.h"

#include <crypto/ethash/include/ethash/progpow.hpp>

//TODO remove these
double algoHashTotal[16];
int algoHashHits[16];

inline uint32_t ROTL32(uint32_t x, int8_t r)
{
    return (x << r) | (x >> (32 - r));
}

unsigned int MurmurHash3(unsigned int nHashSeed, const std::vector<unsigned char>& vDataToHash)
{
    // The following is MurmurHash3 (x86_32), see http://code.google.com/p/smhasher/source/browse/trunk/MurmurHash3.cpp
    uint32_t h1 = nHashSeed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const int nblocks = vDataToHash.size() / 4;

    //----------
    // body
    const uint8_t* blocks = vDataToHash.data();

    for (int i = 0; i < nblocks; ++i) {
        uint32_t k1 = ReadLE32(blocks + i*4);

        k1 *= c1;
        k1 = ROTL32(k1, 15);
        k1 *= c2;

        h1 ^= k1;
        h1 = ROTL32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }

    //----------
    // tail
    const uint8_t* tail = vDataToHash.data() + nblocks * 4;

    uint32_t k1 = 0;

    switch (vDataToHash.size() & 3) {
        case 3:
            k1 ^= tail[2] << 16;
        case 2:
            k1 ^= tail[1] << 8;
        case 1:
            k1 ^= tail[0];
            k1 *= c1;
            k1 = ROTL32(k1, 15);
            k1 *= c2;
            h1 ^= k1;
    }

    //----------
    // finalization
    h1 ^= vDataToHash.size();
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;

    return h1;
}

void BIP32Hash(const ChainCode &chainCode, unsigned int nChild, unsigned char header, const unsigned char data[32], unsigned char output[64])
{
    unsigned char num[4];
    num[0] = (nChild >> 24) & 0xFF;
    num[1] = (nChild >> 16) & 0xFF;
    num[2] = (nChild >>  8) & 0xFF;
    num[3] = (nChild >>  0) & 0xFF;
    CHMAC_SHA512(chainCode.begin(), chainCode.size()).Write(&header, 1).Write(data, 32).Write(num, 4).Finalize(output);
}

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND do { \
    v0 += v1; v1 = ROTL(v1, 13); v1 ^= v0; \
    v0 = ROTL(v0, 32); \
    v2 += v3; v3 = ROTL(v3, 16); v3 ^= v2; \
    v0 += v3; v3 = ROTL(v3, 21); v3 ^= v0; \
    v2 += v1; v1 = ROTL(v1, 17); v1 ^= v2; \
    v2 = ROTL(v2, 32); \
} while (0)

CSipHasher::CSipHasher(uint64_t k0, uint64_t k1)
{
    v[0] = 0x736f6d6570736575ULL ^ k0;
    v[1] = 0x646f72616e646f6dULL ^ k1;
    v[2] = 0x6c7967656e657261ULL ^ k0;
    v[3] = 0x7465646279746573ULL ^ k1;
    count = 0;
    tmp = 0;
}

CSipHasher& CSipHasher::Write(uint64_t data)
{
    uint64_t v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];

    assert(count % 8 == 0);

    v3 ^= data;
    SIPROUND;
    SIPROUND;
    v0 ^= data;

    v[0] = v0;
    v[1] = v1;
    v[2] = v2;
    v[3] = v3;

    count += 8;
    return *this;
}

CSipHasher& CSipHasher::Write(const unsigned char* data, size_t size)
{
    uint64_t v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];
    uint64_t t = tmp;
    int c = count;

    while (size--) {
        t |= ((uint64_t)(*(data++))) << (8 * (c % 8));
        c++;
        if ((c & 7) == 0) {
            v3 ^= t;
            SIPROUND;
            SIPROUND;
            v0 ^= t;
            t = 0;
        }
    }

    v[0] = v0;
    v[1] = v1;
    v[2] = v2;
    v[3] = v3;
    count = c;
    tmp = t;

    return *this;
}

uint64_t CSipHasher::Finalize() const
{
    uint64_t v0 = v[0], v1 = v[1], v2 = v[2], v3 = v[3];

    uint64_t t = tmp | (((uint64_t)count) << 56);

    v3 ^= t;
    SIPROUND;
    SIPROUND;
    v0 ^= t;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t SipHashUint256(uint64_t k0, uint64_t k1, const uint256& val)
{
    /* Specialized implementation for efficiency */
    uint64_t d = val.GetUint64(0);

    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1 ^ d;

    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = val.GetUint64(1);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = val.GetUint64(2);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = val.GetUint64(3);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    v3 ^= ((uint64_t)4) << 59;
    SIPROUND;
    SIPROUND;
    v0 ^= ((uint64_t)4) << 59;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint64_t SipHashUint256Extra(uint64_t k0, uint64_t k1, const uint256& val, uint32_t extra)
{
    /* Specialized implementation for efficiency */
    uint64_t d = val.GetUint64(0);

    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1 ^ d;

    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = val.GetUint64(1);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = val.GetUint64(2);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = val.GetUint64(3);
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    d = (((uint64_t)36) << 56) | extra;
    v3 ^= d;
    SIPROUND;
    SIPROUND;
    v0 ^= d;
    v2 ^= 0xFF;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;
    return v0 ^ v1 ^ v2 ^ v3;
}

uint256 KAWPOWHash(const CBlockHeader& blockHeader, uint256& mix_hash)
{
    static ethash::epoch_context_ptr context{nullptr, nullptr};

    // Get the context from the block height
    const auto epoch_number = ethash::get_epoch_number(blockHeader.nHeight);

    if (!context || context->epoch_number != epoch_number)
        context = ethash::create_epoch_context(epoch_number);

    // Build the header_hash
    uint256 nHeaderHash = blockHeader.GetKAWPOWHeaderHash();
    const auto header_hash = to_hash256(nHeaderHash.GetHex());

    // ProgPow hash
    const auto result = progpow::hash(*context, blockHeader.nHeight, header_hash, blockHeader.nNonce64);

    mix_hash = uint256S(to_hex(result.mix_hash));
    return uint256S(to_hex(result.final_hash));
}

uint256 KAWPOWHash_OnlyMix(const CBlockHeader& blockHeader)
{
    // Build the header_hash
    uint256 nHeaderHash = blockHeader.GetKAWPOWHeaderHash();
    const auto header_hash = to_hash256(nHeaderHash.GetHex());

    // ProgPow hash
    const auto result = progpow::hash_no_verify(blockHeader.nHeight, header_hash, to_hash256(blockHeader.mix_hash.GetHex()), blockHeader.nNonce64);

    return uint256S(to_hex(result));
}


// Function to convert a vector of bytes into a uint256 hash
uint256 HashVectorToUint256(const std::vector<unsigned char>& input) {
    uint256 result;
    CSHA256().Write(input.data(), input.size()).Finalize((unsigned char*)&result);
    return result;
}

// Helper function to hash with a nonce appended
std::vector<unsigned char> HashWithNonce(const std::vector<unsigned char>& input, uint32_t nonce) {
    std::vector<unsigned char> nonceBytes(4);
    nonceBytes[0] = nonce & 0xFF;
    nonceBytes[1] = (nonce >> 8) & 0xFF;
    nonceBytes[2] = (nonce >> 16) & 0xFF;
    nonceBytes[3] = (nonce >> 24) & 0xFF;

    std::vector<unsigned char> combinedInput(input);
    combinedInput.insert(combinedInput.end(), nonceBytes.begin(), nonceBytes.end());

    std::vector<unsigned char> hashOutput(CSHA256::OUTPUT_SIZE);
    CSHA256().Write(combinedInput.data(), combinedInput.size()).Finalize(hashOutput.data());
    return hashOutput;
}

// Find the first N/(K+1) bits of the hash (collision bits)
inline uint32_t GetCollisionBits(const std::vector<unsigned char>& hash, unsigned int bitLength) {
    uint32_t collisionBits = 0;
    for (unsigned int i = 0; i < bitLength; i++) {
        unsigned int byteIndex = i / 8;
        unsigned int bitIndex = i % 8;
        collisionBits |= ((hash[byteIndex] >> (7 - bitIndex)) & 1) << (bitLength - 1 - i);
    }
    return collisionBits;
}

// Equihash hashing function
uint256 EquihashHash(const CBlockHeader& blockHeader) {
    // Equihash parameters (e.g., 200,9 or 144,5)
    constexpr unsigned int N = 255;
    constexpr unsigned int K = 11;
    constexpr unsigned int numSteps = K + 1;
    constexpr unsigned int collisionBitLength = N / numSteps;

    // Get the header hash, which is the input for the Equihash algorithm
    uint256 nHeaderHash = blockHeader.GetEQUIHASHHeaderHash();
    std::vector<unsigned char> headerHashVec(nHeaderHash.begin(), nHeaderHash.end());

    // Generate a set of hash values using different nonces (size = 2^(K+1))
    const size_t numHashes = 1 << K; // 2^K
    std::vector<std::pair<std::vector<unsigned char>, uint32_t>> hashes;

    // Step 1: Generate initial hash values by hashing the header with different nonces
    for (uint32_t nonce = 0; nonce < numHashes; nonce++) {
        std::vector<unsigned char> hash = HashWithNonce(headerHashVec, nonce);
        hashes.emplace_back(hash, nonce);
    }

    // Step 2: Collision finding process (iterate K times)
    for (unsigned int step = 0; step < K; ++step) {
        std::vector<std::pair<std::vector<unsigned char>, uint32_t>> newHashes;
        std::set<uint32_t> seenCollisionBits;

        for (size_t i = 0; i < hashes.size(); i += 2) {
            // Take pairs of hashes
            std::vector<unsigned char>& hashA = hashes[i].first;
            std::vector<unsigned char>& hashB = hashes[i + 1].first;

            // Get the collision bits for the current step
            uint32_t collisionBitsA = GetCollisionBits(hashA, collisionBitLength);
            uint32_t collisionBitsB = GetCollisionBits(hashB, collisionBitLength);

            if (collisionBitsA == collisionBitsB) {
                // Combine the two hashes by XORing them
                std::vector<unsigned char> newHash(hashA.size());
                for (size_t j = 0; j < hashA.size(); j++) {
                    newHash[j] = hashA[j] ^ hashB[j];
                }

                // Ensure unique collisions
                if (seenCollisionBits.count(collisionBitsA) == 0) {
                    newHashes.emplace_back(newHash, hashes[i].second);
                    seenCollisionBits.insert(collisionBitsA);
                }
            }
        }

        // Replace the current hashes with the newly generated set
        hashes = std::move(newHashes);
    }

    // Step 3: Validate the final hashes (should be a few remaining)
    if (hashes.empty()) {
//        throw std::runtime_error("Equihash solving failed: no valid solution.");
        return nHeaderHash;
    }

    // Take the final solution and return the hash
    std::vector<unsigned char> finalSolution = hashes[0].first;
    return HashVectorToUint256(finalSolution);
}

//pos hash
void scrypt_hash(const char* pass, unsigned int pLen, const char* salt, unsigned int sLen, char* output, unsigned int N, unsigned int r, unsigned int p, unsigned int dkLen)
{
//    crypt(pass, pLen, salt, sLen, output, N, r, p, dkLen);
}


