"""Check the candidate compile graph and archive against admitted subsystems."""
import json
from pathlib import Path
import subprocess
import sys


def main():
    directory = Path(sys.argv[1]) if len(sys.argv) == 2 else Path("build/candidate")
    commands = json.loads((directory / "compile_commands.json").read_text())
    rejected = []
    source_root = Path(__file__).resolve().parents[1]
    for header in ("SkLRUCache.h", "SkTDynamicHash.h", "SkTMultiMap.h", "SkPtrRecorder.h", "SkTSearch.h"):
        if (source_root / "skia/src/core" / header).exists():
            rejected.append("Unused CPU helper: " + header)
    if (source_root / "skia/src/utils/SkBitSet.h").exists():
        rejected.append("Unused CPU helper: SkBitSet.h")
    templates = (source_root / "skia/include/private/SkTemplates.h").read_text()
    for name in ("SkFunctionObject", "SkOverloadedFunctionObject", "SkAutoTCallVProc",
                 "SkMakeArray", "SkTAfter"):
        if name in templates:
            rejected.append("Unused generic adapter: " + name)
    forbidden_paths = ["/src/gpu/", "/src/codec/", "/src/encode/", "/src/android/",
                       "SkImageGeneratorCG.cpp", "SkFontMgr_mac", "SkTypeface_mac",
                       "SkTypeface_FreeType", "SkOpts_ml3", "SkOpts_ml4",
                       "skcms_Transform",
                       "SkExecutor.cpp", "SkTaskGroup.cpp", "SkSharedMutex.cpp",
                       "SkPtrRecorder.cpp", "SkTSearch.cpp", "SkOSFile_",
                       "SkGlobalInitialization_core.cpp", "SkGlobalInitialization_default.cpp"]
    for command in commands:
        name = command["file"]
        for pattern in forbidden_paths:
            if pattern in name:
                rejected.append(name)
    symbols = subprocess.check_output(["llvm-nm", "--defined-only", "-C",
                                      str(directory / "libstx_skia.a")], text=True)
    for pattern in ["SkCodec::", "SkImageGeneratorCG::", "SkPngCodec::",
                    "SkFontMgr_New_CoreText", "SkTypeface_FreeType::",
                    "SkExecutor::", "SkTaskGroup::", "SkThreadPool<", "SkSharedMutex::",
                    "SkPtrSet::", "SkRefCntSet::", "SkNamedFactorySet::", "SkStrSearch(",
                    "SkStrLCSearch(", "SkAutoAsciiToLC::", "SkBinaryWriteBuffer::setFactoryRecorder",
                    "SkFlattenable::Register", "SkFlattenable::NameToFactory",
                    "SkFlattenable::FactoryToName", "SkFlattenable::Finalize",
                    "SkFlattenable::PrivateInitializer", "SkRegister",
                    "SkReadBuffer::setFactoryPlayback", "SkBinaryWriteBuffer::writeStream",
                    "skcms_Transform", "skcms_Parse", "skcms_ApproximateCurve"]:
        if pattern in symbols:
            rejected.append(pattern)
    if rejected:
        raise RuntimeError("Outside compiled scope: " + repr(rejected))
    print("CPU pixel-only build scope: PASS")


if __name__ == "__main__":
    main()
