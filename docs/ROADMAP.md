# Aftermath 开发路线图

类 DayZ 的末日生存游戏。这个文件是项目进度的唯一依据，每完成一个阶段就更新。

## 已确定的决定

| 项目 | 决定 |
|---|---|
| 引擎 | Unreal Engine 5.8.3 源码版（`D:\UE\UE_5.8.3`），C++ 为主 |
| 联机 | 专用服务器，多人 PvP，服务器说了算（防作弊前提） |
| 规模 | 每服 60 人以上，地图 10×10 公里以上 |
| 视角 | 第三人称越肩，V 键切第一人称；服务器可强制第一人称 |
| 画风 | 写实。阶段 6 之前只用免费素材，之后再决定是否花钱 |
| 部署 | 先本机 + 局域网，云服务器放到最后 |
| 网络同步 | Iris（已开启） |

## 阶段总览

| 阶段 | 内容 | 状态 |
|---|---|---|
| 0 环境 | 引擎源码编译、VS2022、项目骨架、服务器/客户端打包、git + LFS | ✅ 完成 |
| 1 联机原型 | 灰盒地图、跑/走/蹲/趴、翻越、开门、捡东西，全部服务器判定 | ✅ 完成 |
| 2 生存 + 背包 | 生存数值和伤病、格子背包、装备栏、附近栏、物品组合 | ✅ 完成 |
| 3 战斗 PvP | 枪械弹道、弹匣、后坐力、延迟补偿命中、近战、部位伤害、护甲、死亡掉落 | ✅ 完成 |
| 4 感染者 | 服务器 AI：视觉、听觉、叫同伴；按距离降低更新频率 | ✅ 完成 |
| 5 世界 | 10×10 公里地形、World Partition、城镇和军事区、物资刷新、昼夜天气 | ⬜ 下一个（先出方案） |
| 6 持久化 | SQLite 存档、断线保护、永久死亡、服务器配置、登录流程 | ⬜ |
| 7 写实化 | 换正式素材、MetaHuman、动画、音效、Lumen 调光、性能、压力测试 | ⬜ |
| 8 上线准备 | Linux 服务器、云端部署、反作弊（EAC）、服务器列表 | ⬜ |

## 各阶段细节

### 阶段 2：生存 + 背包
- 生存数值：生命、血液、能量（饥饿）、水分、体温、体力。能量和水分随时间下降，跑步下降更快，降到零掉生命
- 体温：受昼夜、天气、衣服保暖、潮湿影响
- 负重：越重越慢，体力上限越低
- 伤病：出血（绷带/布条）、骨折（夹板，不能跑）、感冒（咳嗽引怪）、食物中毒
- 格子背包：物品占格、拖动、旋转、叠放、拆分；容量来自上衣、裤子、背心、背包
- 附近栏：脚边地面和附近容器
- 耐久五档；罐头要刀或开罐器；子弹压弹匣、衣服撕布条
- 快捷栏 1~9
- 物品定义放数据表（CSV/JSON 导入），加物品不改代码
- 背包增量同步，只发给能看到的人

### 阶段 3：战斗 PvP
- 客户端先播开枪效果，服务器按弹道重算，按延迟回溯目标位置判定命中
- 部位伤害（头最致命），头盔和防弹衣减伤
- 近战轻击/蓄力重击，消耗体力
- 噪音系统：脚步、枪声、破窗、咳嗽

阶段 3 完成情况：
- 武器/弹匣/护甲数据在 items.json；手持栏位；装弹匣、R 换弹、卸弹匣
- 弹道纯逻辑（下坠、部位判定、射线-胶囊求交）+ 位置历史回溯（延迟补偿）
- 服务器开火校验：弹药、射速、瞄准方向、出发点（离眼睛太远 = `bad_origin` 拒绝）
- 部位伤害（头/躯干/手/腿），防弹衣减躯干伤害约一半，出血、骨折，击杀记录
- 噪音：枪声、近战、脚步、咳嗽（阶段 4 感染者会用到）
- 客户端：左键开火/近战、右键瞄准、R 换弹、弹药显示、弹道线、命中提示、受伤方向提示
- 测试：自动化 48 个全过；联机 `smoke_stage3.sh` 32/32（射击→护甲减伤→作弊被拒→换弹→击杀→重生）；`smoke_stage2.sh` 回归 44/44
- 测试专用：`-AftermathAllowDebug` 启动的服务器才接受"给装满的弹匣"请求，Shipping 版直接编译掉

