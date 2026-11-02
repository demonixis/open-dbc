#include "system_info.hpp"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_platform.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#define DBC_X86_CPUID 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlwapi.h>
#include <windows.h>
#include <winioctl.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#endif

namespace dbc
{
    namespace
    {
#if defined(DBC_X86_CPUID)
        std::array<std::uint32_t, 4> Cpuid(std::uint32_t Leaf)
        {
            std::array<std::uint32_t, 4> Registers{};
#if defined(_MSC_VER)
            int Values[4] = {};
            __cpuid(Values, static_cast<int>(Leaf));
            for (std::size_t Index = 0; Index < 4; ++Index)
                Registers[Index] = static_cast<std::uint32_t>(Values[Index]);
#else
            __cpuid(Leaf, Registers[0], Registers[1], Registers[2], Registers[3]);
#endif
            return Registers;
        }

        // Each bit the processor reports in register EDX of a CPUID leaf it supports.
        bool CpuidEdxBit(std::uint32_t Leaf, int Bit)
        {
            const std::uint32_t Highest = Cpuid(Leaf & 0x80000000u)[0];
            return Leaf <= Highest && (Cpuid(Leaf)[3] >> Bit & 1u) != 0;
        }
#endif

        std::string Trim(std::string Value)
        {
            const auto First = Value.find_first_not_of(" \t\r\n");
            if (First == std::string::npos)
                return {};
            return Value.substr(First, Value.find_last_not_of(" \t\r\n") - First + 1);
        }

#if defined(__linux__)
        // The value of the first "Key : value" line of /proc/cpuinfo with this key.
        std::string CpuinfoField(const char* Key)
        {
            std::ifstream Input("/proc/cpuinfo");
            std::string Line;
            while (std::getline(Input, Line))
            {
                const auto Colon = Line.find(':');
                if (Colon != std::string::npos && Trim(Line.substr(0, Colon)) == Key)
                    return Trim(Line.substr(Colon + 1));
            }
            return {};
        }

        std::uint64_t FirstNumberIn(const std::string& Path)
        {
            std::ifstream Input(Path);
            std::uint64_t Value = 0;
            Input >> Value;
            return Input ? Value : 0;
        }
#endif

#if defined(__APPLE__)
        std::string SysctlString(const char* Name)
        {
            std::size_t Size = 0;
            if (sysctlbyname(Name, nullptr, &Size, nullptr, 0) != 0 || Size == 0)
                return {};
            std::string Value(Size, '\0');
            if (sysctlbyname(Name, Value.data(), &Size, nullptr, 0) != 0)
                return {};
            return std::string(Value.c_str());
        }

        std::uint64_t SysctlNumber(const char* Name)
        {
            std::uint64_t Value = 0;
            std::size_t Size = sizeof(Value);
            if (sysctlbyname(Name, &Value, &Size, nullptr, 0) != 0)
                return 0;
            // Some entries are 32-bit.
            return Size == sizeof(std::uint32_t) ? static_cast<std::uint32_t>(Value) : Value;
        }
#endif

#if defined(_WIN32)
        std::string ProcessorRegistryString(const char* Name)
        {
            char Buffer[256] = {};
            DWORD Size = sizeof(Buffer);
            if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", Name,
                             RRF_RT_REG_SZ, nullptr, Buffer, &Size) != ERROR_SUCCESS)
                return {};
            return Trim(Buffer);
        }

        DWORD ProcessorRegistryNumber(const char* Name)
        {
            DWORD Value = 0;
            DWORD Size = sizeof(Value);
            if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", Name,
                             RRF_RT_REG_DWORD, nullptr, &Value, &Size) != ERROR_SUCCESS)
                return 0;
            return Value;
        }
