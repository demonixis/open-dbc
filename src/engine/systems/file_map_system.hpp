#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace dbc
{
    // The Enhancement Pack's file maps: named memory that programs running at the same time share. Windows keeps
    // it in a named mapping; other systems in a file of the temporary directory that every program maps.
    class FileMapSystem
    {
      public:
        FileMapSystem();
        ~FileMapSystem();

        FileMapSystem(const FileMapSystem&) = delete;
        FileMapSystem& operator=(const FileMapSystem&) = delete;

        // Joins the map when another program created it already, keeping its size.
        bool Create(int Id, const std::string& Name, int Size, std::string* Error = nullptr);
        bool Open(int Id, const std::string& Name, std::string* Error = nullptr);
        // Close detaches this program; Destroy also removes the map once no other program uses it.
        bool Close(int Id, std::string* Error = nullptr);
        bool Destroy(int Id, std::string* Error = nullptr);

        bool ReadDword(int Id, int Offset, std::uint32_t& Output, std::string* Error = nullptr);
        bool ReadFloat(int Id, int Offset, float& Output, std::string* Error = nullptr);
        bool ReadString(int Id, int Offset, std::string& Output, std::string* Error = nullptr);
        bool WriteDword(int Id, int Offset, std::uint32_t Value, std::string* Error = nullptr);
        bool WriteFloat(int Id, int Offset, float Value, std::string* Error = nullptr);
        bool WriteString(int Id, int Offset, const std::string& Value, std::string* Error = nullptr);

        // Waits up to five seconds for a write to the map, as WAIT FOR FILE MAP EVENT's timeout did.
        bool WaitForWrite(int Id, std::string* Error = nullptr);

        // The longest string a map field holds, as in the Enhancement Pack.
        static constexpr int StringField = 255;

      private:
        class Mapping;

        Mapping* Find(int Id, std::string* Error);
        bool Access(int Id, int Offset, int Width, bool Write, const void* Input, void* Output, std::string* Error);

        std::unordered_map<int, std::unique_ptr<Mapping>> Maps_;
    };

} // namespace dbc
