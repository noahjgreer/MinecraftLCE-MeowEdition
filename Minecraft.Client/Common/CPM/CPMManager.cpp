#include "stdafx.h"
#include "CPMManager.h"
#include "CPMModel.h"
#include "CPMAnim.h"
#include "CPMRender.h"
#include "CPMFiles.h"
#include "CPMZip.h"
#include "CPMProject.h"
#include "../../Minecraft.h"
#include "../../Textures.h"
#include "../../BufferedImage.h"
#include "../../ModelPart.h"
#include "../../../Minecraft.World/Player.h"
#include "../../../Minecraft.World/Abilities.h"
#include "../../MultiPlayerLocalPlayer.h"
#include "../../Gui.h"
#include "CPMNet.h"

static const float CPM_RAD_DEG = 180.0f / PI;

//////////////////////////////////////////////////////////////////////////
// CPMPlayerModel
//////////////////////////////////////////////////////////////////////////

CPMPlayerModel::CPMPlayerModel() : def(NULL), anims(NULL)
{
}

CPMPlayerModel::~CPMPlayerModel()
{
	delete def;
	delete anims;
}

//////////////////////////////////////////////////////////////////////////
// Manager state
//////////////////////////////////////////////////////////////////////////

namespace
{
	// playerName -> model. Owns the models.
	std::map<std::wstring, CPMPlayerModel *> g_models;

	std::wstring g_localModelName;

	// Free-running animation clock in milliseconds.
	long long g_timeMillis = 0;

	// Set for the duration of one entity's render.
	CPMPlayerModel *g_active = NULL;

	void freeModel(CPMPlayerModel *m)
	{
		if (m == NULL) return;
		// The GL texture is released with the model. Textures::getTexture hands
		// out ids that stay valid until the texture manager is torn down, and
		// LCE has no public "release one dynamic texture" call, so the id is
		// simply dropped here.
		delete m;
	}

	// D3DX will happily abort the process through app.FatalLoadError() if it is
	// handed something that is not an image, and model data can arrive from
	// another player, so the PNG header is validated first.
	bool looksLikePng(const std::vector<unsigned char> &d, int &w, int &h)
	{
		if (d.size() < 33) return false;
		static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		for (int i = 0; i < 8; i++) if (d[i] != sig[i]) return false;
		// Bytes 12..15 must be the IHDR chunk type, then width and height.
		if (d[12] != 'I' || d[13] != 'H' || d[14] != 'D' || d[15] != 'R') return false;
		w = (d[16] << 24) | (d[17] << 16) | (d[18] << 8) | d[19];
		h = (d[20] << 24) | (d[21] << 16) | (d[22] << 8) | d[23];
		if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
		return true;
	}

	void ensureTexture(CPMPlayerModel *m)
	{
		if (m == NULL || m->def == NULL) return;
		if (m->def->textureId != -1) return;
		if (m->def->skinPng.empty()) { m->def->textureId = 0; return; }

		int w = 0, h = 0;
		if (!looksLikePng(m->def->skinPng, w, h)) { m->def->textureId = 0; return; }

		Minecraft *mc = Minecraft::GetInstance();
		if (mc == NULL || mc->textures == NULL) return;   // retry next frame

		BufferedImage *img = new BufferedImage(&m->def->skinPng[0],
		                                       (DWORD)m->def->skinPng.size());
		m->def->textureId = mc->textures->getTexture(img);
		// Textures::getTexture uploads and keeps its own copy of the pixels.
		delete img;
	}

	// Maps LCE player state onto the VanillaPose the model's triggers use.
	int detectPose(Player *p, float limbSwingAmount)
	{
		if (p == NULL) return CPM_POSE_STANDING;

		if (!p->isAlive()) return CPM_POSE_DYING;
		if (p->isSleeping()) return CPM_POSE_SLEEPING;
		if (p->riding != NULL) return CPM_POSE_RIDING;
		if (p->abilities.flying) return CPM_POSE_CREATIVE_FLYING;
		if (p->isInWater()) return CPM_POSE_SWIMMING;

		bool moving = limbSwingAmount > 0.05f;

		if (p->isSneaking()) return moving ? CPM_POSE_SNEAK_WALK : CPM_POSE_SNEAKING;
		if (!p->onGround && p->fallDistance > 1.0f) return CPM_POSE_FALLING;
		if (!p->onGround) return CPM_POSE_JUMPING;
		if (p->isSprinting() && moving) return CPM_POSE_RUNNING;
		if (moving) return CPM_POSE_WALKING;
		return CPM_POSE_STANDING;
	}
}

