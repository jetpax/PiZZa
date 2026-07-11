#include "Game.h"

#include "Renderer.h"

#include "hp_assert.h"
#include "Helpers.h"

//static const unsigned int kGid_Empty = 0; // unused
static const unsigned int kGid_Wall = 1;
static const unsigned int kGid_Floor = 2;
static const unsigned int kGid_Man = 3;
static const unsigned int kGid_Box = 4;
static const unsigned int kGid_Goal = 5;
static const unsigned int kGid_BoxOnGoal = 6;

static const unsigned int kMoveSpeedPixelsPerFrame = 2;	

//--------------------------------------------------------------------------------------------------

Game::Game()
	: m_time( 0.0 )
	, m_deltaTimeSeconds( 0.0f )
{
	m_camera.pixelOffset = {};
}

Game::~Game()
{
}

bool Game::Init( Renderer& renderer )
{
	m_pFont = renderer.LoadFont( "Font/ARCADE_N.TTF", 8 );

	m_man = {};
	m_man.position = {};
	m_man.tileGid = kGid_Man;

	for( unsigned int i = 0; i < kMaxBoxes; ++i )
	{
		m_boxes[i] = {};
		m_boxes[i].position = {};
	}

	for( unsigned int i = 0; i < kMaxGoals; ++i )
	{
		m_goals[i] = {};
		m_goals[i].position = {};
	}

	return true;
}

bool Game::InitLevel( unsigned int levelNumber, Renderer& renderer )
{
	m_currentLevel = levelNumber;

	char mapfilename[64];
//	SDL_snprintf(mapfilename, sizeof(mapfilename), "test.tmx" );
	SDL_snprintf(mapfilename, sizeof(mapfilename), "%s/level_%03u.tmx", m_mapFolder, levelNumber);

	FILE* fp = fopen(mapfilename, "r");
	if(fp == NULL)
	{
		// file doesn't exist
		return false;
	}
	fclose(fp);
	fp = NULL;

	m_tileMap.LoadTMX( mapfilename );
	HP_ASSERT( m_tileMap.GetNumTileLayers() == 1, "there should only be a single Tile Layer containing the walls, floor etc" );

	m_tileMap.LoadTilesetTextures( renderer );

	int widthPixels = m_tileMap.GetWidthPixels();
	int heightPixels = m_tileMap.GetHeightPixels();

	if( m_pMapTexture )
	{
		SDL_DestroyTexture( m_pMapTexture );
		m_pMapTexture = nullptr;
	}
	m_pMapTexture = SDL_CreateTexture( renderer.GetSdlRenderer(), SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, widthPixels, heightPixels );
	if( !m_pMapTexture )
	{
		fprintf( stderr, "Failed to create tilemap texture: %s\n", SDL_GetError() );
		HP_ASSERT( 0, "Failed to create tilemap texture" );
	}

	m_mapRender.RenderToTexture( renderer, m_tileMap, *m_pMapTexture );

	InitObjects();

	m_stackPointer = 0;

	return true;
}

void Game::InitObjects()
{
	m_numBoxes = 0;
	m_numGoals = 0;

	tmx_map* pMap = m_tileMap.GetMap();
	tmx_layer* pLayers = pMap->ly_head;		// head of linked list

	bool bFoundStartPosition = false;
	HP_UNUSED(bFoundStartPosition);
	while( pLayers )
	{
		if( pLayers->type == L_OBJGR )
		{
			tmx_object* pObject = pLayers->content.objgr->head;	// linked list

			while( pObject )
			{
				if( pObject->visible )
				{
					if( pObject->obj_type == OT_TILE )
					{
						// Tile object
						// n.b. The origin of Tile Objects is their BOTTOM LEFT CORNER! https://github.com/bjorn/tiled/issues/386
						// This means that on a 32 by 32 grid, to position a tile object at tile (0,0) its position must be (0,32) ! Utterly nuts.
						int x = (int)pObject->x;
						int y = (int)pObject->y - pMap->tile_height;		// Origin at bottom left! See above comment.
// 						int w = (int)pObject->width;
// 						int h = (int)pObject->height;

						int tileX = x / pMap->tile_width;
						int tileY = y / pMap->tile_height;

						if( pObject->content.gid == kGid_Man )
						{
							HP_ASSERT( !bFoundStartPosition, "Level has multiple start positions" );
							m_man.position.tileX = tileX;
							m_man.position.tileY = tileY;
							bFoundStartPosition = true;
						}
						else if( pObject->content.gid == kGid_Box )
						{
							AddBox( tileX, tileY );
						}
						else if( pObject->content.gid == kGid_Goal )
						{
							AddGoal( tileX, tileY );
						}
						else if( pObject->content.gid == kGid_BoxOnGoal )
						{
							AddBox( tileX, tileY );
							AddGoal( tileX, tileY );
						}
					}
				}
				pObject = pObject->next;
			}
		}
		pLayers = pLayers->next;
	}

	m_man.bMoving = false;
	m_man.position.pixelOffsetX = 0;
	m_man.position.pixelOffsetY = 0;

	HP_ASSERT( bFoundStartPosition, "Level has no start position" );
	HP_ASSERT( m_numGoals > 0, "Level has no goals" );
	HP_ASSERT( (m_numGoals == m_numBoxes), "Must be the same number of boxes as goals" );
}

