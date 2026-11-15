# TH185 ECL 操作码缺口清单（对照 TH20）

> 状态：**93 / 117 个实体操作码在 TH20 有权威实现；48 个尚未移植。**
> 日期：2026-11-15

---

## 1. 为什么能对照 TH20

`eagler-touhou-th11/docs/TOUHOU_PROGRESS.md` 的官方进度表里 TH185 是
**Unassigned**（全球无还原项目），但列出了同代际的 **TH20**：

```
th20  Oracatt/Touhou20            还原 ✅
      Ritosa2333/eagler-touhou   移植 ✅
```

TH20 的 `source_reconstruction/audit/ecl_opcode_coverage.json`（564KB）
是**完整操作码审计**：224 个实体 opcode + 75 个通用 opcode，
每条都带原版地址、源码文件、函数名、行号和实现摘录。

### 决定性验证

把 TH185 实际用到的操作码与 TH20 的表做集合比较：

```
通用区（<300）：TH185 有但 TH20 没有 = []      ← 空集
实体区（>=300）：TH185 用 117 个，93 个在 TH20 表里
```

**通用操作码完全是子集**，实体操作码 79% 有参考。

## 2. TH20 的分发表解出方法

比逐个逆向快一个数量级：

```
entity_entry_va       0x0048c010
minimum_opcode        300
maximum_opcode        1003
selector_table_va     0x004966b8   704 项
target_table_va       0x00496400   174 项
default_return_zero   0x004963df

core_entry_va         0x0053b5c0
core_target_table_va  0x0053e128    98 项
core_delegate_default 0x0053def5
```

704 个选择槽里 224 个映射到具体动作，480 个映射到默认返回。

## 3. 尚未移植的 48 个

按指令数排序（TH185 全量 23770 条）：

| opcode | 次数 | TH20 语义 |
|---|---:|---|
| 502 | 418 | `s.fields_2c8[0]\|=unsigned(r.integer(0))&0x3fffu;if(s.fields_2c8[0]&32u` |
| 616 | 188 | `{const float radius=r.real(0);env.cancel_circle(r.context(),position()` |
| 1003 | 153 | `r.state.fields_250[10]=unsigned(r.integer(0));break;` |
| 525 | 147 | `env.clear(r.controller(),EnemyClearKind::ordinary,0);break;` |
| 306 | 145 | `{const int index=r.integer(0),script=r.integer(1);auto& link=resize_li` |
| 609 | 135 | `{auto& q=at(true);auto v=shot_values(q);auto& metadata=meta(q);const b` |
| 1002 | 127 | `r.controller().field_e0=unsigned(r.integer(0));break;` |
| 303 | 114 | `{const int index=r.integer(0),script=r.integer(1);auto& link=resize_li` |
| 615 | 83 | `{const float radius=r.real(0);env.cancel_circle(r.context(),position()` |
| 555 | 49 | `{ if(r.get<int>(0x250)>=0)env.sound(r.get<int>(0x250),position().x); i` |
| 441 | 48 | `{MotionEditor edit(record(s,op==441?selected_index():1));auto& m=edit.` |
| 522 | 40 | `{ char name[128]{};const auto length=r.raw_word(12);if(length>128\|\|16u` |
| 422 | 34 | `{ MotionEditor edit(record(s,(op==408\|\|op==420)?selected_index():1));a` |
| 313 | 31 | `{const int index=r.integer(0);const int variant=r.opcode()==316?r.inte` |
| 520 | 31 | `for(unsigned i=0;i<3;++i)if(env.selected(r.controller(),i))return -1;b` |
| 603 | 31 | `{auto& q=at(false);auto v=shot_values(q);v.offset.x=r.real(1);v.offset` |
| 610 | 31 | `{auto& q=at(true);auto v=shot_values(q);auto& metadata=meta(q);const b` |
| 511 | 29 | `health.words[0]=unsigned(r.integer(0));health.words[1]=health.words[0]` |
| 627 | 26 | `{const float radius=r.real(0);env.cancel_circle(r.context(),position()` |
| 626 | 18 | `s.fields_2c8[7]=env.callback(2,r.integer(0));break;` |
| 518 | 16 | `env.dialogue(r.integer(0));env.cancel_bullets(r.context());env.erase_l` |
| 519 | 13 | `if(env.hud().collecting&&env.hud().collecting->field_100==0)return -1;` |
| 526 | 11 | `{const float radius=r.real(0);r.put(0x274,recovered::mul32(radius,radi` |
| 319 | 9 | `change_enemy_animation(r,env);break;` |
| 554 | 9 | `{const float y=r.real(1),x=r.real(0);static_cast<bullet::Controller*>(` |
| 628 | 7 | `{const float radius=r.real(0);env.cancel_circle(r.context(),position()` |
| 509 | 5 | `if(env.session().mode!=2)drop();break;` |
| 624 | 5 | `s.fields_2c8[6]=env.callback(0,r.integer(0));s.fields_2c8[4]=op==631;b` |
| 625 | 5 | `s.fields_2c8[5]=env.callback(1,r.integer(0));break;` |
| 506 | 4 | `reset_enemy_drop_counts(pattern);break;` |
| 508 | 4 | `{const float y=r.real(1),x=r.real(0);pattern.field_a0=x;pattern.field_` |
| 510 | 4 | `pattern.fields_00[1]=unsigned(r.integer(0));break;` |
| 301 | 3 | `spawn_enemy_from_opcode(r,env);break;` |
| 702 | 3 | `{auto& q=queued();const auto v=shot_values(q);const auto& m=metadata(q` |
| 423 | 2 | `{ MotionEditor edit(record(s,(op==409\|\|op==421)?selected_index():1));a` |
| 424 | 2 | `{const auto value=unsigned(r.integer(0));s.fields_2c8[1]=(s.fields_2c8` |
| 608 | 2 | `{auto& q=at(true);meta(q).field_3c=r.integer(1);meta(q).field_40=r.int` |
| 630 | 2 | `{const int score=r.integer(0);env.add_score(r.context(),position(),sco` |
| 700 | 2 | `{auto& q=queued();auto v=shot_values(q);v.parameters.position.x=r.real` |
| 308 | 1 | `{ sprite::Vec3 position=r.get<sprite::Vec3>(0x110);float angle=0; if(r` |
| 318 | 1 | `{auto& link=s.animations.at(0);env.delete_animation(link.handle);attac` |
| 320 | 1 | `{const int index=r.integer(0);s.animations.at(unsigned(index)).offset[` |
| 333 | 1 | `change_enemy_animation(r,env);break;` |
| 338 | 1 | `{ sprite::Vec3 position=r.get<sprite::Vec3>(0x110);float angle=0; if(r` |
| 408 | 1 | `{ MotionEditor edit(record(s,(op==408\|\|op==420)?selected_index():1));a` |
| 444 | 1 | `{MotionEditor edit(record(s,op==444?selected_index():1));const float v` |
| 517 | 1 | `{const int third=r.integer(2),second=r.integer(1),first=r.integer(0);e` |
| 565 | 1 | `env.clear(r.controller(),EnemyClearKind::no_effect,0);break;` |

