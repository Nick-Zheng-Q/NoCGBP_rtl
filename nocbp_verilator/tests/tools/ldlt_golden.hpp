// tests/tools/ldlt_golden.hpp
// Step-by-step golden reference for LDLT solve (A = L*D*L^T).
// Used by the ldlt_solve_core Verilator unit test.

#ifndef LDLT_GOLDEN_HPP
#define LDLT_GOLDEN_HPP

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ldlt {

using fp32_t = uint32_t;

inline float u2f(uint32_t u) {
  union { float f; uint32_t u; } c;
  c.u = u;
  return c.f;
}

inline uint32_t f2u(float f) {
  union { float f; uint32_t u; } c;
  c.f = f;
  return c.u;
}

struct Result {
  int d;
  int nrhs;
  std::vector<std::vector<float>> A; // d x d full symmetric matrix
  std::vector<std::vector<float>> L; // d x d unit lower triangular
  std::vector<float> D;              // d diagonal entries
  std::vector<std::vector<float>> y; // d x nrhs forward intermediate
  std::vector<std::vector<float>> z; // d x nrhs scaled intermediate
  std::vector<std::vector<float>> x; // d x nrhs final solution
};

// Convert packed upper-triangular row-major flat array to full symmetric matrix.
// Packed index for (row, col) with row <= col:
//   idx = row * d - row * (row - 1) / 2 + (col - row)
inline std::vector<std::vector<float>> unpack_A(const std::vector<fp32_t>& A_flat,
                                                int d) {
  std::vector<std::vector<float>> A(d, std::vector<float>(d, 0.0f));
  for (int row = 0; row < d; ++row) {
    int base = row * d - row * (row - 1) / 2;
    for (int col = row; col < d; ++col) {
      float v = u2f(A_flat[base + (col - row)]);
      A[row][col] = v;
      A[col][row] = v;
    }
  }
  return A;
}

// Golden LDLT solve with optional per-step printing.
inline Result solve(const std::vector<fp32_t>& A_flat,
                    const std::vector<std::vector<float>>& B,
                    int d, int nrhs, bool verbose = false) {
  Result r;
  r.d = d;
  r.nrhs = nrhs;
  r.A = unpack_A(A_flat, d);
  r.L.assign(d, std::vector<float>(d, 0.0f));
  r.D.assign(d, 0.0f);
  r.y.assign(d, std::vector<float>(nrhs, 0.0f));
  r.z.assign(d, std::vector<float>(nrhs, 0.0f));
  r.x.assign(d, std::vector<float>(nrhs, 0.0f));

  if (verbose) {
    std::fprintf(stderr, "[GOLDEN] A matrix:\n");
    for (int i = 0; i < d; ++i) {
      std::fprintf(stderr, "  row%d:", i);
      for (int j = 0; j < d; ++j) std::fprintf(stderr, " %g", r.A[i][j]);
      std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "[GOLDEN] B matrix:\n");
    for (int i = 0; i < d; ++i) {
      std::fprintf(stderr, "  row%d:", i);
      for (int c = 0; c < nrhs; ++c) std::fprintf(stderr, " %g", B[i][c]);
      std::fprintf(stderr, "\n");
    }
  }

  // Factorization: D[k] and L[i][k]
  for (int k = 0; k < d; ++k) {
    float acc = r.A[k][k];
    for (int j = 0; j < k; ++j) {
      acc -= r.L[k][j] * r.L[k][j] * r.D[j];
    }
    r.D[k] = acc;

    for (int i = k + 1; i < d; ++i) {
      float a_ik = r.A[i][k];
      for (int j = 0; j < k; ++j) {
        a_ik -= r.L[i][j] * r.L[k][j] * r.D[j];
      }
      r.L[i][k] = a_ik / r.D[k];
    }
  }

  if (verbose) {
    std::fprintf(stderr, "[GOLDEN] D:");
    for (int i = 0; i < d; ++i) std::fprintf(stderr, " %g", r.D[i]);
    std::fprintf(stderr, "\n[GOLDEN] L matrix:\n");
    for (int i = 0; i < d; ++i) {
      std::fprintf(stderr, "  row%d:", i);
      for (int j = 0; j < d; ++j) std::fprintf(stderr, " %g", r.L[i][j]);
      std::fprintf(stderr, "\n");
    }
  }

  // Forward solve: L * y = B
  for (int c = 0; c < nrhs; ++c) {
    for (int i = 0; i < d; ++i) {
      float acc = B[i][c];
      for (int j = 0; j < i; ++j) {
        acc -= r.L[i][j] * r.y[j][c];
      }
      r.y[i][c] = acc;
    }
  }

  if (verbose) {
    std::fprintf(stderr, "[GOLDEN] y (forward):\n");
    for (int i = 0; i < d; ++i) {
      std::fprintf(stderr, "  row%d:", i);
      for (int c = 0; c < nrhs; ++c) std::fprintf(stderr, " %g", r.y[i][c]);
      std::fprintf(stderr, "\n");
    }
  }

  // Diagonal scale: z = y / D
  for (int c = 0; c < nrhs; ++c) {
    for (int i = 0; i < d; ++i) {
      r.z[i][c] = r.y[i][c] / r.D[i];
    }
  }

  if (verbose) {
    std::fprintf(stderr, "[GOLDEN] z (diag scale):\n");
    for (int i = 0; i < d; ++i) {
      std::fprintf(stderr, "  row%d:", i);
      for (int c = 0; c < nrhs; ++c) std::fprintf(stderr, " %g", r.z[i][c]);
      std::fprintf(stderr, "\n");
    }
  }

  // Backward solve: L^T * x = z
  for (int c = 0; c < nrhs; ++c) {
    for (int i = d - 1; i >= 0; --i) {
      float acc = r.z[i][c];
      for (int j = i + 1; j < d; ++j) {
        acc -= r.L[j][i] * r.x[j][c];
      }
      r.x[i][c] = acc;
    }
  }

  if (verbose) {
    std::fprintf(stderr, "[GOLDEN] x (solution):\n");
    for (int i = 0; i < d; ++i) {
      std::fprintf(stderr, "  row%d:", i);
      for (int c = 0; c < nrhs; ++c) std::fprintf(stderr, " %g", r.x[i][c]);
      std::fprintf(stderr, "\n");
    }
  }

  return r;
}

} // namespace ldlt

#endif // LDLT_GOLDEN_HPP
