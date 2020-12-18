/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "SDL.h"

#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>

#include "input.h"


// this header is protected so you don't include it from anywere
#define KEYS_INCLUDE
#include "keynames.h"
#undef KEYS_INCLUDE

// support older SDL version (pre 2.0.6)
#ifndef SDL_JOYSTICK_AXIS_MIN
	#define SDL_JOYSTICK_AXIS_MIN -32768
#endif
#ifndef SDL_JOYSTICK_AXIS_MAX
	#define SDL_JOYSTICK_AXIS_MAX 32767
#endif

// for platform specific features that aren't available or is broken in SDL
#include "SDL_syswm.h"

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <imm.h>
#endif

void CInput::AddEvent(char *pText, int Key, int Flags)
{
	if(m_NumEvents != INPUT_BUFFER_SIZE)
	{
		m_aInputEvents[m_NumEvents].m_Key = Key;
		m_aInputEvents[m_NumEvents].m_Flags = Flags;
		if(!pText)
			m_aInputEvents[m_NumEvents].m_aText[0] = 0;
		else
			str_copy(m_aInputEvents[m_NumEvents].m_aText, pText, sizeof(m_aInputEvents[m_NumEvents].m_aText));
		m_aInputEvents[m_NumEvents].m_InputCount = m_InputCounter;
		m_NumEvents++;
	}
}

CInput::CInput()
{
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
	mem_zero(m_aInputState, sizeof(m_aInputState));

	m_pConfig = 0;
	m_pConsole = 0;
	m_pGraphics = 0;

	m_InputCounter = 1;
	m_MouseInputRelative = false;
	m_pClipboardText = 0;

	m_SelectedJoystickIndex = -1;
	m_aSelectedJoystickGUID[0] = '\0';

	m_PreviousHat = 0;

	m_MouseDoubleClick = false;

	m_NumEvents = 0;
	
	m_CompositionLength = COMP_LENGTH_INACTIVE;
	m_CompositionCursor = 0;
	m_CompositionSelectedLength = 0;
	m_CandidateCount = 0;
	m_CandidateSelectedIndex = -1;
}

CInput::~CInput()
{
	if(m_pClipboardText)
	{
		SDL_free(m_pClipboardText);
	}
	CloseJoysticks();
}

void CInput::Init()
{
	// enable system messages
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
	SDL_StopTextInput();

	m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	MouseModeRelative();

	InitJoysticks();
}

void CInput::InitJoysticks()
{
	if(!SDL_WasInit(SDL_INIT_JOYSTICK))
	{
		if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0)
		{
			dbg_msg("joystick", "unable to init SDL joystick: %s", SDL_GetError());
			return;
		}
	}

	int NumJoysticks = SDL_NumJoysticks();
	if(NumJoysticks > 0)
	{
		dbg_msg("joystick", "%d joystick(s) found", NumJoysticks);

		for(int i = 0; i < NumJoysticks; i++)
		{
			SDL_Joystick *pJoystick = SDL_JoystickOpen(i);
			if(!pJoystick)
			{
				dbg_msg("joystick", "Could not open joystick %d: %s", i, SDL_GetError());
				continue;
			}
			m_apJoysticks.add(pJoystick);

			dbg_msg("joystick", "Opened Joystick %d", i);
			dbg_msg("joystick", "Name: %s", SDL_JoystickNameForIndex(i));
			dbg_msg("joystick", "Number of Axes: %d", SDL_JoystickNumAxes(pJoystick));
			dbg_msg("joystick", "Number of Buttons: %d", SDL_JoystickNumButtons(pJoystick));
			dbg_msg("joystick", "Number of Balls: %d", SDL_JoystickNumBalls(pJoystick));
		}
	}
	else
	{
		dbg_msg("joystick", "No joysticks found");
	}
}

