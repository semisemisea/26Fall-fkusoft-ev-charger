---
name: EV Charger Mobile
description: 充电桩用户端桌面应用的设计系统，手机隐喻 UI，Stripe 式靛蓝 + 藏青中性色
colors:
  primary: "#5f63f0"
  primary-deep: "#4f52d9"
  primary-press: "#4749c4"
  primary-soft: "#7b7ff5"
  primary-bg: "#eef0ff"
  primary-border: "#d9dcff"
  on-primary: "#ffffff"
  success: "#10b981"
  success-deep: "#059669"
  success-ink: "#047857"
  success-bg: "#ecfdf5"
  success-disabled: "#a7f3d0"
  warning: "#f59e0b"
  warning-hover: "#fbbf24"
  warning-pressed: "#d97706"
  warning-ink: "#92400e"
  warning-button-ink: "#451a03"
  warning-bg: "#fef3c7"
  error: "#f43f5e"
  error-strong: "#e11d48"
  error-deep: "#be123c"
  error-bg: "#fff1f2"
  text: "#1e293b"
  text-secondary: "#64748d"
  text-disabled: "#94a3b8"
  neutral-gray: "#475569"
  border: "#cbd5e1"
  hairline: "#e2e8f0"
  fill-hover: "#f1f5f9"
  bg-layout: "#f8fafc"
  bg-container: "#ffffff"
  gradient-lavender: "#f5f3ff"
typography:
  page-heading:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 22px
    fontWeight: 700
  hero-title:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 20px
    fontWeight: 700
  page-title:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 18px
    fontWeight: 700
  card-heading:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 16px
    fontWeight: 700
  card-title:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 15px
    fontWeight: 700
  body:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 14px
    fontWeight: 400
  meta:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 12px
    fontWeight: 400
  faint:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 11px
    fontWeight: 400
  metric-large:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 34px
    fontWeight: 700
  metric-medium:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 28px
    fontWeight: 700
  metric-total:
    fontFamily: "PingFang SC, Microsoft YaHei, Noto Sans CJK SC, sans-serif"
    fontSize: 26px
    fontWeight: 700
rounded:
  sm: 6px
  md: 10px
  lg: 14px
  xl: 18px
  pill: 26px
