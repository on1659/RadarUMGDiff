"""FPackageFileSummary (dev 문서 §11.3). 5.8 Runtime/CoreUObject/Private/UObject/PackageFileSummary.cpp operator<< 순서.
5.1(1008)은 같은 순서에서 게이트 밖 필드가 빠진다."""

from .reader import Reader, ParseError, Refuse

PACKAGE_FILE_TAG = 0x9E2A83C1
PKG_FILTER_EDITOR_ONLY = 0x80000000

# EUnrealEngineObjectUE5Version (5.8 Runtime/Core/Public/UObject/ObjectVersion.h)
UE5_NAMES_REFERENCED_FROM_EXPORT_DATA = 1001
UE5_PAYLOAD_TOC = 1002
UE5_OPTIONAL_RESOURCES = 1003
UE5_LARGE_WORLD_COORDINATES = 1004
UE5_REMOVE_OBJECT_EXPORT_PACKAGE_GUID = 1005
UE5_TRACK_OBJECT_EXPORT_IS_INHERITED = 1006
UE5_FSOFTOBJECTPATH_REMOVE_ASSET_PATH_FNAMES = 1007
UE5_ADD_SOFTOBJECTPATH_LIST = 1008
UE5_DATA_RESOURCES = 1009
UE5_SCRIPT_SERIALIZATION_OFFSET = 1010
UE5_PROPERTY_TAG_EXTENSION_AND_OVERRIDABLE_SERIALIZATION = 1011
UE5_PROPERTY_TAG_COMPLETE_TYPE_NAME = 1012
UE5_METADATA_SERIALIZATION_OFFSET = 1014
UE5_VERSE_CELLS = 1015
UE5_PACKAGE_SAVED_HASH = 1016
UE5_IMPORT_TYPE_HIERARCHIES = 1018

# 이 분석기가 읽는 FileVersionUE5 범위 (dev 문서 §11.2). 1012 미만은 struct_kinds 표가 있어야 한다.
SUPPORTED_UE5_MIN = 1004
SUPPORTED_UE5_MAX = 1018


def _engine_version(r):
    major, minor, patch = r.u16(), r.u16(), r.u16()
    changelist = r.u32()
    branch = r.fstring()
    return {"major": major, "minor": minor, "patch": patch, "changelist": changelist, "branch": branch,
            "text": "%d.%d.%d-%d+%s" % (major, minor, patch, changelist, branch)}


