# AlongWithYourself 技术审查报告

> 审查对象：`F:/unrealprojects/AlongWithYourself`（Unreal Engine 5.7 工程）
> 审查日期：2026-11-24
> 依据：工程内实际源码、`.uproject` / `Config/*.ini`、`Content` 资产、以及 2026-11-18 / 2026-11-20 两天的工作记录与实机日志。
> 原则：只写工程里真实存在的东西；无法从文件确认的内容一律标注「未验证 / 无法确认」。

---

## 1. 工程版本与目录结构

### 1.1 版本信息

| 项 | 值 | 来源 |
|---|---|---|
| 引擎版本 | **UE 5.7**（`EngineAssociation: "5.7"`） | `AlongWithYourself.uproject` |
| 引擎安装路径 | `D:/UE/UE_5.7` | 编译日志、操作记录 |
| 编译器 | VS2022 / MSVC **14.44.35207**（x64） | `Intermediate/**/Build.rsp`、编译日志 |
| 工程模块 | 单模块 `AlongWithYourself`（Runtime，LoadingPhase Default） | `.uproject` |
| 附加依赖 | Engine、AIModule、UMG | `.uproject` |
| 启用插件 | ModelingToolsEditorMode、StateTree、GameplayStateTree、PythonScriptPlugin、EditorScriptingUtilities | `.uproject` |
| 项目名（ini 内） | `Third Person Game Template`（模板原名，未改） | `Config/DefaultGame.ini` |
| 目标硬件 | `TargetedHardwareClass=Mobile`（**模板默认值，未改**） | `Config/DefaultEngine.ini` |
| 版本控制 | **无**（工程目录没有 `.git`） | `git status` 报 `not a git repository` |

### 1.2 顶层目录

```
AlongWithYourself/
├── AlongWithYourself.uproject
├── AlongWithYourself.sln
├── Config/                 DefaultEngine / DefaultGame / DefaultInput / DefaultEditor*
├── Content/                738 个 .uasset（绝大部分是模板自带）
│   ├── Room/               本项目自建：Lvl_Room.umap、M_RoomMetal、M_Ball
│   ├── ThirdPerson/        模板：BP_ThirdPersonCharacter / GameMode / PlayerController、Lvl_ThirdPerson
│   ├── Input/              模板：IA_Move / IA_Look / IA_Jump / IA_MouseLook、IMC_Default、IMC_MouseLook
│   ├── Characters/ …       模板：Mannequin 骨骼网格与动画
│   ├── Variant_Combat / Variant_Platforming / Variant_SideScrolling   模板三套示例（当前关卡不用）
│   └── __ExternalActors__/ 仅 ThirdPerson / 三个 Variant 关卡使用；**Lvl_Room 是自包含的**
├── Source/AlongWithYourself/
│   ├── Room/               本项目核心：RoomShell.{h,cpp}、RoomBall.{h,cpp}
│   ├── AlongWithYourselfCharacter.{h,cpp}
│   ├── AlongWithYourselfGameMode.{h,cpp}
│   ├── AlongWithYourselfPlayerController.{h,cpp}
│   ├── AlongWithYourself.{h,cpp}   （仅定义日志分类 LogAlongWithYourself）
│   └── Variant_*           模板遗留代码（仍参与编译）
├── Binaries/Win64/         67 MB，UnrealEditor-AlongWithYourself.dll
├── Intermediate/           2.6 GB（UHT 生成代码 + 编译产物）
├── Saved/                  Logs（大量验证日志）、Screenshots、Crashes
├── DerivedDataCache/       空壳（真实 DDC 已迁到 E:）
└── .vscode/                AI 生成的 VS Code 配置（c_cpp_properties / tasks / settings / extensions）
```

> 注：`Intermediate` 达 2.6 GB 属正常（含 UHT 与各模块 obj）；`DerivedDataCache` 目录保留但已是空壳，真实缓存见 §7 环境项。

---

## 2. 当前已实现的功能

以下每条都能在代码中找到对应实现，不是推测。

### 2.1 空心立方体房间（`ARoomShell`）

- 6 块 `/Engine/BasicShapes/Cube` 拼成空心立方体，外壳**无碰撞**、可投影（默认不投影，见 §7）。
- 边长 = 角色胶囊总高 × `SizeMultiplier`，实测 **540 cm**（胶囊半高 90 → 身高 180 → ×3），墙厚 10。
- Actor 原点 = 立方体形心；`ShellPivot` 承载「外壳 + 全部碰撞体 + 墙面相机」，绕形心旋转，**形心位置恒定**。
- 不可见碰撞：`BoundaryColliders[0]` 地板、`[1]` 天花板、`[2..13]` 共 12 段多边形围墙（内切圆半径 250，段厚 8，额外内缩 10），全部挂在 `ShellPivot` 下**跟着一起转**。
- 材质：`/Game/Room/M_RoomMetal`（半透明金属），加载失败逐级回退到引擎默认材质。
- 编辑器可一键重建：`RebuildInEditor()`（`CallInEditor`）。

### 2.2 重心驱动的旋转

- 每帧算「角色 + 小球」的质量加权重心 → 相对形心的矢量 `r` → 重力力矩 `τ = r × Mg` → 角加速度 `α = τ/I` → 阻尼衰减 → 四元数左乘积分。
- 力矩天然过形心 ⇒ 纯转动、不平移，形心不动。
- 倾角限位 `MaxTiltAngle = 20°`（到限位把角速度清零）。
- 另有「落地回正」刚度 `UprightStiffness`（默认 0，未启用）。

### 2.3 角色

