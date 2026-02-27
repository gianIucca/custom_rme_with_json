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

#ifndef RME_JSON_EXPORT_H_
#define RME_JSON_EXPORT_H_

#include "map.h"
#include "editor.h"
#include <wx/image.h>
#include <unordered_map>
#include <unordered_set>

class IOMapJSON
{
public:
	IOMapJSON(Editor* editor);
	~IOMapJSON();

	// Export selected area to Tiled JSON format
	bool exportSelection(const std::string& directory, const std::string& name);

	const std::string& getError() const noexcept { return m_error; }

private:
	// Generate spritesheet PNG from used sprites
	bool generateSpritesheet(const std::string& directory, const std::string& name,
		const std::unordered_map<uint16_t, int>& spriteMapping);

	// Get sprite data from game sprite
	bool extractSpriteData(uint16_t clientID, uint8_t* buffer);

	Editor* m_editor;
	std::string m_error;
};

#endif