阶段 4 完成情况：
- 纯逻辑（`Infected/AftermathInfectedMath`）：视野锥 + 余光、姿态/移动/光线影响视距；听觉按距离衰减，隔墙减半，雨天减弱；状态机 闲逛→警觉→追击→攻击→找人→回去；平民/警察/士兵（血量、速度、头盔/防弹衣减伤）
- 服务器角色 `AAftermathInfected`：没有 AIController，服务器直接驱动移动；沿 navmesh 寻路（服务器开局时运行时生成，门关着就挡住）；吼叫叫附近同伴；永远不会开门
- 管理器 `UAftermathInfectedManager`：按离最近玩家的距离决定思考频率（30 m 内 10 次/秒，80 m 内 3 次/秒，200 m 内 1 次/秒，更远且闲着就休眠），噪音能叫醒休眠的；每 5 秒打一行 `InfectedStats`（帧率、耗时）
- 战斗打通：子弹和近战能打感染者（有延迟补偿），感染者抓人走和玩家一样的护甲/伤口流程
- 地图：navmesh 范围覆盖整张地图；东北角围墙院子里 5 个感染者（大门是一扇门，它们出不来，玩家开门就会冲出来）；StartG 测试通道 20 m 外 1 个平民
- 测试：自动化 57 个全过（新增感染者纯逻辑 4 个 + 世界测试 5 个）；联机 `smoke_stage4.sh` 26/26（开枪→听见→追→打人→被打死）；150 感染者压力测试：156 个全部醒着、130 个同时追击，服务器稳定 30 帧，单帧平均 3 ms（上限 33 ms）；阶段 2/3 回归 44/44、32/32
- 测试专用：`-AftermathNoInfected` 去掉地图里的感染者（阶段 1~3 测试用）；`-AftermathInfected=N` 多刷 N 个；`-AftermathAllowDebug -AftermathGodMode` 玩家不掉血（压力测试用）。都只在开发版有效
- 修了一个老问题：出生点被人/感染者挡住时引擎会拒绝生成角色，玩家就一直没有身体。现在会挪到旁边的空位

### 阶段 4：感染者
- 视觉（视野、光线、姿态）+ 听觉（噪音，隔墙衰减）
- 状态：闲逛 → 警觉 → 追击 → 攻击 → 找人 → 回去闲逛；吼叫叫同伴；不会开门
- 类型：平民、警察、士兵
- 目标：单服 150+ 感染者，服务器 30 帧以上

### 阶段 5：世界
- 地形 + World Partition + HLOD，PCG 铺树和草
- 区域：海岸、城镇（民居/诊所/警局）、农场、工业区、军事基地
- 简化版 DayZ 中央经济：刷新点按类别，物品有全图目标数量，地上物品定时清理
- 昼夜和天气由服务器统一计算

### 阶段 6：持久化
- SQLite 存角色、背包、世界物品、尸体、搜过的容器
- 断线时角色留在原地一段时间
- 永久死亡：只删角色，世界保留

### 阶段 7：写实化
- Megascans 免费部分 + Fab 付费包、MetaHuman、Game Animation Sample
- 目标：RTX 5070 上 1440p 60 帧以上（配合 DLSS）

### 阶段 8：上线准备
- Linux 服务器交叉编译、云部署、EAC、EOS 或 Steam 服务器列表

## 已完成内容（阶段 0~2）

- 项目：`Aftermath.uproject`，模块 `Source/Aftermath`
- 移动：`Movement/AftermathMovementComponent`（姿态、冲刺、翻越，走 CMC 预测和服务器纠正；速度受伤病/负重/体力限制）
- 交互：`Interaction/`（门、地上物品、交互接口），服务器校验距离
- 物品：`Content/Data/items.json`（20 种物品，改这个文件就能加物品，不用改代码）+ `Items/AftermathItemDatabase`
- 背包：`Items/AftermathInventoryComponent`（格子来自衣服和背包，只同步给本人）、`Items/AftermathGrid`（格子逻辑）
- 物品使用：`Items/AftermathItemActions`（吃喝、包扎、夹板、吃药、穿衣、开罐头、撕布条、装卸弹匣）
- 生存：`Survival/AftermathSurvivalComponent`（生命、血液、能量、水分、体温、体力、负重、流血、骨折、感冒、中毒、摔伤、死亡）
- 规则：`Core/AftermathGameMode`（出生送衣服和布条，死亡掉落，8 秒后重生，尸体 10 分钟后清理）
- 地图：`/Game/Maps/Proto`，由 `Tools/build_proto_map.py` 生成（3 栋房子、17 件物资、翻越墙、矮洞、摔伤台阶）
- 屏幕显示：`UI/AftermathHUD` + `UI/AftermathInventoryLayout`（Canvas 绘制，暂不用 UMG）
- 测试驱动：`Debug/AftermathTestScript`（`-AftermathScript=` 参数触发，Shipping 版不包含）

