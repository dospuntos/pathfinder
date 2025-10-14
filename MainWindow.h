/*
 * Copyright 2025, Johan Wagenheim <johan@dospuntos.no>
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef MAINWINDOW_H
#define MAINWINDOW_H


#include "GameDatabase.h"
#include <FilePanel.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Window.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <ListView.h>

class GameDatabase;

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
			void			_UpdateStatusBar(const GameState& state);

			BMenuItem*		fSaveMenuItem;
			BFilePanel*		fOpenPanel;
			BFilePanel*		fSavePanel;

			BStringView* fRoomImageView;
			BStringView* fRoomNameView;
			BTextView* fRoomDescriptionView;
			BListView* fInventoryList;
			BScrollView* fInventoryScroll;

			BStringView* fHealthView;
			BStringView* fScoreView;
			BStringView* fMovesView;
};

#endif
