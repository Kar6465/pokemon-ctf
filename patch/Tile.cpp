#include "Tile.h"
#include "Graphics.h"

Tile::Tile()
{
}

void Tile::Load(double x, double y, int ID)
{
	_ID = ID;
	_pos.x = x * 32; _pos.y = y * 32;
	_pos.w = _pos.h = 32;

	_tile.x = ((_ID - 1) % 6) * 16;
	_tile.y = ((_ID - 1) / 6) * 16;
	_tile.w = _tile.h = 16;
}

void Tile::Display(Bitmap *tileset)
{
	int sx = ((_ID - 1) % 6) * 16, sy = ((_ID - 1) / 6) * 16;
	// dst = 32x32 tile at _pos, src = 16x16 region of the tileset.
	Draw::BITMAP_region(_pos.x, _pos.y, _pos.w, _pos.h,
	                    sx, sy, _tile.w, _tile.h, tileset);
}

void Tile::SetID(int ID)
{
	_ID = ID;
	_tile.x = ((_ID - 1) % 6) * 16;
	_tile.y = ((_ID - 1) / 6) * 16;
	
}

int Tile::GetID()
{
	return _ID;
}
