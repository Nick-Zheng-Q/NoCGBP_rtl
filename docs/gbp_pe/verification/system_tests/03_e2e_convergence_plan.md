# GBP 端到端收敛测试计划

> 版本：2026-06-11 v2
> 目标：在参数化 mesh 上运行 synthetic_line/lattice/fr1desk_small 的完整 GBP 迭代，验证 ARE 收敛。

---

## 1. 测试目标

**输入**：图数据 txt 文件 + partition JSON（节点到 PE 的映射）
**输出**：运行 N 轮后所有 variable 节点的 belief → 计算 ARE
**标准**：ARE < threshold

| 图 | 数据格式 | 数据来源 |
|----|---------|---------|
| synthetic_line | txt（格式待定） | 后续生成 |
| synthetic_lattice | txt（格式待定） | 后续生成 |
| bal_fr1desk_small | BAL txt | `NoCBP/data/fr1desk_small.txt` |

**BAL txt 格式**：
```
n_cameras n_landmarks n_observations
fx fy cx cy
camera_id landmark_id pixel_u pixel_v   (× n_observations)
camera_mean[6] × n_cameras
landmark_mean[3] × n_landmarks
```

---

## 2. 验证策略

### 为什么不直接对比数值

RTL 和 GBPSim 的调度顺序不同。GBP 可以使用过期消息（stale messages），所以：
- 不同调度顺序 → 不同的中间消息 → 不同的最终 belief
- 这是 GBP 的正常行为，不是 bug
- 因此**不能**逐节点对比数值

### 验证标准

**ARE 收敛到阈值以下**。这是 GBP 的核心指标：
- `are()` = 所有因子节点的残差平均值
- BA 图：reprojection error
- 线性图：residual norm

### 计算方式

在 Verilator testbench 中**自己实现 ARE 计算**，不链接外部库：
1. 从 SPM 读取所有 variable 节点的 belief（eta, lambda）
2. 从图数据中获取因子参数（measurement, Jacobian）
3. 对每个因子：用 connected variables 的 belief 计算残差
4. ARE = sum(residuals) / n_factors

---

## 3. 迭代控制

### 自调度模式

PE 释放 reset 后自动运行：schedule → scan → fetch → compute → writeback → repeat。

**控制方式**：通过 C++ testbench 监控 `phase_switch_pulse_o` 信号：
- 每次 factor→variable 或 variable→factor 切换时，`phase_switch_pulse_o` 脉冲一次
- 计数脉冲数 = 迭代轮数
- 到达目标轮数后，停止仿真，读取结果

**停止条件**：
- 达到目标迭代轮数（如 50 轮）
- 或超时（如 10000 cycles）

---

## 4. SPM 初始化

### 4.1 数据来源

- **图结构**：partition JSON（fac_mapping_table, var_mapping_table, factor_var_edges）
- **节点状态**：txt 数据文件（prior eta/lambda, measurement, Jacobian）
- **分区信息**：partition JSON（mesh 维度，每个 PE 的节点列表）

### 4.2 初始化流程

```
1. 解析 partition JSON → 获取 mesh 维度、每个 PE 的节点列表
2. 解析 txt 数据文件 → 获取所有节点的状态数据
3. 对每个 PE：
   a. 写入 NodeHeader（node_id, dof, adj_count, adj_base, state_base, state_words）
   b. 写入 FwdEdgeArray（neighbor_id, neighbor_x, neighbor_y, is_local）
   c. 写入 RevKeyHash + RevHeader + RevEntryArray（reverse CSR）
   d. 写入 STATE（prior eta/lambda for variables, messages+measurement+Jacobian for factors）
4. 验证：读回 NodeHeader 确认正确
```

### 4.3 NodeHeader 位布局

