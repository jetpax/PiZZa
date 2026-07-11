#ifndef TILE_MAP_H
#define TILE_MAP_H

#include "tmx.h"

class Renderer;
struct SDL_Texture;

class TileMap
{
public:

	TileMap();
	~TileMap();

	void			LoadTMX( const char* filename );
	void			LoadTilesetTextures( Renderer& renderer );

	tmx_map*		GetMap() const { return m_pMap; }
	SDL_Texture*	GetTextureForTileset( tmx_tileset* pTileSet );

	unsigned int	GetTileWidth() const	{ return m_pMap->tile_width; }
	unsigned int	GetTileHeight() const	{ return m_pMap->tile_height; }
	unsigned int	GetWidthPixels() const;
	unsigned int	GetHeightPixels() const;

	unsigned int	GetNumTileLayers();
	unsigned int	GetLayerGid( tmx_layer* pLayer, unsigned int tileX, unsigned int tileY );

	unsigned int	GetBackgroundRGBA() const;

private:

	void			FreeTilesetTextures();

	tmx_map* m_pMap = nullptr;

	struct TilesetTexture
	{
		tmx_tileset* pTileset = nullptr;
		SDL_Texture* pTexture = nullptr;
	};

	static const unsigned int kMaxTilesets = 8;
	TilesetTexture m_tilesetTextures[kMaxTilesets];
	unsigned int m_numTilesetTextures = 0;
};


#endif // TILE_MAP_H