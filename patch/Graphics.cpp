#include "Graphics.h"
#include <algorithm>
#include <cmath>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// ============================================================================
// COLOR
// ============================================================================
Color rgba_color(float r, float g, float b, float a) { return Color(r, g, b, a); }
Color rgb_color(float r, float g, float b)           { return Color(r, g, b, 1); }
Color::Color()                                       { r = g = b = a = 0; }
Color::Color(float R, float G, float B, float A)     { r = R; g = G; b = B; a = A; }

// ============================================================================
// Gfx software rasterizer
// ============================================================================
namespace Gfx
{
	SDL_Surface *framebuffer = nullptr;

	static double s_scale = 1.0, s_offx = 0.0, s_offy = 0.0;
	// Render-resolution scale: world/GUI coords (1080x720) -> framebuffer pixels.
	// Native renders at full res (1.0); the PDF build renders DIRECTLY at the grid
	// resolution so the rasterizer touches ~10x fewer pixels (and no downscale).
	static double s_fx = 1.0, s_fy = 1.0;

#ifndef __EMSCRIPTEN__
	// Native build keeps a real window so the port can be watched/validated.
	static SDL_Window   *s_win = nullptr;
	static SDL_Renderer *s_ren = nullptr;
	static SDL_Texture  *s_tex = nullptr;
#else
	// PDF build: framebuffer IS the text-field grid (must match generate_pdf.py
	// and pokemon_pre.js).
	static const int GRID_W = 480, GRID_H = 270;  // higher = more readable (more cells)
#endif

	void SetTransform(double scale, double offx, double offy)
	{
		s_scale = scale; s_offx = offx; s_offy = offy;
	}

