#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "UE/UEGameProfile.hpp"
#include "UE/UEWrappers.hpp"

#include "Utils/BufferFmt.hpp"
#include "Utils/ProgressUtils.hpp"

using ProgressCallback = std::function<void(const SimpleProgressBar &)>;
using UEPackagesArray = std::vector<std::pair<uint8_t *const, std::vector<UE_UObject>>>;

#include "UPackageGenerator.hpp"

class UEDumper
{
public:
    // SDK output style. AIOHeader.hpp is always emitted; these flags gate SDK_A/.
    enum class SDKMode : uint8_t {
        Both    = 0, // emit SDK_A/
        OnlyA   = 1, // emit SDK_A/
        OnlyB   = 2, // legacy, treated as OnlyA
        None    = 3, // skip SDK_A/, AIOHeader.hpp only
    };

private:
    IGameProfile const *_profile;
    std::string _lastError;
    std::function<void(bool)> _dumpExeInfoNotify;
    std::function<void(bool)> _dumpNamesInfoNotify;
    std::function<void(bool)> _dumpObjectsInfoNotify;
    std::function<void(bool)> _dumpOffsetsInfoNotify;
    ProgressCallback _objectsProgressCallback;
    ProgressCallback _dumpProgressCallback;

    // per-game ProcessEvent vtable slot, baked into emitted SDK
    int _processEventIndex = 0;

    SDKMode _sdkMode = SDKMode::Both;

    // cached output of BuildProcessedPackages (cleared per Dump())
    std::vector<UE_UPackage> _sdkProcessed;
    std::unordered_map<std::string, size_t> _sdkNameToPkg;
    // name -> underlying type, for cross-pkg enum forward decls
    std::unordered_map<std::string, std::string> _sdkEnumUnderlying;
    std::vector<size_t> _sdkPkgOrder;
    std::string _sdkPackagesUnsaved;

public:
    UEDumper() : _profile(nullptr), _dumpExeInfoNotify(nullptr), _dumpNamesInfoNotify(nullptr), _dumpObjectsInfoNotify(nullptr), _objectsProgressCallback(nullptr), _dumpProgressCallback(nullptr) {}

    void SetSDKMode(SDKMode m) { _sdkMode = m; }
    SDKMode GetSDKMode() const { return _sdkMode; }

    bool Init(IGameProfile *profile);

    bool Dump(std::unordered_map<std::string, BufferFmt> *outBuffersMap);

    const IGameProfile *GetProfile() const { return _profile; }

    std::string GetLastError() const { return _lastError; }

    inline void setDumpExeInfoNotify(const std::function<void(bool)> &f) { _dumpExeInfoNotify = f; }
    inline void setDumpNamesInfoNotify(const std::function<void(bool)> &f) { _dumpNamesInfoNotify = f; }
    inline void setDumpObjectsInfoNotify(const std::function<void(bool)> &f) { _dumpObjectsInfoNotify = f; }
    inline void setDumpOffsetsInfoNotify(const std::function<void(bool)> &f) { _dumpOffsetsInfoNotify = f; }

    inline void setObjectsProgressCallback(const ProgressCallback &f) { _objectsProgressCallback = f; }
    inline void setDumpProgressCallback(const ProgressCallback &f) { _dumpProgressCallback = f; }

private:
    void DumpExecutableInfo(BufferFmt &logsBufferFmt);

    void DumpNamesInfo(BufferFmt &logsBufferFmt);

    void DumpObjectsInfo(BufferFmt &logsBufferFmt);

    void DumpOffsetsInfo(BufferFmt &logsBufferFmt, BufferFmt &offsetsBufferFmt);

    void GatherUObjects(BufferFmt &logsBufferFmt, BufferFmt &objsBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback);

    void BuildProcessedPackages(UEPackagesArray &packages, const ProgressCallback &progressCallback);

    // Inject empty Struct entries for non-UObject reflection metadata types
    // (FField / FFieldClass / FProperty + 13 FProperty subclasses) into CoreUObject
    // package. Members stay empty here; augment() fills them via fieldsFor().
    // Runs after dropDups but before augment.
    void SynthesizeReflectionTypes();

    void DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt);

    // Per-package SDK_A/<pkg>.hpp + SDK_A/SDK.hpp aggregator.
    void DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap);

};