- 标准 `ACharacter` + `CharacterMovementComponent` 行走：`MaxWalkSpeed 500`、`JumpZVelocity 500`、`AirControl 0.35`。
- 重力：`GravityScale = 1.0`（≈980 cm/s²）、`bApplyGravityWhileJumping = true`；质量 80 kg 写入 `BodyInstance`。
- 摩擦：`GroundFriction = 8`、`BrakingDecelerationWalking = 2000`（均回到引擎默认量级）。
- 可行走坡度：`MaxWalkableSlopeAngle = 50°`。
- 胶囊：半径 35 / 半高 90（模板 42/96 被改小）。
- 10 段 hitbox（头/胸/腹/盆骨/大腿×2/小腿×2/上臂×2），`QueryOnly`，只对 Visibility `Block`，专用对象通道 `Hitbox`。
- 移动方向在墙面机位模式下取自 `FaceCamera` 的 forward / right（否则按 W 与画面不一致）。

### 2.4 小球（`ARoomBall`）

- 根组件即球网格，`ECC_PhysicsBody`、全通道 Block、`QueryAndPhysics`。
- 半径 30 cm、质量 20 kg、`Restitution 0.6`、`Friction 0.4`（运行时 `UPhysicalMaterial`）。
- **物理在 `BeginPlay` 才打开**（位置先由房间摆好），`SetEnableGravity(true)`。
- 出生点：内地面 + `RoomSize × 0.55`（≈297 cm），X 偏 `RoomSize × 0.15`（≈81 cm）避开角色头顶。

### 2.5 视角

- 主视角：`ARoomShell::FaceCamera`，挂在 `ShellPivot` 下，位置 = `CameraWall`（默认 -Y 墙）内侧 12 cm、距内地面 105 cm，水平看向房间中心，FOV 90。
- 角色 `Tick` 里兜底抢回视点：`PC->SetViewTarget(RoomShell)`；`ARoomShell::CalcCamera()` 直接返回这面相机。
- 备用（当前被短路）：弹簧臂俯瞰机位 `ApplyOverviewCamera()`，臂长 = 房间边长 × 1.92，俯仰 −30°，瞄准点抬高 0.5×边长。

### 2.6 兜底与调试

- `RescueBallIfOutside()`：球被甩出盒外 → 夹回内壁、清零线速度/角速度。
- `RescuePawnIfOutside()`：角色被挤到盒外 → 按 12 边形内表面半径硬夹回盒内。
- `ClampPawnToRoomFloor()`：站在房间地板上时把局部 Z 钉在「内底板 + 胶囊半高」。
- `AAlongWithYourselfGameMode::EnsureRoomScene()`：关卡缺 RoomShell / PlayerStart 就地补齐，并把出生点搬到内地面正上方。
- 控制台调试命令（全部 `UFUNCTION(Exec)`，挂在角色上）：
  `RoomRotate P Y R`、`RoomBalanceDebug 1`、`RoomWalkDebug 1`、`RoomWalkFor N`、`RoomSolver 0/1`、`RoomGhost 1`、`RoomCarry 0/1`、`RoomCharState`、`RoomShot N`、`RoomHitboxDebug 1`、`RoomHitboxTestTrace`。
- `R` 键把盒子姿态归零。

---

## 3. 实际创建 / 修改过的资产与类清单

### 3.1 本项目新建的 C++ 类

| 类 | 文件 | 说明 |
|---|---|---|
| `ARoomShell` | `Source/AlongWithYourself/Room/RoomShell.h` / `.cpp` | 空心立方体、碰撞、旋转求解器、墙面相机、小球生成 |
| `ARoomBall` | `Source/AlongWithYourself/Room/RoomBall.h` / `.cpp` | 带重力的物理小球 |
| `FCharacterHitboxDef` | `AlongWithYourselfCharacter.h`（USTRUCT） | hitbox 定义（部位名 / 骨骼 / 偏移 / 半尺寸 / 倍率） |

### 3.2 修改过的既有 C++ 类（模板派生）

| 类 | 文件 | 改动要点 |
|---|---|---|
| `AAlongWithYourselfCharacter` | `AlongWithYourselfCharacter.{h,cpp}` | 胶囊 42/96→35/90；重力/质量/摩擦参数；hitbox 系统；`TickGroup = TG_PostPhysics`；墙面机位接管；移动方向改用墙面相机；一套 Exec 调试命令 |
| `AAlongWithYourselfGameMode` | `AlongWithYourselfGameMode.{h,cpp}` | 新增 `InitGame → EnsureRoomScene()`；`bAutoCreateMissingSceneActors`、`bAlignPlayerStartToRoom` |
| `AAlongWithYourselfPlayerController` | `AlongWithYourselfPlayerController.cpp` | 未改逻辑（模板原样），仅因类重定向而存在 |
| `AlongWithYourself.Build.cs` | 同目录 | 追加 `PhysicsCore`（`UPhysicalMaterial` 符号需要） |
| `AlongWithYourself.{h,cpp}` | 同目录 | 仅 `DECLARE/DEFINE_LOG_CATEGORY(LogAlongWithYourself)` |

### 3.3 组件（全部是 C++ 里 `CreateDefaultSubobject` 生成的，不是编辑器里摆的）

`ARoomShell`：`Root`(USceneComponent)、`ShellPivot`(USceneComponent)、`FaceCamera`(UCameraComponent)、`ShellFace_0..5`(UStaticMeshComponent)、`Boundary_0..17`(UBoxComponent，实际启用 14 个)。

`AAlongWithYourselfCharacter`：`CameraBoom`(USpringArmComponent)、`FollowCamera`(UCameraComponent，模板自带)、运行时 `Hitbox_*`(UBoxComponent × 10，`RebuildHitboxes()` 里 `NewObject` + `RegisterComponent`)。

