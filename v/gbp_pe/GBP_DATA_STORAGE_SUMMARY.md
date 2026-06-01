# GBP PE 数据存储机制总结

## 1. 概述

GBP (General BP) PE 是基于 scratchpad memory (SPM) 的处理单元，采用银行分区机制来存储不同类型的数据。SPM 总容量为 1MB，由 8 个独立的银行组成，每 bank 128KB。

## 2. 核心参数

| 参数 | 值 | 说明 |
|------|-----|------|
| NB | 8 | 银行数量 |
| SPM_ADDR_W | 20 | SPM 地址宽度 (1MB) |
| BEAT_BYTES | 32 | 每次传输粒度 (256b) |
| ROW_ADDR_W | 12 | 每 bank 的行地址宽度 |
| TXN_ID_W | 8 | 事务 ID 宽度 |
| XFER_BYTES_W | 16 | 传输长度字段宽度 |
| STEP_BYTES_W | 8 | 步长字段宽度 |

## 3. 银行分区

```
┌─────────────────────────────────────────┐
│                SPM (1MB)                │
├──────┬──────┬──────┬──────┬────────────┤
│  B0  │ B1-B3│ B4-B7│      │            │
│ META │STATE │MSG   │      │            │
│32B   │512KB │512KB │      │            │
└──────┴──────┴──────┴──────┴────────────┘
```

- **B0**: META 和控制元数据
- **B1-B3**: STATE 载荷平面 (3个bank)
- **B4-B7**: MESSAGE 载荷平面 (4个bank)

## 4. META 数据结构

META 记录存储在 B0，每个活动工作项一个 META 记录。

### 4.1 META 格式 (32B = 256b)

| Word | 位域 | 字段 |
|------|------|------|
| word0[31:0] | [31:24] version | 版本号 (8b) |
| | [23:16] flags | 标志 (8b): valid, in_use, done |
| | [15:8] compute_op | 计算操作类型 (8b) |
| | [7:0] txn_id | 事务 ID (8b) |
| word1[31:0] | [31:12] state_base_addr | STATE 基地址 (20b) |
| | [11:9] state_bank_hint | STATE bank 提示 (3b) |
| word2[31:0] | [31:12] message_base_addr | MESSAGE 基地址 (20b) |
| | [11:9] message_bank_hint | MESSAGE bank 提示 (3b) |
| word3[31:0] | [31:16] state_xfer_bytes | STATE 传输字节数 (16b) |
| | [15:0] message_xfer_bytes | MESSAGE 传输字节数 (16b) |
| word4[31:0] | [31:24] state_step_bytes | STATE 步长 (8b) |
| | [23:16] message_step_bytes | MESSAGE 步长 (8b) |
| word5[31:0] | [31:16] state_count | STATE 数量 (16b) |
| | [15:0] message_count | MESSAGE 数量 (16b) |
| word6[31:0] | [31:0] meta_id | META ID (32b) |
| word7[31:0] | [31:0] checksum_or_reserved | 校验和或保留 (32b) |

### 4.2 META 解析流程

1. **读取 META**: 从 B0 读取 32B 的 META 记录
2. **解析字段**: 提取 txn_id, state_base_addr, message_base_addr 等
3. **银行映射**:
   - STATE 地址强制映射到 B1-B3
   - MESSAGE 地址强制映射到 B4-B7
4. **生成描述符**: 发射 STATE 和 MESSAGE 的读写描述符

## 5. STATE 和 MESSAGE 载荷

### 5.1 STATE 载荷 (B1-B3)

- **传输粒度**: 32B (一个 beat)
- **数据语义**: 对 compute 单元不透明 (opaque)
- **地址计算**:
  ```
  state_addr = state_base_addr + state_idx * state_step_bytes
  ```
- **银行约束**: 地址的 bank 位必须落在 B1-B3 范围内

### 5.2 MESSAGE 载荷 (B4-B7)

- **传输粒度**: 32B (一个 beat)
- **数据语义**: 不透明的读写载荷
- **地址计算**:
  ```
  msg_addr = message_base_addr + msg_idx * message_step_bytes
  ```