spacing:
  xxs: 4px
  sm: 8px
  md: 12px
  lg: 16px
  xl: 24px
  xxl: 32px
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.on-primary}"
    rounded: "{rounded.md}"
    padding: 10px 14px
  button-primary-hover:
    backgroundColor: "{colors.primary-soft}"
  button-primary-pressed:
    backgroundColor: "{colors.primary-press}"
  button-default:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.text}"
    rounded: "{rounded.md}"
    padding: 10px 14px
  button-default-hover:
    textColor: "{colors.primary}"
  button-danger:
    backgroundColor: "{colors.error-strong}"
    textColor: "{colors.on-primary}"
    rounded: "{rounded.md}"
    padding: 12px
  button-danger-hover:
    backgroundColor: "{colors.error}"
  button-danger-pressed:
    backgroundColor: "{colors.error-deep}"
  button-warning:
    backgroundColor: "{colors.warning}"
    textColor: "{colors.warning-button-ink}"
    rounded: "{rounded.md}"
    padding: 10px 14px
  button-warning-hover:
    backgroundColor: "{colors.warning-hover}"
  button-warning-pressed:
    backgroundColor: "{colors.warning-pressed}"
  button-outline-danger:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.error-strong}"
    rounded: "{rounded.md}"
    padding: 10px 14px
  button-cta-round:
    backgroundColor: "{colors.success-deep}"
    textColor: "{colors.on-primary}"
    rounded: "{rounded.pill}"
    size: 150px
  button-cta-round-hover:
    backgroundColor: "{colors.success}"
  button-disabled:
    backgroundColor: "{colors.fill-hover}"
    textColor: "{colors.text-disabled}"
    rounded: "{rounded.md}"
  input:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.text}"
    rounded: "{rounded.md}"
    padding: 10px 12px
  card:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.text}"
    rounded: "{rounded.lg}"
    padding: 16px
  profile-card:
    backgroundColor: "{colors.bg-container}"
    rounded: "{rounded.xl}"
    padding: 16px
  toast:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.text}"
    rounded: "{rounded.md}"
    padding: 9px 16px
  badge:
    backgroundColor: "{colors.error}"
    rounded: "{rounded.sm}"
    size: 8px
  tag-available:
    backgroundColor: "{colors.success-bg}"
    textColor: "{colors.success-ink}"
    rounded: "{rounded.sm}"
    padding: 2px 8px
  tag-reserved:
    backgroundColor: "{colors.warning-bg}"
    textColor: "{colors.warning-ink}"
    rounded: "{rounded.sm}"
    padding: 2px 8px
  tag-charging:
    backgroundColor: "{colors.primary-bg}"
    textColor: "{colors.primary-deep}"
    rounded: "{rounded.sm}"
    padding: 2px 8px
  tag-fault:
    backgroundColor: "{colors.error-bg}"
    textColor: "{colors.error-deep}"
    rounded: "{rounded.sm}"
    padding: 2px 8px
  offline-badge:
    backgroundColor: "{colors.fill-hover}"
    textColor: "{colors.neutral-gray}"
    rounded: "{rounded.sm}"
    padding: 2px 8px
  select-option-selected:
    backgroundColor: "{colors.primary-bg}"
    textColor: "{colors.primary-deep}"
    rounded: "{rounded.sm}"
    padding: 8px 10px
  divider:
    backgroundColor: "{colors.hairline}"
    height: 1px
  ai-banner:
    backgroundColor: "{colors.primary-bg}"
    textColor: "{colors.primary-deep}"
    rounded: "{rounded.md}"
    padding: 9px 12px
  ai-banner-alt:
    backgroundColor: "{colors.gradient-lavender}"
    textColor: "{colors.primary-deep}"
    rounded: "{rounded.md}"
  profile-header:
    backgroundColor: "{colors.primary-press}"
    textColor: "{colors.on-primary}"
    rounded: "{rounded.xl}"
    padding: 18px
  screen:
    backgroundColor: "{colors.bg-layout}"
  price-text:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.error-strong}"
  helper-text:
    backgroundColor: "{colors.bg-container}"
    textColor: "{colors.text-secondary}"
  cta-disabled:
    backgroundColor: "{colors.success-disabled}"
    rounded: "{rounded.pill}"
  outline-danger-pressed:
    backgroundColor: "{colors.error-bg}"
---

## Overview

手机隐喻的桌面充电应用：390×780 视口内模拟一台设备——顶部假状态栏、底部悬浮胶囊 Tab、页面以覆盖栈切换。视觉基调取自 Stripe 式金融质感：**明亮靛蓝品牌色 + slate 文字 + 冷蓝灰中性面**，配大圆角卡片与克制的层级阴影。目标是"金融工具的精致感 + 移动端轻盈"，不做装饰性渐变堆砌。

## Colors

