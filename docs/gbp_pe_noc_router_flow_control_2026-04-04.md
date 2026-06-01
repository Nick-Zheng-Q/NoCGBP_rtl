# GBP PE NoC 架构与流控详细分析

## 前置说明
- 文档目标：详细说明 `bsg_manycore` 中 `gbp_pe` 所连接的 NoC 架构，重点覆盖 router 设计、flow control、endpoint/bridge 行为，以及 `gbp_pe` 如何挂接到 manycore 网络。
- 分析基于 2026-04-04 当前仓库代码。
- 重点源码：
  - `v/bsg_manycore_tile_compute_array_mesh.sv`
  - `v/bsg_manycore_tile_compute_mesh.sv`
  - `v/bsg_manycore_hetero_socket.sv`
  - `v/bsg_manycore_mesh_node.sv`
  - `basejump_stl/bsg_noc/bsg_mesh_router_buffered.sv`
  - `basejump_stl/bsg_noc/bsg_mesh_router.sv`
  - `basejump_stl/bsg_noc/bsg_mesh_router_decoder_dor.sv`
  - `v/bsg_manycore_endpoint.sv`
  - `v/bsg_manycore_endpoint_fc.sv`
  - `v/bsg_manycore_endpoint_standard.sv`
  - `v/gbp_pe/gbp_pe.sv`
  - `v/gbp_pe/gbp_pe_endpoint_adapter.sv`
  - `v/gbp_pe/gbp_pe_noc_bridge.sv`
  - `v/gbp_pe/pe_top.sv`
  - `v/gbp_pe/gbp_pkg.sv`

## 1. 总体层次

当前 `gbp_pe` 所在的 manycore 网络可以从外到内分成 4 层：

1. `bsg_manycore_tile_compute_array_mesh`
   - 负责把多个 compute tile 拼成二维 mesh。
   - 处理横向、纵向链路的 stitch。
   - 管理每个 tile 的全局坐标和 reset 级联。

2. `bsg_manycore_tile_compute_mesh`
   - 一个 tile 内部包含：
     - 一个 `bsg_manycore_mesh_node`
     - 一个 `bsg_manycore_hetero_socket`
     - barrier 逻辑
   - `mesh_node` 负责 P/W/E/N/S 五个方向的网络路由。
   - `hetero_socket` 负责根据 `hetero_type_p` 选择 tile 内核类型。

3. `bsg_manycore_hetero_socket`
   - `hetero_type_p == 8` 时实例化 `gbp_pe`。
   - 所以 `gbp_pe` 本质上是 manycore 异构 tile 中的一类本地处理器核。

4. `gbp_pe`
   - 它不是直接连 router，而是先通过 `gbp_pe_endpoint_adapter` 接 `bsg_manycore_endpoint_standard`。
   - 之后再通过 `gbp_pe_noc_bridge` 把 manycore 请求翻译成：
     - sideband 命令
     - ingress 写入意图
   - 最后进入 `pe_top`，驱动 `control_unit_gbp + stream engines + compute_unit_wrapper + spm_subsystem`。

一句话概括：

`mesh -> tile -> hetero socket -> endpoint/adapter -> noc bridge -> pe_top`

## 2. NoC 链路与包格式

### 2.1 链路结构

manycore 使用的链路不是单一通道，而是一个 `link_sif`，里面包含两条独立逻辑链路：

- `fwd`
  - 传 request packet
  - 包括远程 load/store/sw/amo 等请求
- `rev`
  - 传 return packet
  - 包括 load 返回值、credit 型返回、int/float wb 返回

`bsg_manycore_link_sif_s` 在 `v/bsg_manycore_defines.svh` 中定义为：

- `fwd : bsg_manycore_fwd_link_sif_s`
- `rev : bsg_manycore_rev_link_sif_s`

每条 link 都采用 `ready_and` 风格结构，字段为：

- `v`
- `ready_and_rev`
- `data`

定义见 `basejump_stl/bsg_noc/bsg_noc_links.svh`。

### 2.2 forward packet

forward request packet `bsg_manycore_packet_s` 的主要字段：