void Game::Shutdown( Renderer& renderer )
{
	SDL_DestroyTexture( m_pMapTexture );
	m_pMapTexture = nullptr;

	renderer.FreeFont( m_pFont );
	m_pFont = nullptr;
}

void Game::Update( const GameInput& gameInput, float deltaTimeSeconds, Renderer& renderer )
{
	m_deltaTimeSeconds = deltaTimeSeconds;

	switch( m_state )
	{
		case kStateTitleScreen:
		{
			bool bStart = false;
			if( gameInput.bStartOriginalLevels )
			{
				SDL_snprintf( m_mapFolder, sizeof(m_mapFolder), "Maps/Original" );
				bStart = true;
			}
			else if( gameInput.bStartMicrobanLevels )
			{
				SDL_snprintf(m_mapFolder, sizeof(m_mapFolder), "Maps/Microban" );
				bStart = true;
			}

			if( bStart )
			{
				InitLevel( 1, renderer );
				m_state = kStatePlaying;
				m_timeInState = 0.0f;
				printf( "[game] title -> playing (%s level %u, %u boxes %u goals)\n",
						m_mapFolder, m_currentLevel, m_numBoxes, m_numGoals ); // PiZZa: state tracing
			}

			break;

		}

		case kStatePlaying:
			UpdatePlaying( gameInput, deltaTimeSeconds, renderer );
			if( m_state != kStatePlaying )
				printf( "[game] playing -> level complete\n" ); // PiZZa: state tracing
			break;

		case kStateLevelComplete:
			if( gameInput.bSpacePressed )
			{
				if( InitLevel( m_currentLevel + 1, renderer ) )
				{
					m_state = kStatePlaying;
					m_timeInState = 0.0f;
					printf( "[game] level complete -> playing (level %u)\n", m_currentLevel ); // PiZZa: state tracing
				}
				else
				{
					m_state = kStateGameComplete;
					m_timeInState = 0.0f;
					printf( "[game] level complete -> game complete\n" ); // PiZZa: state tracing
				}
			}
			break;

		case kStateGameComplete:
			if( gameInput.bSpacePressed )
			{
				m_state = kStateTitleScreen;
				m_timeInState = 0.0f;
				printf( "[game] game complete -> title\n" ); // PiZZa: state tracing
			}
			break;
	}

	m_time += (double)deltaTimeSeconds;
	m_timeInState += deltaTimeSeconds;
}

void Game::AddBox( int tileX, int tileY )
{
	HP_ASSERT( m_numBoxes < kMaxBoxes, "Array overflow" );
	m_boxes[m_numBoxes] = {};
	m_boxes[m_numBoxes].position.tileX = tileX;
	m_boxes[m_numBoxes].position.tileY = tileY;
	m_boxes[m_numBoxes].tileGid = kGid_Box;
	++m_numBoxes;
}

void Game::AddGoal( int tileX, int tileY )
{
	HP_ASSERT( m_numGoals < kMaxGoals, "Array overflow" );
	m_goals[m_numGoals] = {};
	m_goals[m_numGoals].position.tileX = tileX;
	m_goals[m_numGoals].position.tileY = tileY;
	m_goals[m_numGoals].tileGid = kGid_Goal;
	++m_numGoals;
}

