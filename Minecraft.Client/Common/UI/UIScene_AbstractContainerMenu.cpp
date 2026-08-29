#include "stdafx.h"
#include "UI.h"
#include "UIScene_AbstractContainerMenu.h"

#include "..\..\..\Minecraft.World\net.minecraft.world.inventory.h"
#include "..\..\..\Minecraft.World\net.minecraft.world.item.h"
#include "..\..\MultiplayerLocalPlayer.h"
#ifdef _UI_MOUSE_POINTER
#include "..\..\Windows64\Win64KeyboardMouse.h"
#endif

UIScene_AbstractContainerMenu::UIScene_AbstractContainerMenu(int iPad, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	m_focusSection = eSectionNone;
	// in this scene, we override the press sound with our own for crafting success or fail
	ui.OverrideSFX(m_iPad,ACTION_MENU_A,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,true);
#ifdef __ORBIS__
	ui.OverrideSFX(m_iPad,ACTION_MENU_TOUCHPAD_PRESS,true);
#endif
	ui.OverrideSFX(m_iPad,ACTION_MENU_X,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_Y,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,true);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,true);

	m_bIgnoreInput=false;
}

UIScene_AbstractContainerMenu::~UIScene_AbstractContainerMenu()
{
	app.DebugPrintf("UIScene_AbstractContainerMenu::~UIScene_AbstractContainerMenu\n");
}

void UIScene_AbstractContainerMenu::handleDestroy()
{
	app.DebugPrintf("UIScene_AbstractContainerMenu::handleDestroy\n");
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if( pMinecraft->localgameModes[m_iPad] != NULL )
	{
		TutorialMode *gameMode = (TutorialMode *)pMinecraft->localgameModes[m_iPad];
		if(gameMode != NULL) gameMode->getTutorial()->changeTutorialState(m_previousTutorialState);
	}

	// 4J Stu - Fix for #11302 - TCR 001: Network Connectivity: Host crashed after being killed by the client while accessing a chest during burst packet loss.
	// We need to make sure that we call closeContainer() anytime this menu is closed, even if it is forced to close by some other reason (like the player dying)	
	if(pMinecraft->localplayers[m_iPad] != NULL) pMinecraft->localplayers[m_iPad]->closeContainer();

	ui.OverrideSFX(m_iPad,ACTION_MENU_A,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_OK,false);
#ifdef __ORBIS__
	ui.OverrideSFX(m_iPad,ACTION_MENU_TOUCHPAD_PRESS,false);
#endif
	ui.OverrideSFX(m_iPad,ACTION_MENU_X,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_Y,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT_SCROLL,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_LEFT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_RIGHT,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_UP,false);
	ui.OverrideSFX(m_iPad,ACTION_MENU_DOWN,false);
}

void UIScene_AbstractContainerMenu::InitDataAssociations(int iPad, AbstractContainerMenu *menu, int startIndex)
{
}