- `addr`
- `op_v2`
- `reg_id`
- `payload`
- `src_y_cord`
- `src_x_cord`
- `y_cord`
- `x_cord`

也就是说，forward 包自己带完整的源坐标与目标坐标，router 只需要从 packet 数据中抽出 `x_cord/y_cord` 就能做 dimension-ordered routing。

### 2.3 reverse packet

reverse return packet `bsg_manycore_return_packet_s` 的主要字段：

- `pkt_type`
- `data`
- `reg_id`
- `y_cord`
- `x_cord`

reverse 包不再携带请求地址，而是只携带返回类型、返回数据和返回目标坐标。

### 2.4 `gbp_pe` 自己定义的本地 ingress/MMIO 地址语义

在 `v/gbp_pe/gbp_pkg.sv` 中，`gbp_pe` 额外定义了一套本地 ingress 地址约定：

- `B0`：MMIO bank
- `B1-B3`：forward/direct ingress bank
- `B4-B7`：payload bank

同时对 `B0` 定义了字段：

- `Q_BASE_ADDR`
- `Q_DEPTH`
- `Q_HEAD`
- `Q_TAIL`
- `Q_CREDIT`
- `Q_EPOCH_DOORBELL`

这不是 manycore 通用协议，而是 `gbp_pe` 自己在 endpoint/bridge 上定义的一层本地命令语义。

## 3. Router 设计

## 3.1 `bsg_manycore_mesh_node` 的核心结构

`bsg_manycore_mesh_node` 里实际上有两套 router：

- 一套 forward router
- 一套 reverse router

对应代码：

- forward：`bsg_mesh_router_buffered(... XY_order_p = 1 ...)`
- reverse：`bsg_mesh_router_buffered(... XY_order_p = 0 ...)`

这说明：

- forward 网络默认按 `X -> Y` 做严格 XY 路由
- reverse 网络默认按 `Y -> X` 做相反顺序的维序路由

当前 `gbp_pe` 所在 tile 配置里：

- `dims_p = 2`
- ruche 逻辑未启用
- 有效方向就是 `P/W/E/N/S`

其中 `P=0, W, E, N, S` 在 `basejump_stl/bsg_noc/bsg_noc_pkg.sv` 里定义。

## 3.2 `bsg_mesh_router_buffered`

这是 router 的“带输入缓冲”封装层，不直接做路由判定，而是先做每端口的输入 FIFO 和端口级流控。

每个输入端口都会先进入一个 `bsg_fifo_1r1w_small`：

- 输入：`link_i_cast[i].v/data`
- 输出：`fifo_valid[i]/fifo_data[i]`
- 出队：`fifo_yumi[i]`

然后再把这些 FIFO 的输出喂给内部的 `bsg_mesh_router`。

因此这个模块承担 3 个职责：

1. 输入缓冲
2. 可选 credit 化的 `ready_and_rev`
3. 可选 output repeater

### 3.2.1 输入缓冲

对每个端口：

- 若 `stub_p[i] = 1`，该端口被裁掉，不接收数据
- 否则分配一个小 FIFO

这意味着 router 本身不是完全组合透传，而是“每输入口先打一个缓冲，再参与路由仲裁”。

### 3.2.2 可选 credit 模式

`bsg_mesh_router_buffered` 支持 `use_credits_p[i]`。

若 `use_credits_p[i] = 0`：

- `ready_and_rev = fifo_ready_lo`
- 上游看到的是普通 ready/valid 反压

若 `use_credits_p[i] = 1`：

- `ready_and_rev` 不再表示“当前拍 ready”
- 它被注册成 `fifo_yumi[i]` 的延后一拍
- 也就是“上一拍 router 真正消费了一个 flit，于是返还一个 credit”

这很关键：

- 同样一个信号名 `ready_and_rev`
- 在 credit 模式下语义已经不是组合 ready
- 而是离散返还的 credit pulse

这也是为什么代码里专门注释“which direction in the router to use credit interface”。

### 3.2.3 当前 `gbp_pe` tile 是否启用 router credit

在 `v/bsg_manycore_tile_compute_mesh.sv` 里：

