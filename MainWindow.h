/*
 * Copyright 2025, Johan Wagenheim <johan@dospuntos.no>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#ifdef DEBUG
#   define TRACE(x...) printf("Pathfinder: " x)
#else
#   define TRACE(x...) ((void)0)
#endif

#include "GameDatabase.h"
#include <Button.h>
#include <FilePanel.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Window.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <ListView.h>

class GameDatabase;

enum {
	MSG_MOVE_NORTH = 'movN',
	MSG_MOVE_SOUTH = 'movS',
	MSG_MOVE_EAST  = 'movE',
	MSG_MOVE_WEST  = 'movW',
	MSG_ITEM_SELECTED = 'SItm',
	MSG_INV_ITEM_SELECTED = 'IItm',
	MSG_TAKE_ITEM  = 'TItm',
	MSG_DROP_ITEM = 'DItm',
	MSG_EXAMINE_ITEM = 'EItm',
	MSG_EXAMINE_INV_ITEM = 'EInv',
	MSG_USE_ITEM = 'Uitm',
	MSG_RESET_GAME = 'rset'
};

class MainWindow : public BWindow
{
public:
							MainWindow();
	virtual					~MainWindow();

	virtual void			MessageReceived(BMessage* msg);

private:
			BMenuBar*		_BuildMenu();

			status_t		_LoadSettings(BMessage& settings);
			status_t		_SaveSettings();

			GameDatabase* 	fDatabase;
			BString			fCurrentDatabasePath;

			void			_InitializeDatabase(BMessage& settings);
			void 			_LoadCurrentRoom();
			void 			_LoadInventory();
			void 			_UpdateStatusBar(const GameState& state);
			void 			_UpdateDirectionButtons();
			void 			_MoveToRoom(int roomId, const char* direction);
			void			_TakeItem(int itemId);
			void			_DropItem(int itemId);
			void 			_UseItem(int itemId);
			void 			_ExecuteItemAction(const ItemAction& action);

			Room			fCurrentRoom;
			std::vector<Item> fCurrentRoomItems;
			std::vector<Item> fInventoryItems;
			BListView* fItemsListView;
			BListView* 		fInventoryList;
			BScrollView* 	fInventoryScroll;
			BButton* fTakeItemBtn;
			BButton* fDropItemBtn;
			BButton* fUseItemBtn;
			BButton* fExamineItemBtn;
			BButton* fExamineInvItemBtn;

			// Structure to track selected item
			Item fSelectedItem;
			bool fHasSelectedItem;

			BMenuItem*		fSaveMenuItem;
			BFilePanel*		fOpenPanel;
			BFilePanel*		fSavePanel;

			BStringView* 	fRoomImageView;
			BStringView* 	fRoomNameView;
			BTextView* 		fRoomDescriptionView;

			BButton* 		fNorthBtn;
			BButton* 		fSouthBtn;
			BButton* 		fEastBtn;
			BButton* 		fWestBtn;

			BStringView* 	fHealthView;
			BStringView* 	fScoreView;
			BStringView* 	fMovesView;
};

#endif
