//////////////////////////////////////////////////////////////////////////////////////////
//
//     ALUNDRA RANDOMIZER
//
// ---------------------------------------------------------------------------------------
//
//     Developed by: Dinopony (@DinoponyRuns)
//
//////////////////////////////////////////////////////////////////////////////////////////

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

#ifndef WIN32
#include <sys/wait.h>
#endif

#include "personal_settings.hpp"
#include "randomizer_options.hpp"

#include "game/game_data.hpp"
#include "model/randomizer_world.hpp"
#include "world_shuffler.hpp"
#include "io/io.hpp"

#include "tools/argument_dictionary.hpp"
#include "tools/exception.hpp"
#include "tools/json.hpp"
#include "tools/base64.hpp"
#include "tools/binary_file.hpp"
#include "tools/psx_exe_file.hpp"
#include "tools/sha1.hpp"
#include "patches/patches.hpp"

namespace
{
    constexpr uint64_t ALUNDRA_USA_1_1_SIZE = 600359760;
    constexpr const char* ALUNDRA_USA_1_1_SHA1 = "26523fc6bd463890066ca81444217b4c10efb4e2";

    struct KnownUnsupportedImage
    {
        uint64_t size;
        const char* name;
    };

    // Redump sizes for known Alundra BIN images that this patcher does not support.
    constexpr KnownUnsupportedImage UNSUPPORTED_ALUNDRA_IMAGES[] = {
        { 600289200, "Alundra USA 1.0" },
        { 606013968, "Alundra Europe" },
        { 606206832, "Alundra France" },
        { 606049248, "Alundra Germany" },
        { 606110400, "Alundra Italy" },
        { 493049760, "Alundra Japan" },
        { 606119808, "Alundra Spain" },
    };

    // Filenames accepted when --input is omitted. input.bin stays first so existing setups keep working.
    constexpr const char* KNOWN_INPUT_IMAGE_NAMES[] = {
        "input.bin",
        "Alundra.bin",
        "Alundra (USA).bin",
        "Alundra (USA) (Rev 1).bin",
        "Alundra (USA) (v1.1).bin",
    };

    std::string format_known_input_image_names()
    {
        std::string result;
        bool first = true;
        for(const char* name : KNOWN_INPUT_IMAGE_NAMES)
        {
            if(!first)
                result += ", ";
            first = false;
            result += "'";
            result += name;
            result += "'";
        }
        return result;
    }

    std::filesystem::path resolve_input_image_path(const ArgumentDictionary& args)
    {
        const std::string input_arg = args.get_string("input");
        if(!input_arg.empty())
            return input_arg;

        for(const char* name : KNOWN_INPUT_IMAGE_NAMES)
        {
            std::filesystem::path candidate("./");
            candidate /= name;
            if(std::filesystem::is_regular_file(candidate))
                return candidate;
        }

        throw RandomizerException("Could not find an Alundra disc image in the randomizer folder. "
                                  "Please place your Alundra 1.1 US image there using one of these names: "
                                  + format_known_input_image_names()
                                  + ". You can also pass a path with --input=.");
    }

    bool requested_help(int argc, char* argv[], const ArgumentDictionary& args)
    {
        if(args.contains("help") || args.contains("h"))
            return true;

        for(int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if(arg == "-h" || arg == "-help" || arg == "/?")
                return true;
        }

        return false;
    }

