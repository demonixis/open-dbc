#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbc
{
    class EngineKernel;

    class MemblockSystem
    {
      public:
        explicit MemblockSystem(EngineKernel& Kernel);
        ~MemblockSystem();

        // Mutable access to the memblock store (transitional)
        std::unordered_map<int, std::vector<uint8_t>>& AllMemblocks();
        const std::unordered_map<int, std::vector<uint8_t>>& AllMemblocks() const;

        // Allocation / destruction
        bool MakeMemblock(int Num, int Size, std::string* Error = nullptr);
        bool DeleteMemblock(int Num, std::string* Error = nullptr);
        bool CopyMemblock(int Src, int Dst, int SrcOff, int DstOff, int Size, std::string* Error = nullptr);
        bool ReplaceMemblock(int Num, std::vector<uint8_t> Data, std::string* Error = nullptr);
        const std::vector<uint8_t>* MemblockData(int Num, std::string* Error = nullptr) const;

        // Write primitives
        bool WriteMemblockByte(int Num, int Offset, int Val, std::string* Error = nullptr);
        bool WriteMemblockWord(int Num, int Offset, int Val, std::string* Error = nullptr);
        bool WriteMemblockDword(int Num, int Offset, int Val, std::string* Error = nullptr);
        bool WriteMemblockFloat(int Num, int Offset, float Val, std::string* Error = nullptr);
        bool WriteMemblockString(int Num, int Offset, const char* S, std::string* Error = nullptr);

        // Read primitives
        bool ReadMemblockByte(int& Output, int Num, int Offset, std::string* Error = nullptr);
        bool ReadMemblockWord(int& Output, int Num, int Offset, std::string* Error = nullptr);
        bool ReadMemblockDword(int& Output, int Num, int Offset, std::string* Error = nullptr);
        bool ReadMemblockFloat(float& Output, int Num, int Offset, std::string* Error = nullptr);

        // Queries
        bool MemblockExist(int& Output, int Num, std::string* Error = nullptr);
        // GET MEMBLOCK PTR addresses carry the memblock number in their top byte and an offset below it, so
        // plugins reach the first 16 MB of each memblock and no native pointer reaches BASIC.
        bool MemblockAddress(std::uint32_t& Output, int Num, std::string* Error = nullptr) const;
        // Null unless Size bytes from the address lie in one memblock.
        std::uint8_t* ResolveAddress(std::uint32_t Address, std::uint32_t Size);
        bool MemblockSize(int& Output, int Num, std::string* Error = nullptr);

        // LOCK BACKBUFFER copies the screen, as MAKE MEMBLOCK FROM BITMAP 0 encodes it, into a buffer whose
        // address lies below every memblock address; UNLOCK BACKBUFFER draws it back as bitmap 0.
        bool LockBackbuffer(std::string* Error = nullptr);
        bool UnlockBackbuffer(std::string* Error = nullptr);
        bool BackbufferAddress(std::uint32_t& Output, std::string* Error = nullptr) const;
        // The locked buffer's layout; false while the backbuffer is unlocked.
        bool LockedBackbuffer(int& Width, int& Height, int& Depth, int& Pitch) const;

        // 3D mesh creation from DBC binary memblock format
        bool MakeMeshFromMemblock(int MeshId, int MemblockNum, std::string* Error = nullptr);
        bool ChangeMeshFromMemblock(int MeshId, int MemblockNum, std::string* Error = nullptr);
        bool MakeMemblockFromMesh(int MemblockNum, int MeshId, std::string* Error = nullptr);
        bool ChangeMesh(int ObjId, int LimbId, int MeshId, std::string* Error = nullptr);

        // Memblock <-> Bitmap/Image conversions (12-byte header + RGB565/RGBA pixel data)
        bool MakeMemblockFromBitmap(int MemblockNum, int BitmapId, std::string* Error = nullptr);
        bool MakeBitmapFromMemblock(int BitmapId, int MemblockNum, std::string* Error = nullptr);
        bool MakeMemblockFromImage(int MemblockNum, int ImageId, std::string* Error = nullptr);
        bool MakeImageFromMemblock(int ImageId, int MemblockNum, std::string* Error = nullptr);
        bool MakeMemblockFromSound(int MemblockNum, int SoundId, std::string* Error = nullptr);
        bool MakeSoundFromMemblock(int SoundId, int MemblockNum, std::string* Error = nullptr);

        // Byte forms of the conversions above, also carried by net messages. Command names the BASIC
        // command in their errors.
        bool EncodeMesh(std::vector<uint8_t>& Output, int MeshId, const char* Command, std::string* Error = nullptr);
        bool DecodeMesh(int MeshId, const std::vector<uint8_t>& Data, const char* Command,
                        std::string* Error = nullptr);
        bool EncodeBitmap(std::vector<uint8_t>& Output, int BitmapId, const char* Command,
                          std::string* Error = nullptr);
        bool DecodeBitmap(int BitmapId, const std::vector<uint8_t>& Data, const char* Command,
                          std::string* Error = nullptr);
        bool EncodeImage(std::vector<uint8_t>& Output, int ImageId, const char* Command, std::string* Error = nullptr);
        bool DecodeImage(int ImageId, const std::vector<uint8_t>& Data, const char* Command,
                         std::string* Error = nullptr);
        bool EncodeSound(std::vector<uint8_t>& Output, int SoundId, std::string* Error = nullptr);
        bool DecodeSound(int SoundId, const std::vector<uint8_t>& Data, std::string* Error = nullptr);

      private:
        static bool ValidateMemblockNumber(int Num, std::string* Error = nullptr);
        static bool ValidateRange(const std::vector<uint8_t>& Memblock, int Offset, std::size_t Size,
                                  std::string* Error = nullptr);
        std::vector<uint8_t>* GetMemblock(int Num);
        const std::vector<uint8_t>* GetMemblock(int Num) const;

        EngineKernel& Kernel_;
        std::unordered_map<int, std::vector<uint8_t>> Memblocks_;
        // Encoded with its 12-byte bitmap header; empty while unlocked.
        std::vector<uint8_t> Backbuffer_;
    };

} // namespace dbc