//////////////////////////////////////////////////////////////////////////
// Loading
//////////////////////////////////////////////////////////////////////////

CPMPlayerModel *CPMManager::loadFromBytes(const std::wstring &name,
                                          const unsigned char *data, int len,
                                          std::string &errOut)
{
	errOut.clear();

	// A .cpmproject is a zip and is read by a completely separate front end;
	// only the exported binary format goes through CPMLoadModelFile.
	if (CPMZipLooksLikeZip(data, len))
	{
		CPMPlayerModel *pm = new CPMPlayerModel();
		pm->name = name;
		pm->def = new CPMModelDefinition();

		if (!CPMLoadProject(data, len, pm->def, errOut))
		{
			delete pm;
			return NULL;
		}

		// Project animations live as separate JSON files in the archive and are
		// not ported; the model renders without them.
		pm->anims = new CPMAnimationSet();
		pm->raw.assign(data, data + len);
		return pm;
	}

	std::string mname, mdesc;
	std::vector<unsigned char> dataBlock;
	if (!CPMLoadModelFile(data, len, mname, mdesc, dataBlock, errOut))
	{
		return NULL;
	}

	CPMPlayerModel *m = new CPMPlayerModel();
	m->name = name;
	m->def = new CPMModelDefinition();

	if (!m->def->load(&dataBlock[0], (int)dataBlock.size()))
	{
		errOut = m->def->error.empty() ? "malformed model data" : m->def->error;
		delete m;
		return NULL;
	}

	// The animation part is optional; a model without one still renders.
	m->anims = new CPMAnimationSet();
	{
		// Re-walk the part stream for ANIMATION_NEW. Keeping this separate from
		// CPMModelDefinition keeps the geometry loader free of animation types.
		CPMIn in(&dataBlock[0], (int)dataBlock.size());
		in.read();                     // header byte
		for (int guard = 0; guard < 65536; guard++)
		{
			int typeId = in.readByte();
			if (in.fail()) break;
			std::vector<unsigned char> block;
			if (!in.readNextBlock(block)) break;
			if (typeId == CPM_PART_END) break;
			if (typeId == CPM_PART_ANIMATION_NEW && !block.empty())
			{
				CPMIn b(&block[0], (int)block.size());
				m->anims->load(b);
			}
		}
	}
	m->anims->bind(m->def);

	m->raw.assign(data, data + len);
	return m;
}

bool CPMManager::assignFromBytes(const std::wstring &playerName,
                                 const unsigned char *data, int len,
                                 std::string &errOut)
{
	CPMPlayerModel *m = loadFromBytes(playerName, data, len, errOut);
	if (m == NULL) return false;

	clearFor(playerName);
	g_models[playerName] = m;
	return true;
}

bool CPMManager::assignFromFolder(const std::wstring &playerName,
                                  const std::wstring &modelName,
                                  std::string &errOut)
{
	std::vector<unsigned char> file;
	if (!CPMReadModel(modelName, file))
	{
		errOut = "model file not found";
		return false;
	}
	if (!assignFromBytes(playerName, &file[0], (int)file.size(), errOut)) return false;
	g_models[playerName]->name = modelName;
	return true;
}

void CPMManager::clearFor(const std::wstring &playerName)
{
	std::map<std::wstring, CPMPlayerModel *>::iterator it = g_models.find(playerName);
	if (it == g_models.end()) return;
	if (g_active == it->second) g_active = NULL;
	freeModel(it->second);
	g_models.erase(it);
}

void CPMManager::clearAll()
{
	g_active = NULL;
	std::map<std::wstring, CPMPlayerModel *>::iterator it;
	for (it = g_models.begin(); it != g_models.end(); ++it) freeModel(it->second);
	g_models.clear();
}

CPMPlayerModel *CPMManager::getFor(const std::wstring &playerName)
{
	std::map<std::wstring, CPMPlayerModel *>::iterator it = g_models.find(playerName);
	return it == g_models.end() ? NULL : it->second;
}

void CPMManager::listModels(std::vector<std::wstring> &out)
{
	CPMListModels(out);
}

void CPMManager::collectAll(std::vector<std::wstring> &owners,
                            std::vector<const std::vector<unsigned char> *> &datas)
{
	owners.clear();
	datas.clear();
	std::map<std::wstring, CPMPlayerModel *>::iterator it;
	for (it = g_models.begin(); it != g_models.end(); ++it)
	{
		if (it->second == NULL || it->second->raw.empty()) continue;
		owners.push_back(it->first);
		datas.push_back(&it->second->raw);
	}
}

