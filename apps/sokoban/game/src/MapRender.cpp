#include "MapRender.h"

#include "Renderer.h"
#include "TileMap.h"
#include "Helpers.h"
#include "hp_assert.h"

//--------------------------------------------------------------------------------------------------

// This strips off the top bits, which are used for flipping etc. See TMX docs
int gid_clear_flags( unsigned int gid )
{
	return gid & TMX_FLIP_BITS_REMOVAL;
}

//--------------------------------------------------------------------------------------------------

void MapRender::RenderToTexture( Renderer& renderer, TileMap& tileMap, SDL_Texture& texture, bool bDebugRenderObjectLayers /*= false */ )
{
	SDL_Renderer* pSdlRenderer = renderer.GetSdlRenderer();

	tmx_map* pMap = tileMap.GetMap();
	tmx_layer* pLayers = pMap->ly_head;		// head of linked list

	SDL_SetRenderTarget( pSdlRenderer, &texture );

	// convert xRGB to RGBA
	unsigned int rgba = tileMap.GetBackgroundRGBA();
	renderer.SetColour( rgba );
	SDL_RenderClear( pSdlRenderer );

	while( pLayers )
	{
		if( pLayers->visible )
		{
			if( (pLayers->type == L_OBJGR) && bDebugRenderObjectLayers )
			{
				DebugDrawObjectLayer( renderer, pLayers->content.objgr, tileMap );
			}
			else if( pLayers->type == L_IMAGE )
			{
				DrawImageLayer( renderer, pLayers->content.image );
			}
			else if( pLayers->type == L_LAYER )
			{
				DrawTileLayer( renderer, tileMap, pLayers );
			}
		}
		pLayers = pLayers->next;
	}

	SDL_SetRenderTarget( pSdlRenderer, NULL );
}

//--------------------------------------------------------------------------------------------------

void MapRender::DrawPolyline( Renderer& renderer, double **points, double x, double y, int numPoints )
{
	SDL_Renderer *pSdlRenderer = renderer.GetSdlRenderer();

	for( int i = 1; i < numPoints; i++ )
	{
		SDL_RenderDrawLine( pSdlRenderer, (int)(x + points[i - 1][0]), (int)(y + points[i - 1][1]), (int)(x + points[i][0]), (int)(y + points[i][1]) );
	}
}


void MapRender::DrawPolygon( Renderer& renderer, double **points, double x, double y, int numPoints )
{
	SDL_Renderer *pSdlRenderer = renderer.GetSdlRenderer();

	DrawPolyline( renderer, points, x, y, numPoints );
	if( numPoints > 2 )
	{
		SDL_RenderDrawLine( pSdlRenderer, (int)(x + points[0][0]), (int)(y + points[0][1]), (int)(x + points[numPoints - 1][0]), (int)(y + points[numPoints - 1][1]) );
	}
}

void MapRender::DrawTileAtTileCoord( Renderer& renderer, TileMap& tileMap, int gid, int ix, int iy )
{
	if(gid == 0)
		return;

	tmx_map* pMap = tileMap.GetMap();
	tmx_tile *pTile = tmx_get_tile( pMap, gid );
	tmx_tileset *pTileset = pTile->tileset;
	int x = ix*pTileset->tile_width;
	int y = iy*pTileset->tile_height;
	DrawTileAtPixelCoord( renderer, tileMap, gid, x, y );
}

void MapRender::DrawTileAtPixelCoord( Renderer& renderer, TileMap& tileMap, int gid, int x, int y )
{
	tmx_map* pMap = tileMap.GetMap();

	SDL_Rect srcrect;
	tmx_tile *pTile = tmx_get_tile( pMap, gid );
	tmx_tileset *pTileSet = pTile->tileset;
	srcrect.x = pTile->ul_x;
	srcrect.y = pTile->ul_y;
	srcrect.w = pTileSet->tile_width;
	srcrect.h = pTileSet->tile_height;

	SDL_Rect dstrect;
	dstrect.x = x;
	dstrect.y = y;
	dstrect.w = pTileSet->tile_width;
	dstrect.h = pTileSet->tile_height;

	SDL_RenderCopy( renderer.GetSdlRenderer(), tileMap.GetTextureForTileset( pTileSet ), &srcrect, &dstrect );
}