`ARoomBall`：`BallMesh`(UStaticMeshComponent，根组件)、运行时 `BallPhysMat`(UPhysicalMaterial)。

### 3.4 资产（本项目真正新建的只有 3 个）

| 资产 | 路径 | 内容 |
|---|---|---|
| 关卡 | `/Game/Room/Lvl_Room` | `RoomShell` + `PlayerStart` + 5 个模板光照 Actor（DirectionalLight / SkyLight / SkyAtmosphere / ExponentialHeightFog），自包含、无 World Partition 外置 Actor |
| 材质 | `/Game/Room/M_RoomMetal` | 灰 0.8 / Metallic 0.9 / Roughness 0.2，**Translucent，Opacity 0.35**，双面关闭，`TLM_VOLUMETRIC_NON_DIRECTIONAL` |
| 材质 | `/Game/Room/M_Ball` | 橙色不透明 |

### 3.5 蓝图

工程里**没有新建任何蓝图**，只使用模板自带的三个（其原生父类被 `DefaultEngine.ini` 的 Class 重定向指到本工程 C++ 类）：

- `/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter` → 原生父类 `AlongWithYourselfCharacter`（经 `ActiveClassRedirects` 从 `TP_ThirdPersonCharacter` 映射）
- `/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode` → 原生父类 `AlongWithYourselfGameMode`（`GlobalDefaultGameMode` 指向它）
- `/Game/ThirdPerson/Blueprints/BP_ThirdPersonPlayerController` → 原生父类 `AlongWithYourselfPlayerController`

> 这三个 BP 是模板资产，本项目**没有在编辑器里给它们加节点/改默认值**（记录中没有相关操作）；如果后续有人在 BP 里改过参数并保存，会覆盖 C++ 默认值——交接时建议在 BP 的 Class Defaults 里逐项核对一遍（无法从二进制资产确认，标注为「未验证」）。

### 3.6 配置文件改动

`Config/DefaultEngine.ini`：
- `EditorStartupMap` / `GameDefaultMap` → `/Game/Room/Lvl_Room`
- `GlobalDefaultGameMode` → `BP_ThirdPersonGameMode_C`
- 新增碰撞对象通道：`ECC_GameTraceChannel1` → `Hitbox`（`DefaultResponse=Ignore`，`bTraceType=False`）
- DDC：`[Zen.AutoLaunch] DataPath=E:/UE_DDC/Zen/Data`；`[DerivedDataBackendGraph]` 与 `[InstalledDerivedDataBackendGraph]` 的 `Local` → `E:/UE_DDC/Local`

`Config/DefaultInput.ini`、`Config/DefaultGame.ini`：模板原样（仅 EnhancedInput 默认类与轴配置）。

---

## 4. 立方体旋转机制的实现方式

### 4.1 两套路径，运行时只用一套

`ARoomShell` 有两种旋转实现，由 `bEnableInertialRotation`（默认 `true`）切换：

**A. 重心物理求解器（当前生效）** — `ARoomShell::UpdateInertialRotation()`（`RoomShell.cpp:286`）

```
1) 重心        ComputeContentCenterOfMass()  → WorldCOM, TotalMass
                角色质心 = 胶囊位置 − 胶囊 Up × 半高×0.25   （人体重心约在身高 55%）
                球质心   = 球 Actor 位置
                （空盒重心在形心上，不计入；它只影响转动惯量）
2) 偏移        R = WorldCOM − ShellPivot 位置
3) 力矩        τ = R × (0,0,−M·g)        ← g 取 World->GetGravityZ() 绝对值（980）
4) 可选回正    UprightStiffness>0 时叠加  LeanAxis × (k · LeanAngle · W · RoomSize/2)
5) 惯量        I = ShellMass · RoomSize² / 6  +  Σ m·|R|²         （薄壁立方体近似）
6) 积分        ω += (τ/I) · dt · InertialResponse
                ω *= exp(−AngularDamping · dt)                     （阻尼 1.2/s）
                |ω| 夹在 MaxAngularSpeed = 40 °/s
                WorldQuat = Quat(ω̂, |ω|·dt) * WorldQuat           （左乘 = 绕世界轴、过形心）
7) 限位        若外壳 Up 与世界 Z 夹角 > MaxTiltAngle(20°)：
                绕 (BoxUp × WorldUp) 把超出的角度转回去，并把 ω 清零
8) 落位        CurrentRotation = RootQuat⁻¹ * WorldQuat
                ShellPivot->SetRelativeRotation(CurrentRotation)
                PendingCarryQuat = (WorldQuat * OldWorldQuat⁻¹) * PendingCarryQuat
```

**B. 鼠标拖拽 / 目标姿态插值（备用）**：`AddRotation(WorldAxis, Angle)`（`RoomShell.cpp:564`）把世界轴换算到 Root 局部后左乘 `TargetRotation`；`Tick` 里 `FMath::QInterpTo(..., RotationInterpSpeed=20)` 逼近。当前 `bEnableMouseDragRotate = false`，此路径不生效；但 `R` 键复位、`RoomRotate` 调试命令仍走 `AddRotation`/`ResetRotation`。

### 4.2 关键设计点

- **旋转轴与形心**：所有旋转都在 `ShellPivot` 上做，`ShellPivot` 挂在 `Root`（Actor 原点）下，所以「绕任意过形心的轴」= 对 `ShellPivot` 做任意四元数旋转，位置分量恒为 0。
- **碰撞体必须跟着转**：`BoundaryColliders` 挂在 `ShellPivot` 而不是 `Root`。这是整个项目最容易踩的坑——挂在 `Root` 时，盒子刚倾斜几度，静止的地板就会切到倾斜外壳之外，角色会露出来。
- **质量参数**：`ShellMass = 800 kg`（只影响惯量，决定"迟钝程度"）、`CharacterMass = 80`、`BallMass = 20`。当前惯量 ≈ 38.9e6（壳）+ 4.0e6（内容物）kg·cm²，典型力矩 ≈ 19.6e6 kg·cm²/s² ⇒ α ≈ 0.46 rad/s² ≈ 26 °/s²。

