#include "network_system.hpp"
#include "log.hpp"

#include <enet/enet.h>

#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace dbc
{
    namespace
    {
        using namespace std::chrono_literals;

        constexpr std::uint32_t KProtocolVersion = 1;
        constexpr std::array<char, 8> KQueryMagic{'O', 'D', 'B', 'C', 'N', 'E', 'T', 'Q'};
        constexpr std::array<char, 8> KReplyMagic{'O', 'D', 'B', 'C', 'N', 'E', 'T', 'R'};
        constexpr std::size_t KMaxNameBytes = 4096;
        constexpr std::size_t KMaxTextBytes = 1024 * 1024;
        constexpr std::size_t KMaxQueuedMessages = 65536;
        constexpr std::size_t KChannelCount = 2;
        constexpr int KServiceMilliseconds = 5;
        constexpr auto KDiscoveryWait = 500ms;
        constexpr auto KJoinWait = 5s;
        constexpr auto KRequestWait = 3s;
        constexpr auto KDisconnectWait = 200ms;

        enum class Packet : std::uint8_t
        {
            Hello = 1,
            Welcome,
            Reject,
            PlayerAdded,
            PlayerRemoved,
            CreatePlayer,
            CreateReply,
            FreePlayer,
            Data,
        };

        bool Fail(std::string* Error, std::string Message)
        {
            if (Error)
                *Error = std::move(Message);
            return false;
        }

        // Little-endian packet fields; strings carry a 32-bit length.
        class PacketWriter
        {
          public:
            explicit PacketWriter(Packet Kind) { U8(static_cast<std::uint8_t>(Kind)); }

            void U8(std::uint8_t Value) { Bytes_.push_back(Value); }
            void U16(std::uint16_t Value)
            {
                for (int Shift = 0; Shift < 16; Shift += 8)
                    Bytes_.push_back(static_cast<std::uint8_t>(Value >> Shift));
            }
            void U32(std::uint32_t Value)
            {
                for (int Shift = 0; Shift < 32; Shift += 8)
                    Bytes_.push_back(static_cast<std::uint8_t>(Value >> Shift));
            }
            void U64(std::uint64_t Value)
            {
                U32(static_cast<std::uint32_t>(Value));
                U32(static_cast<std::uint32_t>(Value >> 32));
            }
            void I32(std::int32_t Value) { U32(static_cast<std::uint32_t>(Value)); }
            void String(std::string_view Value)
            {
                U32(static_cast<std::uint32_t>(Value.size()));
                Bytes_.insert(Bytes_.end(), Value.begin(), Value.end());
            }
            void Raw(const void* Data, std::size_t Size)
            {
                const auto* Begin = static_cast<const std::uint8_t*>(Data);
                Bytes_.insert(Bytes_.end(), Begin, Begin + Size);
            }

            const std::vector<std::uint8_t>& Bytes() const { return Bytes_; }

          private:
            std::vector<std::uint8_t> Bytes_;
        };

        class PacketReader
        {
          public:
            PacketReader(const std::uint8_t* Data, std::size_t Size) : Data_(Data), Size_(Size) {}

            bool U8(std::uint8_t& Value) { return Read(&Value, 1); }
            bool U16(std::uint16_t& Value)
            {
                std::uint8_t Bytes[2];
                if (!Read(Bytes, 2))
                    return false;
                Value = static_cast<std::uint16_t>(Bytes[0] | (Bytes[1] << 8));
                return true;
            }
            bool U32(std::uint32_t& Value)
            {
                std::uint8_t Bytes[4];
                if (!Read(Bytes, 4))
                    return false;
                Value = static_cast<std::uint32_t>(Bytes[0]) | (static_cast<std::uint32_t>(Bytes[1]) << 8) |
                        (static_cast<std::uint32_t>(Bytes[2]) << 16) | (static_cast<std::uint32_t>(Bytes[3]) << 24);
                return true;
            }
            bool U64(std::uint64_t& Value)
            {
                std::uint32_t Low = 0;
                std::uint32_t High = 0;
                if (!U32(Low) || !U32(High))
                    return false;
                Value = Low | (static_cast<std::uint64_t>(High) << 32);
                return true;
            }
            bool I32(std::int32_t& Value)
            {
                std::uint32_t Bits = 0;
                if (!U32(Bits))
                    return false;
                Value = static_cast<std::int32_t>(Bits);
                return true;
            }
            bool String(std::string& Value, std::size_t Limit)
            {
                std::uint32_t Size = 0;
                if (!U32(Size) || Size > Limit || Size > Size_ - Offset_)
                    return false;
                Value.assign(reinterpret_cast<const char*>(Data_ + Offset_), Size);
                Offset_ += Size;
                return true;
            }
            bool Read(void* Output, std::size_t Size)
            {
                if (Size > Size_ - Offset_)
                    return false;
                std::memcpy(Output, Data_ + Offset_, Size);
                Offset_ += Size;
                return true;
            }
            bool AtEnd() const { return Offset_ == Size_; }

          private:
            const std::uint8_t* Data_;
            std::size_t Size_;
            std::size_t Offset_ = 0;
        };

        void WriteMessage(PacketWriter& Writer, const NetworkSystem::Message& Value)
        {
            Writer.U8(static_cast<std::uint8_t>(Value.Type));
            switch (Value.Type)
            {
                case NetworkSystem::MessageType::Integer:
                    Writer.I32(Value.Integer);
                    break;
                case NetworkSystem::MessageType::Float:
                    Writer.U32(std::bit_cast<std::uint32_t>(Value.Float));
                    break;
                case NetworkSystem::MessageType::String:
                    Writer.String(Value.Text);
                    break;
                case NetworkSystem::MessageType::None:
                    break;
            }
        }

        bool ReadMessage(PacketReader& Reader, NetworkSystem::Message& Value)
        {
            std::uint8_t Type = 0;
            if (!Reader.U8(Type))
                return false;
            switch (static_cast<NetworkSystem::MessageType>(Type))
            {
                case NetworkSystem::MessageType::Integer:
                    Value.Type = NetworkSystem::MessageType::Integer;
                    return Reader.I32(Value.Integer) && Reader.AtEnd();
                case NetworkSystem::MessageType::Float:
                {
                    std::uint32_t Bits = 0;
                    if (!Reader.U32(Bits) || !Reader.AtEnd())
                        return false;
                    Value.Type = NetworkSystem::MessageType::Float;
                    Value.Float = std::bit_cast<float>(Bits);
                    return true;
                }
                case NetworkSystem::MessageType::String:
                    Value.Type = NetworkSystem::MessageType::String;
                    return Reader.String(Value.Text, KMaxTextBytes) && Reader.AtEnd();
                case NetworkSystem::MessageType::None:
                    break;
            }
            return false;
        }

        void SendPacket(ENetPeer* Peer, const PacketWriter& Writer)
        {
            ENetPacket* Value =
                enet_packet_create(Writer.Bytes().data(), Writer.Bytes().size(), ENET_PACKET_FLAG_RELIABLE);
            if (Value && enet_peer_send(Peer, 0, Value) < 0)
                enet_packet_destroy(Value);
        }

        const char* MessageTypeName(NetworkSystem::MessageType Type)
        {
            switch (Type)
            {
                case NetworkSystem::MessageType::Integer:
                    return "an integer";
                case NetworkSystem::MessageType::Float:
                    return "a float";
                case NetworkSystem::MessageType::String:
                    return "a string";
                case NetworkSystem::MessageType::None:
                    break;
            }
            return "no";
        }

        std::vector<std::uint16_t> CandidatePorts(std::uint16_t Port, bool Explicit)
        {
            if (Explicit)
                return {Port};
            std::vector<std::uint16_t> Ports;
            for (int Index = 0; Index < NetworkSystem::KDefaultPortCount; ++Index)
                Ports.push_back(static_cast<std::uint16_t>(NetworkSystem::KDefaultPort + Index));
            return Ports;
        }
    } // namespace

    // State the network thread publishes and the requests the BASIC thread queues for it.
    struct NetworkSystem::Shared
    {
        struct Request
        {
            enum class Kind : std::uint8_t
            {
                Send,
                CreatePlayer,
                FreePlayer,
            };
            Kind What = Kind::Send;
            int Player = 0;
            std::uint32_t Token = 0;
            std::string Name{};
            Message Value{};
        };

        std::mutex Mutex;
        std::condition_variable Changed;
        bool Stop = false;
        std::vector<Request> Requests;

        bool Joined = false;
        bool JoinFailed = false;
        std::string JoinFailure;
        bool Lost = false;
        int LocalPlayer = 0;
        std::vector<Player> Players;
        std::deque<Message> Inbox;
        bool InboxOverflowLogged = false;
        std::deque<int> Created;
        std::deque<int> Destroyed;
        std::unordered_map<std::uint32_t, int> CreateReplies;

        void Deliver(Message Value)
        {
            if (Inbox.size() >= KMaxQueuedMessages)
            {
                Inbox.pop_front();
                if (!InboxOverflowLogged)
                    DBC_LOG_WARN("The net message queue is full; the oldest messages are dropped");
                InboxOverflowLogged = true;
            }
            Inbox.push_back(std::move(Value));
        }
    };

    // Owns the ENet host. Its thread handles every ENet call between construction and destruction.
    class NetworkSystem::Worker
    {
      public:
        struct HostSettings
        {
            std::string GameName;
            std::string PlayerName;
            int MaxPlayers = 0;
            std::uint64_t Token = 0;
        };

        static std::unique_ptr<Worker> StartHost(std::shared_ptr<Shared> State, ENetHost* Host,
                                                 HostSettings Settings)
        {
            std::unique_ptr<Worker> Result(new Worker(std::move(State), Host));
            Result->Hosting_ = true;
            Result->Settings_ = std::move(Settings);
            Result->Members_[1] = Member{1, Result->Settings_.PlayerName, nullptr, true, true};
            Result->LocalPlayer_ = 1;
            Result->Host->intercept = &Worker::Intercept;
            Result->Thread_ = std::thread([Self = Result.get()] { Self->Run(); });
            return Result;
        }

        static std::unique_ptr<Worker> StartClient(std::shared_ptr<Shared> State, ENetHost* Host, ENetPeer* Server,
                                                   std::string PlayerName)
        {
            std::unique_ptr<Worker> Result(new Worker(std::move(State), Host));
            Result->Server_ = Server;
            Result->Settings_.PlayerName = std::move(PlayerName);
            Result->Thread_ = std::thread([Self = Result.get()] { Self->Run(); });
            return Result;
        }

        ~Worker()
        {
            {
                const std::lock_guard Lock(State_->Mutex);
                State_->Stop = true;
            }
            if (Thread_.joinable())
                Thread_.join();
            enet_host_destroy(Host);
        }

        Worker(const Worker&) = delete;
        Worker& operator=(const Worker&) = delete;

        ENetHost* const Host;

      private:
        struct Member
        {
            int Id = 0;
            std::string Name;
            // The host's own players have no peer; on a client this marks the players it owns.
            ENetPeer* Peer = nullptr;
            bool Local = false;
            bool IsHost = false;
        };

        Worker(std::shared_ptr<Shared> State, ENetHost* HostValue) : Host(HostValue), State_(std::move(State)) {}

        // ENet calls this on the worker thread for every datagram, so discovery shares the game port.
        static int ENET_CALLBACK Intercept(ENetHost* Host, ENetEvent*)
        {
            Worker* Self = Current;
            if (Self == nullptr || Host->receivedDataLength != KQueryMagic.size() + 4 ||
                std::memcmp(Host->receivedData, KQueryMagic.data(), KQueryMagic.size()) != 0)
                return 0;
            std::vector<std::uint8_t> Bytes;
            Bytes.assign(KReplyMagic.begin(), KReplyMagic.end());
            PacketWriter Fields(Packet::Hello);
            Fields.U32(KProtocolVersion);
            Fields.U64(Self->Settings_.Token);
            Fields.U16(Host->address.port);
            Fields.U32(static_cast<std::uint32_t>(Self->Members_.size()));
            Fields.U32(static_cast<std::uint32_t>(Self->Settings_.MaxPlayers));
            Fields.String(Self->Settings_.GameName);
            // Skip the packet kind byte the writer starts with.
            Bytes.insert(Bytes.end(), Fields.Bytes().begin() + 1, Fields.Bytes().end());
            ENetBuffer Buffer{};
            Buffer.data = Bytes.data();
            Buffer.dataLength = Bytes.size();
            enet_socket_send(Host->socket, &Host->receivedAddress, &Buffer, 1);
            return 1;
        }

        void Run()
        {
            Current = this;
            for (;;)
            {
                std::vector<Shared::Request> Requests;
                {
                    const std::lock_guard Lock(State_->Mutex);
                    if (State_->Stop)
                        break;
                    Requests.swap(State_->Requests);
                }
                for (Shared::Request& Value : Requests)
                    Handle(Value);
                ENetEvent Event{};
                int Status = enet_host_service(Host, &Event, KServiceMilliseconds);
                while (Status > 0)
                {
                    Handle(Event);
                    Status = enet_host_service(Host, &Event, 0);
                }
            }
            Disconnect();
            Current = nullptr;
        }

        void Disconnect()
        {
            if (Hosting_)
            {
                for (std::size_t Index = 0; Index < Host->peerCount; ++Index)
                {
                    if (Host->peers[Index].state == ENET_PEER_STATE_CONNECTED)
                        enet_peer_disconnect(&Host->peers[Index], 0);
                }
                // Lets the peers acknowledge so they see the game lost at once rather than at their timeout.
                enet_host_flush(Host);
                const auto Disconnecting = [this]
                {
                    for (std::size_t Index = 0; Index < Host->peerCount; ++Index)
                    {
                        if (Host->peers[Index].state != ENET_PEER_STATE_DISCONNECTED)
                            return true;
                    }
                    return false;
                };
                const auto Deadline = std::chrono::steady_clock::now() + KDisconnectWait;
                ENetEvent Event{};
                while (Disconnecting() && std::chrono::steady_clock::now() < Deadline &&
                       enet_host_service(Host, &Event, 10) >= 0)
                {
                    if (Event.type == ENET_EVENT_TYPE_RECEIVE)
                        enet_packet_destroy(Event.packet);
                }
                return;
            }
            if (Server_ == nullptr || Server_->state != ENET_PEER_STATE_CONNECTED)
                return;
            enet_peer_disconnect(Server_, 0);
            const auto Deadline = std::chrono::steady_clock::now() + KDisconnectWait;
            ENetEvent Event{};
            while (std::chrono::steady_clock::now() < Deadline && enet_host_service(Host, &Event, 10) >= 0)
            {
                if (Event.type == ENET_EVENT_TYPE_RECEIVE)
                    enet_packet_destroy(Event.packet);
                else if (Event.type == ENET_EVENT_TYPE_DISCONNECT)
                    break;
            }
        }

        void Publish()
        {
            std::vector<Player> Players;
            Players.reserve(Members_.size());
            for (const auto& [Id, Value] : Members_)
                Players.push_back(Player{Id, Value.Name, Value.Local, Value.IsHost});
            const std::lock_guard Lock(State_->Mutex);
            State_->Players = std::move(Players);
            State_->LocalPlayer = LocalPlayer_;
        }

        void Notify(const std::function<void(Shared&)>& Change)
        {
            {
                const std::lock_guard Lock(State_->Mutex);
                Change(*State_);
            }
            State_->Changed.notify_all();
        }

        void Handle(Shared::Request& Value)
        {
            switch (Value.What)
            {
                case Shared::Request::Kind::Send:
                    if (Hosting_)
                    {
                        Route(LocalPlayer_, Value.Player, Value.Value);
                    }
                    else if (Server_ && LocalPlayer_ != 0)
                    {
                        PacketWriter Writer(Packet::Data);
                        Writer.I32(LocalPlayer_);
                        Writer.I32(Value.Player);
                        WriteMessage(Writer, Value.Value);
                        SendPacket(Server_, Writer);
                    }
                    break;
                case Shared::Request::Kind::CreatePlayer:
                    if (Hosting_)
                    {
                        const int Id = AddMember(Value.Name, nullptr, nullptr);
                        Notify([&](Shared& State) { State.CreateReplies[Value.Token] = Id; });
                    }
                    else if (Server_)
                    {
                        PendingNames_[Value.Token] = Value.Name;
                        PacketWriter Writer(Packet::CreatePlayer);
                        Writer.U32(Value.Token);
                        Writer.String(Value.Name);
                        SendPacket(Server_, Writer);
                    }
                    break;
                case Shared::Request::Kind::FreePlayer:
                    if (Hosting_)
                    {
                        RemoveMember(Value.Player);
                    }
                    else if (Server_)
                    {
                        Members_.erase(Value.Player);
                        Publish();
                        PacketWriter Writer(Packet::FreePlayer);
                        Writer.I32(Value.Player);
                        SendPacket(Server_, Writer);
                    }
                    break;
            }
        }

        void Handle(ENetEvent& Event)
        {
            switch (Event.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                    if (!Hosting_)
                    {
                        PacketWriter Writer(Packet::Hello);
                        Writer.U32(KProtocolVersion);
                        Writer.String(Settings_.PlayerName);
                        SendPacket(Event.peer, Writer);
                    }
                    break;
                case ENET_EVENT_TYPE_RECEIVE:
                {
                    PacketReader Reader(Event.packet->data, Event.packet->dataLength);
                    const bool Valid = Hosting_ ? Receive(Event.peer, Reader) : ReceiveFromHost(Reader);
                    enet_packet_destroy(Event.packet);
                    if (!Valid)
                    {
                        DBC_LOG_WARN("A net game peer sent an invalid packet and is disconnected");
                        enet_peer_disconnect(Event.peer, 0);
                        if (Hosting_)
                            RemovePeer(Event.peer);
                    }
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                    if (Hosting_)
                    {
                        RemovePeer(Event.peer);
                    }
                    else
                    {
                        Server_ = nullptr;
                        Notify(
                            [](Shared& State)
                            {
                                if (State.Joined)
                                {
                                    State.Lost = true;
                                }
                                else if (!State.JoinFailed)
                                {
                                    State.JoinFailed = true;
                                    State.JoinFailure = "the host did not answer";
                                }
                            });
                    }
                    break;
                case ENET_EVENT_TYPE_NONE:
                    break;
            }
        }

        // Host side.
        int AddMember(const std::string& Name, ENetPeer* Peer, ENetPeer* Requester)
        {
            if (static_cast<int>(Members_.size()) >= Settings_.MaxPlayers)
                return 0;
            int Id = 1;
            while (Members_.contains(Id))
                ++Id;
            Members_[Id] = Member{Id, Name, Peer, Peer == nullptr, false};
            PacketWriter Added(Packet::PlayerAdded);
            Added.I32(Id);
            Added.String(Name);
            Added.U8(0);
            for (std::size_t Index = 0; Index < Host->peerCount; ++Index)
            {
                ENetPeer* Other = &Host->peers[Index];
                if (Other->data != nullptr && Other != Peer && Other != Requester)
                    SendPacket(Other, Added);
            }
            Publish();
            if (Peer != nullptr || Requester != nullptr)
                Notify([Id](Shared& State) { State.Created.push_back(Id); });
            return Id;
        }

        void RemoveMember(int Id)
        {
            const auto Found = Members_.find(Id);
            if (Found == Members_.end())
                return;
            const bool Remote = Found->second.Peer != nullptr;
            Members_.erase(Found);
            PacketWriter Removed(Packet::PlayerRemoved);
            Removed.I32(Id);
            for (std::size_t Index = 0; Index < Host->peerCount; ++Index)
            {
                if (Host->peers[Index].data != nullptr)
                    SendPacket(&Host->peers[Index], Removed);
            }
            Publish();
            if (Remote)
                Notify([Id](Shared& State) { State.Destroyed.push_back(Id); });
        }

        void RemovePeer(ENetPeer* Peer)
        {
            Peer->data = nullptr;
            std::vector<int> Owned;
            for (const auto& [Id, Value] : Members_)
            {
                if (Value.Peer == Peer)
                    Owned.push_back(Id);
            }
            for (const int Id : Owned)
                RemoveMember(Id);
        }

        bool OwnedBy(int PlayerId, ENetPeer* Peer) const
        {
            const auto Found = Members_.find(PlayerId);
            return Found != Members_.end() && Found->second.Peer == Peer;
        }

        void Route(int From, int To, const Message& Value)
        {
            for (const auto& [Id, Target] : Members_)
            {
                if ((To == 0 && Id == From) || (To != 0 && Id != To))
                    continue;
                if (Target.Peer == nullptr)
                {
                    Message Copy = Value;
                    Copy.From = From;
                    Copy.To = Id;
                    Notify([&Copy](Shared& State) { State.Deliver(std::move(Copy)); });
                    continue;
                }
                PacketWriter Writer(Packet::Data);
                Writer.I32(From);
                Writer.I32(Id);
                WriteMessage(Writer, Value);
                SendPacket(Target.Peer, Writer);
            }
        }

        bool Receive(ENetPeer* Peer, PacketReader& Reader)
        {
            std::uint8_t Kind = 0;
            if (!Reader.U8(Kind))
                return false;
            const bool Welcomed = Peer->data != nullptr;
            switch (static_cast<Packet>(Kind))
            {
                case Packet::Hello:
                {
                    std::uint32_t Version = 0;
                    std::string Name;
                    if (Welcomed || !Reader.U32(Version) || !Reader.String(Name, KMaxNameBytes) || !Reader.AtEnd())
                        return false;
                    if (Version != KProtocolVersion)
                        return Reject(Peer, "the host runs another OpenDBC network protocol");
                    const int Id = AddMember(Name, Peer, nullptr);
                    if (Id == 0)
                        return Reject(Peer, "the game is full");
                    Peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(Id));
                    PacketWriter Welcome(Packet::Welcome);
                    Welcome.I32(Id);
                    Welcome.U32(static_cast<std::uint32_t>(Members_.size()));
                    for (const auto& [MemberId, Value] : Members_)
                    {
                        Welcome.I32(MemberId);
                        Welcome.String(Value.Name);
                        Welcome.U8(Value.IsHost ? 1 : 0);
                    }
                    SendPacket(Peer, Welcome);
                    return true;
                }
                case Packet::CreatePlayer:
                {
                    std::uint32_t Token = 0;
                    std::string Name;
                    if (!Welcomed || !Reader.U32(Token) || !Reader.String(Name, KMaxNameBytes) || !Reader.AtEnd())
                        return false;
                    const int Id = AddMember(Name, Peer, Peer);
                    PacketWriter Reply(Packet::CreateReply);
                    Reply.U32(Token);
                    Reply.I32(Id);
                    SendPacket(Peer, Reply);
                    return true;
                }
                case Packet::FreePlayer:
                {
                    std::int32_t Id = 0;
                    if (!Welcomed || !Reader.I32(Id) || !Reader.AtEnd())
                        return false;
                    const int Primary = static_cast<int>(reinterpret_cast<std::uintptr_t>(Peer->data));
                    if (Id != Primary && OwnedBy(Id, Peer))
                        RemoveMember(Id);
                    return true;
                }
                case Packet::Data:
                {
                    std::int32_t From = 0;
                    std::int32_t To = 0;
                    Message Value;
                    if (!Welcomed || !Reader.I32(From) || !Reader.I32(To) || To < 0 || !ReadMessage(Reader, Value) ||
                        !OwnedBy(From, Peer))
                        return false;
                    Route(From, To, Value);
                    return true;
                }
                default:
                    return false;
            }
        }

        bool Reject(ENetPeer* Peer, std::string_view Reason)
        {
            PacketWriter Writer(Packet::Reject);
            Writer.String(Reason);
            SendPacket(Peer, Writer);
            enet_peer_disconnect_later(Peer, 0);
            return true;
        }

        // Client side.
        bool ReceiveFromHost(PacketReader& Reader)
        {
            std::uint8_t Kind = 0;
            if (!Reader.U8(Kind))
                return false;
            switch (static_cast<Packet>(Kind))
            {
                case Packet::Welcome:
                {
                    std::int32_t Self = 0;
                    std::uint32_t Count = 0;
                    if (LocalPlayer_ != 0 || !Reader.I32(Self) || !Reader.U32(Count) || Count > 256)
                        return false;
                    for (std::uint32_t Index = 0; Index < Count; ++Index)
                    {
                        Member Value;
                        std::int32_t Id = 0;
                        std::uint8_t IsHost = 0;
                        if (!Reader.I32(Id) || !Reader.String(Value.Name, KMaxNameBytes) || !Reader.U8(IsHost))
                            return false;
                        Value.Id = Id;
                        Value.Local = Id == Self;
                        Value.IsHost = IsHost != 0;
                        Members_[Id] = std::move(Value);
                    }
                    if (!Reader.AtEnd() || !Members_.contains(Self))
                        return false;
                    LocalPlayer_ = Self;
                    Publish();
                    Notify([](Shared& State) { State.Joined = true; });
                    return true;
                }
                case Packet::Reject:
                {
                    std::string Reason;
                    if (!Reader.String(Reason, KMaxNameBytes) || !Reader.AtEnd())
                        return false;
                    Notify(
                        [&Reason](Shared& State)
                        {
                            State.JoinFailed = true;
                            State.JoinFailure = std::move(Reason);
                        });
                    return true;
                }
                case Packet::PlayerAdded:
                {
                    Member Value;
                    std::int32_t Id = 0;
                    std::uint8_t IsHost = 0;
                    if (!Reader.I32(Id) || !Reader.String(Value.Name, KMaxNameBytes) || !Reader.U8(IsHost) ||
                        !Reader.AtEnd())
                        return false;
                    Value.Id = Id;
                    Value.IsHost = IsHost != 0;
                    Members_[Id] = std::move(Value);
                    Publish();
                    Notify([Id](Shared& State) { State.Created.push_back(Id); });
                    return true;
                }
                case Packet::PlayerRemoved:
                {
                    std::int32_t Id = 0;
                    if (!Reader.I32(Id) || !Reader.AtEnd())
                        return false;
                    const auto Found = Members_.find(Id);
                    if (Found == Members_.end())
                        return true;
                    const bool Remote = !Found->second.Local;
                    Members_.erase(Found);
                    Publish();
                    if (Remote)
                        Notify([Id](Shared& State) { State.Destroyed.push_back(Id); });
                    return true;
                }
                case Packet::CreateReply:
                {
                    std::uint32_t Token = 0;
                    std::int32_t Id = 0;
                    if (!Reader.U32(Token) || !Reader.I32(Id) || !Reader.AtEnd())
                        return false;
                    std::string Name;
                    if (const auto Found = PendingNames_.find(Token); Found != PendingNames_.end())
                    {
                        Name = std::move(Found->second);
                        PendingNames_.erase(Found);
                    }
                    if (Id != 0)
                    {
                        Members_[Id] = Member{Id, std::move(Name), nullptr, true, false};
                        Publish();
                    }
                    Notify([Token, Id](Shared& State) { State.CreateReplies[Token] = Id; });
                    return true;
                }
                case Packet::Data:
                {
                    std::int32_t From = 0;
                    std::int32_t To = 0;
                    Message Value;
                    if (!Reader.I32(From) || !Reader.I32(To) || !ReadMessage(Reader, Value))
                        return false;
                    const auto Target = Members_.find(To);
                    if (Target == Members_.end() || !Target->second.Local)
                        return true;
                    Value.From = From;
                    Value.To = To;
                    Notify([&Value](Shared& State) { State.Deliver(std::move(Value)); });
                    return true;
                }
                default:
                    return false;
            }
        }

        static thread_local Worker* Current;

        std::shared_ptr<Shared> State_;
        std::thread Thread_;
        bool Hosting_ = false;
        HostSettings Settings_;
        ENetPeer* Server_ = nullptr;
        int LocalPlayer_ = 0;
        std::map<int, Member> Members_;
        std::unordered_map<std::uint32_t, std::string> PendingNames_;
    };

    thread_local NetworkSystem::Worker* NetworkSystem::Worker::Current = nullptr;

    NetworkSystem::NetworkSystem() = default;

    NetworkSystem::~NetworkSystem()
    {
        StopWorker();
        if (EnetInitialized_)
            enet_deinitialize();
    }

    bool NetworkSystem::EnsureEnet()
    {
        if (!EnetInitialized_)
            EnetInitialized_ = enet_initialize() == 0;
        if (!EnetInitialized_)
            DBC_LOG_WARN("The network library failed to initialize");
        return EnetInitialized_;
    }

    void NetworkSystem::StopWorker()
    {
        Worker_.reset();
        Shared_.reset();
        Current_ = {};
    }

    bool NetworkSystem::SetConnection(int Connection, std::string_view Address, std::string* Error)
    {
        if (Connection != 1)
            return Fail(Error, "Net connection " + std::to_string(Connection) +
                                   " does not exist; connection 1 is TCP/IP");
        while (!Address.empty() && Address.front() == ' ')
            Address.remove_prefix(1);
        while (!Address.empty() && Address.back() == ' ')
            Address.remove_suffix(1);
        std::uint16_t Port = KDefaultPort;
        bool Explicit = false;
        if (const std::size_t Colon = Address.rfind(':'); Colon != std::string_view::npos)
        {
            const std::string_view Digits = Address.substr(Colon + 1);
            unsigned Value = 0;
            const auto [End, Status] = std::from_chars(Digits.data(), Digits.data() + Digits.size(), Value);
            if (Digits.empty() || Status != std::errc{} || End != Digits.data() + Digits.size() || Value == 0 ||
                Value > 65535)
                return Fail(Error, "Net address '" + std::string(Address) + "' must be a host, host:port or :port");
            Port = static_cast<std::uint16_t>(Value);
            Explicit = true;
            Address = Address.substr(0, Colon);
        }
        ConnectionSet_ = true;
        Address_ = std::string(Address);
        Port_ = Port;
        ExplicitPort_ = Explicit;
        SessionsFound_ = false;
        Sessions_.clear();
        return true;
    }

    std::vector<std::string> NetworkSystem::FindSessions()
    {
        if (!ConnectionSet_)
            SetConnection(1, {});
        SessionsFound_ = true;
        Sessions_.clear();
        std::vector<std::string> Names;
        if (!EnsureEnet())
            return Names;

        std::vector<ENetAddress> Targets;
        const std::vector<std::uint16_t> Ports = CandidatePorts(Port_, ExplicitPort_);
        const auto AddTargets = [&](std::uint32_t HostAddress)
        {
            for (const std::uint16_t Port : Ports)
                Targets.push_back(ENetAddress{HostAddress, Port});
        };
        if (Address_.empty())
        {
            // The broadcast reaches the local network; games on this machine also answer on loopback.
            AddTargets(ENET_HOST_BROADCAST);
            ENetAddress Loopback{};
            enet_address_set_host_ip(&Loopback, "127.0.0.1");
            AddTargets(Loopback.host);
        }
        else
        {
            ENetAddress Resolved{};
            if (enet_address_set_host(&Resolved, Address_.c_str()) != 0)
            {
                DBC_LOG_WARN("PERFORM CHECKLIST FOR NET SESSIONS cannot resolve '{}'", Address_);
                return Names;
            }
            AddTargets(Resolved.host);
        }

        const ENetSocket Socket = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (Socket == ENET_SOCKET_NULL)
        {
            DBC_LOG_WARN("PERFORM CHECKLIST FOR NET SESSIONS cannot open a socket");
            return Names;
        }
        enet_socket_set_option(Socket, ENET_SOCKOPT_NONBLOCK, 1);
        enet_socket_set_option(Socket, ENET_SOCKOPT_BROADCAST, 1);
        std::array<std::uint8_t, KQueryMagic.size() + 4> Query{};
        std::memcpy(Query.data(), KQueryMagic.data(), KQueryMagic.size());
        for (int Shift = 0; Shift < 32; Shift += 8)
            Query[KQueryMagic.size() + Shift / 8] = static_cast<std::uint8_t>(KProtocolVersion >> Shift);
        ENetBuffer QueryBuffer{};
        QueryBuffer.data = Query.data();
        QueryBuffer.dataLength = Query.size();
        for (const ENetAddress& Target : Targets)
            enet_socket_send(Socket, &Target, &QueryBuffer, 1);

        const auto Deadline = std::chrono::steady_clock::now() + KDiscoveryWait;
        std::array<std::uint8_t, 8192> Reply{};
        for (;;)
        {
            const auto Remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                Deadline - std::chrono::steady_clock::now());
            if (Remaining.count() <= 0)
                break;
            enet_uint32 Condition = ENET_SOCKET_WAIT_RECEIVE;
            if (enet_socket_wait(Socket, &Condition, static_cast<enet_uint32>(Remaining.count())) != 0 ||
                (Condition & ENET_SOCKET_WAIT_RECEIVE) == 0)
                continue;
            ENetAddress From{};
            ENetBuffer Buffer{};
            Buffer.data = Reply.data();
            Buffer.dataLength = Reply.size();
            const int Received = enet_socket_receive(Socket, &From, &Buffer, 1);
            if (Received <= static_cast<int>(KReplyMagic.size()) ||
                std::memcmp(Reply.data(), KReplyMagic.data(), KReplyMagic.size()) != 0)
                continue;
            PacketReader Reader(Reply.data() + KReplyMagic.size(),
                                static_cast<std::size_t>(Received) - KReplyMagic.size());
            std::uint32_t Version = 0;
            Session Found;
            std::uint32_t Players = 0;
            std::uint32_t MaxPlayers = 0;
            if (!Reader.U32(Version) || Version != KProtocolVersion || !Reader.U64(Found.Token) ||
                !Reader.U16(Found.Port) || !Reader.U32(Players) || !Reader.U32(MaxPlayers) ||
                !Reader.String(Found.Name, KMaxNameBytes) || !Reader.AtEnd())
                continue;
            const bool Known = std::any_of(Sessions_.begin(), Sessions_.end(),
                                           [&](const Session& Value) { return Value.Token == Found.Token; });
            if (Known)
                continue;
            Found.Host = From.host;
            Names.push_back(Found.Name);
            Sessions_.push_back(std::move(Found));
        }
        enet_socket_destroy(Socket);
        return Names;
    }

    bool NetworkSystem::CreateGame(std::string_view GameName, std::string_view PlayerName, int MaxPlayers, int Mode,
                                   std::string* Error)
    {
        if (Worker_)
            return Fail(Error, "CREATE NET GAME requires FREE NET GAME before another game");
        if (MaxPlayers < 2 || MaxPlayers > 256)
            return Fail(Error, "CREATE NET GAME number of players must be between 2 and 256");
        // Every Classic example passes 0, which selects the default like 1.
        if (Mode < 0 || Mode > 2)
            return Fail(Error, "CREATE NET GAME flag must be 1 for peer to peer or 2 for client/server");
        if (GameName.size() > KMaxNameBytes || PlayerName.size() > KMaxNameBytes)
            return Fail(Error, "CREATE NET GAME names are limited to 4096 bytes");
        if (!ConnectionSet_)
            SetConnection(1, {});
        if (!EnsureEnet())
            return true;

        ENetHost* Host = nullptr;
        for (const std::uint16_t Port : CandidatePorts(Port_, ExplicitPort_))
        {
            const ENetAddress Address{ENET_HOST_ANY, Port};
            Host = enet_host_create(&Address, static_cast<std::size_t>(MaxPlayers) + 4, KChannelCount, 0, 0);
            if (Host)
                break;
        }
        if (Host == nullptr)
        {
            DBC_LOG_WARN("CREATE NET GAME cannot listen on port {}", Port_);
            return true;
        }

        std::random_device Random;
        Worker::HostSettings Settings;
        Settings.GameName = std::string(GameName);
        Settings.PlayerName = std::string(PlayerName);
        Settings.MaxPlayers = MaxPlayers;
        Settings.Token = (static_cast<std::uint64_t>(Random()) << 32) | Random();
        Shared_ = std::make_shared<Shared>();
        Shared_->Joined = true;
        Shared_->LocalPlayer = 1;
        Shared_->Players.push_back(Player{1, Settings.PlayerName, true, true});
        Current_ = {};
        Worker_ = Worker::StartHost(Shared_, Host, std::move(Settings));
        DBC_LOG_INFO("Net game '{}' is hosted on port {}", GameName, Host->address.port);
        return true;
    }

    bool NetworkSystem::JoinGame(int SessionNumber, std::string_view PlayerName, std::string* Error)
    {
        if (Worker_)
            return Fail(Error, "JOIN NET GAME requires FREE NET GAME before another game");
        if (PlayerName.size() > KMaxNameBytes)
            return Fail(Error, "JOIN NET GAME player names are limited to 4096 bytes");
        // Classic programs join session 1 without listing sessions first.
        if (!SessionsFound_)
            FindSessions();
        if (SessionNumber < 1 || SessionNumber > static_cast<int>(Sessions_.size()))
        {
            DBC_LOG_WARN("JOIN NET GAME found no session {}", SessionNumber);
            return true;
        }
        if (!EnsureEnet())
            return true;
        const Session& Target = Sessions_[static_cast<std::size_t>(SessionNumber - 1)];
        ENetHost* Host = enet_host_create(nullptr, 1, KChannelCount, 0, 0);
        if (Host == nullptr)
        {
            DBC_LOG_WARN("JOIN NET GAME cannot open a socket");
            return true;
        }
        const ENetAddress Address{Target.Host, Target.Port};
        ENetPeer* Server = enet_host_connect(Host, &Address, KChannelCount, 0);
        if (Server == nullptr)
        {
            enet_host_destroy(Host);
            DBC_LOG_WARN("JOIN NET GAME cannot reach session {}", SessionNumber);
            return true;
        }

        Shared_ = std::make_shared<Shared>();
        Current_ = {};
        Worker_ = Worker::StartClient(Shared_, Host, Server, std::string(PlayerName));
        std::unique_lock Lock(Shared_->Mutex);
        Shared_->Changed.wait_for(Lock, KJoinWait, [this] { return Shared_->Joined || Shared_->JoinFailed; });
        if (Shared_->Joined)
            return true;
        const std::string Reason = Shared_->JoinFailed ? Shared_->JoinFailure : "the host did not answer";
        Lock.unlock();
        DBC_LOG_WARN("JOIN NET GAME cannot join '{}': {}", Target.Name, Reason);
        StopWorker();
        return true;
    }

    void NetworkSystem::FreeGame()
    {
        StopWorker();
    }

    int NetworkSystem::CreatePlayer(std::string_view Name)
    {
        if (!GameExists() || Name.size() > KMaxNameBytes)
            return 0;
        const std::uint32_t Token = ++NextToken_;
        std::unique_lock Lock(Shared_->Mutex);
        Shared_->Requests.push_back(Shared::Request{Shared::Request::Kind::CreatePlayer, 0, Token, std::string(Name)});
        Shared_->Changed.wait_for(Lock, KRequestWait,
                                  [&] { return Shared_->CreateReplies.contains(Token) || Shared_->Lost; });
        const auto Found = Shared_->CreateReplies.find(Token);
        if (Found == Shared_->CreateReplies.end())
            return 0;
        const int Id = Found->second;
        Shared_->CreateReplies.erase(Found);
        return Id;
    }

    bool NetworkSystem::FreePlayer(int PlayerId, std::string* Error)
    {
        if (!GameExists())
            return true;
        const std::lock_guard Lock(Shared_->Mutex);
        const bool Extra = std::any_of(Shared_->Players.begin(), Shared_->Players.end(),
                                       [&](const Player& Value)
                                       { return Value.Id == PlayerId && Value.Local && Value.Id != Shared_->LocalPlayer; });
        if (!Extra)
            return Fail(Error, "FREE NET PLAYER " + std::to_string(PlayerId) +
                                   " is not a player CREATE NET PLAYER added in this application");
        Shared_->Requests.push_back(Shared::Request{Shared::Request::Kind::FreePlayer, PlayerId});
        return true;
    }

    std::vector<NetworkSystem::Player> NetworkSystem::Players() const
    {
        if (!Shared_)
            return {};
        const std::lock_guard Lock(Shared_->Mutex);
        return Shared_->Players;
    }

    bool NetworkSystem::Send(int ToPlayer, Message Value, std::string* Error)
    {
        if (ToPlayer < 0)
            return Fail(Error, "Net message player number must be 0 or a player of the net game");
        if (Value.Text.size() > KMaxTextBytes)
            return Fail(Error, "SEND NET MESSAGE STRING accepts at most 1 MB");
        if (!GameExists())
            return true;
        const std::lock_guard Lock(Shared_->Mutex);
        Shared_->Requests.push_back(Shared::Request{Shared::Request::Kind::Send, ToPlayer, 0, {}, std::move(Value)});
        return true;
    }

    void NetworkSystem::NextMessage()
    {
        Current_ = {};
        if (!Shared_)
            return;
        const std::lock_guard Lock(Shared_->Mutex);
        if (Shared_->Inbox.empty())
            return;
        Current_ = std::move(Shared_->Inbox.front());
        Shared_->Inbox.pop_front();
    }

    bool NetworkSystem::MessageExists() const
    {
        if (Current_.Type != MessageType::None)
            return true;
        if (!Shared_)
            return false;
        const std::lock_guard Lock(Shared_->Mutex);
        return !Shared_->Inbox.empty();
    }

    const NetworkSystem::Message& NetworkSystem::CurrentMessage() const
    {
        return Current_;
    }

    namespace
    {
        bool RequireType(const NetworkSystem::Message& Current, NetworkSystem::MessageType Type, const char* Command,
                         std::string* Error)
        {
            if (Current.Type == NetworkSystem::MessageType::None || Current.Type == Type)
                return true;
            return Fail(Error, std::string(Command) + " requires " + MessageTypeName(Type) +
                                   " message; the current net message is " + MessageTypeName(Current.Type));
        }
    } // namespace

    bool NetworkSystem::CurrentInteger(std::int32_t& Output, std::string* Error) const
    {
        Output = Current_.Integer;
        return RequireType(Current_, MessageType::Integer, "NET MESSAGE INTEGER", Error);
    }

    bool NetworkSystem::CurrentFloat(float& Output, std::string* Error) const
    {
        Output = Current_.Float;
        return RequireType(Current_, MessageType::Float, "NET MESSAGE FLOAT", Error);
    }

    bool NetworkSystem::CurrentString(std::string& Output, std::string* Error) const
    {
        Output = Current_.Text;
        return RequireType(Current_, MessageType::String, "NET MESSAGE STRING$", Error);
    }

    bool NetworkSystem::GameExists() const
    {
        if (!Shared_)
            return false;
        const std::lock_guard Lock(Shared_->Mutex);
        return Shared_->Joined && !Shared_->Lost;
    }

    bool NetworkSystem::GameLost() const
    {
        if (!Shared_)
            return false;
        const std::lock_guard Lock(Shared_->Mutex);
        return Shared_->Lost;
    }

    int NetworkSystem::TakeCreatedPlayer()
    {
        if (!Shared_)
            return 0;
        const std::lock_guard Lock(Shared_->Mutex);
        if (Shared_->Created.empty())
            return 0;
        const int Id = Shared_->Created.front();
        Shared_->Created.pop_front();
        return Id;
    }

    int NetworkSystem::TakeDestroyedPlayer()
    {
        if (!Shared_)
            return 0;
        const std::lock_guard Lock(Shared_->Mutex);
        if (Shared_->Destroyed.empty())
            return 0;
        const int Id = Shared_->Destroyed.front();
        Shared_->Destroyed.pop_front();
        return Id;
    }
} // namespace dbc
