# Web 大屏模块 - 充电桩运营管理大屏

## 一、模块简介

本模块是充电桩运营管理平台的 **Web 大数据可视化大屏**，面向运营决策者，提供充电桩运营数据的实时监控与可视化分析。

**核心功能**：

- 管理员登录（获取 Token）
- KPI 核心指标卡（今日/本月/总营收、用户数、充电站、电桩总数）
- 营收趋势图（近 7 日/近 30 日切换）
- 电桩状态分布（环形图）
- 充电站列表（点击查看电桩详情）
- 数据自动刷新（每 5 秒）

---

## 二、技术栈

| 技术 | 版本 | 用途 |
|:---|:---:|:---|
| HTML5 + CSS3 | — | 页面结构与样式 |
| JavaScript (ES6) | — | 业务逻辑 |
| ECharts | 5.4.3 | 图表渲染（折线图、饼图） |
| axios | 1.6.0 | HTTP 请求（联调时使用） |

---

## 三、文件结构
```
web-dashboard/
├── login.html # 管理员登录页
├── dashboard.html # 大屏主页面
└── README.md # 本文件
```
---

## 四、运行方式

### 方式一：开发模式（Mock 数据）—— 当前可用

1. 直接用浏览器打开 `dashboard.html`
2. 页面自动展示假数据，无需后端

> 页面左下角会显示 **🧪 Mock 模式 (假数据)**

### 方式二：联调模式（真实接口）

1. 确保后端服务已启动
2. 修改 `dashboard.html` 中的 `USE_MOCK = false`
3. 确认 `API_BASE_URL` 指向后端地址
4. 打开 `login.html`，输入账号密码登录
5. 自动跳转到大屏，展示真实数据

> 页面左下角会显示 **🔗 已联调 (真实接口)**

---

## 五、接口清单（联调用）

所有接口均使用 `ADMIN_READONLY` 角色，符合 `apis.md` 契约。

| 接口 | 方法 | 说明 | 用在哪里 |
|:---|:---:|:---|:---|
| `/auth/admin/login` | POST | 管理员登录，获取 Token | `login.html` |
| `/admin/dashboard/summary` | GET | 核心 KPI 指标 | 顶部指标卡 |
| `/admin/dashboard/revenue-series` | GET | 营收趋势（7d/30d） | 折线图 |
| `/admin/dashboard/charger-status` | GET | 电桩状态分布 | 饼图 |
| `/admin/stations` | GET | 充电站列表 | 站点列表 |
| `/admin/stations/{id}/chargers` | GET | 站点电桩详情 | 点击站点弹窗 |

---

## 六、联调检查清单

联调前逐项确认：

- [ ] 后端服务已启动
- [ ] `API_BASE_URL` 配置正确
- [ ] `USE_MOCK` 已改为 `false`
- [ ] `login.html` 能正常登录获取 Token
- [ ] 大屏能正常展示真实数据

---

## 七、遇到问题？

| 问题 | 解决方案 |
|:---|:---|
| 登录失败 | 检查后端服务是否启动，确认账号密码正确 |
| 大屏无数据 | 确认 `USE_MOCK = false`，检查控制台错误信息 |
| 跨域报错 | 联系后端同学配置 CORS |
| Token 过期 | 重新登录即可 |