- 只有 `hetero_type_p == 0` 时
  - `fwd_use_credits_lp = 5'b00001`
  - `rev_use_credits_lp = 5'b00001`
- 也就是只给 `P` 端口启用 credit

而 `gbp_pe` 对应 `hetero_type_p == 8`，所以：

- forward router：不启用 P 端口 credit
- reverse router：不启用 P 端口 credit

这意味着当前 `gbp_pe` tile 与本地 router 的 P 口仍是普通 ready/valid 反压，不是 router-level credit 接口。

## 3.3 `bsg_mesh_router`

真正的路由选择与仲裁在 `bsg_mesh_router` 里完成。

它做的事情可以拆成 4 步：

1. 从每个输入 flit 的 `data_i` 中抽取 `x_cord/y_cord`
2. 通过 `bsg_mesh_router_decoder_dor` 计算每个输入想去哪个输出方向
3. 对每个输出方向收集全部请求者
4. 对每个输出方向做 round-robin 仲裁，并用 one-hot mux 选出数据

### 3.3.1 路由判定

每个输入方向都会实例化一个 `bsg_mesh_router_decoder_dor`。

输入：

- `x_dirs_i`
- `y_dirs_i`
- `my_x_i`
- `my_y_i`
- `from_p`

输出：

- one-hot `req_o`

如果 `x_eq & y_eq`：

- 请求 `P` 端口

否则：

- 按维序路由选择 `W/E/N/S`

在当前 2D mesh、无 ruche 的配置下，逻辑很直接：

- `XY_order_p = 1` 时：
  - 先看 X
  - X 相等后再看 Y
- `XY_order_p = 0` 时：
  - 先看 Y
  - Y 相等后再看 X

### 3.3.2 输出侧仲裁

`bsg_mesh_router` 不是“每个输入自己决定走哪条线然后直接驱动”，而是“每个输出方向自带一个仲裁器”。

每个输出方向的流程是：

1. `bsg_array_concentrate_static`
   - 把允许通向这个输出的输入数据收拢起来
2. `bsg_concentrate_static`
   - 把对应的请求位也收拢起来
3. `bsg_arb_round_robin`
   - 在所有请求者之间轮询仲裁
4. `bsg_mux_one_hot`
   - 用 grant 选择最终输出数据
5. `bsg_unconcentrate_static`
   - 把 `yumi` 展回原始输入维度

所以这个 router 的 arbitration policy 是：

- 每个输出方向独立 round-robin
- 同一时刻一个输出最多服务一个输入
- 一个输入同一时刻最多被一个输出消费

### 3.3.3 `yumi` 的含义

输入 FIFO 的真正出队条件不是“请求存在”，而是：

- 该输入赢得某个输出方向仲裁
- 且目标输出 `ready_and_i[i]` 为高

只有这时，`yumi_o[i]` 才拉高，输入 FIFO 才会弹出一个 flit。

因此数据移动是严格受目标输出 ready 约束的。

## 4. NoC Flow Control 分层分析

整个 manycore 的 flow control 不是单层的，而是 3 层叠加：

1. router 端口级 ready/credit
2. endpoint 的 request/response FIFO 约束
3. endpoint_fc 对“是否允许把一个 request 暴露给本地核”的 credit 保证

### 4.1 Router 级 flow control

router 级靠的是每个输入端口的小 FIFO，以及输出方向的 `ready_and_i`。

本质规则是：

- 输入先入 FIFO
- 输出仲裁成功且目标 ready 时，输入 FIFO 才出队
- ready 可选地退化成“返还 credit”

因此 router 自身保证的是：

- 不会无缓冲直接顶在上游
- 不会在目标输出未 ready 时误消费输入

### 4.2 `bsg_manycore_endpoint`

`bsg_manycore_endpoint` 是 mesh node P 口和本地核之间的基本端点。

它内部做了两件事：

1. forward incoming request 先进一个小 FIFO
   - `link_sif_in.fwd -> fifo -> packet_o/packet_v_o`
2. reverse incoming response 先进一个 `bsg_two_fifo`
   - `link_sif_in.rev -> returned_fifo -> return_packet_o/return_packet_v_o`