CPMPlayerModel *CPMManager::getLocalModel()
{
	Minecraft *mc = Minecraft::GetInstance();
	if (mc == NULL || mc->player == NULL) return NULL;
	return getFor(mc->player->name);
}

void CPMManager::setLocalModelName(const std::wstring &modelName)
{
	g_localModelName = modelName;
}

const std::wstring &CPMManager::getLocalModelName()
{
	return g_localModelName;
}

void CPMManager::tick()
{
	// One client tick is 50ms, matching AnimationEngine.getTime.
	g_timeMillis += 50;
}

//////////////////////////////////////////////////////////////////////////
// Rendering
//////////////////////////////////////////////////////////////////////////

bool CPMManager::isActive()
{
	return g_active != NULL && g_active->def != NULL;
}

bool CPMManager::beginEntity(Entity *entity, float limbSwingAmount)
{
	g_active = NULL;
	if (g_models.empty()) return false;

	Player *p = dynamic_cast<Player *>(entity);
	if (p == NULL) return false;

	CPMPlayerModel *m = getFor(p->name);
	if (m == NULL || m->def == NULL) return false;

	g_active = m;
	ensureTexture(m);

	// Animations mutate the shared cube tree, so they are re-applied from the
	// static pose for every entity drawn.
	m->def->resetAnimationPos();
	if (m->anims != NULL && !m->anims->empty())
		m->anims->applyForPose(detectPose(p, limbSwingAmount), g_timeMillis);

	return true;
}

void CPMManager::endEntity()
{
	g_active = NULL;
}

bool CPMManager::bindActiveTexture()
{
	if (!isActive()) return false;
	if (g_active->def->textureId <= 0) return false;

	Minecraft *mc = Minecraft::GetInstance();
	if (mc == NULL || mc->textures == NULL) return false;

	mc->textures->bind(g_active->def->textureId);
	return true;
}

bool CPMManager::renderPart(int playerPart, ModelPart *vanilla, float scale)
{
	if (!isActive()) return false;
	if (playerPart < 0 || playerPart >= CPM_PP_COUNT) return false;

	CPMRenderedCube *root = g_active->def->roots[playerPart];
	if (root == NULL) return false;

	bool suppressVanilla = root->rootHidden || root->disableVanilla;

	if (!root->children.empty() && vanilla != NULL)
	{
		glPushMatrix();

		// RootModelElement::setPosAndRot / getPos / getRot: the root's own
		// offset and the animated delta are ADDED to the vanilla bone's pivot
		// and rotation, and the result is applied as a single translate and a
		// single rotate.
		//
		// Doing it as two transforms in sequence - bone, then root - would
		// rotate about the bone's pivot and only then shift, which puts the
		// pivot in the wrong place for any model that moves a root. That is
		// what made a head with a custom root offset swing about its centre
		// instead of its base.
		const float px = vanilla->x + root->cube.pos.x + root->pos.x;
		const float py = vanilla->y + root->cube.pos.y + root->pos.y;
		const float pz = vanilla->z + root->cube.pos.z + root->pos.z;

		const float rx = vanilla->xRot + root->cube.rotation.x + root->rotation.x;
		const float ry = vanilla->yRot + root->cube.rotation.y + root->rotation.y;
		const float rz = vanilla->zRot + root->cube.rotation.z + root->rotation.z;

		glTranslatef(px * scale, py * scale, pz * scale);
		if (rz != 0) glRotatef(rz * CPM_RAD_DEG, 0, 0, 1);
		if (ry != 0) glRotatef(ry * CPM_RAD_DEG, 0, 1, 0);
		if (rx != 0) glRotatef(rx * CPM_RAD_DEG, 1, 0, 0);

		CPMRenderCubeTree(root, scale,
		                  g_active->def->skinUvWidth, g_active->def->skinUvHeight);

		glPopMatrix();
	}

	return suppressVanilla;
}

