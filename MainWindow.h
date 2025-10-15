/*
 * Copyright 2025, Johan Wagenheim <johan@dospuntos.no>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H


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
	MSG_TAKE_ITEM  = 'TItm'
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
			void 			_MoveToRoom(int roomId);
			void			_TakeItem(int itemId);

			Room			fCurrentRoom;
			std::vector<Item> fCurrentRoomItems;
			BListView* fItemsListView;
			BButton* fTakeItemBtn;

			// Structure to track selected item
			Item fSelectedItem;
			bool fHasSelectedItem;

			BMenuItem*		fSaveMenuItem;
			BFilePanel*		fOpenPanel;
			BFilePanel*		fSavePanel;

			BStringView* 	fRoomImageView;
			BStringView* 	fRoomNameView;
			BTextView* 		fRoomDescriptionView;
			BListView* 		fInventoryList;
			BScrollView* 	fInventoryScroll;
			BButton* 		fNorthBtn;
			BButton* 		fSouthBtn;
			BButton* 		fEastBtn;
			BButton* 		fWestBtn;

			BStringView* 	fHealthView;
			BStringView* 	fScoreView;
			BStringView* 	fMovesView;
};

#endif
