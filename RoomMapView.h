#ifndef ROOM_MAP_VIEW_H
#define ROOM_MAP_VIEW_H

#include "GameDatabase.h"
#include <View.h>
#include <vector>

class RoomMapView : public BView {
public:
    RoomMapView(const char* name);
    virtual ~RoomMapView();

    virtual void Draw(BRect updateRect);
    virtual void MouseDown(BPoint where);

    // Set the database and current room
    void SetDatabase(GameDatabase* database);
    void SetCurrentRoom(int roomId);

    // Control what's displayed
    void SetShowAllRooms(bool showAll);  // true = edit mode, false = only visited
    void SetVisitedRooms(const std::vector<int>& visited);  // For player mode

    // Get room at click position (returns 0 if none)
    int RoomAtPoint(BPoint point);

private:
    void _LoadRooms();
    void _CalculateBounds();
    BRect _GetRoomRect(const Room& room);
    BPoint _GraphToScreen(int graphX, int graphY);

    GameDatabase* fDatabase;
    std::vector<Room> fRooms;
    int fCurrentRoomId;
    bool fShowAllRooms;
    std::vector<int> fVisitedRooms;

    // Map scaling and offset
    float fScale;
    BPoint fOffset;
    int fMinX, fMaxX, fMinY, fMaxY;
};

#endif // ROOM_MAP_VIEW_H