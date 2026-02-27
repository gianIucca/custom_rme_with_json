//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "iomap_json.h"
#include "editor.h"
#include "tile.h"
#include "item.h"
#include "gui.h"
#include "graphics.h"

#include <nlohmann/json.hpp>
#include <wx/image.h>
#include <wx/wfstream.h>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

IOMapJSON::IOMapJSON(Editor* editor) :
	m_editor(editor)
{
}

IOMapJSON::~IOMapJSON()
{
}

bool IOMapJSON::exportSelection(const std::string& directory, const std::string& name)
{
	if (!m_editor->hasSelection()) {
		m_error = "No selection to export";
		return false;
	}

	const auto& selection = m_editor->getSelection();
	const auto& tiles = selection.getTiles();

	if (tiles.empty()) {
		m_error = "Selection is empty";
		return false;
	}

	// Calculate bounds of selection
	int min_x = INT_MAX;
	int min_y = INT_MAX;
	int max_x = INT_MIN;
	int max_y = INT_MIN;
	int floor = -1;

	for (auto tile : tiles) {
		if (!tile) continue;

		const auto& pos = tile->getPosition();
		if (floor == -1) {
			floor = pos.z;
		} else if (floor != pos.z) {
			m_error = "Selection spans multiple floors. Please select tiles from a single floor only.";
			return false;
		}

		if (pos.x < min_x) min_x = pos.x;
		if (pos.x > max_x) max_x = pos.x;
		if (pos.y < min_y) min_y = pos.y;
		if (pos.y > max_y) max_y = pos.y;
	}

	int width = max_x - min_x + 1;
	int height = max_y - min_y + 1;

	if (width <= 0 || height <= 0) {
		m_error = "Invalid selection bounds";
		return false;
	}

	// Collect all unique sprites used and assign them tile IDs
	std::unordered_map<uint16_t, int> spriteToTileID;
	std::vector<uint16_t> usedSprites;
	int nextTileID = 1; // Tiled uses 1-based indexing, 0 = empty

	// First pass: collect all unique client IDs
	for (auto tile : tiles) {
		if (!tile || !tile->hasItems()) continue;

		// Get ground item
		if (tile->ground) {
			uint16_t clientID = tile->ground->getClientID();
			if (spriteToTileID.find(clientID) == spriteToTileID.end()) {
				spriteToTileID[clientID] = nextTileID++;
				usedSprites.push_back(clientID);
			}
		}

		// Get other items (we'll use the topmost non-ground item for simplicity)
		if (!tile->items.empty()) {
			for (auto item : tile->items) {
				if (item && !item->isBorder()) { // Skip border items
					uint16_t clientID = item->getClientID();
					if (spriteToTileID.find(clientID) == spriteToTileID.end()) {
						spriteToTileID[clientID] = nextTileID++;
						usedSprites.push_back(clientID);
					}
					break; // Only use first non-border item
				}
			}
		}
	}

	if (usedSprites.empty()) {
		m_error = "No sprites found in selection";
		return false;
	}

	// Generate spritesheet
	if (!generateSpritesheet(directory, name, spriteToTileID)) {
		return false;
	}

	// Build tile data array (row-major order)
	std::vector<int> tileData(width * height, 0);

	for (auto tile : tiles) {
		if (!tile || !tile->hasItems()) continue;

		const auto& pos = tile->getPosition();
		int x = pos.x - min_x;
		int y = pos.y - min_y;
		int index = y * width + x;

		// Get the appropriate sprite for this tile
		uint16_t clientID = 0;
		if (tile->ground) {
			clientID = tile->ground->getClientID();
		} else if (!tile->items.empty()) {
			for (auto item : tile->items) {
				if (item && !item->isBorder()) {
					clientID = item->getClientID();
					break;
				}
			}
		}

		if (clientID > 0 && spriteToTileID.find(clientID) != spriteToTileID.end()) {
			tileData[index] = spriteToTileID[clientID];
		}
	}

	// Build collision layer
	std::vector<int> collisionData(width * height, 0);
	for (auto tile : tiles) {
		if (!tile) continue;

		const auto& pos = tile->getPosition();
		int x = pos.x - min_x;
		int y = pos.y - min_y;
		int index = y * width + x;

		// 1 = blocked/not walkable, 0 = walkable
		collisionData[index] = tile->isBlocking() ? 1 : 0;
	}

	// Create JSON structure using nlohmann_json
	json root;
	root["compressionlevel"] = -1;
	root["height"] = height;
	root["infinite"] = false;

	// Layers array
	json layers = json::array();

	// Ground/tile layer
	json tileLayer;
	tileLayer["id"] = 1;
	tileLayer["name"] = "tiles";
	tileLayer["type"] = "tilelayer";
	tileLayer["visible"] = true;
	tileLayer["opacity"] = 1.0;
	tileLayer["x"] = 0;
	tileLayer["y"] = 0;
	tileLayer["width"] = width;
	tileLayer["height"] = height;
	tileLayer["data"] = tileData;

	layers.push_back(tileLayer);

	// Collision layer
	json collisionLayer;
	collisionLayer["id"] = 2;
	collisionLayer["name"] = "collision";
	collisionLayer["type"] = "tilelayer";
	collisionLayer["visible"] = true;
	collisionLayer["opacity"] = 0.5;
	collisionLayer["x"] = 0;
	collisionLayer["y"] = 0;
	collisionLayer["width"] = width;
	collisionLayer["height"] = height;
	collisionLayer["data"] = collisionData;

	layers.push_back(collisionLayer);

	root["layers"] = layers;

	// Tileset
	json tilesets = json::array();
	json tileset;
	tileset["columns"] = 10;
	tileset["firstgid"] = 1;
	tileset["image"] = name + "_spritesheet.png";
	
	int rows = (usedSprites.size() + 9) / 10;
	tileset["imageheight"] = rows * 32;
	tileset["imagewidth"] = 10 * 32;
	tileset["margin"] = 0;
	tileset["name"] = name;
	tileset["spacing"] = 0;
	tileset["tilecount"] = (int)usedSprites.size();
	tileset["tileheight"] = 32;
	tileset["tilewidth"] = 32;

	// Add tile properties for collision
	json tilesArray = json::array();
	for (size_t i = 0; i < usedSprites.size(); ++i) {
		json tileObj;
		tileObj["id"] = (int)i;
		
		json properties = json::array();
		json collisionProp;
		collisionProp["name"] = "collision";
		collisionProp["type"] = "bool";
		collisionProp["value"] = false;
		properties.push_back(collisionProp);
		
		tileObj["properties"] = properties;
		tilesArray.push_back(tileObj);
	}
	tileset["tiles"] = tilesArray;

	tilesets.push_back(tileset);
	root["tilesets"] = tilesets;

	// Map properties
	root["nextlayerid"] = 3;
	root["nextobjectid"] = 1;
	root["orientation"] = "orthogonal";
	root["renderorder"] = "right-down";
	root["tiledversion"] = "1.10";
	root["tileheight"] = 32;
	root["tilewidth"] = 32;
	root["type"] = "map";
	root["version"] = "1.10";
	root["width"] = width;

	// Write JSON to file
	std::string jsonPath = directory + "/" + name + ".json";
	std::ofstream file(jsonPath);
	if (!file.is_open()) {
		m_error = "Failed to create JSON file: " + jsonPath;
		return false;
	}

	file << root.dump(2); // Pretty print with 2-space indent
	file.close();

	return true;
}

