# RadarUMGDiff 정확도 보고서

`tools/accuracy_report.py` 가 만든다. 손으로 고치지 않는다.

## 커버리지

| 파일 | 익스포트 바이트 | 전체 | 범위 안 (UStruct 데이터 제외) |
|---|---:|---:|---:|
| WBP_Fix_Anim v1 | 25569 | 0.9756 | 1.0000 |
| WBP_Fix_Anim v2 | 25569 | 0.9756 | 1.0000 |
| WBP_Fix_Anim v3 | 25569 | 0.9756 | 1.0000 |
| WBP_Fix_Anim v4 | 29290 | 0.9751 | 1.0000 |
| WBP_Fix_Bind v1 | 13212 | 0.9416 | 1.0000 |
| WBP_Fix_Bind v2 | 13212 | 0.9416 | 1.0000 |
| WBP_Fix_Bind v3 | 12259 | 0.9370 | 1.0000 |
| WBP_Fix_Child v1 | 5288 | 0.9871 | 1.0000 |
| WBP_Fix_Graph v1 | 12824 | 0.8986 | 1.0000 |
| WBP_Fix_Graph v2 | 15468 | 0.9119 | 1.0000 |
| WBP_Fix_Graph v3 | 15469 | 0.9120 | 1.0000 |
| WBP_Fix_Graph v4 | 15510 | 0.9118 | 1.0000 |
| WBP_Fix_Graph v5 | 15510 | 0.9118 | 1.0000 |
| WBP_Fix_Layout v1 | 19696 | 0.9655 | 1.0000 |
| WBP_Fix_Layout v2 | 16076 | 0.9653 | 1.0000 |
| WBP_Fix_Text v1 | 9266 | 0.9424 | 1.0000 |
| WBP_Fix_Text v2 | 9270 | 0.9424 | 1.0000 |
| WBP_Fix_Text v3 | 9270 | 0.9424 | 1.0000 |
| WBP_Fix_Text v4 | 9270 | 0.9424 | 1.0000 |
| WBP_Fix_Text v5 | 13218 | 0.9326 | 1.0000 |
| WBP_Fix_Text v6 | 13218 | 0.9326 | 1.0000 |
| CL9 git 블롭 37개 (합) | | | |
| **합계** | 1111953 | 0.9797 | 1.0000 |

해석하지 않은 바이트 (구조체·접미부별):

- `(struct-data:WidgetBlueprintGeneratedClass)` 17475 바이트
- `(struct-data:Function)` 5140 바이트

### CL9 git 히스토리 — 네이티브 vs Python

쌍 22개 · 판정 불일치 **0** · 판정 보류 5

- `WBP_HUD\4475389_a4662be` cdo CookGaugeLift — Python: 기본값일 수 있음(판정 보류)
- `WBP_HUD\a4662be_7fec481` cdo CookGaugeLift — Python: 기본값일 수 있음(판정 보류)
- `WBP_HUD\7fec481_5fc74bc` cdo HotbarSlotClass — Python: 기본값일 수 있음(판정 보류)
- `WBP_HUD\5fc74bc_347f315` cdo HotbarSlotClass — Python: 기본값일 수 있음(판정 보류)
- `WBP_Scan\2b309e5_354532d` cdo ScanKinds[0] — Python: 기본값일 수 있음(판정 보류)

### CL9 git 히스토리 — 네이티브 vs Python (스키마)

쌍 22개 · 판정 불일치 **0** · 판정 보류 2

- `WBP_HUD\7fec481_5fc74bc` cdo HotbarSlotClass — Python: 기본값일 수 있음(판정 보류)
- `WBP_HUD\5fc74bc_347f315` cdo HotbarSlotClass — Python: 기본값일 수 있음(판정 보류)

### 픽스처 — 네이티브 vs Python

쌍 15개 · 판정 불일치 **0** · 판정 보류 0

### 픽스처 — 네이티브 vs Python (스키마)

쌍 15개 · 판정 불일치 **0** · 판정 보류 0

## 스키마 유무와 판정

쌍 37개 · 스키마를 넣어 사라지거나 바뀐 판정 **0** · 스키마가 확정한 보류 줄 123 (엔진 5.8.3)
