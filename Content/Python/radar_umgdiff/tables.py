"""이름표 · 소프트 경로 목록 · 임포트 · 익스포트 (dev 문서 §11.4). 5.8 ObjectResource.cpp operator<<."""

from .reader import Reader, ParseError
from . import summary as S


def read_names(data, summ):
    r = Reader(data, summ["nameOffset"])
    names = []
    for _ in range(summ["nameCount"]):
        names.append(r.fstring())
        r.u16()   # NonCasePreservingHash
        r.u16()   # CasePreservingHash
    return names, r.pos


def read_soft_object_paths(data, summ, names):
    """요약의 소프트 경로 목록. 각 항목은 전체 레이아웃(FTopLevelAssetPath + 서브 경로)."""
    from .softpath import read_soft_object_path_full
    count = summ.get("softObjectPathsCount", 0)
    if count <= 0:
        return [], None
    r = Reader(data, summ["softObjectPathsOffset"], names=names)
    paths = [read_soft_object_path_full(r, summ) for _ in range(count)]
    return paths, r.pos


def read_imports(data, summ, names):
    ue5 = summ["fileVersionUE5"]
    r = Reader(data, summ["importOffset"], names=names)
    out = []
    for i in range(summ["importCount"]):
        imp = {"index": -(i + 1)}
        imp["classPackage"] = r.fname()
        imp["className"] = r.fname()
        imp["outer"] = r.i32()
        imp["objectName"] = r.fname()
        imp["packageName"] = r.fname()                # VER_UE4_NON_OUTER_PACKAGE_IMPORT, 에디터 데이터 포함
        imp["optional"] = r.bool32() if ue5 >= S.UE5_OPTIONAL_RESOURCES else False
        out.append(imp)
    return out, r.pos


def read_exports(data, summ, names):
    ue5 = summ["fileVersionUE5"]
    r = Reader(data, summ["exportOffset"], names=names)
    out = []
    for i in range(summ["exportCount"]):
        e = {"index": i + 1}
        e["class"] = r.i32()
        e["super"] = r.i32()
        e["template"] = r.i32()
        e["outer"] = r.i32()
        e["objectName"] = r.fname()
        e["objectFlags"] = r.u32()
        e["serialSize"] = r.i64()
        e["serialOffset"] = r.i64()
        e["forcedExport"] = r.bool32()
        e["notForClient"] = r.bool32()
        e["notForServer"] = r.bool32()
        if ue5 < S.UE5_REMOVE_OBJECT_EXPORT_PACKAGE_GUID:
            e["packageGuid"] = r.fguid()
        e["isInheritedInstance"] = r.bool32() if ue5 >= S.UE5_TRACK_OBJECT_EXPORT_IS_INHERITED else False
        e["packageFlags"] = r.u32()
        e["notAlwaysLoadedForEditorGame"] = r.bool32()
        e["isAsset"] = r.bool32()
        e["generatePublicHash"] = r.bool32() if ue5 >= S.UE5_OPTIONAL_RESOURCES else False
        e["firstExportDependency"] = r.i32()
        e["deps"] = [r.i32() for _ in range(4)]
        if ue5 >= S.UE5_SCRIPT_SERIALIZATION_OFFSET:
            e["scriptStart"] = r.i64()
            e["scriptEnd"] = r.i64()
        else:
            e["scriptStart"] = None
            e["scriptEnd"] = None
        out.append(e)
    return out, r.pos