void Game::UpdatePlaying( const GameInput& gameInput, float /*deltaTimeSeconds*/, Renderer& renderer )
{
	if( !m_man.bMoving )
		UpdateStatic( gameInput );

	// may have started moving now
	if( m_man.bMoving )
		UpdateMovingObject( m_man );

	for( unsigned int i = 0; i < m_numBoxes; ++i )
	{
		if( m_boxes[i].bMoving )
			UpdateMovingObject( m_boxes[i] );
	}

	if( gameInput.bRestartLevel )
	{
		InitLevel( m_currentLevel, renderer );
	}
	else if( gameInput.bUndo )
	{
		UndoLastMove();
	}

	// Level complete logic
	if( IsLevelComplete() || gameInput.bDebugSkipLevel )
	{
		m_state = kStateLevelComplete;
		m_timeInState = 0.0f;
	}
}

bool Game::IsLevelComplete()
{
	unsigned int numBoxesOnGoals = 0;
	for( unsigned int i = 0; i < m_numBoxes; ++i )
	{
		for( unsigned int j = 0; j < m_numGoals; ++j )
		{
			if( m_boxes[i].position == m_goals[j].position )
			{
				++numBoxesOnGoals;
				break;
			}
		}
	}

	return (numBoxesOnGoals == m_numBoxes );
}

void Game::UpdateStatic(  const GameInput &gameInput )
{
	bool bMoveInput = false;
	Position targetPos = m_man.position;
	if( gameInput.bLeft )
	{
		targetPos.tileX--;
		m_man.moveDirection = kDirection_Left;
		bMoveInput = true;
	}
	if( gameInput.bRight )
	{
		targetPos.tileX++;
		m_man.moveDirection = kDirection_Right;
		bMoveInput = true;
	}
	if( gameInput.bUp )
	{
		targetPos.tileY--;
		m_man.moveDirection = kDirection_Up;
		bMoveInput = true;
	}
	if( gameInput.bDown )
	{
		targetPos.tileY++;
		m_man.moveDirection = kDirection_Down;
		bMoveInput = true;
	}

	if( !bMoveInput )
		return;

	bool bPushingBox = false;
	unsigned int targetGid = GetTileGid( targetPos.tileX, targetPos.tileY );
	if( bMoveInput && (targetGid == kGid_Floor) )
	{
		bool bBoxInTargetTile = false;

		for( unsigned int i = 0; i < m_numBoxes; ++i )
		{
			if( (m_boxes[i].position.tileX == targetPos.tileX) && (m_boxes[i].position.tileY == targetPos.tileY) )
			{
				bBoxInTargetTile = true;

				// Check if there is room to push the box into space
				const int dx = m_boxes[i].position.tileX - m_man.position.tileX;
				const int dy = m_boxes[i].position.tileY - m_man.position.tileY;
				Position targetBoxPos = {targetPos.tileX + dx, targetPos.tileY + dy};
				if( IsTileEmpty( targetBoxPos.tileX, targetBoxPos.tileY ) )
				{
					// start man and box moving
					m_man.bMoving = true;
					m_boxes[i].bMoving = true;
					m_boxes[i].moveDirection = m_man.moveDirection;
					bPushingBox = true;
					break;
				}
			}
		}

		if( !bBoxInTargetTile )
		{
			m_man.bMoving = true;
		}
	}

	// move accepted so push move onto stack
	if( m_man.bMoving )
	{
		HP_ASSERT( m_stackPointer < kStackSize, "Move stack overflow" );
		m_moveStack[m_stackPointer].direction = m_man.moveDirection;
		m_moveStack[m_stackPointer].bPushedBox = bPushingBox;
		++m_stackPointer;
	}
}

