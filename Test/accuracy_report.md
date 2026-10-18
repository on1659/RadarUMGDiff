# RadarUMGDiff 정확도 보고서

`tools/accuracy_report.py` 가 만든다. 손으로 고치지 않는다.

## 커버리지

| 파일 | 익스포트 바이트 | 전체 | 범위 안 (UStruct 데이터 제외) |
|---|---:|---:|---:|
| WBP_Fix_Anim v1 | 24126 | 0.9762 | 1.0000 |
| WBP_Fix_Anim v2 | 24126 | 0.9762 | 1.0000 |
| WBP_Fix_Anim v3 | 24126 | 0.9762 | 1.0000 |
| WBP_Fix_Anim v4 | 27031 | 0.9750 | 1.0000 |
| WBP_Fix_Bind v1 | 14545 | 0.9493 | 1.0000 |
| WBP_Fix_Bind v2 | 14545 | 0.9493 | 1.0000 |
| WBP_Fix_Bind v3 | 13605 | 0.9458 | 1.0000 |
| WBP_Fix_Child v1 | 8017 | 0.9783 | 1.0000 |
| WBP_Fix_Graph v1 | 14869 | 0.9137 | 1.0000 |
| WBP_Fix_Graph v2 | 17464 | 0.9230 | 1.0000 |
| WBP_Fix_Graph v3 | 17465 | 0.9230 | 1.0000 |
| WBP_Fix_Graph v4 | 17506 | 0.9228 | 1.0000 |
| WBP_Fix_Graph v5 | 17506 | 0.9228 | 1.0000 |
| WBP_Fix_Layout v1 | 19877 | 0.9168 | 1.0000 |
| WBP_Fix_Layout v2 | 16774 | 0.9204 | 1.0000 |
| WBP_Fix_Text v1 | 9880 | 0.9511 | 1.0000 |
| WBP_Fix_Text v2 | 9884 | 0.9511 | 1.0000 |
| WBP_Fix_Text v3 | 9884 | 0.9511 | 1.0000 |
| WBP_Fix_Text v4 | 9884 | 0.9511 | 1.0000 |
| WBP_Fix_Text v5 | 11840 | 0.9334 | 1.0000 |
| WBP_Fix_Text v6 | 11840 | 0.9334 | 1.0000 |
| **합계** | 334794 | 0.9464 | 1.0000 |

해석하지 않은 바이트 (구조체·접미부별):

- `(struct-data:WidgetBlueprintGeneratedClass)` 13845 바이트
- `(struct-data:Function)` 4116 바이트

### 픽스처 — 네이티브 vs Python

쌍 15개 · 판정 불일치 **0** · 판정 보류 0
