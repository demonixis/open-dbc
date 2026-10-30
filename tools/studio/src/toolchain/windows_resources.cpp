#include "windows_resources.hpp"
#include "core/i18n.hpp"

#include <array>
#include <charconv>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace dbc::studio
{
    namespace
    {
        std::u16string Utf16(std::string_view Text)
        {
            std::u16string Result;
            for (std::size_t Index = 0; Index < Text.size();)
            {
                const unsigned char Lead = static_cast<unsigned char>(Text[Index]);
                char32_t Code = 0xfffd;
                std::size_t Length = 1;
                if (Lead < 0x80)
                {
                    Code = Lead;
                }
                else if ((Lead >> 5) == 0x6)
                {
                    Length = 2;
                    Code = Lead & 0x1f;
                }
                else if ((Lead >> 4) == 0xe)
                {
                    Length = 3;
                    Code = Lead & 0x0f;
                }
                else if ((Lead >> 3) == 0x1e)
                {
                    Length = 4;
                    Code = Lead & 0x07;
                }
                if (Length > 1)
                {
                    if (Index + Length > Text.size())
                    {
                        Code = 0xfffd;
                        Length = 1;
                    }
                    else
                    {
                        for (std::size_t Next = 1; Next < Length; ++Next)
                            Code = (Code << 6) | (static_cast<unsigned char>(Text[Index + Next]) & 0x3f);
                    }
                }
                Index += Length;
                if (Code >= 0x10000)
                {
                    Code -= 0x10000;
                    Result += static_cast<char16_t>(0xd800 + (Code >> 10));
                    Result += static_cast<char16_t>(0xdc00 + (Code & 0x3ff));
                }
                else
                {
                    Result += static_cast<char16_t>(Code);
                }
            }
            return Result;
        }

        class ResourceWriter
        {
          public:
            std::vector<std::uint8_t> Bytes;

            void Word(std::uint16_t Value)
            {
                Bytes.push_back(static_cast<std::uint8_t>(Value & 0xff));
                Bytes.push_back(static_cast<std::uint8_t>(Value >> 8));
            }

            void Dword(std::uint32_t Value)
            {
                Word(static_cast<std::uint16_t>(Value & 0xffff));
                Word(static_cast<std::uint16_t>(Value >> 16));
            }

            void Text(std::u16string_view Value)
            {
                for (const char16_t Character : Value)
                    Word(static_cast<std::uint16_t>(Character));
                Word(0);
            }

            void Align()
            {
                while (Bytes.size() % 4 != 0)
                    Bytes.push_back(0);
            }

            // Writes one VERSIONINFO node: its header and key; Body writes the value and the children.
            template <typename Writer>
            void Node(std::u16string_view Key, std::uint16_t ValueLength, bool IsText, Writer&& Body)
            {
                Align();
                const std::size_t Start = Bytes.size();
                Word(0);
                Word(ValueLength);
                Word(IsText ? 1 : 0);
                Text(Key);
                Align();
                Body();
                const std::size_t Length = Bytes.size() - Start;
                Bytes[Start] = static_cast<std::uint8_t>(Length & 0xff);
                Bytes[Start + 1] = static_cast<std::uint8_t>(Length >> 8);
            }
        };

        std::array<std::uint16_t, 4> VersionParts(const toolchain::WindowsVersionInfo& Info)
        {
            std::array<std::uint16_t, 4> Parts{};
            std::size_t Index = 0;
            const std::string_view Version = Info.Version;
            const char* Cursor = Version.data();
            const char* End = Version.data() + Version.size();
            while (Cursor < End && Index < 3)
            {
                unsigned Part = 0;
                const auto [Next, Error] = std::from_chars(Cursor, End, Part);
                if (Error != std::errc{})
                    break;
                Parts[Index++] = static_cast<std::uint16_t>(Part > 0xffff ? 0xffff : Part);
                Cursor = Next < End && *Next == '.' ? Next + 1 : End;
            }
            Parts[3] = static_cast<std::uint16_t>(Info.Build < 0 ? 0 : (Info.Build > 0xffff ? 0xffff : Info.Build));
            return Parts;
        }
    } // namespace

    namespace toolchain
    {
        std::vector<std::uint8_t> BuildVersionResource(const WindowsVersionInfo& Info)
        {
            const std::array<std::uint16_t, 4> Parts = VersionParts(Info);
            const std::uint32_t VersionMs = (std::uint32_t{Parts[0]} << 16) | Parts[1];
            const std::uint32_t VersionLs = (std::uint32_t{Parts[2]} << 16) | Parts[3];
            const std::string DisplayVersion = std::to_string(Parts[0]) + "." + std::to_string(Parts[1]) + "." +
                                               std::to_string(Parts[2]) + "." + std::to_string(Parts[3]);
            const std::array<std::pair<std::u16string_view, std::string>, 8> Strings = {{
                {u"CompanyName", Info.CompanyName},
                {u"FileDescription", Info.Description.empty() ? Info.ProductName : Info.Description},
                {u"FileVersion", DisplayVersion},
                {u"InternalName", Info.ProductName},
                {u"LegalCopyright", Info.Copyright},
                {u"OriginalFilename", Info.OriginalFilename},
                {u"ProductName", Info.ProductName},
                {u"ProductVersion", DisplayVersion},
            }};

            ResourceWriter Writer;
            constexpr std::uint16_t FixedFileInfoSize = 52;
            Writer.Node(u"VS_VERSION_INFO", FixedFileInfoSize, false,
                        [&]
                        {
                            Writer.Dword(0xfeef04bd); // signature
                            Writer.Dword(0x00010000); // structure version
                            Writer.Dword(VersionMs);
                            Writer.Dword(VersionLs);
                            Writer.Dword(VersionMs);
                            Writer.Dword(VersionLs);
                            Writer.Dword(0x3f);    // file flags mask
                            Writer.Dword(0);       // file flags
                            Writer.Dword(0x40004); // VOS_NT_WINDOWS32
                            Writer.Dword(1);       // VFT_APP
                            Writer.Dword(0);       // subtype
                            Writer.Dword(0);       // date
                            Writer.Dword(0);
                            Writer.Node(u"StringFileInfo", 0, true,
                                        [&]
                                        {
                                            // U.S. English, Unicode.
                                            Writer.Node(u"040904b0", 0, true,
                                                        [&]
                                                        {
                                                            for (const auto& [Key, Value] : Strings)
                                                            {
                                                                const std::u16string Text = Utf16(Value);
                                                                Writer.Node(Key,
                                                                            static_cast<std::uint16_t>(Text.size() + 1),
                                                                            true, [&] { Writer.Text(Text); });
                                                            }
                                                        });
                                        });
                            Writer.Node(u"VarFileInfo", 0, true,
                                        [&]
                                        {
                                            Writer.Node(u"Translation", 4, false,
                                                        [&]
                                                        {
                                                            Writer.Word(0x0409);
                                                            Writer.Word(1200);
                                                        });
                                        });
                        });
            return std::move(Writer.Bytes);
        }

        std::vector<std::uint8_t> BuildIconGroupResource(const std::vector<WindowsIconImage>& Images,
                                                         std::uint16_t FirstId)
        {
            ResourceWriter Writer;
            Writer.Word(0); // reserved
            Writer.Word(1); // icon
            Writer.Word(static_cast<std::uint16_t>(Images.size()));
            std::uint16_t Id = FirstId;
            for (const WindowsIconImage& Image : Images)
            {
                // 256 pixels is written as 0.
                Writer.Bytes.push_back(static_cast<std::uint8_t>(Image.Size >= 256 ? 0 : Image.Size));
                Writer.Bytes.push_back(static_cast<std::uint8_t>(Image.Size >= 256 ? 0 : Image.Size));
                Writer.Bytes.push_back(0); // colour count
                Writer.Bytes.push_back(0); // reserved
                Writer.Word(1);            // planes
                Writer.Word(32);           // bits per pixel
                Writer.Dword(static_cast<std::uint32_t>(Image.Png.size()));
                Writer.Word(Id++);
            }
            return std::move(Writer.Bytes);
        }

#if defined(_WIN32)
        bool UpdateWindowsExecutableResources(const std::filesystem::path& Executable,
                                              const std::vector<WindowsIconImage>& Icons,
                                              const WindowsVersionInfo& Version, std::string& Error)
        {
            // The IDs and the language of the runner's own resources (tools/runner/runner.rc.in).
            constexpr WORD IconGroupId = 1;
            constexpr WORD VersionId = 1;
            constexpr WORD FirstIconId = 1;
            // Icon images left over from the runner's own icon are removed, so Explorer cannot pick them.
            std::vector<WORD> OldIcons;
            if (HMODULE Module = LoadLibraryExW(Executable.wstring().c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE))
            {
                EnumResourceNamesW(
                    Module, MAKEINTRESOURCEW(3),
                    [](HMODULE, LPCWSTR, LPWSTR Name, LONG_PTR Parameter) -> BOOL
                    {
                        if (IS_INTRESOURCE(Name))
                            reinterpret_cast<std::vector<WORD>*>(Parameter)->push_back(
                                static_cast<WORD>(reinterpret_cast<ULONG_PTR>(Name)));
                        return TRUE;
                    },
                    reinterpret_cast<LONG_PTR>(&OldIcons));
                FreeLibrary(Module);
            }
            HANDLE Update = BeginUpdateResourceW(Executable.wstring().c_str(), FALSE);
            if (Update == nullptr)
            {
                Error = Tr("Cannot open the Windows executable resources");
                return false;
            }
            const WORD Language = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
            bool Updated = true;
            for (const WORD Id : OldIcons)
                Updated =
                    Updated && UpdateResourceW(Update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(Id), Language, nullptr, 0);
            WORD Id = FirstIconId;
            for (const WindowsIconImage& Image : Icons)
            {
                Updated = Updated && UpdateResourceW(Update, MAKEINTRESOURCEW(3), MAKEINTRESOURCEW(Id++), Language,
                                                     const_cast<std::uint8_t*>(Image.Png.data()),
                                                     static_cast<DWORD>(Image.Png.size()));
            }
            std::vector<std::uint8_t> Group = BuildIconGroupResource(Icons, FirstIconId);
            std::vector<std::uint8_t> VersionResource = BuildVersionResource(Version);
            Updated = Updated &&
                      UpdateResourceW(Update, MAKEINTRESOURCEW(14), MAKEINTRESOURCEW(IconGroupId), Language,
                                      Group.data(), static_cast<DWORD>(Group.size())) &&
                      UpdateResourceW(Update, MAKEINTRESOURCEW(16), MAKEINTRESOURCEW(VersionId), Language,
                                      VersionResource.data(), static_cast<DWORD>(VersionResource.size()));
            if (!EndUpdateResourceW(Update, Updated ? FALSE : TRUE) || !Updated)
            {
                Error = Tr("Cannot update the Windows executable icon and version");
                return false;
            }
            return true;
        }
#else
        bool UpdateWindowsExecutableResources(const std::filesystem::path&, const std::vector<WindowsIconImage>&,
                                              const WindowsVersionInfo&, std::string& Error)
        {
            Error = Tr("Windows executables can only be customized on Windows");
            return false;
        }
#endif
    } // namespace toolchain

} // namespace dbc::studio
