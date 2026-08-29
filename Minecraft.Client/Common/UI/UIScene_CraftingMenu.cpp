#include "stdafx.h"
#include "UI.h"
#include "..\..\Minecraft.h"
#include "..\..\MultiplayerLocalPlayer.h"
#include "..\..\..\Minecraft.World\net.minecraft.world.inventory.h"
#include "UIScene_CraftingMenu.h"
#ifdef _UI_MOUSE_POINTER
#include "..\..\Windows64\Win64KeyboardMouse.h"
#endif

#ifdef __PSVITA__
#define GAME_CRAFTING_TOUCHUPDATE_TIMER_ID 0
#define GAME_CRAFTING_TOUCHUPDATE_TIMER_TIME 100
#endif

UIScene_CraftingMenu::UIScene_CraftingMenu(int iPad, void *_initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	m_bIgnoreKeyPresses = false;

	CraftingPanelScreenInput* initData = (CraftingPanelScreenInput*)_initData;
	m_iContainerType=initData->iContainerType;
	m_pPlayer=initData->player;
	m_bSplitscreen=initData->bSplitscreen;

	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	for(unsigned int i = 0; i < 4; ++i)	m_labelIngredientsDesc[i].init(L"");
	m_labelDescription.init(L"");
	m_labelGroupName.init(L"");
	m_labelItemName.init(L"");
	m_labelInventory.init( app.GetString(IDS_INVENTORY) );
	m_labelIngredients.init( app.GetString(IDS_INGREDIENTS) );

	if(m_iContainerType==RECIPE_TYPE_2x2)
	{
		m_menu = m_pPlayer->inventoryMenu;
		m_iMenuInventoryStart = InventoryMenu::INV_SLOT_START;
		m_iMenuHotBarStart = InventoryMenu::USE_ROW_SLOT_START;
	}
	else
	{
		CraftingMenu *menu = new CraftingMenu(m_pPlayer->inventory, m_pPlayer->level, initData->x, initData->y, initData->z);
		Minecraft::GetInstance()->localplayers[m_iPad]->containerMenu = menu;

		m_menu = menu;
		m_iMenuInventoryStart = CraftingMenu::INV_SLOT_START;
		m_iMenuHotBarStart = CraftingMenu::USE_ROW_SLOT_START;
	}
	m_slotListInventory.addSlots(CRAFTING_INVENTORY_SLOT_START,CRAFTING_INVENTORY_SLOT_END - CRAFTING_INVENTORY_SLOT_START);
	m_slotListHotBar.addSlots(CRAFTING_HOTBAR_SLOT_START, CRAFTING_HOTBAR_SLOT_END - CRAFTING_HOTBAR_SLOT_START);

#if TO_BE_IMPLEMENTED
	// if we are in splitscreen, then we need to figure out if we want to move this scene
	if(m_bSplitscreen)
	{
		app.AdjustSplitscreenScene(m_hObj,&m_OriginalPosition,m_iPad);	
	}

	XuiElementSetShow(m_hGrid,TRUE);
	XuiElementSetShow(m_hPanel,TRUE);
#endif

	if(m_iContainerType==RECIPE_TYPE_3x3)
	{
		m_iIngredientsMaxSlotC = m_iIngredients3x3SlotC;
		m_pGroupA=(Recipy::_eGroupType *)&m_GroupTypeMapping9GridA;
		m_pGroupTabA=(_eGroupTab *)&m_GroupTabBkgMapping3x3A;
		m_iCraftablesMaxHSlotC=m_iMaxHSlot3x3C;
	}
	else
	{
		m_iIngredientsMaxSlotC = m_iIngredients2x2SlotC;
		m_pGroupA=(Recipy::_eGroupType *)&m_GroupTypeMapping4GridA;
		m_pGroupTabA=(_eGroupTab *)&m_GroupTabBkgMapping2x2A;
		m_iCraftablesMaxHSlotC=m_iMaxHSlot2x2C;
	}

#if TO_BE_IMPLEMENTED


	// display the first group tab 
	m_hTabGroupA[m_iGroupIndex].SetShow(TRUE);

	// store the slot 0 position
	m_pHSlotsBrushImageControl[0]->GetPosition(&m_vSlot0Pos);
	m_pHSlotsBrushImageControl[1]->GetPosition(&vec);
	m_fSlotSize=vec.x-m_vSlot0Pos.x;

	// store the slot 0 highlight position
	m_hHighlight.GetPosition(&m_vSlot0HighlightPos);
	// Store the V slot position
	m_hScrollBar2.GetPosition(&m_vSlot0V2ScrollPos);
	m_hScrollBar3.GetPosition(&m_vSlot0V3ScrollPos);

	// get the position of the slot from the xui, and apply any offset needed
	for(int i=0;i<m_iCraftablesMaxHSlotC;i++)
	{
		m_pHSlotsBrushImageControl[i]->SetShow(FALSE);
	}

	XuiElementSetShow(m_hGridInventory,FALSE);

	m_hScrollBar2.SetShow(FALSE);
	m_hScrollBar3.SetShow(FALSE);

#endif

	app.SetRichPresenceContext(m_iPad,CONTEXT_GAME_STATE_CRAFTING);
	setGroupText(GetGroupNameText(m_pGroupA[m_iGroupIndex]));

	// Update the tutorial state
	Minecraft *pMinecraft = Minecraft::GetInstance();

	if( pMinecraft->localgameModes[m_iPad] != NULL )
	{
		TutorialMode *gameMode = (TutorialMode *)pMinecraft->localgameModes[m_iPad];
		m_previousTutorialState = gameMode->getTutorial()->getCurrentState();
		if(m_iContainerType==RECIPE_TYPE_2x2)
		{
			gameMode->getTutorial()->changeTutorialState(e_Tutorial_State_2x2Crafting_Menu, this);
		}
		else
		{
			gameMode->getTutorial()->changeTutorialState(e_Tutorial_State_3x3Crafting_Menu, this);
		}
	}

#ifdef _TO_BE_IMPLEMENTED
	XuiSetTimer(m_hObj,IGNORE_KEYPRESS_TIMERID,IGNORE_KEYPRESS_TIME);
#endif

	for(unsigned int i = 0; i < 4; ++i)
	{
		m_slotListIngredients[i].addSlot(CRAFTING_INGREDIENTS_DESCRIPTION_START + i);
	}
	m_slotListCraftingOutput.addSlot(CRAFTING_OUTPUT_SLOT_START);
	m_slotListIngredientsLayout.addSlots(CRAFTING_INGREDIENTS_LAYOUT_START, m_iIngredientsMaxSlotC);

	// 3 Slot vertical scroll
	m_slotListCrafting3VSlots[0].addSlot(CRAFTING_V_SLOT_START + 0);
	m_slotListCrafting3VSlots[1].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting3VSlots[2].addSlot(CRAFTING_V_SLOT_START + 2);

	// 2 Slot vertical scroll
	// 2 slot scroll has swapped order
	m_slotListCrafting2VSlots[0].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting2VSlots[1].addSlot(CRAFTING_V_SLOT_START + 0);

	// 1 Slot scroll (for 480 mainly)
	m_slotListCrafting1VSlots.addSlot(CRAFTING_V_SLOT_START);

	m_slotListCraftingHSlots.addSlots(CRAFTING_H_SLOT_START,m_iCraftablesMaxHSlotC);

	// Check which recipes are available with the resources we have
	CheckRecipesAvailable();
	// reset the vertical slots
	iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
	iVSlotIndexA[1]=0;
	iVSlotIndexA[2]=1;
	UpdateVerticalSlots();
	UpdateHighlight();

	if(initData) delete initData;

	// in this scene, we override the press sound with our own for crafting success or fail
	ui.OverrideSFX(m_iPad,ACTION_MENU_A,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,true);
#ifdef __ORBIS__
	ui.OverrideSFX(m_iPad,ACTION_MENU_TOUCHPAD_PRESS,true);
#endif
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,true);

	// 4J-PB - Must be after the CanBeMade list has been set up with CheckRecipesAvailable
	UpdateTooltips();

