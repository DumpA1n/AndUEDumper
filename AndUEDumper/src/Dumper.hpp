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
    IGameProfile const *_profile;
    std::string _lastError;
    std::function<void(bool)> _dumpExeInfoNotify;
    std::function<void(bool)> _dumpNamesInfoNotify;
    std::function<void(bool)> _dumpObjectsInfoNotify;
    std::function<void(bool)> _dumpOffsetsInfoNotify;
    ProgressCallback _objectsProgressCallback;
    ProgressCallback _dumpProgressCallback;

    // ProcessEvent vtable index found by DumpOffsetsInfo, baked into emitted SDK.
    int _processEventIndex = 0;

    // cached output of BuildProcessedPackages (cleared per Dump())
    std::vector<UE_UPackage> _sdkProcessed;
    std::unordered_map<std::string, size_t> _sdkNameToPkg;
    // name -> underlying type, for cross-pkg enum forward decls
    std::unordered_map<std::string, std::string> _sdkEnumUnderlying;
    std::vector<size_t> _sdkPkgOrder;
    std::string _sdkPackagesUnsaved;

public:
    UEDumper() : _profile(nullptr), _dumpExeInfoNotify(nullptr), _dumpNamesInfoNotify(nullptr), _dumpObjectsInfoNotify(nullptr), _objectsProgressCallback(nullptr), _dumpProgressCallback(nullptr) {}

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

    void BuildProcessedPackages(BufferFmt &logsBufferFmt, UEPackagesArray &packages, const ProgressCallback &progressCallback);

    // Inject empty Struct entries for non-UObject reflection metadata types
    // (FFieldClass, FField, FProperty, the FProperty subclasses the dumped structs use
    // and the container layouts) into the CoreUObject package. Members stay empty here;
    // augment() fills them via fieldsFor(). Runs after dropDups but before augment.
    // Returns the allocation size the engine constructs each property subclass with,
    // for the subclasses whose size could be decoded.
    std::unordered_map<std::string, uint32_t> SynthesizeReflectionTypes(BufferFmt &logsBufferFmt, const UEPackagesArray &packages);

    void DumpAIOHeader(BufferFmt &logsBufferFmt, BufferFmt &aioBufferFmt);

    // Per-package SDK/<pkg>.hpp + SDK/SDK.hpp aggregator.
    void DumpSDK_PerPackage(BufferFmt &logsBufferFmt, std::unordered_map<std::string, BufferFmt> &outBuffersMap);

};