	void Init(int w, int h)
	{
#ifndef __EMSCRIPTEN__
		framebuffer = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
		s_fx = 1.0; s_fy = 1.0;
		s_win = SDL_CreateWindow("Pokemon Grey", SDL_WINDOWPOS_CENTERED,
		                         SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_SHOWN);
		s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
		s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGBA32,
		                          SDL_TEXTUREACCESS_STREAMING, w, h);
#else
		// Render straight into a grid-sized buffer; scale all draws down to fit.
		framebuffer = SDL_CreateRGBSurfaceWithFormat(0, GRID_W, GRID_H, 32, SDL_PIXELFORMAT_RGBA32);
		s_fx = (double)GRID_W / (double)w;
		s_fy = (double)GRID_H / (double)h;
#endif
	}

	void Clear(float r, float g, float b, float a)
	{
		if (!framebuffer) return;
		Uint32 c = SDL_MapRGBA(framebuffer->format,
		                       (Uint8)(r * 255), (Uint8)(g * 255),
		                       (Uint8)(b * 255), (Uint8)(a * 255));
		SDL_FillRect(framebuffer, nullptr, c);
	}

	void Present()
	{
		if (!framebuffer) return;
#ifndef __EMSCRIPTEN__
		// Debug: POKEMON_DUMP=<frame> dumps that frame to frame.png then exits.
		static int frame = 0; frame++;
		const char *dump = SDL_getenv("POKEMON_DUMP");
		if (dump && frame == atoi(dump)) { IMG_SavePNG(framebuffer, "frame.png"); exit(0); }
		SDL_UpdateTexture(s_tex, nullptr, framebuffer->pixels, framebuffer->pitch);
		SDL_RenderClear(s_ren);
		SDL_RenderCopy(s_ren, s_tex, nullptr, nullptr);
		SDL_RenderPresent(s_ren);
#else
		// framebuffer is already the grid: hand it straight to pre.js (no downscale).
		int len = GRID_W * GRID_H * 4;
		EM_ASM({ if (typeof update_framebuffer === 'function') update_framebuffer($0, $1, $2, $3); },
		       framebuffer->pixels, len, GRID_W, GRID_H);
#endif
	}

	void Shutdown()
	{
		if (framebuffer) { SDL_FreeSurface(framebuffer); framebuffer = nullptr; }
#ifndef __EMSCRIPTEN__
		if (s_tex) SDL_DestroyTexture(s_tex);
		if (s_ren) SDL_DestroyRenderer(s_ren);
		if (s_win) SDL_DestroyWindow(s_win);
#endif
	}

	// --- pixel helpers (framebuffer + sources are all RGBA32: bytes R,G,B,A) ---
	static inline void blend_px(int x, int y, float sr, float sg, float sb, float sa)
	{
		if (!framebuffer) return;
		if (x < 0 || y < 0 || x >= framebuffer->w || y >= framebuffer->h) return;
		if (sa <= 0.0f) return;
		if (sa > 1.0f) sa = 1.0f;
		Uint8 *p = (Uint8 *)framebuffer->pixels + y * framebuffer->pitch + x * 4;
		float ia = 1.0f - sa;
		p[0] = (Uint8)std::min(255.0f, sr * 255.0f * sa + p[0] * ia);
		p[1] = (Uint8)std::min(255.0f, sg * 255.0f * sa + p[1] * ia);
		p[2] = (Uint8)std::min(255.0f, sb * 255.0f * sa + p[2] * ia);
		p[3] = (Uint8)std::min(255.0f, sa * 255.0f      + p[3] * ia);
	}

	static inline double edge(double ax, double ay, double bx, double by, double px, double py)
	{
		return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
	}

	// Rasterize triangle (i0,i1,i2) of a quad given screen-space corners sx/sy,
	// per-corner uv, optional texture, and tint.
	static void raster_tri(const double sx[4], const double sy[4],
	                       const float u[4], const float v[4],
	                       int i0, int i1, int i2,
	                       SDL_Surface *tex, float tr, float tg, float tb, float ta)
	{
		double area = edge(sx[i0], sy[i0], sx[i1], sy[i1], sx[i2], sy[i2]);
		if (area == 0.0 || !framebuffer) return;

		int minx = (int)std::floor(std::min(std::min(sx[i0], sx[i1]), sx[i2]));
		int maxx = (int)std::ceil (std::max(std::max(sx[i0], sx[i1]), sx[i2]));
		int miny = (int)std::floor(std::min(std::min(sy[i0], sy[i1]), sy[i2]));
		int maxy = (int)std::ceil (std::max(std::max(sy[i0], sy[i1]), sy[i2]));
		minx = std::max(minx, 0); miny = std::max(miny, 0);
		maxx = std::min(maxx, framebuffer->w - 1);
		maxy = std::min(maxy, framebuffer->h - 1);

		int tw = tex ? tex->w : 0, thh = tex ? tex->h : 0;
		const Uint8 *tpix = tex ? (const Uint8 *)tex->pixels : nullptr;
		int tpitch = tex ? tex->pitch : 0;

		for (int y = miny; y <= maxy; y++)
		{
			for (int x = minx; x <= maxx; x++)
			{
				double pcx = x + 0.5, pcy = y + 0.5;
				double e0 = edge(sx[i1], sy[i1], sx[i2], sy[i2], pcx, pcy);
				double e1 = edge(sx[i2], sy[i2], sx[i0], sy[i0], pcx, pcy);
				double e2 = edge(sx[i0], sy[i0], sx[i1], sy[i1], pcx, pcy);
				bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
				              (e0 <= 0 && e1 <= 0 && e2 <= 0);
				if (!inside) continue;

				double w0 = e0 / area, w1 = e1 / area, w2 = e2 / area;
				float sr = tr, sg = tg, sb = tb, sa = ta;
				if (tex)
				{
					float uu = (float)(w0 * u[i0] + w1 * u[i1] + w2 * u[i2]);
					float vv = (float)(w0 * v[i0] + w1 * v[i1] + w2 * v[i2]);
					int txc = std::min(std::max((int)(uu * tw), 0), tw - 1);
					int tyc = std::min(std::max((int)(vv * thh), 0), thh - 1);
					const Uint8 *s = tpix + tyc * tpitch + txc * 4;
					sr *= s[0] / 255.0f; sg *= s[1] / 255.0f;
					sb *= s[2] / 255.0f; sa *= s[3] / 255.0f;
				}
				blend_px(x, y, sr, sg, sb, sa);
			}
		}
	}

	void BlitQuad(const double cx[4], const double cy[4],
	              const float u[4], const float v[4],
	              SDL_Surface *tex, float tr, float tg, float tb, float ta)
	{
		double sx[4], sy[4];
		for (int i = 0; i < 4; i++)
		{
			sx[i] = (cx[i] - s_offx) * s_scale * s_fx;
			sy[i] = (cy[i] - s_offy) * s_scale * s_fy;
		}
		raster_tri(sx, sy, u, v, 0, 1, 2, tex, tr, tg, tb, ta);
		raster_tri(sx, sy, u, v, 0, 2, 3, tex, tr, tg, tb, ta);
	}
}

