#ifndef MAP_RENDER_H
#define MAP_RENDER_H

#include "tmx.h"

class Renderer;
class Texture;
class TileMap;

struct SDL_Texture;

class MapRender
{
public:

	void RenderToTexture( Renderer& renderer, TileMap& tileMap, SDL_Texture& texture, bool bDebugRenderObjectLayers = false );
	void DrawTileAtPixelCoord( Renderer& renderer, TileMap& tileMap, int gid, int x, int y );

private:

	void DrawPolyline( Renderer& renderer, double **points, double x, double y, int numPoints );
	void DrawPolygon( Renderer& renderer, double **points, double x, double y, int numPoints );
	void DebugDrawObjectLayer( Renderer& renderer, tmx_object_group *objgr, TileMap& tileMap );
	void DrawTileAtTileCoord( Renderer& renderer, TileMap& tileMap, int gid, int ix, int iy );
	void DrawTileLayer( Renderer& renderer, TileMap& tileMap, tmx_layer *layer );
	void DrawImageLayer( Renderer& renderer, tmx_image *img );

};

#endif // MAP_RENDER_H