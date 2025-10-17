#include "RoomMapView.h"
#include <Window.h>
#include <algorithm>
#include <cstdio>

const float ROOM_SIZE = 60.0f;
const float ROOM_SPACING = 80.0f;
const float CONNECTION_WIDTH = 2.0f;

RoomMapView::RoomMapView(const char* name)
    :
    BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
    fDatabase(nullptr),
    fCurrentRoomId(0),
    fShowAllRooms(true),
    fScale(1.0f),
    fOffset(20, 20),
    fMinX(0), fMaxX(0), fMinY(0), fMaxY(0)
{
    SetViewColor(240, 240, 240);  // Light gray background
}


RoomMapView::~RoomMapView()
{
}


void
RoomMapView::SetDatabase(GameDatabase* database)
{
    fDatabase = database;
    _LoadRooms();
    Invalidate();
}


void
RoomMapView::SetCurrentRoom(int roomId)
{
    fCurrentRoomId = roomId;
    Invalidate();
}


void
RoomMapView::SetShowAllRooms(bool showAll)
{
    fShowAllRooms = showAll;
    Invalidate();
}


void
RoomMapView::SetVisitedRooms(const std::vector<int>& visited)
{
    fVisitedRooms = visited;
    if (!fShowAllRooms)
        Invalidate();
}


void
RoomMapView::_LoadRooms()
{
    fRooms.clear();

    if (!fDatabase || !fDatabase->IsOpen())
        return;

    // Query all rooms
    const char* sql = "SELECT id, name, description, image_path, "
                      "north_room_id, south_room_id, east_room_id, west_room_id, "
                      "graph_x, graph_y FROM rooms;";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(fDatabase->Handle(), sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Room room;
        room.id = sqlite3_column_int(stmt, 0);
        room.name = (const char*)sqlite3_column_text(stmt, 1);
        room.description = (const char*)sqlite3_column_text(stmt, 2);

        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            room.imagePath = (const char*)sqlite3_column_text(stmt, 3);

        room.northRoomId = sqlite3_column_int(stmt, 4);
        room.southRoomId = sqlite3_column_int(stmt, 5);
        room.eastRoomId = sqlite3_column_int(stmt, 6);
        room.westRoomId = sqlite3_column_int(stmt, 7);
        room.graphX = sqlite3_column_int(stmt, 8);
        room.graphY = sqlite3_column_int(stmt, 9);

        fRooms.push_back(room);
    }

    sqlite3_finalize(stmt);

    _CalculateBounds();
}


void
RoomMapView::_CalculateBounds()
{
    if (fRooms.empty())
        return;

    fMinX = fMaxX = fRooms[0].graphX;
    fMinY = fMaxY = fRooms[0].graphY;

    for (size_t i = 1; i < fRooms.size(); i++) {
        if (fRooms[i].graphX < fMinX) fMinX = fRooms[i].graphX;
        if (fRooms[i].graphX > fMaxX) fMaxX = fRooms[i].graphX;
        if (fRooms[i].graphY < fMinY) fMinY = fRooms[i].graphY;
        if (fRooms[i].graphY > fMaxY) fMaxY = fRooms[i].graphY;
    }

    // Calculate scale to fit in view
    BRect bounds = Bounds();
    float viewWidth = bounds.Width() - 40;
    float viewHeight = bounds.Height() - 40;

    float mapWidth = (fMaxX - fMinX + 1) * ROOM_SPACING;
    float mapHeight = (fMaxY - fMinY + 1) * ROOM_SPACING;

    if (mapWidth > 0 && mapHeight > 0) {
        float scaleX = viewWidth / mapWidth;
        float scaleY = viewHeight / mapHeight;
        fScale = std::min(scaleX, scaleY);
        fScale = std::min(fScale, 1.0f);  // Don't scale up
    }
}


