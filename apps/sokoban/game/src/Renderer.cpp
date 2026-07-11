#include "Renderer.h"

#include "hp_assert.h"
#include "Helpers.h"

#include "SDL.h"
#include "SDL_image.h"

#include <stdio.h>

//--------------------------------------------------------------------------------------------------
// Helpers

static SDL_Color MakeSDL_Colour( uint32_t rgba )
{
	SDL_Color color;
	color.r = (unsigned char)( ( rgba >> 24 ) & 0xff );
	color.g = (unsigned char)( ( rgba >> 16 ) & 0xff );
	color.b = (unsigned char)( ( rgba >> 8 ) & 0xff );
	color.a = (unsigned char)( ( rgba >> 0 ) & 0xff );		// alpha
	return color;
}

//--------------------------------------------------------------------------------------------------

Renderer::Renderer( SDL_Window& window, unsigned int logicalWidth, unsigned int logicalHeight )
{
	Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
	m_pSdlRenderer = SDL_CreateRenderer( &window, -1, rendererFlags );
	if(!m_pSdlRenderer)
	{
		printf( "Failed to create SDL renderer: %s\n", SDL_GetError() );
		SDL_assert( false );
	}

	// n.b. The display size may not equal the logical size
 	int displayWidth, displayHeight;
 	SDL_GetWindowSize( &window, &displayWidth, &displayHeight );
	printf( "Display size = (%d, %d)\n", displayWidth, displayHeight );
	printf( "Renderer logical size = (%u, %u)\n", logicalWidth, logicalHeight );
	if( displayWidth != (int)logicalWidth || displayHeight != (int)logicalHeight )
	{
		printf( "Logical size != display size (%u, %u) vs (%u, %u). Scaling will be applied\n", logicalWidth, logicalHeight, displayWidth, displayHeight );
	}
	const float displayAspect = (float)displayWidth / (float)displayHeight;
	const float logicalAspect = (float)logicalWidth / (float)logicalHeight;
	if( logicalAspect != displayAspect)
	{
		printf( "Logical aspect != display aspect. Letterboxing will be applied\n" );
	}

	m_logicalWidth = logicalWidth;
	m_logicalHeight = logicalHeight;
	SDL_RenderSetLogicalSize( m_pSdlRenderer, logicalWidth, logicalHeight );

	SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "linear" );  // make the scaled rendering look smoother

}

Renderer::~Renderer()
{
	SDL_DestroyRenderer( m_pSdlRenderer );
}

void Renderer::Clear( unsigned int rgba )
{
	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_SetRenderDrawColor( m_pSdlRenderer, color.r, color.g, color.b, color.a );
	SDL_RenderClear( m_pSdlRenderer );
}

void Renderer::Present()
{
	SDL_RenderPresent( m_pSdlRenderer );
}

SDL_Texture* Renderer::LoadTexture( const char* imagePath )
{
	SDL_assert( imagePath );
	SDL_Surface* pSurface = IMG_Load( imagePath );
	if( !pSurface )
	{
		printf( "Failed to load image from file %s: %s\n", imagePath, SDL_GetError() );
		return nullptr;
	}

	SDL_Texture* pSdlTexture = SDL_CreateTextureFromSurface( m_pSdlRenderer, pSurface );
	SDL_FreeSurface( pSurface );
	pSurface = nullptr;
	if( !pSdlTexture )
	{
		printf( "Failed to create texture from surface: %s\n", SDL_GetError() );
		return nullptr;
	}

	return pSdlTexture;
}

void Renderer::FreeTexture( SDL_Texture* pTexture )
{
	SDL_DestroyTexture( pTexture );
}

void Renderer::DrawTexture( SDL_Texture& texture, int x, int y )
{
	int width;
	int height;
	SDL_QueryTexture( &texture, nullptr, nullptr, &width, &height );
	SDL_Rect dstRect = {x, y, width, height};

	SDL_RenderCopy( m_pSdlRenderer, &texture, nullptr, &dstRect );
}

void Renderer::DrawTexture( SDL_Texture& texture, int x, int y, unsigned int width, unsigned int height )
{
	SDL_Rect dstRect = {x, y, (int)width, (int)height};
	SDL_RenderCopy( m_pSdlRenderer, &texture, nullptr, &dstRect );
}

