/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_UI_H
#define GAME_CLIENT_UI_H

#include "lineinput.h"

class CUIRect
{
	enum
	{
		NUM_ROUND_CORNER_SEGMENTS = 8
	};
	static class IGraphics *m_pGraphics;

public:
	static void Init(class IGraphics *pGraphics) { m_pGraphics = pGraphics; }

	float x, y, w, h;

	void HSplitMid(CUIRect *pTop, CUIRect *pBottom, float Spacing = 0.0f) const;
	void HSplitTop(float Cut, CUIRect *pTop, CUIRect *pBottom) const;
	void HSplitBottom(float Cut, CUIRect *pTop, CUIRect *pBottom) const;
	void VSplitMid(CUIRect *pLeft, CUIRect *pRight, float Spacing = 0.0f) const;
	void VSplitLeft(float Cut, CUIRect *pLeft, CUIRect *pRight) const;
	void VSplitRight(float Cut, CUIRect *pLeft, CUIRect *pRight) const;

	void Margin(float Cut, CUIRect *pOtherRect) const;
	void VMargin(float Cut, CUIRect *pOtherRect) const;
	void HMargin(float Cut, CUIRect *pOtherRect) const;

	bool Inside(float x, float y) const;

	enum
	{
		CORNER_NONE=0,
		CORNER_TL=1,
		CORNER_TR=2,
		CORNER_BL=4,
		CORNER_BR=8,
		CORNER_ITL=16,
		CORNER_ITR=32,
		CORNER_IBL=64,
		CORNER_IBR=128,

		CORNER_T=CORNER_TL|CORNER_TR,
		CORNER_B=CORNER_BL|CORNER_BR,
		CORNER_R=CORNER_TR|CORNER_BR,
		CORNER_L=CORNER_TL|CORNER_BL,

		CORNER_IT=CORNER_ITL|CORNER_ITR,
		CORNER_IB=CORNER_IBL|CORNER_IBR,
		CORNER_IR=CORNER_ITR|CORNER_IBR,
		CORNER_IL=CORNER_ITL|CORNER_IBL,

		CORNER_ALL=CORNER_T|CORNER_B,
		CORNER_INV_ALL=CORNER_IT|CORNER_IB
	};

	void Draw(const vec4 &Color, float Rounding = 5.0f, int Corners = CUIRect::CORNER_ALL) const;
	void Draw4(const vec4 &ColorTopLeft, const vec4 &ColorTopRight, const vec4 &ColorBottomLeft, const vec4 &ColorBottomRight, float Rounding = 5.0f, int Corners = CUIRect::CORNER_ALL) const;
};


class IScrollbarScale
{
public:
	virtual float ToRelative(int AbsoluteValue, int Min, int Max) = 0;
	virtual int ToAbsolute(float RelativeValue, int Min, int Max) = 0;
};
static class CLinearScrollbarScale : public IScrollbarScale
{
public:
	float ToRelative(int AbsoluteValue, int Min, int Max)
	{
		return (AbsoluteValue - Min) / (float)(Max - Min);
	}
	int ToAbsolute(float RelativeValue, int Min, int Max)
	{
		return round_to_int(RelativeValue*(Max - Min) + Min + 0.1f);
	}
} LinearScrollbarScale;
static class CLogarithmicScrollbarScale : public IScrollbarScale
{
private:
	int m_MinAdjustment;
public:
	CLogarithmicScrollbarScale(int MinAdjustment)
	{
		m_MinAdjustment = max(MinAdjustment, 1); // must be at least 1 to support Min == 0 with logarithm
	}
	float ToRelative(int AbsoluteValue, int Min, int Max)
	{
		if(Min < m_MinAdjustment)
		{
			AbsoluteValue += m_MinAdjustment;
			Min += m_MinAdjustment;
			Max += m_MinAdjustment;
		}
		return (log(AbsoluteValue) - log(Min)) / (float)(log(Max) - log(Min));
	}
	int ToAbsolute(float RelativeValue, int Min, int Max)
	{
		int ResultAdjustment = 0;
		if(Min < m_MinAdjustment)
		{
			Min += m_MinAdjustment;
			Max += m_MinAdjustment;
			ResultAdjustment = -m_MinAdjustment;
		}
		return round_to_int(exp(RelativeValue*(log(Max) - log(Min)) + log(Min))) + ResultAdjustment;
	}
} LogarithmicScrollbarScale(25);