void Game::UpdateMovingObject( Object& object ) const
{
	tmx_map* pMap = m_tileMap.GetMap();
	HP_ASSERT( (pMap->tile_width % kMoveSpeedPixelsPerFrame) == 0, "Move speed must be a multiple of tile width" );
	HP_ASSERT( (pMap->tile_height % kMoveSpeedPixelsPerFrame) == 0, "Move speed must be a multiple of tile height" );

	switch( object.moveDirection )
	{
		case kDirection_Left:
			object.position.pixelOffsetX -= kMoveSpeedPixelsPerFrame;
			if( object.position.pixelOffsetX < 0 )
			{
				// starting to move
				object.position.tileX--;
				object.position.pixelOffsetX += pMap->tile_width;
			}
			else if( object.position.pixelOffsetX == 0 )
			{
				// finished moving
				object.bMoving = false;
			}
			return;
		case kDirection_Right:
			object.position.pixelOffsetX += kMoveSpeedPixelsPerFrame;
			if( object.position.pixelOffsetX == (int)pMap->tile_width )
			{
				object.position.tileX++;
				object.position.pixelOffsetX = 0;
				object.bMoving = false;
			}
			return;
		case kDirection_Up:
			object.position.pixelOffsetY -= kMoveSpeedPixelsPerFrame;
			if( object.position.pixelOffsetY < 0 )
			{
				// starting to move
				object.position.tileY--;
				object.position.pixelOffsetY += pMap->tile_height;
			}
			else if ( object.position.pixelOffsetY == 0  )
			{
				// finished moving
				object.bMoving = false;
			}
			return;
		case kDirection_Down:
			object.position.pixelOffsetY += kMoveSpeedPixelsPerFrame;
			if( object.position.pixelOffsetY == (int)pMap->tile_height )
			{
				object.position.tileY++;
				object.position.pixelOffsetY = 0;
				object.bMoving = false;
			}
			return;
	}
}

void Game::UndoLastMove()
{
	if( m_stackPointer == 0 )
		return;

	const Move move = m_moveStack[--m_stackPointer];

	// if a box was pushed it will be in the direction move.direction of the player's current position
	Position boxPosition = m_man.position;
	if( move.bPushedBox )
	{
		switch( move.direction )
		{
			case kDirection_Left:
				boxPosition.tileX--;
				break;
			case kDirection_Right:
				boxPosition.tileX++;
				break;
			case kDirection_Up:
				boxPosition.tileY--;
				break;
			case kDirection_Down:
				boxPosition.tileY++;
				break;
		}

		for( unsigned int i = 0; i < m_numBoxes; ++i )
		{
			if( (m_boxes[i].position.tileX == boxPosition.tileX) && (m_boxes[i].position.tileY == boxPosition.tileY) )
			{
				m_boxes[i].position = boxPosition;
				switch( move.direction )
				{
					case kDirection_Left:
						m_boxes[i].position.tileX++;	// move right
						break;
					case kDirection_Right:
						m_boxes[i].position.tileX--; // move left
						break;
					case kDirection_Up:
						m_boxes[i].position.tileY++; // move down
						break;
					case kDirection_Down:
						m_boxes[i].position.tileY--; // move up
						break;
				}
				break;
			}
		}
	}

	switch( move.direction )
	{
		case kDirection_Left:
			m_man.position.tileX++;	// move right
			break;
		case kDirection_Right:
			m_man.position.tileX--; // move left
			break;
		case kDirection_Up:
			m_man.position.tileY++; // move down
			break;
		case kDirection_Down:
			m_man.position.tileY--; // move up
			break;
	}


}

unsigned int Game::GetTileGid( int tileX, int tileY )
{
	if( tileX < 0 || tileY < 0 )
		return 0;

	// all tiles of interest are stored in the single Tile Layer
	tmx_layer* pLayers = m_tileMap.GetMap()->ly_head;		// head of linked list

	while( pLayers )
	{
		if( pLayers->visible )
		{
			if( pLayers->type == L_LAYER )
			{
				return m_tileMap.GetLayerGid( pLayers, (unsigned int)tileX, (unsigned int)tileY );
			}
		}
		pLayers = pLayers->next;
	}

	HP_ASSERT( 0, "Failed to find Tile Layer" );
	return 0;
}

bool Game::IsTileEmpty( int tileX, int tileY )
{
	unsigned int targetGid = GetTileGid( tileX, tileY );
	if( targetGid == kGid_Wall )
	{
		return false;
	}
	else if( targetGid == kGid_Floor )
	{
		for( unsigned int i = 0; i < m_numBoxes; ++i )
		{
			if( (m_boxes[i].position.tileX == tileX) && (m_boxes[i].position.tileY == tileY) )
			{
				return false;
			}
		}
	}

	return true;
}

