#include "Network.hpp"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>

// ============================================================
// КОНСТРУКТОР / ДЕСТРУКТОР
// ============================================================

NetworkManager::NetworkManager(NetworkType type)
    : mType(type)
    , m_isInitialized(false)
    , m_isServer(false)
    , m_isConnected(false)
    , m_enetHost(nullptr)
    , m_enetServerPeer(nullptr)
{
}

NetworkManager::~NetworkManager()
{
    Shutdown();

#ifdef _WIN32
    WSACleanup();
#endif
}

// ============================================================
// ИНИЦИАЛИЗАЦИЯ
// ============================================================

bool NetworkManager::Init()
{
    if (m_isInitialized) {
        Log("[Network] Already initialized");
        return true;
    }

    if (mType == NetworkType::ENet) {
        if (enet_initialize() != 0) {
            Log("[Network] ERROR: Failed to initialize ENet!");
            return false;
        }
        Log("[Network] ENet initialized successfully");
        m_isInitialized = true;
        return true;
    }
    else {
        // Для Steam - заглушка
        Log("[Network] Steam Sockets initialized (stub)");
        m_isInitialized = true;
        return true;
    }
}

void NetworkManager::Shutdown()
{
    Cleanup();

    if (m_isInitialized && mType == NetworkType::ENet) {
        enet_deinitialize();
        Log("[Network] ENet deinitialized");
    }

    m_isInitialized = false;
}

// ============================================================
// УПРАВЛЕНИЕ СЕРВЕРОМ
// ============================================================

bool NetworkManager::StartServer(uint16_t port, int maxClients)
{
    if (!m_isInitialized) {
        Log("[Network] ERROR: Not initialized!");
        return false;
    }

    if (m_isServer) {
        Log("[Network] Server already running");
        return false;
    }

    Cleanup();

    if (mType == NetworkType::ENet) {
        ENetAddress* address = new ENetAddress();
        address->host = ENET_HOST_ANY;
        address->port = port;

        m_enetHost = enet_host_create(address, maxClients, 2, 0, 0);
        if (!m_enetHost) {
            Log("[Network] ERROR: Failed to create ENet server host!");
            return false;
        }

        m_isServer = true;
        m_isConnected = true;
        m_enetClients.clear();

        std::stringstream ss;
        ss << "[Network] Server started on port " << port;
        Log(ss.str());

        delete address;
        return true;
    }
    else {
        Log("[Network] Steam server not implemented in this stub");
        return false;
    }
}

void NetworkManager::StopServer()
{
    if (!m_isServer) return;

    // Отключаем всех клиентов
    for (ENetPeer* peer : m_enetClients) {
        enet_peer_disconnect(peer, 0);
    }

    // Даем время на отключение
    if (m_enetHost) {
        enet_host_service(m_enetHost, nullptr, 100);

        // Принудительно отключаем оставшихся
        for (ENetPeer* peer : m_enetClients) {
            enet_peer_reset(peer);
        }
        m_enetClients.clear();
    }

    Cleanup();
    m_isServer = false;
    m_isConnected = false;
    Log("[Network] Server stopped");
}

// ============================================================
// УПРАВЛЕНИЕ КЛИЕНТОМ
// ============================================================

bool NetworkManager::Connect(const std::string& addressStr, uint16_t port)
{
    if (!m_isInitialized) {
        Log("[Network] ERROR: Not initialized!");
        return false;
    }

    if (m_isConnected) {
        Log("[Network] Already connected");
        return false;
    }

    Cleanup();

    if (mType == NetworkType::ENet) {
        ENetAddress* address = new ENetAddress();
        if (enet_address_set_host(address, addressStr.c_str()) < 0) {
            std::stringstream ss;
            ss << "[Network] ERROR: Invalid address: " << addressStr;
            Log(ss.str());
            return false;
        }
        address->port = port;

        m_enetHost = enet_host_create(nullptr, 1, 2, 0, 0);
        if (!m_enetHost) {
            Log("[Network] ERROR: Failed to create ENet client host!");
            delete address;
            return false;
        }

        m_enetServerPeer = enet_host_connect(m_enetHost, address, 2, 0);
        if (!m_enetServerPeer) {
            Log("[Network] ERROR: Failed to connect to server!");
            enet_host_destroy(m_enetHost);
            m_enetHost = nullptr;
            delete address;
            return false;
        }

        m_isServer = false;
        m_isConnected = true;

        std::stringstream ss;
        ss << "[Network] Connecting to " << addressStr << ":" << port;
        Log(ss.str());
        delete address;
        return true;
    }
    else {
        Log("[Network] Steam client not implemented in this stub");
        return false;
    }
}