def read_summary(data):
    """bytes → dict. 지원 범위 밖이면 Refuse."""
    r = Reader(data)
    s = {}
    tag = r.u32()
    if tag != PACKAGE_FILE_TAG:
        raise Refuse("패키지 태그가 아님: 0x%08X" % tag)
    legacy = r.i32()
    s["legacyFileVersion"] = legacy
    if legacy >= 0 or legacy < -9:
        raise Refuse("지원하지 않는 LegacyFileVersion %d" % legacy)
    if legacy != -4:
        s["legacyUE3Version"] = r.i32()
    s["fileVersionUE4"] = r.i32()
    s["fileVersionUE5"] = r.i32() if legacy <= -8 else 0
    s["licenseeVersion"] = r.i32()
    ue5 = s["fileVersionUE5"]
    if s["fileVersionUE4"] == 0 and ue5 == 0 and s["licenseeVersion"] == 0:
        raise Refuse("버전 없는(쿠킹) 패키지는 지원하지 않습니다")
    if ue5 < SUPPORTED_UE5_MIN or ue5 > SUPPORTED_UE5_MAX:
        raise Refuse("지원 범위 밖 FileVersionUE5 %d (지원 %d~%d)" % (ue5, SUPPORTED_UE5_MIN, SUPPORTED_UE5_MAX))

    if ue5 >= UE5_PACKAGE_SAVED_HASH:
        s["savedHash"] = r.bytes(20).hex().upper()
        s["totalHeaderSize"] = r.i32()
    # 커스텀 버전: LegacyFileVersion <= -2, -9/-8 은 Optimized 형식 = i32 수 + {FGuid, i32}×N
    cv = {}
    if legacy <= -2:
        if legacy > -6:
            raise Refuse("옛 커스텀 버전 형식(LegacyFileVersion %d)은 지원하지 않습니다" % legacy)
        n = r.i32()
        for _ in range(n):
            g = r.fguid()
            cv[g] = r.i32()
    s["customVersions"] = cv
    if ue5 < UE5_PACKAGE_SAVED_HASH:
        s["totalHeaderSize"] = r.i32()
    s["packageName"] = r.fstring()
    s["packageFlags"] = r.u32()
    if s["packageFlags"] & PKG_FILTER_EDITOR_ONLY:
        raise Refuse("에디터 데이터가 걸러진(쿠킹) 패키지는 지원하지 않습니다")
    s["nameCount"], s["nameOffset"] = r.i32(), r.i32()
    if ue5 >= UE5_ADD_SOFTOBJECTPATH_LIST:
        s["softObjectPathsCount"], s["softObjectPathsOffset"] = r.i32(), r.i32()
    else:
        s["softObjectPathsCount"], s["softObjectPathsOffset"] = 0, 0
    s["localizationId"] = r.fstring()                 # VER_UE4_ADDED_PACKAGE_SUMMARY_LOCALIZATION_ID < 522
    s["gatherableTextDataCount"], s["gatherableTextDataOffset"] = r.i32(), r.i32()
    s["exportCount"], s["exportOffset"] = r.i32(), r.i32()
    s["importCount"], s["importOffset"] = r.i32(), r.i32()
    if ue5 >= UE5_VERSE_CELLS:
        s["cellExportCount"], s["cellExportOffset"] = r.i32(), r.i32()
        s["cellImportCount"], s["cellImportOffset"] = r.i32(), r.i32()
    if ue5 >= UE5_METADATA_SERIALIZATION_OFFSET:
        s["metaDataOffset"] = r.i32()
    s["dependsOffset"] = r.i32()
    s["softPackageReferencesCount"], s["softPackageReferencesOffset"] = r.i32(), r.i32()
    s["searchableNamesOffset"] = r.i32()
    s["thumbnailTableOffset"] = r.i32()
    if ue5 >= UE5_IMPORT_TYPE_HIERARCHIES:
        s["importTypeHierarchiesCount"], s["importTypeHierarchiesOffset"] = r.i32(), r.i32()
    if ue5 < UE5_PACKAGE_SAVED_HASH:
        s["guid"] = r.fguid()
    s["persistentGuid"] = r.fguid()                   # VER_UE4_ADDED_PACKAGE_OWNER, 에디터 데이터 포함
    gens = r.i32()
    if gens < 0 or gens > 100000:
        raise ParseError("GenerationCount 이상: %d" % gens)
    s["generations"] = [{"exportCount": r.i32(), "nameCount": r.i32()} for _ in range(gens)]
    s["savedByEngineVersion"] = _engine_version(r)
    s["compatibleWithEngineVersion"] = _engine_version(r)
    s["compressionFlags"] = r.u32()
    chunks = r.i32()
    if chunks != 0:
        raise Refuse("압축 청크가 있는 패키지는 지원하지 않습니다")
    s["packageSource"] = r.u32()
    n = r.i32()
    s["additionalPackagesToCook"] = [r.fstring() for _ in range(n)]
    if legacy > -7:
        s["numTextureAllocations"] = r.i32()
    s["assetRegistryDataOffset"] = r.i32()
    s["bulkDataStartOffset"] = r.i64()
    s["worldTileInfoDataOffset"] = r.i32()
    n = r.i32()
    s["chunkIds"] = [r.i32() for _ in range(n)]
    s["preloadDependencyCount"], s["preloadDependencyOffset"] = r.i32(), r.i32()
    s["namesReferencedFromExportDataCount"] = r.i32()
    s["payloadTocOffset"] = r.i64()
    if ue5 >= UE5_DATA_RESOURCES:
        s["dataResourceOffset"] = r.i32()
    s["summaryEnd"] = r.pos
    return s
