#include "TileMap.h"

#include "Renderer.h"
#include "hp_assert.h"

TileMap::TileMap()
{
}

TileMap::~TileMap()
{
	FreeTilesetTextures();

	tmx_map_free( m_pMap );
	m_pMap = nullptr;
}

void TileMap::LoadTMX( const char * filename )
{
	if( m_pMap )
	{
		FreeTilesetTextures();
		tmx_map_free( m_pMap );
		m_pMap = nullptr;
	}

	// Load .tmx map (created with Tiled tile map editor)
	// n.b. TMX cannot load .tmx files with Tile Layers stored in XML format. Base64 (zlib) works fine though.
	m_pMap = tmx_load( filename );
	if( !m_pMap )
	{
		tmx_perror( "Failed to load map file" );
	}
	HP_ASSERT( m_pMap, "Failed to load map file" );
}

void TileMap::LoadTilesetTextures( Renderer& renderer )
{
	tmx_tileset_list* pList = m_pMap->ts_head;
	
	while(pList)
	{
		tmx_tileset* pTileset = pList->tileset;
		if(pTileset)
		{
			HP_ASSERT(m_numTilesetTextures < kMaxTilesets, "Increase array size!");
			m_tilesetTextures[m_numTilesetTextures].pTexture = SDL_CreateTextureFromSurface(renderer.GetSdlRenderer(), (SDL_Surface*)pTileset->image->resource_image);
			HP_ASSERT(m_tilesetTextures[m_numTilesetTextures].pTexture);
			m_tilesetTextures[m_numTilesetTextures].pTileset = pTileset;
			pList = pList->next;
		}
		++m_numTilesetTextures;
	}
}

void TileMap::FreeTilesetTextures()
{
	for( unsigned int i = 0; i < m_numTilesetTextures; ++i )
	{
		SDL_DestroyTexture( m_tilesetTextures[i].pTexture );
		m_tilesetTextures[i] = {};
	}
	m_numTilesetTextures = 0;
}

unsigned int TileMap::GetNumTileLayers()
{
	unsigned int numTileLayers = 0;
	tmx_layer* pLayers = m_pMap->ly_head;		// head of linked list

	while( pLayers )
	{
		if( pLayers->visible )
		{
			if( pLayers->type == L_LAYER )
			{
				++numTileLayers;
			}
		}
		pLayers = pLayers->next;
	}

	return numTileLayers;
}

unsigned int TileMap::GetLayerGid( tmx_layer* pLayer, unsigned int tileX, unsigned int tileY )
{
	HP_ASSERT( pLayer );
	HP_ASSERT( tileX < m_pMap->width );
	HP_ASSERT( tileY < m_pMap->height );
	return pLayer->content.gids[(tileY*m_pMap->width) + tileX];
}

unsigned int TileMap::GetBackgroundRGBA() const
{
	// convert xRGB to RGBA
	unsigned int rgba = (m_pMap->backgroundcolor << 8) | 0xff;
	return rgba;
}

SDL_Texture* TileMap::GetTextureForTileset( tmx_tileset * pTileSet )
{
	for( unsigned int i = 0; i < m_numTilesetTextures; ++i )
	{
		if( m_tilesetTextures[i].pTileset == pTileSet )
		{
			return m_tilesetTextures[i].pTexture;
		}
	}

	return nullptr;
}

unsigned int TileMap::GetWidthPixels() const
{
	return m_pMap->width  * m_pMap->tile_width;
}

unsigned int TileMap::GetHeightPixels() const
{
	return m_pMap->height * m_pMap->tile_height;
}