// ============================================================================
// Bitmap  (CPU surface, RGBA32)
// ============================================================================
static SDL_Surface *to_rgba32(SDL_Surface *s)
{
	if (!s) return nullptr;
	SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
	if (c) { SDL_FreeSurface(s); return c; }
	return s; // conversion failed: keep original rather than leak/return null

}

Bitmap::Bitmap(const Bitmap &o)
{
	if (o._surf) { _surf = SDL_DuplicateSurface(o._surf); _w = o._w; _h = o._h; _loaded = o._loaded; }
}

Bitmap &Bitmap::operator=(const Bitmap &o)
{
	if (this == &o) return *this;
	if (_loaded && _surf) SDL_FreeSurface(_surf);
	_surf = nullptr; _loaded = false; _w = _h = 0;
	if (o._surf) { _surf = SDL_DuplicateSurface(o._surf); _w = o._w; _h = o._h; _loaded = o._loaded; }
	return *this;
}

void Bitmap::Load(const char *filename)
{
	if (_loaded && _surf) { SDL_FreeSurface(_surf); _surf = nullptr; }
	SDL_Surface *img = IMG_Load(filename);
	if (img)
	{
		_surf = to_rgba32(img);
		_w = _surf->w; _h = _surf->h;
		_loaded = true;
	}
	else cout << "Can't load " << filename << " : " << IMG_GetError() << endl;
}

void Bitmap::LoadText(TTF_Font *font, const char *text, SDL_Color color)
{
	if (_loaded && _surf) { SDL_FreeSurface(_surf); _surf = nullptr; }
	SDL_Surface *img = TTF_RenderText_Blended(font, text, color);
	if (img)
	{
		_surf = to_rgba32(img);
		_w = _surf->w; _h = _surf->h;
		_loaded = true;
	}
	else cout << text << "  :  " << TTF_GetError() << endl;
}

void Bitmap::Destroy()
{
	if (_loaded && _surf) { SDL_FreeSurface(_surf); _surf = nullptr; }
	_loaded = false;
}

bool Bitmap::isLoaded() { return _loaded; }
SDL_Surface *Bitmap::GetSurface() { return _surf; }
int Bitmap::GetW() { return _w; }
int Bitmap::GetH() { return _h; }
Bitmap::~Bitmap() { if (_loaded && _surf) SDL_FreeSurface(_surf); }

// ============================================================================
// Draw  (each call builds a screen-space quad, exactly like the old GL quads)
// ============================================================================
int Draw::d = 0;

void Draw::Rectangle(double X1, double Y1, double X2, double Y2, Color color)
{
	double cx[4] = { X1, X2, X2, X1 };
	double cy[4] = { Y1, Y1, Y2, Y2 };
	float u[4] = { 0,0,0,0 }, v[4] = { 0,0,0,0 };
	Gfx::BlitQuad(cx, cy, u, v, nullptr, color.r, color.g, color.b, color.a);
}