#endif

        std::string BrandForVendor(const std::string& Vendor)
        {
            if (Vendor == "GenuineIntel")
                return "Intel";
            if (Vendor == "AuthenticAMD")
                return "AMD";
            if (Vendor == "CentaurHauls")
                return "Centaur";
            if (Vendor == "HygonGenuine")
                return "Hygon";
            return Vendor;
        }

        // A volume serial in the Enhancement Pack's "XXXX-XXXX" form.
        std::string FormatSerial(std::uint32_t Value)
        {
            char Buffer[16] = {};
            std::snprintf(Buffer, sizeof(Buffer), "%04X-%04X", static_cast<unsigned>(Value >> 16),
                          static_cast<unsigned>(Value & 0xFFFFu));
            return Buffer;
        }

        bool IsOpticalFileSystem(const std::string& Type)
        {
            return Type == "cd9660" || Type == "iso9660" || Type == "udf" || Type == "cddafs";
        }

#if defined(__linux__)
        struct MountEntry
        {
            std::string Device;
            std::string Root;
            std::string Type;
        };

        // /proc/self/mounts escapes spaces and other characters as \ooo octal.
        std::string UnescapeMountField(const std::string& Field)
        {
            std::string Result;
            for (std::size_t Index = 0; Index < Field.size(); ++Index)
            {
                if (Field[Index] == '\\' && Index + 3 < Field.size())
                {
                    const std::string Octal = Field.substr(Index + 1, 3);
                    if (Octal.size() == 3 && std::all_of(Octal.begin(), Octal.end(), [](char Character)
                                                         { return Character >= '0' && Character <= '7'; }))
                    {
                        Result.push_back(static_cast<char>(std::stoi(Octal, nullptr, 8)));
                        Index += 3;
                        continue;
                    }
                }
                Result.push_back(Field[Index]);
            }
            return Result;
        }

        std::vector<MountEntry> Mounts()
        {
            std::vector<MountEntry> Result;
            std::ifstream Input("/proc/self/mounts");
            std::string Line;
            while (std::getline(Input, Line))
            {
                std::istringstream Fields(Line);
                MountEntry Entry;
                Fields >> Entry.Device >> Entry.Root >> Entry.Type;
                if (!Fields)
                    continue;
                Entry.Device = UnescapeMountField(Entry.Device);
                Entry.Root = UnescapeMountField(Entry.Root);
                Result.push_back(std::move(Entry));
            }
            return Result;
        }
#endif

#if !defined(_WIN32)
        bool PosixDrive(const std::string& Root, DriveDetails& Output)
        {
            struct statvfs Volume{};
            if (statvfs(Root.c_str(), &Volume) != 0)
                return false;
            const std::uint64_t Unit = Volume.f_frsize != 0 ? Volume.f_frsize : Volume.f_bsize;
            Output.TotalBytes = static_cast<std::uint64_t>(Volume.f_blocks) * Unit;
            Output.FreeBytes = static_cast<std::uint64_t>(Volume.f_bfree) * Unit;
            Output.MaximumNameLength = static_cast<int>(std::min<unsigned long>(Volume.f_namemax, 0x7FFFFFFF));
            Output.Serial = FormatSerial(static_cast<std::uint32_t>(Volume.f_fsid));
            return true;
        }
