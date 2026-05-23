#include "New.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace
{
    // ANSI Color Codes (same convention as Pack.cpp).
    constexpr const char *kReset = "\033[0m";
    constexpr const char *kBold  = "\033[1m";
    constexpr const char *kGreen = "\033[32m";
    constexpr const char *kCyan  = "\033[36m";
    constexpr const char *kRed   = "\033[31m";
    constexpr const char *kDim   = "\033[2m";

    constexpr const char *kTemplateRepo = "https://github.com/bokken-technologies/template.git";

    // Validation

    // Slugs are used as the directory name and the internalSlug field
    // in project.bokken. They must satisfy CMake's identifier rules at
    // minimum — letters/digits/underscore, not starting with a digit —
    // plus we permit dashes because they're conventional in directory
    // names.
    bool isValidSlug(const std::string &slug)
    {
        if (slug.empty())
            return false;
        if (!std::isalpha(static_cast<unsigned char>(slug[0])) && slug[0] != '_')
            return false;
        for (char c : slug)
        {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
                return false;
        }
        return true;
    }

    // Project names are human-facing — displayed in the window title,
    // the README heading, etc. We allow most printable text but reject
    // characters that would either break shell quoting (we don't
    // interpolate the name into a shell command, but the user might
    // copy-paste the project later) or wreck JSON / Markdown rendering.
    // The bar is low: no control chars, no embedded newlines.
    bool isValidProjectName(const std::string &name)
    {
        if (name.empty())
            return false;
        for (char c : name)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            // Reject ASCII control characters (incl. tab and newline).
            // Higher-byte UTF-8 sequences are passed through untouched —
            // someone wanting a project called "ねこ" or "Café" should
            // not be blocked by an ASCII bias.
            if (uc < 0x20 || uc == 0x7F)
                return false;
        }
        return true;
    }

    // String utilities

    std::string toLower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::string trim(const std::string &s)
    {
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    // Convert a project name into a CMake/Make-safe identifier while
    // preserving the original casing. Used for the CMake project()
    // value and the Makefile APP_NAME — both of which want something
    // close to what the human typed, just legal as an identifier.
    //
    // Rules: letters/digits/underscores pass through unchanged; spaces,
    // tabs, and dashes become underscores; everything else (punctuation,
    // non-ASCII bytes) is dropped. Runs of underscores are collapsed
    // and trailing underscores are trimmed. A leading underscore is
    // prepended if the result would otherwise start with a digit, so
    // CMake's identifier rule is satisfied.
    //
    // Examples:
    //   "Pizza Time"      → "Pizza_Time"
    //   "my-game-2"       → "my_game_2"
    //   "Café"            → "Caf"
    //   "42 Game"         → "_42_Game"
    //   "Foo  Bar---Baz"  → "Foo_Bar_Baz"
    std::string identifierize(const std::string &name)
    {
        std::string out;
        out.reserve(name.size());
        bool lastWasUnderscore = false;
        for (char c : name)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || uc == '_')
            {
                out += static_cast<char>(c);
                lastWasUnderscore = (uc == '_');
            }
            else if (uc == ' ' || uc == '\t' || uc == '-')
            {
                if (!lastWasUnderscore && !out.empty())
                {
                    out += '_';
                    lastWasUnderscore = true;
                }
            }
            // Everything else (punctuation, non-ASCII bytes) is dropped.
        }
        // Trim trailing underscore left over from name-ending whitespace
        // or punctuation.
        while (!out.empty() && out.back() == '_')
            out.pop_back();

        // Identifiers may not start with a digit (CMake rule, also the
        // safe choice for Make variable names).
        if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
            out = "_" + out;

        return out;
    }

    // Derive a sensible default slug from a project name. Lowercases,
    // turns whitespace into dashes, drops anything that isn't a letter,
    // digit, dash, or underscore, and collapses runs of dashes. The
    // result is guaranteed to be a valid slug if there's at least one
    // ASCII letter in the input; otherwise the caller has to prompt
    // the user for an explicit slug.
    std::string slugify(const std::string &name)
    {
        std::string out;
        out.reserve(name.size());
        bool lastWasDash = false;
        for (char c : name)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isalnum(uc) || uc == '_')
            {
                out += static_cast<char>(std::tolower(uc));
                lastWasDash = false;
            }
            else if (uc == ' ' || uc == '\t' || uc == '-')
            {
                if (!lastWasDash && !out.empty())
                {
                    out += '-';
                    lastWasDash = true;
                }
            }
            // Anything else (punctuation, non-ASCII bytes, etc.) is dropped.
        }
        while (!out.empty() && out.back() == '-')
            out.pop_back();

        if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
            out = "_" + out;

        return out;
    }

    // Escape a string for embedding inside a JSON string literal.
    // Only the characters JSON requires escaping are touched; UTF-8
    // bytes pass through (RFC 8259 §7 explicitly allows raw UTF-8 in
    // JSON strings).
    std::string jsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            switch (c)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                }
                else
                {
                    out += c;
                }
                break;
            }
        }
        return out;
    }

    // File I/O

    bool readWholeFile(const fs::path &path, std::string &out)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open())
            return false;
        std::stringstream buf;
        buf << ifs.rdbuf();
        out = buf.str();
        return true;
    }

    bool writeWholeFile(const fs::path &path, const std::string &content)
    {
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open())
            return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        return ofs.good();
    }

    std::string regexReplace(const std::string &content,
                             const std::string &pattern,
                             const std::string &replacement)
    {
        return std::regex_replace(content, std::regex(pattern), replacement);
    }

    // Interactive prompts

    // Print a prompt, read a line, return it trimmed. If the user just
    // hits enter (empty line) and a default is provided, the default is
    // returned. The `defaultValue` is rendered in dim text inside [...]
    // brackets so it's clear that pressing enter will accept it.
    std::string promptLine(const std::string &label, const std::string &defaultValue = "")
    {
        std::cout << kBold << label << kReset;
        if (!defaultValue.empty())
            std::cout << " " << kDim << "[" << defaultValue << "]" << kReset;
        std::cout << ": " << std::flush;

        std::string line;
        if (!std::getline(std::cin, line))
        {
            // EOF (Ctrl-D) — give back the default if there is one,
            // empty otherwise. The caller validates either way.
            std::cout << "\n";
            return defaultValue;
        }

        const std::string trimmed = trim(line);
        return trimmed.empty() ? defaultValue : trimmed;
    }

    // Placeholder rewrites
    //
    // All rewrites use anchored regexes so the literal word "Template"
    // inside comments or documentation stays untouched. Only structural
    // occurrences (after `project(`, after `APP_NAME =`, inside specific
    // JSON fields, in the top-level Markdown heading) are rewritten.
    //
    // The CMake project() and Makefile APP_NAME values are derived from
    // the project NAME (case-preserving identifierize), not the slug —
    // so a project named "Pizza Time" produces `project(Pizza_Time)`
    // and `APP_NAME = Pizza_Time`, not `project(pizza_time)`.

    bool rewriteCMakeLists(const fs::path &path, const std::string &name)
    {
        std::string content;
        if (!readWholeFile(path, content))
        {
            std::cerr << kRed << "  [!] Could not read " << path << kReset << "\n";
            return false;
        }
        const std::string ident = identifierize(name);

        content = regexReplace(content, R"(project\(\s*Template\b)", "project(" + ident);
        return writeWholeFile(path, content);
    }

    bool rewriteMakefile(const fs::path &path, const std::string &name)
    {
        std::string content;
        if (!readWholeFile(path, content))
        {
            std::cerr << kRed << "  [!] Could not read " << path << kReset << "\n";
            return false;
        }
        const std::string ident = identifierize(name);

        content = regexReplace(content, R"((^|\n)APP_NAME\s*=\s*Template\b)",
                               "$1APP_NAME = " + ident);
        return writeWholeFile(path, content);
    }

    bool rewriteProjectBokken(const fs::path &path,
                              const std::string &name,
                              const std::string &slug)
    {
        std::string content;
        if (!readWholeFile(path, content))
        {
            std::cerr << kRed << "  [!] Could not read " << path << kReset << "\n";
            return false;
        }

        const std::string nameJson = jsonEscape(name);
        // Slug is already restricted to [A-Za-z0-9_-] so JSON-escaping
        // is a no-op, but routing it through the same helper keeps the
        // rewrite path uniform.
        const std::string slugJson = jsonEscape(slug);

        content = regexReplace(content,
                               R"("displayTitle"\s*:\s*"Template")",
                               "\"displayTitle\": \"" + nameJson + "\"");
        content = regexReplace(content,
                               R"("internalSlug"\s*:\s*"template")",
                               "\"internalSlug\": \"" + slugJson + "\"");
        return writeWholeFile(path, content);
    }

    // The README's `# Template` heading is the very first line of the
    // template's README.md. Anchored to start-of-string (^) plus the
    // trailing newline so we don't accidentally rewrite a `# Template`
    // that might appear later inside a code block. Avoiding
    // std::regex::multiline keeps us portable across older libc++
    // releases that didn't ship the flag until Clang 15.
    bool rewriteReadme(const fs::path &path, const std::string &name)
    {
        std::string content;
        if (!readWholeFile(path, content))
        {
            // README is optional from the rewrite step's perspective —
            // a user may have deleted it before re-running. Don't fail
            // the scaffold over a missing optional file.
            return true;
        }

        // $ is the back-reference sigil in std::regex_replace's
        // replacement format, so a literal $ in the project name has to
        // be doubled. (Backslash is NOT special in ECMAScript-mode
        // replacements, so it passes through unchanged.)
        std::string escapedName = name;
        escapedName = std::regex_replace(escapedName, std::regex(R"(\$)"), "$$$$");

        content = std::regex_replace(content,
                                     std::regex(R"(^# Template[ \t]*\n)"),
                                     "# " + escapedName + "\n");
        return writeWholeFile(path, content);
    }

    // Misc

    bool stripGitDirectory(const fs::path &projectDir)
    {
        const fs::path gitDir = projectDir / ".git";
        if (!fs::exists(gitDir))
            return true;

        std::error_code ec;
        fs::remove_all(gitDir, ec);
        if (ec)
        {
            std::cerr << kRed << "  [!] Could not remove " << gitDir
                      << ": " << ec.message() << kReset << "\n";
            return false;
        }
        return true;
    }

    void printUsage(std::ostream &out)
    {
        out << "Usage:\n"
            << "  bokken-cli new                       Interactive — prompts for project name and slug\n"
            << "  bokken-cli new --name <name> --slug <slug>\n"
            << "                                       Non-interactive — for scripting\n"
            << "  bokken-cli new --name <name>         Non-interactive — slug derived from name\n";
    }

    // Read --name / --slug flags from argv. Returns true on success;
    // returns false (with an error message printed) for malformed flags.
    // Missing values are returned as empty strings — the caller
    // populates them from prompts or derived defaults.
    bool parseFlags(int argc, char *argv[], std::string &outName, std::string &outSlug)
    {
        // argv[0] is "new"; flags start at index 1.
        for (int i = 1; i < argc; i++)
        {
            const std::string a = argv[i];
            if (a == "--name" && i + 1 < argc)
            {
                outName = argv[++i];
            }
            else if (a == "--slug" && i + 1 < argc)
            {
                outSlug = argv[++i];
            }
            else if (a == "--help" || a == "-h")
            {
                printUsage(std::cout);
                std::exit(0);
            }
            else
            {
                std::cerr << kRed << "Error: unknown argument '" << a << "'" << kReset << "\n\n";
                printUsage(std::cerr);
                return false;
            }
        }
        return true;
    }
}

