#include "placement_seed_map.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

#include "../constants/item_codes.hpp"
#include "../game/game_data.hpp"
#include "../model/item_source.hpp"
#include "../model/randomizer_world.hpp"
#include "../randomizer_options.hpp"
#include "../tools/exception.hpp"

namespace
{
    constexpr int PLACEMENT_FORMAT = 1;

    bool parse_location_id(const std::string& key, uint16_t& location_id)
    {
        if(key.empty())
            return false;

        for(char character : key)
        {
            if(character < '0' || character > '9')
                return false;
        }

        uint16_t value = 0;
        const char* begin = key.data();
        const char* end = key.data() + key.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if(result.ec != std::errc() || result.ptr != end)
            return false;

        // Reject leading zeros so "01" and "1" cannot both appear.
        if(std::to_string(value) != key)
            return false;

        location_id = value;
        return true;
    }

    std::map<uint16_t, uint8_t> read_placements_object(const Json& object)
    {
        if(!object.is_object())
        {
            throw RandomizerException("Placement seed map \"placements\" must be a JSON object "
                                      "mapping location ids to item ids.");
        }
        if(object.empty())
            throw RandomizerException("Placement seed map contains no locations.");

        std::map<uint16_t, uint8_t> placements;
        for(auto& [key, value] : object.items())
        {
            uint16_t location_id = 0;
            if(!parse_location_id(key, location_id))
            {
                throw RandomizerException("Placement location id '" + key
                                          + "' must be a decimal id with no leading zeros.");
            }

            if(!value.is_number_integer())
            {
                throw RandomizerException("Placement item id for location " + key
                                          + " must be an integer item id.");
            }

            const int64_t raw_item_id = value.get<int64_t>();
            if(raw_item_id < 0 || raw_item_id >= ITEM_COUNT)
            {
                throw RandomizerException("Placement item id " + std::to_string(raw_item_id)
                                          + " for location " + key + " is not a valid item id.");
            }

            placements.emplace(location_id, static_cast<uint8_t>(raw_item_id));
        }

        return placements;
    }

    bool is_bare_placement_map(const Json& json)
    {
        if(!json.is_object() || json.empty())
            return false;

        for(auto& [key, _] : json.items())
        {
            uint16_t location_id = 0;
            if(!parse_location_id(key, location_id))
                return false;
        }

        return true;
    }

    void read_optional_string(const Json& json, const char* key, std::string& output)
    {
        if(!json.contains(key))
            return;

        if(!json.at(key).is_string())
            throw RandomizerException(std::string("Placement seed map \"") + key + "\" must be a string.");

        output = json.at(key).get<std::string>();
    }

    std::map<uint16_t, ItemSource*> index_item_sources(const RandomizerWorld& world)
    {
        std::map<uint16_t, ItemSource*> by_location_id;
        for(ItemSource* source : world.item_sources())
        {
            const uint16_t location_id = source->id();
            if(location_id == 0)
                throw RandomizerException("Item source '" + source->name() + "' has no location id.");

            if(by_location_id.contains(location_id))
            {
                throw RandomizerException("Duplicate location id " + std::to_string(location_id)
                                          + " on item source '" + source->name() + "'.");
            }

            by_location_id.emplace(location_id, source);
        }

        return by_location_id;
    }
}

