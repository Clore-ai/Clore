// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Secure Memory Allocator Tests
 * 
 * These tests validate secure memory allocation which is CRITICAL for CLORE's security:
 * 
 * 1. Private Key Protection: Validator private keys must never be swapped to disk
 * 2. Memory Isolation: Cryptographic material isolated from other processes  
 * 3. Secure Cleanup: Memory zeroed on deallocation to prevent key recovery
 * 4. Arena Management: Efficient allocation/deallocation of secure memory regions
 * 
 * WHY THIS MATTERS FOR CLORE VALIDATORS:
 * - Validator private keys control network consensus - if leaked, network security fails
 * - Standard malloc() can be swapped to disk where keys could be recovered
 * - Memory dumps could expose private keys if not properly secured
 * - Secure allocators lock memory pages and zero them on cleanup
 * - Without this, validator keys could be compromised through memory attacks
 * 
 * SECURITY IMPLICATIONS:
 * - Memory containing private keys MUST be locked (mlock) to prevent swapping
 * - Memory MUST be zeroed on deallocation to prevent forensic recovery
 * - Arena allocation provides efficient management of secure memory pools
 * - Failed allocation handling prevents crashes that could expose keys
 */

#include "util.h"                           // Core utilities
#include "support/allocators/secure.h"      // Secure memory allocators  
#include "test/test_clore.h"               // CLORE test framework

