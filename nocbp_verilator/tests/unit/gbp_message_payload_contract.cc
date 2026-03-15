#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "verilated.h"
#include "Vgbp_message_payload_contract.h"

#include "../common/gbp_message_payload_codec.hpp"
#define GBP_MESSAGE_PAYLOAD_CODEC_AVAILABLE 1

namespace {

struct FixtureCase {
  std::string case_id;
  bool expect_valid = false;
  std::string expected_error;
  uint32_t schema_version = 0;
  std::string direction;
  std::string transport_direction;
  uint32_t bank = 0;
  uint32_t slot = 0;
  uint32_t dim = 0;
  uint32_t eta_len = 0;
  uint32_t lam_len = 0;
  std::vector<uint32_t> segment_payload_words;
  std::vector<uint32_t> segment_word_data;
  std::vector<float> expected_eta;
  std::vector<float> expected_lam;
  uint32_t expected_remaining_words = 0;
};

static void tick(Vgbp_message_payload_contract* dut) {
  dut->clk = 0;
  dut->eval();
  dut->clk = 1;
  dut->eval();
}

static std::string trim(const std::string& in) {
  size_t begin = 0;
  while (begin < in.size() && std::isspace(static_cast<unsigned char>(in[begin])) != 0) {
    ++begin;
  }
  size_t end = in.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1])) != 0) {
    --end;
  }
  return in.substr(begin, end - begin);
}

static std::vector<std::string> split_csv(const std::string& in) {
  std::vector<std::string> out;
  std::stringstream ss(in);
  std::string token;
  while (std::getline(ss, token, ',')) {
    const std::string compact = trim(token);
    if (!compact.empty()) {
      out.push_back(compact);
    }
  }
  return out;
}

static bool parse_bool_value(const std::string& value, bool* out) {
  if (value == "true" || value == "1") {
    *out = true;
    return true;
  }
  if (value == "false" || value == "0") {
    *out = false;
    return true;
  }
  return false;
}

static bool parse_u32(const std::string& value, uint32_t* out) {
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 0);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = static_cast<uint32_t>(parsed);
  return true;
}