bool IOMapJSON::generateSpritesheet(const std::string& directory, const std::string& name,
	const std::unordered_map<uint16_t, int>& spriteMapping)
{
	const int TILE_SIZE = 32;
	const int COLUMNS = 10;
	int numSprites = (int)spriteMapping.size();
	int rows = (numSprites + COLUMNS - 1) / COLUMNS; // Round up

	int imageWidth = COLUMNS * TILE_SIZE;
	int imageHeight = rows * TILE_SIZE;

	// Create image buffer
	wxImage image(imageWidth, imageHeight);
	image.InitAlpha();
	
	unsigned char* rgb = image.GetData();
	unsigned char* alpha = image.GetAlpha();
	
	// Clear to transparent
	memset(rgb, 0, imageWidth * imageHeight * 3);
	memset(alpha, 0, imageWidth * imageHeight);

	// Sort sprites by their tile ID to maintain consistent ordering
	std::vector<std::pair<uint16_t, int>> sortedSprites(spriteMapping.begin(), spriteMapping.end());
	std::sort(sortedSprites.begin(), sortedSprites.end(),
		[](const auto& a, const auto& b) { return a.second < b.second; });

	// Draw each sprite
	for (const auto& pair : sortedSprites) {
		uint16_t clientID = pair.first;
		int tileID = pair.second - 1; // Convert to 0-based for positioning

		int col = tileID % COLUMNS;
		int row = tileID / COLUMNS;

		// Get sprite from graphics manager
		Sprite* sprite = g_gui.gfx.getSprite(clientID);
		if (!sprite) continue;

		// Create a temporary bitmap and DC to draw the sprite
		wxBitmap tempBitmap(TILE_SIZE, TILE_SIZE, 32);
		wxMemoryDC tempDC;
		tempDC.SelectObject(tempBitmap);
		
		// Clear with transparent background
		tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
		tempDC.Clear();

		// Draw sprite to temporary DC
		sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32, 0, 0);
		
		tempDC.SelectObject(wxNullBitmap);

		// Convert to image
		wxImage spriteImg = tempBitmap.ConvertToImage();
		if (spriteImg.IsOk()) {
			// Ensure sprite has alpha channel
			if (!spriteImg.HasAlpha()) {
				spriteImg.InitAlpha();
			}

			// Copy sprite data to main image
			int destX = col * TILE_SIZE;
			int destY = row * TILE_SIZE;

			for (int y = 0; y < TILE_SIZE && y < spriteImg.GetHeight(); ++y) {
				for (int x = 0; x < TILE_SIZE && x < spriteImg.GetWidth(); ++x) {
					int destIdx = ((destY + y) * imageWidth + (destX + x));

					if (destIdx * 3 + 2 < imageWidth * imageHeight * 3) {
						rgb[destIdx * 3] = spriteImg.GetRed(x, y);
						rgb[destIdx * 3 + 1] = spriteImg.GetGreen(x, y);
						rgb[destIdx * 3 + 2] = spriteImg.GetBlue(x, y);
						
						if (spriteImg.HasAlpha()) {
							alpha[destIdx] = spriteImg.GetAlpha(x, y);
						} else {
							// Check for magenta transparency (common in old sprites)
							if (spriteImg.GetRed(x, y) == 255 &&
								spriteImg.GetGreen(x, y) == 0 &&
								spriteImg.GetBlue(x, y) == 255) {
								alpha[destIdx] = 0;
							} else {
								alpha[destIdx] = 255;
							}
						}
					}
				}
			}
		}
	}

	// Save PNG
	std::string pngPath = directory + "/" + name + "_spritesheet.png";
	if (!image.SaveFile(pngPath, wxBITMAP_TYPE_PNG)) {
		m_error = "Failed to save spritesheet PNG: " + pngPath;
		return false;
	}

	return true;
}