关键特点：

- incoming request 允许本地核延后消费
- incoming response 也先缓冲，不要求本地核同拍接住
- reverse ready 常量拉高：
  - `link_sif_out.rev.ready_and_rev = 1'b1`

也就是说，端点对网络承诺：

- reverse channel 总是可接收
- 如果本地 return FIFO 满了，设计要求下游必须及时 dequeue

### 4.3 `bsg_manycore_endpoint_fc`

这是 `endpoint` 上面再加的一层 flow-control 安全壳。

它做两件额外的事：

1. 确保 incoming request 只有在“本地一定还有 response 槽位”时才暴露给本地核
2. 统计 outgoing request 的 outstanding credits

#### 4.3.1 为什么要 gate incoming request

代码里的核心判断是：

- `rev_fifo_credit_available = rev_fifo_credit_r + return_packet_credit_or_ready_lo`
- `rev_fifo_has_space = (rev_fifo_credit_available != 0)`
- `packet_v_o = packet_v_lo & rev_fifo_has_space`

这表示：

- 即便网络上已经来了一个 forward request
- 也不一定马上交给本地核
- 必须先确认 return path 的 FIFO 还有空间

这个机制的目的很明确：

- 一旦本地核开始处理一个请求，后面就必须能把 response/credit 发出去
- 不能出现“请求接进来了，但 response 无处可放”的死锁

这是 manycore endpoint flow control 里最重要的一个安全机制。

#### 4.3.2 outgoing request 的 credit 计数

`endpoint_fc` 里还有一个 `out_credits_used_o` 计数器：

- 发出一个 request：加一
- 收到一个 return credit：减一

这里统计的是：

- 当前已经发出去、但还没有完全拿回 credit 的 remote request 数量

这不是 router 输入端口那种 credit。
这是 endpoint 视角下“网络事务 outstanding 数”的记账器。

### 4.4 当前 `gbp_pe` 本地 P 口实际采用的流控模式

对 `gbp_pe` 来说，要把下面几件事区分开：

1. router P 口不是 credit 模式
   - 因为 `hetero_type_p == 8`
   - tile 没有给 `P` 端口打开 `use_credits_p`

2. endpoint_fc 仍然会做 response-space gating
   - 所以 incoming request 不会在 return path 没空间时交给 `gbp_pe`

3. endpoint_fc 仍然统计 outstanding requests
   - `out_credits_used_o` 仍有效

所以当前 `gbp_pe` 的本地 P 口更准确的说法是：

- router 到 endpoint：普通 ready/valid
- endpoint 内部：有 response credit 安全约束
- endpoint 输出统计：有 outstanding credit 计数

## 5. `gbp_pe` 特有的 NoC 接入层

## 5.1 `gbp_pe_endpoint_adapter`

这个模块不是 manycore 通用 endpoint 的一部分，而是 `gbp_pe` 自己的前端适配器。

它站在 `bsg_manycore_endpoint_standard` 的简化接口之后，做两件事：

1. 维护一份本地可见的 queue/MMIO/payload shadow state
2. 把网络读写继续翻译成 `core_req_*` 形式，交给后面的 bridge

### 5.1.1 本地 shadow state

adapter 内部维护：

- `q_base_addr`
- `q_depth`
- `q_head`
- `q_tail`
- `q_credit`
- `q_doorbell`
- `q_epoch`
- `payload_mem_r[0:3][0:255]`

这说明对 `gbp_pe` 来说，NoC 写入的 B0/B4-B7 不只是“马上执行一次动作”，还会被记录成一份本地队列状态。

### 5.1.2 `forward_local_writes_p = 1`

`gbp_pe` 实例化 adapter 时显式打开了：

- `forward_local_writes_p(1'b1)`

结果是：

- B0/B4-B7 的写既会更新 adapter 本地 shadow
- 也会继续向 `core_req_*` 转发

所以当前架构不是“adapter 自己消费 MMIO/payload，bridge 完全看不到”。
而是：

- adapter 保留一份软件可见/可读的影子状态
- bridge 同时还能拿到这些写请求，用于真正驱动 PE 行为

