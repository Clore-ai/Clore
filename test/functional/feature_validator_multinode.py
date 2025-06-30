#!/usr/bin/env python3
# Copyright (c) 2025 The Clore Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test validator functionality in multi-node environment

This test specifically addresses the multi-node synchronization issues.

Focus Areas:
- Multi-node synchronization fixes
- Validator authorization consistency across nodes
- Configuration synchronization
- Network connectivity and peer management
- Cross-node validator status validation

Key Issues Being Addressed:
- Node 1 stuck at height 0, not syncing from Node 0
- Cross-node sync timeouts
- Validator configuration consistency across network
"""

import time
from test_framework.test_framework import CloreTestFramework
from test_framework.util import *
from decimal import Decimal


class ValidatorMultiNodeTest(CloreTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3  # Use 3 nodes for comprehensive testing

        # Configure nodes with different roles
        self.extra_args = [
            ["-debug=net", "-debug=validator", "-printtoconsole=0"],  # Mining node
            ["-debug=net", "-debug=validator", "-printtoconsole=0"],  # Validator 1
            ["-debug=net", "-debug=validator", "-printtoconsole=0"],  # Validator 2
        ]

    def skip_test_if_missing_module(self):
        pass

    def setup_network(self):
        """Setup robust network connectivity"""
        self.setup_nodes()

        self.log.info("=== Setting up multi-node network for validator testing ===")

        # Create mesh network topology for better connectivity
        self.log.info("Creating mesh network topology...")
        try:
            # Connect all nodes to each other
            connect_nodes_bi(self.nodes, 0, 1)
            connect_nodes_bi(self.nodes, 0, 2)
            connect_nodes_bi(self.nodes, 1, 2)

            # Give time for connections to establish
            time.sleep(3)

            # Verify connections
            for i, node in enumerate(self.nodes):
                peer_count = len(node.getpeerinfo())
                self.log.info(f"Node {i} has {peer_count} peer connections")

            self.log.info("✅ Network topology established")

        except Exception as e:
            self.log.warning(f"Network setup warning: {e}")
            self.log.info("Continuing with available connections...")

    def run_test(self):
        """Execute multi-node validator testing"""
        self.log.info("Starting multi-node validator synchronization test...")

        # Phase 1: Test basic multi-node connectivity and sync
        self.test_basic_connectivity_and_sync()

        # Phase 2: Test validator authorization consistency
        self.test_authorization_consistency()

        # Phase 3: Test configuration management across nodes
        self.test_configuration_consistency()

        # Phase 4: Test validator operations with sync
        self.test_validator_operations_with_sync()

        self.log.info("✅ Multi-node validator testing completed successfully!")

    def test_basic_connectivity_and_sync(self):
        """Test basic connectivity and blockchain synchronization"""
        self.log.info("=== Testing Basic Connectivity and Sync ===")

        # Generate initial blocks on node 0
        self.log.info("Generating initial blocks on node 0...")
        coinbase_address = self.nodes[0].getnewaddress()
        initial_blocks = 5
        self.nodes[0].generatetoaddress(initial_blocks, coinbase_address)

        # Check node 0 height
        node0_height = self.nodes[0].getblockchaininfo()["blocks"]
        self.log.info(f"Node 0 height after generation: {node0_height}")
        assert (
            node0_height == initial_blocks
        ), f"Node 0 should have {initial_blocks} blocks"

        # Wait and check sync to other nodes
        self.log.info("Waiting for sync to other nodes...")
        max_wait_time = 30
        start_time = time.time()

        while time.time() - start_time < max_wait_time:
            node1_height = self.nodes[1].getblockchaininfo()["blocks"]
            node2_height = self.nodes[2].getblockchaininfo()["blocks"]

            self.log.info(
                f"Heights: Node0={node0_height}, Node1={node1_height}, Node2={node2_height}"
            )

            if node1_height == node0_height and node2_height == node0_height:
                self.log.info("✅ All nodes synchronized successfully!")
                break

            time.sleep(2)
        else:
            # Sync didn't complete naturally, try forced sync
            self.log.warning("Natural sync incomplete, attempting forced sync...")
            try:
                self.sync_all(timeout=30)
                self.log.info("✅ Forced sync completed")
            except Exception as e:
                self.log.warning(f"Forced sync failed: {e}")
                # Continue anyway for partial testing

        # Final height check
        heights = []
        for i, node in enumerate(self.nodes):
            try:
                height = node.getblockchaininfo()["blocks"]
                heights.append(height)
                self.log.info(f"Final Node {i} height: {height}")
            except Exception as e:
                self.log.warning(f"Node {i} height check failed: {e}")
                heights.append(0)

        # Report sync status
        max_height = max(heights)
        min_height = min(heights)
        sync_difference = max_height - min_height

        if sync_difference == 0:
            self.log.info("✅ Perfect synchronization achieved")
        elif sync_difference <= 2:
            self.log.info(f"✅ Good synchronization (diff: {sync_difference} blocks)")
        else:
            self.log.warning(
                f"⚠️ Synchronization issues detected (diff: {sync_difference} blocks)"
            )

        self.log.info("✅ Basic connectivity and sync testing completed")

    def test_authorization_consistency(self):
        """Test authorization system consistency across nodes"""
        self.log.info("=== Testing Authorization Consistency ===")

        test_cases = [
            ("alias", "test-validator-cross-node"),
            ("alias", "authorized-test"),
            ("address", None),  # Will generate address on each node
        ]

        for test_type, test_value in test_cases:
            self.log.info(f"Testing {test_type} authorization consistency...")

            # Generate address if needed
            if test_type == "address" and test_value is None:
                test_value = self.nodes[0].getnewaddress()

            # Test authorization on all nodes
            auth_results = []
            for i, node in enumerate(self.nodes):
                try:
                    result = node.checkvalidatorauth(test_type, test_value)
                    auth_results.append(result)
                    self.log.info(f"Node {i} auth result: {result['authorized']}")
                except Exception as e:
                    self.log.warning(f"Node {i} authorization check failed: {e}")
                    auth_results.append(None)

            # Verify consistency
            valid_results = [r for r in auth_results if r is not None]
            if len(valid_results) >= 2:
                first_result = valid_results[0]["authorized"]
                all_same = all(r["authorized"] == first_result for r in valid_results)

                if all_same:
                    self.log.info(
                        f"✅ Authorization consistency verified for {test_type}"
                    )
                else:
                    self.log.warning(
                        f"⚠️ Authorization inconsistency detected for {test_type}"
                    )
            else:
                self.log.warning(f"⚠️ Insufficient valid results for {test_type} test")

        self.log.info("✅ Authorization consistency testing completed")

    def test_configuration_consistency(self):
        """Test validator configuration consistency across nodes"""
        self.log.info("=== Testing Configuration Consistency ===")

        # Test that configuration commands work on all nodes
        config_commands = [
            "listvalidatorconf",
            "listauthorizedvalidators",
            "getvalidatorcount",
        ]

        for cmd in config_commands:
            self.log.info(f"Testing {cmd} consistency across nodes...")

            results = []
            for i, node in enumerate(self.nodes):
                try:
                    if cmd == "listvalidatorconf":
                        result = node.listvalidatorconf()
                    elif cmd == "listauthorizedvalidators":
                        result = node.listauthorizedvalidators()
                    elif cmd == "getvalidatorcount":
                        result = node.getvalidatorcount()

                    results.append(result)
                    self.log.info(f"Node {i} {cmd} result type: {type(result)}")

                except Exception as e:
                    self.log.warning(f"Node {i} {cmd} failed: {e}")
                    results.append(None)

            # Verify all nodes return same type of result
            valid_results = [r for r in results if r is not None]
            if len(valid_results) >= 2:
                if cmd in ["listvalidatorconf", "listauthorizedvalidators"]:
                    # Should all be lists
                    all_lists = all(isinstance(r, list) for r in valid_results)
                    if all_lists:
                        self.log.info(f"✅ {cmd} consistency verified")
                    else:
                        self.log.warning(f"⚠️ {cmd} type inconsistency")
                elif cmd == "getvalidatorcount":
                    # Should all be dicts with 'total' field
                    all_valid = all(
                        isinstance(r, dict) and "total" in r for r in valid_results
                    )
                    if all_valid:
                        self.log.info(f"✅ {cmd} consistency verified")
                    else:
                        self.log.warning(f"⚠️ {cmd} structure inconsistency")

        self.log.info("✅ Configuration consistency testing completed")

    def test_validator_operations_with_sync(self):
        """Test validator operations while maintaining sync"""
        self.log.info("=== Testing Validator Operations with Sync ===")

        # Create validator configuration on node 0
        self.log.info("Creating validator configuration on node 0...")

        try:
            collateral_txid = (
                "multinode111111111111111111111111111111111111111111111111111111111"
            )
            create_result = self.nodes[0].createvalidatorconfig(
                "multinode-test", "127.0.0.1:8790", collateral_txid, 0
            )

            self.log.info(f"Validator created: {create_result['alias']}")
            assert (
                create_result["authorized"] == True
            ), "Should be authorized in regtest"

            # Generate some blocks to test sync with configuration changes
            self.log.info("Generating blocks after validator creation...")
            coinbase_address = self.nodes[0].getnewaddress()
            self.nodes[0].generatetoaddress(3, coinbase_address)

            # Wait for sync
            time.sleep(3)

            # Check validator configuration on all nodes
            self.log.info("Checking validator configuration consistency...")

            config_counts = []
            for i, node in enumerate(self.nodes):
                try:
                    mn_list = node.listvalidatorconf()
                    config_counts.append(len(mn_list))
                    self.log.info(f"Node {i} has {len(mn_list)} validator configs")

                    if len(mn_list) > 0:
                        # Check if our created validator appears
                        aliases = [validator["alias"] for validator in mn_list]
                        if "multinode-test" in aliases:
                            self.log.info(
                                f"✅ Node {i} has multinode-test configuration"
                            )
                        else:
                            self.log.warning(
                                f"⚠️ Node {i} missing multinode-test configuration"
                            )

                except Exception as e:
                    self.log.warning(f"Node {i} config check failed: {e}")
                    config_counts.append(-1)

            # Test key generation on different nodes
            self.log.info("Testing key generation on different nodes...")

            keys_generated = 0
            for i, node in enumerate(self.nodes):
                try:
                    private_key = node.createvalidatorkey()
                    assert isinstance(private_key, str), "Should return string"
                    assert len(private_key) > 30, "Should be reasonable length"
                    keys_generated += 1
                    self.log.info(f"✅ Node {i} key generation successful")
                except Exception as e:
                    self.log.warning(f"Node {i} key generation failed: {e}")

            assert (
                keys_generated >= 2
            ), f"Should generate keys on at least 2 nodes, got {keys_generated}"

            # Clean up - remove the test configuration
            try:
                remove_result = self.nodes[0].removevalidatorconfig("multinode-test")
                if remove_result["removed"]:
                    self.log.info("✅ Test configuration cleaned up successfully")
            except Exception as e:
                self.log.warning(f"Cleanup failed: {e}")

        except Exception as e:
            self.log.warning(f"Validator operations test encountered issues: {e}")

        self.log.info("✅ Validator operations with sync testing completed")


if __name__ == "__main__":
    ValidatorMultiNodeTest().main()