Font* Renderer::LoadFont( const char* path, int size )
{
	TTF_Font* pTtfFont = TTF_OpenFont( path, size );
	if( !pTtfFont )
	{
		fprintf( stderr, "Failed to open font \"%s\" Error: %s\n", path, TTF_GetError() );
		HP_ASSERT( pTtfFont, "Failed to open font" );
		return nullptr;
	}
	Font* pFont = new Font;
	pFont->pTtfFont = pTtfFont;
	pFont->size = size;
	return pFont;
}

void Renderer::FreeFont( Font* pFont )
{
	TTF_CloseFont( pFont->pTtfFont );
}

void Renderer::SetColour( uint32_t rgba )
{
	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_SetRenderDrawColor( m_pSdlRenderer, color.r, color.g, color.b, color.a );
}

void Renderer::DrawRect( int x, int y, int w, int h, uint32_t rgba /*= 0xffffffff */ )
{
	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_SetRenderDrawColor( m_pSdlRenderer, color.r, color.g, color.b, color.a );

	SDL_Rect rect = {x, y, w, h};
	SDL_RenderDrawRect( m_pSdlRenderer, &rect );
}

void Renderer::DrawSolidRect( int x, int y, int w, int h, uint32_t rgba /*= 0xffffffff */ )
{
	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_SetRenderDrawColor( m_pSdlRenderer, color.r, color.g, color.b, color.a );

	SDL_Rect rect = { x, y, w, h };
	SDL_RenderFillRect( m_pSdlRenderer, &rect );
}

void Renderer::DrawText( const Font& font, const char* text, int x, int y, uint32_t rgba /*= 0xffffffff */ )
{
	SDL_assert( text );

	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_Surface* pSurface = TTF_RenderText_Blended( font.pTtfFont, text, color );
	SDL_Texture* pTexture = SDL_CreateTextureFromSurface( m_pSdlRenderer, pSurface );
	int width, height;
	SDL_QueryTexture(pTexture, NULL, NULL, &width, &height);
	SDL_Rect dstRect = { x, y, width, height };
	SDL_RenderCopy( m_pSdlRenderer, pTexture, nullptr, &dstRect );
	SDL_DestroyTexture( pTexture );
	SDL_FreeSurface( pSurface );
}

void Renderer::GetTextDimensions( const Font& font, const char* text, int& width, int& height )
{
	SDL_Color color = {};
	SDL_Surface* pSurface = TTF_RenderText_Blended( font.pTtfFont, text, color );
	SDL_Texture* pTexture = SDL_CreateTextureFromSurface( m_pSdlRenderer, pSurface );
	SDL_QueryTexture( pTexture, NULL, NULL, &width, &height );
	SDL_DestroyTexture( pTexture );
	SDL_FreeSurface( pSurface );
}

void Renderer::DrawLine( int x1, int y1, int x2, int y2, uint32_t rgba )
{
	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_SetRenderDrawColor( m_pSdlRenderer, color.r, color.g, color.b, color.a );

	SDL_RenderDrawLine( m_pSdlRenderer, x1, y1, x2, y2 );
}

void Renderer::DrawDebugCircle( int x, int y, int boundingRadius, unsigned int rgba )
{
	SDL_Color color = MakeSDL_Colour( rgba );
	SDL_SetRenderDrawColor( m_pSdlRenderer, color.r, color.g, color.b, color.a );

	// hacky method
	const unsigned int numSegments = 16;
	for( unsigned int i = 0; i < numSegments; ++i )
	{
		float x1 = x + (float)boundingRadius * sinf( TWOPI * (float)i / numSegments );
		float y1 = y + (float)boundingRadius * cosf( TWOPI * (float)i / numSegments );
		float x2 = x + (float)boundingRadius * sinf( TWOPI * (float)(i+1) / numSegments );
		float y2 = y + (float)boundingRadius * cosf( TWOPI * (float)(i+1) / numSegments );
		SDL_RenderDrawLine( m_pSdlRenderer, (int)x1, (int)y1, (int)x2, (int)y2 );
	}
}