合计 **1995 条指令**。

## 4. 已移植的（本轮）

| opcode | TH20 语义 | 实现方式 |
|---|---|---|
| 302 | `s.fields_1c[0] = integer(0)` | 已有 case，改名 |
| 500 | `s.bounds_5c = {real(0), real(1)}` | 已有 case，改名 |
| 501 | `s.bounds_64 = {real(0), real(1)}` | 已有 case，改名 |
| 503 | `fields_2c8[0] &= ~(arg & 0x3fff)` | 已有 case，改名 |
| 504 | `fields_2c8[1] |= 2` | 已有 case，改名 |
| 505 | `fields_2c8[1] &= ~2` | 已有 case，改名 |

## 5. ★ 一个失败的尝试

我把 `op502/525/616/1002/1003` 从 `kInferredNet`（只记录、net 0）
移到专用 case，想「让它们真正执行」。结果：

```
faults   2 → 32393
总耗时  2.6s → 171s
```

**原因**：`kInferredNet` 的 `net 0` 是一个声明「这个 opcode 不碰栈」。
移到专用 case 后，`inferred_net` 的提前 `continue` 不再生效，
而我的专用 case 只写了状态更新、没写栈效果，于是栈平衡被破坏。

**结论**：移植一个操作码时，「状态更新」和「栈效果」是两件独立的事。
TH20 的 `body_excerpt` 只给出前者；后者要靠 `kInferredNet` 的 net 值
或者从循环体结构反推。只做前者会让栈失衡，比「只记录」更糟。

已全部回退，`faults=2`（既有问题）恢复。

## 6. 正确的移植顺序

1. 先确认该 opcode 的**栈净效应**（`kInferredNet` 的 net 值、或循环体反推）
2. 再移植 TH20 的**状态更新**逻辑
3. 两者都写进同一个 case，且不能同时出现在 `kInferredNet` 里
4. 每移植一个就跑 9 关验证 `faults` 不增加

## 7. 工具

```powershell
# TH20 审计 JSON（只读，不修改）
th20/source_reconstruction/audit/ecl_opcode_coverage.json

# TH20 的各族实现
th20/source_reconstruction/gameplay/enemy_opcode_*.cpp
th20/source_reconstruction/gameplay/enemy_shot.cpp
```