class IButtonColorFunction
{
public:
	virtual vec4 GetColor(bool Active, bool Hovered) = 0;
};
static class CDarkButtonColorFunction : public IButtonColorFunction
{
public:
	vec4 GetColor(bool Active, bool Hovered)
	{
		if(Active)
			return vec4(0.15f, 0.15f, 0.15f, 0.25f);
		else if(Hovered)
			return vec4(0.5f, 0.5f, 0.5f, 0.25f);
		return vec4(0.0f, 0.0f, 0.0f, 0.25f);
	}
} DarkButtonColorFunction;
static class CLightButtonColorFunction : public IButtonColorFunction
{
public:
	vec4 GetColor(bool Active, bool Hovered)
	{
		if(Active)
			return vec4(1.0f, 1.0f, 1.0f, 0.4f);
		else if(Hovered)
			return vec4(1.0f, 1.0f, 1.0f, 0.6f);
		return vec4(1.0f, 1.0f, 1.0f, 0.5f);
	}
} LightButtonColorFunction;


class CUI
{
	enum
	{
		MAX_CLIP_NESTING_DEPTH = 16
	};

	bool m_Enabled;

	const void *m_pHotItem;
	const void *m_pActiveItem;
	const void *m_pLastActiveItem;
	const void *m_pBecommingHotItem;
	bool m_ActiveItemValid;

	float m_MouseX, m_MouseY; // in gui space
	float m_MouseWorldX, m_MouseWorldY; // in world space
	unsigned m_MouseButtons;
	unsigned m_LastMouseButtons;

	unsigned m_HotkeysPressed;
	CLineInput *m_pActiveInput;

	CUIRect m_Screen;

	CUIRect m_aClips[MAX_CLIP_NESTING_DEPTH];
	unsigned m_NumClips;
	void UpdateClipping();

	class CConfig *m_pConfig;
	class IGraphics *m_pGraphics;
	class IInput *m_pInput;
	class ITextRender *m_pTextRender;

public:
	static const vec4 ms_DefaultTextColor;
	static const vec4 ms_DefaultTextOutlineColor;
	static const vec4 ms_HighlightTextColor;
	static const vec4 ms_HighlightTextOutlineColor;
	static const vec4 ms_TransparentTextColor;

	static const float ms_ButtonHeight;
	static const float ms_ListheaderHeight;
	static const float ms_FontmodHeight;

	// TODO: Refactor: Fill this in
	void Init(class CConfig *pConfig, class IGraphics *pGraphics, class IInput *pInput, class ITextRender *pTextRender);
	class CConfig *Config() const { return m_pConfig; }
	class IGraphics *Graphics() const { return m_pGraphics; }
	class IInput *Input() const { return m_pInput; }
	class ITextRender *TextRender() const { return m_pTextRender; }

	CUI();

	enum EAlignment
	{
		ALIGN_LEFT,
		ALIGN_CENTER,
		ALIGN_RIGHT,
	};

	enum
	{
		HOTKEY_ENTER = 1,
		HOTKEY_ESCAPE = 2,
		HOTKEY_UP = 4,
		HOTKEY_DOWN = 8,
		HOTKEY_DELETE = 16,
		HOTKEY_TAB = 32,
	};

	void SetEnabled(bool Enabled) { m_Enabled = Enabled; }
	bool Enabled() const { return m_Enabled; }
	void Update(float MouseX, float MouseY, float MouseWorldX, float MouseWorldY);