void CPMManager::renderCustomRoot(float scale)
{
	if (!isActive()) return;

	CPMRenderedCube *root = g_active->def->roots[CPM_PP_CUSTOM_PART];
	if (root == NULL || root->children.empty()) return;

	// CUSTOM_PART hangs off the model root rather than a bone, so there is no
	// bone transform to add - but the static and animated parts still combine
	// into one translate and one rotate, as above.
	const float px = root->cube.pos.x + root->pos.x;
	const float py = root->cube.pos.y + root->pos.y;
	const float pz = root->cube.pos.z + root->pos.z;
	const float rx = root->cube.rotation.x + root->rotation.x;
	const float ry = root->cube.rotation.y + root->rotation.y;
	const float rz = root->cube.rotation.z + root->rotation.z;

	glPushMatrix();
	glTranslatef(px * scale, py * scale, pz * scale);
	if (rz != 0) glRotatef(rz * CPM_RAD_DEG, 0, 0, 1);
	if (ry != 0) glRotatef(ry * CPM_RAD_DEG, 0, 1, 0);
	if (rx != 0) glRotatef(rx * CPM_RAD_DEG, 1, 0, 0);

	CPMRenderCubeTree(root, scale,
	                  g_active->def->skinUvWidth, g_active->def->skinUvHeight);

	glPopMatrix();
}

//////////////////////////////////////////////////////////////////////////
// Chat command
//////////////////////////////////////////////////////////////////////////

namespace
{
	void cpmChatMsg(int pad, const std::wstring &text)
	{
		Minecraft *mc = Minecraft::GetInstance();
		if (mc != NULL && mc->gui != NULL) mc->gui->addMessage(text, pad);
	}

	std::wstring cpmWiden(const std::string &s)
	{
		std::wstring w;
		for (size_t i = 0; i < s.size(); i++) w += (wchar_t)(unsigned char)s[i];
		return w;
	}
}

bool CPMManager::handleChatCommand(const std::wstring &message, int pad)
{
	if (message.compare(0, 4, L"/cpm") != 0) return false;
	if (message.size() > 4 && message[4] != L' ') return false;

	std::wstring args = message.size() > 5 ? message.substr(5) : L"";
	while (!args.empty() && args[0] == L' ') args.erase(0, 1);
	while (!args.empty() && args[args.size() - 1] == L' ') args.erase(args.size() - 1);

	Minecraft *mc = Minecraft::GetInstance();
	std::wstring self;
	if (mc != NULL && mc->localplayers[pad] != NULL) self = mc->localplayers[pad]->name;

	if (args.empty() || args == L"help")
	{
		cpmChatMsg(pad, L"/cpm list - show available models");
		cpmChatMsg(pad, L"/cpm set <name> - wear a model");
		cpmChatMsg(pad, L"/cpm clear - go back to your skin");
		return true;
	}

	if (args == L"list")
	{
		std::vector<std::wstring> models;
		listModels(models);
		if (models.empty())
		{
			cpmChatMsg(pad, L"No models in the " + CPMGetModelFolder() + L" folder.");
			return true;
		}
		cpmChatMsg(pad, L"Models:");
		for (size_t i = 0; i < models.size(); i++)
		{
			// Check the magic byte while listing, so a project file or a skin
			// PNG dropped in the folder is obvious before it is selected.
			std::wstring line = L"  " + models[i];
			std::vector<unsigned char> head;
			if (!CPMReadModel(models[i], head) || head.empty())
			{
				line += L"  (unreadable)";
			}
			else if (head[0] != CPM_FILE_HEADER && !CPMZipLooksLikeZip(&head[0], (int)head.size()))
			{
				line += L"  (not a model: " + cpmWiden(CPMIdentifyFile(&head[0], (int)head.size())) + L")";
			}
			cpmChatMsg(pad, line);
		}
		return true;
	}

	if (args == L"clear")
	{
		if (self.empty()) return true;
		clearFor(self);
		setLocalModelName(L"");
		CPMNet::sendClear(pad);
		cpmChatMsg(pad, L"Custom model cleared.");
		return true;
	}

	if (args.compare(0, 4, L"set ") == 0)
	{
		std::wstring modelName = args.substr(4);
		while (!modelName.empty() && modelName[0] == L' ') modelName.erase(0, 1);

		if (self.empty() || modelName.empty())
		{
			cpmChatMsg(pad, L"Usage: /cpm set <name>");
			return true;
		}

		std::string err;
		if (!assignFromFolder(self, modelName, err))
		{
			cpmChatMsg(pad, L"Could not load '" + modelName + L"': " + cpmWiden(err));
			return true;
		}

		setLocalModelName(modelName);
		CPMNet::sendLocalModel(pad);
		cpmChatMsg(pad, L"Now wearing '" + modelName + L"'.");
		return true;
	}

	cpmChatMsg(pad, L"Unknown /cpm command. Try /cpm help");
	return true;
}