void Draw::Circle(double x, double y, double r, int accuracy, Color color)
{
	// The old GL_POLYGON was an N-gon approximation; a filled axis-aligned quad
	// is a coarse stand-in. Circles are used only for a couple of minor FX.
	(void)accuracy;
	double cx[4] = { x - r, x + r, x + r, x - r };
	double cy[4] = { y - r, y - r, y + r, y + r };
	float u[4] = { 0,0,0,0 }, v[4] = { 0,0,0,0 };
	Gfx::BlitQuad(cx, cy, u, v, nullptr, color.r, color.g, color.b, color.a);
}

void Draw::Line(double X1, double Y1, double X2, double Y2, Color color)
{
	// Thin quad approximation of a 1px line (in world space).
	double cx[4] = { X1, X2, X2, X1 };
	double cy[4] = { Y1, Y2, Y2 + 1, Y1 + 1 };
	float u[4] = { 0,0,0,0 }, v[4] = { 0,0,0,0 };
	Gfx::BlitQuad(cx, cy, u, v, nullptr, color.r, color.g, color.b, color.a);
}

void Draw::Line(Point2D p1, Point2D p2, Color color)
{
	Line(p1.X(), p1.Y(), p2.X(), p2.Y(), color);
}

void Draw::Text(string text, double size, double x, double y)
{
	(void)text; (void)size; (void)x; (void)y; // unused in the original too
}

void Draw::BITMAP(double x, double y, Bitmap *bmp)
{
	int w = bmp->GetW(), h = bmp->GetH();
	double cx[4] = { x, x + w, x + w, x };
	double cy[4] = { y, y, y + h, y + h };
	float u[4] = { 0, 1, 1, 0 }, v[4] = { 0, 0, 1, 1 };
	Gfx::BlitQuad(cx, cy, u, v, bmp->GetSurface(), 1, 1, 1, 1);
}

void Draw::tinted_BITMAP(double x, double y, Color color, Bitmap *bmp)
{
	int w = bmp->GetW(), h = bmp->GetH();
	double cx[4] = { x, x + w, x + w, x };
	double cy[4] = { y, y, y + h, y + h };
	float u[4] = { 0, 1, 1, 0 }, v[4] = { 0, 0, 1, 1 };
	Gfx::BlitQuad(cx, cy, u, v, bmp->GetSurface(), color.r, color.g, color.b, color.a);
}

void Draw::BITMAP_region(double dx, double dy, double dw, double dh,
                         double sx, double sy, double sw, double sh, Bitmap *bmp)
{
	double W = bmp->GetW(), H = bmp->GetH();
	double cx[4] = { dx, dx + dw, dx + dw, dx };
	double cy[4] = { dy, dy, dy + dh, dy + dh };
	float u[4] = { (float)(sx / W), (float)((sx + sw) / W), (float)((sx + sw) / W), (float)(sx / W) };
	float v[4] = { (float)(sy / H), (float)(sy / H), (float)((sy + sh) / H), (float)((sy + sh) / H) };
	Gfx::BlitQuad(cx, cy, u, v, bmp->GetSurface(), 1, 1, 1, 1);
}

void Draw::tinted_BITMAP_region(double dx, double dy, double dw, double dh,
                                double sx, double sy, double sw, double sh,
                                Color color, Bitmap *bmp)
{
	double W = bmp->GetW(), H = bmp->GetH();
	double cx[4] = { dx, dx + dw, dx + dw, dx };
	double cy[4] = { dy, dy, dy + dh, dy + dh };
	float u[4] = { (float)(sx / W), (float)((sx + sw) / W), (float)((sx + sw) / W), (float)(sx / W) };
	float v[4] = { (float)(sy / H), (float)(sy / H), (float)((sy + sh) / H), (float)((sy + sh) / H) };
	Gfx::BlitQuad(cx, cy, u, v, bmp->GetSurface(), color.r, color.g, color.b, color.a);
}

