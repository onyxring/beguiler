#pragma once
#include <string>
#include <vector>
#include <ostream>

// A single asset discovered in the asset directory
struct BlorbAsset {
    std::string name;        // enum member name, e.g. "priestPng"
    std::string filePath;    // full path to the file
    int         resourceId;  // 1-based, globally sequential across all asset types
    bool        isPicture;   // true → Pict/PNG , false → Snd /AIFF
};

class Blorb {
public:
    // Phase 1 — Asset scan
    // Scan assetDir (non-recursive) for PNG/JPEG/AIFF files.
    // Returns assets sorted by type (pictures first) then name, IDs 1-based.
    std::vector<BlorbAsset> scanAssets(const std::string& assetDir);

    // Write _blorbAssets.bgl containing a single eAssets enum.
    // outputBglPath: full path to the file to write.
    // sourceFileName: shown in the header comment.
    void writeEnumFile(const std::vector<BlorbAsset>& assets,
                       const std::string& outputBglPath,
                       const std::string& sourceFileName);

    // Phase 2 — Blorb assembly
    // Assemble an IFF/IFRS blorb from the story file and assets.
    // Returns true on error.
    struct Metadata {
        std::string ifid, title, author, headline, genre, description;
        std::string language, series, firstPublished, forgiveness;
        int seriesNumber = 0;
    };
    bool build(const std::string& storyFile,
               const std::string& blorbOutPath,
               const std::vector<BlorbAsset>& assets,
               const std::string& copyright,
               bool isGlulx,
               const Metadata& meta);

private:
    void             writeU32BE(std::ostream& out, uint32_t v);
    std::vector<char> readFile(const std::string& path);

    // Derive a camelCase enum member name from a filename stem + extension.
    // E.g. "cover-art" + "png" → "coverArtPng"
    std::string makeMemberName(const std::string& stem, const std::string& ext);
};
