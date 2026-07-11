#ifndef RENDERER_H
#define RENDERER_H

#include "SDL_ttf.h"

// SDL forward
struct SDL_Window;
struct SDL_Renderer;

struct Font
{
	TTF_Font *pTtfFont;
	int size;
};

class Renderer
{
public:

	Renderer( SDL_Window& window, unsigned int logicalWidth, unsigned int logicalHeight );
	~Renderer();

	void			Clear( unsigned int rgba );
	void			Present();

	SDL_Texture*	LoadTexture( const char* imagePath );
	void			FreeTexture( SDL_Texture* pTexture );
	void			DrawTexture( SDL_Texture& texture, int x, int y );
	void			DrawTexture( SDL_Texture& texture, int x, int y, unsigned int width, unsigned int height );

	Font*			LoadFont( const char* path, int size );
	void			FreeFont( Font* pFont );

	unsigned int	GetLogicalWidth() const		{ return m_logicalWidth; }
	unsigned int	GetLogicalHeight() const	{ return m_logicalHeight; }

	void			SetColour( uint32_t rgba );

	void			DrawRect( int x, int y, int w, int h, uint32_t rgba = 0xffffffff );
	void			DrawSolidRect( int x, int y, int w, int h, uint32_t rgba = 0xffffffff );
	void			DrawText( const Font& font, const char* text, int x, int y, uint32_t rgba = 0xffffffff );
	void			GetTextDimensions( const Font& font, const char* text, int& width, int& height );
	
	void			DrawLine( int x1, int y1, int x2, int y2, uint32_t rgba = 0xffffffff );

	void			DrawDebugCircle( int x, int y, int radius, unsigned int rgba );

	// TODO: Remove this
	SDL_Renderer* GetSdlRenderer() const { return m_pSdlRenderer; }

private:

	unsigned int	m_logicalWidth = 0;
	unsigned int	m_logicalHeight = 0;

	SDL_Renderer*	m_pSdlRenderer = nullptr;
};

#endif // RENDERER_H
