#ifndef APP_H
#define APP_H

// SDL forward
struct SDL_Window;

class Game;
class Renderer;

// The application layer. Responsible for managing major systems e.g. Rendering, audio, input, game.

class App
{
public:

	App();

	bool	Init( bool bFullScreen, unsigned int logicalWidth, unsigned int logicalHeight );
	void	ShutDown();
	void	Run();

private:

	bool	InitAudio();

	SDL_Window*			m_pWindow			= nullptr;
	Renderer*			m_pRenderer			= nullptr;
	Game*				m_pGame				= nullptr;
};

#endif // APP_H
