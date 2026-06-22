#include "main.h"
#include "Map.h"
#include "GUI.h"
#include "Database.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// Persistent overworld state. In the native build these would be MainLoop
// locals, but the PDF/browser build drives one Frame() per JS timer tick, so the
// state must outlive a single call.
static Map    *g_map = nullptr;
static Camera  g_cam;
static Player *g_player = nullptr;
static Clock   g_frame_timer;

//Main
SDL_Window *Main::screen = NULL;
SDL_Renderer *Main::renderer = NULL;
SDL_GLContext Main::GL_context = NULL;
int Main::Disp_w = 0;
int Main::Disp_h = 0;
int Main::GUI = 0;
Inputs Main::input;

void Main::Exit()
{
	Database::Destroy();

	SDL_DestroyWindow(screen);
	TTF_Quit();
	SDL_Quit();
}

void Main::Init()
{
#ifdef __EMSCRIPTEN__
	// In the PDF/pdfium sandbox there is no canvas/WebGL/Web Audio, so we must NOT
	// init SDL video/audio (they reach for document/AudioContext and abort). But
	// the SDL library itself must be initialized or every SDL call (timer, surface
	// blits) returns "Library not initialized". Init only the timer subsystem,
	// which uses performance.now (polyfilled) and touches no browser globals.
	if (SDL_Init(SDL_INIT_TIMER) < 0)
		cout << "SDL timer init error: " << SDL_GetError() << endl;
	if (TTF_Init() == -1)
		cout << "TTF init error: " << TTF_GetError() << endl;
#else
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
	{
		cout << "SDL initialisation error : " << SDL_GetError() << endl;
		SDL_Quit();

		Exit();
	}

	if (TTF_Init() == -1)
	{
		cout << "TTF initialisation error : " << TTF_GetError() << endl;
		SDL_Quit();

		Exit();
	}

	// Audio is non-fatal: a PDF has no audio device, and we still want the game
	// (and the CTF flag path) to run. Failures just disable sound.
	if (Mix_OpenAudio(22050, MIX_DEFAULT_FORMAT, 2, 4096) == -1)
		cout << "SDL_mixer init (non-fatal): " << Mix_GetError() << endl;
	else
		cout << "Allocated Channels : " << Mix_AllocateChannels(32) << endl;
#endif
}

void Main::SetupGame()
{
	g_player = Player_info::GetPlayer_ptr();
	g_player->SetPos(8, 10);
	g_player->SetName("Red");

	g_map = new Map();
	g_map->Load(1);
	g_map->BindPlayer(g_player);

	g_cam.SetPos(0, 0, 0);
	g_cam.Follow(g_player->GetCenter());

	Main::GUI = 0;
	DialogueGUI::LoadScript("TEST");
	g_frame_timer.start();
}

// One frame of the overworld. Shared by the native while-loop and the PDF tick.
// NOTE: GUI==3 enters FightGUI::Battle(), which still runs its own nested
// blocking loop. That is fine natively but cannot work under the PDF tick model
// (a PDF has no re-entrant event pump). Converting Battle()/Dialogue() to the
// same tick/state-machine model is the remaining step (PORTING.md Phase 3b)
// before a battle can be fought — and the flag won — inside the PDF.
void Main::Frame()
{
	input.UpdateControllerInputs(true);
	input.UpdateKeyboardInputs(true);

	if (GUI == 0)
	{
		if (input.pushedInput(input.T)) GUI = 1;
		if (input.pushedInput(input.E)) GUI = 2;
		if (input.GetInput(input.S)) g_map->MovePlayer(input.GetInput(input.L_SHIFT), 0);
		if (input.GetInput(input.Z)) g_map->MovePlayer(input.GetInput(input.L_SHIFT), 1);
		if (input.GetInput(input.Q)) g_map->MovePlayer(input.GetInput(input.L_SHIFT), 2);
		if (input.GetInput(input.D)) g_map->MovePlayer(input.GetInput(input.L_SHIFT), 3);
		if (input.pushedInput(input.Space)) g_map->Interact();
	}
	else if (GUI == 1) PkmnTeamGUI::Update();
	else if (GUI == 2) BagGUI::Update();
	else if (GUI == 3)
	{
		Mix_HaltMusic();
		FightGUI::Battle(Player_info::GetPlayer_ptr(), 5, 100, 0);
		g_map->PlayMusic();
	}
	else if (GUI == 4) DialogueGUI::Update();

	MainInfoGUI::Update();
	Sound_manager::Update_sound();
	g_map->Update();
	g_player->Animate();

	// Display
	Gfx::Clear(0.1f, 0.1f, 0.1f, 1.0f);

	g_cam.SetLimit(0, g_map->GetW() * 32, 0, g_map->GetH() * 32);
	g_cam.Update();
	g_cam.Perspective();
	g_map->Display();

	g_cam.GUI();
	MainInfoGUI::Display();
	if (GUI == 1) PkmnTeamGUI::Display();
	if (GUI == 2) BagGUI::Display();
	else if (GUI == 4) DialogueGUI::Display();

	if (!input.CloseGame) Gfx::Present();
}

