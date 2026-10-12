// Penumbra map viewer. Distributed under GPLv3 or later.
#ifndef PENUMBRA_FREE_CAMERA_H
#define PENUMBRA_FREE_CAMERA_H
#include "engine/Updateable.h"
#include "system/Event.h"
#include "resources/FontManager.h"
#include "graphics/FontData.h"
#include <SDL.h>

namespace hpl { class cEngine; class cCamera; class cViewport; class iCharacterBody; class cGuiSet; class cGuiGfxElement; }

namespace penumbra {
	class cMapScripts;
	class cFreeCamera : public hpl::iUpdateable
	{
	public:
		cFreeCamera(hpl::cEngine* apEngine, hpl::cCamera* apCamera,
			hpl::cViewport* apViewport, int alFrames, bool abWalk, bool abDebugAreas, cMapScripts* scripts = nullptr);
		~cFreeCamera();
		void Update(float afTimeStep) override;
		void OnDraw(float afFrameTime) override;
		void OnPostBufferSwap() override;
		bool Failed() const { return mbFailed; }
	private:
		void HandleEvent(const SDL_Event& aEvent);
		void CreatePlayerBody();
		bool ChangeMap();
		hpl::cEngine* mpEngine;
		hpl::cCamera* mpCamera;
		hpl::cViewport* mpViewport;
		hpl::iCharacterBody* mpBody = nullptr;
		hpl::cGuiSet* mpDebugGui = nullptr;
		hpl::SharedResourceHandle<hpl::iFontData> mDebugFont;
		bool mbDebugAreas;
		bool mbWalk;
		bool mbFailed = false;
		cMapScripts* mpScripts;
		hpl::cGuiGfxElement* mpBlack = nullptr;
		int mlFrameLimit, mlFrames = 0;
		hpl::EventHandler<const SDL_Event&> mEvents;
	};
}
#endif