class Package:
    """한 .uasset 의 헤더 전체. package_name = 이 파일이 원래 놓이는 패키지 경로(/Game/UI/WBP_HUD) — 오브젝트 경로를 만들 때 쓴다."""

    def __init__(self, data, path="", package_name=None):
        self.data = data
        self.path = path
        self.summary = S.read_summary(data)
        self.names, self.names_end = read_names(data, self.summary)
        self.imports, self.imports_end = read_imports(data, self.summary, self.names)
        self.exports, self.exports_end = read_exports(data, self.summary, self.names)
        self.soft_paths, self.soft_paths_end = read_soft_object_paths(data, self.summary, self.names)
        self.package_name = package_name or self._infer_package_name()

    def _infer_package_name(self):
        """파일만으로 원래 패키지 경로를 정한다: Content/ 아래 상대 경로 → 소프트 경로 목록의 자기 애셋 항목 → /Unknown/<애셋>."""
        p = self.path.replace("\\", "/")
        if "/Content/" in "/" + p:
            rel = ("/" + p).split("/Content/", 1)[1]
            if rel.endswith(".uasset"):
                return "/Game/" + rel[:-len(".uasset")]
        asset = next((e["objectName"] for e in self.exports if e["isAsset"]), None)
        for sp in self.soft_paths:
            head = sp.split(":", 1)[0]
            if "." in head and head.rsplit(".", 1)[1] == asset:
                return head.rsplit(".", 1)[0]
        return "/Unknown/" + (asset or "Asset")
    @property
    def ue5(self):
        return self.summary["fileVersionUE5"]

    def reader(self, pos=0, end=None):
        return Reader(self.data, pos, end, self.names)

    # ── 패키지 인덱스 ──
    def resource(self, index):
        if index > 0:
            return self.exports[index - 1]
        if index < 0:
            return self.imports[-index - 1]
        return None

    def object_name(self, index):
        res = self.resource(index)
        return None if res is None else res["objectName"]

    def full_path(self, index):
        """패키지 인덱스 → /Script/UMG.Border 같은 경로 (임포트), 또는 익스포트의 Outer 체인 경로."""
        if index == 0:
            return None
        parts = []
        i = index
        while i != 0:
            res = self.resource(i)
            parts.append(res["objectName"])
            i = res["outer"]
        parts.reverse()
        if index < 0:
            # 임포트: 첫 토막이 패키지 이름
            pkg = parts[0]
            rest = parts[1:]
            if not rest:
                return pkg
            return pkg + "." + ":".join(rest) if len(rest) > 1 else pkg + "." + rest[0]
        return ".".join(parts)

    def object_path(self, index):
        """패키지 인덱스 → UObject::GetPathName 과 같은 전체 경로. 0 이면 None.
        구분자: 첫 오브젝트 앞은 '.', 패키지 바로 아래 오브젝트의 하위 오브젝트 앞은 ':', 그 아래는 '.'."""
        if index == 0:
            return None
        chain = []
        i = index
        guard = 0
        while i != 0:
            res = self.resource(i)
            chain.append(res["objectName"])
            i = res["outer"]
            guard += 1
            if guard > 256:
                raise ParseError("Outer 체인이 끝나지 않음: %d" % index)
        chain.reverse()
        if index > 0:
            pkg, objs = self.package_name, chain
        else:
            pkg, objs = chain[0], chain[1:]
        out = pkg
        for k, name in enumerate(objs):
            out += (":" if k == 1 else ".") + name
        return out

    def relative_path(self, index, stop_index):
        """stop_index 오브젝트 기준 상대 경로 (UObject::GetPathName(StopOuter) 와 같다). 위젯 id 에 쓴다."""
        chain = []
        i = index
        while i != 0 and i != stop_index:
            res = self.resource(i)
            chain.append((i, res))
            i = res["outer"]
        if i != stop_index:
            return None
        chain.reverse()
        out = ""
        for k, (idx, res) in enumerate(chain):
            if k > 0:
                outer = chain[k - 1][1]
                sep = ":" if outer["outer"] == 0 and idx > 0 else "."
                out += sep
            out += res["objectName"]
        return out

    def normalize_path(self, path):
        return path

    def class_path(self, export):
        """익스포트의 클래스 경로 (/Script/UMG.Border, 또는 같은 패키지의 BPGC 이름)."""
        ci = export["class"]
        if ci < 0:
            return self.full_path(ci)
        if ci > 0:
            return self.summary["packageName"] + "." + self.exports[ci - 1]["objectName"] if self.summary.get("packageName") not in (None, "None") else self.exports[ci - 1]["objectName"]
        return "/Script/CoreUObject.Class"

    def class_name(self, export):
        ci = export["class"]
        if ci == 0:
            return "Class"
        return self.resource(ci)["objectName"]
