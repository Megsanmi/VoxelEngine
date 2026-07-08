#pragma once

#include <iostream>
#include <vector>
#include <unordered_map>
#include <functional>
#include <string>
#include <algorithm>
#include <memory>
#include <cstring>

#pragma pack(push, 8)
#include <enet.h>
#pragma pack(pop)

#ifdef GetObject
#undef GetObject
#endif
#define NOMINMAX

// ============================================================
// ТИПЫ СООБЩЕНИЙ
// ============================================================
enum class PacketType : uint8_t
{
    // Системные
    ConnectionRequest = 0,
    ConnectionAccept = 1,
    ConnectionDeny = 2,
    Disconnect = 3,
    Heartbeat = 4,

    // Игровые
    PlayerState = 10,
    PlayerJoined = 11,
    PlayerLeft = 12,
    WorldSeed = 20,
    VoxelChange = 30,
    ChatMessage = 40,
};

// ============================================================
// СТРУКТУРЫ ПАКЕТОВ
// ============================================================
#pragma pack(push, 1)

struct PacketHeader
{
    PacketType type;
    uint32_t senderID;
    uint32_t timestamp;
    uint32_t dataSize;
};

#pragma pack(pop)

enum class NetworkType {
    Steam,
    ENet
};

// ============================================================
// NETWORK MANAGER
// ============================================================
class NetworkManager
{
public:
    NetworkManager(NetworkType type = NetworkType::ENet);
    ~NetworkManager();

    // Запрещаем копирование
    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // Инициализация и управление
    bool Init();
    void Shutdown();
    bool IsInitialized() const { return m_isInitialized; }

    // Серверные методы
    bool StartServer(uint16_t port, int maxClients = 32);
    void StopServer();

    // Клиентские методы
    bool Connect(const std::string& addressStr, uint16_t port);
    void Disconnect();

    // Обновление (вызывать каждый кадр)
    void Update();

    // Отправка сообщений
    bool SendPacket(const void* data, size_t size, bool reliable = true);
    bool SendPacketToPeer(ENetPeer* peer, const void* data, size_t size, bool reliable = true);

    // Статус
    bool IsServer() const { return m_isServer; }
    bool IsConnected() const { return m_isConnected; }

    // Получение информации
    std::string GetServerIP() const;
    int GetServerPort() const;
    std::string GetLocalIP() const;
    std::vector<ENetPeer*> GetClients() const { return m_enetClients; }
    int GetClientCount() const { return static_cast<int>(m_enetClients.size()); }

    // Callback'и
    void SetLogCallback(std::function<void(const std::string&)> callback) { m_logCallback = callback; }
    void SetConnectionCallback(std::function<void(ENetPeer*)> callback) { m_connectionCallback = callback; }
    void SetDisconnectionCallback(std::function<void(ENetPeer*)> callback) { m_disconnectionCallback = callback; }
    void SetPacketCallback(std::function<void(ENetPeer*, const uint8_t*, size_t)> callback) { m_packetCallback = callback; }

private:
    // Обработка событий
    void HandleConnect(ENetEvent& event);
    void HandleReceive(ENetEvent& event);
    void HandleDisconnect(ENetEvent& event);
    void HandlePacket(ENetEvent& event);

    // Вспомогательные методы
    void Log(const std::string& message);
    void Cleanup();

    // --- Переменные состояния ---
    bool m_isInitialized = false;
    bool m_isServer = false;
    bool m_isConnected = false;
    NetworkType mType;

    // --- Переменные для ENet ---
    ENetHost* m_enetHost = nullptr;
    ENetPeer* m_enetServerPeer = nullptr;  // Если мы клиент
    std::vector<ENetPeer*> m_enetClients;  // Если мы сервер

    // --- Переменные для Steam (заглушка) ---
    void* m_steamSockets = nullptr;  // В реальном коде замените на ISteamNetworkingSockets*

    // --- Callback'и ---
    std::function<void(const std::string&)> m_logCallback = nullptr;
    std::function<void(ENetPeer*)> m_connectionCallback = nullptr;
    std::function<void(ENetPeer*)> m_disconnectionCallback = nullptr;
    std::function<void(ENetPeer*, const uint8_t*, size_t)> m_packetCallback = nullptr;
};