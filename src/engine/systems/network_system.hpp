#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dbc
{
    // DarkBASIC Classic multiplayer over ENet. One TCP/IP connection replaces the DirectPlay service providers;
    // the host relays every message, so peer-to-peer and client/server games behave alike. A network thread
    // owns ENet and services it while the program runs; the BASIC thread only queues requests and reads the
    // state that thread publishes. See docs/network.md.
    class NetworkSystem
    {
      public:
        static constexpr std::string_view KConnectionName = "Internet TCP/IP Connection For DirectPlay";
        static constexpr std::uint16_t KDefaultPort = 24600;
        // A host takes the first free port from the default one; discovery asks all of them.
        static constexpr int KDefaultPortCount = 4;

        enum class MessageType : int
        {
            None = 0,
            Integer = 1,
            Float = 2,
            String = 3,
            Memblock = 4,
            Image = 5,
            Bitmap = 6,
            Sound = 7,
            Mesh = 8,
        };

        struct Message
        {
            MessageType Type = MessageType::None;
            int From = 0;
            int To = 0;
            std::int32_t Integer = 0;
            float Float = 0.0f;
            std::string Text{};
            // Memblock bytes, or an image, bitmap, sound or mesh in its memblock layout.
            std::vector<std::uint8_t> Bytes{};
            // Sending only: an unguaranteed message may be dropped, and travels on its own channel.
            bool Guaranteed = true;
        };

        struct Player
        {
            int Id = 0;
            std::string Name{};
            bool Local = false;
            bool Host = false;
        };

        NetworkSystem();
        ~NetworkSystem();

        NetworkSystem(const NetworkSystem&) = delete;
        NetworkSystem& operator=(const NetworkSystem&) = delete;

        // Connection 1 is the only one. The address is "host", "host:port" or empty for the local network;
        // its port, when given, is also the one a created game listens on.
        bool SetConnection(int Connection, std::string_view Address, std::string* Error = nullptr);
        // Asks the connection's address for running games and keeps their list for JOIN NET GAME.
        std::vector<std::string> FindSessions();

        // A game that cannot start or be joined is not an error: NET GAME EXISTS() stays 0 and a warning
        // is logged. Invalid arguments and a second game are errors.
        bool CreateGame(std::string_view GameName, std::string_view PlayerName, int MaxPlayers, int Mode,
                        std::string* Error = nullptr);
        bool JoinGame(int SessionNumber, std::string_view PlayerName, std::string* Error = nullptr);
        void FreeGame();

        // Returns 0 when there is no game or the game is full.
        int CreatePlayer(std::string_view Name);
        bool FreePlayer(int PlayerId, std::string* Error = nullptr);
        std::vector<Player> Players() const;

        // Player 0 sends to every player except the local one. Without a game, or to a player who has
        // just left, the message is dropped.
        bool Send(int ToPlayer, Message Value, std::string* Error = nullptr);
        // Makes the oldest received message current, or clears the current message when none is waiting.
        void NextMessage();
        // The current message or a waiting one exists, so both Classic message loops terminate.
        bool MessageExists() const;
        const Message& CurrentMessage() const;
        // Reading another type than the current message's fails; without a current message it reads 0.
        bool CurrentInteger(std::int32_t& Output, std::string* Error = nullptr) const;
        bool CurrentFloat(float& Output, std::string* Error = nullptr) const;
        bool CurrentString(std::string& Output, std::string* Error = nullptr) const;
        // Binary messages have nothing to read without a current message of their type.
        const std::vector<std::uint8_t>* CurrentBytes(MessageType Type, const char* Command,
                                                      std::string* Error = nullptr) const;

        bool GameExists() const;
        bool GameLost() const;

        // The host never changes until host migration is implemented, so this application is never newly made host.
        bool TakeNowHosting() const
        {
            return false;
        }

        // Each call returns the next player another application added or removed, or 0.
        int TakeCreatedPlayer();
        int TakeDestroyedPlayer();

      private:
        struct Session
        {
            std::string Name;
            std::uint32_t Host = 0;
            std::uint16_t Port = 0;
            std::uint64_t Token = 0;
        };
        struct Shared;
        class Worker;

        bool EnsureEnet();
        void StopWorker();

        bool EnetInitialized_ = false;
        bool ConnectionSet_ = false;
        std::string Address_{};
        std::uint16_t Port_ = 0;
        bool ExplicitPort_ = false;
        bool SessionsFound_ = false;
        std::vector<Session> Sessions_{};
        Message Current_{};
        std::uint32_t NextToken_ = 0;

        std::shared_ptr<Shared> Shared_{};
        std::unique_ptr<Worker> Worker_{};
    };

} // namespace dbc