void Main::MainLoop()
{
	const double frame_rate = 90;
	SetupGame();
	while (!input.CloseGame)
	{
		if (g_frame_timer.duration() > 1000000.0 / frame_rate)
		{
			g_frame_timer.start();
			Frame();
		}
		else Clock::sleep(1000.0 / frame_rate - g_frame_timer.duration() * 0.001);
	}
}

// Entry point for the PDF/browser timer (pokemon_pre.js: app.setInterval).
extern "C"
{
#ifdef __EMSCRIPTEN__
	EMSCRIPTEN_KEEPALIVE
#endif
	void pokemon_tick()
	{
		if (!Main::input.CloseGame) Main::Frame();
	}
}

void Main::Editor()
{
	double ActTime = 0, LastTime = 0, frame_rate = 90;
	bool Keep = true;

	double zoom = 1;
	Clock framerate_timer; framerate_timer.start();

	Map map;
	map.Load(1);

	Camera cam;
	cam.SetPos(0, 0, 0);

	double px, py;
	double time = 17;

	while (Keep && !input.CloseGame)
	{
		if (framerate_timer.duration() >= 1000000.0 / frame_rate)
		{
			//cout << framerate_timer.duration() * 0.001 << endl;
			framerate_timer.start();
			// Inputs
			input.UpdateControllerInputs(true);
			input.UpdateKeyboardInputs(true);
			if (input.GetInput(input.S)) cam.SetY(cam.Y() + 4);
			if (input.GetInput(input.Z)) cam.SetY(cam.Y() - 4);
			if (input.GetInput(input.Q)) cam.SetX(cam.X() - 4);
			if (input.GetInput(input.D)) cam.SetX(cam.X() + 4);

			map.Update();


			// Display
			Gfx::Clear(0.1f, 0.1f, 0.1f, 1.0f);

			cam.Update();
			cam.Perspective();

			map.Display();

			// GUI
			cam.GUI();

			if (!input.CloseGame) Gfx::Present();
			
		}
		else Clock::sleep(1000.0 / frame_rate - framerate_timer.duration() * 0.001);
	}

}

void Main::Create_window(const char * name, int x, int y, int w, int h)
{
	(void)name; (void)x; (void)y;
	Disp_w = w; Disp_h = h;
	Display_info::width = w; Display_info::height = h;

	// Software renderer owns the framebuffer (and, on native builds, the window).
	Gfx::Init(w, h);
	Display_info::renderer = renderer;
}



int main(int argc, char *argv[])
{
	Main::Init();

	Main::Create_window("Pokemon Grey", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1080, 720);
	//Main::Create_window("Pokemon Grey - Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1400, 900);
	Database::Load();
	Player_info::Load();
	MainInfoGUI::Init();
	PkmnTeamGUI::Init();
	BagGUI::Init();
	FightGUI::Init();
	DialogueGUI::Init();

#ifdef __EMSCRIPTEN__
	// In the PDF/browser, do not run a blocking loop: set up the world and
	// return. pokemon_pre.js drives pokemon_tick() via app.setInterval, and the
	// Emscripten runtime stays alive (EXIT_RUNTIME=0) so exports remain callable.
	Main::SetupGame();
#else
	Main::MainLoop();
	Main::Editor();
	Main::Exit();
#endif

	return 0;
}