void NetworkManager::Disconnect()
{
    if (!m_isConnected) return;

    if (m_enetServerPeer) {
        enet_peer_disconnect(m_enetServerPeer, 0);
        // Даем время на отключение
        if (m_enetHost) {
            enet_host_service(m_enetHost, nullptr, 100);
            enet_peer_reset(m_enetServerPeer);
        }
        m_enetServerPeer = nullptr;
    }

    Cleanup();
    m_isConnected = false;
    m_isServer = false;
    Log("[Network] Disconnected");
}

// ============================================================
// ОБНОВЛЕНИЕ
// ============================================================

void NetworkManager::Update()
{
    if (!m_isInitialized || !m_enetHost) return;

    if (mType == NetworkType::ENet) {
        ENetEvent event;

        // Обрабатываем все события за один кадр
        while (enet_host_service(m_enetHost, &event, 0) > 0) {
            std::cout << (int)event.type <<std::endl;
            
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                HandleConnect(event);
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                HandleReceive(event);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
                HandleDisconnect(event);
                break;

            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }
    else {
        // Steam update stub
    }
}

// ============================================================
// ОТПРАВКА ПАКЕТОВ
// ============================================================

bool NetworkManager::SendPacket(const void* data, size_t size, bool reliable)
{
    if (!data || size == 0) {
        Log("[Network] ERROR: Invalid packet data");
        return false;
    }

    if (!m_isConnected || !m_enetHost) {
        Log("[Network] ERROR: Not connected!");
        return false;
    }

    if (mType == NetworkType::ENet) {
        uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
        ENetPacket* packet = enet_packet_create(data, size, flags);

        if (!packet) {
            Log("[Network] ERROR: Failed to create packet!");
            return false;
        }

        if (m_isServer) {
            // Шлем всем клиентам
            enet_host_broadcast(m_enetHost, 0, packet);
        }
        else if (m_enetServerPeer) {
            // Шлем серверу
            if (enet_peer_send(m_enetServerPeer, 0, packet) < 0) {
                enet_packet_destroy(packet);
                Log("[Network] ERROR: Failed to send packet!");
                return false;
            }
        }


        if (m_enetServerPeer == nullptr) {
            Log("[Network] ERROR: Peer is nullptr!");
            //enet_packet_destroy(packet); // Обязательно удаляем, чтобы не было утечки
            return false;
        }
        return true;
    }
    else {
        Log("[Network] Steam send not implemented in this stub");
        return false;
    }
}

bool NetworkManager::SendPacketToPeer(ENetPeer* peer, const void* data, size_t size, bool reliable)
{
    if (!peer || !data || size == 0) {
        return false;
    }

    if (!m_isServer || !m_enetHost) {
        return false;
    }

    uint32_t flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
    ENetPacket* packet = enet_packet_create(data, size, flags);

    if (!packet) {
        return false;
    }

    if (enet_peer_send(peer, 0, packet) < 0) {
        enet_packet_destroy(packet);
        return false;
    }

    return true;
}

// ============================================================
// ОБРАБОТКА СОБЫТИЙ
// ============================================================

void NetworkManager::HandleConnect(ENetEvent& event)
{
    std::stringstream ss;
    char ip[64];
    enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));
    ss << "[Network] Client connected from " << ip << ":" << event.peer->address.port;
    Log(ss.str());

    if (m_isServer) {
        m_enetClients.push_back(event.peer);
        if (m_connectionCallback) {
            m_connectionCallback(event.peer);
        }
    }
    else {
        // Мы клиент - подключились к серверу
        Log("[Network] Connected to server!");
        if (m_connectionCallback) {
            m_connectionCallback(event.peer);
        }
    }
}

