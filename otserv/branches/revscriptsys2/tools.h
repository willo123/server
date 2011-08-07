//////////////////////////////////////////////////////////////////////
// OpenTibia - an opensource roleplaying game
//////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//////////////////////////////////////////////////////////////////////

#ifndef __OTSERV_TOOLS_H__
#define __OTSERV_TOOLS_H__

#include "enums.h"
#include "const.h"

enum DistributionType_t {
	DISTRO_UNIFORM,
	DISTRO_SQUARE,
	DISTRO_NORMAL
};

void replaceString(std::string& str, const std::string& sought, const std::string& replacement);
void trim_right(std::string& source, const std::string& t = "\n\t ");
void trim_left(std::string& source, const std::string& t = "\n\t ");
void trim(std::string& source, const std::string& t = "\n\t ");
void toLowerCaseString(std::string& source);
void toUpperCaseString(std::string& source);
std::string asLowerCaseString(const std::string& source);
std::string asUpperCaseString(const std::string& source);
bool utf8ToLatin1(char* intext, std::string& outtext);
bool readXMLInteger(xmlNodePtr node, const char* tag, int32_t& value);
bool readXMLInteger64(xmlNodePtr node, const char* tag, uint64_t& value);
bool readXMLFloat(xmlNodePtr node, const char* tag, float& value);
bool readXMLString(xmlNodePtr node, const char* tag, std::string& value);
bool readXMLContentString(xmlNodePtr node, std::string& value);
std::vector<std::string> explodeString(const std::string& inString, const std::string& separator);
bool hasBitSet(uint32_t flag, uint32_t flags);
int random_range(int lowest_number, int highest_number, DistributionType_t type = DISTRO_UNIFORM);
void hexdump(unsigned char *_data, int _len);
char upchar(char c);
bool passwordTest(std::string plain, std::string &hash);
std::string convertIPToString(uint32_t ip);
void formatDate(time_t time, char* buffer);
void formatDateShort(time_t time, char* buffer);
std::string getViolationReasonString(int32_t reasonId);
std::string getViolationActionString(ViolationAction actionId, bool ipBanishment);
std::string playerSexAdjectiveString(PlayerSex sex);
std::string playerSexSubjectString(PlayerSex sex);
std::string combatTypeToString(CombatType type);
uint32_t adlerChecksum(uint8_t *data, int32_t len);
#endif
