#pragma once
#include <cstdint>

// Basic vector used by the game's API.
struct Vector3
{
    float x;
    float y;
    float z;
};

// Forward declaration (fields not needed for current usage).
struct PTTRFirstPersonController_o;

// Minimal PTTRPlayer layout: only the fpCharacterController field is required.
struct PTTRPlayer_o
{
    void* klass;
    void* monitor;
    struct
    {
        char pad_0000[0x720];
        PTTRFirstPersonController_o* fpCharacterController; // +0x720
    } fields;
};

// Shared globals for local player tracking (defined in dllmain.cpp).
extern PTTRPlayer_o* g_LocalPlayer;
extern Vector3 g_LocalPlayerPos;

// Minimal IL2CPP list/array helpers for NetPlayer tracking.
template <typename T>
struct Il2CppArray
{
    void* klass;
    void* monitor;
    void* bounds;
    size_t max_length;
    T m_Items[1];
};

template <typename T>
struct Il2CppList
{
    void* klass;
    void* monitor;
    Il2CppArray<T*>* _items;
    int _size;
    int _version;
};

struct NetworkManager_NetPlayer_o;
struct Enemy_o;

struct NetworkManager_o
{
    void* klass;
    void* monitor;
    char pad_0010[0x28]; // skip base/unknowns up to connectedPlayers
    Il2CppList<NetworkManager_NetPlayer_o>* connectedPlayers; // 0x38
};

struct NetPlayerModel_o;

struct NetworkManager_NetPlayer_o
{
    void* klass;
    void* monitor;
    char pad_0010[0x30];          // skip to implementationObject
    void* implementationObject;   // 0x40? (actually 0x38 but padding accounts for base)
    NetPlayerModel_o* netPlayerModel; // 0x40
    bool self;                    // 0x48
    char pad_rest[0x18];          // unused
};

struct NetPlayerModel_o
{
    void* klass;
    void* monitor;
    char pad_0010[0x2C8];         // skip to netPlayer field
    NetworkManager_NetPlayer_o* netPlayer; // 0x2C8
};

struct RemotePlayer
{
    NetPlayerModel_o* model;
    Vector3 pos;
    bool isSelf;
};

extern RemotePlayer g_RemotePlayers[16];
extern int g_RemotePlayerCount;

// Enemy minimal layout: faction at +0xD4 (int), inherits MonoBehaviour so transform via Component::get_transform.
struct Enemy_o
{
    void* klass;
    void* monitor;
    char pad_0010[0xC4]; // skip to faction
    int faction;         // 0xD4 (after padding + base)
};

struct EnemyInfo
{
    Enemy_o* obj;
    Vector3 pos;
    int faction;
};

extern EnemyInfo g_Enemies[300];
extern int g_EnemyCount;