#endif
    } // namespace

    const ProcessorIdentity& SystemInfo::Processor()
    {
        if (ProcessorRead_)
            return Processor_;
        ProcessorRead_ = true;
#if defined(DBC_X86_CPUID)
        const auto Leaf0 = Cpuid(0);
        char Vendor[13] = {};
        std::memcpy(Vendor, &Leaf0[1], 4);
        std::memcpy(Vendor + 4, &Leaf0[3], 4);
        std::memcpy(Vendor + 8, &Leaf0[2], 4);
        Processor_.Vendor = Vendor;
        if (Leaf0[0] >= 1)
        {
            const std::uint32_t Signature = Cpuid(1)[0];
            Processor_.Stepping = static_cast<int>(Signature & 0xF);
            Processor_.Model = static_cast<int>(Signature >> 4 & 0xF);
            Processor_.Family = static_cast<int>(Signature >> 8 & 0xF);
            Processor_.Type = static_cast<int>(Signature >> 12 & 0x3);
        }
        if (Cpuid(0x80000000u)[0] >= 0x80000004u)
        {
            char Name[49] = {};
            for (std::uint32_t Leaf = 0; Leaf < 3; ++Leaf)
            {
                const auto Registers = Cpuid(0x80000002u + Leaf);
                std::memcpy(Name + Leaf * 16, Registers.data(), 16);
            }
            Processor_.Name = Trim(Name);
        }
#elif defined(__APPLE__)
        Processor_.Vendor = "Apple";
#elif defined(_WIN32)
        Processor_.Vendor = ProcessorRegistryString("VendorIdentifier");
#elif defined(__linux__)
        Processor_.Vendor = CpuinfoField("vendor_id");
#endif
        Processor_.Brand = BrandForVendor(Processor_.Vendor);

#if defined(__APPLE__)
        if (Processor_.Name.empty())
            Processor_.Name = Trim(SysctlString("machdep.cpu.brand_string"));
        // Only Intel Macs report a clock rate.
        Processor_.FrequencyMhz = static_cast<int>(SysctlNumber("hw.cpufrequency") / 1000000);
#elif defined(_WIN32)
        if (Processor_.Name.empty())
            Processor_.Name = ProcessorRegistryString("ProcessorNameString");
        Processor_.FrequencyMhz = static_cast<int>(ProcessorRegistryNumber("~MHz"));
#elif defined(__linux__)
        if (Processor_.Name.empty())
            Processor_.Name = CpuinfoField("model name");
        if (Processor_.Name.empty())
            Processor_.Name = CpuinfoField("Hardware");
        const std::uint64_t MaximumKhz = FirstNumberIn("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
        if (MaximumKhz != 0)
            Processor_.FrequencyMhz = static_cast<int>(MaximumKhz / 1000);
        else
            Processor_.FrequencyMhz = static_cast<int>(std::atof(CpuinfoField("cpu MHz").c_str()));
#endif
        return Processor_;
    }

    int SystemInfo::ProcessorCount() const
    {
        return std::max(1, SDL_GetNumLogicalCPUCores());
    }

    std::vector<std::string> SystemInfo::ProcessorCacheFeatures() const
    {
        std::array<std::uint64_t, 3> Sizes{};
#if defined(__APPLE__)
        Sizes = {SysctlNumber("hw.l1dcachesize"), SysctlNumber("hw.l2cachesize"), SysctlNumber("hw.l3cachesize")};
#elif defined(_WIN32)
        DWORD Length = 0;
        GetLogicalProcessorInformation(nullptr, &Length);
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> Entries(Length /
                                                                  sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (!Entries.empty() && GetLogicalProcessorInformation(Entries.data(), &Length))
        {
            for (const SYSTEM_LOGICAL_PROCESSOR_INFORMATION& Entry : Entries)
            {
                if (Entry.Relationship != RelationCache || Entry.Cache.Level < 1 || Entry.Cache.Level > 3 ||
                    Entry.Cache.Type == CacheInstruction)
                    continue;
                std::uint64_t& Size = Sizes[Entry.Cache.Level - 1];
                if (Size == 0)
                    Size = Entry.Cache.Size;
            }
        }
#elif defined(__linux__)
        for (int Index = 0; Index < 8; ++Index)
        {
            const std::string Directory = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(Index) + "/";
            std::ifstream LevelFile(Directory + "level");
            std::ifstream TypeFile(Directory + "type");
            std::ifstream SizeFile(Directory + "size");
            int Level = 0;
            std::string Type;
            std::string Size;
            if (!(LevelFile >> Level) || !(TypeFile >> Type) || !(SizeFile >> Size))
                break;
            if (Level < 1 || Level > 3 || Type == "Instruction" || Sizes[Level - 1] != 0)
                continue;
            std::uint64_t Bytes = std::strtoull(Size.c_str(), nullptr, 10);
            if (Size.back() == 'K')
                Bytes *= 1024;
            else if (Size.back() == 'M')
                Bytes *= 1024 * 1024;
            Sizes[Level - 1] = Bytes;
        }
#endif
        std::vector<std::string> Features;
        for (std::size_t Level = 0; Level < Sizes.size(); ++Level)
        {
            if (Sizes[Level] != 0)
                Features.push_back("L" + std::to_string(Level + 1) + " cache = " + std::to_string(Sizes[Level] / 1024) +
                                   " kb");
        }
#if defined(DBC_X86_CPUID)
        if (CpuidEdxBit(1, 9))
            Features.push_back("APIC present");
#endif
        return Features;
    }

    std::vector<std::string> SystemInfo::ProcessorPowerFeatures() const
    {
        std::vector<std::string> Features;
#if defined(DBC_X86_CPUID)
        if (CpuidEdxBit(1, 29))
            Features.push_back("Thermal monitor");
        if (CpuidEdxBit(0x80000007u, 0))
            Features.push_back("Temperature sensor");
        if (CpuidEdxBit(0x80000007u, 1))
            Features.push_back("Frequency ID");
        if (CpuidEdxBit(0x80000007u, 2))
            Features.push_back("Voltage ID");
#endif
        return Features;
    }

    std::vector<std::string> SystemInfo::ProcessorInstructionFeatures() const
    {
        std::vector<std::string> Features;
#if defined(DBC_X86_CPUID)
        // The Enhancement Pack's own names first, then what processors have gained since.
        if (CpuidEdxBit(1, 23))
            Features.push_back("MMX");
        if (CpuidEdxBit(0x80000001u, 22))
            Features.push_back("MMX+");
        if (CpuidEdxBit(1, 25))
            Features.push_back("SSE");
        if (CpuidEdxBit(1, 26))
            Features.push_back("SSE2");
        if (CpuidEdxBit(0x80000001u, 31))
            Features.push_back("AMD 3D Now");
        if (CpuidEdxBit(0x80000001u, 30))
            Features.push_back("AMD 3D Now+");
#endif
        if (ProcessorCount() > 1)
            Features.push_back("MP");
#if defined(DBC_X86_CPUID)
        if (CpuidEdxBit(1, 18))
            Features.push_back("Serial");
#endif
        const std::pair<bool, const char*> Modern[] = {
            {SDL_HasSSE3(), "SSE3"}, {SDL_HasSSE41(), "SSE4.1"},     {SDL_HasSSE42(), "SSE4.2"}, {SDL_HasAVX(), "AVX"},
            {SDL_HasAVX2(), "AVX2"}, {SDL_HasAVX512F(), "AVX-512F"}, {SDL_HasNEON(), "NEON"},
        };
        for (const auto& [Present, Name] : Modern)
        {
            if (Present)
                Features.push_back(Name);
        }
        return Features;
    }

    int SystemInfo::ProcessorUsage()
    {
        std::uint64_t Busy = 0;
        std::uint64_t Total = 0;
#if defined(_WIN32)
        FILETIME Idle{};
        FILETIME Kernel{};
        FILETIME User{};
        if (!GetSystemTimes(&Idle, &Kernel, &User))
            return 0;
        const auto Ticks = [](const FILETIME& Time)
        { return static_cast<std::uint64_t>(Time.dwHighDateTime) << 32 | Time.dwLowDateTime; };
        // Kernel time includes idle time.
        Total = Ticks(Kernel) + Ticks(User);
        Busy = Total - Ticks(Idle);
#elif defined(__APPLE__)
        host_cpu_load_info_data_t Load{};
        mach_msg_type_number_t Count = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&Load), &Count) !=
            KERN_SUCCESS)
            return 0;
        Busy = static_cast<std::uint64_t>(Load.cpu_ticks[CPU_STATE_USER]) + Load.cpu_ticks[CPU_STATE_SYSTEM] +
               Load.cpu_ticks[CPU_STATE_NICE];
        Total = Busy + Load.cpu_ticks[CPU_STATE_IDLE];