## 按键

| 键 | 作用 |
|---|---|
| WASD / 鼠标 | 移动 / 视角 |
| Shift（按住） | 冲刺（耗体力，骨折或太重时不能冲） |
| Ctrl | 切换慢走 |
| C / Z | 蹲 / 趴 |
| 空格 | 跳；前面有矮墙时翻越 |
| F | 开门、捡东西 |
| V | 第一 / 第三人称 |
| Tab | 打开背包 |
| 1~9 | 使用快捷栏物品；背包打开时悬停物品再按数字 = 设快捷键 |
| 背包里：左键拖动 | 移动；拖到装备格 = 穿上；拖到另一个物品上 = 组合（刀→罐头、子弹→弹匣、同类叠放）；拖出面板 = 丢下；从“附近”拖进来 = 捡起 |
| 背包里：R / 右键 / G / X | 拖动时旋转 / 使用 / 丢下 / 拆一半 |

## 常用命令

编译编辑器模块（在 `D:\UE\UE_5.8.3` 下）：
```
Engine\Build\BatchFiles\Build.bat AftermathEditor Win64 Development -Project=D:\NAR\narprojects\game\Aftermath.uproject -WaitMutex
```

打包服务器和客户端：
```
Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project=D:\NAR\narprojects\game\Aftermath.uproject -noP4 -platform=Win64 -server -serverplatform=Win64 -client -clientconfig=Development -serverconfig=Development -build -cook -stage -pak -archive -archivedirectory=D:\NAR\narprojects\game\Packaged -unattended
```

自动化测试（57 个）：
```
UnrealEditor-Cmd.exe Aftermath.uproject -ExecCmds="Automation RunTests Aftermath.; Quit" -unattended -nullrhi -testexit="Automation Test Queue Empty"
```

联机测试（1 服务器 + 2 脚本客户端，44 项检查，含背包截图）：
```
bash Tools/smoke_stage2.sh packaged
```

战斗联机测试（射手 StartE + 靶子 StartF，32 项检查，含截图 `stage3_combat.png`）：
```
bash Tools/smoke_stage3.sh packaged
```

感染者联机测试（1 服务器 + 1 脚本客户端 StartG，26 项检查，端口 17777，含截图 `stage4_infected.png`）：
```
bash Tools/smoke_stage4.sh packaged
```

150 感染者压力测试（服务器要稳在 30 帧，端口 17777）：
```
bash Tools/perf_infected.sh packaged 150 45
```

重建测试地图：
```
UnrealEditor.exe Aftermath.uproject -ExecutePythonScript=Tools/build_proto_map.py -unattended -nullrhi
```

手动试玩：先运行 `Packaged\WindowsServer\AftermathServer.exe -log -port=7777`，看到 `listening on port 7777` 后运行 `Packaged\WindowsClient\AftermathClient.exe 127.0.0.1:7777 -windowed`。

## 待处理

- 磁盘：C 盘约剩 44GB，D 盘约剩 107GB，大地图和素材会继续占用
- 代码在 GitHub `boka233/aftermath`，分支 `stage3-combat`（阶段 0~3）、`stage4-infected`（阶段 4），还没合并到 main
- 还没有人亲手试玩过手感（跑动、镜头、翻越、背包操作）
- 环境温度现在是固定值（偏凉），阶段 5 做昼夜天气时接上；光线值（影响感染者视距）也先固定为白天
- 感染者还是灰色圆柱（追击时变红、警觉时变黄），没有动画和声音；吼叫只在日志里，玩家听不到
- 压力测试是 1 个玩家站在 150 个感染者中间的最坏情况；多个玩家分散在大地图上时每个感染者的开销更低，但还没测过 60 人
- 编辑器模式跑联机测试会有 Iris 的"模块加载太晚"警告（编辑器插件造成），打包版没有
- 死亡后装备直接掉在地上，还没改成留在尸体上可搜刮（放到阶段 6 持久化一起做尸体）
- 还没人亲手试过枪械手感（后坐力、瞄准、弹道线）；近战蓄力重击只有逻辑，没专门联机测
- 测试靶子是站着不动的，移动目标的延迟补偿只在自动化测试里验证过