void NetworkManager::HandleReceive(ENetEvent& event)
{
    if (!event.packet) return;

    // Вызываем колбэк для сырых данных
    if (m_packetCallback) {
        m_packetCallback(event.peer, event.packet->data, event.packet->dataLength);
    }

    // Обрабатываем пакет
    HandlePacket(event);

    // Уничтожаем пакет
    enet_packet_destroy(event.packet);
}

void NetworkManager::HandleDisconnect(ENetEvent& event)
{
    char ip[64];
    enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));

    std::stringstream ss;
    ss << "[Network] Client disconnected from " << ip << ":" << event.peer->address.port;
    Log(ss.str());

    if (m_isServer) {
        auto it = std::find(m_enetClients.begin(), m_enetClients.end(), event.peer);
        if (it != m_enetClients.end()) {
            m_enetClients.erase(it);
        }
        if (m_disconnectionCallback) {
            m_disconnectionCallback(event.peer);
        }
    }
    else {
        // Сервер отключил нас
        Log("[Network] Disconnected from server!");
        m_isConnected = false;
        m_enetServerPeer = nullptr;
        if (m_disconnectionCallback) {
            m_disconnectionCallback(event.peer);
        }
    }
}

void NetworkManager::HandlePacket(ENetEvent& event)
{
    if (!event.packet || event.packet->dataLength < 1) {
        return;
    }

    uint8_t packetId = event.packet->data[0];

    switch (static_cast<PacketType>(packetId)) {
    case PacketType::ChatMessage: {
        if (event.packet->dataLength <= 1) break;

        std::string chatMsg(
            reinterpret_cast<char*>(event.packet->data + 1),
            event.packet->dataLength - 1
        );

        std::string prefix = m_isServer ? "[Client]: " : "[Server]: ";
        Log(prefix + chatMsg);

        // Отправляем обратно всем (эхо)
        if (m_isServer) {
            // Отправляем всем клиентам
            SendPacket(event.packet->data, event.packet->dataLength, true);
        }
        else {
            // Отправляем только серверу (хотя он уже получил)
            // Можно не отправлять обратно, чтобы избежать цикла
        }
        break;
    }

    case PacketType::Heartbeat: {
        // Просто игнорируем или отвечаем
        break;
    }

    case PacketType::PlayerState: {
        // Обработка состояния игрока
        break;
    }

    case PacketType::PlayerJoined: {
        Log("[Network] Player joined");
        break;
    }

    case PacketType::PlayerLeft: {
        Log("[Network] Player left");
        break;
    }

    default: {
        std::stringstream ss;
        ss << "[Network] Unknown packet type: " << (int)packetId;
        Log(ss.str());
        break;
    }
    }
}

// ============================================================
// ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ
// ============================================================

std::string NetworkManager::GetServerIP() const
{
    if (m_enetServerPeer == nullptr) {
        return "0.0.0.0";
    }

    char ipBuffer[64] = { 0 };
    if (enet_address_get_host_ip(&m_enetServerPeer->address, ipBuffer, sizeof(ipBuffer)) == 0) {
        return std::string(ipBuffer);
    }

    return "0.0.0.0";
}

int NetworkManager::GetServerPort() const
{
    if (m_enetServerPeer == nullptr) {
        return 0;
    }
    return m_enetServerPeer->address.port;
}

std::string NetworkManager::GetLocalIP() const
{
    char hostName[256];
    if (gethostname(hostName, sizeof(hostName)) != 0) {
        return "127.0.0.1";
    }

    struct addrinfo hints, * res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostName, nullptr, &hints, &res) != 0) {
        return "127.0.0.1";
    }

    std::string ipStr = "127.0.0.1";

    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
        char ipBuffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(ipv4->sin_addr), ipBuffer, INET_ADDRSTRLEN);

        std::string currentIp(ipBuffer);
        if (currentIp != "127.0.0.1") {
            ipStr = currentIp;
            break;
        }
    }

    freeaddrinfo(res);
    return ipStr;
}

void NetworkManager::Cleanup()
{
    if (m_enetHost) {
        enet_host_destroy(m_enetHost);
        m_enetHost = nullptr;
    }
    m_enetServerPeer = nullptr;
    m_enetClients.clear();
}

void NetworkManager::Log(const std::string& message)
{
    if (m_logCallback) {
        m_logCallback(message);
    }
    else {
        // Стандартный вывод, если callback не установлен
        std::cout << message << std::endl;
    }
}