需要与 Verilator 的 `+:` 操作符编码一致。根据实际测试，Verilator 将所有字段放在 bits[35:18]：
- `node_id` = bits[9:0]
- `dof` = bits[13:10]
- `adj_count` = bits[17:14]
- `adj_base` = bits[35:18]（与 state_base 共享）
- `state_base` = bits[35:18]（与 adj_base 共享）
- `state_words` = bits[44:36]（Verilator 实际位置）

**注意**：由于 Verilator 编码问题，adj_base 和 state_base 共享同一个位域。需要 adj_base == state_base，或者在 C++ 中用与 Verilator 一致的位布局。

---

## 5. ARE 计算

### 5.1 BA 图的 ARE

对于 Bundle Adjustment 图，ARE = mean reprojection error：

```cpp
double compute_are_ba(
    const std::vector<CameraNode>& cameras,  // 6 DOF each (rotation + translation)
    const std::vector<LandmarkNode>& landmarks,  // 3 DOF each (x, y, z)
    const std::vector<Observation>& observations,  // (cam_id, lmk_id, pixel_u, pixel_v)
    const Eigen::Matrix3d& K  // camera intrinsics
) {
    double total_error = 0.0;
    for (const auto& obs : observations) {
        // Reproject landmark through camera
        Eigen::Vector3d point = landmarks[obs.lmk_id].pos;
        Eigen::Vector3d rotated = cameras[obs.cam_id].rotation * point;
        Eigen::Vector3d translated = rotated + cameras[obs.cam_id].translation;
        Eigen::Vector2d projected(translated[0] / translated[2], translated[1] / translated[2]);
        Eigen::Vector2d pixel = K.block<2,2>(0,0) * projected + K.block<2,1>(0,2);
        
        // Reprojection error
        double err = (pixel - Eigen::Vector2d(obs.u, obs.v)).norm();
        total_error += err;
    }
    return total_error / observations.size();
}
```

### 5.2 线性图的 ARE

对于线性图（LinearFactorGraph），ARE = mean residual norm：

```cpp
double compute_are_linear(
    const std::vector<VariableNode>& variables,
    const std::vector<FactorNode>& factors
) {
    double total_residual = 0.0;
    for (const auto& factor : factors) {
        // residual = measurement - H * belief
        // For binary factor connecting V_i and V_j:
        //   residual = z - (belief_j - belief_i)
        Eigen::VectorXd residual = factor.measurement;
        for (int k = 0; k < factor.connected_vars.size(); k++) {
            residual -= factor.J[k] * variables[factor.connected_vars[k]].belief;
        }
        total_residual += residual.norm();
    }
    return total_residual / factors.size();
}
```

### 5.3 从 SPM 读取 beliefs

```cpp
struct VariableBelief {
    int node_id;
    int dof;
    std::vector<float> eta;    // information vector
    std::vector<float> lambda; // upper-triangular precision matrix
};

std::vector<VariableBelief> read_beliefs_from_spm(int pe_id, const std::vector<int>& var_node_ids) {
    std::vector<VariableBelief> beliefs;
    for (int node_id : var_node_ids) {
        VariableBelief b;
        b.node_id = node_id;
        // Read NodeHeader to get state_base and dof
        auto header = spm_read_node_header(pe_id, node_id);
        b.dof = header.dof;
        int compact_words = b.dof + b.dof * (b.dof + 1) / 2;
        // Read state words
        for (int i = 0; i < compact_words; i++) {
            uint32_t word = spm_read_word(pe_id, header.state_base + i);
            if (i < b.dof) {
                b.eta.push_back(uint32_to_float(word));
            } else {
                b.lambda.push_back(uint32_to_float(word));
            }
        }
        beliefs.push_back(b);
    }
    return beliefs;
}
```

---

## 6. 测试流程

### 6.1 端到端测试