#ifdef __PSVITA__
	// initialise vita touch controls with ids
	for(unsigned int i = 0; i < ETouchInput_Count; ++i)
	{
		m_TouchInput[i].init(i);
	}
	ui.TouchBoxRebuild(this);
#endif
}

void UIScene_CraftingMenu::handleDestroy()
{
	Minecraft *pMinecraft = Minecraft::GetInstance();

	if( pMinecraft->localgameModes[m_iPad] != NULL )
	{
		TutorialMode *gameMode = (TutorialMode *)pMinecraft->localgameModes[m_iPad];
		if(gameMode != NULL) gameMode->getTutorial()->changeTutorialState(m_previousTutorialState);
	}

	// We need to make sure that we call closeContainer() anytime this menu is closed, even if it is forced to close by some other reason (like the player dying)	
	if(Minecraft::GetInstance()->localplayers[m_iPad] != NULL) Minecraft::GetInstance()->localplayers[m_iPad]->closeContainer();

	ui.OverrideSFX(m_iPad,ACTION_MENU_A,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,false);
#ifdef __ORBIS__
	ui.OverrideSFX(m_iPad,ACTION_MENU_TOUCHPAD_PRESS,false);
#endif
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,false);
}

EUIScene UIScene_CraftingMenu::getSceneType()
{
	if(m_iContainerType==RECIPE_TYPE_3x3)
	{
		return eUIScene_Crafting3x3Menu;
	}
	else
	{
		return eUIScene_Crafting2x2Menu;
	}
}

