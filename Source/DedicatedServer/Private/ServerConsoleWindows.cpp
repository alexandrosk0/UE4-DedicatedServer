// Copyright 2004-2016 YaS-Online, Inc. All Rights Reserved.

#include "DedicatedServerPrivatePCH.h"
#include "Engine/World.h"
#include "Engine/Console.h"

void DumpConsoleHelp();

#if WITH_SERVER_CODE && PLATFORM_WINDOWS
	void FServerConsole::Tick()
	{
		if( m_hInputHandle != INVALID_HANDLE_VALUE )
		{
			unsigned long iEventsRead;
			INPUT_RECORD hInputEvents[1];

			if( m_hCachedInputEvent.EventType == KEY_EVENT )
			{
				hInputEvents[0] = m_hCachedInputEvent;

				if( m_hCachedInputEvent.Event.KeyEvent.wRepeatCount == 0 ) m_hCachedInputEvent.EventType = -1;
				else --m_hCachedInputEvent.Event.KeyEvent.wRepeatCount;
			}
			else if( ::ReadConsoleInput( m_hInputHandle, hInputEvents, 1, &iEventsRead ) && iEventsRead > 0 )
			{
				// ToDo: Alt+Numpad sequence: http://referencesource.microsoft.com/#mscorlib/system/console.cs,1512

				if( !hInputEvents[0].Event.KeyEvent.bKeyDown )
				{
					if( hInputEvents[0].Event.KeyEvent.wRepeatCount > 1 )
					{
						--hInputEvents[0].Event.KeyEvent.wRepeatCount;
						m_hCachedInputEvent = hInputEvents[0];
					}
				}
			}
			else UE_LOG( LogServerConsole, Fatal, TEXT( "Failed to read console input" ) );

			if( hInputEvents[0].EventType == KEY_EVENT && hInputEvents[0].Event.KeyEvent.bKeyDown )
			{
				KEY_EVENT_RECORD hEvent = hInputEvents[0].Event.KeyEvent;

				if ( hEvent.wVirtualKeyCode != VK_TAB && hEvent.wVirtualKeyCode != VK_SHIFT )
				{
					// For AutoCompletion
					m_bRepeatedTab = false;
				}

				if( hEvent.wVirtualKeyCode == VK_RETURN )
				{
					FScopeLock hLock( &m_hLock );

					ClearInputLine();

					m_pConsole->SetColor( COLOR_GREEN );
					auto &sInput = m_sInput;
					AsyncTask( ENamedThreads::GameThread, [this, sInput]()
					{
						// code to execute on game thread here

						TCHAR sOutput[MAX_SPRINTF] = TEXT( "" );
						unsigned long iCharsWritten = 0;

						if( sInput.StartsWith( TEXT( "help" ) ) )
						{
							DumpConsoleHelp();

							FCString::Sprintf( sOutput, TEXT( "> %s%s" ), *sInput, LINE_TERMINATOR );
							::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );

							m_hCommandHistory.Add( sInput );
						}
						else if( GEngine->Exec( GWorld, *sInput ) )
						{
							FCString::Sprintf( sOutput, TEXT( "> %s%s" ), *sInput, LINE_TERMINATOR );
							::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );

							m_hCommandHistory.Add( sInput );
						}
						else if ( GWorld->GetAuthGameMode() != nullptr && GWorld->GetAuthGameMode()->ProcessConsoleExec( *sInput, *GLog, nullptr ) )
						{
							FCString::Sprintf( sOutput, TEXT("> %s%s"), *sInput, LINE_TERMINATOR );
							::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );

							m_hCommandHistory.Add( sInput );
						}
						else if ( !sInput.IsEmpty() )
						{
							FCString::Sprintf( sOutput, TEXT( "Unknown Command: %s%s" ), *sInput, LINE_TERMINATOR );
							::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );
						}
					} );

					if( m_sInput.Equals( m_sUserInput ) ) m_sUserInput.Reset();
					m_sInput.Empty();

					m_pConsole->SetColor( COLOR_NONE );
				}
				else if( hEvent.wVirtualKeyCode == VK_BACK )
				{
					FScopeLock hLock( &m_hLock );

					if( m_sInput.Len() >= 1 )
					{
						m_sInput.RemoveAt( m_sInput.Len() - 1 );

						RedrawInputLine();
					}
				}
				else if( hEvent.wVirtualKeyCode == VK_ESCAPE )
				{
					FScopeLock hLock( &m_hLock );

					ClearInputLine();
					m_sInput.Empty();
				}
				else if( hEvent.wVirtualKeyCode == VK_TAB )
				{
					FScopeLock hLock( &m_hLock );

					// AutoCompletion section

					// Do the initialization late, so late registrations are captured
					// Alternatively it can be done in FServerConsole::Show
					InitLocalConsoleCommandsArray();

					static uint32 iMaxIndex = m_hLocalConsoleCommandLibrary.Num();

					if ( iMaxIndex == 0 )
					{
						// Avoiding division by zero in a project with zero console commands
						return;
					}

					if ( !m_bRepeatedTab )
					{
						//Reset the index and get current input
						m_iCurrentIndex = 0;
						m_sAutocompleteInput = m_sInput;
						m_bRepeatedTab = true;
					}
					else if ( hEvent.dwControlKeyState & SHIFT_PRESSED )
					{
						// Reverse the order, shift is pressed
						if ( m_iCurrentIndex != 0 )
						{
							--m_iCurrentIndex;
						}
						else
						{
							// Loop around to the end
							m_iCurrentIndex = iMaxIndex - 1;
						}
					}
					else
					{
						m_iCurrentIndex = (m_iCurrentIndex + 1) % iMaxIndex;
					}

					bool bLoopAroundOnce = false;
					for ( ; m_iCurrentIndex < iMaxIndex; )
					{
						if ( m_hLocalConsoleCommandLibrary[m_iCurrentIndex].StartsWith( m_sAutocompleteInput ) )
						{
							m_sInput = m_hLocalConsoleCommandLibrary[m_iCurrentIndex];
							RedrawInputLine();

							break;
						}

						if ( hEvent.dwControlKeyState & SHIFT_PRESSED )
						{
							--m_iCurrentIndex;
						}
						else
						{
							++m_iCurrentIndex;
						}
						
						if ( !bLoopAroundOnce && m_iCurrentIndex > iMaxIndex )
						{
							// Reverse order, loop around to the end
							bLoopAroundOnce = true;
							m_iCurrentIndex = iMaxIndex - 1;
						}
						else if ( !bLoopAroundOnce && m_iCurrentIndex == iMaxIndex )
						{
							// Loop around to the start
							bLoopAroundOnce = true;
							m_iCurrentIndex = 0;
						}
					}
				}
				else if( hEvent.wVirtualKeyCode == VK_UP )
				{
					FScopeLock hLock( &m_hLock );

					if( m_hCommandHistory.Num() == 0 ) return;

					if( m_iCommandHistoryIndex == -1 ) m_iCommandHistoryIndex = m_hCommandHistory.Num() - 1;
					else --m_iCommandHistoryIndex;

					if( m_iCommandHistoryIndex < 0 ) m_iCommandHistoryIndex = 0;

					if( ( m_iCommandHistoryIndex + 1 ) == m_hCommandHistory.Num() && m_sUserInput.Len() == 0 ) m_sUserInput = m_sInput;

					m_sInput = m_hCommandHistory[m_iCommandHistoryIndex];

					RedrawInputLine();
				}
				else if( hEvent.wVirtualKeyCode == VK_DOWN )
				{
					FScopeLock hLock( &m_hLock );

					if( m_iCommandHistoryIndex == -1 ) return;

					++m_iCommandHistoryIndex;

					if( m_iCommandHistoryIndex > m_hCommandHistory.Num() ) m_iCommandHistoryIndex = m_hCommandHistory.Num();

					if( m_iCommandHistoryIndex == m_hCommandHistory.Num() )
					{
						if( m_sUserInput.Len() > 0 ) m_sInput = m_sUserInput;
						else m_sInput.Empty();

						RedrawInputLine();
					}
					else
					{
						m_sInput = m_hCommandHistory[m_iCommandHistoryIndex];

						RedrawInputLine();
					}
				}
				else if( hEvent.wVirtualKeyCode == VK_LEFT )
				{
					FScopeLock hLock( &m_hLock );

					COORD hCursorPosition( GetCursorPosition() );

					if( hCursorPosition.X > 0 )
					{
						--hCursorPosition.X;
						::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
					}
				}
				else if( hEvent.wVirtualKeyCode == VK_RIGHT )
				{
					FScopeLock hLock( &m_hLock );

					COORD hCursorPosition( GetCursorPosition() );

					if( hCursorPosition.X < m_sInput.Len() )
					{
						++hCursorPosition.X;
						::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
					}
				}
				else if( hEvent.wVirtualKeyCode == VK_HOME )
				{
					FScopeLock hLock( &m_hLock );

					COORD hCursorPosition( GetCursorPosition() );
					hCursorPosition.X = 0;
					::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
				}
				else if( hEvent.wVirtualKeyCode == VK_END )
				{
					FScopeLock hLock( &m_hLock );

					COORD hCursorPosition( GetCursorPosition() );
					hCursorPosition.X = m_sInput.Len();
					::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
				}
				else if( hEvent.wVirtualKeyCode == VK_DELETE )
				{
					FScopeLock hLock( &m_hLock );

					COORD hCursorPosition( GetCursorPosition() );

					if( hCursorPosition.X <= m_sInput.Len() )
					{
						m_sInput.RemoveAt( hCursorPosition.X );
						m_sInput.AppendChar( ' ' );

						RedrawInputLine();

						m_sInput.RemoveAt( m_sInput.Len() - 1 );

						::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
					}
				}
				else if( hEvent.uChar.UnicodeChar != '\0' )
				{
					FScopeLock hLock( &m_hLock );

					m_sInput.AppendChar( hEvent.uChar.UnicodeChar );

					RedrawInputLine();
				}
			}
		}
	}

	void FServerConsole::SendNullInput()
	{
		if( m_hInputHandle != INVALID_HANDLE_VALUE )
		{
			unsigned long iEventsWritten;
			INPUT_RECORD hInputEvents[1];

			hInputEvents[0].EventType = KEY_EVENT;
			hInputEvents[0].Event.KeyEvent.bKeyDown = true;
			hInputEvents[0].Event.KeyEvent.dwControlKeyState = 0;
			hInputEvents[0].Event.KeyEvent.uChar.UnicodeChar = '\0';
			hInputEvents[0].Event.KeyEvent.wRepeatCount = 1;
			hInputEvents[0].Event.KeyEvent.wVirtualKeyCode = 0;
			hInputEvents[0].Event.KeyEvent.wVirtualScanCode = 0;

			::WriteConsoleInput( m_hInputHandle, hInputEvents, 1, &iEventsWritten );
		}
	}

	void FServerConsole::ClearInputLine()
	{
		FScopeLock hLock( &m_hLock );

		COORD hCursorPosition( GetCursorPosition() );

		if( hCursorPosition.X == 0 ) return;

		TCHAR sOutput[MAX_SPRINTF] = TEXT( "" );
		for( int i = 0; i <= hCursorPosition.X; ++i ) sOutput[i] = ' ';

		hCursorPosition.X = 0;
		::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );

		m_pConsole->SetColor( COLOR_NONE );

		unsigned long iCharsWritten;
		::WriteConsole( m_hOutputHandle, sOutput, FCString::Strlen( sOutput ), &iCharsWritten, NULL );

		hCursorPosition.X = 0;
		::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );
	}

	void FServerConsole::RedrawInputLine()
	{
		ClearInputLine();

		if( m_hOutputHandle != INVALID_HANDLE_VALUE && !m_sInput.IsEmpty() )
		{
			FScopeLock hLock( &m_hLock );

			m_pConsole->SetColor( COLOR_GREEN );

			unsigned long iCharsWritten;
			::WriteConsole( m_hOutputHandle, *m_sInput, m_sInput.Len(), &iCharsWritten, NULL );
		}
	}

	COORD FServerConsole::GetCursorPosition()
	{
		COORD hCursorPosition = {};
		if( m_hOutputHandle != INVALID_HANDLE_VALUE )
		{
			CONSOLE_SCREEN_BUFFER_INFO hConsoleInfo;

			if( ::GetConsoleScreenBufferInfo( m_hOutputHandle, &hConsoleInfo ) ) hCursorPosition = hConsoleInfo.dwCursorPosition;
		}

		return hCursorPosition;
	}

	bool FServerConsole::SetCursorPosition( COORD hCursorPosition )
	{
		if( m_hOutputHandle != INVALID_HANDLE_VALUE ) return !!::SetConsoleCursorPosition( m_hOutputHandle, hCursorPosition );

		return false;
	}
#endif