void Game::UpdateCamera( Renderer& renderer )
{
	const unsigned int screenWidth = renderer.GetLogicalWidth();
	const unsigned int screenHeight = renderer.GetLogicalHeight();
	const unsigned int mapWidth = m_tileMap.GetWidthPixels();
	const unsigned int mapHeight = m_tileMap.GetHeightPixels();
	const unsigned int tileWidth = m_tileMap.GetTileWidth();
	const unsigned int tileHeight = m_tileMap.GetTileHeight();

	// Make the camera bounds for the map are rectangle one tile size bigger than the map
	// TODO: In a more complex game requiring custom camera bounds, the rect be stored in the .tmx file as an object layer rectangle
	SDL_Rect cameraBounds;
	cameraBounds.x = -1 * tileWidth;
	cameraBounds.y = -1 * tileHeight;
	cameraBounds.w = mapWidth + (2 * tileWidth);
	cameraBounds.h = mapHeight + (2 * tileHeight);

	// horizontal
	if( mapWidth <= screenWidth )
	{
		// map width is smaller than screen width, so centre map in screen
		m_camera.pixelOffset.x = ((int)mapWidth - (int)screenWidth) / 2;
	}
	else
	{
		// Map width is larger than screen width.
		// Centre man horizontally
		const int manPosX = (m_man.position.tileX * tileWidth) + m_man.position.pixelOffsetX;
		m_camera.pixelOffset.x = manPosX - (screenWidth / 2);

		// Constrain camera to map bounds
		const int minCameraX = cameraBounds.x;
		const int maxCameraX = cameraBounds.x + cameraBounds.w - (int)screenWidth;

		if( m_camera.pixelOffset.x < minCameraX )
			m_camera.pixelOffset.x = minCameraX;

		if( m_camera.pixelOffset.x > maxCameraX )
			m_camera.pixelOffset.x = maxCameraX;
	}

	// vertical
	if( mapHeight <= screenHeight )
	{
		// map height is smaller than screen height, so centre map in screen
		m_camera.pixelOffset.y = ((int)mapHeight - (int)screenHeight) / 2;
	}
	else
	{
		// Map height is larger than screen height.
		// Centre man vertically
		const int manPosY = (m_man.position.tileY * tileHeight) + m_man.position.pixelOffsetY;
		m_camera.pixelOffset.y = manPosY - (screenHeight / 2);

		// Constrain camera to map bounds
		const int minCameraY = cameraBounds.y;
		const int maxCameraY = cameraBounds.y + cameraBounds.h - (int)screenHeight;

		if( m_camera.pixelOffset.y < minCameraY )
			m_camera.pixelOffset.y = minCameraY;

		if( m_camera.pixelOffset.y > maxCameraY )
			m_camera.pixelOffset.y = maxCameraY;
	}
}

void Game::Draw( Renderer& renderer )
{
	renderer.Clear( 0x000000ff );

	switch( m_state )
	{
		case kStateTitleScreen:
		{
			char text[128];
			int width, height;

			SDL_snprintf( text, sizeof(text), "SOKOBAN" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			int y = (renderer.GetLogicalHeight() - height) / 3;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );

			SDL_snprintf( text, sizeof(text), "PRESS O TO START ORIGINAL LEVELS" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			y += 4 * m_pFont->size;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );

			SDL_snprintf( text, sizeof(text), "PRESS M TO START MICROBAN LEVELS" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			y += 4 * m_pFont->size;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );

#ifdef CONFIG_BTINPUT
			// PiZZa: BT pad legend (mapping in sokoban_btinput.c)
			SDL_snprintf( text, sizeof(text), "PAD: X ORIGINAL   Y MICROBAN" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			y += 6 * m_pFont->size;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xff8888ff );

			SDL_snprintf( text, sizeof(text), "B UNDO   L RESTART   A CONTINUE" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			y += 3 * m_pFont->size;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xff8888ff );
#endif
			break;
		}
		case kStatePlaying:
			DrawScene( renderer );
			DrawHUD( renderer );
			break;
		case kStateLevelComplete:
		{
			DrawScene( renderer );

			char text[128];
			SDL_snprintf( text, sizeof(text), "LEVEL COMPLETE" );
			int width, height;
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			int y = (renderer.GetLogicalHeight() - height) / 2;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );

			SDL_snprintf( text, sizeof(text), "PRESS SPACE TO CONTINUE" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			y += 4 * m_pFont->size;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );

			break;
		}
		case kStateGameComplete:
		{
			DrawScene( renderer );

			char text[128];
			SDL_snprintf( text, sizeof(text), "GAME COMPLETE" );
			int width, height;
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			int y = (renderer.GetLogicalHeight() - height) / 2;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );

			SDL_snprintf( text, sizeof(text), "PRESS SPACE TO RETURN TO TITLE SCREEN" );
			renderer.GetTextDimensions( *m_pFont, text, width, height );
			y += 4 * m_pFont->size;
			renderer.DrawText( *m_pFont, text, (renderer.GetLogicalWidth() - width) / 2, y, 0xffffffff );
			break;
		}
	}