    void print_help()
    {
        std::cout
            << "======== Alundra Randomizer v" << RELEASE << " ========\n"
            << "\n"
            << "Generate a randomized Alundra (USA 1.1) disc image.\n"
            << "\n"
            << "Usage:\n"
            << "  alundra-randomizer [options]\n"
            << "\n"
            << "Options:\n"
            << "  --help, -h             Show this help and exit.\n"
            << "  --input=PATH           Path to an Alundra USA 1.1 .bin image.\n"
            << "                         If omitted, the current folder is searched for:\n";
        for(const char* name : KNOWN_INPUT_IMAGE_NAMES)
            std::cout << "                           " << name << "\n";
        std::cout
            << "  --outputrom=PATH       Output .bin path, or a directory (default: ./).\n"
            << "                         If PATH is a directory, the file is named after the\n"
            << "                         seed hash sentence.\n"
            << "  --outputlog=PATH       Spoiler/generation log .json path, or a directory\n"
            << "                         (default: alongside the output ROM).\n"
            << "  --preset=NAME          Preset JSON from ./presets/ (e.g. default).\n"
            << "                         If omitted, you will be prompted (Enter = default).\n"
            << "  --permalink[=CODE]     Rebuild a seed from a permalink. If CODE is omitted,\n"
            << "                         you will be prompted to paste it.\n"
            << "  --seedcount=N          Generate N seeds (default: 1).\n"
            << "  --only-logic           Randomize and write the log without dumping or\n"
            << "                         patching a disc image (no input ROM required).\n"
            << "  --graph                Write ./logic.dot as a Graphviz logic graph.\n"
            << "  --debuglog=PATH        Write a debug log JSON (only if spoiler logs are\n"
            << "                         allowed by the preset).\n"
            << "  --verbose[=PATH]       Keep dumpsxiso/mkpsxiso output in a log file\n"
            << "                         (default: ./tool.log). Without --verbose the log\n"
            << "                         is kept only if patching fails. Success stays off\n"
            << "                         the console; failures still print the tool output.\n"
#ifdef DEBUG
            << "  --dumpmodel            Dump the logic model to ./json_data/.\n"
#endif
            << "  --pause                Wait for Enter before exiting (this is the default).\n"
            << "                         Same as --pause=true.\n"
            << "  --nopause              Exit as soon as generation finishes. Same as\n"
            << "                         --pause=false.\n"
            << "\n"
            << "Game and randomizer settings (item distribution, starting inventory, crests,\n"
            << "and so on) are not CLI flags. Put them in a preset JSON under ./presets/.\n"
            << "\n"
            << "Examples:\n"
            << "  alundra-randomizer --preset=default --nopause\n"
            << "  alundra-randomizer --preset=default --verbose --nopause\n"
            << "  alundra-randomizer --input=\"Alundra (USA) (Rev 1).bin\" --outputrom=./seeds/\n"
            << "  alundra-randomizer --permalink --nopause\n"
            << "  alundra-randomizer --only-logic --preset=default --outputlog=./spoiler.json --nopause\n";
    }

    void validate_input_image(const std::filesystem::path& input_path)
    {
        if(!std::filesystem::exists(input_path))
        {
            throw RandomizerException("Input file '" + input_path.string() + "' was not found. "
                                      "Please place your Alundra 1.1 US disc image at that path, "
                                      "or omit --input to auto-detect a known filename ("
                                      + format_known_input_image_names() + ").");
        }

        const uint64_t file_size = std::filesystem::file_size(input_path);
        for(const KnownUnsupportedImage& image : UNSUPPORTED_ALUNDRA_IMAGES)
        {
            if(file_size == image.size)
            {
                throw RandomizerException("This image looks like " + std::string(image.name)
                                          + ", which is not supported. The randomizer requires Alundra USA 1.1.");
            }
        }

        if(file_size != ALUNDRA_USA_1_1_SIZE)
        {
            throw RandomizerException("Invalid file size (" + std::to_string(file_size) + ") on the image file. "
                                      "Make sure you are using a 1.1 US image.");
        }

        std::cout << "Verifying image checksum...\n";
        sha1::Hash digest;
        if(!sha1::hash_file(input_path, digest))
        {
            throw RandomizerException("Could not read image file '" + input_path.string()
                                      + "' while computing SHA-1 checksum.");
        }

        const std::string digest_hex = digest.hex();
        if(digest_hex != ALUNDRA_USA_1_1_SHA1)
        {
            throw RandomizerException("Image SHA-1 (" + digest_hex + ") does not match the known Alundra USA 1.1 dump ("
                                      + std::string(ALUNDRA_USA_1_1_SHA1) + "). "
                                      "The file may be corrupted or an unclean rip.");
        }
    }

#ifdef WIN32
    constexpr const char* DUMPSXISO_TOOL = "tools\\dumpsxiso.exe";
    constexpr const char* MKPSXISO_TOOL = "tools\\mkpsxiso.exe";
#else
    constexpr const char* DUMPSXISO_TOOL = "./tools/dumpsxiso";
    constexpr const char* MKPSXISO_TOOL = "./tools/mkpsxiso";
#endif

