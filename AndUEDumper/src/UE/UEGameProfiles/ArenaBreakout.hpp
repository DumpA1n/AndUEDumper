#pragma once

#include "../UEGameProfile.hpp"
using namespace UEMemory;

class ArenaBreakoutProfile : public IGameProfile
{
public:
    ArenaBreakoutProfile() = default;

    bool ArchSupprted() const override
    {
        auto e_machine = GetUnrealELF().header().e_machine;
        // only arm64
        return e_machine == EM_AARCH64;
    }

    std::string GetAppName() const override
    {
        return "Arena Breakout";
    }

    std::vector<std::string> GetAppIDs() const override
    {
        return {"com.proximabeta.mf.uamo"};
    }

    bool isUsingCasePreservingName() const override
    {
        return false;
    }

    bool IsUsingFNamePool() const override
    {
        return true;
    }

    bool isUsingOutlineNumberName() const override
    {
        return false;
    }

    uintptr_t GetGUObjectArrayPtr() const override
    {
        std::vector<std::pair<std::string, int>> idaPatterns = {
            {"91 E1 03 ? AA E0 03 08 AA E2 03 1F 2A", -7},
            {"B4 21 0C 40 B9 ? ? ? ? ? ? ? 91", 5},
            {"9F E5 00 ? 00 E3 FF ? 40 E3 ? ? A0 E1", -2},
            {"96 df 02 17 ? ? ? ? 54 ? ? ? ? ? ? ? 91 e1 03 13 aa", 9},
            {"f4 03 01 2a ? 00 00 34 ? ? ? ? ? ? ? ? ? ? 00 54 ? 00 00 14 ? ? ? ? ? ? ? 91", 0x18},
            {"69 3e 40 b9 1f 01 09 6b ? ? ? 54 e1 03 13 aa ? ? ? ? f4 4f ? a9 ? ? ? ? ? ? ? 91", 0x18},
        };

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        for (const auto &it : idaPatterns)
        {
            std::string ida_pattern = it.first;
            const int step = it.second;

            uintptr_t adrl = Arm64::DecodeADRL(findIdaPattern(map_type, ida_pattern, step));
            if (adrl != 0) return adrl;
        }

        return 0;
    }

    uintptr_t GetNamesPtr() const override
    {
        std::string pattern = "F4 4F 01 A9 FD 7B 02 A9 FD 83 00 91 ? ? ? ? A8 02 ? 39";

        PATTERN_MAP_TYPE map_type = isEmulator() ? PATTERN_MAP_TYPE::ANY_R : PATTERN_MAP_TYPE::ANY_X;

        uintptr_t find = findIdaPattern(map_type, pattern, 0);
        if (find != 0)
        {
            bool skippedFirst = false;
            intptr_t adrp_adr = 0;
            for (int i = 0; i < 8; i++)
            {
                uint32_t insn = vm_rpm_ptr<uint32_t>((void*)(find + (i * 4)));
                if (KittyArm64::decodeInsnType(insn) != EKittyInsnTypeArm64::ADRP)
                    continue;

                if (!skippedFirst)
                {
                    skippedFirst = true;
                    continue;
                }

                adrp_adr = find + (i * 4);
                break;
            }

            if (adrp_adr != 0)
            {
                // 0 so the it scans for next imm instruction offset
                return Arm64::DecodeADRL(adrp_adr, 0);
            }
        }

        return 0;
    }

    UE_Offsets *GetOffsets() const override
    {
        static UE_Offsets offsets = UE_DefaultOffsets::UE4_25_27(isUsingCasePreservingName());

        static bool once = false;
        if (!once)
        {
            once = true;
            offsets.FNamePool.BlocksOff += sizeof(void *);

            // https://github.com/MJx0/AndUEDumper/issues/42
            offsets.UStruct.SuperStruct += sizeof(void *);
            offsets.UStruct.Children += sizeof(void *);
            offsets.UStruct.ChildProperties += sizeof(void *);
            offsets.UStruct.PropertiesSize += sizeof(void *);
            offsets.UStruct.MinAlignment += sizeof(void *);

            offsets.UField.Next += sizeof(void *);
            offsets.UEnum.Names += sizeof(void *);

            offsets.UFunction.EFunctionFlags += sizeof(void *);
            offsets.UFunction.NumParams += sizeof(void *);
            offsets.UFunction.ParamSize += sizeof(void *);
            offsets.UFunction.Func += sizeof(void *);
        }

        return &offsets;
    }
};