- **Primary (#5f63f0)**：长春花靛蓝，唯一的操作驱动色——主按钮、选中态、链接、倒计时数字、图标高亮。白字对比 4.6:1，比通用企业蓝更通透年轻。
- **Primary-deep (#4f52d9)**：小号蓝字专用（链接文字、选中项、横幅、预约钮），白底 6.0:1。
- **Primary-press (#4749c4)**：按压态与头部渐变起点，深靛。
- **Primary-bg (#eef0ff) / primary-border (#d9dcff)**：浅靛填充对，用于选中行、迷你钮、AI 横幅底。
- **Success (#10b981)**：状态圆点、充电环、CTA hover；**启动充电大按钮底色用 success-deep (#059669)**（22px 粗体大字按 AA 大字号 3:1 达标，lint 的 4.5 阈值对此豁免）。绿色文字一律 success-ink (#047857)。
- **Warning (#f59e0b)**：去充值按钮、待支付/过期状态。按钮文字用 **warning-button-ink (#451a03)** 深棕，按压 warning-pressed (#d97706)；浅底标签文字用 warning-ink (#92400e)。
- **Error (#f43f5e)**：红点角标、危险按钮 hover。**价格/金额文字与危险实心钮用 error-strong (#e11d48)**（4.7:1），按压与故障标签文字 error-deep (#be123c)。
- **文字层级（slate 系，禁纯黑）**：text #1e293b → text-secondary #64748d → text-disabled #94a3b8。深石板蓝代替纯黑是质感的关键，又比藏青轻盈；禁用态按 WCAG 1.4.3 豁免。
- **中性面（冷蓝调）**：border #cbd5e1（控件描边）、hairline #e2e8f0（卡片描边）、fill-hover #f1f5f9、bg-layout #f8fafc（页面底）、bg-container #fff（卡片/浮层）。灰色一律掺蓝，杜绝"水泥灰"。
- 渐变仅两处：个人中心头部（primary-press → primary）、AI 推荐横幅（primary-bg → gradient-lavender）。

## Typography

系统字体栈（PingFang SC / 微软雅黑 / Noto Sans CJK），不捆绑字体。层级为：page-heading 22 全屏页主标题；hero-title 20 结算/站名；page-title 18 覆盖页顶栏；card-heading 16 / card-title 15 卡片首行；body 14 默认；meta 12 / faint 11 次要信息。数字强调（电量、余额、倒计时）走 metric-large/medium/total，一律 700 字重。标题与数字用字重区分层级，正文不靠颜色硬凑层级。

## Layout

4 的倍数间距体系：xxs 4 / sm 8 / md 12 / lg 16 / xl 24 / xxl 32。页面左右留白 16，卡片间距 12，卡片内边距 16，全屏页顶栏到内容 12。列表信息密度按"一屏 3-4 张卡"控制，卡片内最多两行文字 + 一行操作。

## Elevation & Depth

阴影掺蓝（rgba(15,23,42,α)），呼应冷调中性面，宁缺毋滥：

- **level-1（卡片）**：主要靠 1px hairline 描边区分，投影极轻。
- **level-2（浮层）**：Tab 胶囊与 Toast 用 blur 24-30 / offset 4-6 / 藏青 16-18% 透明度投影，配半透明白底 + 顶部高光边制造悬浮感。

页面栈切换 160ms 淡入，弹窗 160ms 淡入，按压反馈 90ms 缩放 0.97。动效只服务状态变化。

## Shapes

圆角即身份：控件 md 10、卡片 lg 14、个人中心大卡 xl 18、底部胶囊 pill 26、徽章与小操作钮 sm 6、圆形按钮取半径一半。禁止出现体系外圆角。

## Components

- **按钮**：primary（靛蓝底白字，hover 提亮 primary-soft、按压 primary-press）、default（白底冷灰描边，hover 边框文字变靛）、danger（玫红深底）、warning（琥珀底深棕字）、outline-danger（白底玫红字）、CTA 大圆钮（翠绿渐变，仅"启动充电"）。所有按钮 hover/pressed 必须有反馈。
- **Toast**：顶部 44px 居中白卡 + 8px 状态圆点，2.4s 自动消失，不阻塞操作；确认类交互仍用对话框，通知类一律 Toast。
- **卡片**：白底 + 1px hairline 描边 + lg 圆角；电站卡 hover 边框变 primary。
- **状态标签（电桩）**：浅底深字——success-bg/warning-bg/primary-bg/error-bg 配对应 ink 色，禁止白字压浅底。
- **下拉框**：自绘控件，弹层圆角裁剪 + 自绘 chevron，选中项 primary-bg 浅底 primary-deep 字。
- **Tab 栏**：悬浮胶囊内三枚图文按钮，选中 primary，未选中 text-secondary；充电进行中显示 error 红点角标。

## Do's and Don'ts

- Do 保持一个页面只有一个 primary 主操作。
- Do 金额一律 error-strong 加粗，余额/倒计时用 primary。
- Do 状态色通过 Theme.h 引用，禁止硬编码。
- Do 文字用 slate 系（#1e293b/#64748d），永远不用纯黑。
- Don't 在 warning 底上用白字，在浅底上用 text-disabled。
- Don't 新增第三种渐变或第三级阴影。
- Don't 给含网页视图（QWebEngineView）的页面套透明度动效。
- Don't 用 emoji 当图标（目标机缺彩色字体时退化为方框），图标一律 QPainter 自绘。