SDL_Joystick* CInput::GetActiveJoystick()
{
	if(m_apJoysticks.size() == 0)
	{
		return NULL;
	}
	if(m_aSelectedJoystickGUID[0] && str_comp(m_aSelectedJoystickGUID, m_pConfig->m_JoystickGUID) != 0)
	{
		// Refresh if cached GUID differs from configured GUID
		m_SelectedJoystickIndex = -1;
	}
	if(m_SelectedJoystickIndex == -1)
	{
		for(int i = 0; i < m_apJoysticks.size(); i++)
		{
			char aGUID[sizeof(m_aSelectedJoystickGUID)];
			SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(m_apJoysticks[i]), aGUID, sizeof(aGUID));
			if(str_comp(m_pConfig->m_JoystickGUID, aGUID) == 0)
			{
				m_SelectedJoystickIndex = i;
				str_copy(m_aSelectedJoystickGUID, m_pConfig->m_JoystickGUID, sizeof(m_aSelectedJoystickGUID));
				break;
			}
		}
		// could not find configured joystick, falling back to first available
		if(m_SelectedJoystickIndex == -1)
		{
			m_SelectedJoystickIndex = 0;
			SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(m_apJoysticks[0]), m_pConfig->m_JoystickGUID, sizeof(m_pConfig->m_JoystickGUID));
			str_copy(m_aSelectedJoystickGUID, m_pConfig->m_JoystickGUID, sizeof(m_aSelectedJoystickGUID));
		}
	}
	return m_apJoysticks[m_SelectedJoystickIndex];
}

void CInput::CloseJoysticks()
{
	for(sorted_array<SDL_Joystick*>::range r = m_apJoysticks.all(); !r.empty(); r.pop_front())
	{
		if (SDL_JoystickGetAttached(r.front()))
		{
			SDL_JoystickClose(r.front());
		}
	}
	m_apJoysticks.clear();
}

void CInput::SelectNextJoystick()
{
	const int Num = m_apJoysticks.size();
	if(Num > 1)
	{
		const int NextIndex = (m_SelectedJoystickIndex + 1) % Num;
		SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(m_apJoysticks[NextIndex]), m_pConfig->m_JoystickGUID, sizeof(m_pConfig->m_JoystickGUID));
	}
}

const char* CInput::GetJoystickName()
{
	SDL_Joystick* pJoystick = GetActiveJoystick();
	dbg_assert((bool)pJoystick, "Requesting joystick name, but no joysticks were initialized");
	return SDL_JoystickName(pJoystick);
}

float CInput::GetJoystickAxisValue(int Axis)
{
	SDL_Joystick* pJoystick = GetActiveJoystick();
	dbg_assert((bool)pJoystick, "Requesting joystick axis value, but no joysticks were initialized");
	return (SDL_JoystickGetAxis(pJoystick, Axis)-SDL_JOYSTICK_AXIS_MIN)/float(SDL_JOYSTICK_AXIS_MAX-SDL_JOYSTICK_AXIS_MIN)*2.0f - 1.0f;
}

int CInput::GetJoystickNumAxes()
{
	SDL_Joystick* pJoystick = GetActiveJoystick();
	dbg_assert((bool)pJoystick, "Requesting joystick axes count, but no joysticks were initialized");
	return SDL_JoystickNumAxes(pJoystick);
}

bool CInput::JoystickRelative(float *pX, float *pY)
{
	if(!m_MouseInputRelative)
		return false;

	if(m_pConfig->m_JoystickEnable && GetActiveJoystick())
	{
		const vec2 RawJoystickPos = vec2(GetJoystickAxisValue(m_pConfig->m_JoystickX), GetJoystickAxisValue(m_pConfig->m_JoystickY));
		const float Len = length(RawJoystickPos);
		const float DeadZone = m_pConfig->m_JoystickTolerance/50.0f;
		if(Len > DeadZone)
		{
			const float Factor = 0.1f * max((Len - DeadZone) / (1 - DeadZone), 0.001f) / Len;
			*pX = RawJoystickPos.x * Factor;
			*pY = RawJoystickPos.y * Factor;
			return true;
		}
	}
	return false;
}

bool CInput::JoystickAbsolute(float *pX, float *pY)
{
	if(m_pConfig->m_JoystickEnable && GetActiveJoystick())
	{
		const vec2 RawJoystickPos = vec2(GetJoystickAxisValue(m_pConfig->m_JoystickX), GetJoystickAxisValue(m_pConfig->m_JoystickY));
		const float DeadZone = m_pConfig->m_JoystickTolerance/50.0f;
		if(length(RawJoystickPos) > DeadZone)
		{
			*pX = RawJoystickPos.x;
			*pY = RawJoystickPos.y;
			return true;
		}
	}
	return false;
}