### 4.3 角色如何"跟着转"（两段式搬运）

```
ARoomShell::Tick
   └─ UpdateInertialRotation()
        └─ PendingCarryQuat = 本帧世界旋转增量 * PendingCarryQuat     // 只累积，不搬人

AAlongWithYourselfCharacter::Tick   (TickGroup = TG_PostPhysics，在 CMC 解算之后)
   └─ RoomShell->ApplyPendingCarryToPawn()
        └─ CarryPlayerWithRotation(Delta)
             SetActorLocationAndRotation(
                 Pivot + Delta.RotateVector(OldLoc − Pivot),
                 (Delta * OldQuat).Normalized(),
                 TeleportPhysics)
   └─ RoomShell->ClampPawnToRoomFloor()
        站在房间地板上时，把局部 Z 硬夹到「−Half + Thickness + 胶囊半高」
```

为什么必须这样：`CharacterMovementComponent` 在自己的预处理 Tick 里会按世界空间把角色摆回去；房间 Tick 里直接 `SetActorLocation` 当帧就被覆盖（现象 = 盒子绕着他转、人悬在盒中间）。把角色 Tick 放到 `TG_PostPhysics` 后，搬完这一帧就没人再覆盖。

---

## 5. 角色碰撞问题：原始表现 → 排查 → 解决 → 验证

这是本项目耗时最长的一条线，过程有日志与记录支撑，按时间顺序列出。

### 5.1 原始表现（问题 1：外壳有碰撞）

- 早期外壳 `ShellFace_*` 用的是 `QueryAndPhysics`。盒子一转，外壳把角色顶飞，弹簧臂摄像机也被顶回来。
- **解决**：外壳整体 `NoCollision`，碰撞完全交给不可见的 `BoundaryColliders`（地板/天花板 Box + 12 段围墙）。围墙上忽略 Camera 通道，相机不再被顶。

### 5.2 问题 2：碰撞体不跟着转 → 角色"漏出"盒子

- 表现：`BoundaryColliders` 挂在 `Root` 时，盒子倾斜十几度，静止的地板就跑到倾斜外壳外面，角色站在"盒外的地板"上，视觉上穿出盒子。
- **解决**：碰撞体全部改挂 `ShellPivot`（`RoomShell.cpp:78-93` 构造函数 + `Rebuild()` 里设相对变换）。副作用是"角色永远站在当前朝下的那一面"，这也是后面滑坡/滚筒问题的根源。

### 5.3 问题 3：角色不被房间带走（最隐蔽）

- 表现：盒子绕角色转，角色世界坐标不动；倾斜 20° 时角色局部 X 正好等于 `173·sin20° ≈ 59 cm`。
- 排查：日志里 `MovementBase` 已登记为 `Boundary_0`，但引擎并没有按基座旋转搬人——旋转是每帧 `SetRelativeRotation` 强行做的，不是物理基座运动。
- **解决**：`PendingCarryQuat` + `ApplyPendingCarryToPawn()` 两段式搬运（见 §4.3）。

### 5.4 问题 4：胶囊不随盒子倾斜（人"躺在空中"）

- 表现：`bOrientRotationToMovement = true` 时，CMC 每帧只按移动方向改 yaw 并把 pitch/roll 清零，盒子倾 45° 人仍世界竖直，脚底只有一个点搭在斜面上。
- **解决**：`ApplyPhysicsSettings()` 里关掉 `bOrientRotationToMovement`，改由 `CarryPlayerWithRotation` 一并旋转 Actor 四元数（胶囊垂直于内壁）。
- 相关工作：`bIgnoreBaseRotation = true`（不接受基座旋转，避免"一帧搬两倍"）。

### 5.5 问题 5：每帧几毫米的"上飘" → 人浮到盒子中间

- 表现：世界竖直的贴地修正 vs 房间局部参照系，二者每帧差一点点；十几秒后角色顺着内壁蹭到盒子中间，重心被顶到枢轴以上，盒子接着变滚筒。
- **解决**：`ClampPawnToRoomFloor()`（`RoomShell.cpp:449`）。只在 `CurrentFloor.HitResult.GetActor() == this`（踩的是房间自己的面）时生效，局部 Z 高于「内底板 + 胶囊半高」就夹回去；站在小球上（命中球）自动放过。

### 5.6 问题 6：角色贴墙穿模 → 被穿透修正"顶上天"

- 表现：`RescuePawnIfOutside()` 早期按可见外壳计算活动范围，角色走到 12 边形围墙的"弦"上时会与墙穿插，穿透修正顺着墙把人往上顶，人浮在半空。
- **解决**：改按**围墙碰撞体内表面**计算：`WallFaceRadius = R·cos(π/N) − 8`，再减胶囊半径得 XY 限制；Z 夹在 `[−Inner+半高, Inner−半高]`。

### 5.7 问题 7：会"爬墙"（仓鼠轮）

- 表现：`MaxWalkableSlopeAngle = 85` 时，角色一路怼着已经倾斜的内壁往上走，重心被顶到形心以上，盒子越转越快（实测能转到 156°）。
- **解决**：可行走坡度降到 **50°**；`MaxTiltAngle` 兜底 20°。

### 5.8 问题 8：纯自由悬挂 + 会走路的角色 = 永不停滚筒

