/* 
 * This file is part of keeper.
 *
 * Copyright 2013, cdavis
 *  
 * keeper is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * keeper is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with keeper.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
	Main process:

	1. On boot, keeper is inactive and in a low power state. The
	   current time may be displayed on the LCD, but that's about
	   it. Any key presses entered into the keyboard attached to
	   keeper are relayed directly to the user's PC.

	2. While in this low power state, keeper monitors input for a
	   keystroke sequence on the keyboard or a button press on a
	   GPIO pin to be activiated.

	3. Once activated, keeper checks that the account DB is present.
	   If it is not present, keeper prompts for a passphrase from the
	   user and verifies and then formats the DB for use. If it is
	   already there, keeper prompts for a passphrase once and
	   attempts to unlock the DB. After several failed attempts to
	   unlock, keeper should go back into its inactive state.
	   XXX: how many attempts?

	   The DB file should be stored in "sdcard:/keeper/account.db",
	   keeping in line with FAT 8.3 filename limitations.

           XXX: it would be good to deal gracefully with card insertion
		and extraction events. Worry about this later.

	4. Upon successful unlocking, the search input and account list
	   are present on the LCD screen. By default, the search input
	   box is empty and the user can browse every account in the
	   account list with UP and DOWN arrows. TAB is used to switch
	   between the search input and account list.

	   The user can enter search terms in the search input. By
	   default, only the account brief is searched. The search is
	   performed after the user presses ENTER or TABs to the list.

	   Searching happens dynamically as the user walks the account
	   list. Accounts matching search terms are added a screenful
	   at a time. This way, memory use is contained.

	   The user can hit ENTER at any of the accounts in the list
	   to view the account. The N key is used to create a new
	   account.

	5. New accounts

	6. Viewing/Editing accounts.

	7. Options. ESC key
*/ 