BPoint
RoomMapView::_GraphToScreen(int graphX, int graphY)
{
    float x = (graphX - fMinX) * ROOM_SPACING * fScale + fOffset.x;
    float y = (graphY - fMinY) * ROOM_SPACING * fScale + fOffset.y;
    return BPoint(x, y);
}


BRect
RoomMapView::_GetRoomRect(const Room& room)
{
    BPoint center = _GraphToScreen(room.graphX, room.graphY);
    float halfSize = ROOM_SIZE * fScale / 2.0f;

    return BRect(
        center.x - halfSize,
        center.y - halfSize,
        center.x + halfSize,
        center.y + halfSize
    );
}


void
RoomMapView::Draw(BRect updateRect)
{
    if (fRooms.empty())
        return;

    // Draw connections first (so they appear behind rooms)
    SetHighColor(100, 100, 100);  // Dark gray for connections
    SetPenSize(CONNECTION_WIDTH);

    for (size_t i = 0; i < fRooms.size(); i++) {
        const Room& room = fRooms[i];

        // Skip if not showing this room
        if (!fShowAllRooms) {
            bool isVisited = std::find(fVisitedRooms.begin(),
                                      fVisitedRooms.end(),
                                      room.id) != fVisitedRooms.end();
            if (!isVisited)
                continue;
        }

        BPoint center = _GraphToScreen(room.graphX, room.graphY);

        // Draw connection lines to connected rooms
        for (size_t j = 0; j < fRooms.size(); j++) {
            if (i == j) continue;

            const Room& other = fRooms[j];

            // Check if there's a connection
            bool connected = false;
            if (room.northRoomId == other.id ||
                room.southRoomId == other.id ||
                room.eastRoomId == other.id ||
                room.westRoomId == other.id) {
                connected = true;
            }

            if (connected) {
                BPoint otherCenter = _GraphToScreen(other.graphX, other.graphY);
                StrokeLine(center, otherCenter);
            }
        }
    }

    SetPenSize(1.0f);

    // Draw rooms
    for (size_t i = 0; i < fRooms.size(); i++) {
        const Room& room = fRooms[i];

        // Skip if not showing this room
        if (!fShowAllRooms) {
            bool isVisited = std::find(fVisitedRooms.begin(),
                                      fVisitedRooms.end(),
                                      room.id) != fVisitedRooms.end();
            if (!isVisited)
                continue;
        }

        BRect rect = _GetRoomRect(room);

        // Color based on whether it's the current room
        if (room.id == fCurrentRoomId) {
            SetHighColor(100, 150, 255);  // Blue for current room
        } else {
            SetHighColor(200, 200, 200);  // Light gray for other rooms
        }

        FillRect(rect);

        // Draw border
        SetHighColor(50, 50, 50);
        StrokeRect(rect);

        // Draw room name (truncated if needed)
        SetHighColor(0, 0, 0);
        BFont font;
        GetFont(&font);
        font.SetSize(10.0f * fScale);
        SetFont(&font);

        BString displayName = room.name;
        if (displayName.Length() > 10)
            displayName.Truncate(10);

        float stringWidth = StringWidth(displayName.String());
        BPoint textPos(
            rect.left + (rect.Width() - stringWidth) / 2,
            rect.top + rect.Height() / 2 + 4
        );

        DrawString(displayName.String(), textPos);
    }
}


void
RoomMapView::MouseDown(BPoint where)
{
    int roomId = RoomAtPoint(where);
    if (roomId > 0) {
        printf("Clicked on room %d\n", roomId);

        // Send a message to parent window
        BMessage msg('rmcl');  // Room map click
        msg.AddInt32("room_id", roomId);
        Window()->PostMessage(&msg);
    }
}


int
RoomMapView::RoomAtPoint(BPoint point)
{
    for (size_t i = 0; i < fRooms.size(); i++) {
        BRect rect = _GetRoomRect(fRooms[i]);
        if (rect.Contains(point)) {
            return fRooms[i].id;
        }
    }
    return 0;
}