    void require_tool(const char* tool_path)
    {
        if(!std::filesystem::exists(tool_path))
        {
            throw RandomizerException("required tool " + std::string(tool_path)
                                      + " is missing; release packages ship it under tools/\n"
                                        "       If you built from source, get dumpsxiso and "
                                        "mkpsxiso from https://github.com/Lameguy64/mkpsxiso");
        }
    }

    void require_patch_tools()
    {
        require_tool(DUMPSXISO_TOOL);
        require_tool(MKPSXISO_TOOL);
    }

    std::string read_text_file(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if(!in)
            return {};

        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }

    void print_captured_output(const std::string& captured)
    {
        if(captured.empty())
            return;

        std::cerr << captured;
        if(captured.back() != '\n')
            std::cerr << '\n';
    }

    constexpr const char* log_file = "./tool.log";

    std::filesystem::path resolve_tool_log_path(const ArgumentDictionary& args)
    {
        if(!args.get_boolean("verbose"))
            return {};

        const std::string path = args.get_string("verbose");
        std::filesystem::path resolved = (path.empty() || path == "true") ? log_file : std::filesystem::path(path);
        if(std::filesystem::is_directory(resolved))
            resolved /= "tool.log";
        return resolved;
    }

    std::filesystem::path resolve_capture_path(const std::filesystem::path& tool_log_path)
    {
        if(!tool_log_path.empty())
            return tool_log_path;
        return log_file;
    }

    void reset_tool_log(const std::filesystem::path& log_path)
    {
        std::ofstream out(log_path, std::ios::trunc);
        if(!out)
            throw RandomizerException("Could not open tool log file for writing at path '" + log_path.string() + "'");
    }

    void discard_capture_log(const std::filesystem::path& tool_log_path)
    {
        if(!tool_log_path.empty())
            return;

        std::error_code ec;
        std::filesystem::remove(resolve_capture_path({}), ec);
    }

    // Runs the command quietly. stdout/stderr append to one log file (./tool.log, or
    // --verbose=PATH). That file is kept on --verbose or if dump/rebuild fails.
    void run_external_command(const std::string& command,
                              const std::filesystem::path& tool_log_path = {},
                              const std::string& extra_failure_hint = "")
    {
        const std::filesystem::path capture_path = resolve_capture_path(tool_log_path);

        std::ofstream header(capture_path, std::ios::app);
        if(!header)
            throw RandomizerException("Could not open tool log file for writing at path '" + capture_path.string() + "'");
        header << ">>> " << command << "\n";
        header.close();

        const std::string redirected = command + " >> \"" + capture_path.string() + "\" 2>&1";
        const int status = std::system(redirected.c_str());
        if(status == -1)
        {
            print_captured_output(read_text_file(capture_path));
            throw RandomizerException("Could not launch command: " + command
                                      + " Tool output written to '" + capture_path.string() + "'.");
        }

#ifdef WIN32
        const bool success = (status == 0);
        const int code = status;
#else
        const bool exited = WIFEXITED(status);
        const int code = exited ? WEXITSTATUS(status) : status;
        const bool success = exited && code == 0;
#endif
        if(!success)
        {
            print_captured_output(read_text_file(capture_path));
            std::string message = "Command failed with code " + std::to_string(code) + ": " + command;
            if(!extra_failure_hint.empty())
                message += " " + extra_failure_hint;
            throw RandomizerException(message + " Tool output written to '" + capture_path.string() + "'.");
        }
    }
}

/**
 * Calls the external tool `dumpsxiso` in order to dump the game image into a folder containing
 * all game files. A failed extract or missing output files are fatal.
 * 
 * @param input_file_path the path to the disc image file
 * @param output_dir_path the path to the output directory where game files will be extracted
 */
void dump_iso(const std::filesystem::path& input_file_path, const std::filesystem::path& output_dir_path,
              const std::filesystem::path& tool_log_path)
{
    const std::filesystem::path capture_path = resolve_capture_path(tool_log_path);
    reset_tool_log(capture_path);

    std::string command = DUMPSXISO_TOOL;
    command += " \"" + input_file_path.string() + "\"";
    command += " -x \"" + output_dir_path.string() + "\"";
    command += " -s \"" + output_dir_path.string() + "/build.xml\"";

    run_external_command(command, tool_log_path);

    const std::filesystem::path required_files[] = {
        output_dir_path / "DATA" / "DATAS.BIN",
        output_dir_path / "ALUN_CD.EXE",
        output_dir_path / "build.xml",
    };
    for(const std::filesystem::path& path : required_files)
    {
        if(!std::filesystem::exists(path))
        {
            print_captured_output(read_text_file(capture_path));
            throw RandomizerException("Required file '" + path.string()
                                      + "' was not created. See the extract log above. Tool output written to '"
                                      + capture_path.string() + "'.");
        }
    }
}

