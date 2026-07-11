#ifndef GAME_H
#define GAME_H

#include "MapRender.h"
#include "TileMap.h"

#include <tmx.h>

class Renderer;
struct Sample;
struct Font;

struct GameInput
{
	bool bSpacePressed;
	bool bStartOriginalLevels;
	bool bStartMicrobanLevels;
	bool bLeft;
	bool bRight;
	bool bUp;
	bool bDown;
	bool bPause;
	bool bRestartLevel;
	bool bUndo;
	bool bDebugSkipLevel;
};

struct Position
{
	int tileX;
	int tileY;
	int pixelOffsetX;
	int pixelOffsetY;
};

static bool operator==( const Position& a, const Position& b )
{
	return (a.tileX == b.tileX) && (a.tileY == b.tileY) && (a.pixelOffsetX == b.pixelOffsetX) && (a.pixelOffsetY == b.pixelOffsetY);
}

enum Direction
{
	kDirection_Left = 0,
	kDirection_Right,
	kDirection_Up,
	kDirection_Down
};

struct Object
{
	Position		position;
	unsigned int	tileGid;
	bool			bMoving;
	Direction		moveDirection;

};

struct Move
{
	Direction direction;
	bool bPushedBox;
};

struct int2
{
	int x, y;
};

struct Camera
{
	int2 pixelOffset;		// from tilemap origin
};


class Game
{
public:

	Game();
	~Game();

	bool			Init( Renderer& renderer );
	void			Shutdown( Renderer& renderer );
	void			Update( const GameInput& gameInput, float deltaTimeSeconds, Renderer& renderer );
	void			Draw( Renderer& renderer );

private:

	bool			InitLevel( unsigned int levelNumber, Renderer& renderer );
	void			InitObjects();
	void			AddBox( int tileX, int tileY );
	void			AddGoal( int tileX, int tileY );

	void			UpdatePlaying( const GameInput& gameInput, float deltaTimeSeconds, Renderer& renderer );
	void			UpdateStatic( const GameInput &gameInput );
	void			UpdateMovingObject( Object& object ) const;
	void			UndoLastMove();
	bool			IsLevelComplete();

	void			UpdateCamera( Renderer& renderer );
	void			DrawScene( Renderer& renderer );
	void			DrawHUD( Renderer& renderer );
	void			DrawTile( Renderer& renderer, int gid, const Position& pos );
	void			DrawObject( Renderer& renderer, const Object& object );

	unsigned int	GetTileGid( int tileX, int tileY );
	bool			IsTileEmpty( int tileX, int tileY );

	enum State
	{
		kStateTitleScreen,
		kStatePlaying,
		kStateLevelComplete,
		kStateGameComplete
	};

	double			m_time;
	float			m_deltaTimeSeconds;
	State			m_state = kStateTitleScreen;
	float			m_timeInState = 0.0f;
	unsigned int	m_currentLevel = 1;

	Font*			m_pFont = nullptr;

	TileMap			m_tileMap;
	SDL_Texture*	m_pMapTexture = nullptr;
	MapRender		m_mapRender;

	Object			m_man;

	static const unsigned int kMaxBoxes = 64;
	Object			m_boxes[kMaxBoxes];
	unsigned int	m_numBoxes = 0;

	static const unsigned int kMaxGoals = kMaxBoxes;
	Object			m_goals[kMaxGoals];
	unsigned int	m_numGoals = 0;

	// stack of moves for Undo functionality
	// PiZZa: was 1024 * 1024 (an 8 MB Game member); 16K undo depth is plenty
	static const unsigned int kStackSize = 16 * 1024;
	Move			m_moveStack[kStackSize];
	unsigned int	m_stackPointer = 0;

	Camera			m_camera;

	char			m_mapFolder[260];
};

#endif // GAME_H