bool CInput::MouseRelative(float *pX, float *pY)
{
	if(!m_MouseInputRelative)
		return false;

	int MouseX = 0, MouseY = 0;
	SDL_GetRelativeMouseState(&MouseX, &MouseY);
	if(MouseX || MouseY)
	{
		*pX = MouseX;
		*pY = MouseY;
		return true;
	}
	return false;
}

void CInput::MouseModeAbsolute()
{
	if(m_MouseInputRelative)
	{
		m_MouseInputRelative = false;
		SDL_ShowCursor(SDL_ENABLE);
		SDL_SetRelativeMouseMode(SDL_FALSE);
	}
}

void CInput::MouseModeRelative()
{
	if(!m_MouseInputRelative)
	{
		m_MouseInputRelative = true;
		SDL_ShowCursor(SDL_DISABLE);
		if(SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_MODE_WARP, m_pConfig->m_InpGrab ? "0" : "1", SDL_HINT_OVERRIDE) == SDL_FALSE)
		{
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "input", "unable to switch relative mouse mode");
		}
		SDL_SetRelativeMouseMode(SDL_TRUE);
		SDL_GetRelativeMouseState(NULL, NULL);
	}
}

int CInput::MouseDoubleClick()
{
	if(m_MouseDoubleClick)
	{
		m_MouseDoubleClick = false;
		return 1;
	}
	return 0;
}

const char *CInput::GetClipboardText()
{
	if(m_pClipboardText)
	{
		SDL_free(m_pClipboardText);
	}
	m_pClipboardText = SDL_GetClipboardText();
	if(m_pClipboardText)
		str_sanitize_cc(m_pClipboardText);
	return m_pClipboardText;
}

void CInput::SetClipboardText(const char *pText)
{
	SDL_SetClipboardText(pText);
}

void CInput::StartTextInput()
{
	SDL_StartTextInput();
}

void CInput::StopTextInput()
{
	SDL_StopTextInput();
	m_CompositionLength = COMP_LENGTH_INACTIVE;
	m_CompositionCursor = 0;
	m_aComposition[0] = 0;
	m_CompositionSelectedLength = 0;
	m_CandidateCount = 0;
}

void CInput::Clear()
{
	mem_zero(m_aInputState, sizeof(m_aInputState));
	mem_zero(m_aInputCount, sizeof(m_aInputCount));
	m_NumEvents = 0;
}

bool CInput::KeyState(int Key) const
{
	return m_aInputState[Key>=KEY_MOUSE_1 ? Key : SDL_GetScancodeFromKey(KeyToKeycode(Key))];
}

void CInput::SetCompositionWindowPosition(float X, float Y)
{
	SDL_Rect Rect;
	Rect.x = X;
	Rect.y = Y;
	Rect.h = Graphics()->ScreenHeight() / 2;  // unused by SDL2
	Rect.w = Graphics()->ScreenWidth();	      // unused by SDL2

	// TODO: use window coordinate instead of canvas coordinate (requires #2827)
	SDL_SetTextInputRect(&Rect);
}

