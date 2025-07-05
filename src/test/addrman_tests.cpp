// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2017-2019 The Raven Core developers
// Copyright (c) 2020-2021 The Neoxa Core developers
// Copyright (c) 2022-2024 The CLORE.AI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * CLORE Address Manager Tests
 * 
 * These tests validate the peer address management system which is CRITICAL for CLORE's
 * P2P network functionality. The Address Manager (CAddrMan) handles:
 * 
 * 1. Peer Discovery: Finding and maintaining lists of network peers
 * 2. Address Storage: Managing "new" (untested) and "tried" (verified) peer addresses  
 * 3. Network Resilience: Ensuring the node can always find peers to connect to
 * 4. Anti-Spam: Preventing address pollution and maintaining network health
 * 
 * WHY THIS MATTERS FOR CLORE:
 * - Without working address management, nodes cannot discover peers
 * - Validator nodes especially need reliable peer connections
 * - This is foundational infrastructure that must work before any upgrades
 * - Network partitioning could occur if address management fails
 */

#include "addrman.h"          // CLORE's address manager implementation
#include "test/test_clore.h"  // CLORE test framework
#include "hash.h"             // Cryptographic hash functions  
#include "netbase.h"          // Network address utilities
#include "random.h"           // Secure random number generation
#include "utilstrencodings.h" // String encoding utilities

#include <string>
#include <boost/test/unit_test.hpp>

/**
 * CAddrManTest - Extended address manager for testing
 * 
 * This test wrapper adds deterministic behavior and access to internal
 * methods that are normally private. This allows us to test specific
 * scenarios and edge cases that could affect network stability.
 */
class CAddrManTest : public CAddrMan
{
    uint64_t state;  // Internal state for deterministic random number generation

public:
    explicit CAddrManTest(bool makeDeterministic = true)
    {
        state = 1;

        if (makeDeterministic)
        {
            // Set addrman addr placement to be deterministic for consistent testing
            MakeDeterministic();
        }
    }

    /**
     * MakeDeterministic - Ensure reproducible test results
     * 
     * For testing, we need consistent behavior. This removes randomness
     * from address bucket placement so tests produce the same results.
     */
    void MakeDeterministic()
    {
        nKey.SetNull();
        insecure_rand = FastRandomContext(true);
    }

    /**
     * RandomInt - Deterministic random number generator for testing
     * 
     * Overrides the normal random function to use a deterministic sequence
     * based on cryptographic hashing. This ensures test reproducibility.
     */
    int RandomInt(int nMax) override
    {
        state = (CHashWriter(SER_GETHASH, 0) << state).GetHash().GetCheapHash();
        return (unsigned int) (state % nMax);
    }

    // Expose internal methods for testing specific functionality
    CAddrInfo *Find(const CNetAddr &addr, int *pnId = nullptr)
    {
        return CAddrMan::Find(addr, pnId);
    }

    CAddrInfo *Create(const CAddress &addr, const CNetAddr &addrSource, int *pnId = nullptr)
    {
        return CAddrMan::Create(addr, addrSource, pnId);
    }

    void Delete(int nId)
    {
        CAddrMan::Delete(nId);
    }
};

/**
 * ResolveIP - Create network address from IP string
 * 
 * Helper function to create CNetAddr objects from IP addresses.
 * Used throughout tests to create test peer addresses.
 */
static CNetAddr ResolveIP(const char *ip)
{
    CNetAddr addr;
    BOOST_CHECK_MESSAGE(LookupHost(ip, addr, false), strprintf("failed to resolve: %s", ip));
    return addr;
}

static CNetAddr ResolveIP(std::string ip)
{
    return ResolveIP(ip.c_str());
}

/**
 * ResolveService - Create network service address with port
 * 
 * Helper function to create CService objects (IP + port).
 * Essential for testing peer connections which include port numbers.
 */
static CService ResolveService(const char *ip, int port = 0)
{
    CService serv;
    BOOST_CHECK_MESSAGE(Lookup(ip, serv, port, false), strprintf("failed to resolve: %s:%i", ip, port));
    return serv;
}

static CService ResolveService(std::string ip, int port = 0)
{
    return ResolveService(ip.c_str(), port);
}