placement_seed_map::Contents placement_seed_map::read(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if(!input)
        throw RandomizerException("Could not open placement seed map '" + path.string() + "'.");

    std::stringstream buffer;
    buffer << input.rdbuf();

    Json json;
    try
    {
        json = Json::parse(buffer.str(), nullptr, true, true);
    }
    catch(const Json::parse_error& error)
    {
        throw RandomizerException("Malformed placement seed map '" + path.string() + "': " + error.what());
    }

    if(!json.is_object())
        throw RandomizerException("Placement seed map '" + path.string() + "' must be a JSON object.");

    Contents contents;
    if(is_bare_placement_map(json))
    {
        contents.placements = read_placements_object(json);
        return contents;
    }

    if(json.contains("format"))
    {
        if(!json.at("format").is_number_integer() || json.at("format").get<int>() != PLACEMENT_FORMAT)
        {
            throw RandomizerException("Unsupported placement seed map format in '" + path.string()
                                      + "'. This build reads format " + std::to_string(PLACEMENT_FORMAT) + ".");
        }
    }

    read_optional_string(json, "permalink", contents.permalink);
    if(json.contains("permalink") && contents.permalink.empty())
        throw RandomizerException("Placement seed map permalink is empty.");

    std::string hash_sentence;
    read_optional_string(json, "hashSentence", hash_sentence);
    (void)hash_sentence;

    if(!contents.permalink.empty())
    {
        if(json.contains("seed") || json.contains("gameSettings") || json.contains("randomizerSettings"))
        {
            throw RandomizerException("Placement seed map '" + path.string()
                                      + "' contains a permalink and also seed or settings. Keep only one.");
        }

        contents.has_options = true;
        contents.options_json["permalink"] = contents.permalink;
    }
    else
    {
        if(json.contains("seed"))
        {
            if(!json.at("seed").is_number_integer())
                throw RandomizerException("Placement seed map \"seed\" must be an integer.");

            const int64_t seed = json.at("seed").get<int64_t>();
            if(seed < 0 || seed > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
                throw RandomizerException("Placement seed map \"seed\" does not fit in an unsigned 32-bit value.");

            contents.options_json["seed"] = seed;
        }
        if(json.contains("gameSettings"))
        {
            if(!json.at("gameSettings").is_object())
                throw RandomizerException("Placement seed map \"gameSettings\" must be a JSON object.");
            contents.options_json["gameSettings"] = json.at("gameSettings");
        }
        if(json.contains("randomizerSettings"))
        {
            if(!json.at("randomizerSettings").is_object())
                throw RandomizerException("Placement seed map \"randomizerSettings\" must be a JSON object.");
            contents.options_json["randomizerSettings"] = json.at("randomizerSettings");
        }

        contents.has_options = !contents.options_json.empty();
    }

    if(!json.contains("placements"))
    {
        throw RandomizerException("Placement seed map '" + path.string()
                                  + "' is missing \"placements\" (location id to item id).");
    }

    contents.placements = read_placements_object(json.at("placements"));

    for(auto& [key, _] : json.items())
    {
        if(key == "format" || key == "permalink" || key == "hashSentence" || key == "seed"
           || key == "gameSettings" || key == "randomizerSettings" || key == "placements")
            continue;

        throw RandomizerException("Unknown key '" + key + "' in placement seed map '" + path.string()
                                  + "'. Location ids belong inside \"placements\".");
    }

    return contents;
}

void placement_seed_map::write(const std::filesystem::path& path, const RandomizerOptions& options, const RandomizerWorld& world)
{
    const std::map<uint16_t, ItemSource*> by_location_id = index_item_sources(world);

    Json placements = Json::object();
    for(const auto& [location_id, source] : by_location_id)
    {
        if(source->item() == nullptr)
        {
            throw RandomizerException("Location " + std::to_string(location_id) + " (" + source->name()
                                      + ") has no item to record in the placement seed map.");
        }

        placements[std::to_string(location_id)] = static_cast<int>(source->item()->id());
    }

    Json json = Json::object();
    json["format"] = PLACEMENT_FORMAT;
    json["permalink"] = options.permalink();
    json["hashSentence"] = options.hash_sentence();
    json["placements"] = std::move(placements);

    std::ofstream output(path);
    if(!output)
        throw RandomizerException("Could not open placement seed map for writing at path '" + path.string() + "'.");

    output << json.dump(4);
    if(!output)
        throw RandomizerException("Could not write placement seed map at path '" + path.string() + "'.");
}

void placement_seed_map::apply(RandomizerWorld& world, const GameData& game_data, const std::map<uint16_t, uint8_t>& placements)
{
    const std::map<uint16_t, ItemSource*> by_location_id = index_item_sources(world);

    for(const auto& [location_id, item_id] : placements)
    {
        const auto found = by_location_id.find(location_id);
        if(found == by_location_id.end())
        {
            throw RandomizerException("Placement seed map refers to unknown location id "
                                      + std::to_string(location_id) + ".");
        }

        if(item_id >= ITEM_COUNT)
        {
            throw RandomizerException("Placement item id " + std::to_string(item_id) + " for location "
                                      + std::to_string(location_id) + " is not a valid item id.");
        }

        found->second->item(game_data.item(item_id));
    }

    for(const auto& [location_id, source] : by_location_id)
    {
        if(!placements.contains(location_id))
        {
            throw RandomizerException("Placement seed map is missing location id " + std::to_string(location_id)
                                      + " (" + source->name() + ").");
        }
    }
}