void UIScene_AbstractContainerMenu::PlatformInitialize(int iPad, int startIndex)
{

	m_labelInventory.init( app.GetString(IDS_INVENTORY) );

	if(startIndex >= 0)
	{
		m_slotListInventory.addSlots(startIndex, 27);
		m_slotListHotbar.addSlots(startIndex + 27, 9);
	}

	// Determine min and max extents for pointer, it needs to be able to move off the container to drop items.
	float fPanelWidth, fPanelHeight;
	float fPanelX, fPanelY;
	float fPointerWidth, fPointerHeight;

	// We may have varying depths of controls here, so base off the pointers parent
#if TO_BE_IMPLEMENTED
	HXUIOBJ parent;
	XuiElementGetBounds( m_pointerControl->m_hObj, &fPointerWidth, &fPointerHeight );	
#else
	fPointerWidth = 50;
	fPointerHeight = 50;
#endif

	fPanelWidth = m_controlBackgroundPanel.getWidth();
	fPanelHeight = m_controlBackgroundPanel.getHeight();
	fPanelX = m_controlBackgroundPanel.getXPos();
	fPanelY = m_controlBackgroundPanel.getYPos();
	// Get size of pointer
	m_fPointerImageOffsetX = 0; //floor(fPointerWidth/2.0f);
	m_fPointerImageOffsetY = 0; //floor(fPointerHeight/2.0f);
	
	m_fPanelMinX = fPanelX;
	m_fPanelMaxX = fPanelX + fPanelWidth;
	m_fPanelMinY = fPanelY;
	m_fPanelMaxY = fPanelY + fPanelHeight;

#ifdef __ORBIS__
	// we need to map the touchpad rectangle to the UI rectangle. While it works great for the creative menu, it is much too sensitive for the smaller menus.
	//X coordinate of the touch point (0 to 1919)    
	//Y coordinate of the touch point (0 to 941: DUALSHOCK®4 wireless controllers and the CUH-ZCT1J/CAP-ZCT1J/CAP-ZCT1U controllers for the PlayStation®4 development tool,    
	//0 to 753: JDX-1000x series controllers for the PlayStation®4 development tool,)     
	m_fTouchPadMulX=fPanelWidth/1919.0f;
	m_fTouchPadMulY=fPanelHeight/941.0f;
	m_fTouchPadDeadZoneX=15.0f*m_fTouchPadMulX;
	m_fTouchPadDeadZoneY=15.0f*m_fTouchPadMulY;

#endif

	// 4J-PB - need to limit this in splitscreen
	if(app.GetLocalPlayerCount()>1)
	{
		// don't let the pointer go into someone's screen
		m_fPointerMinY = floor(fPointerHeight/2.0f);
	}
	else
	{
		m_fPointerMinY = fPanelY -fPointerHeight;
	}
	m_fPointerMinX = fPanelX - fPointerWidth;
	m_fPointerMaxX = m_fPanelMaxX + fPointerWidth;
	m_fPointerMaxY = m_fPanelMaxY + (fPointerHeight/2);

// 	m_hPointerText=NULL;
// 	m_hPointerTextBkg=NULL;

	// Put the pointer over first item in use row to start with.
	UIVec2D itemPos;
	UIVec2D itemSize;
	GetItemScreenData( m_eCurrSection, 0, &( itemPos ), &( itemSize ) );

	UIVec2D sectionPos;
	GetPositionOfSection( m_eCurrSection, &( sectionPos ) );

	UIVec2D vPointerPos = sectionPos;
	vPointerPos += itemPos;
	vPointerPos.x += ( itemSize.x / 2.0f );
	vPointerPos.y += ( itemSize.y / 2.0f );

	vPointerPos.x -= m_fPointerImageOffsetX;
	vPointerPos.y -= m_fPointerImageOffsetY;

	//m_pointerControl->SetPosition( &vPointerPos );
	m_pointerPos = vPointerPos;

	IggyEvent mouseEvent;
	S32 width, height;
	m_parentLayer->getRenderDimensions(width, height);
	S32 x = m_pointerPos.x*((float)width/m_movieWidth);
	S32 y = m_pointerPos.y*((float)height/m_movieHeight); 
	IggyMakeEventMouseMove( &mouseEvent, x, y);

	IggyEventResult result;
	IggyPlayerDispatchEventRS ( getMovie() , &mouseEvent , &result );

#ifdef USE_POINTER_ACCEL	
	m_fPointerVelX = 0.0f;
	m_fPointerVelY = 0.0f;
	m_fPointerAccelX = 0.0f;
	m_fPointerAccelY = 0.0f;
#endif
}

void UIScene_AbstractContainerMenu::tick()
{
	UIScene::tick();

#ifdef _UI_MOUSE_POINTER
	UpdatePointerFromMouse();

	// 4J Meow - the section hit-test in IUIScene_AbstractContainerMenu::onMouseTick
	// reads its rectangles straight out of these UIControls, and a control's
	// cached bounds are only correct for the frame they were taken on -
	// ActionScript positions and resizes things after setupControl ran. The slot
	// lists happen to be authored where they end up; the creative menu's tab
	// panels are not, so their rectangles were never where the pointer was and
	// no tab could be hovered, let alone clicked.
	//
	// UIController::TickMousePointer refreshes controls the same way for every
	// other scene. A container menu has a couple of dozen controls, and this only
	// runs on the mouse path, so the cost is the same as that one's.
	if(m_bPointerFromMouse) UpdateSceneControls();
#endif

	onMouseTick();

	IggyEvent mouseEvent;
	S32 width, height;
	m_parentLayer->getRenderDimensions(width, height);
	S32 x = m_pointerPos.x*((float)width/m_movieWidth);
	S32 y = m_pointerPos.y*((float)height/m_movieHeight);
	IggyMakeEventMouseMove( &mouseEvent, x, y);

	// 4J Stu - This seems to be broken on Durango, so do it ourself
#ifdef _DURANGO
	//mouseEvent.x = x;
	//mouseEvent.y = y;
#endif

	IggyEventResult result;
	IggyPlayerDispatchEventRS ( getMovie() , &mouseEvent , &result );
}