wstring UIScene_CraftingMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		m_bSplitscreen = true;
		if(m_iContainerType==RECIPE_TYPE_3x3)
		{
			return L"Crafting3x3MenuSplit";
		}
		else
		{
			return L"Crafting2x2MenuSplit";
		}
	}
	else
	{
		if(m_iContainerType==RECIPE_TYPE_3x3)
		{
			return L"Crafting3x3Menu";
		}
		else
		{
			return L"Crafting2x2Menu";
		}
	}
}

#ifdef _UI_POINTER_SUPPORT
UIControl* UIScene_CraftingMenu::GetMainPanel()
{
	return &m_controlMainPanel;
}
#endif

#ifdef __PSVITA__
void UIScene_CraftingMenu::handleTouchInput(unsigned int iPad, S32 x, S32 y, int iId, bool bPressed, bool bRepeat, bool bReleased)
{
	// perform action on release
	if(bPressed)
	{
		if(iId == ETouchInput_CraftingHSlots)
		{
			m_iCraftingSlotTouchStartY = y;
		}
	}
	else if(bRepeat)
	{
		if(iId == ETouchInput_CraftingHSlots)
		{
			if(y >= m_iCraftingSlotTouchStartY + m_TouchInput[ETouchInput_CraftingHSlots].getHeight())			// scroll list down
			{
				if(iVSlotIndexA[1]==(CanBeMadeA[m_iCurrentSlotHIndex].iCount-1))
				{
					iVSlotIndexA[1]=0;
				}
				else
				{
					iVSlotIndexA[1]++;
				}
				ui.PlayUISFX(eSFX_Focus);

				UpdateVerticalSlots();
				UpdateHighlight();

				m_iCraftingSlotTouchStartY = y;
			}
			else if(y <= m_iCraftingSlotTouchStartY - m_TouchInput[ETouchInput_CraftingHSlots].getHeight())		// scroll list up
			{
				if(iVSlotIndexA[1]==0)
				{
					iVSlotIndexA[1]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
				}
				else
				{
					iVSlotIndexA[1]--;
				}		
				ui.PlayUISFX(eSFX_Focus);

				UpdateVerticalSlots();
				UpdateHighlight();

				m_iCraftingSlotTouchStartY = y;
			}
		}
	}
	else if(bReleased)
	{
		if(iId >= ETouchInput_TouchPanel_0 && iId <= ETouchInput_TouchPanel_6)		// Touch Change Group
		{
			SelectGroup(iId);
		}
		else if(iId == ETouchInput_CraftingHSlots)									// Touch Change Slot
		{
			int iNewSlot = (x - m_TouchInput[ETouchInput_CraftingHSlots].getXPos() - m_controlMainPanel.getXPos()) / m_TouchInput[ETouchInput_CraftingHSlots].getHeight();

			SelectHSlot(iNewSlot);
		}
	}
}

void UIScene_CraftingMenu::handleTouchBoxRebuild()
{
	addTimer(GAME_CRAFTING_TOUCHUPDATE_TIMER_ID,GAME_CRAFTING_TOUCHUPDATE_TIMER_TIME);
}

void UIScene_CraftingMenu::handleTimerComplete(int id)
{
	if(id == GAME_CRAFTING_TOUCHUPDATE_TIMER_ID)
	{
		// we cannot rebuild touch boxes in an iggy callback because it requires further iggy calls
		GetMainPanel()->UpdateControl();
		ui.TouchBoxRebuild(this);
		killTimer(GAME_CRAFTING_TOUCHUPDATE_TIMER_ID);
	}
}
#endif

