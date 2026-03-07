#include <cstdio>
#include <cstdlib>
#include <string>
#include "oracle_contract.hpp"

int main(int argc, char** argv) {
    const char* test_type = argv[1];
    
    if (std::string(test_type) == "load") {
        std::string error;
        oracle_contract::Contract contract;
        
        bool ok = oracle_contract::load_contract(
            "tests/oracle/gbp_compute_nodes_contract_phase1.yaml", 
            &contract, 
            &error
        );
        
        if (!ok) {
            std::fprintf(stderr, "FAIL: contract load failed: %s\n", error.c_str());
            return 1;
        }
        
        std::printf("oracle_contract: contract_type=%s\n", contract.contract_type.c_str());
        std::printf("oracle_contract: abs_tol=%.6g\n", contract.abs_tol);
        std::printf("oracle_contract: abs_only=%s\n", contract.abs_only ? "true" : "false");
        std::printf("oracle_contract: generator_seed=%d\n", contract.generator_seed);
        std::printf("oracle_contract: function_count=%d\n", contract.function_count);
        std::printf("oracle_contract: contract loaded successfully\n");
        
        if (contract.abs_tol != 1e-4) {
            std::fprintf(stderr, "FAIL: expected abs_tol=1e-4, got %g\n", contract.abs_tol);
            return 1;
        }
        
        if (!contract.abs_only) {
            std::fprintf(stderr, "FAIL: expected abs_only=true\n");
            return 1;
        }
        
        std::printf("oracle_contract: PASS: abs_only contract loaded with abs_tol=1e-4\n");
        return 0;
    }
    
    if (std::string(test_type) == "compare_pass") {
        std::string mismatch;
        double observed = 1.0;
        double expected = 1.00001;
        double abs_tol = 1e-4;
        
        bool ok = oracle_contract::compare_abs_only("test_scenario", observed, expected, abs_tol, &mismatch);
        
        if (!ok) {
            std::fprintf(stderr, "FAIL: comparison should have passed: %s\n", mismatch.c_str());
            return 1;
        }
        
        std::printf("oracle_contract: PASS: compare_abs_only passed for obs=%.8f exp=%.8f\n", observed, expected);
        return 0;
    }
    
    if (std::string(test_type) == "compare_fail") {
        std::string mismatch;
        double observed = 1.0;
        double expected = 2.0;
        double abs_tol = 1e-4;
        
        bool ok = oracle_contract::compare_abs_only("test_scenario_fail", observed, expected, abs_tol, &mismatch);
        
        if (ok) {
            std::fprintf(stderr, "FAIL: comparison should have failed\n");
            return 1;
        }
        
        std::printf("oracle_contract: FAIL: %s\n", mismatch.c_str());
        
        if (mismatch.find("observed=") == std::string::npos ||
            mismatch.find("expected=") == std::string::npos ||
            mismatch.find("abs_err=") == std::string::npos ||
            mismatch.find("abs_tol=") == std::string::npos ||
            mismatch.find("scenario=") == std::string::npos) {
            std::fprintf(stderr, "FAIL: mismatch output missing required fields\n");
            return 1;
        }
        
        std::printf("oracle_contract: PASS: mismatch output contains required fields\n");
        return 1;
    }
    
    std::fprintf(stderr, "Usage: test_oracle_contract [load|compare_pass|compare_fail]\n");
    return 1;
}
