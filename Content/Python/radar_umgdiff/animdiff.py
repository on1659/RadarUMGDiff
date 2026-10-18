"""애니메이션 키 단위 diff (dev 문서 §7.4). 네이티브 RadarNativeDiffProducer 의 DiffAnimations 와 같은 규칙.

- 애니메이션: 블루프린트 Animations 의 오브젝트 이름으로 짝짓는다.
- 트랙: "<바인딩 위젯 이름 또는 Root>/<트랙 클래스>" (같은 이름이 또 있으면 #2, #3 …). 루트 트랙 다음 ObjectBindings 순서.
- 섹션: 트랙 Sections 배열 인덱스. 범위(SectionRange)·채널 밖의 섹션 속성은 태그 비교 (SectionRange·Signature·채널 제외).
- 채널: 섹션 속성 중 구조체 이름이 …Channel 인 것. 이름 = 속성 이름, 정적 배열 1번 이후는 이름[i].
  채널 자체 값(DefaultValue·외삽·TickResolution)은 두 쪽 다 저장된 채널일 때만 — 한쪽이 없으면 아키타입 값이라 파일만으로는 모른다.
- 키: 프레임으로 짝짓고(같은 프레임이 여럿이면 등장 순서), 값은 Value 멤버, 나머지(보간·탄젠트)는 속성 줄.
"""

from collections import OrderedDict

from .state import _props_of

SECTION_STRUCTURAL = {"SectionRange", "Signature"}
CHANNEL_STRUCTURAL = {"Times", "Values", "KeyHandles", "bShowCurve"}
KEY_STRUCTURAL = {"Value", "PaddingByte"}


def _is_channel(tag):
    node = tag.get("_node")
    return node is not None and node.name == "StructProperty" and node.params and node.params[0].name.endswith("Channel")


class _Anims:
    def __init__(self, state):
        self.state = state
        self.p2e = {state.path_of[i]: e for i, e in state.by_index.items()}
        self.anims = OrderedDict()
        for apath in state.bp_props().get("Animations") or []:
            ae = self.p2e.get(apath) if isinstance(apath, str) else None
            if ae is not None:
                self.anims[ae["objectName"]] = ae

    def export(self, path):
        return self.p2e.get(path) if isinstance(path, str) else None

    def tracks(self, anim_export):
        """[(트랙 id, 트랙 익스포트)] — 네이티브 CollectTracks 와 같은 순서·같은 id."""
        aprops = _props_of(anim_export)
        ms = self.export(aprops.get("MovieScene"))
        out, used = [], set()

        def add(owner, te):
            cls = te["className"]
            tid = "%s/%s" % (owner, cls)
            n = 2
            while tid in used:
                tid = "%s/%s#%d" % (owner, cls, n)
                n += 1
            used.add(tid)
            out.append((tid, te))

        if ms is None:
            return out
        mprops = _props_of(ms)
        for tpath in mprops.get("Tracks") or []:
            te = self.export(tpath)
            if te is not None:
                add("Root", te)
        names = {}
        for b in aprops.get("AnimationBindings") or []:
            if isinstance(b, dict) and b.get("AnimationGuid"):
                names[b["AnimationGuid"]] = "Root" if b.get("bIsRootWidget") else b.get("WidgetName", "None")
        for binding in mprops.get("ObjectBindings") or []:
            if not isinstance(binding, dict):
                continue
            owner = names.get(binding.get("ObjectGuid"), binding.get("ObjectGuid"))
            for tpath in binding.get("Tracks") or []:
                te = self.export(tpath)
                if te is not None:
                    add(owner, te)
        return out

    def sections(self, track_export):
        return [self.export(p) for p in (_props_of(track_export).get("Sections") or [])]


def _channels(section_export):
    """{채널 이름: (태그, 값)} — 0번 원소는 이름만, 그 뒤는 이름[i] (네이티브 ChannelsOf 와 같은 규칙)."""
    out = OrderedDict()
    for t in section_export.get("properties", []):
        if _is_channel(t):
            name = t["name"] if not t["arrayIndex"] else "%s[%d]" % (t["name"], t["arrayIndex"])
            out[name] = t
    return out


def _range(props):
    r = props.get("SectionRange")
    if not isinstance(r, dict) or "LowerBound" not in r:
        return None
    return r


def _bound_value(b):
    return None if b["Type"] == 2 else b["Value"]


def _same_bound(a, b):
    return a["Type"] == b["Type"] and (a["Type"] == 2 or a["Value"] == b["Value"])


def _frames(ch):
    out = []
    for t in ch.get("Times") or []:
        out.append(t["Value"] if isinstance(t, dict) else t)
    return out


def _key_value(v):
    return v["Value"] if isinstance(v, dict) and "Value" in v else v


def _value_obj(v):
    from .diff import _text
    return OrderedDict([("text", _text(v) if not isinstance(v, (int, float)) else _num_text(v)), ("value", v)])


def _num_text(v):
    if isinstance(v, bool):
        return "True" if v else "False"
    if isinstance(v, float):
        return ("%.6f" % v).rstrip("0").rstrip(".") if v != int(v) else "%d.000000" % int(v)
    return str(v)