#ifdef _DEBUG
	float fps = 1.0f / m_deltaTimeSeconds;
	char text[128];
	SDL_snprintf( text, sizeof(text), "FPS: %.1f", fps );
	renderer.DrawText( *m_pFont, text, 0, 0, 0xffffffff );
#endif
}

void Game::DrawScene( Renderer& renderer )
{
	UpdateCamera( renderer );

	renderer.Clear( m_tileMap.GetBackgroundRGBA() );
	renderer.DrawTexture( *m_pMapTexture, -m_camera.pixelOffset.x, -m_camera.pixelOffset.y );

	for( unsigned int i = 0; i < m_numGoals; ++i )
	{
		DrawObject( renderer, m_goals[i] );
	}

	for( unsigned int i = 0; i < m_numBoxes; ++i )
	{
		const Object& box = m_boxes[i];
		bool bBoxOnGoal = false;
		for( unsigned int j = 0; j < m_numGoals; ++j )
		{
			//if( (box.position.tileX == m_goals[j].position.tileX) && (box.position.tileY == m_goals[j].position.tileY) )
			if( box.position == m_goals[j].position )
			{
				bBoxOnGoal = true;
				break;
			}
		}

		if( bBoxOnGoal )
		{
			DrawTile( renderer, kGid_BoxOnGoal, box.position );
		}
		else
			DrawObject( renderer, box );
	}

	DrawObject( renderer, m_man );
}

void Game::DrawTile( Renderer& renderer, int gid, const Position& pos )
{
	int worldX = pos.tileX * m_tileMap.GetMap()->tile_width + pos.pixelOffsetX;
	int worldY = pos.tileY * m_tileMap.GetMap()->tile_height + pos.pixelOffsetY;
	int screenX = worldX - m_camera.pixelOffset.x;
	int screenY = worldY - m_camera.pixelOffset.y;
	m_mapRender.DrawTileAtPixelCoord( renderer, m_tileMap, gid, screenX, screenY );
}

void Game::DrawObject( Renderer& renderer, const Object& object )
{
	DrawTile( renderer, object.tileGid, object.position );
}

void Game::DrawHUD( Renderer& renderer )
{
	// HUD
	char text[128];

#ifdef CONFIG_BTINPUT
	// PiZZa: name the pad buttons too (mapping in sokoban_btinput.c)
	SDL_snprintf( text, sizeof(text), "R / pad L: restart" );
#else
	SDL_snprintf( text, sizeof(text), "Press R to restart level" );
#endif
	int width, height;
	renderer.GetTextDimensions( *m_pFont, text, width, height );
	renderer.DrawText( *m_pFont, text, (1 * renderer.GetLogicalWidth() / 4 ) - (width / 2), renderer.GetLogicalHeight() - m_pFont->size - 1, 0xffffffff );

#ifdef CONFIG_BTINPUT
	SDL_snprintf( text, sizeof(text), "U / pad B: undo" );
#else
	SDL_snprintf( text, sizeof(text), "Press U to undo move" );
#endif
	renderer.GetTextDimensions( *m_pFont, text, width, height );
	renderer.DrawText( *m_pFont, text, (3 * renderer.GetLogicalWidth() / 4) - (width / 2), renderer.GetLogicalHeight() - m_pFont->size - 1, 0xffffffff );

}