## 5.2 `gbp_pe_noc_bridge`

bridge 是 `gbp_pe` 的 NoC 协议解释器。

它把 `core_req_*` 分成三类：

1. sideband 命令
2. ingress payload 写入
3. 读状态/错误寄存器

### 5.2.1 地址分类

bridge 依据 bank 分类：

- `B0`
  - MMIO 队列寄存器
- `B4-B7`
  - payload staging 区
- `B1-B3`
  - direct ingress write

### 5.2.2 payload/tail/doorbell 顺序协议

bridge 内部有一个顺序状态机：

- `ORD_IDLE`
- `ORD_PAYLOAD_WRITTEN`
- `ORD_TAIL_WRITTEN`

协议要求是：

1. 先写 payload bank
2. 再写 `Q_TAIL`
3. 最后写 `Q_EPOCH_DOORBELL` 且 bit0 为 1

只有当 doorbell 命中并且顺序正确时，bridge 才会同时做两件事：

- 发出 `sideband_cmd_valid_o`
- 发出 `ingress_intent_v_o`

这实际上就是 `gbp_pe` 自己的“小型 doorbell 队列协议”。

### 5.2.3 direct ingress write

若地址落在 `B1-B3`，bridge 走的是 `direct_ingress_write`：

- 直接产生 `ingress_intent`
- 不必经过 payload/tail/doorbell 三拍协议

这条路径更像“直接向 PE 的 ingress 数据口灌一个 beat”。

### 5.2.4 sideband 命令

bridge 生成的 sideband 命令字段是：

- `sideband_cmd_valid_o`
- `sideband_cmd_kind_o`
- `sideband_cmd_txn_id_o`

这些信号进入 `gbp_pe` 后，会再送入 `pe_top` 的命令口。

## 5.3 `gbp_pe` 顶层如何把 NoC 请求接入 PE

在 `v/gbp_pe/gbp_pe.sv` 里，路径是：

1. `endpoint_adapter`
2. `noc_bridge`
3. `pe_top`

其中：

- `sideband_cmd_*` 进 `pe_top` 的命令口
- `ingress_intent_*` 进 `pe_top` 的 ingress write 口

另外 `gbp_pe` 还会在 `compute_done` 时构造一个 `remote_sw` completion packet，再通过 endpoint 发回网络。

这个 completion packet 的目标坐标被填成当前 tile 自己，所以它是一个“自回环的 manycore 包”，用于把事务完成事件重新走 manycore 标准返回路径。

## 6. `pe_top` 与 NoC 的边界

`pe_top` 是 NoC 世界和本地 compute/SPM 世界的分界线。

对 NoC 来说，它只暴露两类接口：

1. sideband 命令
   - `cmd_valid_i/cmd_kind_i/cmd_txn_id_i`
2. ingress beat 写入
   - `ingress_wr_valid_i/addr/data`

进入 `pe_top` 后：

- sideband 命令会和 whitebox 命令多路复用
- ingress 写入会进入 `addr_fifo + data_fifo + mic_write`
- 真正的 GBP 读写都在本地 SPM 里完成，不再走 manycore 网络

因此要明确一点：

- manycore NoC 负责把“命令”和“输入数据”送到 `gbp_pe`
- compute 过程中的 META/VEC/MESSAGE 读写，是 PE 内部 `spm_subsystem` 的本地事务
- 不是每次 belief/message 更新都回到片上 NoC

## 7. 当前 `gbp_pe` 内部本地数据通路与 NoC 的关系

`pe_top` 内部的数据面是：

- `control_unit_gbp`
- `stream_dispatcher`
- `read_stream_engine`
- `compute_unit_wrapper`
- `write_stream_engine`
- `spm_subsystem`

这部分和 NoC 的边界关系如下：

### 7.1 从 NoC 进入 PE 的两条路

1. sideband 命令路
   - NoC -> endpoint -> adapter -> bridge -> `pe_top.cmd_*`
2. ingress 数据路
   - NoC -> endpoint -> adapter -> bridge -> `pe_top.ingress_wr_*`

### 7.2 PE 内部执行

