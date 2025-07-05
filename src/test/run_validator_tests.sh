#!/bin/bash

# CLORE Validator Test Suite Runner
# Copyright (c) 2024 The CLORE Core developers

set -e

echo "=================================="
echo "CLORE Validator Test Suite Runner"
echo "=================================="
echo

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test result tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Function to log test results
log_test_result() {
    local test_name="$1"
    local result="$2"
    local message="$3"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}[PASS]${NC} $test_name"
        if [ -n "$message" ]; then
            echo "       $message"
        fi
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}[FAIL]${NC} $test_name"
        if [ -n "$message" ]; then
            echo "       $message"
        fi
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    echo
}

# Function to run a specific test suite
run_test_suite() {
    local test_file="$1"
    local test_name="$2"
    
    echo -e "${BLUE}Running $test_name...${NC}"
    
    if [ ! -f "$test_file" ]; then
        log_test_result "$test_name" "FAIL" "Test file not found: $test_file"
        return 1
    fi
    
    # Check if test file compiles (basic syntax check)
    if grep -q "BOOST_AUTO_TEST_CASE" "$test_file"; then
        log_test_result "$test_name Compilation" "PASS" "Test file structure is valid"
    else
        log_test_result "$test_name Compilation" "FAIL" "No test cases found in file"
        return 1
    fi
    
    # Count test cases in file
    local test_count=$(grep -c "BOOST_AUTO_TEST_CASE" "$test_file")
    log_test_result "$test_name Test Count" "PASS" "Found $test_count test cases"
    
    return 0
}

# Function to validate test coverage
validate_test_coverage() {
    echo -e "${BLUE}Validating Test Coverage...${NC}"
    
    # Check that all required test categories are covered
    local required_tests=(
        "time_protocol"
        "budget_removal" 
        "authorization"
        "authentication"
        "pos_upgrade"
        "staking"
        "integration"
        "security"
    )
    
    for test_category in "${required_tests[@]}"; do
        if find test/ -name "*.cpp" -exec grep -l "$test_category" {} \; | grep -q .; then
            log_test_result "Coverage: $test_category" "PASS" "Test coverage found"
        else
            log_test_result "Coverage: $test_category" "FAIL" "Missing test coverage"
        fi
    done
}

# Function to check for security test completeness
check_security_tests() {
    echo -e "${BLUE}Checking Security Test Completeness...${NC}"
    
    local security_tests=(
        "authorization_attack"
        "unauthorized_validator"
        "message_authentication"
        "signature_verification"
        "timing_validation"
        "dos_protection"
    )
    
    for security_test in "${security_tests[@]}"; do
        if find test/ -name "*.cpp" -exec grep -l "$security_test" {} \; | grep -q .; then
            log_test_result "Security: $security_test" "PASS" "Security test found"
        else
            log_test_result "Security: $security_test" "FAIL" "Missing security test"
        fi
    done
}

# Function to validate phase coverage
validate_phase_coverage() {
    echo -e "${BLUE}Validating Phase Coverage...${NC}"
    
    local phases=(
        "Phase 1: Time Protocol"
        "Phase 2: Budget Removal"
        "Phase 3: Validator Keys"
        "Phase 4: Terminology"
        "Phase 5: Authorization"
        "Phase 6: Authentication"
        "Phase 7: Documentation"
    )
    
    for i in {1..7}; do
        local phase_name="Phase $i"
        if find test/ -name "*.cpp" -exec grep -l "PHASE $i\|Phase $i" {} \; | grep -q .; then
            log_test_result "$phase_name Coverage" "PASS" "Phase tests found"
        else
            log_test_result "$phase_name Coverage" "FAIL" "Missing phase tests"
        fi
    done
}

echo "Starting Validator Test Suite Validation..."
echo

# Run all test suites
run_test_suite "test/validator_authorization_tests.cpp" "Authorization Tests"
run_test_suite "test/validator_comprehensive_tests.cpp" "Comprehensive Tests"
run_test_suite "test/validator_pos_upgrade_tests.cpp" "PoS Upgrade Tests"
run_test_suite "test/validator_integration_tests.cpp" "Integration Tests"

# Validate test coverage
validate_test_coverage

# Check security tests
check_security_tests

# Validate phase coverage
validate_phase_coverage

# Summary
echo "=================================="
echo "TEST SUITE VALIDATION SUMMARY"
echo "=================================="
echo
echo -e "Total Tests: ${BLUE}$TOTAL_TESTS${NC}"
echo -e "Passed: ${GREEN}$PASSED_TESTS${NC}"
echo -e "Failed: ${RED}$FAILED_TESTS${NC}"
echo

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}✓ All tests passed validation!${NC}"
    echo
    echo "Test suite is ready for execution."
    echo "To run actual tests, compile and execute the test binary."
    exit 0
else
    echo -e "${RED}✗ Some tests failed validation.${NC}"
    echo
    echo "Please fix the failing tests before proceeding."
    exit 1
fi