- 表现：房间一倾，最低点就换位置，角色和小球跟着滑过去，盒子继续转；实测一路转到 **156°**。
- 两种方案都试过：
  - 方案 A（当前）：`MaxTiltAngle = 20°` 限位，到限位清角速度 → 稳定锁死。
  - 方案 B（已试后回退）：把限位放宽到 65°，改靠角色静摩擦站住（`GroundFriction 18` + 松手清速度）——**用户看完效果后要求整体回退**，见 §8。
- 另外两处与碰撞相关的接口问题：
  - `FCollisionQueryParams(..., InIgnoreActor=this)` 会把本 Actor **所有组件**（含 hitbox）一起忽略 → `TraceHitbox()` 里**不能**传 `this`。已修正。
  - `LogTemplateCharacter` 只有 DECLARE 没有 DEFINE，`UE_LOG` 会链接失败（LNK2001）→ 统一改用 `LogAlongWithYourself`。

### 5.9 验证结果（实机日志，可复现）

| 场景 | 数据 | 结论 |
|---|---|---|
| 静止站立 | 倾角 6.9°、角速度 0、滑速 0.0 cm/s、脚底离地 0.0 cm | 小球滚到一侧（重心偏 ~24 cm），盒子按 `atan(24/198)` 倾斜后停住 |
| 自动行走 4 s（早期） | 峰值 20°、松手后稳定 17° | 限位兜住，不爬墙 |
| 自动行走 5 s（2026-11-20 18:41 复测） | 峰值 **17.3°**，松手 1 s 后滑速 **0.0**，角色局部 Z 恒 **−170**（站在地板上） | 回退后行为符合预期 |
| hitbox 射线（从头顶向下） | 命中 `Head`、倍率 **2.00**、命中点 z = −84.85 | 10 段 hitbox 生效且不挡路 |
| 出生点对齐 | `PlayerStart z = −166`（= −270 + 10 + 90 + 4） | GameMode 兜底正确 |
| 启动日志 | `[Friction] 地面摩擦 8.0 \| 行走刹车 2000 cm/s² \| 可行走坡度 50°` | 参数最终态 |

---

## 6. 角色是否支持「随立方体旋转 / 任意表面行走 / 动态重力」

| 能力 | 结论 | 依据 |
|---|---|---|
| **随立方体旋转** | ✅ **支持** | `ApplyPendingCarryToPawn` → `CarryPlayerWithRotation` 每帧把角色的位置与朝向一起绕形心旋转；实测盒子倾 17° 时角色局部坐标稳定、脚底离地 0.0 cm、局部朝上 `V(Z=1.00)`（垂直于内壁） |
| **任意表面行走** | ⚠️ **机制上可行，实测未触发** | 碰撞体都随枢轴转，所以盒子翻到任何角度角色都站在"当前朝下的面"上。但当前 `MaxTiltAngle = 20°`，盒子从未翻过 90°，**"换面（墙变地板）行走"在实测中一次都没发生过**。另外 CMC 的 up 仍是世界 Z（`bIgnoreBaseRotation=true`），一旦真翻到 90°，"胶囊垂直于内壁"与"CMC 认为世界 Z 是上"会矛盾——这条**没有验证过，属于已知风险** |
| **动态重力** | ❌ **不支持"重力方向随盒子转"** | 重力始终是世界 −Z、`GravityScale = 1`（980 cm/s²）。盒子倾斜带来的是"斜面 + 重力分量"，不是重力方向改变。小球的物理重力同理 |
| 斜面行走 | ✅ 支持（≤ 可行走坡度 50°，盒子限位 20°） | 实测在倾斜 17° 的内壁上行走、停下不滑 |

---

## 7. 当前存在的 Bug、未完成事项与已知限制

### 7.1 已实测到的行为缺陷

1. **盒子会长时间缓慢摆动，不是静止**。最后一次复测：行走 5 s 后峰值 17.3°，松手后 14.5° → 12 s 后 0.3°。原因是小球在倾斜地板上滚动、把重心带回另一侧，盒子跟着回摆；`AngularDamping = 1.2` 压不住这种"球驱动"的低频摆动。**不是崩溃，但"稳定在某个角度"的说法只在短窗口内成立。**
2. **限位是硬手段**。到 20° 直接把角速度和超出角度抹掉，没有反弹/摩擦/滑移，视觉上会有"撞墙感"。
3. **角色搬运是 teleport 式的**。`SetActorLocationAndRotation(..., TeleportPhysics)` 不产生速度、不与物理世界交换冲量。角色对盒子的影响只通过"质量分布"体现，不是真实反作用力；角色与小球之间的推挤是 Chaos 碰撞，但角色位置每帧被强行改写，穿模风险未专门测试。
4. **相机是刚性钉墙**，没有 SpringArm 的碰撞回避：小球 / 角色贴近镜头时会直接穿进画面（`CameraWallInset = 12` 只能防近裁剪切墙）。
5. **外壳不投影**（`bShellCastsShadow = false`）。室内亮度完全靠"外部光透进来"，所以"封闭金属盒"的光照是假的；一打开投影，室内立刻全黑。
6. **`ClampPawnToRoomFloor` 有前提**：只在 `CurrentFloor.HitResult.GetActor() == RoomShell` 时生效。站在球上、跳跃、下落都不管；如果因为穿模被顶起且脚底命中的不是房间，仍会上飘。

### 7.2 结构性问题

