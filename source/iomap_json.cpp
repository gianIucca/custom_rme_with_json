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
	// For multi-tile sprites (64x64, etc.), we need separate tile IDs for each piece
	// Key: clientID, Value: base tile ID (for single-tile) or first tile ID (for multi-tile)
	std::unordered_map<uint16_t, int> spriteToTileID;
	std::vector<SpriteInfo> usedSprites;
	int nextTileID = 1; // Tiled uses 1-based indexing, 0 = empty

	wxLogMessage("=== FIRST PASS: Assigning base tile IDs ===");

	// First pass: collect all unique client IDs and check for multi-tile sprites
	for (auto tile : tiles) {
		if (!tile || !tile->hasItems()) continue;

		// Get ground item
		if (tile->ground) {
			uint16_t clientID = tile->ground->getClientID();
			// Fallback to item ID if clientID is 0
			if (clientID == 0) {
				clientID = tile->ground->getID();
			}
			if (clientID > 0 && spriteToTileID.find(clientID) == spriteToTileID.end()) {
				// Get sprite to check its actual dimensions
				Sprite* sprite = g_gui.gfx.getSprite(clientID);
				GameSprite* gameSprite = dynamic_cast<GameSprite*>(sprite);
				
				uint8_t w = 1;
				uint8_t h = 1;
				
				if (gameSprite) {
					w = gameSprite->width > 0 ? gameSprite->width : 1;
					h = gameSprite->height > 0 ? gameSprite->height : 1;
					wxLogMessage("Item sprite - clientID: %d, width: %d, height: %d", clientID, w, h);
				} else {
					wxLogMessage("Item sprite - clientID: %d (not a GameSprite)", clientID);
				}
				
				wxLogMessage("  ASSIGNING: clientID=%d gets baseTileID=%d (before incrementing nextTileID)", clientID, nextTileID);
				spriteToTileID[clientID] = nextTileID;
				usedSprites.push_back({clientID, w, h});  // Store actual dimensions!
				// Reserve tile IDs for multi-tile sprites (they take up w*h grid cells in spritesheet)
				nextTileID += (w * h);
				wxLogMessage("  After reserving %d tiles, nextTileID is now %d", w * h, nextTileID);
			}
		}

	// Get other items (collect ALL items including borders/walls)
	if (!tile->items.empty()) {
		for (auto item : tile->items) {
			if (item) {
				uint16_t clientID = item->getClientID();
				// Fallback to item ID if clientID is 0
				if (clientID == 0) {
					clientID = item->getID();
				}
			if (clientID > 0 && spriteToTileID.find(clientID) == spriteToTileID.end()) {
				// Get sprite to check its actual dimensions
				Sprite* sprite = g_gui.gfx.getSprite(clientID);
				GameSprite* gameSprite = dynamic_cast<GameSprite*>(sprite);
				
				uint8_t w = 1;
				uint8_t h = 1;
				
				if (gameSprite) {
					w = gameSprite->width > 0 ? gameSprite->width : 1;
					h = gameSprite->height > 0 ? gameSprite->height : 1;
					
					// Check if spriteList[0] has actual pixel data larger than 32x32
					int actualPixelWidth = 32;
					int actualPixelHeight = 32;
					if (!gameSprite->spriteList.empty() && gameSprite->spriteList[0]) {
						uint8_t* rawData = gameSprite->spriteList[0]->getRGBAData();
						if (rawData) {
							// Check the actual allocated size - sprites are always 32x32 in spriteList
							actualPixelWidth = 32;
							actualPixelHeight = 32;
						}
					}
					
					wxLogMessage("Ground sprite - clientID: %d, width: %d, height: %d, spriteList.size: %d, numsprites: %d, pattern_x: %d, pattern_y: %d, actualPixels: %dx%d", 
						clientID, w, h, gameSprite->spriteList.size(), gameSprite->numsprites, 
						gameSprite->pattern_x, gameSprite->pattern_y, actualPixelWidth, actualPixelHeight);
				} else {
					wxLogMessage("Ground sprite - clientID: %d (not a GameSprite)", clientID);
				}
				
				wxLogMessage("  ASSIGNING: clientID=%d gets baseTileID=%d (before incrementing nextTileID)", clientID, nextTileID);
				spriteToTileID[clientID] = nextTileID;
				usedSprites.push_back({clientID, w, h});  // Store actual dimensions!
				// Reserve tile IDs for multi-tile sprites (they take up w*h grid cells in spritesheet)
				nextTileID += (w * h);
				wxLogMessage("  After reserving %d tiles, nextTileID is now %d", w * h, nextTileID);
			}
				// Don't break - collect ALL items on this tile
			}
		}
	}
	}

	if (usedSprites.empty()) {
		m_error = "No sprites found in selection";
		return false;
	}

	// Debug: Show the spriteToTileID mapping
	wxLogMessage("=== SPRITE TO TILEID MAPPING ===");
	for (const auto& pair : spriteToTileID) {
		wxLogMessage("  clientID=%d -> baseTileID=%d", pair.first, pair.second);
	}
	wxLogMessage("=== END MAPPING ===");

	// Debug: Show the order of sprites in usedSprites vector
	wxLogMessage("=== USED SPRITES ORDER (this is the order in spritesheet) ===");
	for (size_t i = 0; i < usedSprites.size(); ++i) {
		wxLogMessage("  [%d] clientID=%d, size=%dx%d", i, usedSprites[i].clientID, usedSprites[i].width, usedSprites[i].height);
	}
	wxLogMessage("=== END ORDER ===");

	// Generate spritesheet
	if (!generateSpritesheet(directory, name, usedSprites)) {
		return false;
	}

	// Find the maximum number of items on any tile
	int maxItemsPerTile = 0;
	for (auto tile : tiles) {
		if (!tile) continue;
		int itemCount = tile->items.size();
		if (itemCount > maxItemsPerTile) {
			maxItemsPerTile = itemCount;
		}
	}

	// Create ground layer data
	std::vector<int> groundData(width * height, 0);

	// Create multiple object layers (one for each stacking position)
	std::vector<std::vector<int>> objectLayers(maxItemsPerTile);
	for (int i = 0; i < maxItemsPerTile; ++i) {
		objectLayers[i].resize(width * height, 0);
	}

	// Build layer data
	for (auto tile : tiles) {
		if (!tile) continue;

		const auto& pos = tile->getPosition();
		int x = pos.x - min_x;
		int y = pos.y - min_y;
		int index = y * width + x;

		// Ground layer
		if (tile->ground) {
			uint16_t clientID = tile->ground->getClientID();
			// Fallback to item ID if clientID is 0
			if (clientID == 0) {
				clientID = tile->ground->getID();
			}
			if (clientID > 0 && spriteToTileID.find(clientID) != spriteToTileID.end()) {
				int baseTileID = spriteToTileID[clientID];
				
				// Check if this is a multi-tile sprite
				Sprite* sprite = g_gui.gfx.getSprite(clientID);
				GameSprite* gameSprite = dynamic_cast<GameSprite*>(sprite);
				uint8_t spriteWidth = 1;
				uint8_t spriteHeight = 1;
				
				if (gameSprite) {
					spriteWidth = gameSprite->width > 0 ? gameSprite->width : 1;
					spriteHeight = gameSprite->height > 0 ? gameSprite->height : 1;
				}
				
				// Place all pieces of the multi-tile sprite
				// Tibia sprites: spriteList[0]=bottom-left, [1]=bottom-right, [2]=top-left, [3]=top-right
				// Map layout: (x,y)=top-left, (x+1,y)=top-right, (x,y+1)=bottom-left, (x+1,y+1)=bottom-right
				if (spriteWidth > 1 || spriteHeight > 1) {
					wxLogMessage("Placing multi-tile ground sprite: clientID=%d at map pos (%d,%d), baseTileID=%d, size=%dx%d", 
						clientID, x, y, baseTileID, spriteWidth, spriteHeight);
				}
				
				for (uint8_t h = 0; h < spriteHeight; ++h) {
					for (uint8_t w = 0; w < spriteWidth; ++w) {
						// Map position: Tibia uses bottom-to-top, so h=0 is bottom, h=1 is top
						// We want h=0 (bottom) at y+1, and h=1 (top) at y+0
						int tileX = x + w;
						int tileY = y + (spriteHeight - 1 - h);
						
						if (tileX >= 0 && tileX < width && tileY >= 0 && tileY < height) {
							int targetIndex = tileY * width + tileX;
							
							// Get the piece index using the same method as spritesheet generation
							// This ensures the tileID matches the spritesheet layout
							int pieceIndex = gameSprite ? gameSprite->getIndex(w, h, 0, 0, 0, 0, 0) : (h * spriteWidth + w);
							int pieceID = baseTileID + pieceIndex;
							
							if (spriteWidth > 1 || spriteHeight > 1) {
								wxLogMessage("  Piece [h=%d,w=%d] pieceIndex=%d -> tileID=%d at map pos (%d,%d)", 
									h, w, pieceIndex, pieceID, tileX, tileY);
							}
							
							groundData[targetIndex] = pieceID;
						}
					}
				}
			}
		}

		// Object layers (distribute items across layers by stack position)
		// Items are ordered bottom-to-top in the vector (index 0 = bottom)
		for (size_t layerIdx = 0; layerIdx < tile->items.size() && layerIdx < (size_t)maxItemsPerTile; ++layerIdx) {
			Item* item = tile->items[layerIdx];
			if (item) {
				uint16_t clientID = item->getClientID();
				// Fallback to item ID if clientID is 0
				if (clientID == 0) {
					clientID = item->getID();
				}
				
				// Debug: log ALL items with their clientIDs and check ItemType properties
				const ItemType& itemType = g_items.getItemType(item->getID());
				Sprite* itemSprite = g_gui.gfx.getSprite(clientID);
				GameSprite* itemGameSprite = dynamic_cast<GameSprite*>(itemSprite);
				if (itemGameSprite) {
					wxLogMessage("Item at map pos (%d,%d): clientID=%d, ID=%d, width=%d, height=%d, spriteList.size=%d", 
						x, y, clientID, item->getID(), itemGameSprite->width, itemGameSprite->height, itemGameSprite->spriteList.size());
					
					// Special debug for cacti/stone area - check if this is part of a multi-tile object
					if (clientID == 3698 || (clientID >= 3696 && clientID <= 3702)) {
						wxLogMessage("  *** STONE/CACTI DETECTED: clientID=%d at (%d,%d)", clientID, x, y);
					}
				} else {
					wxLogMessage("Item at map pos (%d,%d): clientID=%d, ID=%d (not GameSprite)", x, y, clientID, item->getID());
				}
				
				if (clientID > 0 && spriteToTileID.find(clientID) != spriteToTileID.end()) {
					int baseTileID = spriteToTileID[clientID];
					
					// Check if this is a multi-tile sprite
					Sprite* sprite = g_gui.gfx.getSprite(clientID);
					GameSprite* gameSprite = dynamic_cast<GameSprite*>(sprite);
					uint8_t spriteWidth = 1;
					uint8_t spriteHeight = 1;
					
					if (gameSprite) {
						spriteWidth = gameSprite->width > 0 ? gameSprite->width : 1;
						spriteHeight = gameSprite->height > 0 ? gameSprite->height : 1;
					}
					
					// Place all pieces of the multi-tile sprite
					// Tibia sprites: spriteList[0]=bottom-left, [1]=bottom-right, [2]=top-left, [3]=top-right
					// Map layout: (x,y)=top-left, (x+1,y)=top-right, (x,y+1)=bottom-left, (x+1,y+1)=bottom-right
					if (spriteWidth > 1 || spriteHeight > 1) {
						wxLogMessage("Placing multi-tile item sprite: clientID=%d at map pos (%d,%d), baseTileID=%d, size=%dx%d, layer=%d", 
							clientID, x, y, baseTileID, spriteWidth, spriteHeight, layerIdx);
					}
					
					for (uint8_t h = 0; h < spriteHeight; ++h) {
						for (uint8_t w = 0; w < spriteWidth; ++w) {
							// Map position: Tibia uses bottom-to-top for h, right-to-left for w
							// h=0 is bottom, h=1 is top -> flip vertically
							// w=0 is right, w=1 is left -> flip horizontally
							int tileX = x + (spriteWidth - 1 - w);
							int tileY = y + (spriteHeight - 1 - h);
							
							if (spriteWidth > 1 || spriteHeight > 1) {
								wxLogMessage("  Loop iteration: h=%d, w=%d, tileX=%d, tileY=%d, width=%d, height=%d", 
									h, w, tileX, tileY, width, height);
							}
							
							if (tileX >= 0 && tileX < width && tileY >= 0 && tileY < height) {
								int targetIndex = tileY * width + tileX;
								
								// Get the piece index using the same method as spritesheet generation
								// This ensures the tileID matches the spritesheet layout
								int pieceIndex = gameSprite ? gameSprite->getIndex(w, h, 0, 0, 0, 0, 0) : (h * spriteWidth + w);
								int pieceID = baseTileID + pieceIndex;
								
								if (spriteWidth > 1 || spriteHeight > 1) {
									wxLogMessage("    PLACING: Piece [h=%d,w=%d] pieceIndex=%d -> tileID=%d at map pos (%d,%d)", 
										h, w, pieceIndex, pieceID, tileX, tileY);
								}
								
								objectLayers[layerIdx][targetIndex] = pieceID;
							} else {
								if (spriteWidth > 1 || spriteHeight > 1) {
									wxLogMessage("    SKIPPED: Out of bounds (tileX=%d, tileY=%d)", tileX, tileY);
								}
							}
						}
					}
				}
			}
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
	int nextLayerId = 1;

	// Collision layer FIRST (invisible - only for collision detection, won't render)
	json collisionLayer;
	collisionLayer["id"] = nextLayerId++;
	collisionLayer["name"] = "collision";
	collisionLayer["type"] = "tilelayer";
	collisionLayer["visible"] = false; // Invisible so it doesn't interfere with rendering
	collisionLayer["opacity"] = 1.0;
	collisionLayer["x"] = 0;
	collisionLayer["y"] = 0;
	collisionLayer["width"] = width;
	collisionLayer["height"] = height;
	collisionLayer["data"] = collisionData;

	layers.push_back(collisionLayer);

	// Ground layer (bottom visible layer)
	json groundLayer;
	groundLayer["id"] = nextLayerId++;
	groundLayer["name"] = "ground";
	groundLayer["type"] = "tilelayer";
	groundLayer["visible"] = true;
	groundLayer["opacity"] = 1.0;
	groundLayer["x"] = 0;
	groundLayer["y"] = 0;
	groundLayer["width"] = width;
	groundLayer["height"] = height;
	groundLayer["data"] = groundData;

	layers.push_back(groundLayer);

	// Create multiple object layers (one for each stack position) - rendered on top
	for (int i = 0; i < maxItemsPerTile; ++i) {
		json objectLayer;
		objectLayer["id"] = nextLayerId++;
		
		// Name layers sequentially: "objects_1", "objects_2", etc.
		std::string layerName = "objects_" + std::to_string(i + 1);
		objectLayer["name"] = layerName;
		
		objectLayer["type"] = "tilelayer";
		objectLayer["visible"] = true;
		objectLayer["opacity"] = 1.0;
		objectLayer["x"] = 0;
		objectLayer["y"] = 0;
		objectLayer["width"] = width;
		objectLayer["height"] = height;
		objectLayer["data"] = objectLayers[i];

		layers.push_back(objectLayer);
	}

	root["layers"] = layers;

	// Tileset
	json tilesets = json::array();
	json tileset;
	tileset["columns"] = 10;
	tileset["firstgid"] = 1;
	tileset["image"] = name + "_spritesheet.png";
	
	// Calculate total number of tiles (accounting for multi-tile sprites)
	int totalTiles = 0;
	for (const auto& spriteInfo : usedSprites) {
		totalTiles += spriteInfo.width * spriteInfo.height;
	}
	
	int rows = (totalTiles + 9) / 10;
	tileset["imageheight"] = rows * 32;
	tileset["imagewidth"] = 10 * 32;
	tileset["margin"] = 0;
	tileset["name"] = name;
	tileset["spacing"] = 0;
	tileset["tilecount"] = totalTiles;
	tileset["tileheight"] = 32;
	tileset["tilewidth"] = 32;

	// Add tile properties for collision
	json tilesArray = json::array();
	for (int i = 0; i < totalTiles; ++i) {
		json tileObj;
		tileObj["id"] = i;
		
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
	root["nextlayerid"] = nextLayerId;
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
	const std::vector<SpriteInfo>& sprites)
{
	const int TILE_SIZE = 32;
	const int COLUMNS = 10;
	
	// Calculate total number of tile pieces needed
	int totalTiles = 0;
	for (const auto& spriteInfo : sprites) {
		totalTiles += spriteInfo.width * spriteInfo.height;
	}
	
	int rows = (totalTiles + COLUMNS - 1) / COLUMNS; // Round up

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

	int currentTileID = 0; // 0-based for positioning in spritesheet

	// Draw each sprite (and all its pieces if multi-tile)
	for (const auto& spriteInfo : sprites) {
		uint16_t clientID = spriteInfo.clientID;
		uint8_t spriteWidth = spriteInfo.width;
		uint8_t spriteHeight = spriteInfo.height;

		wxLogMessage("Processing sprite clientID=%d at currentTileID=%d (1-based=%d)", clientID, currentTileID, currentTileID + 1);

		// Get sprite from graphics manager
		Sprite* sprite = g_gui.gfx.getSprite(clientID);
		if (!sprite) {
			// Skip this sprite but still advance tile IDs
			wxLogMessage("  Sprite not found, skipping %d tile IDs", spriteWidth * spriteHeight);
			currentTileID += spriteWidth * spriteHeight;
			continue;
		}

		// Check if this is a multi-tile sprite that needs special handling
		GameSprite* gameSprite = dynamic_cast<GameSprite*>(sprite);
		uint8_t actualWidth = 1;
		uint8_t actualHeight = 1;
		
		if (gameSprite) {
			actualWidth = gameSprite->width > 0 ? gameSprite->width : 1;
			actualHeight = gameSprite->height > 0 ? gameSprite->height : 1;
		}

		// For multi-tile sprites (width>1 or height>1), extract each piece as a separate 32x32 tile
		if (actualWidth > 1 || actualHeight > 1) {
			wxLogMessage("Extracting multi-tile sprite: clientID=%d, width=%d, height=%d, creating %d separate tiles", 
				spriteInfo.clientID, actualWidth, actualHeight, actualWidth * actualHeight);
			
			// Extract each 32x32 piece as a separate tile in the spritesheet
			// Access the individual sprite pieces directly from spriteList
			if (gameSprite && !gameSprite->spriteList.empty()) {
				wxLogMessage("  Total pieces in spriteList: %d", gameSprite->spriteList.size());
				
				// Debug: Show what getIndex returns for each (w,h) combination
				wxLogMessage("  getIndex mapping for this sprite:");
				for (uint8_t h = 0; h < actualHeight; ++h) {
					for (uint8_t w = 0; w < actualWidth; ++w) {
						int idx = gameSprite->getIndex(w, h, 0, 0, 0, 0, 0);
						wxLogMessage("    getIndex(w=%d, h=%d) = %d", w, h, idx);
					}
				}
				
				for (uint8_t h = 0; h < actualHeight; ++h) {
					for (uint8_t w = 0; w < actualWidth; ++w) {
						int col = currentTileID % COLUMNS;
						int row = currentTileID / COLUMNS;
						
						// Get the index for this piece
						int pieceIndex = gameSprite->getIndex(w, h, 0, 0, 0, 0, 0);
						
						wxLogMessage("  Spritesheet position: row=%d, col=%d (tileID=%d, 1-based=%d) will contain: spriteList[%d] (from h=%d,w=%d)", 
							row, col, currentTileID, currentTileID + 1, pieceIndex, h, w);
						
						if (pieceIndex < gameSprite->spriteList.size()) {
							uint8_t* pieceData = gameSprite->spriteList[pieceIndex]->getRGBData();
							
							if (pieceData) {
								wxLogMessage("    Successfully extracted pieceData for spriteList[%d]", pieceIndex);
								// Copy this 32x32 piece to the spritesheet
								int destX = col * TILE_SIZE;
								int destY = row * TILE_SIZE;
								
								for (int py = 0; py < TILE_SIZE; ++py) {
									for (int px = 0; px < TILE_SIZE; ++px) {
										int srcIdx = (py * TILE_SIZE + px) * 3;
										int destIdx = ((destY + py) * imageWidth + (destX + px));
										
										if (destIdx * 3 + 2 < imageWidth * imageHeight * 3) {
											rgb[destIdx * 3] = pieceData[srcIdx];
											rgb[destIdx * 3 + 1] = pieceData[srcIdx + 1];
											rgb[destIdx * 3 + 2] = pieceData[srcIdx + 2];
											
											// Check for magenta (transparency)
											if (pieceData[srcIdx] == 255 && pieceData[srcIdx + 1] == 0 && pieceData[srcIdx + 2] == 255) {
												alpha[destIdx] = 0;
											} else {
												alpha[destIdx] = 255;
											}
										}
									}
								}
							} else {
								wxLogMessage("    ERROR: pieceData is NULL for spriteList[%d]!", pieceIndex);
							}
						} else {
							wxLogMessage("    ERROR: pieceIndex=%d is out of bounds (spriteList.size=%d)!", pieceIndex, gameSprite->spriteList.size());
						}
						
						currentTileID++;
					}
				}
				wxLogMessage("  After processing multi-tile sprite, currentTileID=%d (1-based=%d)", currentTileID, currentTileID + 1);
			} else {
				// Fallback: skip these tile IDs
				currentTileID += actualWidth * actualHeight;
				wxLogMessage("  Multi-tile sprite has no spriteList, skipped to currentTileID=%d (1-based=%d)", currentTileID, currentTileID + 1);
			}
		} else {
			// Single-tile sprite: extract normally at 32x32
			int col = currentTileID % COLUMNS;
			int row = currentTileID / COLUMNS;

			wxBitmap tempBitmap(TILE_SIZE, TILE_SIZE, 32);
			wxMemoryDC tempDC;
			tempDC.SelectObject(tempBitmap);
			
			tempDC.SetBackground(*wxTRANSPARENT_BRUSH);
			tempDC.Clear();

			sprite->DrawTo(&tempDC, SPRITE_SIZE_32x32, 0, 0);
			
			tempDC.SelectObject(wxNullBitmap);

			wxImage spriteImg = tempBitmap.ConvertToImage();
			if (spriteImg.IsOk()) {
				if (!spriteImg.HasAlpha()) {
					spriteImg.InitAlpha();
				}

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
			
			currentTileID++;
			wxLogMessage("  After processing single-tile sprite, currentTileID=%d (1-based=%d)", currentTileID, currentTileID + 1);
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