	float MouseX() const { return m_MouseX; }
	float MouseY() const { return m_MouseY; }
	float MouseWorldX() const { return m_MouseWorldX; }
	float MouseWorldY() const { return m_MouseWorldY; }
	bool MouseButton(int Index) const { return (m_MouseButtons>>Index)&1; }
	bool MouseButtonClicked(int Index) const { return MouseButton(Index) && !((m_LastMouseButtons>>Index)&1) ; }

	void SetHotItem(const void *pID) { m_pBecommingHotItem = pID; }
	void SetActiveItem(const void *pID) { m_ActiveItemValid = true; m_pActiveItem = pID; if (pID) m_pLastActiveItem = pID; }
	bool CheckActiveItem(const void *pID) { if(m_pActiveItem == pID) { m_ActiveItemValid = true; return true; } return false; };
	void ClearLastActiveItem() { m_pLastActiveItem = 0; }
	const void *HotItem() const { return m_pHotItem; }
	const void *NextHotItem() const { return m_pBecommingHotItem; }
	const void *GetActiveItem() const { return m_pActiveItem; }
	const void *LastActiveItem() const { return m_pLastActiveItem; }

	void StartCheck() { m_ActiveItemValid = false; };
	void FinishCheck() { if(!m_ActiveItemValid) SetActiveItem(0); };

	bool MouseInside(const CUIRect *pRect) const { return pRect->Inside(m_MouseX, m_MouseY); };
	bool MouseInsideClip() const { return !IsClipped() || MouseInside(ClipArea()); };
	bool MouseHovered(const CUIRect *pRect) const { return MouseInside(pRect) && MouseInsideClip(); };
	void ConvertCursorMove(float *pX, float *pY, int CursorType) const;

	bool KeyPress(int Key) const;
	bool KeyIsPressed(int Key) const;
	bool ConsumeHotkey(unsigned Hotkey);
	void ClearHotkeys() { m_HotkeysPressed = 0; }
	bool OnInput(const IInput::CEvent &e);
	bool IsInputActive() const { return m_pActiveInput != 0; }

	const CUIRect *Screen();
	float PixelSize();

	// clipping
	void ClipEnable(const CUIRect *pRect);
	void ClipDisable();
	const CUIRect *ClipArea() const;
	inline bool IsClipped() const { return m_NumClips > 0; };

	bool DoButtonLogic(const void *pID, const CUIRect *pRect, int Button = 0);
	bool DoPickerLogic(const void *pID, const CUIRect *pRect, float *pX, float *pY);

	// labels
	void DoLabel(const CUIRect *pRect, const char *pText, float FontSize, EAlignment Align, float LineWidth = -1.0f, bool MultiLine = true);
	void DoLabelHighlighted(const CUIRect *pRect, const char *pText, const char *pHighlighted, float FontSize, const vec4 &TextColor, const vec4 &HighlightColor);

	// editboxes
	bool DoEditBox(CLineInput *pLineInput, const CUIRect *pRect, float FontSize, bool Hidden = false, int Corners = CUIRect::CORNER_ALL, IButtonColorFunction *pColorFunction = &DarkButtonColorFunction);
	void DoEditBoxOption(CLineInput *pLineInput, const CUIRect *pRect, const char *pStr, float VSplitVal, bool Hidden = false);

	// scrollbars
	float DoScrollbarV(const void *pID, const CUIRect *pRect, float Current);
	float DoScrollbarH(const void *pID, const CUIRect *pRect, float Current);
	void DoScrollbarOption(void *pID, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, IScrollbarScale *pScale = &LinearScrollbarScale, bool Infinite = false);
	void DoScrollbarOptionLabeled(void *pID, int *pOption, const CUIRect *pRect, const char *pStr, const char *apLabels[], int Num, IScrollbarScale *pScale = &LinearScrollbarScale);

	// client ID
	float DrawClientID(float FontSize, vec2 Position, int ID,
					const vec4& BgColor = vec4(1.0f, 1.0f, 1.0f, 0.5f), const vec4& TextColor = vec4(0.1f, 0.1f, 0.1f, 1.0f));
	float GetClientIDRectWidth(float FontSize);
};


#endif