#ifdef _UI_POINTER_SUPPORT
// 4J Meow - lifted verbatim out of the Vita touch handler so the mouse can reach
// it too. Nothing here is new behaviour; it is what a tab press has always done.
void UIScene_CraftingMenu::SelectGroup(int iGroup)
{
	m_iGroupIndex = iGroup;
	// turn on the new group
	showTabHighlight(m_iGroupIndex,true);

	m_iCurrentSlotHIndex=0;
	m_iCurrentSlotVIndex=1;
	CheckRecipesAvailable();
	// reset the vertical slots
	iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
	iVSlotIndexA[1]=0;
	iVSlotIndexA[2]=1;
	ui.PlayUISFX(eSFX_Focus);
	UpdateVerticalSlots();
	UpdateHighlight();
	setGroupText(GetGroupNameText(m_pGroupA[m_iGroupIndex]));
}

void UIScene_CraftingMenu::SelectHSlot(int iSlot)
{
	int iOldHSlot=m_iCurrentSlotHIndex;

	m_iCurrentSlotHIndex = iSlot;
	if(m_iCurrentSlotHIndex>=m_iCraftablesMaxHSlotC) m_iCurrentSlotHIndex=0;
	m_iCurrentSlotVIndex=1;
	// clear the indices
	iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
	iVSlotIndexA[1]=0;
	iVSlotIndexA[2]=1;	

	UpdateVerticalSlots();
	UpdateHighlight();
	// re-enable the old hslot
	if(CanBeMadeA[iOldHSlot].iCount>0)
	{
		setShowCraftHSlot(iOldHSlot,true);
	}
	ui.PlayUISFX(eSFX_Focus);
}
#endif // _UI_POINTER_SUPPORT

#ifdef _UI_MOUSE_POINTER
// ---------------------------------------------------------------------------
// 4J Meow - clicking the crafting menu.
//
// This scene is not a container menu: it has no free pointer of its own, and
// none of its controls are the button-ish types UIController::TickMousePointer
// knows how to focus. So the mouse could do exactly one thing here - a left
// click arrived as ACTION_MENU_A, which is "craft the selected recipe" - and
// every attempt to click a tab crafted a plank instead.
//
// The Vita already had the answer: its touch handler picks a tab or a craftable
// from a screen position. That code is now SelectGroup/SelectHSlot and this
// does the hit-testing half of what the touchbox builder did for it.
//
// Crafting is left to ACTION_MENU_A from the keyboard or the pad - space, enter
// or A - which is what the owner asked for and what the button prompt says.
// ---------------------------------------------------------------------------

// Which tab is at (x,y)? Coordinates are in main-panel space. -1 for none.
int UIScene_CraftingMenu::TabIndexAt(S32 x, S32 y)
{
	// Bounds are only correct for the frame they were read on.
	m_controlCraftingTabs.UpdateControl();

	const int iTabC = (m_iContainerType==RECIPE_TYPE_3x3) ? m_iMaxGroup3x3 : m_iMaxGroup2x2;

	S32 iPitch  = m_controlCraftingTabs.getWidth();
	S32 iHeight = m_controlCraftingTabs.getHeight();
	if(iPitch <= 0 || iHeight <= 0) return -1;

	// CraftingTabs is one keyframe per tab - the clip holds a single tab-sized
	// child that the movie slides along as SetActiveTab changes frame - so the
	// width it reports is one tab, and the strip is that repeated iTabC times
	// from the clip's own origin, which does not move with the frame.
	//
	// Guarded rather than assumed: if some movie reports the whole strip
	// instead, that is a width of about the panel, and dividing it gets to the
	// same place.
	S32 iStrip = iPitch * iTabC;
	const S32 iPanelWidth = m_controlMainPanel.getWidth();
	if(iPanelWidth > 0 && iStrip > (iPanelWidth * 3) / 2)
	{
		iStrip = iPitch;
		iPitch = iStrip / iTabC;
		if(iPitch <= 0) return -1;
	}

	const S32 x0 = m_controlCraftingTabs.getXPos();
	const S32 y0 = m_controlCraftingTabs.getYPos();

	if(x < x0 || x >= x0 + iStrip)  return -1;
	if(y < y0 || y >= y0 + iHeight) return -1;

	int iTab = (x - x0) / iPitch;
	if(iTab < 0)       iTab = 0;
	if(iTab >= iTabC)  iTab = iTabC - 1;
	return iTab;
}