- **银行约束**: 地址的 bank 位必须落在 B4-B7 范围内

## 6. 流类型 (Stream Types)

| Stream Type | 值 | 用途 | 银行范围 |
|-------------|-----|------|----------|
| STREAM_META | 2'b00 | META 元数据 | B0 |
| STREAM_VEC | 2'b01 | STATE 向量数据 | B1-B3 |
| STREAM_MESSAGE | 2'b10 | MESSAGE 消息数据 | B4-B7 |

## 7. 描述符 (Descriptor) 格式

```systemverilog
typedef struct packed {
    op_e                     op;               // 读/写操作
    logic [TXN_ID_W-1:0]     txn_id;           // 事务 ID
    logic                    start;            // 起始信号
    logic [SPM_ADDR_W-1:0]   base_addr;        // 基地址
    logic [XFER_BYTES_W-1:0] xfer_bytes;       // 传输字节数
    logic [STEP_BYTES_W-1:0] addr_step_bytes;  // 步长
    logic [3:0]              operand_id;        // 操作数 ID
    wstrb_mode_e             wstrb_mode;       // 写掩码模式
    // ... 其他字段
} desc_t;
```

## 8. 数据流控制

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ control_unit│ --> │stream_disp. │ --> │ SPM Subsys. │
│  (解析META) │     │ (路由分发)  │     │  (银行阵列)  │
└─────────────┘     └─────────────┘     └──────┬──────┘
                                               │
                    ┌─────────────┐             │
                    │compute_unit │ <───────────┘
                    │ (计算单元)  │ ───────────>│
                    └─────────────┘             │
                                               │
                    ┌─────────────┐             │
                    │write_stream│ <───────────┘
                    │  (写引擎)   │ --> SPM (MESSAGE bank)
                    └─────────────┘
```

### 8.1 控制单元 (control_unit)

- 发出 META 读取请求
- 解析 META 字段
- 发射 STATE/MESSAGE 读取请求
- 触发计算单元

### 8.2 流分发器 (stream_dispatcher)

- 读取请求路由
- 根据 stream type 强制 bank 映射

### 8.3 计算单元 (compute_unit)

- 消费读取的 STATE/MESSAGE 数据
- 执行计算操作 (ADD/SUB/MUL/DIV)
- 发出写回载荷

### 8.4 写流引擎 (write_stream_engine)

- 将计算结果写入 MESSAGE bank (B4-B7)

## 9. 地址映射规则

### 9.1 SPM 地址格式

```
[SPM_ADDR_W-1:0] = [row_addr][bank_id][byte_offset]
                  20 bits   12      3         2
```

- **row_addr** (12 bits): 行地址
- **bank_id** (3 bits): 银行 ID (0-7)
- **byte_offset** (2 bits): 32B beat 内的字节偏移

### 9.2 银行强制映射

control_unit 中的 `force_state_bank()` 和 `force_message_bank()` 函数确保:

- STATE 地址的 bank 位被强制映射到 1-3 (B1-B3)
- MESSAGE 地址的 bank 位被强制映射到 4-7 (B4-B7)

## 10. 验证规则

1. META 读取正好是 32B (一个 beat)
2. 所有 stream class 的基地址都是 32B 对齐
3. 活跃 META 的 `state_xfer_bytes != 0` 且 `message_xfer_bytes != 0`
4. 无效的 class-to-bank 映射是测试失败
5. txn_id 在 META->STATE->MESSAGE 描述符链中保持不变

## 11. 总结

GBP PE 的数据存储机制采用银行分区的 SPM 设计:

1. **分离存储**: META/STATE/MESSAGE 三类数据分别存储在不同银行
2. **对齐传输**: 固定 32B beat 传输粒度
3. **事务追踪**: 通过 txn_id 追踪整个 META→STATE→MESSAGE 流程
4. **地址约束**: 硬件强制 bank 映射确保数据不出错
5. **不透明载荷**: STATE/MESSAGE 数据内容对硬件透明，允许算法迭代