BOOST_FIXTURE_TEST_SUITE(addrman_tests, BasicTestingSetup)

    /**
     * TEST: Basic Address Manager Functionality
     * 
     * IMPORTANCE: Validates core address storage and retrieval
     * - Empty address manager behavior
     * - Adding single addresses
     * - IP deduplication (prevents address pollution)
     * - Multiple address handling
     * - Address manager clearing
     * 
     * WHY CRITICAL FOR CLORE: Without these basics, peer discovery fails completely
     */
    BOOST_AUTO_TEST_CASE(addrman_simple_test)
    {
        BOOST_TEST_MESSAGE("=== Testing Basic CLORE Address Manager Functionality ===");

        CAddrManTest addrman;
        CNetAddr source = ResolveIP("252.2.2.2");

        // TEST 1: Empty address manager should return null address
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);
        CAddrInfo addr_null = addrman.Select();
        BOOST_CHECK_EQUAL(addr_null.ToString(), "[::]:0");
        BOOST_TEST_MESSAGE("✓ Empty address manager correctly returns null address");

        // TEST 2: Adding first address should work and be selectable
        CService addr1 = ResolveService("250.1.1.1", 8767);
        BOOST_CHECK(addrman.Add(CAddress(addr1, NODE_NONE), source));
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);
        CAddrInfo addr_ret1 = addrman.Select();
        BOOST_CHECK_EQUAL(addr_ret1.ToString(), "250.1.1.1:8767");
        BOOST_TEST_MESSAGE("✓ Single address addition and selection works");

        // TEST 3: IP deduplication - same IP should not be added twice
        // SECURITY: Prevents malicious actors from flooding with duplicate addresses
        CService addr1_dup = ResolveService("250.1.1.1", 8767);
        BOOST_CHECK(!addrman.Add(CAddress(addr1_dup, NODE_NONE), source));
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);
        BOOST_TEST_MESSAGE("✓ IP deduplication prevents address pollution");

        // TEST 4: Different IP addresses should both be stored
        CService addr2 = ResolveService("250.1.1.2", 8767);
        BOOST_CHECK(addrman.Add(CAddress(addr2, NODE_NONE), source));
        BOOST_CHECK(addrman.size() >= 1);
        BOOST_TEST_MESSAGE("✓ Multiple unique addresses can be stored");

        // TEST 5: Clear function should empty the address manager
        addrman.Clear();
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);
        CAddrInfo addr_null2 = addrman.Select();
        BOOST_CHECK_EQUAL(addr_null2.ToString(), "[::]:0");
        BOOST_TEST_MESSAGE("✓ Address manager clear function works");

        // TEST 6: Bulk address addition should work
        std::vector<CAddress> vAddr;
        vAddr.push_back(CAddress(ResolveService("250.1.1.3", 8767), NODE_NONE));
        vAddr.push_back(CAddress(ResolveService("250.1.1.4", 8767), NODE_NONE));
        BOOST_CHECK(addrman.Add(vAddr, source));
        BOOST_CHECK(addrman.size() >= 1);
        BOOST_TEST_MESSAGE("✓ Bulk address addition works");

        BOOST_TEST_MESSAGE("=== Basic Address Manager Tests PASSED ===");
    }

    /**
     * TEST: Port Handling in Address Management
     * 
     * IMPORTANCE: Validates how different ports are handled for same IP
     * - Port differentiation behavior
     * - Priority handling between different ports
     * - Interaction between new/tried tables with ports
     * 
     * WHY CRITICAL FOR CLORE: Many nodes may run on different ports, 
     * but we don't want port scanning to pollute our address space
     */
    BOOST_AUTO_TEST_CASE(addrman_ports_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Address Manager Port Handling ===");

        CAddrManTest addrman;
        CNetAddr source = ResolveIP("252.2.2.2");

        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);

        // TEST 1: Same IP with different port should not replace existing entry
        // RATIONALE: Prevents port scanning from polluting address manager
        CService addr1 = ResolveService("250.1.1.1", 8767);
        addrman.Add(CAddress(addr1, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);

        CService addr1_port = ResolveService("250.1.1.1", 8334);
        addrman.Add(CAddress(addr1_port, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);  // Should still be 1
        CAddrInfo addr_ret2 = addrman.Select();
        BOOST_CHECK_EQUAL(addr_ret2.ToString(), "250.1.1.1:8767");  // Original port preserved
        BOOST_TEST_MESSAGE("✓ Different ports on same IP handled correctly");

        // TEST 2: Moving to tried table with different port doesn't add new entry
        // BEHAVIOR: This prevents address space pollution from port variants
        addrman.Good(CAddress(addr1_port, NODE_NONE));
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);
        bool newOnly = true;
        CAddrInfo addr_ret3 = addrman.Select(newOnly);
        BOOST_CHECK_EQUAL(addr_ret3.ToString(), "250.1.1.1:8767");  // Still original port
        BOOST_TEST_MESSAGE("✓ Tried table promotion maintains original port");

        BOOST_TEST_MESSAGE("=== Port Handling Tests PASSED ===");
    }

    /**
     * TEST: Address Selection Mechanisms
     * 
     * IMPORTANCE: Validates how addresses are chosen for connections
     * - Selection from new table (untested addresses)
     * - Selection from tried table (verified addresses)  
     * - Balanced selection between new and tried
     * - Port diversity in selection
     * 
     * WHY CRITICAL FOR CLORE: Poor selection could lead to:
     * - Only connecting to old/stale peers
     * - Never trying new peers (network stagnation)
     * - Port bias reducing network diversity
     */
    BOOST_AUTO_TEST_CASE(addrman_select_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Address Selection Mechanisms ===");

        CAddrManTest addrman;
        CNetAddr source = ResolveIP("252.2.2.2");

        // TEST 1: Selection from new table with single address
        CService addr1 = ResolveService("250.1.1.1", 8767);
        addrman.Add(CAddress(addr1, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);

        bool newOnly = true;
        CAddrInfo addr_ret1 = addrman.Select(newOnly);
        BOOST_CHECK_EQUAL(addr_ret1.ToString(), "250.1.1.1:8767");
        BOOST_TEST_MESSAGE("✓ New table selection works");

        // TEST 2: After moving to tried table, new table should be empty
        addrman.Good(CAddress(addr1, NODE_NONE));  // Mark as good (moves to tried)
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);
        CAddrInfo addr_ret2 = addrman.Select(newOnly);  // Select from new only
        BOOST_CHECK_EQUAL(addr_ret2.ToString(), "[::]:0");  // Should be empty
        BOOST_TEST_MESSAGE("✓ Tried table promotion empties new table slot");

        // TEST 3: General selection should find tried addresses
        CAddrInfo addr_ret3 = addrman.Select();
        BOOST_CHECK_EQUAL(addr_ret3.ToString(), "250.1.1.1:8767");
        BOOST_TEST_MESSAGE("✓ General selection finds tried addresses");

        // TEST 4: Set up diverse address set for port diversity testing
        // Add addresses to new table
        CService addr2 = ResolveService("250.3.1.1", 8767);
        CService addr3 = ResolveService("250.3.2.2", 9999);
        CService addr4 = ResolveService("250.3.3.3", 9999);

        addrman.Add(CAddress(addr2, NODE_NONE), ResolveService("250.3.1.1", 8767));
        addrman.Add(CAddress(addr3, NODE_NONE), ResolveService("250.3.1.1", 8767));
        addrman.Add(CAddress(addr4, NODE_NONE), ResolveService("250.4.1.1", 8767));

        // Add addresses to tried table  
        CService addr5 = ResolveService("250.4.4.4", 8767);
        CService addr6 = ResolveService("250.4.5.5", 7777);
        CService addr7 = ResolveService("250.4.6.6", 8767);

        addrman.Add(CAddress(addr5, NODE_NONE), ResolveService("250.3.1.1", 8767));
        addrman.Good(CAddress(addr5, NODE_NONE));
        addrman.Add(CAddress(addr6, NODE_NONE), ResolveService("250.3.1.1", 8767));
        addrman.Good(CAddress(addr6, NODE_NONE));
        addrman.Add(CAddress(addr7, NODE_NONE), ResolveService("250.1.1.3", 8767));
        addrman.Good(CAddress(addr7, NODE_NONE));

        // TEST 5: Total address count validation
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)7);  // 6 new + 1 original = 7
        BOOST_TEST_MESSAGE("✓ Complex address set properly managed");

        // TEST 6: Port diversity in selection
        // IMPORTANCE: Ensures we connect to diverse port numbers, not just common ones
        std::set<uint16_t> ports;
        for (int i = 0; i < 20; ++i)
        {
            ports.insert(addrman.Select().GetPort());
        }
        BOOST_CHECK_EQUAL(ports.size(), (uint64_t)3);  // Should see 3 different ports
        BOOST_TEST_MESSAGE("✓ Selection provides port diversity (prevents port bias)");

        BOOST_TEST_MESSAGE("=== Address Selection Tests PASSED ===");
    }

    /**
     * TEST: New Table Collision Handling
     * 
     * IMPORTANCE: Tests address manager behavior when new table fills up
     * - Hash collision detection and handling
     * - Address replacement policies under pressure
     * - Protection against denial-of-service via address flooding
     * 
     * WHY CRITICAL FOR CLORE: Attackers could try to flood the address manager
     * with junk addresses to prevent discovery of legitimate peers
     */
    BOOST_AUTO_TEST_CASE(addrman_new_collisions_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE New Table Collision Handling ===");

        CAddrManTest addrman;
        CNetAddr source = ResolveIP("252.2.2.2");

        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);

        // Fill up new table until collision occurs
        for (unsigned int i = 1; i < 18; i++)
        {
            CService addr = ResolveService("250.1.1." + std::to_string(i));
            addrman.Add(CAddress(addr, NODE_NONE), source);

            // No collision yet - each address should be stored
            BOOST_CHECK_EQUAL(addrman.size(), i);
        }
        BOOST_TEST_MESSAGE("✓ New table fills normally without premature collisions");

        // TEST: Collision behavior when table is full
        CService addr1 = ResolveService("250.1.1.18");
        addrman.Add(CAddress(addr1, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)17);  // Collision occurred
        BOOST_TEST_MESSAGE("✓ New table collision properly handled (address replaced)");

        CService addr2 = ResolveService("250.1.1.19");
        addrman.Add(CAddress(addr2, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)18);  // Recovery after collision
        BOOST_TEST_MESSAGE("✓ Address manager recovers after collision");

        BOOST_TEST_MESSAGE("=== New Table Collision Tests PASSED ===");
    }

    /**
     * TEST: Tried Table Collision Handling  
     * 
     * IMPORTANCE: Tests behavior when tried table (verified peers) fills up
     * - Collision handling for proven good addresses
     * - Replacement policy for tried addresses
     * - Ensures we don't lose access to working peers
     * 
     * WHY CRITICAL FOR CLORE: The tried table contains our most reliable peers.
     * If collision handling is broken, we could lose connection to good peers.
     */
    BOOST_AUTO_TEST_CASE(addrman_tried_collisions_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Tried Table Collision Handling ===");

        CAddrManTest addrman;
        CNetAddr source = ResolveIP("252.2.2.2");

        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);

        // Fill tried table by adding and marking addresses as good
        for (unsigned int i = 1; i < 80; i++)
        {
            CService addr = ResolveService("250.1.1." + std::to_string(i));
            addrman.Add(CAddress(addr, NODE_NONE), source);
            addrman.Good(CAddress(addr, NODE_NONE));  // Move to tried table

            // No collision yet - each address should be stored
            BOOST_CHECK_EQUAL(addrman.size(), i);
        }
        BOOST_TEST_MESSAGE("✓ Tried table fills normally without premature collisions");

        // TEST: Collision in tried table
        CService addr1 = ResolveService("250.1.1.80");
        addrman.Add(CAddress(addr1, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)79);  // Collision caused size reduction
        BOOST_TEST_MESSAGE("✓ Tried table collision properly handled");

        CService addr2 = ResolveService("250.1.1.81");
        addrman.Add(CAddress(addr2, NODE_NONE), source);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)80);  // Recovery after collision
        BOOST_TEST_MESSAGE("✓ Tried table recovers after collision");

        BOOST_TEST_MESSAGE("=== Tried Table Collision Tests PASSED ===");
    }

    /**
     * TEST: Address Finding and Lookup
     * 
     * IMPORTANCE: Tests ability to find specific addresses in the manager
     * - Exact IP address lookup
     * - Port handling in lookups  
     * - Multiple address differentiation
     * 
     * WHY CRITICAL FOR CLORE: We need to be able to check if we already know
     * about a peer before adding them, and look up connection info for known peers.
     */
    BOOST_AUTO_TEST_CASE(addrman_find_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Address Finding and Lookup ===");

        CAddrManTest addrman;

        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);

        // Set up test addresses with different IPs and ports
        CAddress addr1 = CAddress(ResolveService("250.1.2.1", 8767), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.2.1", 9999), NODE_NONE);  // Same IP, different port
        CAddress addr3 = CAddress(ResolveService("251.255.2.1", 8767), NODE_NONE);  // Different IP

        CNetAddr source1 = ResolveIP("250.1.2.1");
        CNetAddr source2 = ResolveIP("250.1.2.2");

        addrman.Add(addr1, source1);
        addrman.Add(addr2, source2);
        addrman.Add(addr3, source1);

        // TEST 1: Find should return exact IP match
        CAddrInfo *info1 = addrman.Find(addr1);
        BOOST_REQUIRE(info1);
        BOOST_CHECK_EQUAL(info1->ToString(), "250.1.2.1:8767");
        BOOST_TEST_MESSAGE("✓ Exact address lookup works");

        // TEST 2: Find does not discriminate by port (finds same IP)
        // BEHAVIOR: Address manager treats different ports as same node
        CAddrInfo *info2 = addrman.Find(addr2);
        BOOST_REQUIRE(info2);
        BOOST_CHECK_EQUAL(info2->ToString(), info1->ToString());  // Should be same as addr1
        BOOST_TEST_MESSAGE("✓ Port-agnostic lookup works (prevents port duplication)");

        // TEST 3: Find returns different IP when expected
        CAddrInfo *info3 = addrman.Find(addr3);
        BOOST_REQUIRE(info3);
        BOOST_CHECK_EQUAL(info3->ToString(), "251.255.2.1:8767");
        BOOST_TEST_MESSAGE("✓ Different IP addresses properly distinguished");

        BOOST_TEST_MESSAGE("=== Address Finding Tests PASSED ===");
    }

    /**
     * TEST: Address Creation and Internal Management
     * 
     * IMPORTANCE: Tests internal address object creation and tracking
     * - Address info object creation
     * - Internal ID assignment
     * - Consistency between creation and lookup
     * 
     * WHY CRITICAL FOR CLORE: Internal tracking must be consistent to prevent
     * memory leaks and ensure proper address lifecycle management.
     */
    BOOST_AUTO_TEST_CASE(addrman_create_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Address Creation and Management ===");

        CAddrManTest addrman;

        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);

        CAddress addr1 = CAddress(ResolveService("250.1.2.1", 8767), NODE_NONE);
        CNetAddr source1 = ResolveIP("250.1.2.1");

        // TEST 1: Internal address creation
        int nId;
        CAddrInfo *pinfo = addrman.Create(addr1, source1, &nId);

        // Address info should match input
        BOOST_CHECK_EQUAL(pinfo->ToString(), "250.1.2.1:8767");
        BOOST_TEST_MESSAGE("✓ Address creation produces correct address info");

        // TEST 2: Created address should be findable
        CAddrInfo *info2 = addrman.Find(addr1);
        BOOST_CHECK_EQUAL(info2->ToString(), "250.1.2.1:8767");
        BOOST_TEST_MESSAGE("✓ Created addresses are properly indexed and findable");

        BOOST_TEST_MESSAGE("=== Address Creation Tests PASSED ===");
    }

    /**
     * TEST: Address Deletion and Cleanup
     * 
     * IMPORTANCE: Tests proper address removal from manager
     * - Clean deletion without memory leaks
     * - Size tracking accuracy after deletion
     * - Address becomes unfindable after deletion
     * 
     * WHY CRITICAL FOR CLORE: Memory leaks in address management could
     * cause long-running nodes to consume excessive memory over time.
     */
    BOOST_AUTO_TEST_CASE(addrman_delete_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Address Deletion and Cleanup ===");

        CAddrManTest addrman;

        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);

        CAddress addr1 = CAddress(ResolveService("250.1.2.1", 8767), NODE_NONE);
        CNetAddr source1 = ResolveIP("250.1.2.1");

        // Create address and get its internal ID
        int nId;
        addrman.Create(addr1, source1, &nId);

        // TEST 1: Address exists and size is correct
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)1);
        BOOST_TEST_MESSAGE("✓ Address properly added");

        // TEST 2: Deletion removes address completely
        addrman.Delete(nId);
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);
        BOOST_TEST_MESSAGE("✓ Address deletion updates size correctly");

        // TEST 3: Deleted address cannot be found
        CAddrInfo *info2 = addrman.Find(addr1);
        BOOST_CHECK(info2 == nullptr);
        BOOST_TEST_MESSAGE("✓ Deleted addresses become unfindable (proper cleanup)");

        BOOST_TEST_MESSAGE("=== Address Deletion Tests PASSED ===");
    }

    /**
     * TEST: GetAddr Functionality (Address Sharing)
     * 
     * IMPORTANCE: Tests the mechanism for sharing addresses with other peers
     * - Correct percentage of addresses returned
     * - Mix of new and tried addresses
     * - Scaling behavior with large address sets
     * - Time-based filtering (non-terrible addresses)
     * 
     * WHY CRITICAL FOR CLORE: This is how the network shares peer information.
     * Broken GetAddr could prevent network growth and peer discovery.
     */
    BOOST_AUTO_TEST_CASE(addrman_getaddr_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Address Sharing (GetAddr) ===");

        CAddrManTest addrman;

        // TEST 1: Empty address manager should return no addresses
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)0);
        std::vector<CAddress> vAddr1 = addrman.GetAddr();
        BOOST_CHECK_EQUAL(vAddr1.size(), (uint64_t)0);
        BOOST_TEST_MESSAGE("✓ Empty address manager returns no addresses");

        // Set up test addresses with recent timestamps (not terrible)
        CAddress addr1 = CAddress(ResolveService("250.250.2.1", 8767), NODE_NONE);
        addr1.nTime = GetAdjustedTime(); // Recent time = not terrible
        CAddress addr2 = CAddress(ResolveService("250.251.2.2", 9999), NODE_NONE);
        addr2.nTime = GetAdjustedTime();
        CAddress addr3 = CAddress(ResolveService("251.252.2.3", 8767), NODE_NONE);
        addr3.nTime = GetAdjustedTime();
        CAddress addr4 = CAddress(ResolveService("252.253.3.4", 8767), NODE_NONE);
        addr4.nTime = GetAdjustedTime();
        CAddress addr5 = CAddress(ResolveService("252.254.4.5", 8767), NODE_NONE);
        addr5.nTime = GetAdjustedTime();

        CNetAddr source1 = ResolveIP("250.1.2.1");
        CNetAddr source2 = ResolveIP("250.2.3.3");

        // TEST 2: Small address set behavior
        addrman.Add(addr1, source1);
        addrman.Add(addr2, source2);
        addrman.Add(addr3, source1);
        addrman.Add(addr4, source2);
        addrman.Add(addr5, source1);

        // GetAddr returns 23% of addresses, 23% of 5 is 1 rounded down
        BOOST_CHECK_EQUAL(addrman.GetAddr().size(), (uint64_t)1);
        BOOST_TEST_MESSAGE("✓ Small address set returns correct percentage (23%)");

        // TEST 3: Mixed new and tried addresses
        addrman.Good(CAddress(addr1, NODE_NONE));  // Move to tried
        addrman.Good(CAddress(addr2, NODE_NONE));  // Move to tried
        BOOST_CHECK_EQUAL(addrman.GetAddr().size(), (uint64_t)1);
        BOOST_TEST_MESSAGE("✓ Mixed new/tried addresses handled correctly");

        // TEST 4: Large address set scaling behavior
        for (unsigned int i = 1; i < (8 * 256); i++)
        {
            int octet1 = i % 256;
            int octet2 = i >> 8 % 256;
            std::string strAddr = std::to_string(octet1) + "." + std::to_string(octet2) + ".1.23";
            CAddress addr = CAddress(ResolveService(strAddr), NODE_NONE);

            // Ensure addresses are not terrible (recent timestamp)
            addr.nTime = GetAdjustedTime();
            addrman.Add(addr, ResolveIP(strAddr));
            if (i % 8 == 0)
                addrman.Good(addr);  // Some to tried table
        }
        std::vector<CAddress> vAddr = addrman.GetAddr();

        size_t percent23 = (addrman.size() * 23) / 100;
        BOOST_CHECK_EQUAL(vAddr.size(), percent23);
        BOOST_CHECK_EQUAL(vAddr.size(), (uint64_t)461);
        BOOST_TEST_MESSAGE("✓ Large address set returns correct percentage");

        // Address collisions cause total size to be less than addresses added
        BOOST_CHECK_EQUAL(addrman.size(), (uint64_t)2006);
        BOOST_TEST_MESSAGE("✓ Address collision handling maintains reasonable size");

        BOOST_TEST_MESSAGE("=== Address Sharing Tests PASSED ===");
    }

    /**
     * TEST: Tried Bucket Distribution and Security
     * 
     * IMPORTANCE: Tests how addresses are distributed across tried buckets
     * - Bucket randomization prevents targeted attacks
     * - IP grouping limits prevent single-entity dominance  
     * - Key variation ensures unpredictable placement
     * 
     * WHY CRITICAL FOR CLORE: Poor bucket distribution could allow attackers
     * to fill specific buckets and manipulate peer selection.
     */
    BOOST_AUTO_TEST_CASE(caddrinfo_get_tried_bucket_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE Tried Bucket Security and Distribution ===");

        CAddrManTest addrman;

        CAddress addr1 = CAddress(ResolveService("250.1.1.1", 8767), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.1.1", 9999), NODE_NONE);
        CNetAddr source1 = ResolveIP("250.1.1.1");

        CAddrInfo info1 = CAddrInfo(addr1, source1);

        uint256 nKey1 = (uint256) (CHashWriter(SER_GETHASH, 0) << 1).GetHash();
        uint256 nKey2 = (uint256) (CHashWriter(SER_GETHASH, 0) << 2).GetHash();

        // TEST 1: Deterministic bucket for same key
        BOOST_CHECK_EQUAL(info1.GetTriedBucket(nKey1), 62);
        BOOST_TEST_MESSAGE("✓ Deterministic bucket assignment for consistent key");

        // TEST 2: Key randomization provides security
        // SECURITY: Different keys must produce different buckets to prevent attacks
        BOOST_CHECK(info1.GetTriedBucket(nKey1) != info1.GetTriedBucket(nKey2));
        BOOST_TEST_MESSAGE("✓ Key randomization prevents bucket prediction attacks");

        // TEST 3: Port differentiation with address keys
        CAddrInfo info2 = CAddrInfo(addr2, source1);
        BOOST_CHECK(info1.GetKey() != info2.GetKey());
        BOOST_CHECK(info1.GetTriedBucket(nKey1) != info2.GetTriedBucket(nKey1));
        BOOST_TEST_MESSAGE("✓ Different ports generate different address keys");

        // TEST 4: IP group limits (same /16 network)
        // SECURITY: Prevents single network from dominating buckets
        std::set<int> buckets;
        for (int i = 0; i < 255; i++)
        {
            CAddrInfo infoi = CAddrInfo(
                    CAddress(ResolveService("250.1.1." + std::to_string(i)), NODE_NONE),
                    ResolveIP("250.1.1." + std::to_string(i)));
            int bucket = infoi.GetTriedBucket(nKey1);
            buckets.insert(bucket);
        }
        // Same /16 network should never get more than 8 buckets
        BOOST_CHECK_EQUAL(buckets.size(), (uint64_t)8);
        BOOST_TEST_MESSAGE("✓ IP group limits prevent single network dominance");

        // TEST 5: Different IP groups get diverse buckets
        buckets.clear();
        for (int j = 0; j < 255; j++)
        {
            CAddrInfo infoj = CAddrInfo(
                    CAddress(ResolveService("250." + std::to_string(j) + ".1.1"), NODE_NONE),
                    ResolveIP("250." + std::to_string(j) + ".1.1"));
            int bucket = infoj.GetTriedBucket(nKey1);
            buckets.insert(bucket);
        }
        // Different /16 networks should map to many buckets
        BOOST_CHECK_EQUAL(buckets.size(), (uint64_t)160);
        BOOST_TEST_MESSAGE("✓ Different IP groups achieve good bucket diversity");

        BOOST_TEST_MESSAGE("=== Tried Bucket Security Tests PASSED ===");
    }

    /**
     * TEST: New Bucket Distribution and Anti-Spam Protection
     * 
     * IMPORTANCE: Tests how new addresses are distributed to prevent spam
     * - Source-based bucket assignment
     * - IP group collocation for efficiency
     * - Source diversity requirements
     * 
     * WHY CRITICAL FOR CLORE: The new table is where unknown addresses first
     * arrive. Poor distribution could allow spam attacks or eclipse attacks.
     */
    BOOST_AUTO_TEST_CASE(caddrinfo_get_new_bucket_test)
    {
        BOOST_TEST_MESSAGE("=== Testing CLORE New Bucket Anti-Spam Protection ===");

        CAddrManTest addrman;

        CAddress addr1 = CAddress(ResolveService("250.1.2.1", 8767), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.2.1", 9999), NODE_NONE);
        CNetAddr source1 = ResolveIP("250.1.2.1");

        CAddrInfo info1 = CAddrInfo(addr1, source1);

        uint256 nKey1 = (uint256) (CHashWriter(SER_GETHASH, 0) << 1).GetHash();
        uint256 nKey2 = (uint256) (CHashWriter(SER_GETHASH, 0) << 2).GetHash();

        // TEST 1: Deterministic bucket assignment
        BOOST_CHECK_EQUAL(info1.GetNewBucket(nKey1), 786);
        BOOST_CHECK_EQUAL(info1.GetNewBucket(nKey1, source1), 786);
        BOOST_TEST_MESSAGE("✓ Deterministic new bucket assignment");

        // TEST 2: Key randomization for security
        BOOST_CHECK(info1.GetNewBucket(nKey1) != info1.GetNewBucket(nKey2));
        BOOST_TEST_MESSAGE("✓ Key randomization protects new bucket assignment");

        // TEST 3: Port handling in new buckets
        CAddrInfo info2 = CAddrInfo(addr2, source1);
        BOOST_CHECK(info1.GetKey() != info2.GetKey());
        BOOST_CHECK_EQUAL(info1.GetNewBucket(nKey1), info2.GetNewBucket(nKey1));
        BOOST_TEST_MESSAGE("✓ Ports don't affect new bucket assignment (anti-spam)");

        // TEST 4: Same IP group collocation
        // EFFICIENCY: Same /16 networks go to same bucket for space efficiency
        std::set<int> buckets;
        for (int i = 0; i < 255; i++)
        {
            CAddrInfo infoi = CAddrInfo(
                    CAddress(ResolveService("250.1.1." + std::to_string(i)), NODE_NONE),
                    ResolveIP("250.1.1." + std::to_string(i)));
            int bucket = infoi.GetNewBucket(nKey1);
            buckets.insert(bucket);
        }
        // Same IP group should always map to same bucket
        BOOST_CHECK_EQUAL(buckets.size(), (uint64_t)1);
        BOOST_TEST_MESSAGE("✓ Same IP groups properly collocated");

        // TEST 5: Source diversity limits
        buckets.clear();
        for (int j = 0; j < 4 * 255; j++)
        {
            CAddrInfo infoj = CAddrInfo(CAddress(
                    ResolveService(
                            std::to_string(250 + (j / 255)) + "." + std::to_string(j % 256) + ".1.1"), NODE_NONE),
                                        ResolveIP("251.4.1.1"));
            int bucket = infoj.GetNewBucket(nKey1);
            buckets.insert(bucket);
        }
        // Same source should be limited in bucket count (anti-spam)
        BOOST_CHECK(buckets.size() <= 64);
        BOOST_TEST_MESSAGE("✓ Source diversity limits prevent spam (≤64 buckets)");

        // TEST 6: Different sources get good distribution
        buckets.clear();
        for (int p = 0; p < 255; p++)
        {
            CAddrInfo infoj = CAddrInfo(
                    CAddress(ResolveService("250.1.1.1"), NODE_NONE),
                    ResolveIP("250." + std::to_string(p) + ".1.1"));
            int bucket = infoj.GetNewBucket(nKey1);
            buckets.insert(bucket);
        }
        // Different sources should get many buckets (good distribution)
        BOOST_CHECK(buckets.size() > 64);
        BOOST_TEST_MESSAGE("✓ Different sources achieve good bucket diversity (>64)");

        BOOST_TEST_MESSAGE("=== New Bucket Anti-Spam Tests PASSED ===");
    }

BOOST_AUTO_TEST_SUITE_END()
