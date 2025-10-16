#ifndef GAME_EDITOR_H
#define GAME_EDITOR_H

#include "GameDatabase.h"
#include <String.h>
#include <vector>

class GameEditor {
public:
    GameEditor(GameDatabase* database);
    ~GameEditor();

    // Check if database is available
    bool IsReady() const { return fDatabase != nullptr && fDatabase->IsOpen(); }

    // Room editing - Basic CRUD
    status_t CreateRoom(const BString& name, const BString& description,
                       int graphX, int graphY, int& newRoomId);
    status_t UpdateRoom(int roomId, const BString& name,
                       const BString& description);
    status_t DeleteRoom(int roomId);

    // Room connections
    status_t ConnectRooms(int roomId, const char* direction, int targetRoomId);
    status_t DisconnectRoom(int roomId, const char* direction);

    // Item editing - Basic CRUD
    status_t CreateItem(const BString& name, const BString& description,
                       const BString& roomDescription, bool canTake,
                       bool canUse, int& newItemId);
    status_t UpdateItem(int itemId, const BString& name,
                       const BString& description,
                       const BString& roomDescription);
    status_t DeleteItem(int itemId);
    status_t PlaceItem(int itemId, int roomId);

    // Item actions (for puzzles)
    status_t CreateItemAction(int itemId, int roomId,
                             const BString& actionType,
                             int targetItemId, const BString& targetDirection,
                             const BString& successMessage, bool consumesItem,
                             int& newActionId);
    status_t DeleteItemAction(int actionId);

    // Exit conditions (locked doors)
    status_t CreateExitCondition(int roomId, const BString& direction,
                                const BString& lockedMessage,
                                int requiredItemId);
    status_t DeleteExitCondition(int roomId, const BString& direction);

    // Finalize editing - save current state as the initial game state
    status_t SaveAsInitialState();

    // Clear all game state (useful when entering edit mode)
    status_t ClearGameState();

private:
    GameDatabase* fDatabase;
};

#endif // GAME_EDITOR_H