// Which craftable in the horizontal strip is at (x,y)? Main-panel space, -1 for
// none. The slots are square, so the pitch is the strip's height - the same
// thing the Vita touch handler divides by.
int UIScene_CraftingMenu::HSlotIndexAt(S32 x, S32 y)
{
	m_slotListCraftingHSlots.UpdateControl();

	const S32 x0 = m_slotListCraftingHSlots.getXPos();
	const S32 y0 = m_slotListCraftingHSlots.getYPos();
	const S32 w  = m_slotListCraftingHSlots.getWidth();
	const S32 h  = m_slotListCraftingHSlots.getHeight();
	if(w <= 0 || h <= 0) return -1;

	if(x < x0 || x >= x0 + w) return -1;
	if(y < y0 || y >= y0 + h) return -1;

	const int iSlot = (x - x0) / h;
	if(iSlot < 0 || iSlot >= m_iCraftablesMaxHSlotC) return -1;

	// An empty slot is not a craftable; leave the selection where it is rather
	// than jumping it onto a blank.
	if(CanBeMadeA[iSlot].iCount == 0) return -1;

	return iSlot;
}

bool UIScene_CraftingMenu::HandleMouseClick()
{
	if(!Win64Input::IsPointerActive()) return false;

	float fPointerX = 0.0f, fPointerY = 0.0f;
	if(!Win64Input::GetPointerPos(fPointerX, fPointerY)) return false;

	int iClientW = 0, iClientH = 0;
	if(!Win64Input::GetClientSize(iClientW, iClientH)) return false;
	if(iClientW <= 0 || iClientH <= 0) return false;

	const int iMovieW = getMovieWidth();
	const int iMovieH = getMovieHeight();
	if(iMovieW <= 0 || iMovieH <= 0) return false;

	// Client pixels -> the scene's authored space -> main-panel space, which is
	// what every control in this scene is positioned in.
	m_controlMainPanel.UpdateControl();

	const S32 x = (S32)(fPointerX * (float)iMovieW / (float)iClientW) - m_controlMainPanel.getXPos();
	const S32 y = (S32)(fPointerY * (float)iMovieH / (float)iClientH) - m_controlMainPanel.getYPos();

	const int iTab = TabIndexAt(x, y);
	if(iTab >= 0)
	{
		if(iTab != m_iGroupIndex) SelectGroup(iTab);
		return true;
	}

	const int iSlot = HSlotIndexAt(x, y);
	if(iSlot >= 0)
	{
		if(iSlot != m_iCurrentSlotHIndex) SelectHSlot(iSlot);
		return true;
	}

	return false;
}
#endif // _UI_MOUSE_POINTER

void UIScene_CraftingMenu::handleReload()
{
	m_slotListInventory.addSlots(CRAFTING_INVENTORY_SLOT_START,CRAFTING_INVENTORY_SLOT_END - CRAFTING_INVENTORY_SLOT_START);
	m_slotListHotBar.addSlots(CRAFTING_HOTBAR_SLOT_START, CRAFTING_HOTBAR_SLOT_END - CRAFTING_HOTBAR_SLOT_START);

	for(unsigned int i = 0; i < 4; ++i)
	{
		m_slotListIngredients[i].addSlot(CRAFTING_INGREDIENTS_DESCRIPTION_START + i);
	}
	m_slotListCraftingOutput.addSlot(CRAFTING_OUTPUT_SLOT_START);
	m_slotListIngredientsLayout.addSlots(CRAFTING_INGREDIENTS_LAYOUT_START, m_iIngredientsMaxSlotC);

	// 3 Slot vertical scroll
	m_slotListCrafting3VSlots[0].addSlot(CRAFTING_V_SLOT_START + 0);
	m_slotListCrafting3VSlots[1].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting3VSlots[2].addSlot(CRAFTING_V_SLOT_START + 2);

	// 2 Slot vertical scroll
	// 2 slot scroll has swapped order
	m_slotListCrafting2VSlots[0].addSlot(CRAFTING_V_SLOT_START + 1);
	m_slotListCrafting2VSlots[1].addSlot(CRAFTING_V_SLOT_START + 0);

	// 1 Slot scroll (for 480 mainly)
	m_slotListCrafting1VSlots.addSlot(CRAFTING_V_SLOT_START);

	m_slotListCraftingHSlots.addSlots(CRAFTING_H_SLOT_START,m_iCraftablesMaxHSlotC);

	app.DebugPrintf(app.USER_SR,"Reloading MultiPanel\n");
	int temp = m_iDisplayDescription;
	m_iDisplayDescription = m_iDisplayDescription==0?1:0;
	UpdateMultiPanel();
	m_iDisplayDescription = temp;
	UpdateMultiPanel();

	app.DebugPrintf(app.USER_SR,"Reloading Highlight and scroll\n");

	// reset the vertical slots
	m_iCurrentSlotHIndex = 0;
	m_iCurrentSlotVIndex = 1;
	iVSlotIndexA[0]=CanBeMadeA[m_iCurrentSlotHIndex].iCount-1;
	iVSlotIndexA[1]=0;
	iVSlotIndexA[2]=1;
	UpdateVerticalSlots();
	UpdateHighlight();

	app.DebugPrintf(app.USER_SR,"Reloading tabs\n");
	showTabHighlight(0,false);
	showTabHighlight(m_iGroupIndex,true);
}

