#pragma once
// are_calculator.hpp
// Pure C++ ARE (Average Reprojection Error / Average Residual) calculator.
// No external dependencies. Used in Verilator testbench.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace gbp {

// ============================================================================
// FP32 helpers
// ============================================================================
inline float u32_to_float(uint32_t u) {
  union { uint32_t u; float f; } c;
  c.u = u;
  return c.f;
}

inline uint32_t float_to_u32(float f) {
  union { float f; uint32_t u; } c;
  c.f = f;
  return c.u;
}

// ============================================================================
// Data structures
// ============================================================================

struct NodeHeader {
  uint32_t node_id;
  uint32_t dof;
  uint32_t adj_count;
  uint32_t adj_base;
  uint32_t state_base;
  uint32_t state_words;
};

struct VariableBelief {
  int node_id;
  int dof;
  std::vector<float> eta;     // length = dof
  std::vector<float> lambda;  // upper-triangular, length = dof*(dof+1)/2
};

struct FactorInfo {
  int factor_id;
  int dof;                      // measurement dimension
  std::vector<int> var_ids;     // connected variable node IDs
  std::vector<int> var_dofs;    // DOF of each connected variable
  std::vector<float> measurement;  // measurement residual (dof words)
  std::vector<float> jacobian;     // Jacobian matrix (dof * sum(var_dofs) words)
};

struct Observation {
  int cam_id;
  int lmk_id;
  float u;
  float v;
};

struct CameraIntrinsics {
  float fx, fy, cx, cy;
};

struct CameraPose {
  float rotation[9];     // 3x3 rotation matrix
  float translation[3];  // 3-vector
};

struct Landmark {
  float pos[3];  // 3D position
};

// ============================================================================
// NodeHeader decoding (matches RTL bit layout)
// ============================================================================

inline NodeHeader decode_node_header(uint32_t lo, uint32_t hi) {
  uint64_t h = (uint64_t)hi << 32 | (uint64_t)lo;
  NodeHeader hdr;
  hdr.node_id    = (h >> 0)  & 0x3FF;
  hdr.dof        = (h >> 10) & 0xF;
  hdr.adj_count  = (h >> 14) & 0xF;
  hdr.adj_base   = (h >> 18) & 0x3FFFF;
  hdr.state_base = (h >> 36) & 0x3FFFF;
  hdr.state_words = (h >> 54) & 0x1FF;
  return hdr;
}

// ============================================================================
// Compact payload helpers
// ============================================================================

inline int compact_words(int dof) {
  return dof + dof * (dof + 1) / 2;
}

// Decode compact payload (eta + upper-triangular lambda)
inline void decode_compact(const uint32_t* words, int dof,
                           std::vector<float>& eta,
                           std::vector<float>& lambda) {
  int idx = 0;
  for (int i = 0; i < dof; i++) {
    eta.push_back(u32_to_float(words[idx++]));
  }
  for (int i = 0; i < dof; i++) {
    for (int j = i; j < dof; j++) {
      lambda.push_back(u32_to_float(words[idx++]));
    }
  }
}

// Get lambda[i,j] from upper-triangular compact form
inline float get_lambda(const std::vector<float>& lam, int i, int j, int dof) {
  if (j < i) std::swap(i, j);  // ensure upper-triangular
  int idx = 0;
  for (int r = 0; r < i; r++) {
    idx += (dof - r);
  }
  idx += (j - i);
  return lam[idx];
}

// ============================================================================
// Linear graph ARE
// ============================================================================

// For a linear factor graph:
//   factor k connects variables i and j
//   measurement z_k (dof words)
//   Jacobian J_k (dof × (dof_i + dof_j) words)
//
//   residual = z_k - J_k * [belief_i; belief_j]
//   ARE = mean(||residual||)

inline double compute_are_linear(
    const std::vector<VariableBelief>& variables,
    const std::vector<FactorInfo>& factors
) {
  if (factors.empty()) return 0.0;

  double total_residual = 0.0;

  for (const auto& factor : factors) {
    int total_dof = 0;
    for (int d : factor.var_dofs) total_dof += d;

    // Build J * belief vector
    std::vector<double> j_belief(factor.dof, 0.0);
    int col_offset = 0;
    for (size_t v = 0; v < factor.var_ids.size(); v++) {
      // Find variable belief
      const VariableBelief* var = nullptr;
      for (const auto& vb : variables) {
        if (vb.node_id == factor.var_ids[v]) { var = &vb; break; }
      }
      if (!var) continue;

      int vd = factor.var_dofs[v];
      // J_k[:, col_offset:col_offset+vd] * belief_v
      for (int r = 0; r < factor.dof; r++) {
        for (int c = 0; c < vd; c++) {
          int j_idx = r * total_dof + col_offset + c;
          if (j_idx < (int)factor.jacobian.size()) {
            j_belief[r] += (double)factor.jacobian[j_idx] * (double)var->eta[c];
          }
        }
      }
      col_offset += vd;
    }

    // residual = measurement - J * belief
    double residual_norm = 0.0;
    for (int r = 0; r < factor.dof; r++) {
      double diff = (double)factor.measurement[r] - j_belief[r];
      residual_norm += diff * diff;
    }
    total_residual += std::sqrt(residual_norm);
  }

  return total_residual / factors.size();
}

