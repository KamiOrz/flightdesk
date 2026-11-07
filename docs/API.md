# 位置 API 约定

固件不依赖服务部署细节。实现相同 JSON 的其他服务也可以替换当前适配器。

```text
GET /api/nearby?lat=31.230&lon=121.470&radius_km=20&airborne=1
```

返回示意（示例坐标和飞机不是实时数据）：

```json
{
  "now": 1700000000,
  "upstream_age": 1,
  "ac": [{
    "hex": "abcdef", "flight": "TEST123", "r": "EXAMPLE", "t": "A320",
    "lat": 31.24, "lon": 121.48, "seen_pos": 1,
    "alt_baro": 10000, "gs": 200, "track": 90,
    "origin": "AAA", "destination": "BBB", "onground": "0"
  }]
}
```

`now` 为 Unix 时间戳（秒或毫秒）；`upstream_age` 为上游最后一帧年龄，单位秒。`seen_pos` 为单架位置年龄，单位秒。坐标为 WGS84；`alt_baro` 是英尺或字符串 `ground`，`gs` 是节，`track` 是度。注册号、机型、机场允许缺失；身份字段至少需要 hex、注册号或呼号之一。不要伪造地址。

固件会再次检查距离、年龄与地面状态。接口必须返回有效新鲜度字段，过期不能更新成当前时间冒充新位置。上游失败应返回非 2xx，不要返回成功的空列表掩盖错误。详细边界以 `include/feed_freshness.h`、`include/observation.h` 为准。

## 自带适配器

```sh
cd tools/flight-source-probe
npm ci
npm test
HOST=0.0.0.0 PORT=8787 node server.cjs
```

默认只监听回环地址。需要开放给设备时显式配置 HOST，并使用防火墙限定在可信局域网。兼容 `/v2/point/{lat}/{lon}/{radius_nm}`；附近查询使用 `radius_km`，单位不要混用。

`server.cjs` 按查询范围订阅上游位置并转成以上格式，`nearby.cjs` 提供交互诊断。没有内置演示飞机或自动伪造定位。上游网页协议不稳定，也不构成官方 API 使用授权；失效时可自行提供符合约定的来源。