#ifdef _UI_MOUSE_POINTER
// 4J Meow - drive the in-menu pointer from the real mouse.
//
// The container menus already had a free-moving pointer with slot hit-testing
// behind it; what they did not have was a pointing device. The stick drove it
// through onMouseTick as a rate control - hold a direction, the pointer
// accelerates - with a snap-to-slot-centre and a tap-to-jump-a-slot on top to
// make that bearable. A mouse needs none of that: it already is a position.
//
// So the position is written straight in here, in movie coordinates, and
// m_bPointerFromMouse tells onMouseTick to treat it as settled input. This is
// the only place with both halves of the conversion in scope - the window from
// Win64Input, and the scene's authored size from UIScene.
//
// The OS cursor is hidden while a container menu is up (Win64Input's
// ePointerMode_HiddenCursor), because this Flash pointer is the cursor; two of
// them, one lagging the other by a frame, is worse than either.
void UIScene_AbstractContainerMenu::UpdatePointerFromMouse()
{
	m_bPointerFromMouse = false;

	if(!Win64Input::IsPointerActive()) return;
	if(Win64Input::GetPointerMode() != Win64Input::ePointerMode_HiddenCursor) return;

	// Player 1 only, matching the rest of the keyboard and mouse support -
	// splitscreen guests keep the stick pointer.
	if(m_iPad != 0) return;

	float fPointerX = 0.0f, fPointerY = 0.0f;
	if(!Win64Input::GetPointerPos(fPointerX, fPointerY)) return;

	int iClientW = 0, iClientH = 0;
	if(!Win64Input::GetClientSize(iClientW, iClientH)) return;

	if(m_movieWidth <= 0 || m_movieHeight <= 0) return;

	m_pointerPos.x = fPointerX * (float)m_movieWidth  / (float)iClientW;
	m_pointerPos.y = fPointerY * (float)m_movieHeight / (float)iClientH;

	m_bPointerFromMouse = true;
}
#endif // _UI_MOUSE_POINTER

void UIScene_AbstractContainerMenu::render(S32 width, S32 height, C4JRender::eViewportType viewpBort)
{
	m_cacheSlotRenders = true;

	m_needsCacheRendered = m_needsCacheRendered || m_menu->needsRendered();

	if(m_needsCacheRendered)
	{
		m_expectedCachedSlotCount = 0;
		unsigned int count = m_menu->getSize();
		for(unsigned int i = 0; i < count; ++i)
		{
			if(m_menu->getSlot(i)->hasItem())
			{
				++m_expectedCachedSlotCount;
			}
		}
	}

	UIScene::render(width, height, viewpBort);

	m_needsCacheRendered = false;
}

void UIScene_AbstractContainerMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
	Minecraft *pMinecraft = Minecraft::GetInstance();
	if(pMinecraft->localplayers[m_iPad] == NULL || pMinecraft->localgameModes[m_iPad] == NULL) return;

	shared_ptr<ItemInstance> item = nullptr;
	if(wcscmp((wchar_t *)region->name,L"pointerIcon")==0)
	{		
		m_cacheSlotRenders = false;
		item = pMinecraft->localplayers[m_iPad]->inventory->getCarried();
	}
	else
	{
		int slotId = -1;
		swscanf((wchar_t*)region->name,L"slot_%d",&slotId);
		if (slotId == -1)
		{
			app.DebugPrintf("This is not the control we are looking for\n");
		}
		else
		{			
			m_cacheSlotRenders = true;
			Slot *slot = m_menu->getSlot(slotId);
			item = slot->getItem();
		}
	}

	if(item != NULL) customDrawSlotControl(region,m_iPad,item,1.0f,item->isFoil(),true);
}

void UIScene_AbstractContainerMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	if(m_bIgnoreInput) return;

	//app.DebugPrintf("UIScene_InventoryMenu handling input for pad %d, key %d, down- %s, pressed- %s, released- %s\n", iPad, key, down?"TRUE":"FALSE", pressed?"TRUE":"FALSE", released?"TRUE":"FALSE");
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	if(pressed)
	{
		handled = handleKeyDown(m_iPad, key, repeat);
	}
}

void UIScene_AbstractContainerMenu::SetPointerText(const wstring &description, vector<wstring> &unformattedStrings, bool newSlot)
{
	//app.DebugPrintf("Setting pointer text\n");
	m_cursorPath.setLabel(description,false,newSlot);
}

void UIScene_AbstractContainerMenu::setSectionFocus(ESceneSection eSection, int iPad)
{
	if(m_focusSection != eSectionNone)
	{
		UIControl *currentFocus = getSection(m_focusSection);
		if(currentFocus) currentFocus->setFocus(false);
	}
	UIControl *newFocus = getSection(eSection);
	if(newFocus) newFocus->setFocus(true);
	m_focusSection = eSection;
}

void UIScene_AbstractContainerMenu::setFocusToPointer(int iPad)
{
	if(m_focusSection != eSectionNone)
	{
		UIControl *currentFocus = getSection(m_focusSection);
		if(currentFocus) currentFocus->setFocus(false);
	}
	m_focusSection = eSectionNone;
}

shared_ptr<ItemInstance> UIScene_AbstractContainerMenu::getSlotItem(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return slot->getItem();
	else return nullptr;
}

bool UIScene_AbstractContainerMenu::isSlotEmpty(ESceneSection eSection, int iSlot)
{
	Slot *slot = m_menu->getSlot( getSectionStartOffset(eSection) + iSlot );
	if(slot) return !slot->hasItem();
	else return false;
}

void UIScene_AbstractContainerMenu::adjustPointerForSafeZone()
{
	// Handled by AS
}
