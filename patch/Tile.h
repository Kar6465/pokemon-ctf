#pragma once
#include "Basics.h"

class Bitmap; // defined in Graphics.h

class Tile
{
	public:
		Tile();
		void Load(double x, double y, int ID);
		void Display(Bitmap *tileset);
		void SetID(int ID);
		int GetID();

	private:
		int _ID;
		SDL_Rect _pos, _tile;

};