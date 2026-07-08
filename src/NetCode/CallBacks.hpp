#pragma once
#include <steam_api.h>
#include <iostream>
#include <string>
#include "NetCode/Network.hpp"

//class SteamCallbacks
//{
//public:
//    SteamCallbacks(NetworkManager* net)
//        : m_network(net)
//        , m_gameLobbyJoinRequestedCallback(this, &SteamCallbacks::OnGameLobbyJoinRequested)
//        , m_lobbyCreatedCallback(this, &SteamCallbacks::OnLobbyCreated)
//    {
//    }
//
//    void OnGameLobbyJoinRequested(GameLobbyJoinRequested_t* pCallback)
//    {
//        if (!m_network) return;
//        CSteamID friendID = pCallback->m_steamIDFriend;
//
//        std::cout << "Friend wants to join! SteamID: " << friendID.ConvertToUint64() << std::endl;
//
//        if (m_network->IsHost())
//        {
//            m_network->AcceptFriendJoin(friendID);
//        }
//    }
//
//    // ИСПРАВЛЕНО: Убран аргумент 'bool bIOFailure', так как CCallback требует ровно 1 аргумент
//    void OnLobbyCreated(LobbyCreated_t* pCallback)
//    {
//        if (pCallback->m_eResult != k_EResultOK)
//        {
//            std::cerr << "Failed to create lobby! Error code: " << pCallback->m_eResult << std::endl;
//            return;
//        }
//
//        CSteamID lobbyID = CSteamID(pCallback->m_ulSteamIDLobby);
//        m_network->SetLobbyID(lobbyID);
//
//        SteamMatchmaking()->SetLobbyData(lobbyID, "name", "Voxel Game");
//        SteamMatchmaking()->SetLobbyData(lobbyID, "host", SteamFriends()->GetPersonaName());
//
//        SteamFriends()->SetRichPresence("lobby", std::to_string(lobbyID.ConvertToUint64()).c_str());
//        SteamFriends()->SetRichPresence("status", "In Game");
//        SteamFriends()->SetRichPresence("connect", "lobby");
//
//        std::cout << "Lobby created! ID: " << lobbyID.ConvertToUint64() << std::endl;
//        std::cout << "Press Shift+Tab to invite friends!" << std::endl;
//    }
//
//private:
//    NetworkManager* m_network;
//
//    CCallback<SteamCallbacks, GameLobbyJoinRequested_t> m_gameLobbyJoinRequestedCallback;
//    CCallback<SteamCallbacks, LobbyCreated_t> m_lobbyCreatedCallback;
//};
