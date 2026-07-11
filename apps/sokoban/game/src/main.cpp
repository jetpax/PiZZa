
#include "App.h"

#include "SDL.h"

#include <stdio.h>

int main( int argc, char** argv )
{
	bool bFullScreen = false;
	unsigned int displayWidth = 640;
	unsigned int displayHeight = 480;
	for( int i = 1; i < argc; ++i )
	{
		if(SDL_strcmp( argv[i], "--fullscreen" ) == 0 )
		{
			bFullScreen = true;
		}
		else if(SDL_strcmp( argv[i], "--width" ) == 0)
		{
			SDL_assert( argc > i ); // make sure we have another argument
			displayWidth = (unsigned int)SDL_atoi( argv[++i] );
		}
		else if(SDL_strcmp( argv[i], "--height" ) == 0)
		{
			SDL_assert( argc > i ); // make sure we have another argument
			displayHeight = (unsigned int)SDL_atoi( argv[++i] );
		}
	}

	App app;
	if( !app.Init( bFullScreen, displayWidth, displayHeight ) )
	{
		printf( "ERROR - App failed to initialise\n" );
		app.ShutDown();
		return 1;
	}

	app.Run();

	app.ShutDown();

	return 0;
}
