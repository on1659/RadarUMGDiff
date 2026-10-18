"""FSoftObjectPath (dev 문서 §11.8). 5.8 Runtime/CoreUObject/Private/UObject/SoftObjectPath.cpp SerializePathWithoutFixup.

- 전체 레이아웃(요약의 목록, 또는 목록이 없는 패키지의 속성 값):
    >= 1007: FTopLevelAssetPath{FName PackageName, FName AssetName} + 서브 경로
      서브 경로: FFortniteMainBranchObjectVersion < SoftObjectPathUtf8SubPaths(192) 면 FString 길이 규칙
                 (SoftObjectPathLoadSubPathWorkaround: 양수=바이트, 음수=UTF-16 코드 유닛), 이상이면 FUtf8String.
- 속성 값 안: 요약의 SoftObjectPathsCount > 0 이면 i32 목록 인덱스 하나 (FLinkerLoad::operator<<(FSoftObjectPath&)).
"""

from .reader import ParseError, Refuse
from . import summary as S

FORTNITE_MAIN_GUID = "601D1886AC644F84AA16D3DE0DEAC7D6"
SOFT_OBJECT_PATH_UTF8_SUBPATHS = 192    # FortniteMainBranchObjectVersions.inl:590 (5.8)


def _subpath(r, summ):
    fm = summ["customVersions"].get(FORTNITE_MAIN_GUID, 0)
    if fm >= SOFT_OBJECT_PATH_UTF8_SUBPATHS:
        return r.utf8string()
    n = r.i32()
    if n == 0:
        return ""
    if n > 0:
        raw = r.bytes(n)
        raw = raw.rstrip(b"\x00")
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError:
            return raw.decode("latin-1")
    raw = r.bytes(-n * 2)
    s = raw.decode("utf-16-le", errors="surrogatepass")
    return s.rstrip("\x00")


def format_path(pkg, asset, sub):
    if pkg in ("None", "") and asset in ("None", ""):
        return ""
    path = pkg if asset in ("None", "") else "%s.%s" % (pkg, asset)
    if sub:
        path += ":" + sub
    return path


def read_soft_object_path_full(r, summ):
    if summ["fileVersionUE5"] < S.UE5_FSOFTOBJECTPATH_REMOVE_ASSET_PATH_FNAMES:
        raise Refuse("FileVersionUE5 < 1007 의 FSoftObjectPath 는 지원하지 않습니다")
    pkg = r.fname()
    asset = r.fname()
    sub = _subpath(r, summ)
    return format_path(pkg, asset, sub)


def read_soft_object_path_value(r, pkg):
    """속성 값 안의 FSoftObjectPath. pkg = tables.Package."""
    if pkg.summary.get("softObjectPathsCount", 0) > 0:
        idx = r.i32()
        if idx < 0 or idx >= len(pkg.soft_paths):
            raise ParseError("소프트 경로 목록 인덱스 범위 밖: %d" % idx)
        return pkg.soft_paths[idx]
    return read_soft_object_path_full(r, pkg.summary)