// Rotated variants: corner math copied verbatim from the old GL version; only
// the emit changes (GL quad -> BlitQuad).
void Draw::Rotated_BITMAP(double x, double y, double cx, double cy, float a, Bitmap *bmp)
{
	double W = bmp->GetW(), H = bmp->GetH();
	double dist; float a2; double qx[4], qy[4];

	dist = sqrt(cx * cx + cy * cy);                        a2 = atan2(-cy, -cx);
	qx[0] = x + cx + cos(a2 + a) * dist; qy[0] = y + cy + sin(a2 + a) * dist;
	dist = sqrt((W - cx) * (W - cx) + cy * cy);            a2 = atan2(-cy, W - cx);
	qx[1] = x + cx + cos(a2 + a) * dist; qy[1] = y + cy + sin(a2 + a) * dist;
	dist = sqrt((W - cx) * (W - cx) + (H - cy) * (H - cy)); a2 = atan2(H - cy, W - cx);
	qx[2] = x + cx + cos(a2 + a) * dist; qy[2] = y + cy + sin(a2 + a) * dist;
	dist = sqrt(cx * cx + (H - cy) * (H - cy));            a2 = atan2(H - cy, -cx);
	qx[3] = x + cx + cos(a2 + a) * dist; qy[3] = y + cy + sin(a2 + a) * dist;

	float u[4] = { 0, 1, 1, 0 }, v[4] = { 0, 0, 1, 1 };
	Gfx::BlitQuad(qx, qy, u, v, bmp->GetSurface(), 1, 1, 1, 1);
}

void Draw::Rotated_BITMAP_Region(double dx, double dy, double dw, double dh,
                                 double sx, double sy, double sw, double sh,
                                 double cx, double cy, float a, Bitmap *bmp)
{
	double W = bmp->GetW(), H = bmp->GetH();
	double dist; float a2; double qx[4], qy[4];

	dist = sqrt(cx * cx + cy * cy);                          a2 = atan2(-cy, -cx);
	qx[0] = dx + cx + cos(a2 + a) * dist; qy[0] = dy + cy + sin(a2 + a) * dist;
	dist = sqrt((dw - cx) * (dw - cx) + cy * cy);            a2 = atan2(-cy, dw - cx);
	qx[1] = dx + cx + cos(a2 + a) * dist; qy[1] = dy + cy + sin(a2 + a) * dist;
	dist = sqrt((dw - cx) * (dw - cx) + (dh - cy) * (dh - cy)); a2 = atan2(dh - cy, dw - cx);
	qx[2] = dx + cx + cos(a2 + a) * dist; qy[2] = dy + cy + sin(a2 + a) * dist;
	dist = sqrt(cx * cx + (dh - cy) * (dh - cy));            a2 = atan2(dh - cy, -cx);
	qx[3] = dx + cx + cos(a2 + a) * dist; qy[3] = dy + cy + sin(a2 + a) * dist;

	float u[4] = { (float)(sx / W), (float)((sx + sw) / W), (float)((sx + sw) / W), (float)(sx / W) };
	float v[4] = { (float)(sy / H), (float)(sy / H), (float)((sy + sh) / H), (float)((sy + sh) / H) };
	Gfx::BlitQuad(qx, qy, u, v, bmp->GetSurface(), 1, 1, 1, 1);
}