```
1. 加载 partition JSON → 获取 mesh 维度和节点映射
2. 加载图数据 txt → 获取节点状态和因子参数
3. 编译对应的 mesh top（参数化 MESH_X, MESH_Y）
4. 初始化所有 PE 的 SPM
5. 释放 reset
6. 等待 N 轮迭代（监控 phase_switch_pulse_o）
7. 从 SPM 读取所有 variable 节点的 belief
8. 计算 ARE
9. 检查 ARE < threshold → PASS/FAIL
```

### 6.2 最小可行测试

先用最简单的图跑通流程：
1. **synthetic_line 2×1**：2 个 PE，16 个 variable 节点，15 个 factor 节点
2. 验证 SPM 初始化、自调度、ARE 计算全部工作
3. 然后扩展到 lattice 和 fr1desk_small

---

## 7. 文件结构

```
nocbp_verilator/tests/system/
├── configs/                              (已有 partition JSON)
├── mesh_NxM_gbp_top.sv                  (新建: 参数化 mesh top)
├── mesh_NxM_gbp_interconnect.cc         (新建: 统一 testbench)
├── mesh_NxM_gbp_interconnect.f          (新建: file list)
├── mesh_NxM_gbp_interconnect.yaml       (新建: test config)
└── golden_ref/
    └── are_calculator.hpp               (新建: ARE 计算)

nocbp_verilator/tests/tools/
├── parse_bal_txt.py                     (新建: 解析 BAL txt)
├── parse_synthetic_txt.py               (新建: 解析 synthetic txt)
├── gen_spm_init.py                      (新建: 生成 SPM 初始化数据)
└── run_e2e_convergence.py               (新建: 端到端测试脚本)
```

---

## 8. 实施步骤

### Phase 1: 数据解析（1 天）

1. **`parse_bal_txt.py`**：解析 `fr1desk_small.txt`
   - 输出：cameras (6 DOF each), landmarks (3 DOF each), observations, intrinsics
   
2. **`parse_synthetic_txt.py`**：解析 synthetic txt（格式待定）

3. **`gen_spm_init.py`**：从图数据 + partition JSON 生成 SPM 初始化
   - 输入：txt 数据文件 + partition JSON
   - 输出：C++ header 文件（`spm_init_data.h`）包含每个 PE 的初始化数据

### Phase 2: 参数化 Mesh Top（1 天）

1. **`mesh_NxM_gbp_top.sv`**：参数化 mesh
   - 参数：MESH_X, MESH_Y
   - 复用现有 router 和 PE 模块

### Phase 3: ARE 计算（1-2 天）

1. **`are_calculator.hpp`**：纯 C++ 实现
   - `compute_are_ba()`：BA 图的 reprojection error
   - `compute_are_linear()`：线性图的 residual norm
   - 从 SPM 读取 beliefs 的辅助函数
   - 不依赖任何外部库

### Phase 4: 端到端测试（2-3 天）

1. **`mesh_NxM_gbp_interconnect.cc`**：
   - 加载 SPM 初始化数据
   - 释放 reset
   - 监控 phase_switch_pulse_o 计数迭代
   - 读取 beliefs
   - 计算 ARE
   - 输出 PASS/FAIL

2. **`run_e2e_convergence.py`**：
   - 自动化：生成 SPM init → 编译 → 运行 → 解析输出 → 报告

### Phase 5: 调试（持续）

- RTL bug 修复（你负责）
- 测试环境调试（我负责）

---

## 9. 待确认项

1. **synthetic 图的 txt 格式**：你生成后告诉我格式，我来写 parser
2. **ARE 阈值**：每个图的阈值是多少？用 oracle 的阈值（abs=1e-3, rel=1e-2）还是其他？
3. **迭代轮数**：每个图跑多少轮？50? 100?
4. **mesh 尺寸**：先用哪个尺寸跑通？2×1?
5. **partition JSON 的作用**：它定义了节点到 PE 的映射。在自调度模式下，PE 需要知道自己负责哪些节点。这个信息是通过 SPM 中的 NodeHeader 传递的，还是需要额外的配置？