int CInput::Update()
{
	// keep the counter between 1..0xFFFF, 0 means not pressed
	m_InputCounter = (m_InputCounter%0xFFFF)+1;

	{
		int i;
		const Uint8 *pState = SDL_GetKeyboardState(&i);
		if(i >= KEY_LAST)
			i = KEY_LAST-1;
		mem_copy(m_aInputState, pState, i);
	}

	// these states must always be updated manually because they are not in the GetKeyState from SDL
	int i = SDL_GetMouseState(NULL, NULL);
	if(i&SDL_BUTTON(1)) m_aInputState[KEY_MOUSE_1] = 1; // 1 is left
	if(i&SDL_BUTTON(3)) m_aInputState[KEY_MOUSE_2] = 1; // 3 is right
	if(i&SDL_BUTTON(2)) m_aInputState[KEY_MOUSE_3] = 1; // 2 is middle
	if(i&SDL_BUTTON(4)) m_aInputState[KEY_MOUSE_4] = 1;
	if(i&SDL_BUTTON(5)) m_aInputState[KEY_MOUSE_5] = 1;
	if(i&SDL_BUTTON(6)) m_aInputState[KEY_MOUSE_6] = 1;
	if(i&SDL_BUTTON(7)) m_aInputState[KEY_MOUSE_7] = 1;
	if(i&SDL_BUTTON(8)) m_aInputState[KEY_MOUSE_8] = 1;
	if(i&SDL_BUTTON(9)) m_aInputState[KEY_MOUSE_9] = 1;

	{
		SDL_Event Event;

		while(SDL_PollEvent(&Event))
		{
			int Key = -1;
			int Scancode = 0;
			int Action = IInput::FLAG_PRESS;
			switch(Event.type)
			{
				// handle text editing candidate
				case SDL_SYSWMEVENT:
					ProcessSystemMessage(Event.syswm.msg);
					break;

				// handle on the spot text editing
				case SDL_TEXTEDITING:
				{
					m_CompositionLength = str_length(Event.edit.text);
					if(m_CompositionLength)
					{
						str_copy(m_aComposition, Event.edit.text, sizeof(m_aComposition));
						m_CompositionCursor = 0;
						for(int i = 0; i < Event.edit.start; i++)
							m_CompositionCursor = str_utf8_forward(m_aComposition, m_CompositionCursor);
						int m_CompositionEnd = m_CompositionCursor;
						for(int i = 0; i < Event.edit.length; i++)
							m_CompositionEnd = str_utf8_forward(m_aComposition, m_CompositionEnd);
						m_CompositionSelectedLength = m_CompositionEnd - m_CompositionCursor;
						AddEvent(0, 0, IInput::FLAG_TEXT);
					}
					else
					{
						m_aComposition[0] = '\0';
						m_CompositionLength = 0;
						m_CompositionCursor = 0;
						m_CompositionSelectedLength = 0;
					}
					dbg_msg("text", "edit: %d, %d, %d", m_CompositionLength, m_CompositionCursor, m_CompositionSelectedLength);
					break;
				}
				case SDL_TEXTINPUT:
					m_aComposition[0] = 0;
					m_CompositionLength = COMP_LENGTH_INACTIVE;
					m_CompositionCursor = 0;
					m_CompositionSelectedLength = 0;
					AddEvent(Event.text.text, 0, IInput::FLAG_TEXT);
					break;

				// handle keys
				case SDL_KEYDOWN:
					Key = KeycodeToKey(Event.key.keysym.sym);
					Scancode = Event.key.keysym.scancode;
					break;
				case SDL_KEYUP:
					Action = IInput::FLAG_RELEASE;
					Key = KeycodeToKey(Event.key.keysym.sym);
					Scancode = Event.key.keysym.scancode;
					break;

				// handle the joystick events
				case SDL_JOYBUTTONUP:
					Action = IInput::FLAG_RELEASE;

					// fall through
				case SDL_JOYBUTTONDOWN:
					Key = Event.jbutton.button + KEY_JOYSTICK_BUTTON_0;
					Scancode = Key;
					break;

				case SDL_JOYHATMOTION:
					switch (Event.jhat.value) {
					case SDL_HAT_LEFTUP:
						Key = KEY_JOY_HAT_LEFTUP;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_UP:
						Key = KEY_JOY_HAT_UP;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_RIGHTUP:
						Key = KEY_JOY_HAT_RIGHTUP;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_LEFT:
						Key = KEY_JOY_HAT_LEFT;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_CENTERED:
						Action = IInput::FLAG_RELEASE;
						Key = m_PreviousHat;
						Scancode = m_PreviousHat;
						m_PreviousHat = 0;
						break;
					case SDL_HAT_RIGHT:
						Key = KEY_JOY_HAT_RIGHT;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_LEFTDOWN:
						Key = KEY_JOY_HAT_LEFTDOWN;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_DOWN:
						Key = KEY_JOY_HAT_DOWN;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					case SDL_HAT_RIGHTDOWN:
						Key = KEY_JOY_HAT_RIGHTDOWN;
						Scancode = Key;
						m_PreviousHat = Key;
						break;
					}
					break;

				// handle mouse buttons
				case SDL_MOUSEBUTTONUP:
					Action = IInput::FLAG_RELEASE;

					// fall through
				case SDL_MOUSEBUTTONDOWN:
					if(Event.button.button == SDL_BUTTON_LEFT) Key = KEY_MOUSE_1; // ignore_convention
					if(Event.button.button == SDL_BUTTON_RIGHT) Key = KEY_MOUSE_2; // ignore_convention
					if(Event.button.button == SDL_BUTTON_MIDDLE) Key = KEY_MOUSE_3; // ignore_convention
					if(Event.button.button == 4) Key = KEY_MOUSE_4; // ignore_convention
					if(Event.button.button == 5) Key = KEY_MOUSE_5; // ignore_convention
					if(Event.button.button == 6) Key = KEY_MOUSE_6; // ignore_convention
					if(Event.button.button == 7) Key = KEY_MOUSE_7; // ignore_convention
					if(Event.button.button == 8) Key = KEY_MOUSE_8; // ignore_convention
					if(Event.button.button == 9) Key = KEY_MOUSE_9; // ignore_convention
					if(Event.button.button == SDL_BUTTON_LEFT)
					{
						if(Event.button.clicks%2 == 0)
							m_MouseDoubleClick = true;
						if(Event.button.clicks == 1)
							m_MouseDoubleClick = false;
					}
					Scancode = Key;
					break;

				case SDL_MOUSEWHEEL:
					if(Event.wheel.y > 0) Key = KEY_MOUSE_WHEEL_UP; // ignore_convention
					if(Event.wheel.y < 0) Key = KEY_MOUSE_WHEEL_DOWN; // ignore_convention
					Action |= IInput::FLAG_RELEASE;
					break;

#if defined(CONF_PLATFORM_MACOSX)	// Todo SDL: remove this when fixed (mouse state is faulty on start)
				case SDL_WINDOWEVENT:
					if(Event.window.event == SDL_WINDOWEVENT_MAXIMIZED)
					{
						MouseModeAbsolute();
						MouseModeRelative();
					}
					break;
#endif

				// other messages
				case SDL_QUIT:
					return 1;
			}

			if(Key != -1 && !HasComposition())
			{
				if(Action&IInput::FLAG_PRESS)
				{
					m_aInputState[Scancode] = 1;
					m_aInputCount[Key] = m_InputCounter;
				}
				AddEvent(0, Key, Action);
			}
		}
	}

	if(m_CompositionLength == 0) {
		m_CompositionLength = COMP_LENGTH_INACTIVE;
	}

	return 0;
}