void UIScene_CraftingMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if(pMinecraft->localplayers[m_iPad] == NULL || pMinecraft->localgameModes[m_iPad] == NULL) return;

	shared_ptr<ItemInstance> item = nullptr;
	int slotId = -1;
	float alpha = 1.0f;
	bool decorations = true;
	bool inventoryItem = false;
	swscanf((wchar_t*)region->name,L"slot_%d",&slotId);
	if (slotId == -1)
	{
		app.DebugPrintf("This is not the control we are looking for\n");
	}
	else if(slotId >= CRAFTING_INVENTORY_SLOT_START && slotId < CRAFTING_INVENTORY_SLOT_END)
	{
		int iIndex = slotId - CRAFTING_INVENTORY_SLOT_START;
		iIndex += m_iMenuInventoryStart;
		Slot *slot = m_menu->getSlot(iIndex);
		item = slot->getItem();
		inventoryItem = true;
	}
	else if(slotId >= CRAFTING_HOTBAR_SLOT_START && slotId < CRAFTING_HOTBAR_SLOT_END)
	{
		int iIndex = slotId - CRAFTING_HOTBAR_SLOT_START;
		iIndex += m_iMenuHotBarStart;
		Slot *slot = m_menu->getSlot(iIndex);
		item = slot->getItem();
		inventoryItem = true;
	}
	else if(slotId >= CRAFTING_V_SLOT_START && slotId < CRAFTING_V_SLOT_END )
	{
		decorations = false;
		int iIndex = slotId - CRAFTING_V_SLOT_START;
		if(m_vSlotsInfo[iIndex].show)
		{
			item = m_vSlotsInfo[iIndex].item;
			alpha = ((float)m_vSlotsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId >= CRAFTING_H_SLOT_START && slotId < (CRAFTING_H_SLOT_START + m_iCraftablesMaxHSlotC) )
	{
		decorations = false;
		int iIndex = slotId - CRAFTING_H_SLOT_START;
		if(m_hSlotsInfo[iIndex].show)
		{
			item = m_hSlotsInfo[iIndex].item;
			alpha = ((float)m_hSlotsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId >= CRAFTING_INGREDIENTS_LAYOUT_START && slotId < (CRAFTING_INGREDIENTS_LAYOUT_START + m_iIngredientsMaxSlotC) )
	{
		int iIndex = slotId - CRAFTING_INGREDIENTS_LAYOUT_START;
		if(m_ingredientsSlotsInfo[iIndex].show)
		{
			item = m_ingredientsSlotsInfo[iIndex].item;
			alpha = ((float)m_ingredientsSlotsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId >= CRAFTING_INGREDIENTS_DESCRIPTION_START && slotId < (CRAFTING_INGREDIENTS_DESCRIPTION_START + 4) )
	{
		int iIndex = slotId - CRAFTING_INGREDIENTS_DESCRIPTION_START;
		if(m_ingredientsInfo[iIndex].show)
		{
			item = m_ingredientsInfo[iIndex].item;
			alpha = ((float)m_ingredientsInfo[iIndex].alpha)/31.0f;
		}
	}
	else if(slotId == CRAFTING_OUTPUT_SLOT_START )
	{
		if(m_craftingOutputSlotInfo.show)
		{
			item = m_craftingOutputSlotInfo.item;
			alpha = ((float)m_craftingOutputSlotInfo.alpha)/31.0f;
		}
	}

	if(item != NULL)
	{
		if(!inventoryItem)
		{
			if( item->id == Item::clock_Id || item->id == Item::compass_Id )
			{
				// 4J Stu - For clocks and compasses we set the aux value to a special one that signals we should use a default texture
				// rather than the dynamic one for the player
				item->setAuxValue(0xFF);
			}
			else if( (item->getAuxValue() & 0xFF) == 0xFF)
			{
				// 4J Stu - If the aux value is set to match any
				item->setAuxValue(0);
			}
		}
		customDrawSlotControl(region,m_iPad,item,alpha,item->isFoil(),decorations);
	}
}

int UIScene_CraftingMenu::getPad()
{
	return m_iPad;
}

bool UIScene_CraftingMenu::allowRepeat(int key)
{
	switch(key)
	{
	// X is used to open this menu, so don't let it repeat
	case ACTION_MENU_X:
		return false;
	}
	return true;
}

void UIScene_CraftingMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	//app.DebugPrintf("UIScene_InventoryMenu handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
#ifdef _UI_MOUSE_POINTER
	// 4J Meow - a left click reaches here as ACTION_MENU_A (see
	// MouseKeyForMenuAction), and ACTION_MENU_A on this scene means craft. That
	// is wrong for a pointing device: a click is "pick the thing I am pointing
	// at". Crafting stays on the keyboard and the pad, where the button prompt
	// points - space, enter or A.
	//
	// Checked against the mouse button rather than the action so that space and
	// enter are untouched; they raise the same action but not this.
	if(pressed && !repeat && (key == ACTION_MENU_A) && Win64Input::IsMouseButtonDown(0))
	{
		HandleMouseClick();
		handled = true;
		return;
	}
#endif

	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	switch(key)
	{
	case ACTION_MENU_OTHER_STICK_UP:
	case ACTION_MENU_OTHER_STICK_DOWN:
		sendInputToMovie(key,repeat,pressed,released);
		break;
	default:
		if(pressed)
		{
			handled = handleKeyDown(m_iPad, key, repeat);
		}
		break;
	};
}

void UIScene_CraftingMenu::hideAllHSlots()
{
	for(unsigned int iIndex = 0; iIndex < m_iMaxHSlotC; ++iIndex)
	{
		m_hSlotsInfo[iIndex].item = nullptr;
		m_hSlotsInfo[iIndex].alpha = 31;
		m_hSlotsInfo[iIndex].show = false;
	}
}

void UIScene_CraftingMenu::hideAllVSlots()
{
	for(unsigned int iIndex = 0; iIndex < m_iMaxDisplayedVSlotC; ++iIndex)
	{
		m_vSlotsInfo[iIndex].item = nullptr;
		m_vSlotsInfo[iIndex].alpha = 31;
		m_vSlotsInfo[iIndex].show = false;
	}
}

void UIScene_CraftingMenu::hideAllIngredientsSlots()
{
	for(int i=0;i<m_iIngredientsC;i++)
	{
		m_ingredientsInfo[i].item = nullptr;
		m_ingredientsInfo[i].alpha = 31;
		m_ingredientsInfo[i].show = false;

		m_labelIngredientsDesc[i].setLabel(L"");

		IggyDataValue result;
		IggyDataValue value[2];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = i;

		value[1].type = IGGY_DATATYPE_boolean;
		value[1].boolval = false;
		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcShowIngredientSlot , 2 , value );
	}
}

void UIScene_CraftingMenu::setCraftHSlotItem(int iPad, int iIndex, shared_ptr<ItemInstance> item, unsigned int uiAlpha)
{
	m_hSlotsInfo[iIndex].item = item;
	m_hSlotsInfo[iIndex].alpha = uiAlpha;
	m_hSlotsInfo[iIndex].show = true;
}

void UIScene_CraftingMenu::setCraftVSlotItem(int iPad, int iIndex, shared_ptr<ItemInstance> item, unsigned int uiAlpha)
{
	m_vSlotsInfo[iIndex].item = item;
	m_vSlotsInfo[iIndex].alpha = uiAlpha;
	m_vSlotsInfo[iIndex].show = true;
}

void UIScene_CraftingMenu::setCraftingOutputSlotItem(int iPad, shared_ptr<ItemInstance> item)
{
	m_craftingOutputSlotInfo.item = item;
	m_craftingOutputSlotInfo.alpha = 31;
	m_craftingOutputSlotInfo.show = item != NULL;
}

void UIScene_CraftingMenu::setCraftingOutputSlotRedBox(bool show)
{
	m_slotListCraftingOutput.showSlotRedBox(0,show);
}

void UIScene_CraftingMenu::setIngredientSlotItem(int iPad, int index, shared_ptr<ItemInstance> item)
{
	m_ingredientsSlotsInfo[index].item = item;
	m_ingredientsSlotsInfo[index].alpha = 31;
	m_ingredientsSlotsInfo[index].show = item != NULL;
}

void UIScene_CraftingMenu::setIngredientSlotRedBox(int index, bool show)
{
	m_slotListIngredientsLayout.showSlotRedBox(index,show);
}

void UIScene_CraftingMenu::setIngredientDescriptionItem(int iPad, int index, shared_ptr<ItemInstance> item)
{
	m_ingredientsInfo[index].item = item;
	m_ingredientsInfo[index].alpha = 31;
	m_ingredientsInfo[index].show = item != NULL;

	IggyDataValue result;
	IggyDataValue value[2];

	value[0].type = IGGY_DATATYPE_number;
	value[0].number = index;

	value[1].type = IGGY_DATATYPE_boolean;
	value[1].boolval = m_ingredientsInfo[index].show;
	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcShowIngredientSlot , 2 , value );
}

void UIScene_CraftingMenu::setIngredientDescriptionRedBox(int index, bool show)
{
	m_slotListIngredients[index].showSlotRedBox(0,show);
}

void UIScene_CraftingMenu::setIngredientDescriptionText(int index, LPCWSTR text)
{
	m_labelIngredientsDesc[index].setLabel(text);
}


void UIScene_CraftingMenu::setShowCraftHSlot(int iIndex, bool show)
{
	m_hSlotsInfo[iIndex].show = show;
}

void UIScene_CraftingMenu::showTabHighlight(int iIndex, bool show)
{
	if(show)
	{	
		IggyDataValue result;
		IggyDataValue value[1];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = iIndex;
		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSetActiveTab , 1 , value );
	}
}

void UIScene_CraftingMenu::setGroupText(LPCWSTR text)
{
	m_labelGroupName.setLabel(text);
}

void UIScene_CraftingMenu::setDescriptionText(LPCWSTR text)
{
	m_labelDescription.setLabel(text);
}

void UIScene_CraftingMenu::setItemText(LPCWSTR text)
{
	m_labelItemName.setLabel(text);
}

void UIScene_CraftingMenu::UpdateMultiPanel()
{
	// Call Iggy function to show the current panel
	IggyDataValue result;
	IggyDataValue value[1];

	value[0].type = IGGY_DATATYPE_number;
	value[0].number = m_iDisplayDescription;

	IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcShowPanelDisplay , 1 , value );
}