7. **没有版本控制**。工程目录不是 git 仓库，任何改动不可回滚（2026-11-20 的回退是手工反做代码完成的）。**交接前强烈建议 `git init` + 首次提交**。
8. **模板遗留物很重**：`Variant_Combat / Variant_Platforming / Variant_SideScrolling` 三套完整示例代码仍参与编译，`Content` 738 个 uasset 绝大部分无用；`Build.cs` 里还留着这些目录的 `PublicIncludePaths`。
9. **`UCLASS(abstract)`**：`AAlongWithYourselfCharacter` 与 `AAlongWithYourselfGameMode` 都是 `abstract`，必须通过 `BP_ThirdPersonCharacter` / `BP_ThirdPersonGameMode` 使用；BP 里的默认值会覆盖 C++ 构造函数（当前记录中没有改过 BP，但**无法从资产二进制确认**）。
10. **`TargetedHardwareClass = Mobile`**（模板默认），桌面端跑没问题，但打包配置、性能预算、移动端着色器都没验证过。
11. **单机假设**：`UGameplayStatics::GetPlayerPawn(this, 0)` 写死在求解器、救援、夹地里，**没有网络复制**（无 `Replicated` 属性、无 RPC），多人不可用。
12. **碰撞通道 `Hitbox` 是工程级 ini 配置**：把角色/资产拷到别的工程时要同步这条 `DefaultChannelResponses`，否则 hitbox 全部失效（静默）。
13. **`Rebuild()` 会被调用两次**：`OnConstruction` 一次、`BeginPlay` 里 `SetCharacterHeight` 触发一次；编辑器中改参数也会立即重建。功能无碍，属于重复开销。
14. **每次改代码都要关编辑器**：Live Coding 占着时 UBT 直接拒绝编译（`Unable to build while Live Coding is active`），本项目的编译一直靠"关掉编辑器 → rsp 直连 cl/link"绕过（见 §9.4）。

### 7.3 明确"没有做"的功能

- 没有战斗/伤害系统，hitbox 只有查询接口（`TraceHitbox` / `GetHitPartName` / `GetHitboxDamageMultiplier`），没有任何消费者。
- 没有 UI、没有存档、没有菜单、没有音效。
- 没有真正的刚体 + 约束的"悬挂"（是用力矩积分模拟的）。
- 没有"多个球"或"角色也能推动盒子旋转"的力学反馈（见 7.1-3）。
- 没有自动化测试；所有验证靠 `-ExecCmds` 打日志 + 人工判读。

### 7.4 环境 / 工程配置风险

15. **DDC 在 `E:/UE_DDC`**，`C:` 盘只剩 ~2.5 GB；换机器要重建缓存。**不要删** `D:/UE/UE_5.7/Engine/DerivedDataCache/Compressed.ddp`（引擎只读 DDC pak，删了会触发全量重编）。
16. `Intermediate` 2.6 GB、`Binaries` 67 MB，交接时如果压缩打包会很大（可以只交 `Source / Config / Content / .uproject`）。

---

## 8. AI 自动实现 vs 人工操作

按三天记录（2026-11-18、2026-11-20、2026-11-24）逐条对照：

### 8.1 由 AI 执行并落地到工程里的

- 全部 C++ 源码：`RoomShell`、`RoomBall`、角色/GameMode 的改造、`Build.cs`、日志分类。
- 全部 `Config/*.ini` 改动（地图、GameMode、碰撞通道、DDC、Zen）。
- 全部资产创建/重建：`Lvl_Room`、`M_RoomMetal`、`M_Ball`（通过**编辑器 Python 命令行一次性脚本**完成，脚本本身已删除，不留痕；因此这些资产的"可重复生成能力"目前**不在工程里**）。
- `.vscode` 三件套（VS Code 报错修复）。
- 编译：UBT + `-NoUBA`，以及环境异常时用 `cl.exe @*.rsp` → `link.exe @*.dll.rsp` 手动编译链接。
- 实机验证：`-game` 启动 + `-ExecCmds` 注入调试命令 + 读 `Saved/Logs/AlongWithYourself.log`，必要时 `HighResShot` 截图。
- 回退（2026-11-20）：手工把摩擦/倾角/按面坡度覆盖逐项反做回 `GroundFriction 8 / Braking 2000 / MaxTiltAngle 20°`，删除静摩擦保持代码。
- 两次结束 `UnrealEditor` 进程（编译前需要）。

### 8.2 由你（用户）主导的

- **全部需求定义与验收**：从"清场景只留角色"到"盒内小球""3×身高""绕任意过形心轴""hitbox + 重力""固定平视摄像机""重心驱动旋转"，共 9 轮需求。
- **两处关键方案选择**（记录中你明确选过）：
  1. 摄像机 → 「完全刚性钉在墙内侧」；
  2. 旋转 → 「纯自由悬挂物理」。
- **一次明确否决**：摩擦方案（放宽到 65°）→ 要求回退。
- 授权我结束编辑器进程（有未保存内容风险的确认）。

### 8.3 无法确认的

- 2026-11-21 ~ 11-23 之间你是否在编辑器里手动改过参数/移动过 Actor/保存过关卡——记录里没有痕迹，`Lvl_Room.umap` 的最后修改时间是 **2026-11-18 22:31**，按此推断 18 日之后没有保存过该关卡。
- 三个模板 BP 是否被手动改过默认值（二进制资产无法核对）。

> 一句话：**代码、资产、配置、编译、测试全部是 AI 写的**；你做的是"产品决策 + 方案选型 + 验收"，以及最后这次"整体回退"的判断。

---

## 9. 关键类 / 函数地图与调用关系

### 9.1 核心类一览

| 类 | 头文件 | 实现 | 行数 |
|---|---|---|---|
| `ARoomShell` | `Source/AlongWithYourself/Room/RoomShell.h` | `.cpp` | 426 / 857 |
| `ARoomBall` | `Source/AlongWithYourself/Room/RoomBall.h` | `.cpp` | 79 / 124 |
| `AAlongWithYourselfCharacter` | `Source/AlongWithYourself/AlongWithYourselfCharacter.h` | `.cpp` | 372 / 872 |
| `AAlongWithYourselfGameMode` | `Source/AlongWithYourself/AlongWithYourselfGameMode.h` | `.cpp` | 53 / 130 |
| `AAlongWithYourselfPlayerController` | 同目录 | `.cpp` | — / 67 |