#elif defined(__linux__)
        // Android 8 and later hide /proc/stat from applications; usage then reads 0.
        std::ifstream Input("/proc/stat");
        std::string Label;
        std::uint64_t Values[8] = {};
        Input >> Label;
        if (Label != "cpu")
            return 0;
        for (std::uint64_t& Value : Values)
            Input >> Value;
        if (!Input)
            return 0;
        const std::uint64_t Idle = Values[3] + Values[4];
        for (const std::uint64_t Value : Values)
            Total += Value;
        Busy = Total - Idle;
#endif
        const std::uint64_t BusyDelta = Busy - PreviousBusy_;
        const std::uint64_t TotalDelta = Total - PreviousTotal_;
        PreviousBusy_ = Busy;
        PreviousTotal_ = Total;
        if (TotalDelta == 0 || Total < Busy)
            return 0;
        return static_cast<int>(std::min<std::uint64_t>(100, (BusyDelta * 100 + TotalDelta / 2) / TotalDelta));
    }

    std::string SystemInfo::PlatformName() const
    {
        return SDL_GetPlatform();
    }

    bool SystemInfo::Memory(MemoryStatus& Output) const
    {
        Output = {};
#if defined(_WIN32)
        MEMORYSTATUSEX Status{};
        Status.dwLength = sizeof(Status);
        if (!GlobalMemoryStatusEx(&Status))
            return false;
        Output.TotalBytes = Status.ullTotalPhys;
        Output.AvailableBytes = Status.ullAvailPhys;
        Output.PercentUsed = static_cast<int>(Status.dwMemoryLoad);
        return true;
#else
#if defined(__APPLE__)
        Output.TotalBytes = SysctlNumber("hw.memsize");
        vm_statistics64_data_t Statistics{};
        mach_msg_type_number_t Count = HOST_VM_INFO64_COUNT;
        vm_size_t PageSize = 0;
        if (host_page_size(mach_host_self(), &PageSize) == KERN_SUCCESS &&
            host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&Statistics), &Count) ==
                KERN_SUCCESS)
        {
            // Inactive pages are reclaimed as soon as something needs them.
            Output.AvailableBytes =
                (static_cast<std::uint64_t>(Statistics.free_count) + Statistics.inactive_count) * PageSize;
        }