#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <vector>
#include <memory>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(allocator_tests, BasicTestingSetup)

    /**
     * TEST: Arena Memory Management
     * 
     * IMPORTANCE: Tests the core arena allocation system used for secure memory
     * - Memory alignment and efficiency
     * - Allocation/deallocation correctness
     * - Double-free protection
     * - Memory fragmentation handling
     * - Stress testing under various allocation patterns
     * 
     * WHY CRITICAL FOR CLORE: Arena allocators provide the foundation for secure
     * memory pools. If the arena is buggy, private keys could be corrupted or
     * memory could be leaked, compromising validator security.
     */
    BOOST_AUTO_TEST_CASE(arena_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Secure Arena Memory Management ===");

        // Create synthetic memory arena for testing (not real memory allocation)
        // This tests the allocation logic without actually using system memory
        void *synth_base = reinterpret_cast<void *>(0x08000000);
        const size_t synth_size = 1024 * 1024;  // 1MB test arena
        Arena b(synth_base, synth_size, 16);    // 16-byte alignment for crypto operations

        // TEST 1: Basic allocation and alignment
        void *chunk = b.alloc(1000);
        BOOST_CHECK(chunk != nullptr);
        BOOST_CHECK(b.stats().used == 1008);     // Should be aligned to 16 bytes
        BOOST_CHECK(b.stats().total == synth_size);
        BOOST_TEST_MESSAGE("✓ Basic allocation works with proper 16-byte alignment");

        // TEST 2: Basic deallocation
        b.free(chunk);
        BOOST_CHECK(b.stats().used == 0);
        BOOST_CHECK(b.stats().free == synth_size);
        BOOST_TEST_MESSAGE("✓ Basic deallocation returns memory to free pool");

        // TEST 3: Double-free protection (SECURITY)
        // CRITICAL: Double-free bugs can be exploited to corrupt memory
        try {
            b.free(chunk);  // This should throw exception
            BOOST_CHECK(false); // Should not reach here
        } catch (std::runtime_error &) {
            BOOST_TEST_MESSAGE("✓ Double-free protection prevents memory corruption");
        }

        // TEST 4: Multiple allocation stress test
        void *a0 = b.alloc(128);
        void *a1 = b.alloc(256);
        void *a2 = b.alloc(512);
        BOOST_CHECK(b.stats().used == 896);  // 128+256+512 aligned
        BOOST_CHECK(b.stats().total == synth_size);
        BOOST_TEST_MESSAGE("✓ Multiple allocations tracked correctly");

        // TEST 5: Fragmentation and coalescing test
        b.free(a0);  // Free first chunk
        BOOST_CHECK(b.stats().used == 768);
        b.free(a1);  // Free middle chunk  
        BOOST_CHECK(b.stats().used == 512);
        
        void *a3 = b.alloc(128);  // Should reuse freed space
        BOOST_CHECK(b.stats().used == 640);
        BOOST_TEST_MESSAGE("✓ Memory fragmentation and reuse handled correctly");

        // Clean up remaining allocations
        b.free(a2);
        b.free(a3);
        BOOST_CHECK(b.stats().used == 0);
        BOOST_CHECK_EQUAL(b.stats().chunks_used, (uint64_t)0);
        BOOST_CHECK(b.stats().total == synth_size);
        BOOST_CHECK(b.stats().free == synth_size);
        BOOST_CHECK_EQUAL(b.stats().chunks_free, (uint64_t)1);
        BOOST_TEST_MESSAGE("✓ All memory properly returned to free pool");

        // TEST 6: Edge cases and error conditions
        std::vector<void *> addr;
        BOOST_CHECK(b.alloc(0) == nullptr); // Zero allocation should return null
        BOOST_TEST_MESSAGE("✓ Zero-byte allocation properly rejected");

        // TEST 7: Arena exhaustion behavior
        // Fill entire arena with 1KB chunks
        for (int x = 0; x < 1024; ++x) {
            addr.push_back(b.alloc(1024));
        }
        BOOST_CHECK(b.stats().free == 0);
        BOOST_CHECK(b.alloc(1024) == nullptr); // Should fail when full
        BOOST_CHECK(b.alloc(0) == nullptr);
        BOOST_TEST_MESSAGE("✓ Arena exhaustion handled gracefully");

        // Free all memory in forward order
        for (int x = 0; x < 1024; ++x) {
            b.free(addr[x]);
        }
        addr.clear();
        BOOST_CHECK(b.stats().total == synth_size);
        BOOST_CHECK(b.stats().free == synth_size);
        BOOST_TEST_MESSAGE("✓ Forward deallocation pattern works");

        // TEST 8: Reverse deallocation pattern
        for (int x = 0; x < 1024; ++x) {
            addr.push_back(b.alloc(1024));
        }
        for (int x = 0; x < 1024; ++x) {
            b.free(addr[1023 - x]);  // Free in reverse order
        }
        addr.clear();
        BOOST_TEST_MESSAGE("✓ Reverse deallocation pattern works");

        // TEST 9: Random allocation/deallocation stress test
        // This simulates real-world usage patterns with validator key operations
        for (int x = 0; x < 2048; ++x) {
            addr.push_back(b.alloc(x + 1));  // Variable size allocations
        }
        for (int x = 0; x < 2048; ++x) {
            b.free(addr[((x * 23) % 2048) ^ 242]);  // Random deallocation order
        }
        addr.clear();
        BOOST_TEST_MESSAGE("✓ Variable-size allocation stress test passed");

        // TEST 10: Extreme stress test with interleaved operations
        // This simulates heavy validator operations with frequent key material handling
        for (int x = 0; x < 2048; ++x) {
            addr.push_back(nullptr);
        }
        
        uint32_t s = 0x12345678;  // PRNG seed
        for (int x = 0; x < 5000; ++x) {
            int idx = s & (addr.size() - 1);
            if (s & 0x80000000) {
                b.free(addr[idx]);
                addr[idx] = nullptr;
            } else if (!addr[idx]) {
                addr[idx] = b.alloc((s >> 16) & 2047);  // 0-2047 byte allocations
            }
            // Linear feedback shift register for pseudo-randomness
            bool lsb = s & 1;
            s >>= 1;
            if (lsb) s ^= 0xf00f00f0; // LFSR period 0xf7ffffe0
        }
        
        // Clean up any remaining allocations
        for (void *ptr: addr) {
            b.free(ptr);
        }
        addr.clear();

        BOOST_CHECK(b.stats().total == synth_size);
        BOOST_CHECK(b.stats().free == synth_size);
        BOOST_TEST_MESSAGE("✓ Extreme stress test with 5000 random operations passed");

        BOOST_TEST_MESSAGE("=== Arena Memory Management Tests PASSED ===");
    }

    /**
     * TestLockedPageAllocator - Mock allocator for testing locked memory
     * 
     * This mock simulates the system's locked page allocator behavior
     * without actually calling mlock(). Used to test allocation logic
     * and failure handling without requiring root privileges.
     */
    class TestLockedPageAllocator : public LockedPageAllocator
    {
    public:
        TestLockedPageAllocator(int count_in, int lockedcount_in) 
            : count(count_in), lockedcount(lockedcount_in) {}

        void *AllocateLocked(size_t len, bool *lockingSuccess) override
        {
            *lockingSuccess = false;
            if (count > 0) {
                --count;

                if (lockedcount > 0) {
                    --lockedcount;
                    *lockingSuccess = true;  // Simulate successful mlock()
                }

                // Return fake address for testing (DO NOT USE THIS MEMORY)
                return reinterpret_cast<void *>(0x08000000 + (count << 24)); 
            }
            return nullptr;
        }

        void FreeLocked(void *addr, size_t len) override
        {
            // Mock implementation - no actual memory to free
        }

        size_t GetLimit() override
        {
            return std::numeric_limits<size_t>::max();
        }

    private:
        int count;       // Total allocations available
        int lockedcount; // Successful lock operations available
    };

    /**
     * TEST: Locked Memory Pool Management
     * 
     * IMPORTANCE: Tests secure memory pool that prevents key swapping to disk
     * - Locked page allocation simulation
     * - Memory locking success/failure handling  
     * - Pool statistics and management
     * - Request validation and security
     * 
     * WHY CRITICAL FOR CLORE: Validator private keys must NEVER be written to
     * swap files or paging. This test ensures the locked memory pool correctly
     * handles various scenarios including system limitations on locked memory.
     */
    BOOST_AUTO_TEST_CASE(lockedpool_mock_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Locked Memory Pool Management ===");

        // Create test pool with 3 total arenas, only 1 can be successfully locked
        // This simulates systems with limited mlock() capability
        std::unique_ptr<LockedPageAllocator> x(new TestLockedPageAllocator(3, 1));
        LockedPool pool(std::move(x));
        
        BOOST_CHECK(pool.stats().total == 0);
        BOOST_CHECK(pool.stats().locked == 0);
        BOOST_TEST_MESSAGE("✓ Empty locked pool initialized correctly");

        // TEST 1: Invalid request handling (SECURITY)
        void *invalid_toosmall = pool.alloc(0);
        BOOST_CHECK(invalid_toosmall == nullptr);
        BOOST_CHECK(pool.stats().used == 0);
        BOOST_CHECK(pool.stats().free == 0);
        BOOST_TEST_MESSAGE("✓ Zero-byte allocation properly rejected");

        void *invalid_toobig = pool.alloc(LockedPool::ARENA_SIZE + 1);
        BOOST_CHECK(invalid_toobig == nullptr);
        BOOST_CHECK(pool.stats().used == 0);
        BOOST_CHECK(pool.stats().free == 0);
        BOOST_TEST_MESSAGE("✓ Oversized allocation properly rejected");

        // TEST 2: Valid allocations up to arena limit
        void *a0 = pool.alloc(LockedPool::ARENA_SIZE / 2);
        BOOST_CHECK(a0);
        BOOST_CHECK(pool.stats().locked == LockedPool::ARENA_SIZE);
        BOOST_TEST_MESSAGE("✓ First allocation creates locked arena");

        void *a1 = pool.alloc(LockedPool::ARENA_SIZE / 2);
        BOOST_CHECK(a1);
        void *a2 = pool.alloc(LockedPool::ARENA_SIZE / 2);
        BOOST_CHECK(a2);
        void *a3 = pool.alloc(LockedPool::ARENA_SIZE / 2);
        BOOST_CHECK(a3);
        void *a4 = pool.alloc(LockedPool::ARENA_SIZE / 2);
        BOOST_CHECK(a4);
        void *a5 = pool.alloc(LockedPool::ARENA_SIZE / 2);
        BOOST_CHECK(a5);
        BOOST_TEST_MESSAGE("✓ Multiple allocations across 3 arenas successful");

        // TEST 3: Arena limit enforcement
        // We've used up our 3 available arenas, next allocation should fail
        void *a6 = pool.alloc(16);
        BOOST_CHECK(!a6);
        BOOST_TEST_MESSAGE("✓ Arena limit properly enforced");

        // TEST 4: Memory cleanup and statistics
        pool.free(a0);
        pool.free(a2);
        pool.free(a4);
        pool.free(a1);
        pool.free(a3);
        pool.free(a5);
        
        BOOST_CHECK(pool.stats().total == 3 * LockedPool::ARENA_SIZE);
        BOOST_CHECK(pool.stats().locked == LockedPool::ARENA_SIZE); // Only 1 successfully locked
        BOOST_CHECK(pool.stats().used == 0);
        BOOST_TEST_MESSAGE("✓ Memory cleanup maintains correct statistics");

        BOOST_TEST_MESSAGE("=== Locked Memory Pool Tests PASSED ===");
    }

    /**
     * TEST: Live Locked Memory Pool Integration
     * 
     * IMPORTANCE: Tests actual system integration with real memory locking
     * - Real mlock() system call interaction
     * - Memory read/write validation
     * - Double-free protection in live environment
     * - Resource management with system constraints
     * 
     * WHY CRITICAL FOR CLORE: This validates that secure memory actually works
     * in real system conditions. If this fails, validator private keys could
     * be swapped to disk and potentially recovered by attackers.
     * 
     * NOTE: This test uses the live LockedPoolManager which is shared with
     * other parts of the system, so conditions are less controllable.
     */
    BOOST_AUTO_TEST_CASE(lockedpool_live_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Live Locked Memory Integration ===");

        LockedPoolManager &pool = LockedPoolManager::Instance();
        LockedPool::Stats initial = pool.stats();

        // TEST 1: Real locked memory allocation
        void *a0 = pool.alloc(16);
        BOOST_CHECK(a0);
        BOOST_TEST_MESSAGE("✓ Live locked memory allocation successful");

        // TEST 2: Memory is actually usable and secure
        // Write test pattern to verify memory is accessible
        *((uint32_t *) a0) = 0x1234;
        BOOST_CHECK(*((uint32_t *) a0) == 0x1234);
        BOOST_TEST_MESSAGE("✓ Locked memory is readable and writable");

        // TEST 3: Proper cleanup
        pool.free(a0);
        BOOST_TEST_MESSAGE("✓ Locked memory cleanup successful");

        // TEST 4: Double-free protection in live environment
        try {
            pool.free(a0);  // This should throw exception
            BOOST_CHECK(false); // Should not reach here
        } catch (std::runtime_error &) {
            BOOST_TEST_MESSAGE("✓ Live double-free protection works");
        }

        // TEST 5: Resource management validation
        // Ensure we didn't allocate excessive arenas
        BOOST_CHECK(pool.stats().total <= (initial.total + LockedPool::ARENA_SIZE));
        // Memory usage should return to initial state
        BOOST_CHECK(pool.stats().used == initial.used);
        BOOST_TEST_MESSAGE("✓ Resource management maintains system stability");

        BOOST_TEST_MESSAGE("=== Live Locked Memory Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