static bool parse_float(const std::string& value, float* out) {
  char* end = nullptr;
  const float parsed = std::strtof(value.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = parsed;
  return true;
}

static bool parse_u32_csv(const std::string& value,
                          std::vector<uint32_t>* out,
                          std::string* error,
                          const char* key) {
  const std::vector<std::string> tokens = split_csv(value);
  out->clear();
  for (const std::string& token : tokens) {
    uint32_t parsed = 0;
    if (!parse_u32(token, &parsed)) {
      *error = std::string("invalid u32 in ") + key + ": " + token;
      return false;
    }
    out->push_back(parsed);
  }
  return true;
}

static bool parse_float_csv(const std::string& value,
                            std::vector<float>* out,
                            std::string* error,
                            const char* key) {
  const std::vector<std::string> tokens = split_csv(value);
  out->clear();
  for (const std::string& token : tokens) {
    float parsed = 0.0f;
    if (!parse_float(token, &parsed)) {
      *error = std::string("invalid float in ") + key + ": " + token;
      return false;
    }
    out->push_back(parsed);
  }
  return true;
}

static bool load_fixture(const std::string& path, FixtureCase* out, std::string* error) {
  std::ifstream in(path);
  if (!in.is_open()) {
    *error = "cannot open fixture: " + path;
    return false;
  }

  std::vector<std::pair<std::string, std::string>> kv;
  std::string line;
  int line_number = 0;
  while (std::getline(in, line)) {
    ++line_number;
    const std::string compact = trim(line);
    if (compact.empty() || compact[0] == '#') {
      continue;
    }
    const size_t colon = compact.find(':');
    if (colon == std::string::npos) {
      std::ostringstream oss;
      oss << "invalid fixture line " << line_number << " (missing ':')";
      *error = oss.str();
      return false;
    }
    const std::string key = trim(compact.substr(0, colon));
    const std::string value = trim(compact.substr(colon + 1));
    if (key.empty() || value.empty()) {
      std::ostringstream oss;
      oss << "invalid fixture line " << line_number << " (empty key/value)";
      *error = oss.str();
      return false;
    }
    kv.push_back(std::make_pair(key, value));
  }

  auto get_required = [&](const char* key, std::string* value) -> bool {
    for (const auto& item : kv) {
      if (item.first == key) {
        *value = item.second;
        return true;
      }
    }
    *error = std::string("missing required key: ") + key;
    return false;
  };

  FixtureCase parsed;
  std::string value;
  if (!get_required("case_id", &parsed.case_id) || !get_required("expect_valid", &value) ||
      !parse_bool_value(value, &parsed.expect_valid) || !get_required("expected_error", &parsed.expected_error) ||
      !get_required("schema_version", &value) || !parse_u32(value, &parsed.schema_version) ||
      !get_required("direction", &parsed.direction) ||
      !get_required("transport_direction", &parsed.transport_direction) || !get_required("bank", &value) ||
      !parse_u32(value, &parsed.bank) || !get_required("slot", &value) ||
      !parse_u32(value, &parsed.slot) || !get_required("dim", &value) || !parse_u32(value, &parsed.dim) ||
      !get_required("eta_len", &value) || !parse_u32(value, &parsed.eta_len) ||
      !get_required("lam_len", &value) || !parse_u32(value, &parsed.lam_len) ||
      !get_required("segment_payload_words", &value) ||
      !parse_u32_csv(value, &parsed.segment_payload_words, error, "segment_payload_words") ||
      !get_required("segment_word_data", &value) ||
      !parse_u32_csv(value, &parsed.segment_word_data, error, "segment_word_data") ||
      !get_required("expected_eta", &value) ||
      !parse_float_csv(value, &parsed.expected_eta, error, "expected_eta") ||
      !get_required("expected_lam", &value) ||
      !parse_float_csv(value, &parsed.expected_lam, error, "expected_lam") ||
      !get_required("expected_remaining_words", &value) || !parse_u32(value, &parsed.expected_remaining_words)) {
    if (error->empty()) {
      *error = "failed to parse fixture fields";
    }
    return false;
  }

  *out = parsed;
  return true;
}

static gbp_message_payload_codec::Direction parse_direction(const std::string& direction) {
  if (direction == "var_to_factor") {
    return gbp_message_payload_codec::Direction::kVarToFactor;
  }
  return gbp_message_payload_codec::Direction::kFactorToVar;
}

static bool nearly_equal(float a, float b) {
  return std::fabs(static_cast<double>(a - b)) <= 1e-6;
}

static bool check_vector_equal(const std::vector<float>& observed,
                               const std::vector<float>& expected,
                               std::string* error,
                               const char* label) {
  if (observed.size() != expected.size()) {
    std::ostringstream oss;
    oss << label << " size mismatch observed=" << observed.size() << " expected=" << expected.size();
    *error = oss.str();
    return false;
  }
  for (size_t i = 0; i < expected.size(); ++i) {
    if (!nearly_equal(observed[i], expected[i])) {
      std::ostringstream oss;
      oss << label << " mismatch at idx=" << i << " observed=" << observed[i]
          << " expected=" << expected[i];
      *error = oss.str();
      return false;
    }
  }
  return true;
}

static bool run_fixture(const FixtureCase& fixture, std::string* error) {
  gbp_message_payload_codec::EncodedPayload encoded;
  encoded.schema_version = fixture.schema_version;
  encoded.bank = fixture.bank;
  encoded.slot = fixture.slot;
  encoded.direction = parse_direction(fixture.direction);
  encoded.dim = fixture.dim;
  encoded.eta_len = fixture.eta_len;
  encoded.lam_len = fixture.lam_len;

  size_t offset = 0;
  for (size_t i = 0; i < fixture.segment_payload_words.size(); ++i) {
    gbp_message_payload_codec::Segment segment;
    segment.segment_idx = static_cast<uint32_t>(i);
    segment.segment_count = static_cast<uint32_t>(fixture.segment_payload_words.size());
    segment.segment_payload_words = fixture.segment_payload_words[i];
    for (uint32_t word_idx = 0; word_idx < segment.segment_payload_words; ++word_idx) {
      if (offset + word_idx >= fixture.segment_word_data.size()) {
        *error = "segment_word_data underflow against segment_payload_words";
        return false;
      }
      segment.words.push_back(fixture.segment_word_data[offset + word_idx]);
    }
    offset += segment.segment_payload_words;
    encoded.segments.push_back(segment);
  }

  if (offset < fixture.segment_word_data.size()) {
    for (size_t i = 0; i < fixture.segment_word_data.size() - offset; ++i) {
      encoded.segments.back().words.push_back(fixture.segment_word_data[offset + i]);
    }
  }

  gbp_message_payload_codec::DecodedPayload decoded;
  std::string decode_error;
  size_t consumed_words = 0;
  const bool decode_ok = gbp_message_payload_codec::decode(encoded, &decoded, &decode_error, &consumed_words);

  if (!fixture.expect_valid) {
    if (decode_ok) {
      *error = "expected invalid fixture to fail decode";
      return false;
    }
    if (decode_error.find(fixture.expected_error) == std::string::npos) {
      std::ostringstream oss;
      oss << "expected error marker='" << fixture.expected_error << "' observed='" << decode_error << "'";
      *error = oss.str();
      return false;
    }
    std::printf("GBP_MESSAGE_PAYLOAD_CONTRACT_EXPECTED_FAIL case=%s marker=%s\n",
                fixture.case_id.c_str(),
                fixture.expected_error.c_str());
    return true;
  }

  if (!decode_ok) {
    std::ostringstream oss;
    oss << "decode failed for valid fixture case=" << fixture.case_id << " error=" << decode_error;
    *error = oss.str();
    return false;
  }

  const uint32_t expected_payload_words =
      static_cast<uint32_t>(fixture.segment_word_data.size() - fixture.expected_remaining_words);
  if (consumed_words != expected_payload_words) {
    std::ostringstream oss;
    oss << "consumed_words mismatch case=" << fixture.case_id << " observed=" << consumed_words
        << " expected=" << expected_payload_words;
    *error = oss.str();
    return false;
  }

  if (decoded.schema_version != fixture.schema_version || decoded.bank != fixture.bank ||
      decoded.slot != fixture.slot || decoded.dim != fixture.dim) {
    *error = "decoded header mismatch";
    return false;
  }

  if (decoded.direction != parse_direction(fixture.transport_direction)) {
    *error = "decoded direction mismatch";
    return false;
  }

  if (!check_vector_equal(decoded.eta, fixture.expected_eta, error, "eta") ||
      !check_vector_equal(decoded.lam, fixture.expected_lam, error, "lam")) {
    return false;
  }

  gbp_message_payload_codec::EncodedPayload reencoded;
  std::string encode_error;
  if (!gbp_message_payload_codec::encode(decoded, &reencoded, &encode_error)) {
    std::ostringstream oss;
    oss << "encode failed for valid fixture case=" << fixture.case_id << " error=" << encode_error;
    *error = oss.str();
    return false;
  }

  gbp_message_payload_codec::DecodedPayload redecode;
  std::string redecode_error;
  size_t redecode_consumed = 0;
  if (!gbp_message_payload_codec::decode(reencoded, &redecode, &redecode_error, &redecode_consumed)) {
    std::ostringstream oss;
    oss << "decode(encode()) failed for case=" << fixture.case_id << " error=" << redecode_error;
    *error = oss.str();
    return false;
  }

  if (!check_vector_equal(redecode.eta, fixture.expected_eta, error, "roundtrip_eta") ||
      !check_vector_equal(redecode.lam, fixture.expected_lam, error, "roundtrip_lam")) {
    return false;
  }

  if (redecode_consumed == 0) {
    *error = "roundtrip consumed_words must be non-zero";
    return false;
  }

  std::printf("GBP_MESSAGE_PAYLOAD_CONTRACT_ROUNDTRIP_OK case=%s consumed_words=%zu\n",
              fixture.case_id.c_str(),
              consumed_words);
  return true;
}

}