void CInput::ProcessSystemMessage(SDL_SysWMmsg *pMsg)
{
#if defined(CONF_FAMILY_WINDOWS)
	// Todo SDL: remove this after SDL2 supports IME candidates
	if(pMsg->subsystem == SDL_SYSWM_WINDOWS)
	{
		if(pMsg->msg.win.msg != WM_IME_NOTIFY)
			return;

		switch(pMsg->msg.win.wParam)
		{
			case IMN_OPENCANDIDATE:
			case IMN_CHANGECANDIDATE:
			{
				HWND WindowHandle = pMsg->msg.win.hwnd;
				DWORD CandidateCount;
				HIMC ImeContext = ImmGetContext(WindowHandle);
				DWORD Size = ImmGetCandidateListCountW(ImeContext, &CandidateCount);
				LPCANDIDATELIST CandidateList = NULL;
				if(Size > 0)
				{
					CandidateList = (LPCANDIDATELIST)mem_alloc(Size, 1);
					Size = ImmGetCandidateListW(ImeContext, 0, CandidateList, Size);
				}
				if(CandidateList && Size > 0)
				{
					m_CandidateCount = 0;
					for (DWORD i = CandidateList->dwPageStart; i < CandidateList->dwCount && m_CandidateCount < (int)CandidateList->dwPageSize; i++)
					{
						LPCWSTR Candidate = (LPCWSTR)((DWORD_PTR)CandidateList + CandidateList->dwOffset[i]);
						WideCharToMultiByte(CP_UTF8, 0, Candidate, -1, m_aaCandidates[m_CandidateCount], MAX_CANDIDATE_ARRAY_SIZE, "?", NULL);
						m_aaCandidates[m_CandidateCount][MAX_CANDIDATE_ARRAY_SIZE-1] = '\0';
						m_CandidateCount++;
					}
					m_CandidateSelectedIndex = CandidateList->dwSelection - CandidateList->dwPageStart;
				}
				else
				{
					m_CandidateCount = 0;
					m_CandidateSelectedIndex = -1;
				}

				if(CandidateList)
					mem_free(CandidateList);
				ImmReleaseContext(WindowHandle, ImeContext);
				break;	
			}
			case IMN_CLOSECANDIDATE:
				m_CandidateCount = 0;
				m_CandidateSelectedIndex = -1;
				break;
		}
	}
#endif
}

IEngineInput *CreateEngineInput() { return new CInput; }
