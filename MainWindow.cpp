/*
 * Copyright 2025, Johan Wagenheim <johan@dospuntos.no>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "MainWindow.h"
#include "GameDatabase.h"

#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <Path.h>
#include <View.h>
#include <ScrollView.h>
#include <Button.h>

#include <cstdio>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Window"

static const uint32 kMsgNewFile = 'fnew';
static const uint32 kMsgOpenFile = 'fopn';
static const uint32 kMsgSaveFile = 'fsav';

static const char* kSettingsFolder = "Pathfinder";
static const char* kSettingsFile = "Pathfinder_settings";
static const char* kDefaultDatabaseFile = "default_adventure.db";


MainWindow::MainWindow()
	:
	BWindow(BRect(100, 100, 500, 400), B_TRANSLATE("Pathfinder"), B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE)
{
	printf("MainWindow constructor starting\n");

	fDatabase = new GameDatabase();
	printf("GameDatabase created at %p\n", fDatabase);

	BMenuBar* menuBar = _BuildMenu();

	BMessenger messenger(this);
	fOpenPanel = new BFilePanel(B_OPEN_PANEL, &messenger, NULL, B_FILE_NODE, false);
	fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger, NULL, B_FILE_NODE, false);

	BMessage settings;
	_LoadSettings(settings);

	BRect frame;
	if (settings.FindRect("main_window_rect", &frame) == B_OK) {
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), frame.Height());
	}
	MoveOnScreen();

	// --- UI Elements ---

	// Room image (placeholder for now)
	fRoomImageView = new BStringView("room_image_placeholder", "📜 [No image]");
	fRoomImageView->SetFont(be_bold_font);
	fRoomImageView->SetAlignment(B_ALIGN_CENTER);

	// Room name
	fRoomNameView = new BStringView("room_name", "Room Name");
	fRoomNameView->SetFont(be_bold_font);
	fRoomNameView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	// Room description
	fRoomDescriptionView = new BTextView("room_description");
	fRoomDescriptionView->MakeEditable(false);
	fRoomDescriptionView->SetWordWrap(true);
	fRoomDescriptionView->SetExplicitMinSize(BSize(250, 150));

	// Inventory section
	fInventoryList = new BListView("inventory_list");
	fInventoryScroll = new BScrollView("inventory_scroll", fInventoryList,
		B_WILL_DRAW | B_FRAME_EVENTS, false, true);
	fInventoryList->SetSelectionMessage(new BMessage(MSG_INV_ITEM_SELECTED));
	fInventoryList->SetInvocationMessage(new BMessage(MSG_EXAMINE_INV_ITEM));

	// --- Action buttons ---
	fNorthBtn = new BButton("north", "Go North", new BMessage(MSG_MOVE_NORTH));
	fSouthBtn = new BButton("south", "Go South", new BMessage(MSG_MOVE_SOUTH));
	fEastBtn = new BButton("east",  "Go East",  new BMessage(MSG_MOVE_EAST));
	fWestBtn = new BButton("west",  "Go West",  new BMessage(MSG_MOVE_WEST));
	fDropItemBtn = new BButton("drop", "Drop", new BMessage(MSG_DROP_ITEM));
	fUseItemBtn = new BButton("use", "Use", new BMessage(MSG_USE_ITEM));
	fExamineItemBtn = new BButton("examine", "Examine", new BMessage(MSG_EXAMINE_ITEM));
	fExamineInvItemBtn = new BButton("examineInv", "Examine", new BMessage(MSG_EXAMINE_INV_ITEM));
	BButton* combineButton = new BButton("combine", "Combine", new BMessage('comb'));

	fItemsListView = new BListView("items_list");
	fItemsListView->SetSelectionMessage(new BMessage(MSG_ITEM_SELECTED));
	fItemsListView->SetInvocationMessage(new BMessage(MSG_EXAMINE_ITEM));

	fTakeItemBtn = new BButton("take", "Take Item", new BMessage(MSG_TAKE_ITEM));
	fTakeItemBtn->SetEnabled(false);

	// --- Layout ---
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
	.Add(menuBar)
	.AddGroup(B_HORIZONTAL, 10)
		.SetInsets(10, 10, 10, 10)
		// --- Left side: room content ---
		.AddGroup(B_VERTICAL, 5, 1.0f)
			.Add(fRoomImageView)
			.Add(fRoomNameView)
			.Add(fRoomDescriptionView)
			.AddGroup(B_HORIZONTAL, 10)
				.AddGlue()
				.Add(fWestBtn)
				.Add(fNorthBtn)
				.Add(fSouthBtn)
				.Add(fEastBtn)
				.AddGlue()
				.End()
			.End()
		// --- Right side: inventory and actions ---
		.AddGroup(B_VERTICAL, 5, 0.3f)
			.Add(new BStringView("items_label", "Items in room:"))
			.Add(new BScrollView("items_scroll", fItemsListView, 0, false, true))
			.AddGroup(B_HORIZONTAL, 5)
				.Add(fExamineItemBtn)
				.Add(fTakeItemBtn)
				.End()
			.Add(new BStringView("inv_label", "Inventory:"))
			.Add(fInventoryScroll, 1.0f)
			.AddGroup(B_HORIZONTAL, 5)
				.Add(fExamineInvItemBtn)
				.Add(fDropItemBtn)
				.Add(fUseItemBtn)
				.Add(combineButton)
				.End()
			.End()
		.End()
	// --- Status bar at bottom ---
	.AddGroup(B_HORIZONTAL, 10)
		.SetInsets(10, 0, 10, 10)
		.Add(new BStringView("status_label", "Status:"))
		.AddGlue()
		.Add(fHealthView = new BStringView("health", "Health: 100"))
		.Add(fScoreView = new BStringView("score", "Score: 0"))
		.Add(fMovesView = new BStringView("moves", "Moves: 0"))
		.End();

	// Initialize database
	_InitializeDatabase(settings);
}


MainWindow::~MainWindow()
{
	_SaveSettings();

	delete fDatabase;
	delete fOpenPanel;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) != B_OK)
				break;

			BPath path(&ref);
			status_t status = fDatabase->Open(path.Path());
			if (status == B_OK) {
				fCurrentDatabasePath = path.Path();
				fSaveMenuItem->SetEnabled(true);
				_LoadCurrentRoom();
			} else {
				BAlert* loadedMsg = new BAlert("Database error", "Error loading database", "OK");
				loadedMsg->Go();
			}
		} break;

		case B_SAVE_REQUESTED:
		{
			entry_ref ref;
			const char* name;
			if (message->FindRef("directory", &ref) == B_OK
				&& message->FindString("name", &name) == B_OK) {
				BDirectory directory(&ref);
				BEntry entry(&directory, name);
				BPath path = BPath(&entry);

				printf("Save path: %s\n", path.Path());
			}
		} break;

		case kMsgNewFile:
		{
			fSaveMenuItem->SetEnabled(false);
			printf("New\n");
		} break;

		case kMsgOpenFile:
		{
			fOpenPanel->Show();
		} break;

		case kMsgSaveFile:
		{
			fSavePanel->Show();
		} break;

		case MSG_MOVE_NORTH:
        {
            _MoveToRoom(fCurrentRoom.northRoomId);
        } break;

        case MSG_MOVE_SOUTH:
        {
            _MoveToRoom(fCurrentRoom.southRoomId);
        } break;

        case MSG_MOVE_EAST:
        {
            _MoveToRoom(fCurrentRoom.eastRoomId);
        } break;

        case MSG_MOVE_WEST:
        {
            _MoveToRoom(fCurrentRoom.westRoomId);
        } break;

		case MSG_ITEM_SELECTED:
		{
			int32 index = fItemsListView->CurrentSelection();
			bool btnState = index >= 0 &&
							index < (int32)fCurrentRoomItems.size();
            fTakeItemBtn->SetEnabled(btnState);
			fExamineItemBtn->SetEnabled(btnState);
		} break;

		case MSG_INV_ITEM_SELECTED:
		{
			int32 index = fInventoryList->CurrentSelection();
			bool btnState = index >= 0 &&
							index < (int32)fInventoryItems.size();
            fDropItemBtn->SetEnabled(btnState);
			fExamineInvItemBtn->SetEnabled(btnState);
			fUseItemBtn->SetEnabled(btnState);
		} break;

		case MSG_EXAMINE_ITEM:
		{
			int32 index = fItemsListView->CurrentSelection();
			if (index >= 0 && index < (int32)fCurrentRoomItems.size()) {
				Item& item = fCurrentRoomItems[index];
				BString message;
				message << item.name << "\n\n" << item.description;
				(new BAlert("Item Details", message.String(), "OK"))->Go();
			}
		} break;

		case MSG_EXAMINE_INV_ITEM:
		{
			int32 index = fInventoryList->CurrentSelection();
			if (index >= 0 && index < (int32)fInventoryItems.size()) {
				Item& item = fInventoryItems[index];
				BString message;
				message << item.name << "\n\n" << item.description;
				(new BAlert("Item Details", message.String(), "OK"))->Go();
			}
		} break;

		case MSG_TAKE_ITEM:
		{
			int32 index = fItemsListView->CurrentSelection();
            if (index >= 0 && index < (int32)fCurrentRoomItems.size()) {
                _TakeItem(fCurrentRoomItems[index].id);
            }
		} break;

		case MSG_DROP_ITEM:
		{
			int32 index = fInventoryList->CurrentSelection();
			if (index >= 0 && index < (int32)fInventoryItems.size()) {
				_DropItem(fInventoryItems[index].id);
			}
		} break;

		case MSG_USE_ITEM:
		{
			int32 index = fInventoryList->CurrentSelection();
			if (index >= 0 && index < (int32)fInventoryItems.size()) {
				_UseItem(fInventoryItems[index].id);
			}
		} break;


		default:
		{
			BWindow::MessageReceived(message);
			break;
		}
	}
}


BMenuBar*
MainWindow::_BuildMenu()
{
	BMenuBar* menuBar = new BMenuBar("menubar");
	BMenu* menu;
	BMenuItem* item;

	// menu 'File'
	menu = new BMenu(B_TRANSLATE("File"));

	item = new BMenuItem(B_TRANSLATE("New"), new BMessage(kMsgNewFile), 'N');
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Open" B_UTF8_ELLIPSIS), new BMessage(kMsgOpenFile), 'O');
	menu->AddItem(item);

	fSaveMenuItem = new BMenuItem(B_TRANSLATE("Save"), new BMessage(kMsgSaveFile), 'S');
	fSaveMenuItem->SetEnabled(false);
	menu->AddItem(fSaveMenuItem);

	menu->AddSeparatorItem();

	item = new BMenuItem(B_TRANSLATE("About" B_UTF8_ELLIPSIS), new BMessage(B_ABOUT_REQUESTED));
	item->SetTarget(be_app);
	menu->AddItem(item);

	item = new BMenuItem(B_TRANSLATE("Quit"), new BMessage(B_QUIT_REQUESTED), 'Q');
	menu->AddItem(item);

	menuBar->AddItem(menu);

	return menuBar;
}


status_t
MainWindow::_LoadSettings(BMessage& settings)
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	// Add subfolder for settings
	status = path.Append(kSettingsFolder);
	if (status != B_OK)
		return status;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK)
		create_directory(path.Path(), 0755);

	status = path.Append(kSettingsFile);
	if (status != B_OK)
		return status;

	BFile file;
	status = file.SetTo(path.Path(), B_READ_ONLY);
	if (status != B_OK)
		return status;

	return settings.Unflatten(&file);
}


status_t
MainWindow::_SaveSettings()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	status = path.Append(kSettingsFolder);
	if (status != B_OK)
		return status;

	BDirectory dir(path.Path());
	if (dir.InitCheck() != B_OK)
		create_directory(path.Path(), 0755);

	status = path.Append(kSettingsFile);
	if (status != B_OK)
		return status;

	BFile file;
	status = file.SetTo(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status != B_OK)
		return status;

	BMessage settings;
	status = settings.AddRect("main_window_rect", Frame());

	if (fCurrentDatabasePath.Length() > 0) {
		status = settings.AddString("current_database", fCurrentDatabasePath);
		if (status != B_OK)
			return status;
	}

	if (status == B_OK)
		status = settings.Flatten(&file);

	return status;
}


void
MainWindow::_InitializeDatabase(BMessage& settings)
{
	BPath defaultDbPath;
	status_t status;

	// Check if there's a current database in settings
	const char* savedPath;
	if (settings.FindString("current_database", &savedPath) == B_OK) {
		BEntry entry(savedPath);
		if (entry.Exists()) {
			// Database exists, try to open
			status = fDatabase->Open(savedPath);
			fSaveMenuItem->SetEnabled(true);
			_LoadCurrentRoom();
			return;
		}
	}

	// Try to load default database from settings dir
	status = find_directory(B_USER_SETTINGS_DIRECTORY, &defaultDbPath);
	if (status == B_OK) {
		defaultDbPath.Append(kSettingsFolder);

		BEntry dirEntry(defaultDbPath.Path());
		if (!dirEntry.Exists())
			create_directory(defaultDbPath.Path(), 0755);

		defaultDbPath.Append(kDefaultDatabaseFile);

		BEntry dbEntry(defaultDbPath.Path());
		if (dbEntry.Exists()) {
			// Default database exists, try to open it
			status = fDatabase->Open(defaultDbPath.Path());
			if (status == B_OK) {
				fCurrentDatabasePath = defaultDbPath.Path();
				fSaveMenuItem->SetEnabled(true);
				_LoadCurrentRoom();
				return;
			}
		}

		// Create default database
		status = fDatabase->CreateNew(defaultDbPath.Path());
		if (status == B_OK) {
			fCurrentDatabasePath = defaultDbPath.Path();
			fSaveMenuItem->SetEnabled(true);
			_LoadCurrentRoom();
			return;
		}
	}

	// If we get here, something went wrong
	(new BAlert("Failed database", "Failed to initialize database", "OK"))->Go();
	// Todo: Show better error to user.
}


void
MainWindow::_UpdateStatusBar(const GameState& state)
{
	fHealthView->SetText(BString().SetToFormat("Health: %d", state.health));
	fScoreView->SetText(BString().SetToFormat("Score: %d", state.score));
	fMovesView->SetText(BString().SetToFormat("Moves: %d", state.movesCount));
}


void
MainWindow::_LoadCurrentRoom()
{
	if (!fDatabase || !fDatabase->IsOpen())
		return;

	GameState state;
	if (fDatabase->GetGameState(state) != B_OK)
		return;

	if (fDatabase->GetRoom(state.currentRoomId, fCurrentRoom) != B_OK)
		return;

	// Get items in room
    std::vector<Item> roomItems;
    if (fDatabase->GetItemsInRoom(state.currentRoomId, roomItems) != B_OK) {
        fprintf(stderr, "Failed to load items for room %d\n", state.currentRoomId);
        // Continue anyway, just no items
    }

	// Update room name and description
	fRoomNameView->SetText(fCurrentRoom.name);

	BString fullDescription = fCurrentRoom.description;

	// Append item descriptions
    for (size_t i = 0; i < roomItems.size(); i++) {
        if (roomItems[i].roomDescription.Length() > 0) {
            fullDescription << "\n\n" << roomItems[i].roomDescription;
        }
    }

	fRoomDescriptionView->SetText(fullDescription.String());

	// Optional: display image placeholder or file name
	if (!fCurrentRoom.imagePath.IsEmpty())
		fRoomImageView->SetText(fCurrentRoom.imagePath);
	else
		fRoomImageView->SetText("📜 [No image]");

	// Clear and populate items list
    fItemsListView->MakeEmpty();
    fCurrentRoomItems.clear();
    fTakeItemBtn->SetEnabled(false);

    for (size_t i = 0; i < roomItems.size(); i++) {
        if (roomItems[i].canTake) {
            fItemsListView->AddItem(new BStringItem(roomItems[i].name.String()));
            fCurrentRoomItems.push_back(roomItems[i]);
        }
    }

	// Update player stats
	_UpdateStatusBar(state);

	// Update inventory too (so it stays in sync)
	_LoadInventory();

	// Update direction buttons
	_UpdateDirectionButtons();
}


void
MainWindow::_LoadInventory()
{
	if (!fDatabase->IsOpen())
		return;

	fInventoryList->MakeEmpty();
	fInventoryItems.clear();

	std::vector<Item> items;
	if (fDatabase->GetInventoryItems(items) != B_OK)
		return;

	for (size_t i = 0; i < items.size(); i++) {
		fInventoryList->AddItem(new BStringItem(items[i].name.String()));
        fInventoryItems.push_back(items[i]);
	}

	fDropItemBtn->SetEnabled(false);
	fUseItemBtn->SetEnabled(false);
	fExamineItemBtn->SetEnabled(false);
	fExamineInvItemBtn->SetEnabled(false);
}


void
MainWindow::_UpdateDirectionButtons()
{
	fNorthBtn->SetEnabled(fCurrentRoom.northRoomId != -1);
	fSouthBtn->SetEnabled(fCurrentRoom.southRoomId!= -1);
	fEastBtn->SetEnabled(fCurrentRoom.eastRoomId != -1);
	fWestBtn->SetEnabled(fCurrentRoom.westRoomId != -1);
}


void
MainWindow::_MoveToRoom(int roomId)
{
    if (!fDatabase || !fDatabase->IsOpen())
        return;

    // Update the database
    status_t status = fDatabase->MoveToRoom(roomId);
    if (status != B_OK) {
        fprintf(stderr, "Failed to move to room %d\n", roomId);
        // TODO: Show error alert to user
        return;
    }

    // Reload the current room
    _LoadCurrentRoom();

    TRACE(("Moved to room %d\n", roomId));
}


void
MainWindow::_TakeItem(int itemId)
{
    if (!fDatabase || !fDatabase->IsOpen())
        return;

    status_t status = fDatabase->MoveItemToInventory(itemId);
    if (status != B_OK) {
        (new BAlert("Error", "Failed to take item", "OK"))->Go();
        return;
    }

    printf("Item %d taken\n", itemId);

    // Refresh the room display
    _LoadCurrentRoom();
}

void
MainWindow::_DropItem(int itemId)
{
	if (!fDatabase || !fDatabase->IsOpen())
		return;

	status_t status = fDatabase->MoveItemToRoom(itemId, fCurrentRoom.id);
	if (status != B_OK) {
		(new BAlert("Error", "Failed to drop item", "OK"))->Go();
        return;
	}

	printf("Item %d dropped in room %d\n", itemId, fCurrentRoom.id);

	// Refresh the room display
	_LoadCurrentRoom();
}


void
MainWindow::_UseItem(int itemId)
{
    if (!fDatabase || !fDatabase->IsOpen())
        return;

    GameState state;
    fDatabase->GetGameState(state);

    // Check if there's an action for this item in this room
    std::vector<ItemAction> actions;
    status_t status = fDatabase->GetItemActions(itemId, state.currentRoomId, actions);

    if (status == B_OK && actions.size() > 0) {
        // Filter out already completed actions
        bool foundAction = false;
        for (size_t i = 0; i < actions.size(); i++) {
            if (!fDatabase->IsActionCompleted(actions[i].id)) {
                _ExecuteItemAction(actions[i]);
                foundAction = true;
                break;  // Execute only the first uncompleted action
            }
        }

        if (!foundAction) {
            (new BAlert("Use Item", "You've already used that here.", "OK"))->Go();
        }
    } else {
        // No action defined, show generic message
        Item* item = nullptr;
        for (size_t i = 0; i < fInventoryItems.size(); i++) {
            if (fInventoryItems[i].id == itemId) {
                item = &fInventoryItems[i];
                break;
            }
        }

        BString message;
        if (item && item->useMessage.Length() > 0) {
            message = item->useMessage;
        } else {
            message = "You can't use that here.";
        }

        (new BAlert("Use Item", message.String(), "OK"))->Go();
    }
}


void
MainWindow::_ExecuteItemAction(const ItemAction& action)
{
    printf("Executing action: %s\n", action.actionType.String());

    bool success = false;
    BString resultMessage = action.successMessage;

    if (action.actionType == "reveal_item") {
        // Make a hidden item visible
        if (action.targetItemId > 0) {
            status_t status = fDatabase->SetItemVisibility(action.targetItemId, true);
            if (status == B_OK) {
                success = true;
                if (resultMessage.Length() == 0)
                    resultMessage = "Something new has been revealed!";
            }
        }
    }
    else if (action.actionType == "remove_item") {
        // Remove an item from the game
        if (action.targetItemId > 0) {
            status_t status = fDatabase->RemoveItemFromRoom(action.targetItemId);
            if (status == B_OK) {
                success = true;
                if (resultMessage.Length() == 0)
                    resultMessage = "The item has been removed.";
            }
        }
    }
    else if (action.actionType == "unlock_exit") {
        // Remove exit condition (unlock a door)
        if (action.targetDirection.Length() > 0) {
            // You'll need to implement this in GameDatabase
            status_t status = fDatabase->UnlockExit(fCurrentRoom.id,
                                                     action.targetDirection.String());
            if (status == B_OK) {
                success = true;
                if (resultMessage.Length() == 0)
                    resultMessage = "The way is now open!";
            }
        }
    }
    else {
        resultMessage = "Unknown action type.";
    }

    if (success) {
        // Mark action as completed
        fDatabase->MarkActionCompleted(action.id);

        // Consume item if needed
        if (action.consumesItem) {
            fDatabase->RemoveItemFromRoom(action.itemId);
            resultMessage << "\n\nThe item was consumed.";
        }

        // Show success message
        if (resultMessage.Length() > 0) {
            (new BAlert("Success", resultMessage.String(), "OK"))->Go();
        }

        // Refresh UI
        _LoadCurrentRoom();
    } else {
        (new BAlert("Error", "Failed to execute action.", "OK"))->Go();
    }
}