int run_test(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);

  Vgbp_message_payload_contract* dut = new Vgbp_message_payload_contract;
  dut->rst_n = 0;
  tick(dut);
  dut->rst_n = 1;
  tick(dut);

  std::vector<std::string> fixture_paths;
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/factor_to_var_dim2.golden");
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/var_to_factor_dim3.golden");
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/variable_length_segmented_dim3.golden");
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/invalid_bad_version.fixture");
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/invalid_bad_bank_slot.fixture");
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/invalid_direction_mismatch.fixture");
  fixture_paths.push_back("tests/unit/data/gbp_message_payload_contract/invalid_zero_payload.fixture");

  for (const std::string& fixture_path : fixture_paths) {
    FixtureCase fixture;
    std::string load_error;
    if (!load_fixture(fixture_path, &fixture, &load_error)) {
      std::fprintf(stderr,
                   "gbp_message_payload_contract: FAIL: fixture_load path=%s error=%s\n",
                   fixture_path.c_str(),
                   load_error.c_str());
      delete dut;
      return 1;
    }

    std::string run_error;
    if (!run_fixture(fixture, &run_error)) {
      std::fprintf(stderr,
                   "gbp_message_payload_contract: FAIL: case=%s path=%s codec_available=%d error=%s\n",
                   fixture.case_id.c_str(),
                   fixture_path.c_str(),
                   GBP_MESSAGE_PAYLOAD_CODEC_AVAILABLE,
                   run_error.c_str());
      delete dut;
      return 1;
    }
  }

  std::printf("gbp_message_payload_contract: PASS\n");
  delete dut;
  return 0;
}