执行过程中：

- `control_unit_gbp` 决定读哪条 META/STATE/MESSAGE
- `read_stream_engine` 从本地 SPM 取数据
- `compute_unit_wrapper -> gbp_compute_engine` 做计算
- `write_stream_engine` 把结果写回本地 SPM

这整个过程不再走 manycore mesh。

### 7.3 执行结束回到 NoC

完成后只有事务完成通知会回到 manycore：

- `compute_done -> completion packet -> endpoint -> mesh`

所以 `gbp_pe` 当前和 NoC 的关系更像：

- NoC 做命令/输入投递与完成回报
- PE 自己在本地 scratchpad 内闭环执行算法

## 8. Whitebox 路径与 NoC 的关系

当前 whitebox 测试并不完全等价于真实 NoC 驱动。

因为在 `GBP_WHITEBOX_TEST` 下，命令可以从顶层 whitebox 端口直接打到：

- `tile_compute_array_mesh`
- `tile_compute_mesh`
- `hetero_socket`
- `gbp_pe`

最后在 `gbp_pe` 内部与正常 sideband 命令做 mux。

所以 whitebox 模式下要区分两件事：

1. PE 内部控制/计算/SPM 路径仍然是真实的
2. 命令注入可能绕过了 NoC 的 adapter/bridge/frontdoor 协议

因此：

- whitebox 可以验证 PE 内部调度、priority switch、写回闭环
- 但不能把它直接等同于“完整 NoC frontdoor 协议也已经被同样强度验证”

## 9. 关键结论

### 9.1 当前 manycore NoC 是“双网络”结构

不是单一 request/response 混传，而是：

- forward network 传 request
- reverse network 传 response/credit

每个 tile 内部是两套独立 router。

### 9.2 Router 是“每输入缓冲、每输出仲裁”的结构

router 不是输入中心仲裁，而是：

- 每个输入先入 FIFO
- 每个输出独立 round-robin
- 最后用 one-hot mux 选数据

这保证了：

- 输入有局部缓冲
- 输出仲裁简单清晰
- 很容易做严格维序路由

### 9.3 `gbp_pe` 当前没有启用 router P 口 credit 模式

这是当前 `gbp_pe` 与 vanilla core 的关键差别之一。

对 `gbp_pe` 而言：

- router P 口仍是 ready/valid 反压
- 但 endpoint_fc 仍然会做 response-space gating

所以不要把 `out_credits_used_o`、`rev_fifo_credit_r` 和 router 的 `use_credits_p` 混为一谈。

### 9.4 `gbp_pe` 的 NoC 前端不是“直接 MMIO”，而是“adapter shadow + bridge 协议解释”

软件写进来的 manycore 包不会直接碰到 `pe_top`。
中间至少经过两层：

1. `gbp_pe_endpoint_adapter`
   - 维护本地 queue/payload shadow
2. `gbp_pe_noc_bridge`
   - 解释 doorbell 协议
   - 生成 sideband 命令与 ingress 写入

### 9.5 `gbp_pe` 的真实计算数据面主要在本地 SPM，不在 manycore mesh 上

manycore NoC 负责的是：

- 启动
- 送入 ingress 数据
- 回报完成

而真正的 GBP META/STATE/MESSAGE 数据搬运是在：

- `read_stream_engine`
- `write_stream_engine`
- `spm_subsystem`

这套本地数据通路里完成的。

## 10. 对当前修复工作的意义

这轮 `priority switch` 和 `ARE` 收敛修复，主要落在 PE 内部闭环：

- `control_unit_gbp`
- `gbp_compute_engine`
- `gbp_control_fsm`
- `pe_top`
- whitebox 测试

它已经证明：

- `gbp_pe` 内部调度和写回闭环能持续推进
- factor/variable phase 可以真实切换
- 本地 SPM 数据通路可以完成多轮执行

但如果后面要验证“软件经 NoC frontdoor 写 payload/tail/doorbell，最终触发整个 GBP 事务”的完整行为，还需要单独补一类 frontdoor/queue 协议测试，而不能只依赖 whitebox。

最后验证日期：2026-04-04
