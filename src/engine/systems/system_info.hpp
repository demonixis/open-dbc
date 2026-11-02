#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dbc
{
    // What the processor tells about itself. Fields a platform cannot read stay zero or empty.
    struct ProcessorIdentity
    {
        std::string Vendor;
        std::string Brand;
        std::string Name;
        int Type = 0;
        int Family = 0;
        int Model = 0;
        int Stepping = 0;
        int FrequencyMhz = 0;
    };

    struct MemoryStatus
    {
        std::uint64_t TotalBytes = 0;
        std::uint64_t AvailableBytes = 0;
        int PercentUsed = 0;
    };

    // A local volume. Geometry the platform does not expose without administrator rights stays zero.
    struct DriveDetails
    {
        std::uint64_t TotalBytes = 0;
        std::uint64_t FreeBytes = 0;
        std::uint64_t Cylinders = 0;
        int TracksPerCylinder = 0;
        int SectorsPerTrack = 0;
        int BytesPerSector = 0;
        int MaximumNameLength = 0;
        std::string Serial;
        std::string FileSystem;
    };

    // The machine queries of the Enhancement Pack: processor, memory, drives and names.
    class SystemInfo
    {
      public:
        const ProcessorIdentity& Processor();
        int ProcessorCount() const;
        // Feature lists in the Enhancement Pack's wording.
        std::vector<std::string> ProcessorCacheFeatures() const;
        std::vector<std::string> ProcessorPowerFeatures() const;
        std::vector<std::string> ProcessorInstructionFeatures() const;
        // Percentage of processor time spent busy since the previous call, or since start-up on the first one.
        int ProcessorUsage();
        std::string PlatformName() const;

        bool Memory(MemoryStatus& Output) const;

        // Drive 0 is the volume the system starts from; the others follow in the platform's order.
        int DriveCount();
        bool Drive(int Index, DriveDetails& Output);
        int CdCount() const;
        // The root of the Nth optical drive, counting from 1, or an empty string.
        std::string CdRoot(int Index) const;

        std::string ComputerName() const;
        std::string UserName() const;
        // The address this machine reaches other networks from, or 127.0.0.1 without a route.
        std::string IpAddress() const;
        // The default web browser, where the platform names it.
        std::string WebClient() const;

      private:
        const std::vector<std::string>& DriveRoots();

        bool ProcessorRead_ = false;
        ProcessorIdentity Processor_;
        std::uint64_t PreviousBusy_ = 0;
        std::uint64_t PreviousTotal_ = 0;
        bool DrivesRead_ = false;
        std::vector<std::string> DriveRoots_;
    };

#if defined(__APPLE__)
    // Implemented in Objective-C++.
    std::string AppleDefaultWebBrowser();
#endif

} // namespace dbc
