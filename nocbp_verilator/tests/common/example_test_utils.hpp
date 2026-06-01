#ifndef NOCBP_VERILATOR_TESTS_COMMON_EXAMPLE_TEST_UTILS_HPP_
#define NOCBP_VERILATOR_TESTS_COMMON_EXAMPLE_TEST_UTILS_HPP_

// 示例：如何使用公共工具库
// 
// 这个文件展示了如何在测试中使用公共工具库来减少代码重复。
// 
// 使用方法：
// 1. 在测试文件中包含需要的头文件：
//    #include "tests/common/test_utils.hpp"
//    #include "tests/common/oracle_utils.hpp"
//    #include "tests/common/verilator_utils.hpp"
//
// 2. 使用命名空间：
//    using namespace test_utils;
//    using namespace oracle_utils;
//    using namespace verilator_utils;
//
// 3. 使用模板函数：
//    // 对于特定DUT类型的tick函数
//    template<>
//    void tick<Vmy_dut>(Vmy_dut* dut) {
//      dut->clk = 0;
//      dut->eval();
//      dut->clk = 1;
//      dut->eval();
//    }
//
// 示例测试结构：
//
// #include "verilated.h"
// #include "Vmy_dut.h"
// #include "tests/common/test_utils.hpp"
// #include "tests/common/oracle_utils.hpp"
// #include "tests/common/verilator_utils.hpp"
//
// using namespace test_utils;
// using namespace oracle_utils;
// using namespace verilator_utils;
//
// // 特化tick函数
// template<>
// void tick<Vmy_dut>(Vmy_dut* dut) {
//   dut->clk = 0;
//   dut->eval();
//   dut->clk = 1;
//   dut->eval();
// }
//
// int run_test(int argc, char** argv) {
//   verilator_init(argc, argv);
//   
//   auto* dut = new Vmy_dut;
//   reset_dut(dut);
//   
//   // 测试逻辑
//   if (!dut->ready_o) {
//     delete dut;
//     return fail("my_test", "ready_o should be high after reset");
//   }
//   
//   // 使用oracle工具
//   std::string oracle_data = load_file_content_or_fail("path/to/oracle.json", "my_test");
//   if (oracle_data.empty()) {
//     delete dut;
//     return 1;
//   }
//   
//   auto result = extract_json_double(oracle_data, "expected_value");
//   if (!result.success) {
//     delete dut;
//     return fail("my_test", "failed to extract expected_value from oracle");
//   }
//   
//   // 使用误差检查
//   if (!check_abs_error(dut->output, result.value, 1e-6, "output", 0, "test_case")) {
//     delete dut;
//     return 1;
//   }
//   
//   print_test_pass("my_test");
//   delete dut;
//   return 0;
// }

#endif  // NOCBP_VERILATOR_TESTS_COMMON_EXAMPLE_TEST_UTILS_HPP_