void Draw::tinted_Rotated_BITMAP_Region(double dx, double dy, double dw, double dh,
                                        double sx, double sy, double sw, double sh,
                                        double cx, double cy, float a, Color color, Bitmap *bmp)
{
	double W = bmp->GetW(), H = bmp->GetH();
	double dist; float a2; double qx[4], qy[4];

	dist = sqrt(cx * cx + cy * cy);                          a2 = atan2(-cy, -cx);
	qx[0] = dx + cx + cos(a2 + a) * dist; qy[0] = dy + cy + sin(a2 + a) * dist;
	dist = sqrt((dw - cx) * (dw - cx) + cy * cy);            a2 = atan2(-cy, dw - cx);
	qx[1] = dx + cx + cos(a2 + a) * dist; qy[1] = dy + cy + sin(a2 + a) * dist;
	dist = sqrt((dw - cx) * (dw - cx) + (dh - cy) * (dh - cy)); a2 = atan2(dh - cy, dw - cx);
	qx[2] = dx + cx + cos(a2 + a) * dist; qy[2] = dy + cy + sin(a2 + a) * dist;
	dist = sqrt(cx * cx + (dh - cy) * (dh - cy));            a2 = atan2(dh - cy, -cx);
	qx[3] = dx + cx + cos(a2 + a) * dist; qy[3] = dy + cy + sin(a2 + a) * dist;

	float u[4] = { (float)(sx / W), (float)((sx + sw) / W), (float)((sx + sw) / W), (float)(sx / W) };
	float v[4] = { (float)(sy / H), (float)(sy / H), (float)((sy + sh) / H), (float)((sy + sh) / H) };
	Gfx::BlitQuad(qx, qy, u, v, bmp->GetSurface(), color.r, color.g, color.b, color.a);
}

// ============================================================================
// Camera  (sets the Gfx 2D transform instead of GL matrices)
// ============================================================================
Camera::Camera() {}

void Camera::Perspective()
{
	// gluOrtho2D(0, W/zoom, H/zoom, 0) + translate(-pos)  ==>  screen=(world-pos)*zoom
	Gfx::SetTransform(_zoom, _pos.X(), _pos.Y());
}

void Camera::GUI()
{
	Gfx::SetTransform(1.0, 0.0, 0.0); // HUD/menus drawn 1:1
}

void Camera::FlatPerspective() {}

void Camera::Update()
{
	if (_follow != nullptr)
		_pos.Set(_follow->X(), _follow->Y());

	if (_x_limit_l != _x_limit_r)
	{
		if (_pos.X() < _x_limit_l) _pos.SetX(_x_limit_l);
		if (_pos.X() + Display_info::width / _zoom > _x_limit_r)
			_pos.SetX(_x_limit_r - Display_info::width / _zoom);
		if (_x_limit_r - _x_limit_l < Display_info::width / _zoom)
			_pos.SetX((_x_limit_r + _x_limit_l) * 0.5 - 0.5 * Display_info::width / _zoom);
	}

	if (_y_limit_u != _y_limit_d)
	{
		if (_pos.Y() < _y_limit_u) _pos.SetY(_y_limit_u);
		if (_pos.Y() + Display_info::height / _zoom > _y_limit_d)
			_pos.SetY(_y_limit_d - Display_info::height / _zoom);
		if (_y_limit_d - _y_limit_u < Display_info::height / _zoom)
			_pos.SetY((_y_limit_d + _y_limit_u) * 0.5 - 0.5 * Display_info::height / _zoom);
	}
}

void Camera::Follow(Point2D *p) { _follow = p; }
void Camera::GoTo(Point2D p)    { _pos.Set(p.X(), p.Y()); }
void Camera::Rotate(float x, float y, float z) { _a = x; _b = y; _c = z; }
void Camera::RotateX(float x) { _a = x; }
void Camera::RotateY(float y) { _b = y; }
void Camera::RotateZ(float z) { _c = z; }
void Camera::SetFOV(double fov) { _fov = fov; }
void Camera::SetZoom(double zoom) { _zoom = zoom; }
void Camera::SetPos(double x, double y, double z) { _pos.Set(x, y); }
void Camera::SetLimit(double lx, double rx, double uy, double dy)
{
	_x_limit_l = lx; _x_limit_r = rx; _y_limit_d = dy; _y_limit_u = uy;
}
double Camera::X() { return _pos.X(); }
double Camera::Y() { return _pos.Y(); }
void Camera::SetX(double x) { _pos.SetX(x); }
void Camera::SetY(double y) { _pos.SetY(y); }