int Bokken::CLI::runNew(int argc, char *argv[])
{
    std::string projectName;
    std::string slug;

    if (!parseFlags(argc, argv, projectName, slug))
        return 1;

    // Step 1: project name
    //
    // If --name wasn't passed, prompt. The prompt loop accepts any
    // human-readable string (rejects only ASCII control characters).
    // The name additionally has to identifierize to something non-empty
    // — otherwise CMake's project() would be called with no argument
    // and the build would fail.

    std::cout << kBold << "Bokken project scaffolder" << kReset << "\n";
    std::cout << kDim << "Press Ctrl-C at any time to abort.\n\n" << kReset;

    if (projectName.empty())
    {
        while (true)
        {
            projectName = promptLine("Project name");
            if (projectName.empty())
            {
                std::cerr << kRed << "  Project name cannot be empty." << kReset << "\n";
                continue;
            }
            if (!isValidProjectName(projectName))
            {
                std::cerr << kRed << "  Project name contains control characters; please try again."
                          << kReset << "\n";
                continue;
            }
            if (identifierize(projectName).empty())
            {
                std::cerr << kRed
                          << "  Project name must contain at least one ASCII letter or digit "
                             "(it's used as the executable name)." << kReset << "\n";
                continue;
            }
            break;
        }
    }
    else
    {
        if (!isValidProjectName(projectName))
        {
            std::cerr << kRed << "Error: --name '" << projectName
                      << "' contains control characters." << kReset << "\n";
            return 1;
        }
        if (identifierize(projectName).empty())
        {
            std::cerr << kRed << "Error: --name '" << projectName
                      << "' must contain at least one ASCII letter or digit "
                      << "(it's used as the executable name)." << kReset << "\n";
            return 1;
        }
    }

    // Step 2: slug
    //
    // Default slug is derived from the project name. The user may
    // accept it (press enter) or type a different value. Either way
    // the final value must satisfy isValidSlug.

    if (slug.empty())
    {
        const std::string defaultSlug = slugify(projectName);
        const bool defaultUsable = isValidSlug(defaultSlug);

        while (true)
        {
            slug = promptLine("Slug (folder name)",
                              defaultUsable ? defaultSlug : "");
            if (slug.empty())
            {
                std::cerr << kRed << "  Slug cannot be empty." << kReset << "\n";
                continue;
            }
            if (!isValidSlug(slug))
            {
                std::cerr << kRed
                          << "  Slug must start with a letter or underscore and "
                             "consist of letters, digits, underscores, or dashes only."
                          << kReset << "\n";
                continue;
            }
            break;
        }
    }
    else if (!isValidSlug(slug))
    {
        std::cerr << kRed << "Error: --slug '" << slug << "' is invalid."
                  << kReset << "\n"
                  << "Slugs must start with a letter or underscore and consist of "
                  << "letters, digits, underscores, or dashes only.\n";
        return 1;
    }

    // Step 3: destination

    const fs::path destination = fs::current_path() / slug;

    if (fs::exists(destination))
    {
        std::cerr << kRed << "Error: " << destination << " already exists." << kReset << "\n"
                  << "Pick a different slug, or remove the existing directory first.\n";
        return 1;
    }

    std::cout << "\n" << kBold << "Scaffolding " << kCyan << projectName
              << kReset << kBold << " into ./" << slug << "/" << kReset << "\n";

    // Step 4: clone

    std::cout << kCyan << "  [1/4] " << kReset
              << "Cloning template from " << kTemplateRepo << "...\n";

    // The slug has been validated against [A-Za-z_][A-Za-z0-9_-]*
    // so it's safe to interpolate into the shell command.
    const std::string cloneCommand =
        std::string("git clone --depth 1 --quiet ") + kTemplateRepo + " " + slug;
    const int cloneRc = std::system(cloneCommand.c_str());
    if (cloneRc != 0)
    {
        std::cerr << kRed << "  [!] git clone failed (exit code " << cloneRc << ")."
                  << kReset << "\n"
                  << "Check that git is installed and you have network access to GitHub.\n";
        std::error_code ec;
        fs::remove_all(destination, ec);
        return 1;
    }

    // Step 5: strip .git

    std::cout << kCyan << "  [2/4] " << kReset
              << "Stripping .git/ for a clean working tree...\n";
    if (!stripGitDirectory(destination))
        return 1;

    // Step 6: rewrite placeholders

    std::cout << kCyan << "  [3/4] " << kReset
              << "Rewriting placeholders (name → '" << projectName
              << "', slug → '" << slug << "')...\n";

    bool ok = true;
    ok &= rewriteCMakeLists   (destination / "CMakeLists.txt", projectName);
    ok &= rewriteMakefile     (destination / "Makefile",       projectName);
    ok &= rewriteProjectBokken(destination / "project.bokken", projectName, slug);
    ok &= rewriteReadme       (destination / "README.md",      projectName);

    if (!ok)
    {
        std::cerr << kRed << "  [!] One or more placeholder rewrites failed. "
                  << "The cloned directory was left at " << destination
                  << " for inspection." << kReset << "\n";
        return 1;
    }

    // Step 7: done

    std::cout << kCyan << "  [4/4] " << kReset << "Done.\n\n";
    std::cout << kGreen << kBold << "✓ Project ready at " << destination
              << kReset << "\n\n";
    std::cout << "Next steps:\n"
              << "  cd " << slug << "\n"
              << "  make build      # First build clones the engine via FetchContent\n"
              << "  make run\n";

    return 0;
}