def _diff_keys(oc, nc, ctx):
    from .diff import diff_members, equal
    of, nf = _frames(oc or {}), _frames(nc or {})
    ovals, nvals = (oc or {}).get("Values") or [], (nc or {}).get("Values") or []
    by_frame = OrderedDict()
    for i, f in enumerate(of[:len(ovals)]):
        by_frame.setdefault(f, []).append(i)
    used = {}
    keys = []
    saved = ctx.structural
    ctx.structural = KEY_STRUCTURAL
    try:
        for i, f in enumerate(nf[:len(nvals)]):
            nxt = used.get(f, 0)
            olds = by_frame.get(f)
            k = OrderedDict([("frame", f), ("kind", "added"), ("old", None), ("new", None), ("properties", [])])
            nv = nvals[i]
            if olds and nxt < len(olds):
                used[f] = nxt + 1
                ov = ovals[olds[nxt]]
                rows = []
                if isinstance(ov, dict) and isinstance(nv, dict):
                    diff_members(ov, nv, "", ctx, rows)
                if equal(_key_value(ov), _key_value(nv), None, ctx) and not rows:
                    continue
                k["kind"] = "changed"
                k["old"] = _value_obj(_key_value(ov))
                k["properties"] = rows
            k["new"] = _value_obj(_key_value(nv))
            keys.append(k)
        for f, idxs in by_frame.items():
            for n in range(used.get(f, 0), len(idxs)):
                keys.append(OrderedDict([("frame", f), ("kind", "removed"), ("old", _value_obj(_key_value(ovals[idxs[n]]))),
                                         ("new", None), ("properties", [])]))
    finally:
        ctx.structural = saved
    keys.sort(key=lambda k: k["frame"])
    return keys


def _diff_channel(name, otag, ntag, ctx):
    from .diff import diff_members
    oc = otag.get("value") if otag else None
    nc = ntag.get("value") if ntag else None
    if (otag and not isinstance(oc, dict)) or (ntag and not isinstance(nc, dict)):
        return None                       # 해석 못 한 채널 — 커버리지에 잡혀 있다
    c = OrderedDict([("name", name), ("type", (ntag or otag)["_node"].params[0].name), ("kind", "changed"), ("properties", []), ("keys", [])])
    if oc is not None and nc is not None:
        saved = ctx.structural
        ctx.structural = CHANNEL_STRUCTURAL
        rows = []
        diff_members(oc, nc, "", ctx, rows)
        ctx.structural = saved
        c["properties"] = rows
    c["keys"] = _diff_keys(oc, nc, ctx)
    return c if (c["properties"] or c["keys"]) else None


def _diff_section(index, ose, nse, ctx):
    from .diff import diff_props
    s = OrderedDict([("index", index), ("kind", "changed")])
    op, np_ = _props_of(ose), _props_of(nse)
    orr, nrr = _range(op), _range(np_)
    if orr and nrr and not (_same_bound(orr["LowerBound"], nrr["LowerBound"]) and _same_bound(orr["UpperBound"], nrr["UpperBound"])):
        s["range"] = OrderedDict([("old", [_bound_value(orr["LowerBound"]), _bound_value(orr["UpperBound"])]),
                                  ("new", [_bound_value(nrr["LowerBound"]), _bound_value(nrr["UpperBound"])])])
    och, nch = _channels(ose), _channels(nse)
    structural = set(SECTION_STRUCTURAL) | {t["name"] for t in och.values()} | {t["name"] for t in nch.values()}
    s["properties"] = diff_props(op, np_, ctx, structural, class_changed=(ose["class"] != nse["class"]))
    s["channels"] = []
    if ose["class"] == nse["class"]:
        for name in OrderedDict.fromkeys(list(nch.keys()) + list(och.keys())):
            c = _diff_channel(name, och.get(name), nch.get(name), ctx)
            if c:
                s["channels"].append(c)
    return s if ("range" in s or s["properties"] or s["channels"]) else None


def diff_animations(old, new, ctx):
    ao, an = _Anims(old), _Anims(new)
    out = []
    for name, ne in an.anims.items():
        oe = ao.anims.get(name)
        a = OrderedDict([("id", name), ("kind", "added"), ("area", "anim"), ("tracks", [])])
        ntracks = an.tracks(ne)
        otracks = ao.tracks(oe) if oe else []
        old_by_id = OrderedDict(otracks)
        for tid, te in ntracks:
            nsecs = an.sections(te)
            t = OrderedDict([("id", tid), ("kind", "added"), ("sectionCount", OrderedDict([("old", None), ("new", len(nsecs))])), ("sections", [])])
            ote = old_by_id.get(tid)
            if ote is None:
                a["tracks"].append(t)
                continue
            osecs = ao.sections(ote)
            t["kind"] = "changed"
            t["sectionCount"]["old"] = len(osecs)
            common = min(len(osecs), len(nsecs))
            for i in range(common):
                if osecs[i] is not None and nsecs[i] is not None:
                    s = _diff_section(i, osecs[i], nsecs[i], ctx)
                    if s:
                        t["sections"].append(s)
            for i in range(common, len(osecs)):
                t["sections"].append(OrderedDict([("index", i), ("kind", "removed"), ("properties", []), ("channels", [])]))
            for i in range(common, len(nsecs)):
                t["sections"].append(OrderedDict([("index", i), ("kind", "added"), ("properties", []), ("channels", [])]))
            if t["sections"]:
                a["tracks"].append(t)
        new_ids = {tid for tid, _ in ntracks}
        for tid, te in otracks:
            if tid not in new_ids:
                a["tracks"].append(OrderedDict([("id", tid), ("kind", "removed"),
                                                ("sectionCount", OrderedDict([("old", len(ao.sections(te))), ("new", None)])), ("sections", [])]))
        if oe is not None:
            if not a["tracks"]:
                continue
            a["kind"] = "changed"
        out.append(a)
    for name in ao.anims:
        if name not in an.anims:
            out.append(OrderedDict([("id", name), ("kind", "removed"), ("area", "anim"), ("tracks", [])]))
    return out
