#ifndef GBP_MESSAGE_PAYLOAD_CODEC_HPP_
#define GBP_MESSAGE_PAYLOAD_CODEC_HPP_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gbp_message_payload_codec {

static const uint32_t kPhase1SchemaVersion = 1;
static const uint32_t kMessageBankLo = 4;
static const uint32_t kMessageBankHi = 7;
static const uint32_t kMessageSlotCount = kMessageBankHi - kMessageBankLo + 1;

enum class Direction {
  kFactorToVar,
  kVarToFactor,
};

struct Segment {
  uint32_t segment_idx = 0;
  uint32_t segment_count = 0;
  uint32_t segment_payload_words = 0;
  std::vector<uint32_t> words;
};

struct EncodedPayload {
  uint32_t schema_version = 0;
  uint32_t bank = 0;
  uint32_t slot = 0;
  Direction direction = Direction::kFactorToVar;
  uint32_t dim = 0;
  uint32_t eta_len = 0;
  uint32_t lam_len = 0;
  std::vector<Segment> segments;
};

struct DecodedPayload {
  uint32_t schema_version = 0;
  uint32_t bank = 0;
  uint32_t slot = 0;
  Direction direction = Direction::kFactorToVar;
  uint32_t dim = 0;
  std::vector<float> eta;
  std::vector<float> lam;
};

inline uint32_t expected_lam_len(uint32_t dim) {
  return (dim * (dim + 1)) / 2;
}

inline uint32_t encode_fp32(float value) {
  uint32_t word = 0;
  std::memcpy(&word, &value, sizeof(word));
  return word;
}

inline float decode_fp32(uint32_t word) {
  float value = 0.0f;
  std::memcpy(&value, &word, sizeof(value));
  return value;
}

inline Direction direction_for_slot(uint32_t slot) {
  return ((slot & 0x1u) == 0u) ? Direction::kFactorToVar : Direction::kVarToFactor;
}

inline bool validate_schema(uint32_t schema_version, std::string* error) {
  if (schema_version != kPhase1SchemaVersion) {
    if (error != nullptr) {
      *error = "bad_version";
    }
    return false;
  }
  return true;
}

inline bool validate_bank_slot(uint32_t bank, uint32_t slot, std::string* error) {
  if (bank < kMessageBankLo || bank > kMessageBankHi || slot >= kMessageSlotCount ||
      bank != (kMessageBankLo + slot)) {
    if (error != nullptr) {
      *error = "bad_bank_slot";
    }
    return false;
  }
  return true;
}

inline bool validate_direction(Direction direction, uint32_t slot, std::string* error) {
  if (direction != direction_for_slot(slot)) {
    if (error != nullptr) {
      *error = "direction_mismatch";
    }
    return false;
  }
  return true;
}

inline bool validate_shape(uint32_t dim,
                           uint32_t eta_len,
                           uint32_t lam_len,
                           std::string* error) {
  if (dim == 0) {
    if (error != nullptr) {
      *error = "invalid_dim";
    }
    return false;
  }
  if (eta_len != dim || lam_len != expected_lam_len(dim)) {
    if (error != nullptr) {
      *error = "length_mismatch";
    }
    return false;
  }
  return true;
}

inline bool extract_payload_words(const EncodedPayload& payload,
                                  uint32_t required_words,
                                  std::vector<uint32_t>* out,
                                  std::string* error) {
  if (payload.segments.empty()) {
    if (error != nullptr) {
      *error = "missing_segments";
    }
    return false;
  }

  out->clear();
  out->reserve(required_words);

  uint32_t declared_payload_words = 0;
  const uint32_t segment_count = static_cast<uint32_t>(payload.segments.size());
  for (uint32_t idx = 0; idx < segment_count; ++idx) {
    const Segment& segment = payload.segments[idx];
    if (segment.segment_count != segment_count || segment.segment_idx != idx) {
      if (error != nullptr) {
        *error = "segment_header_mismatch";
      }
      return false;
    }
    if (segment.words.size() < segment.segment_payload_words) {
      if (error != nullptr) {
        *error = "segment_word_underflow";
      }
      return false;
    }

    declared_payload_words += segment.segment_payload_words;
    const uint32_t remaining = required_words - static_cast<uint32_t>(out->size());
    const uint32_t take_words = std::min(segment.segment_payload_words, remaining);
    for (uint32_t word_idx = 0; word_idx < take_words; ++word_idx) {
      out->push_back(segment.words[word_idx]);
    }
  }

  if (declared_payload_words != required_words) {
    if (error != nullptr) {
      *error = "payload_length_mismatch";
    }
    return false;
  }
  if (out->size() != required_words) {
    if (error != nullptr) {
      *error = "payload_underflow";
    }
    return false;
  }
  return true;
}