#elif defined(__linux__)
        std::ifstream Input("/proc/meminfo");
        std::string Key;
        std::uint64_t Kilobytes = 0;
        std::string Unit;
        while (Input >> Key >> Kilobytes)
        {
            std::getline(Input, Unit);
            if (Key == "MemTotal:")
                Output.TotalBytes = Kilobytes * 1024;
            else if (Key == "MemAvailable:")
                Output.AvailableBytes = Kilobytes * 1024;
        }
#endif
        if (Output.TotalBytes == 0)
            return false;
        Output.AvailableBytes = std::min(Output.AvailableBytes, Output.TotalBytes);
        Output.PercentUsed = static_cast<int>((Output.TotalBytes - Output.AvailableBytes) * 100 / Output.TotalBytes);
        return true;
#endif
    }

    const std::vector<std::string>& SystemInfo::DriveRoots()
    {
        if (DrivesRead_)
            return DriveRoots_;
        DrivesRead_ = true;
#if defined(_WIN32)
        const DWORD Letters = GetLogicalDrives();
        for (char Letter = 'A'; Letter <= 'Z'; ++Letter)
        {
            const std::string Root = std::string(1, Letter) + ":\\";
            if ((Letters >> (Letter - 'A') & 1u) != 0 && GetDriveTypeA(Root.c_str()) == DRIVE_FIXED)
                DriveRoots_.push_back(Root);
        }
        char System[MAX_PATH] = {};
        if (GetWindowsDirectoryA(System, MAX_PATH) > 2)
        {
            const std::string SystemRoot = std::string(System, 2) + "\\";
            const auto Found = std::find_if(DriveRoots_.begin(), DriveRoots_.end(), [&](const std::string& Root)
                                            { return _stricmp(Root.c_str(), SystemRoot.c_str()) == 0; });
            if (Found != DriveRoots_.end())
                std::rotate(DriveRoots_.begin(), Found, Found + 1);
        }
#elif defined(__APPLE__)
        DriveRoots_.push_back("/");
        struct statfs* Volumes = nullptr;
        const int Count = getmntinfo(&Volumes, MNT_NOWAIT);
        for (int Index = 0; Index < Count; ++Index)
        {
            const struct statfs& Volume = Volumes[Index];
            const std::string Root = Volume.f_mntonname;
            // System volumes the Finder hides, such as /System/Volumes/Data, and optical discs are left out.
            if ((Volume.f_flags & MNT_LOCAL) == 0 || (Volume.f_flags & MNT_DONTBROWSE) != 0 || Root == "/" ||
                IsOpticalFileSystem(Volume.f_fstypename))
                continue;
            DriveRoots_.push_back(Root);
        }
#elif defined(__linux__)
        DriveRoots_.push_back("/");
        for (const MountEntry& Entry : Mounts())
        {
            if (Entry.Device.rfind("/dev/", 0) != 0 || Entry.Device.rfind("/dev/loop", 0) == 0 || Entry.Root == "/" ||
                IsOpticalFileSystem(Entry.Type) ||
                std::find(DriveRoots_.begin(), DriveRoots_.end(), Entry.Root) != DriveRoots_.end())
                continue;
            DriveRoots_.push_back(Entry.Root);
        }
#endif
        return DriveRoots_;
    }

    int SystemInfo::DriveCount()
    {
        return static_cast<int>(DriveRoots().size());
    }

    bool SystemInfo::Drive(int Index, DriveDetails& Output)
    {
        Output = {};
        const std::vector<std::string>& Roots = DriveRoots();
        if (Index < 0 || Index >= static_cast<int>(Roots.size()))
            return false;
        const std::string& Root = Roots[static_cast<std::size_t>(Index)];
#if defined(_WIN32)
        ULARGE_INTEGER Available{};
        ULARGE_INTEGER Total{};
        ULARGE_INTEGER Free{};
        if (!GetDiskFreeSpaceExA(Root.c_str(), &Available, &Total, &Free))
            return false;
        Output.TotalBytes = Total.QuadPart;
        Output.FreeBytes = Free.QuadPart;
        DWORD Serial = 0;
        DWORD MaximumName = 0;
        char FileSystem[MAX_PATH + 1] = {};
        if (GetVolumeInformationA(Root.c_str(), nullptr, 0, &Serial, &MaximumName, nullptr, FileSystem,
                                  sizeof(FileSystem)))
        {
            Output.Serial = FormatSerial(Serial);
            Output.MaximumNameLength = static_cast<int>(MaximumName);
            Output.FileSystem = FileSystem;
        }
        // The geometry needs no access rights, only a handle to the volume.
        const std::string Device = "\\\\.\\" + Root.substr(0, 2);
        HANDLE Volume =
            CreateFileA(Device.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (Volume != INVALID_HANDLE_VALUE)
        {
            DISK_GEOMETRY Geometry{};
            DWORD Returned = 0;
            if (DeviceIoControl(Volume, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0, &Geometry, sizeof(Geometry),
                                &Returned, nullptr))
            {
                Output.Cylinders = static_cast<std::uint64_t>(Geometry.Cylinders.QuadPart);
                Output.TracksPerCylinder = static_cast<int>(Geometry.TracksPerCylinder);
                Output.SectorsPerTrack = static_cast<int>(Geometry.SectorsPerTrack);
                Output.BytesPerSector = static_cast<int>(Geometry.BytesPerSector);
            }
            CloseHandle(Volume);
        }
        return true;
#else
        if (!PosixDrive(Root, Output))
            return false;
#if defined(__APPLE__)
        struct statfs Volume{};
        if (statfs(Root.c_str(), &Volume) == 0)
        {
            Output.FileSystem = Volume.f_fstypename;
            Output.Serial = FormatSerial(static_cast<std::uint32_t>(Volume.f_fsid.val[0]));
        }
#elif defined(__linux__)
        for (const MountEntry& Entry : Mounts())
        {
            if (Entry.Root == Root)
                Output.FileSystem = Entry.Type;
        }
#endif
        return true;
#endif
    }

    int SystemInfo::CdCount() const
    {
        int Count = 0;
        for (int Index = 1; !CdRoot(Index).empty(); ++Index)
            ++Count;
        return Count;
    }

    std::string SystemInfo::CdRoot(int Index) const
    {
        if (Index < 1)
            return {};
        int Remaining = Index;
#if defined(_WIN32)
        const DWORD Letters = GetLogicalDrives();
        for (char Letter = 'A'; Letter <= 'Z'; ++Letter)
        {
            const std::string Root = std::string(1, static_cast<char>(Letter + 'a' - 'A')) + ":\\";
            if ((Letters >> (Letter - 'A') & 1u) != 0 && GetDriveTypeA(Root.c_str()) == DRIVE_CDROM && --Remaining == 0)
                return Root;
        }
#elif defined(__APPLE__)
        struct statfs* Volumes = nullptr;
        const int Count = getmntinfo(&Volumes, MNT_NOWAIT);
        for (int Volume = 0; Volume < Count; ++Volume)
        {
            if (IsOpticalFileSystem(Volumes[Volume].f_fstypename) && --Remaining == 0)
                return Volumes[Volume].f_mntonname;
        }
#elif defined(__linux__)
        for (const MountEntry& Entry : Mounts())
        {
            if (IsOpticalFileSystem(Entry.Type) && --Remaining == 0)
                return Entry.Root;
        }
#endif
        return {};
    }

    std::string SystemInfo::ComputerName() const
    {
#if defined(_WIN32)
        char Buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
        DWORD Size = sizeof(Buffer);
        return GetComputerNameA(Buffer, &Size) ? std::string(Buffer, Size) : std::string();
#else
        char Buffer[256] = {};
        if (gethostname(Buffer, sizeof(Buffer) - 1) != 0)
            return {};
        return Buffer;
#endif
    }

    std::string SystemInfo::UserName() const
    {
#if defined(_WIN32)
        char Buffer[257] = {};
        DWORD Size = sizeof(Buffer);
        return GetUserNameA(Buffer, &Size) ? std::string(Buffer) : std::string();
#else
        if (const passwd* Entry = getpwuid(geteuid()); Entry && Entry->pw_name)
            return Entry->pw_name;
        const char* Name = std::getenv("USER");
        return Name ? Name : "";
#endif
    }

    std::string SystemInfo::IpAddress() const
    {
        // Connecting a UDP socket sends nothing; it only asks the system which address would be used.
        std::string Result = "127.0.0.1";
#if defined(_WIN32)
        WSADATA Data{};
        if (WSAStartup(MAKEWORD(2, 2), &Data) != 0)
            return Result;
        const SOCKET Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        const bool Valid = Socket != INVALID_SOCKET;
#else
        const int Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        const bool Valid = Socket >= 0;
#endif
        if (Valid)
        {
            sockaddr_in Remote{};
            Remote.sin_family = AF_INET;
            Remote.sin_port = htons(53);
            inet_pton(AF_INET, "192.0.2.1", &Remote.sin_addr);
            sockaddr_in Local{};
            socklen_t Length = sizeof(Local);
            char Text[INET_ADDRSTRLEN] = {};
            if (connect(Socket, reinterpret_cast<const sockaddr*>(&Remote), sizeof(Remote)) == 0 &&
                getsockname(Socket, reinterpret_cast<sockaddr*>(&Local), &Length) == 0 &&
                inet_ntop(AF_INET, &Local.sin_addr, Text, sizeof(Text)) != nullptr)
                Result = Text;
#if defined(_WIN32)
            closesocket(Socket);
#else
            close(Socket);
#endif
        }
#if defined(_WIN32)
        WSACleanup();
#endif
        return Result;
    }

    std::string SystemInfo::WebClient() const
    {
#if defined(_WIN32)
        char Name[256] = {};
        DWORD Size = sizeof(Name);
        if (AssocQueryStringA(ASSOCF_NONE, ASSOCSTR_FRIENDLYAPPNAME, "http", "open", Name, &Size) != S_OK)
            return {};
        return Name;
#elif defined(__APPLE__)
        return AppleDefaultWebBrowser();
#else
        return {};
#endif
    }

} // namespace dbc