void UIScene_CraftingMenu::scrollDescriptionUp()
{
	// handled differently
}

void UIScene_CraftingMenu::scrollDescriptionDown()
{
	// handled differently
}

void UIScene_CraftingMenu::updateHighlightAndScrollPositions()
{
	{	
		IggyDataValue result;
		IggyDataValue value[2];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = m_iCurrentSlotHIndex;

		int selectorType = 0;
		if(CanBeMadeA[m_iCurrentSlotHIndex].iCount  == 2)
		{
			selectorType = 1;
		}
		else if( CanBeMadeA[m_iCurrentSlotHIndex].iCount  > 2)
		{
			selectorType = 2;
		}

		value[1].type = IGGY_DATATYPE_number;
		value[1].number = selectorType;

		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcMoveSelector , 2 , value );
	}

	{
		IggyDataValue result;
		IggyDataValue value[1];

		value[0].type = IGGY_DATATYPE_number;
		value[0].number = m_iCurrentSlotVIndex;

		IggyResult out = IggyPlayerCallMethodRS ( getMovie() , &result, IggyPlayerRootPath( getMovie() ) , m_funcSelectVerticalItem , 1 , value );
	}
}

void UIScene_CraftingMenu::updateVSlotPositions(int iSlots, int i)
{
	// Not needed
}