inline bool validate_payload_words(const std::vector<uint32_t>& payload_words,
                                   std::string* error) {
  bool all_zero = true;
  for (uint32_t word : payload_words) {
    const float value = decode_fp32(word);
    if (!std::isfinite(static_cast<double>(value))) {
      if (error != nullptr) {
        *error = "invalid_fp32_payload";
      }
      return false;
    }
    if (word != 0u) {
      all_zero = false;
    }
  }

  if (all_zero) {
    if (error != nullptr) {
      *error = "invalid_zero_payload";
    }
    return false;
  }
  return true;
}

inline bool decode(const EncodedPayload& encoded,
                   DecodedPayload* decoded,
                   std::string* error,
                   size_t* words_consumed) {
  if (decoded == nullptr) {
    if (error != nullptr) {
      *error = "decode_null_output";
    }
    if (words_consumed != nullptr) {
      *words_consumed = 0;
    }
    return false;
  }

  if (words_consumed != nullptr) {
    *words_consumed = 0;
  }

  std::string local_error;
  std::string* error_out = (error == nullptr) ? &local_error : error;
  error_out->clear();

  if (!validate_schema(encoded.schema_version, error_out) ||
      !validate_bank_slot(encoded.bank, encoded.slot, error_out) ||
      !validate_direction(encoded.direction, encoded.slot, error_out) ||
      !validate_shape(encoded.dim, encoded.eta_len, encoded.lam_len, error_out)) {
    return false;
  }

  const uint32_t total_payload_words = encoded.eta_len + encoded.lam_len;
  std::vector<uint32_t> payload_words;
  if (!extract_payload_words(encoded, total_payload_words, &payload_words, error_out) ||
      !validate_payload_words(payload_words, error_out)) {
    return false;
  }

  decoded->schema_version = encoded.schema_version;
  decoded->bank = encoded.bank;
  decoded->slot = encoded.slot;
  decoded->direction = direction_for_slot(encoded.slot);
  decoded->dim = encoded.dim;
  decoded->eta.clear();
  decoded->lam.clear();
  decoded->eta.reserve(encoded.eta_len);
  decoded->lam.reserve(encoded.lam_len);

  for (uint32_t eta_idx = 0; eta_idx < encoded.eta_len; ++eta_idx) {
    decoded->eta.push_back(decode_fp32(payload_words[eta_idx]));
  }
  for (uint32_t lam_idx = 0; lam_idx < encoded.lam_len; ++lam_idx) {
    decoded->lam.push_back(decode_fp32(payload_words[encoded.eta_len + lam_idx]));
  }

  if (words_consumed != nullptr) {
    *words_consumed = static_cast<size_t>(total_payload_words);
  }
  return true;
}

inline bool encode(const DecodedPayload& decoded, EncodedPayload* encoded, std::string* error) {
  if (encoded == nullptr) {
    if (error != nullptr) {
      *error = "encode_null_output";
    }
    return false;
  }

  std::string local_error;
  std::string* error_out = (error == nullptr) ? &local_error : error;
  error_out->clear();

  if (!validate_schema(decoded.schema_version, error_out) ||
      !validate_bank_slot(decoded.bank, decoded.slot, error_out) ||
      !validate_direction(decoded.direction, decoded.slot, error_out)) {
    return false;
  }

  const uint32_t dim = decoded.dim;
  const uint32_t eta_len = static_cast<uint32_t>(decoded.eta.size());
  const uint32_t lam_len = static_cast<uint32_t>(decoded.lam.size());
  if (!validate_shape(dim, eta_len, lam_len, error_out)) {
    return false;
  }

  std::vector<uint32_t> words;
  words.reserve(static_cast<size_t>(eta_len + lam_len));
  for (float value : decoded.eta) {
    if (!std::isfinite(static_cast<double>(value))) {
      *error_out = "invalid_fp32_payload";
      return false;
    }
    words.push_back(encode_fp32(value));
  }
  for (float value : decoded.lam) {
    if (!std::isfinite(static_cast<double>(value))) {
      *error_out = "invalid_fp32_payload";
      return false;
    }
    words.push_back(encode_fp32(value));
  }

  if (!validate_payload_words(words, error_out)) {
    return false;
  }

  encoded->schema_version = decoded.schema_version;
  encoded->bank = decoded.bank;
  encoded->slot = decoded.slot;
  encoded->direction = decoded.direction;
  encoded->dim = decoded.dim;
  encoded->eta_len = eta_len;
  encoded->lam_len = lam_len;
  encoded->segments.clear();

  Segment segment;
  segment.segment_idx = 0;
  segment.segment_count = 1;
  segment.segment_payload_words = static_cast<uint32_t>(words.size());
  segment.words = words;
  encoded->segments.push_back(segment);

  return true;
}

}

#endif