### 9.2 启动时序（真实调用链）

```
UEngine::LoadMap(/Game/Room/Lvl_Room)
└─ AGameModeBase::InitGame
   └─ AAlongWithYourselfGameMode::InitGame()                     GameMode.cpp:26
      └─ EnsureRoomScene()                                       GameMode.cpp:69
         ├─ FindRoomShell() / World->SpawnActor<ARoomShell>()
         ├─ Room->SetCharacterHeight(HalfHeight*2)                → Rebuild()
         ├─ FindPlayerStartActor()
         └─ PlayerStart->SetActorLocation(内地面 + 半高 + 4)      （实测量到 z=-166）

└─ ARoomShell::OnConstruction()  → Rebuild()                     RoomShell.cpp:101 / 639
└─ ARoomShell::BeginPlay()                                       RoomShell.cpp:108
   ├─ Rebuild()                                                  （外壳 6 面 + 14 个碰撞体 + 相机位）
   ├─ SetCharacterHeight(玩家胶囊高)                             （玩家此时可能还没生成 → Tick 里补一次）
   └─ SpawnBallIfMissing()                                       RoomShell.cpp:133
      └─ SpawnActorDeferred<ARoomBall> → SetBallRadius → FinishSpawningActor
         └─ ARoomBall::BeginPlay() → SetSimulatePhysics(true)     RoomBall.cpp:47

└─ AAlongWithYourselfCharacter::BeginPlay()                      Character.cpp:100
   ├─ ApplyPhysicsSettings()                                     Character.cpp:199
   │   ├─ CMC: GravityScale / GroundFriction / Braking / WalkableFloorAngle
   │   ├─ CMC: bOrientRotationToMovement=false, bIgnoreBaseRotation=true
   │   └─ Capsule: SetSimulatePhysics(false), SetMassOverride(80)
   ├─ RebuildHitboxes()                                          Character.cpp:253
   │   └─ 10 × NewObject<UBoxComponent> + RegisterComponent + AttachToComponent(Capsule)
   ├─ TActorIterator<ARoomShell> → RoomShell->SetCharacterHeight()
   ├─ ApplyOverviewCamera()                                      （bUseRoomCamera=true 时直接 return）
   └─ ApplyRoomCameraView()                                      Character.cpp:140
       └─ PC->SetViewTarget(RoomShell)
          └─ 渲染时 ARoomShell::CalcCamera() → FaceCamera->GetCameraView()
```

### 9.3 每帧调用链（两个 Tick 的先后是重点）

```
[帧开始]
ARoomShell::Tick(dt)                                            RoomShell.cpp:192
├─ UpdateInertialRotation(dt)                                   RoomShell.cpp:286
│  ├─ ComputeContentCenterOfMass()                              RoomShell.cpp:241
│  │   ├─ GetPlayerPawn(0) → 胶囊位置 − Up×半高×0.25
│  │   └─ Ball->GetActorLocation()
│  ├─ τ / I / ω 积分 / 阻尼 / 限位（详见 §4.1）
│  ├─ ShellPivot->SetRelativeRotation(CurrentRotation)
│  └─ PendingCarryQuat *= 本帧增量                            ← 只累积，不搬人
├─ RescueBallIfOutside()                                        RoomShell.cpp:761
└─ RescuePawnIfOutside()                                        RoomShell.cpp:798

[CharacterMovementComponent 解算：重力、扫掠、贴地]

AAlongWithYourselfCharacter::Tick(dt)   (TickGroup = TG_PostPhysics)   Character.cpp:490
├─ ApplyOverviewCamera() / 抢回 ViewTarget
├─ UpdateRoomDrag()                                             Character.cpp:565
│   └─ R 键 → RoomShell->ResetRotation()；拖拽 → RoomShell->AddRotation()
├─ [可选] bDebugAutoWalk → AddMovementInput(FaceCamera 右方向)
├─ RoomShell->ApplyPendingCarryToPawn()                          RoomShell.cpp:435
│   └─ CarryPlayerWithRotation(Delta)                           RoomShell.cpp:497
│       └─ SetActorLocationAndRotation(绕形心转, 朝向同转, TeleportPhysics)
├─ RoomShell->ClampPawnToRoomFloor()                            RoomShell.cpp:449
├─ [可选] bLogBalance → RoomCharState() 每秒一行
└─ DrawHitboxDebug()
```

### 9.4 输入链

```
EnhancedInput (IMC_Default) → IA_Move → AAlongWithYourselfCharacter::Move()
 └─ DoMove(Right, Forward)                                      Character.cpp:650
     ├─ 取方向：FaceCamera->GetForwardVector()/GetRightVector()  （bUseRoomCamera）
     │          或 Controller 的 yaw 旋转（旧路径）
     └─ AddMovementInput()

IA_Look → Look() → DoLook() → AddControllerYawInput/Pitch（手柄右摇杆；鼠标映射已移除，
                                        鼠标只用于拖拽旋转，且拖拽默认关闭）
IA_Jump → Jump() / StopJumping()
```

### 9.5 核心机制的技术解释（给接手人的话）

1. **为什么"旋转"要拆成两步**：`CharacterMovementComponent` 是权威——它在自己的 Tick 里决定角色位置。任何"外部想把角色搬到别处"的逻辑，只要跑在它之前就会被覆盖。整个 `PendingCarryQuat` 机制的本质就是**把搬运推到 CMC 之后**（`TG_PostPhysics`），代价是角色在这一帧的碰撞检测用的是旧位置，快速旋转时会出现 1 帧的视觉滞后。

