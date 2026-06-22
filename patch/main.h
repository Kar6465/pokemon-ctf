#pragma once

#if defined(_WIN32)
#include <windows.h>
#endif
#include "Basics.h"
#include "Graphics.h"
#include "Inputs.h"
#include "Sound.h"

class Main
{
	public:
		static void Init();
		static void SetupGame();   // one-time overworld setup (shared)
		static void Frame();       // one frame of the overworld (shared by loop + tick)
		static void MainLoop();
		static void Editor();
		static void Create_window(const char * name, int x, int y, int w, int h);
		static void Exit();
		static SDL_Renderer *renderer;
		static SDL_Window* screen;
		static int GUI;
		static Inputs input;

	private:
		static SDL_GLContext GL_context;
		
		static int Disp_w, Disp_h;
};
