#pragma once
// CPM - Custom Player Models
//
// Owns every loaded model, maps players to them, and drives the render hook in
// HumanoidModel. This is the layer the rest of the game talks to.
//
// Players are keyed by name because LCE has no exposed per-player UUID the way
// Java Edition does; names are unique within a session, which is all the
// lifetime a model assignment needs.

#include <string>
#include <vector>
#include <map>

class CPMModelDefinition;
class CPMAnimationSet;
class ModelPart;
class Entity;
class Player;

class CPMPlayerModel
{
public:
	CPMModelDefinition *def;
	CPMAnimationSet *anims;

	// The original .cpmmodel bytes, kept so the model can be forwarded to other
	// players without re-serialising it.
	std::vector<unsigned char> raw;

	std::wstring name;

	CPMPlayerModel();
	~CPMPlayerModel();
};

class CPMManager
{
public:
	// Parses a .cpmmodel file image. Returns NULL on malformed data; `errOut`
	// receives a short reason. The caller owns nothing - models are cached here.
	static CPMPlayerModel *loadFromBytes(const std::wstring &name,
	                                     const unsigned char *data, int len,
	                                     std::string &errOut);

	// Loads `modelName` from the model folder and assigns it to `playerName`.
	static bool assignFromFolder(const std::wstring &playerName,
	                             const std::wstring &modelName,
	                             std::string &errOut);

	// Assigns a model that arrived over the network.
	static bool assignFromBytes(const std::wstring &playerName,
	                            const unsigned char *data, int len,
	                            std::string &errOut);

	static void clearFor(const std::wstring &playerName);
	static void clearAll();

	static CPMPlayerModel *getFor(const std::wstring &playerName);
	static void listModels(std::vector<std::wstring> &out);

	// Every assigned model, for bringing a joining player up to date. The
	// returned byte vectors are owned by the manager.
	static void collectAll(std::vector<std::wstring> &owners,
	                       std::vector<const std::vector<unsigned char> *> &datas);

	// The local player's current model, for broadcasting on join.
	static CPMPlayerModel *getLocalModel();
	static void setLocalModelName(const std::wstring &modelName);
	static const std::wstring &getLocalModelName();

	// Advances the animation clock. Call once per client tick.
	static void tick();

	// Handles the client-side /cpm command. Returns true if `message` was a CPM
	// command and should not be sent on to the server as chat.
	static bool handleChatCommand(const std::wstring &message, int pad);

	//////////////////////////////////////////////////////////////////////
	// Render hooks, called from HumanoidModel / PlayerRenderer
	//////////////////////////////////////////////////////////////////////

	// Selects the model for the entity about to be drawn and runs its
	// animations for the current pose. `limbSwingAmount` distinguishes standing
	// from walking. Returns true if a model is active.
	static bool beginEntity(Entity *entity, float limbSwingAmount);
	static void endEntity();

	// True while a model is active for the entity being drawn.
	static bool isActive();

	// Binds the active model's embedded skin, if it has one. Returns true if a
	// texture was bound, in which case the caller should not bind the vanilla
	// skin over it.
	static bool bindActiveTexture();

	// Draws the CPM subtree attached to one vanilla bone, positioned by that
	// bone's transform. `playerPart` is a CPMPlayerPart.
	//
	// Returns true if the vanilla part should be suppressed, in which case the
	// caller must not draw it.
	static bool renderPart(int playerPart, ModelPart *vanilla, float scale);

	// Draws cubes parented to CUSTOM_PART, which hang off the model root rather
	// than any vanilla bone.
	static void renderCustomRoot(float scale);
};