void MapRender::DrawTileLayer( Renderer& renderer, TileMap& tileMap, tmx_layer* pLayer )
{
	tmx_map* pMap = tileMap.GetMap();

	// TODO: Use opacity
	float opacity = (float)pLayer->opacity;
	HP_UNUSED( opacity );

	for( unsigned int iy = 0; iy < pMap->height; iy++ )
	{
		for( unsigned int ix = 0; ix < pMap->width; ix++ )
		{
			unsigned int gid = pLayer->content.gids[ (iy * pMap->width) + ix ];
			DrawTileAtTileCoord( renderer, tileMap, gid, ix, iy );
		}
	}
}


void MapRender::DebugDrawObjectLayer( Renderer& renderer, tmx_object_group *pObjectGroup, TileMap& tileMap )
{
	// convert xRGB to RGBA
	unsigned int rgba = (pObjectGroup->color << 8) | 0xff;
	renderer.SetColour( rgba );

	SDL_Renderer *pSdlRenderer = renderer.GetSdlRenderer();

	renderer.SetColour( rgba );

	tmx_object *pHead = pObjectGroup->head; 
	while( pHead )
	{
		if( pHead->visible )
		{
			switch( pHead->obj_type)
			{
				case OT_NONE:
					break;
				case OT_SQUARE:
				{
					renderer.SetColour( rgba );
					SDL_Rect rect;
					rect.x = (int)pHead->x;
					rect.y = (int)pHead->y;
					rect.w = (int)pHead->width;
					rect.h = (int)pHead->height;
					SDL_RenderDrawRect( pSdlRenderer, &rect );
					break;
				}
				case OT_POLYGON:
				{
					tmx_shape* pShape = pHead->content.shape;
					DrawPolygon(renderer, pShape->points, pHead->x, pHead->y, pShape->points_len);
					break;
				}
				case OT_POLYLINE:
					HP_ASSERT( 0, "Implement me" );
					// draw_polyline( ren, head->points, head->x, head->y, head->points_len );
					break;
				case OT_ELLIPSE:
					HP_ASSERT( 0, "Implement me" );
					break;
				case OT_TILE:
				{
					// Tile object
					// n.b. The origin of Tile Objects is their BOTTOM LEFT CORNER! https://github.com/bjorn/tiled/issues/386
					// This means that on a 32 by 32 grid, to position a tile object at tile (0,0) its position must be (0,32) ! Utterly nuts.
					// I want the origin to be at the top right for consistency

					tmx_map* pMap = tileMap.GetMap();
					int y = (int)pHead->y - pMap->tile_height;		// Origin at bottom left! See above comment.
					DrawTileAtPixelCoord( renderer, tileMap, pHead->content.gid, (int)pHead->x, y );
					break;
				}
				default:
					HP_ASSERT( 0, "Unknown shape" );
			}

		}
		pHead = pHead->next;
	}
}

void MapRender::DrawImageLayer( Renderer& renderer, tmx_image *img )
{
	SDL_Renderer *pSdlRenderer = renderer.GetSdlRenderer();

	SDL_Surface *pSurface = (SDL_Surface*)img->resource_image;

	//  TODO: Move this per-frame allocation to init!
	SDL_Texture *pTexture = SDL_CreateTextureFromSurface( pSdlRenderer, pSurface );
	if( pTexture )
	{
		SDL_Rect dstrect;
		dstrect.x = dstrect.y = 0;
		dstrect.w = pSurface->w;
		dstrect.h = pSurface->h;
		SDL_RenderCopy( pSdlRenderer, pTexture, NULL, &dstrect );

		//  TODO: Move this per-frame deallocation to shutdown!
		SDL_DestroyTexture( pTexture );
	}
}