/**
 * Calls the external tool `mkpsxiso` in order to re-pack the game image from a folder containing
 * all game files. A non-zero exit is fatal.
 * 
 * @param input_dir_path the path to the directory containing game files
 * @param output_file_path the path to the output disc image file that will be created
 */
void rebuild_iso(const std::filesystem::path& input_dir_path, const std::filesystem::path& output_file_path,
                 const std::filesystem::path& tool_log_path)
{
    std::filesystem::path cue_file_path = output_file_path;
    cue_file_path.replace_extension("cue");

    std::string command = MKPSXISO_TOOL;
    command += " \"" + input_dir_path.string() + "build.xml\"";
    command += " -o \"" + output_file_path.string() + "\"";
    command += " -c \"" + cue_file_path.string() + "\"";
    command += " -y";

    run_external_command(command, tool_log_path, "The output image may currently be in use.");
}

/**
 * Process the given output paths (input by the user) to alter them following a bunch of rules.
 * 
 * @param output_rom_path a reference on the path that will be used for the output ROM
 * @param spoiler_log_path a reference on the path that will be used for the spoiler log
 * @param hash_sentence the seed unique "hash sentence", used as a default filename if none was given
 */
void process_paths(std::filesystem::path& output_rom_path, std::filesystem::path& spoiler_log_path,
                   const std::string& hash_sentence)
{
    // If output ROM path was not specified, put it in the current working directory
    if(output_rom_path.empty())
        output_rom_path = "./";
    
    // If output log path wasn't specified, put it alongside the ROM
    if(spoiler_log_path.empty())
    {
        spoiler_log_path = output_rom_path;
        if(spoiler_log_path.has_extension())
            spoiler_log_path.replace_extension(".json");
    }

    // If path was not containing the appropriate file extension, it is considered as a directory path,
    // so append a default filename to it.
    if(output_rom_path.extension() != ".bin")
        output_rom_path = output_rom_path / (hash_sentence + ".bin");
    if(spoiler_log_path.extension() != ".json")
        spoiler_log_path = spoiler_log_path / (hash_sentence + ".json");
}

Json randomize(RandomizerWorld& world, GameData& game_data, RandomizerOptions& options, PersonalSettings& personal_settings, const ArgumentDictionary& args)
{
    Json spoiler_json;

    spoiler_json["permalink"] = options.permalink();
    spoiler_json["hashSentence"] = options.hash_sentence();
    spoiler_json.merge_patch(options.to_json(game_data, world));

    std::cout << "\nRandomizing world...\n";
    WorldShuffler shuffler(world, game_data, options);
    shuffler.randomize_items();
    shuffler.randomize_hints();

    if(options.allow_spoiler_log())
    {
        spoiler_json.merge_patch(SpoilerWriter::build_spoiler_json(world, options));
        spoiler_json["playthrough"] = shuffler.playthrough_as_json();

        // Output debug log if requested, only if spoiler log is authorized
        std::string debug_log_path = args.get_string("debuglog");
        if (!debug_log_path.empty())
        {
            std::ofstream debug_log_file(debug_log_path);
            debug_log_file << shuffler.debug_log_as_json().dump(4);
            debug_log_file.close();
        }

#ifdef DEBUG
        // Output model if requested, only if spoiler log is authorized
        if(args.get_boolean("dumpmodel"))
        {
            std::cout << "Outputting model...\n\n";
            ModelWriter::write_logic_model(world);
            std::cout << "Model dumped to './json_data/'" << std::endl;
        }
#endif
    }

    return spoiler_json;
}