2. **为什么会有"上飘"**：CMC 的贴地是沿**世界 −Z** 扫掠，而房间的地板法线是**房间局部 +Z**。盒子倾 20° 时两个方向差 20°，每帧的贴地修正会多给一个沿世界 Z 的分量；20° 时这个分量很小（cos20° ≈ 0.94），但每帧累加、且没有任何东西把它扣回去，于是人会缓慢"往上蹭"。`ClampPawnToRoomFloor` 就是把这个漂移一次性吃掉——这是一个**工程补丁，不是物理解**。

3. **为什么会变成滚筒**：内容物重心 `r` 一偏离形心，力矩就把盒子往"r 竖直向下"的方向拽。角色或球一动，`r` 就变，而"最低点"也跟着变，于是盒子持续转（正反馈）。真实的钟摆有唯一稳定点；这里因为"乘客会走路、球会滚"，稳定点本身在移动。**唯一稳妥的收敛手段是限位**（当前 20°），这也是 2026-11-20 那次"靠静摩擦代替限位"的实验失败后回退的原因。

4. **为什么 `bIgnoreBaseRotation = true`**：关掉它时引擎会按基座旋转再额外搬一次，与本项目的手动搬运叠加成"一帧搬两倍"，表现为角色顺着内壁往上爬。开着它，CMC 只认位移不认旋转，正好配合手动搬运。

---

## 10. 交接建议（可选，不属于当前实现）

1. `git init` + 一次完整提交，再打一个 `v0.1` tag——现在任何回退都得靠人肉反做（本次回退就是这么做的）。
2. 把 `Variant_*` 三套示例目录从模块里摘出去（或至少标注"未使用"），能显著缩短编译时间。
3. 想要"盒子真的翻过来"，先解决 `bIgnoreBaseRotation` 与"胶囊随内壁旋转"的矛盾（可能需要改成自定义 `UCharacterMovementComponent`，让 up 跟着房间走），否则 20° 限位是必须的。
4. 想要"稳定收敛"而不是硬限位，可试：加大 `AngularDamping`、给球加滚动阻力（`AngularDamping`/`LinearDamping`）、或把 `UprightStiffness` 开到一个小区间。
5. 把资产生成过程脚本化并保留在 `Scripts/`（现在只有结果，没有可重放的过程）。

---

## 附录 A：复现命令

```bash
# 编译（必须先关掉 UnrealEditor / LiveCodingConsole，否则 UBT 直接拒绝）
"D:/UE/UE_5.7/Engine/Build/BatchFiles/Build.bat" AlongWithYourselfEditor Win64 Development \
  -Project="F:/unrealprojects/AlongWithYourself/AlongWithYourself.uproject" -WaitMutex -FromMsBuild -NoUBA

# 本机环境异常时（UBT 拉起的 cl/link 会被静默杀掉）的绕过方式：
#   1) 先跑一次上面那条让 UHT 生成头文件与 rsp
#   2) cd D:/UE/UE_5.7/Engine/Source
#   3) cl.exe   @".../Intermediate/.../<File>.cpp.obj.rsp"      （含 Module.AlongWithYourself.1/2.cpp）
#   4) link.exe @".../Intermediate/.../UnrealEditor-AlongWithYourself.dll.rsp"

# 实机验证（注意地图路径是 /Game/Room/Lvl_Room；Git Bash 必须关掉路径转换）
MSYS2_ARG_CONV_EXCL='*' "D:/UE/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" \
  "F:/unrealprojects/AlongWithYourself/AlongWithYourself.uproject" /Game/Room/Lvl_Room \
  -game -windowed -resx=1280 -resy=720 -log \
  -ExecCmds="RoomBalanceDebug 1, RoomWalkFor 5, RoomShot 20"

# 日志永远在这里（-LogFile 在 -game 下不生效）
Saved/Logs/AlongWithYourself.log
# 关心的关键字： [Balance] / [Friction] / [Hitbox] / [CharState] / [Camera] / [RoomScene]
```

## 附录 B：关键参数速查

| 参数 | 当前值 | 位置 |
|---|---|---|
| `SizeMultiplier` | 3（→ RoomSize 540） | `RoomShell.h` |
| `WallThickness` | 10 | 同上 |
| `BoundarySegments` / `BoundaryInset` | 12 / 10 | 同上 |
| `MaxTiltAngle` | **20°** | 同上 |
| `ShellMass` / `CharacterMass` / `BallMass` | 800 / 80 / 20 kg | 同上 |
| `AngularDamping` / `MaxAngularSpeed` | 1.2 /s、40 °/s | 同上 |
| `UprightStiffness` | 0 | 同上 |
| `CameraWall` / `CameraEyeHeight` / `CameraWallInset` / `CameraFOV` | 0 / 105 / 12 / 90° | 同上 |
| `BallRadius` / `BallSpawnHeightRatio` | 30 / 0.55 | 同上 |
| 胶囊 | 半径 35 / 半高 90 | `AlongWithYourselfCharacter.cpp` 构造函数 |
| `GravityScale` / 质量 | 1.0 / 80 kg | 同上 |
| `GroundFriction` / `BrakingDecelerationWalking` | 8 / 2000 | 同上（`ApplyPhysicsSettings`） |
| `MaxWalkableSlopeAngle` | 50° | `Character.h` |
| `MaxWalkSpeed` / `JumpZVelocity` | 500 / 500 | 构造函数 |
| `bEnableInertialRotation` | true | `RoomShell.h` |
| `bEnableMouseDragRotate` | false | `Character.h` |
| `bUseRoomCamera` | true | `Character.h` |
| `bShellCastsShadow` | false | `RoomShell.h` |
