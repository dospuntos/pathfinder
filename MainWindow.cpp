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

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGlue()
		.End();

	BMessenger messenger(this);
	fOpenPanel = new BFilePanel(B_OPEN_PANEL, &messenger, NULL, B_FILE_NODE, false);
	fSavePanel = new BFilePanel(B_SAVE_PANEL, &messenger, NULL, B_FILE_NODE, false);

	BMessage settings;
	_LoadSettings(settings);

	BRect frame;
	if (settings.FindRect("main_window_rect", &frame) == B_OK) {
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Width(), Bounds().Height());
	}
	MoveOnScreen();

	// Initialize database
	printf("About to initialize database\n");
	_InitializeDatabase(settings);
	printf("Database initialization complete\n");
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
				BAlert* loadedMsg = new BAlert("Database loaded", "Database loaded", "OK");
				loadedMsg->Go();
				// Todo: update UI with room data
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
			(new BAlert("Database loaded", "Database loaded from settings", "OK"))->Go();
			// Todo: Update UI with room data
			return;
		}
		(new BAlert("Failed database", "Failed to open saved database, falling back to default", "OK"))->Go();
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
				(new BAlert("Default database", "Default database loaded", "OK"))->Go();
				// Todo: Update UI with room data
				return;
			}
			(new BAlert("Failed database", "Failed to open default database, will create", "OK"))->Go();
		}

		// Create default database
		status = fDatabase->CreateNew(defaultDbPath.Path());
		if (status == B_OK) {
			fCurrentDatabasePath = defaultDbPath.Path();
			fSaveMenuItem->SetEnabled(true);
			(new BAlert("New database", "Created new default database.", "OK"))->Go();
			// Todo: Update UI with room data
			return;
		}
	}

	// If we get here, something went wrong
	(new BAlert("Failed database", "Failed to initialize database", "OK"))->Go();
	// Todo: Show better error to user.
}