void build_patched_rom(const std::filesystem::path& input_path, const std::filesystem::path& output_path, 
                       GameData& game_data, RandomizerWorld& world, const RandomizerOptions& options,
                       const std::filesystem::path& tool_log_path)
{
#ifdef DEBUG
    std::filesystem::remove_all("./tmp_dump/");
#endif

    std::cout << "Checking input image '" << input_path.string() << "'...\n";
    validate_input_image(input_path);

    // Dump the input ROM into a "tmp_dump" folder
    std::cout << "Extracting game files...\n";
    if(!tool_log_path.empty())
        std::cout << "Writing tool output to " << tool_log_path << ".\n";
    dump_iso(input_path, "./tmp_dump/", tool_log_path);

    // Apply patches to relevant files that were extracted from the game ROM
    std::cout << "Editing game files...\n";
    BinaryFile datas_file("./tmp_dump/DATA/DATAS.BIN");
    PsxExeFile exe_file("./tmp_dump/ALUN_CD.EXE");

    apply_randomizer_patches(datas_file, exe_file, game_data, world, options);

    datas_file.save();
    exe_file.save();

    // Use an external tool to repack the files into a PS1 disc image
    std::cout << "Building a disc image...\n";
    rebuild_iso("./tmp_dump/", output_path, tool_log_path);

#ifndef DEBUG
    std::filesystem::remove_all("./tmp_dump/");
#endif

    discard_capture_log(tool_log_path);

    std::cout << "Randomized game outputted to " << output_path << ".\n";
}

void generate(const ArgumentDictionary& args)
{
    // Fail fast if the disc image or patch tools are missing (--only-logic needs neither).
    const bool patch_rom = !args.contains("only-logic");
    std::filesystem::path input_rom_path;
    if(patch_rom)
    {
        require_patch_tools();
        input_rom_path = resolve_input_image_path(args);
        std::cout << "Using input image '" << input_rom_path.string() << "'.\n";
    }

    GameData game_data;
    RandomizerWorld world(game_data);
    RandomizerOptions options(args, game_data, world);
    PersonalSettings personal_settings(args);

    game_data.apply_options(options);
    world.apply_options(options, game_data);

    Json spoiler_json = randomize(world, game_data, options, personal_settings, args);

    // Parse output paths from args
    std::filesystem::path output_rom_path = args.get_string("outputrom", "");
    std::filesystem::path spoiler_log_path = args.get_string("outputlog", "");
    process_paths(output_rom_path, spoiler_log_path, options.hash_sentence());

    if(patch_rom)
        build_patched_rom(input_rom_path, output_rom_path, game_data, world, options,
                          resolve_tool_log_path(args));
    
    // Write a spoiler log to help the player
    if(!spoiler_log_path.empty())
    {
        std::ofstream spoiler_file(spoiler_log_path);
        if(!spoiler_file)
            throw RandomizerException("Could not open output log file for writing at path '" + spoiler_log_path.string() + "'");

        spoiler_file << spoiler_json.dump(4);
        spoiler_file.close();
        if(options.allow_spoiler_log())
            std::cout << "Spoiler log written into " << spoiler_log_path << ".\n";
        else
            std::cout << "Generation log written into " << spoiler_log_path << ".\n";
    }

    if(args.contains("graph"))
        GraphvizWriter::write_logic_as_dot(world, "./logic.dot");

    std::cout << "\nHash sentence: " << options.hash_sentence() << "\n";
    std::cout << "\nPermalink: " << options.permalink() << "\n";
    std::cout << "\nShare the permalink above with other people to enable them building the exact same seed.\n" << std::endl;
}

int main(int argc, char* argv[])
{
    int return_code = EXIT_SUCCESS;

    ArgumentDictionary args(argc, argv);

    if(requested_help(argc, argv, args))
    {
        print_help();
        return EXIT_SUCCESS;
    }

    std::cout << "======== Alundra Randomizer v" << RELEASE << " ========\n\n";

    if(args.contains("permalink") && args.get_string("permalink").empty())
    {
        std::string permalink;
        std::cout << "Please specify a permalink: ";
        std::getline(std::cin, permalink);
        args.set_string("permalink", permalink);
    }

    try
    {
        int seed_count = args.get_integer("seedcount", 1);
        for(int i=0 ; i<seed_count ; ++i)
            generate(args);
    }
    catch(RandomizerException& e)
    {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return_code = EXIT_FAILURE;
    }
    catch(Json::parse_error& e)
    {
        std::cerr << "ERROR: Malformed json -> " << e.what() << std::endl;
        return_code = EXIT_FAILURE;
    }

    if(args.get_boolean("pause", true))
    {
        std::cout << "\nPress any key to exit.";
        std::string dummy;
        std::getline(std::cin, dummy);
    }

    return return_code;
}
