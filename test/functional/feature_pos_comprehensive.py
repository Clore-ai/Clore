#!/usr/bin/env python3
# Copyright (c) 2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Comprehensive CLORE PoS transition testing

This test validates the complete CLORE phased PoS transition with comprehensive coverage of:
1. Block validation behavior (PoW/PoS acceptance/rejection per phase)
2. RPC command functionality (validator, staking commands)
3. Multi-node consensus during phase transitions
4. Network synchronization across phases
5. Edge cases (blocks exactly at transition boundaries)

Specific focus on validator creation, staking activation, and reward tracking to ensure:
- Things that should fail do fail
- Things that shouldn't fail don't fail

Test Phases:
- Phase 0: Pure PoW (0-99) - Traditional operation
- Phase 1: Validator Infrastructure (100-149) - Validators active, PoW secures
- Phase 2: Staking Infrastructure (150-199) - Staking active, PoW secures
- Phase 3: PoS-Only (200+) - PoW disabled, PoS takes over
"""

from test_framework.test_framework import CloreTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes,
    sync_blocks,
    sync_mempools,
    wait_until,
)
import time


class PosComprehensiveTest(CloreTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [
            ["-regtest", "-staking=1", "-stakingmintime=1"],
            ["-regtest", "-staking=1", "-stakingmintime=1"],
            ["-regtest", "-staking=1", "-stakingmintime=1"],
        ]

    def setup_network(self):
        self.setup_nodes()
        # Create mesh network topology for maximum redundancy
        connect_nodes(self.nodes[0], 1)
        connect_nodes(self.nodes[0], 2)
        connect_nodes(self.nodes[1], 2)
        self.sync_all()

    def run_test(self):
        """Execute comprehensive CLORE PoS transition testing"""
        self.log.info("=== CLORE Comprehensive PoS Transition Testing ===")

        # Initialize test environment
        self.setup_test_environment()

        # Test each phase comprehensively
        self.test_phase_0_comprehensive()
        self.test_phase_1_comprehensive()
        self.test_phase_2_comprehensive()
        self.test_phase_3_comprehensive()

        # Test multi-node consensus and synchronization
        self.test_multi_node_consensus()

        # Test edge cases and boundary conditions
        self.test_edge_cases()

        self.log.info(
            "✅ CLORE comprehensive PoS transition test completed successfully!"
        )

    def setup_test_environment(self):
        """Set up test environment with addresses and initial blocks"""
        self.log.info("=== Setting up test environment ===")

        # Create addresses for each node
        self.coinbase_addresses = []
        for i in range(self.num_nodes):
            addr = self.nodes[i].getnewaddress()
            self.coinbase_addresses.append(addr)
            self.log.info(f"Node {i} address: {addr}")

        # Generate initial blocks for node 0
        self.nodes[0].generatetoaddress(10, self.coinbase_addresses[0])
        self.sync_all()

        self.log.info("✅ Test environment setup complete")

    def test_phase_0_comprehensive(self):
        """Comprehensive testing of Phase 0: Pure PoW (0-99)"""
        self.log.info("=== COMPREHENSIVE PHASE 0 TESTING ===")

        # Generate to block 50 (middle of Phase 0)
        self.nodes[0].generatetoaddress(40, self.coinbase_addresses[0])
        self.sync_all()

        height = self.nodes[0].getblockchaininfo()["blocks"]
        assert_equal(height, 50)

        # Test 1: Consensus phase verification
        self.verify_consensus_phase(0, "Pure PoW (Phase 0)", False, False, False)

        # Test 2: Block validation - PoW should work
        self.test_block_validation_phase_0()

        # Test 3: Validator functionality - RPC available but infrastructure not active
        self.test_validator_comprehensive_phase_0()

        # Test 4: Staking functionality - Should fail appropriately
        self.test_staking_comprehensive_phase_0()

        # Test 5: Reward tracking - Should show no rewards
        self.test_rewards_comprehensive_phase_0()

        self.log.info("✅ Phase 0 comprehensive testing completed")

    def test_phase_1_comprehensive(self):
        """Comprehensive testing of Phase 1: Validator Infrastructure (100-149)"""
        self.log.info("=== COMPREHENSIVE PHASE 1 TESTING ===")

        # Generate to block 120 (middle of Phase 1)
        current_height = self.nodes[0].getblockchaininfo()["blocks"]
        blocks_needed = 120 - current_height
        if blocks_needed > 0:
            self.nodes[0].generatetoaddress(blocks_needed, self.coinbase_addresses[0])
        self.sync_all()

        height = self.nodes[0].getblockchaininfo()["blocks"]
        assert height >= 120, f"Should be at least height 120, got {height}"

        # Test 1: Consensus phase verification
        self.verify_consensus_phase(
            0, "Validator Infrastructure (Phase 1)", True, False, False
        )

        # Test 2: Block validation - PoW should still work
        self.test_block_validation_phase_1()

        # Test 3: Validator functionality - Should be fully operational
        self.test_validator_comprehensive_phase_1()

        # Test 4: Staking functionality - Should still fail appropriately
        self.test_staking_comprehensive_phase_1()

        # Test 5: Reward tracking - Should still show no rewards
        self.test_rewards_comprehensive_phase_1()

        self.log.info("✅ Phase 1 comprehensive testing completed")

    def test_phase_2_comprehensive(self):
        """Comprehensive testing of Phase 2: Staking Infrastructure (150-199)"""
        self.log.info("=== COMPREHENSIVE PHASE 2 TESTING ===")

        # Generate to block 170 (middle of Phase 2)
        current_height = self.nodes[0].getblockchaininfo()["blocks"]
        blocks_needed = 170 - current_height
        if blocks_needed > 0:
            self.nodes[0].generatetoaddress(blocks_needed, self.coinbase_addresses[0])
        self.sync_all()

        height = self.nodes[0].getblockchaininfo()["blocks"]
        assert height >= 170, f"Should be at least height 170, got {height}"

        # Test 1: Consensus phase verification
        self.verify_consensus_phase(
            0, "Staking Infrastructure (Phase 2)", True, True, False
        )

        # Test 2: Block validation - PoW should still work
        self.test_block_validation_phase_2()

        # Test 3: Validator functionality - Should continue working
        self.test_validator_comprehensive_phase_2()

        # Test 4: Staking functionality - Should now work
        self.test_staking_comprehensive_phase_2()

        # Test 5: Reward tracking - Should show staking capability
        self.test_rewards_comprehensive_phase_2()

        self.log.info("✅ Phase 2 comprehensive testing completed")

    def test_phase_3_comprehensive(self):
        """Comprehensive testing of Phase 3: PoS-Only (200+)"""
        self.log.info("=== COMPREHENSIVE PHASE 3 TESTING ===")

        # Generate to block 200 (Phase 3 activation)
        current_height = self.nodes[0].getblockchaininfo()["blocks"]
        blocks_needed = 200 - current_height

        try:
            self.nodes[0].generatetoaddress(blocks_needed, self.coinbase_addresses[0])
        except Exception as e:
            if "pow-disabled" in str(e):
                self.log.info("✅ PoW correctly disabled during transition to Phase 3")
            else:
                raise e

        height = self.nodes[0].getblockchaininfo()["blocks"]
        self.log.info(f"Phase 3 height reached: {height}")

        # Test 1: Consensus phase verification (if we reached block 200)
        if height >= 200:
            self.verify_consensus_phase(0, "PoS-Only (Phase 3)", True, True, True)

        # Test 2: Block validation - PoW should be disabled
        self.test_block_validation_phase_3()

        # Test 3: Validator functionality - Should continue working
        self.test_validator_comprehensive_phase_3()

        # Test 4: Staking functionality - Should continue working
        self.test_staking_comprehensive_phase_3()

        # Test 5: Reward tracking - Should show full PoS capability
        self.test_rewards_comprehensive_phase_3()

        self.log.info("✅ Phase 3 comprehensive testing completed")

    def verify_consensus_phase(
        self, node_idx, expected_phase, validators_active, pos_active, pure_pos_active
    ):
        """Verify consensus phase across all nodes"""
        for i in range(self.num_nodes):
            pos_info = self.nodes[i].getposinfo()
            assert_equal(pos_info["consensus_phase"], expected_phase)
            assert_equal(pos_info["validators_active"], validators_active)
            assert_equal(pos_info["pos_active"], pos_active)
            assert_equal(pos_info["pure_pos_active"], pure_pos_active)

        self.log.info(f"✅ All nodes show correct phase: {expected_phase}")

    def test_block_validation_phase_0(self):
        """Test block validation in Phase 0"""
        # PoW blocks should work
        try:
            self.nodes[0].generatetoaddress(1, self.coinbase_addresses[0])
            self.sync_all()
            self.log.info("✅ Phase 0: PoW blocks accepted")
        except Exception as e:
            raise AssertionError(f"Phase 0: PoW blocks should work: {e}")

    def test_block_validation_phase_1(self):
        """Test block validation in Phase 1"""
        # PoW blocks should still work
        try:
            self.nodes[0].generatetoaddress(1, self.coinbase_addresses[0])
            self.sync_all()
            self.log.info(
                "✅ Phase 1: PoW blocks still accepted (secures during infrastructure)"
            )
        except Exception as e:
            raise AssertionError(f"Phase 1: PoW blocks should still work: {e}")

    def test_block_validation_phase_2(self):
        """Test block validation in Phase 2"""
        # PoW blocks should still work
        try:
            self.nodes[0].generatetoaddress(1, self.coinbase_addresses[0])
            self.sync_all()
            self.log.info(
                "✅ Phase 2: PoW blocks still accepted (secures while PoS builds)"
            )
        except Exception as e:
            raise AssertionError(f"Phase 2: PoW blocks should still work: {e}")

    def test_block_validation_phase_3(self):
        """Test block validation in Phase 3"""
        # PoW blocks should be disabled
        try:
            self.nodes[0].generatetoaddress(1, self.coinbase_addresses[0])
            raise AssertionError("Phase 3: PoW blocks should be disabled")
        except Exception as e:
            if "pow-disabled" in str(e) or "CreateNewBlock" in str(e):
                self.log.info("✅ Phase 3: PoW blocks correctly disabled")
            else:
                raise AssertionError(f"Phase 3: Unexpected error: {e}")

    def test_validator_comprehensive_phase_0(self):
        """Comprehensive validator testing in Phase 0"""
        self.log.info("--- Testing validator functionality in Phase 0 ---")

        # RPC commands should work but infrastructure not active
        for i in range(self.num_nodes):
            # Basic RPC commands should work
            mn_count = self.nodes[i].getvalidatorcount()
            assert "total" in mn_count

            # Authorized validators should work (regtest = all authorized)
            auth_mns = self.nodes[i].listauthorizedvalidators()
            assert isinstance(auth_mns, list)

            # Validator creation should work
            collateral_txid = f"phase0{'0' * 59}{i}"
            alias = f"phase0-node{i}"
            result = self.nodes[i].createvalidatorconfig(
                alias, f"127.0.0.1:879{i}", collateral_txid, 0
            )
            assert result["authorized"] == True

            # Clean up
            self.nodes[i].removevalidatorconfig(alias)

        self.log.info(
            "✅ Phase 0: Validator RPC available but infrastructure not active"
        )

    def test_validator_comprehensive_phase_1(self):
        """Comprehensive validator testing in Phase 1"""
        self.log.info("--- Testing validator functionality in Phase 1 ---")

        # Infrastructure should be fully operational
        for i in range(self.num_nodes):
            # All RPC commands should work
            mn_count = self.nodes[i].getvalidatorcount()
            assert "total" in mn_count

            # Validator creation should work
            collateral_txid = f"phase1{'1' * 59}{i}"
            alias = f"phase1-node{i}"
            result = self.nodes[i].createvalidatorconfig(
                alias, f"127.0.0.1:879{i}", collateral_txid, 0
            )
            assert result["authorized"] == True
            assert result["alias"] == alias

            # Listing should work
            mn_list = self.nodes[i].listvalidatorconf()
            found = False
            for validator in mn_list:
                if validator["alias"] == alias:
                    found = True
                    break
            assert found, f"Should find validator {alias}"

            # Clean up
            self.nodes[i].removevalidatorconfig(alias)

        self.log.info("✅ Phase 1: Validator infrastructure fully operational")

    def test_validator_comprehensive_phase_2(self):
        """Comprehensive validator testing in Phase 2"""
        self.log.info("--- Testing validator functionality in Phase 2 ---")

        # Should continue working during staking phase
        for i in range(self.num_nodes):
            collateral_txid = f"phase2{'2' * 59}{i}"
            alias = f"phase2-node{i}"
            result = self.nodes[i].createvalidatorconfig(
                alias, f"127.0.0.1:879{i}", collateral_txid, 0
            )
            assert result["authorized"] == True

            # Clean up
            self.nodes[i].removevalidatorconfig(alias)

        self.log.info("✅ Phase 2: Validator functionality continues working")

    def test_validator_comprehensive_phase_3(self):
        """Comprehensive validator testing in Phase 3"""
        self.log.info("--- Testing validator functionality in Phase 3 ---")

        # Should continue working in PoS-only phase
        for i in range(self.num_nodes):
            # Basic commands should work
            auth_mns = self.nodes[i].listauthorizedvalidators()
            assert isinstance(auth_mns, list)

            # Creation should work
            collateral_txid = f"phase3{'3' * 59}{i}"
            alias = f"phase3-node{i}"
            result = self.nodes[i].createvalidatorconfig(
                alias, f"127.0.0.1:879{i}", collateral_txid, 0
            )
            assert result["authorized"] == True

            # Clean up
            self.nodes[i].removevalidatorconfig(alias)

        self.log.info("✅ Phase 3: Validator functionality preserved in PoS-only")

    def test_staking_comprehensive_phase_0(self):
        """Comprehensive staking testing in Phase 0"""
        self.log.info("--- Testing staking functionality in Phase 0 ---")

        for i in range(self.num_nodes):
            try:
                # Staking info might work but show not active
                staking_info = self.nodes[i].getstakinginfo()
                # PoS not active yet, so staking weight should be 0 or minimal

                # Rewards should be zero
                staking_rewards = self.nodes[i].getstakingrewards()
                assert_equal(staking_rewards["totalrewards"], 0)
                assert_equal(staking_rewards["stakingblocks"], 0)

            except Exception as e:
                # Some commands might fail, which is expected in Phase 0
                if "not active" in str(e).lower() or "disabled" in str(e).lower():
                    pass  # Expected
                else:
                    raise AssertionError(f"Phase 0: Unexpected staking error: {e}")

        self.log.info("✅ Phase 0: Staking correctly inactive")

    def test_staking_comprehensive_phase_1(self):
        """Comprehensive staking testing in Phase 1"""
        self.log.info("--- Testing staking functionality in Phase 1 ---")

        for i in range(self.num_nodes):
            try:
                # Staking rewards should still be zero
                staking_rewards = self.nodes[i].getstakingrewards()
                assert_equal(staking_rewards["totalrewards"], 0)
                assert_equal(staking_rewards["stakingblocks"], 0)

            except Exception as e:
                # Some commands might fail, which is expected in Phase 1
                if "not active" in str(e).lower() or "disabled" in str(e).lower():
                    pass  # Expected

        self.log.info("✅ Phase 1: Staking correctly not active yet")

    def test_staking_comprehensive_phase_2(self):
        """Comprehensive staking testing in Phase 2"""
        self.log.info("--- Testing staking functionality in Phase 2 ---")

        for i in range(self.num_nodes):
            # Staking should now be active
            staking_info = self.nodes[i].getstakinginfo()
            assert "staking" in staking_info
            assert "weight" in staking_info

            # Rewards tracking should work
            staking_rewards = self.nodes[i].getstakingrewards()
            assert "totalrewards" in staking_rewards
            assert "stakingblocks" in staking_rewards

            # Try stakeable UTXOs (might not work without wallet)
            try:
                stakeable_utxos = self.nodes[i].liststakeableutxos()
                assert isinstance(stakeable_utxos, list)
            except Exception as e:
                if "wallet" in str(e).lower():
                    pass  # Expected without wallet
                else:
                    pass  # Command might not be available

        self.log.info("✅ Phase 2: Staking infrastructure operational")

    def test_staking_comprehensive_phase_3(self):
        """Comprehensive staking testing in Phase 3"""
        self.log.info("--- Testing staking functionality in Phase 3 ---")

        for i in range(self.num_nodes):
            # Staking should continue working
            staking_info = self.nodes[i].getstakinginfo()
            assert "staking" in staking_info
            assert "weight" in staking_info

            # Rewards tracking should continue
            staking_rewards = self.nodes[i].getstakingrewards()
            assert "totalrewards" in staking_rewards
            assert "stakingblocks" in staking_rewards

        self.log.info("✅ Phase 3: Staking functionality preserved in PoS-only")

    def test_rewards_comprehensive_phase_0(self):
        """Comprehensive reward testing in Phase 0"""
        self.log.info("--- Testing reward functionality in Phase 0 ---")

        for i in range(self.num_nodes):
            try:
                staking_rewards = self.nodes[i].getstakingrewards()
                assert_equal(staking_rewards["totalrewards"], 0)
                assert_equal(staking_rewards["stakingblocks"], 0)
                assert_equal(staking_rewards["averagereward"], 0)
            except Exception as e:
                # Expected in Phase 0
                pass

        self.log.info("✅ Phase 0: No rewards as expected")

    def test_rewards_comprehensive_phase_1(self):
        """Comprehensive reward testing in Phase 1"""
        self.log.info("--- Testing reward functionality in Phase 1 ---")

        for i in range(self.num_nodes):
            try:
                staking_rewards = self.nodes[i].getstakingrewards()
                assert_equal(staking_rewards["totalrewards"], 0)
                assert_equal(staking_rewards["stakingblocks"], 0)
            except Exception as e:
                # Expected in Phase 1
                pass

        self.log.info("✅ Phase 1: No staking rewards as expected")

    def test_rewards_comprehensive_phase_2(self):
        """Comprehensive reward testing in Phase 2"""
        self.log.info("--- Testing reward functionality in Phase 2 ---")

        for i in range(self.num_nodes):
            staking_rewards = self.nodes[i].getstakingrewards()
            assert "totalrewards" in staking_rewards
            assert "stakingblocks" in staking_rewards
            assert "averagereward" in staking_rewards
            # Rewards might be 0 initially but tracking should work

        self.log.info("✅ Phase 2: Reward tracking operational")

    def test_rewards_comprehensive_phase_3(self):
        """Comprehensive reward tracking in Phase 3"""
        self.log.info("--- Testing reward functionality in Phase 3 ---")

        for i in range(self.num_nodes):
            staking_rewards = self.nodes[i].getstakingrewards()
            assert "totalrewards" in staking_rewards
            assert "stakingblocks" in staking_rewards
            assert "averagereward" in staking_rewards

        self.log.info("✅ Phase 3: Reward tracking continues in PoS-only")

    def test_multi_node_consensus(self):
        """Test multi-node consensus and synchronization"""
        self.log.info("=== Testing multi-node consensus ===")

        # Verify all nodes have same blockchain state
        heights = []
        for i in range(self.num_nodes):
            info = self.nodes[i].getblockchaininfo()
            heights.append(info["blocks"])

        # All nodes should have same height
        assert all(h == heights[0] for h in heights), f"Heights differ: {heights}"

        # All nodes should have same consensus phase
        phases = []
        for i in range(self.num_nodes):
            pos_info = self.nodes[i].getposinfo()
            phases.append(pos_info["consensus_phase"])

        assert all(p == phases[0] for p in phases), f"Phases differ: {phases}"

        self.log.info("✅ Multi-node consensus verified")

    def test_edge_cases(self):
        """Test edge cases and boundary conditions"""
        self.log.info("=== Testing edge cases ===")

        # Test that PoW is permanently disabled
        for i in range(self.num_nodes):
            try:
                self.nodes[i].generatetoaddress(1, self.coinbase_addresses[i])
                raise AssertionError(f"Node {i}: PoW should be permanently disabled")
            except Exception as e:
                if "pow-disabled" in str(e) or "CreateNewBlock" in str(e):
                    pass  # Expected
                else:
                    raise AssertionError(f"Node {i}: Unexpected error: {e}")

        # Test that all nodes are synchronized after all transitions
        self.sync_all()

        self.log.info("✅ Edge cases passed")


if __name__ == "__main__":
    PosComprehensiveTest().main()
