#include "App.h"

#include "Game.h"
#include "Renderer.h"
#include "Helpers.h"

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"

#include <stdio.h>
// PiZZa: <chrono> replaced with SDL_GetPerformanceCounter (the upstream TODO)

//--------------------------------------------------------------------------------------------------

static void print_SDL_version( const char* preamble, const SDL_version& v )
{
	printf( "%s %u.%u.%u\n", preamble, v.major, v.minor, v.patch );
}

static bool operator==( SDL_version& a, SDL_version& b )
{
	return (a.major == b.major) && (a.minor == b.minor) && (a.patch == b.patch);
}

//--------------------------------------------------------------------------------------------------

App::App()
{

}

bool App::Init( bool bFullScreen, unsigned int displayWidth, unsigned int displayHeight )
{
	if( SDL_Init( SDL_INIT_EVERYTHING ) != 0)
	{
		fprintf( stderr, "SDL failed to initialise: %s\n",SDL_GetError() );
		return false;
	}

	printf( "SDL initialised\n" );

	SDL_version compiledVersion;
	SDL_version linkedVersion;
	SDL_VERSION( &compiledVersion );
	SDL_GetVersion( &linkedVersion );
	print_SDL_version( "Compiled against SDL version", compiledVersion );
	print_SDL_version( "Linking against SDL version", linkedVersion );
	SDL_assert_release( (compiledVersion == linkedVersion) );

	// SDL_image
	const int sdlImageInitFlags = IMG_INIT_JPG |IMG_INIT_PNG | IMG_INIT_TIF;
	if( IMG_Init( sdlImageInitFlags ) != sdlImageInitFlags )
	{
		fprintf( stderr, "Failed to initialise SDL_image: %s\n", IMG_GetError() );
		return false;
	}
	printf( "SDL_image initialised\n" );
	SDL_IMAGE_VERSION(&compiledVersion);
	const SDL_version *pLinkedVersion = IMG_Linked_Version();
	print_SDL_version( "Compiled against SDL_image version", compiledVersion );
	print_SDL_version( "Linking against SDL_image version", *pLinkedVersion );

	// SDL2_ttf

	if( TTF_Init() == -1 )
	{
		fprintf( stderr, "Failed to initialise SDL2_ttf: %s\n", TTF_GetError() );
		return false;
	}

	printf( "SDL_ttf initialised\n" );

	SDL_TTF_VERSION( &compiledVersion );
	pLinkedVersion = TTF_Linked_Version();
	print_SDL_version( "Compiled against SDL_ttf version", compiledVersion );
	print_SDL_version( "Linking against SDL_ttf version", *pLinkedVersion );

	// Create SDL window
	const char* title = "Sokoban";
	if( bFullScreen )
	{
		m_pWindow = SDL_CreateWindow( title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP );
	}
	else
	{
		m_pWindow = SDL_CreateWindow( title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, displayWidth, displayHeight, SDL_WINDOW_SHOWN );
	}

	if( !m_pWindow )
	{
		printf( "Failed to create SDL window: %s\n", SDL_GetError() );
		return false;
	}

	unsigned int logicalWidth = 640;
	unsigned int logicalHeight = 480;
	m_pRenderer = new Renderer( *m_pWindow, logicalWidth, logicalHeight );

	/* You probably want to create a fuction that creates a SDL_Texture directly here */
	// TODO: Seems that the example code creates and destroys a texture each time it uses one. See if can fix this.
	tmx_img_load_func = (void* (*)(const char*))IMG_Load;
	tmx_img_free_func = (void( *)(void*))      SDL_FreeSurface;

	m_pGame = new Game();
	if( !m_pGame->Init( *m_pRenderer ) )
	{
		printf( "ERROR - Game failed to initialise\n" );
		return false;
	}

	return true;
}

void App::ShutDown()
{
	if( m_pGame )
	{
		m_pGame->Shutdown( *m_pRenderer );
		delete m_pGame;
		m_pGame = nullptr;
	}

	delete m_pRenderer;
	m_pRenderer = nullptr;

	TTF_Quit();	// SDL2_TTF
	IMG_Quit();	// SDL2_image

	SDL_DestroyWindow( m_pWindow );
	SDL_Quit();
}

void App::Run()
{
	Uint32 lastTimeMs = SDL_GetTicks();	// only ms accuracy

	// PiZZa: SDL perf counters instead of <chrono> (the upstream TODO)
	Uint64 lastTime = SDL_GetPerformanceCounter();
	const Uint64 perfFreq = SDL_GetPerformanceFrequency();

	bool bDone = false;
	while( !bDone )
	{
		GameInput gameInput = { 0 };

		// respond to events
		SDL_Event event;
		while( SDL_PollEvent( &event ) )
		{
			if(event.type == SDL_QUIT)
			{
				bDone = true;
			}

			// quit if escape pressed
			if(event.type == SDL_KEYDOWN)
			{
				if(event.key.keysym.sym == SDLK_ESCAPE)
				{
					bDone = true;
				}
				else if( event.key.keysym.sym == SDLK_SPACE )
				{
					gameInput.bSpacePressed = true;
				}
				else if( event.key.keysym.sym == SDLK_o )
				{
					gameInput.bStartOriginalLevels = true;
				}
				else if( event.key.keysym.sym == SDLK_m )
				{
					gameInput.bStartMicrobanLevels = true;
				}
				else if( event.key.keysym.sym == SDLK_p )
				{
					gameInput.bPause = true;
				}
				else if( event.key.keysym.sym == SDLK_r )
				{
					gameInput.bRestartLevel = true;
				}
				else if( event.key.keysym.sym == SDLK_u )
				{
					gameInput.bUndo = true;
				}
#ifdef _DEBUG
				else if( event.key.keysym.sym == SDLK_s )
				{
					gameInput.bDebugSkipLevel = true;
				}
#endif
			}
		}

		const Uint8* keys = SDL_GetKeyboardState( NULL );

		// Supposed to call this to update the state array, but seems to work just fine without it.
		// https://wiki.libsdl.org/SDL_GetKeyboardState
		SDL_PumpEvents();

		if( keys[SDL_SCANCODE_LEFT] )
		{
			gameInput.bLeft = true;
		}
		if( keys[SDL_SCANCODE_RIGHT] )
		{
			gameInput.bRight = true;
		}
		if( keys[SDL_SCANCODE_UP] )
		{
			gameInput.bUp = true;
		}
		if( keys[SDL_SCANCODE_DOWN] )
		{
			gameInput.bDown = true;
		}

		Uint32 currentTimeMs = SDL_GetTicks();
		Uint32 deltaTimeMs = currentTimeMs - lastTimeMs;
		lastTimeMs = currentTimeMs;
		HP_UNUSED( deltaTimeMs );		// This is useless! Oscillates between 16 and 17ms. Not accurate enough.
		
		Uint64 currentTime = SDL_GetPerformanceCounter();
		float deltaTimeSeconds = (float)(currentTime - lastTime) / (float)perfFreq;
		lastTime = currentTime;

#ifdef _DEBUG
		if( deltaTimeSeconds > 1.0f )
		{
			// probably stopped on a breakpoint
			deltaTimeSeconds = 1.0f / 60.0f;
		}
#endif

		m_pGame->Update( gameInput, deltaTimeSeconds, *m_pRenderer );

		m_pGame->Draw( *m_pRenderer );
		m_pRenderer->Present();
	}

}
