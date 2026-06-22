#pragma once
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
// NOTE: OpenGL is gone. Rendering is now a software RGBA framebuffer (see
// PORTING.md) so the game can run inside a PDF, which has no WebGL/canvas.

#include "Basics.h"

#define GE_WINDOWS_CENTERED SDL_WINDOWPOS_CENTERED

class Color;

Color rgba_color(float r, float g, float b, float a);
Color rgb_color(float r, float g, float b);
class Color
{
public:
	Color();
	Color(float R, float G, float B, float A);
	float r, g, b, a;

};

// ============================================================================
// Gfx: software rasterizer. The whole renderer is one textured-quad blitter
// plus a 2D transform, mirroring the old immediate-mode GL pipeline 1:1.
// ============================================================================
namespace Gfx
{
	extern SDL_Surface *framebuffer;   // RGBA32, == game coordinate space

	void Init(int w, int h);           // alloc framebuffer (+ native window)
	void Clear(float r, float g, float b, float a);
	void Present();                    // native: blit to window; PDF: ASCII fields
	void Shutdown();

	// world -> screen:  screen = (world - off) * scale
	void SetTransform(double scale, double offx, double offy);

	// Rasterize a quad whose 4 corners are in WORLD space (transform applied
	// here), with per-corner (u,v) in [0,1]. tex==nullptr draws a solid quad
	// tinted by (tr,tg,tb,ta); otherwise samples tex (nearest) * tint.
	void BlitQuad(const double cx[4], const double cy[4],
	              const float u[4], const float v[4],
	              SDL_Surface *tex,
	              float tr, float tg, float tb, float ta);
}

class Bitmap
{
public:
	Bitmap() = default;
	// Bitmap owns an SDL_Surface*. Game code copies Bitmaps by value (e.g. via
	// Pokemon's by-value operator=), so it MUST deep-copy, otherwise two Bitmaps
	// share one surface and double-free it. (With the old GLuint handle this was
	// a harmless no-op; with a real pointer it crashes.)
	Bitmap(const Bitmap &o);
	Bitmap &operator=(const Bitmap &o);

	void Load(const char *filename);
	void LoadText(TTF_Font *font, const char *text, SDL_Color color);
	void Destroy();
	bool isLoaded();
	SDL_Surface *GetSurface();
	int GetW();
	int GetH();
	~Bitmap();


private:
	SDL_Surface *_surf = nullptr;   // RGBA32 source pixels (CPU)
	bool _loaded = false;
	int _w = 0, _h = 0;

};

class Draw
{
public:
	// 2D
	static void Rectangle(double X1, double Y1, double X2, double Y2, Color color);
	static void Circle(double x, double y, double r, int accuracy, Color color);
	static void Line(double X1, double Y1, double X2, double Y2, Color color);
	static void Line(Point2D p1, Point2D p2, Color color);
	static void Text(string text, double size, double x, double y);
	static void BITMAP(double x, double y, Bitmap *bmp);
	static void Rotated_BITMAP(double x, double y, double cx, double cy, float a, Bitmap *bmp);
	static void BITMAP_region(double dx, double dy, double dw, double dh, double sx, double sy, double sw, double sh, Bitmap *bmp);
	static void tinted_BITMAP(double x, double y, Color color, Bitmap *bmp);
	static void tinted_BITMAP_region(double dx, double dy, double dw, double dh, double sx, double sy, double sw, double sh, Color color, Bitmap * bmp);
	static void Rotated_BITMAP_Region(double dx, double dy, double dw, double dh, double sx, double sy, double sw, double sh, double cx, double cy, float a, Bitmap * bmp);
	static void tinted_Rotated_BITMAP_Region(double dx, double dy, double dw, double dh, double sx, double sy, double sw, double sh, double cx, double cy, float a, Color color, Bitmap * bmp);

	static int d;

};

class Camera
{
public:
	Camera();
	void Perspective();
	void GUI();
	void FlatPerspective();
	void Update();
	void Follow(Point2D *p);
	void GoTo(Point2D p);
	void Rotate(float x, float y, float z);
	void RotateX(float x);
	void RotateY(float y);
	void RotateZ(float z);
	void SetFOV(double fov);
	void SetZoom(double zoom);
	void SetPos(double x, double y, double z);
	void SetLimit(double lx, double rx, double uy, double dy);
	double X();
	double Y();
	void SetX(double x);
	void SetY(double y);

private:
	Point2D _pos;
	Point2D *_follow = nullptr;
	double _zoom = 1;
	double _fov = 36;
	float _a = 0, _b = 0, _c = 0;
	double _x_limit_l = 0, _x_limit_r = 0;
	double _y_limit_u = 0, _y_limit_d = 0;
};