// ============================================================================
// BA graph ARE
// ============================================================================

// Reprojection error for Bundle Adjustment:
//   point_3d = landmark.pos
//   rotated = R * point_3d + t
//   projected = [rotated.x / rotated.z, rotated.y / rotated.z]
//   pixel = K * projected
//   error = ||pixel - observation||

inline void rotation_matrix_from_euler(const float* rvec, float* R) {
  // Rodrigues' formula (simplified for small angles)
  float theta = std::sqrt(rvec[0]*rvec[0] + rvec[1]*rvec[1] + rvec[2]*rvec[2]);
  if (theta < 1e-10f) {
    // Identity + small correction
    R[0] = 1.0f; R[1] = 0.0f; R[2] = 0.0f;
    R[3] = 0.0f; R[4] = 1.0f; R[5] = 0.0f;
    R[6] = 0.0f; R[7] = 0.0f; R[8] = 1.0f;
    return;
  }
  float kx = rvec[0] / theta;
  float ky = rvec[1] / theta;
  float kz = rvec[2] / theta;
  float c = std::cos(theta);
  float s = std::sin(theta);
  float t = 1.0f - c;

  R[0] = c + t*kx*kx;      R[1] = t*kx*ky - s*kz;   R[2] = t*kx*kz + s*ky;
  R[3] = t*ky*kx + s*kz;   R[4] = c + t*ky*ky;       R[5] = t*ky*kz - s*kx;
  R[6] = t*kz*kx - s*ky;   R[7] = t*kz*ky + s*kx;    R[8] = c + t*kz*kz;
}

inline double compute_are_ba(
    const std::vector<CameraPose>& cameras,
    const std::vector<Landmark>& landmarks,
    const std::vector<Observation>& observations,
    const CameraIntrinsics& K
) {
  if (observations.empty()) return 0.0;

  double total_error = 0.0;

  for (const auto& obs : observations) {
    if (obs.cam_id >= (int)cameras.size() || obs.lmk_id >= (int)landmarks.size())
      continue;

    const auto& cam = cameras[obs.cam_id];
    const auto& lmk = landmarks[obs.lmk_id];

    // Rotate and translate
    float rotated[3];
    rotated[0] = cam.rotation[0]*lmk.pos[0] + cam.rotation[1]*lmk.pos[1] + cam.rotation[2]*lmk.pos[2] + cam.translation[0];
    rotated[1] = cam.rotation[3]*lmk.pos[0] + cam.rotation[4]*lmk.pos[1] + cam.rotation[5]*lmk.pos[2] + cam.translation[1];
    rotated[2] = cam.rotation[6]*lmk.pos[0] + cam.rotation[7]*lmk.pos[1] + cam.rotation[8]*lmk.pos[2] + cam.translation[2];

    if (std::abs(rotated[2]) < 1e-10f) continue;

    // Project
    float px = K.fx * (rotated[0] / rotated[2]) + K.cx;
    float py = K.fy * (rotated[1] / rotated[2]) + K.cy;

    // Reprojection error
    float du = px - obs.u;
    float dv = py - obs.v;
    total_error += std::sqrt(du*du + dv*dv);
  }

  return total_error / observations.size();
}

// ============================================================================
// ARE from belief vectors (for RTL testbench)
// ============================================================================

// Compute ARE given variable beliefs as raw float vectors
// beliefs[node_id] = {eta_0, eta_1, ..., eta_d-1, lam_00, lam_01, ..., lam_dd}
inline double compute_are_from_beliefs(
    const std::vector<std::vector<float>>& beliefs,
    const std::vector<int>& var_dofs,
    const std::vector<FactorInfo>& factors
) {
  std::vector<VariableBelief> variables;
  for (size_t i = 0; i < beliefs.size(); i++) {
    VariableBelief vb;
    vb.node_id = i;
    vb.dof = var_dofs[i];
    int d = vb.dof;
    for (int j = 0; j < d; j++) vb.eta.push_back(beliefs[i][j]);
    for (int j = d; j < d + d*(d+1)/2; j++) vb.lambda.push_back(beliefs[i][j]);
    variables.push_back(vb);
  }
  return compute_are_linear(variables, factors);
}

}  // namespace gbp
