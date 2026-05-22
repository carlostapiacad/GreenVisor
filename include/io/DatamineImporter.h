#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace visor::datamine {

namespace fs = std::filesystem;

inline std::string trim(std::string s)
{
    auto notSpace = [](unsigned char c) { return !std::isspace(c) && c != '\0'; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

inline std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

inline std::string normalizeFieldName(const std::string &s)
{
    return upper(trim(s));
}

template <typename T>
T byteSwap(T value)
{
    static_assert(std::is_trivially_copyable<T>::value, "byteSwap requires trivially copyable type");
    std::array<std::uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    std::reverse(bytes.begin(), bytes.end());
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}

enum class DmFieldType
{
    Numeric,
    Alpha
};

struct DmField
{
    std::string name;
    DmFieldType type{};
    int logicalRecPos = 0;
    int wordNumber = 0;
    std::string unit;
    double defaultNumeric = 0.0;
    std::string defaultAlpha;

    bool isImplicit() const { return logicalRecPos == 0; }
    bool isNumeric() const { return type == DmFieldType::Numeric; }
};

using DmValue = std::variant<double, std::string>;

struct DmHeader
{
    bool is64Bit = false;
    bool swapEndian = false;
    int wordSize = 4;
    int pageSizeBytes = 2048;
    int dataWordsPerPage = 508;
    std::string fileName;
    std::string dirName;
    std::string description;
    std::string owner;
    int ownerPerms = 0;
    int otherPerms = 0;
    int lastModDate = 0;
    int rawLogicalRecordWords = 0;
    int explicitLogicalRecordWords = 0;
    int nPhysicalPages = 0;
    int nLastPageRecords = 0;
    std::int64_t nRecords = 0;
    std::vector<DmField> fields;
    std::unordered_map<std::string, std::size_t> fieldIndex;

    bool hasField(const std::string &name) const
    {
        return fieldIndex.find(normalizeFieldName(name)) != fieldIndex.end();
    }

    const DmField &field(const std::string &name) const
    {
        auto it = fieldIndex.find(normalizeFieldName(name));
        if (it == fieldIndex.end()) {
            throw std::runtime_error("Missing Datamine field: " + name);
        }
        return fields[it->second];
    }
};

struct DmRecord
{
    const DmHeader *header = nullptr;
    std::vector<DmValue> values;

    const DmValue &value(const std::string &name) const
    {
        auto it = header->fieldIndex.find(normalizeFieldName(name));
        if (it == header->fieldIndex.end()) {
            throw std::runtime_error("Missing Datamine field in record: " + name);
        }
        return values[it->second];
    }

    double number(const std::string &name) const
    {
        const DmValue &v = value(name);
        if (!std::holds_alternative<double>(v)) {
            throw std::runtime_error("Field is not numeric: " + name);
        }
        return std::get<double>(v);
    }
};

class DmLegacyReader
{
public:
    explicit DmLegacyReader(fs::path path) : path_(std::move(path)) {}

    const DmHeader &readHeader()
    {
        if (headerRead_) {
            return header_;
        }

        std::ifstream in(path_, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open .dm file: " + path_.string());
        }

        std::vector<char> first4096(4096, 0);
        in.read(first4096.data(), static_cast<std::streamsize>(first4096.size()));
        if (in.gcount() < 2048) {
            throw std::runtime_error("File is too small to be a legacy Datamine .dm file.");
        }

        detectFormat(first4096);

        std::vector<char> headerPage(header_.pageSizeBytes, 0);
        std::copy(first4096.begin(), first4096.begin() + header_.pageSizeBytes, headerPage.begin());

        header_.fileName = trim(readChars(headerPage, 0, 2));
        header_.dirName = trim(readChars(headerPage, 2, 2));
        header_.description = trim(readChars(headerPage, 4, 16));
        header_.owner = trim(readChars(headerPage, 20, 2));
        header_.ownerPerms = static_cast<int>(std::llround(readNumber(headerPage, 22)));
        header_.otherPerms = static_cast<int>(std::llround(readNumber(headerPage, 23)));
        header_.lastModDate = static_cast<int>(std::llround(readNumber(headerPage, 24)));
        header_.rawLogicalRecordWords = static_cast<int>(std::llround(readNumber(headerPage, 25)));
        header_.nPhysicalPages = static_cast<int>(std::llround(readNumber(headerPage, 26)));
        header_.nLastPageRecords = static_cast<int>(std::llround(readNumber(headerPage, 27)));

        if (header_.rawLogicalRecordWords <= 0 || header_.rawLogicalRecordWords > 4096) {
            throw std::runtime_error("Invalid logical record length in .dm header.");
        }
        if (header_.nPhysicalPages < 1) {
            throw std::runtime_error("Invalid physical page count in .dm header.");
        }

        const int nVars = header_.rawLogicalRecordWords;
        header_.fields.reserve(static_cast<std::size_t>(nVars));
        int explicitWords = header_.rawLogicalRecordWords;

        for (int v = 0; v < nVars; ++v) {
            const int base = 28 + 7 * v;
            DmField f;
            f.name = normalizeFieldName(readChars(headerPage, base, 2));
            const std::string type = upper(trim(readChars(headerPage, base + 2, 1)));
            f.type = (type == "N") ? DmFieldType::Numeric : DmFieldType::Alpha;
            f.logicalRecPos = static_cast<int>(std::llround(readNumber(headerPage, base + 3)));
            f.wordNumber = static_cast<int>(std::llround(readNumber(headerPage, base + 4)));
            f.unit = trim(readChars(headerPage, base + 5, 1));
            if (f.type == DmFieldType::Numeric) {
                f.defaultNumeric = readNumber(headerPage, base + 6);
            } else {
                f.defaultAlpha = trim(readChars(headerPage, base + 6, 1));
            }
            if (f.isImplicit()) {
                --explicitWords;
            }
            if (!f.name.empty()) {
                header_.fieldIndex[f.name] = header_.fields.size();
            }
            header_.fields.push_back(std::move(f));
        }

        header_.explicitLogicalRecordWords = explicitWords;
        if (header_.explicitLogicalRecordWords < 0) {
            throw std::runtime_error("Invalid explicit logical record length after implicit fields.");
        }

        const int recordsPerPage = header_.explicitLogicalRecordWords > 0
            ? header_.dataWordsPerPage / header_.explicitLogicalRecordWords
            : 0;
        if (header_.explicitLogicalRecordWords > 0 && recordsPerPage <= 0) {
            throw std::runtime_error("Logical record is larger than a Datamine data page.");
        }

        const int fullDataPagesBeforeLast = std::max(0, header_.nPhysicalPages - 2);
        header_.nRecords = static_cast<std::int64_t>(fullDataPagesBeforeLast) * recordsPerPage
            + header_.nLastPageRecords;
        headerRead_ = true;
        return header_;
    }

    bool forEachRecord(const std::function<bool(const DmRecord &, std::int64_t)> &callback)
    {
        const DmHeader &h = readHeader();
        std::ifstream in(path_, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open .dm file: " + path_.string());
        }

        in.seekg(h.pageSizeBytes, std::ios::beg);
        const int recordsPerPage = h.explicitLogicalRecordWords > 0
            ? h.dataWordsPerPage / h.explicitLogicalRecordWords
            : 0;
        std::vector<char> page(static_cast<std::size_t>(h.pageSizeBytes), 0);
        std::int64_t rowIndex = 0;
        const int dataPages = std::max(0, h.nPhysicalPages - 1);

        for (int p = 0; p < dataPages; ++p) {
            std::fill(page.begin(), page.end(), 0);
            in.read(page.data(), static_cast<std::streamsize>(page.size()));
            const auto got = in.gcount();
            if (got <= 0) {
                break;
            }
            if (got < h.pageSizeBytes && p != dataPages - 1) {
                throw std::runtime_error("Unexpected end of file while reading .dm data pages.");
            }

            int rowsThisPage = recordsPerPage;
            if (p == dataPages - 1) {
                rowsThisPage = h.nLastPageRecords;
            }

            for (int r = 0; r < rowsThisPage; ++r) {
                DmRecord rec;
                rec.header = &h;
                rec.values.resize(h.fields.size());

                for (std::size_t i = 0; i < h.fields.size(); ++i) {
                    const DmField &f = h.fields[i];
                    if (f.isImplicit()) {
                        rec.values[i] = f.isNumeric() ? DmValue(f.defaultNumeric) : DmValue(f.defaultAlpha);
                        continue;
                    }
                    const int wordOffset = r * h.explicitLogicalRecordWords + (f.logicalRecPos - 1);
                    if (wordOffset < 0 || wordOffset >= h.dataWordsPerPage) {
                        throw std::runtime_error("Field logical record position is outside data page: " + f.name);
                    }
                    if (f.isNumeric()) {
                        rec.values[i] = readNumber(page, wordOffset);
                    } else {
                        rec.values[i] = trim(readChars(page, wordOffset, 1));
                    }
                }

                if (!callback(rec, rowIndex++)) {
                    return false;
                }
            }
        }
        return true;
    }

private:
    fs::path path_;
    DmHeader header_;
    bool headerRead_ = false;

    static bool almost(double a, double b, double eps = 1e-6)
    {
        return std::abs(a - b) <= eps;
    }

    template <typename T>
    T readPrimitive(const std::vector<char> &buffer, std::size_t offset, bool swap) const
    {
        if (offset + sizeof(T) > buffer.size()) {
            throw std::runtime_error("Attempted to read outside buffer.");
        }
        T v{};
        std::memcpy(&v, buffer.data() + offset, sizeof(T));
        return swap ? byteSwap(v) : v;
    }

    double readDoubleRaw(const std::vector<char> &buffer, int wordIndex, bool swap) const
    {
        return readPrimitive<double>(buffer, static_cast<std::size_t>(wordIndex) * 8, swap);
    }

    float readFloatRaw(const std::vector<char> &buffer, int wordIndex, bool swap) const
    {
        return readPrimitive<float>(buffer, static_cast<std::size_t>(wordIndex) * 4, swap);
    }

    static bool plausibleDate(double x)
    {
        return x >= 720101.0 && x <= 99991231.0;
    }

    static bool plausibleRecordWords(double x)
    {
        return x > 0.0 && x < 4096.0 && std::abs(x - std::round(x)) < 1e-3;
    }

    void detectFormat(const std::vector<char> &first4096)
    {
        const double markerNoSwap = readDoubleRaw(first4096, 24, false);
        const double markerSwap = readDoubleRaw(first4096, 24, true);
        if (almost(markerNoSwap, 456789.0)) {
            header_.is64Bit = true;
            header_.swapEndian = false;
        } else if (almost(markerSwap, 456789.0)) {
            header_.is64Bit = true;
            header_.swapEndian = true;
        } else {
            header_.is64Bit = false;
            const float dateNoSwap = readFloatRaw(first4096, 24, false);
            const float dateSwap = readFloatRaw(first4096, 24, true);
            const float lrlNoSwap = readFloatRaw(first4096, 25, false);
            const float lrlSwap = readFloatRaw(first4096, 25, true);
            const bool noSwapOK = plausibleDate(dateNoSwap) && plausibleRecordWords(lrlNoSwap);
            const bool swapOK = plausibleDate(dateSwap) && plausibleRecordWords(lrlSwap);
            header_.swapEndian = (!noSwapOK && swapOK);
        }
        header_.wordSize = header_.is64Bit ? 8 : 4;
        header_.pageSizeBytes = header_.is64Bit ? 4096 : 2048;
        header_.dataWordsPerPage = 508;
    }

    double readNumber(const std::vector<char> &page, int wordIndex) const
    {
        return header_.is64Bit
            ? readPrimitive<double>(page, static_cast<std::size_t>(wordIndex) * 8, header_.swapEndian)
            : static_cast<double>(readPrimitive<float>(page, static_cast<std::size_t>(wordIndex) * 4, header_.swapEndian));
    }

    std::string readChars(const std::vector<char> &page, int wordStart, int wordCount) const
    {
        std::string out;
        out.reserve(static_cast<std::size_t>(wordCount) * 4);
        for (int w = 0; w < wordCount; ++w) {
            const std::size_t offset = static_cast<std::size_t>(wordStart + w) * header_.wordSize;
            if (offset + 4 > page.size()) {
                break;
            }
            out.append(page.data() + offset, page.data() + offset + 4);
        }
        return out;
    }
};

struct RotationInfo
{
    bool isRotated = false;
    double angle1 = 0.0;
    double angle2 = 0.0;
    double angle3 = 0.0;
    double rotAxis1 = 0.0;
    double rotAxis2 = 0.0;
    double rotAxis3 = 0.0;
    double x0 = 0.0;
    double y0 = 0.0;
    double z0 = 0.0;
};

struct BlockModelPrototype
{
    double xOrigin = 0.0;
    double yOrigin = 0.0;
    double zOrigin = 0.0;
    int nx = 0;
    int ny = 0;
    int nz = 0;
    double parentX = 0.0;
    double parentY = 0.0;
    double parentZ = 0.0;
    RotationInfo rotation;
};

struct BlockCell
{
    std::int64_t ijk = 0;
    int i = 0;
    int j = 0;
    int k = 0;
    double xc = 0.0;
    double yc = 0.0;
    double zc = 0.0;
    double xinc = 0.0;
    double yinc = 0.0;
    double zinc = 0.0;
    double volume = 0.0;
    bool isSubcell = false;
    std::unordered_map<std::string, double> numericAttributes;
    std::unordered_map<std::string, std::string> alphaAttributes;
};

struct BlockModel
{
    DmHeader sourceHeader;
    BlockModelPrototype prototype;
    bool hasSubcells = false;
    bool isSortedByIJK = true;
    std::vector<BlockCell> cells;
};

struct InternalBlockModelInfo
{
    DmHeader sourceHeader;
    BlockModelPrototype prototype;
    bool hasSubcells = false;
    bool isSortedByIJK = true;
    std::int64_t cellCount = 0;
    fs::path internalPath;
};

inline std::array<int, 3> ijkToParentIndices(std::int64_t ijk, int nx, int ny, int nz)
{
    if (ijk < 0) {
        return {0, 0, 0};
    }
    const auto denomI = static_cast<std::int64_t>(ny) * static_cast<std::int64_t>(nz);
    int i = denomI > 0 ? static_cast<int>(ijk / denomI) : 0;
    const auto rem = denomI > 0 ? ijk % denomI : 0;
    int j = nz > 0 ? static_cast<int>(rem / nz) : 0;
    int k = nz > 0 ? static_cast<int>(rem % nz) : 0;
    (void)nx;
    return {i, j, k};
}

class DmBlockModelImporter
{
public:
    using ProgressCallback = std::function<bool(std::int64_t, std::int64_t)>;

    static BlockModel load(const fs::path &path, ProgressCallback progress = {})
    {
        DmLegacyReader reader(path);
        const DmHeader &h = reader.readHeader();
        validateRequiredBlockFields(h);

        BlockModel model;
        model.sourceHeader = h;
        model.cells.reserve(static_cast<std::size_t>(std::min<std::int64_t>(h.nRecords, 1'000'000)));

        std::optional<std::int64_t> previousIJK;
        std::unordered_set<std::int64_t> seenIJK;

        const bool completed = reader.forEachRecord([&](const DmRecord &rec, std::int64_t rowIndex) {
            if (progress && rowIndex % 1000 == 0 && !progress(rowIndex, h.nRecords)) {
                return false;
            }
            if (rowIndex == 0) {
                model.prototype = prototypeFromRecord(rec, h);
            }

            BlockCell cell;
            cell.ijk = static_cast<std::int64_t>(std::llround(rec.number("IJK")));
            const auto idx = ijkToParentIndices(cell.ijk, model.prototype.nx, model.prototype.ny, model.prototype.nz);
            cell.i = idx[0];
            cell.j = idx[1];
            cell.k = idx[2];
            cell.xc = rec.number("XC");
            cell.yc = rec.number("YC");
            cell.zc = rec.number("ZC");
            cell.xinc = rec.number("XINC");
            cell.yinc = rec.number("YINC");
            cell.zinc = rec.number("ZINC");
            cell.volume = cell.xinc * cell.yinc * cell.zinc;

            auto different = [](double a, double b) {
                const double scale = std::max({1.0, std::abs(a), std::abs(b)});
                return std::abs(a - b) > 1e-9 * scale;
            };
            cell.isSubcell = different(cell.xinc, model.prototype.parentX)
                || different(cell.yinc, model.prototype.parentY)
                || different(cell.zinc, model.prototype.parentZ);

            if (cell.isSubcell || seenIJK.find(cell.ijk) != seenIJK.end()) {
                model.hasSubcells = true;
            }
            seenIJK.insert(cell.ijk);
            if (previousIJK && cell.ijk < *previousIJK) {
                model.isSortedByIJK = false;
            }
            previousIJK = cell.ijk;
            fillAttributes(rec, h, cell);
            model.cells.push_back(std::move(cell));
            return true;
        });

        if (!completed) {
            throw std::runtime_error("Datamine block model import was cancelled.");
        }
        if (progress) {
            progress(static_cast<std::int64_t>(model.cells.size()), h.nRecords);
        }
        return model;
    }

    static InternalBlockModelInfo importToInternalFile(
        const fs::path &dmPath,
        const fs::path &outPath,
        ProgressCallback progress = {})
    {
        DmLegacyReader reader(dmPath);
        const DmHeader &h = reader.readHeader();
        validateRequiredBlockFields(h);

        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Cannot write internal block model file: " + outPath.string());
        }

        InternalBlockModelInfo info;
        info.sourceHeader = h;
        info.internalPath = outPath;

        const char magic[8] = {'G', 'V', 'B', 'M', '0', '0', '0', '1'};
        out.write(magic, sizeof(magic));
        writePod(out, static_cast<std::uint32_t>(1));

        const std::streampos cellCountPos = out.tellp();
        writePod(out, static_cast<std::int64_t>(0));
        const std::streampos flagsPos = out.tellp();
        writePod(out, static_cast<std::uint8_t>(0));
        writePod(out, static_cast<std::uint8_t>(1));

        const std::streampos prototypePos = out.tellp();
        writePrototype(out, info.prototype);

        writeString(out, h.description);
        writeString(out, dmPath.filename().string());

        std::vector<const DmField *> storedFields;
        for (const DmField &field : h.fields) {
            if (!isStandardBlockField(field.name)) {
                storedFields.push_back(&field);
            }
        }
        writePod(out, static_cast<std::uint32_t>(storedFields.size()));
        for (const DmField *field : storedFields) {
            writeString(out, field->name);
            writePod(out, static_cast<std::uint8_t>(field->isNumeric() ? 1 : 0));
        }

        std::optional<std::int64_t> previousIJK;
        std::unordered_set<std::int64_t> seenIJK;

        const bool completed = reader.forEachRecord([&](const DmRecord &rec, std::int64_t rowIndex) {
            if (progress && rowIndex % 1000 == 0 && !progress(rowIndex, h.nRecords)) {
                return false;
            }
            if (rowIndex == 0) {
                info.prototype = prototypeFromRecord(rec, h);
                const std::streampos current = out.tellp();
                out.seekp(prototypePos);
                writePrototype(out, info.prototype);
                out.seekp(current);
            }

            BlockCell cell = cellFromRecord(rec, h, info.prototype);
            if (cell.isSubcell || seenIJK.find(cell.ijk) != seenIJK.end()) {
                info.hasSubcells = true;
            }
            seenIJK.insert(cell.ijk);
            if (previousIJK && cell.ijk < *previousIJK) {
                info.isSortedByIJK = false;
            }
            previousIJK = cell.ijk;

            writeBaseCell(out, cell);
            for (const DmField *field : storedFields) {
                const DmValue &value = rec.values[h.fieldIndex.at(field->name)];
                if (field->isNumeric()) {
                    writePod(out, std::get<double>(value));
                } else {
                    writeString(out, std::get<std::string>(value));
                }
            }
            ++info.cellCount;
            return true;
        });

        if (!completed) {
            out.close();
            std::error_code ec;
            fs::remove(outPath, ec);
            throw std::runtime_error("Datamine block model import was cancelled.");
        }

        out.seekp(cellCountPos);
        writePod(out, info.cellCount);
        out.seekp(flagsPos);
        writePod(out, static_cast<std::uint8_t>(info.hasSubcells ? 1 : 0));
        writePod(out, static_cast<std::uint8_t>(info.isSortedByIJK ? 1 : 0));
        out.close();

        if (progress) {
            progress(info.cellCount, h.nRecords);
        }
        return info;
    }

    static InternalBlockModelInfo writeInternalFile(
        const fs::path &outPath,
        const BlockModelPrototype &prototype,
        const std::string &description,
        const std::string &fileName,
        const std::vector<DmField> &storedFields,
        const std::vector<BlockCell> &cells,
        ProgressCallback progress = {})
    {
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Cannot write internal block model file: " + outPath.string());
        }

        InternalBlockModelInfo info;
        info.prototype = prototype;
        info.internalPath = outPath;
        info.cellCount = static_cast<std::int64_t>(cells.size());
        info.hasSubcells = false;
        info.isSortedByIJK = true;
        info.sourceHeader.description = description;
        info.sourceHeader.fileName = fileName;
        info.sourceHeader.nRecords = info.cellCount;
        info.sourceHeader.fields = storedFields;
        for (std::size_t i = 0; i < info.sourceHeader.fields.size(); ++i) {
            DmField &field = info.sourceHeader.fields[i];
            field.name = normalizeFieldName(field.name);
            info.sourceHeader.fieldIndex[field.name] = i;
        }

        const char magic[8] = {'G', 'V', 'B', 'M', '0', '0', '0', '1'};
        out.write(magic, sizeof(magic));
        writePod(out, static_cast<std::uint32_t>(1));
        writePod(out, info.cellCount);
        writePod(out, static_cast<std::uint8_t>(0));
        writePod(out, static_cast<std::uint8_t>(1));
        writePrototype(out, prototype);
        writeString(out, description);
        writeString(out, fileName);
        writePod(out, static_cast<std::uint32_t>(info.sourceHeader.fields.size()));
        for (const DmField &field : info.sourceHeader.fields) {
            writeString(out, field.name);
            writePod(out, static_cast<std::uint8_t>(field.isNumeric() ? 1 : 0));
        }

        std::optional<std::int64_t> previousIJK;
        for (std::int64_t row = 0; row < static_cast<std::int64_t>(cells.size()); ++row) {
            if (progress && row % 1000 == 0 && !progress(row, info.cellCount)) {
                out.close();
                std::error_code ec;
                fs::remove(outPath, ec);
                throw std::runtime_error("Internal block model write was cancelled.");
            }
            const BlockCell &cell = cells[static_cast<std::size_t>(row)];
            if (cell.isSubcell) {
                info.hasSubcells = true;
            }
            if (previousIJK && cell.ijk < *previousIJK) {
                info.isSortedByIJK = false;
            }
            previousIJK = cell.ijk;
            writeBaseCell(out, cell);
            for (const DmField &field : info.sourceHeader.fields) {
                const std::string fieldName = normalizeFieldName(field.name);
                if (field.isNumeric()) {
                    const auto it = cell.numericAttributes.find(fieldName);
                    writePod(out, it == cell.numericAttributes.end() ? 0.0 : it->second);
                } else {
                    const auto it = cell.alphaAttributes.find(fieldName);
                    writeString(out, it == cell.alphaAttributes.end() ? std::string() : it->second);
                }
            }
        }

        out.close();
        if (progress) {
            progress(info.cellCount, info.cellCount);
        }
        return info;
    }

    static InternalBlockModelInfo readInternalInfo(const fs::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open internal block model file: " + path.string());
        }
        char magic[8] = {};
        in.read(magic, sizeof(magic));
        if (std::string(magic, magic + 8) != std::string("GVBM0001", 8)) {
            throw std::runtime_error("Invalid internal block model file.");
        }
        const std::uint32_t version = readPod<std::uint32_t>(in);
        if (version != 1) {
            throw std::runtime_error("Unsupported internal block model version.");
        }

        InternalBlockModelInfo info;
        info.internalPath = path;
        info.cellCount = readPod<std::int64_t>(in);
        info.hasSubcells = readPod<std::uint8_t>(in) != 0;
        info.isSortedByIJK = readPod<std::uint8_t>(in) != 0;
        info.prototype = readPrototype(in);
        info.sourceHeader.description = readString(in);
        info.sourceHeader.fileName = readString(in);

        const std::uint32_t fieldCount = readPod<std::uint32_t>(in);
        info.sourceHeader.fields.reserve(fieldCount);
        for (std::uint32_t i = 0; i < fieldCount; ++i) {
            DmField field;
            field.name = readString(in);
            field.type = readPod<std::uint8_t>(in) != 0 ? DmFieldType::Numeric : DmFieldType::Alpha;
            info.sourceHeader.fieldIndex[field.name] = info.sourceHeader.fields.size();
            info.sourceHeader.fields.push_back(std::move(field));
        }
        return info;
    }

    static bool forEachInternalBaseCell(
        const fs::path &path,
        const std::function<bool(const BlockCell &, std::int64_t, std::int64_t)> &callback,
        std::optional<std::int64_t> maxCells = std::nullopt)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open internal block model file: " + path.string());
        }
        char magic[8] = {};
        in.read(magic, sizeof(magic));
        if (std::string(magic, magic + 8) != std::string("GVBM0001", 8)) {
            throw std::runtime_error("Invalid internal block model file.");
        }
        const std::uint32_t version = readPod<std::uint32_t>(in);
        if (version != 1) {
            throw std::runtime_error("Unsupported internal block model version.");
        }

        const std::int64_t cellCount = readPod<std::int64_t>(in);
        readPod<std::uint8_t>(in);
        readPod<std::uint8_t>(in);
        readPrototype(in);
        readString(in);
        readString(in);

        struct StoredField
        {
            bool numeric = true;
        };
        std::vector<StoredField> fields;
        const std::uint32_t fieldCount = readPod<std::uint32_t>(in);
        fields.reserve(fieldCount);
        for (std::uint32_t i = 0; i < fieldCount; ++i) {
            readString(in);
            fields.push_back({readPod<std::uint8_t>(in) != 0});
        }

        const std::int64_t limit = maxCells ? std::min(*maxCells, cellCount) : cellCount;
        for (std::int64_t row = 0; row < limit; ++row) {
            BlockCell cell = readBaseCell(in);
            for (const StoredField &field : fields) {
                if (field.numeric) {
                    readPod<double>(in);
                } else {
                    readString(in);
                }
            }
            if (!callback(cell, row, cellCount)) {
                return false;
            }
        }
        return true;
    }

    static bool forEachInternalCellValue(
        const fs::path &path,
        const std::string &valueFieldName,
        const std::function<bool(const BlockCell &, double, bool, std::int64_t, std::int64_t)> &callback,
        std::optional<std::int64_t> maxCells = std::nullopt,
        bool includeAttributes = false)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open internal block model file: " + path.string());
        }
        char magic[8] = {};
        in.read(magic, sizeof(magic));
        if (std::string(magic, magic + 8) != std::string("GVBM0001", 8)) {
            throw std::runtime_error("Invalid internal block model file.");
        }
        const std::uint32_t version = readPod<std::uint32_t>(in);
        if (version != 1) {
            throw std::runtime_error("Unsupported internal block model version.");
        }

        const std::int64_t cellCount = readPod<std::int64_t>(in);
        readPod<std::uint8_t>(in);
        readPod<std::uint8_t>(in);
        readPrototype(in);
        readString(in);
        readString(in);

        struct StoredField
        {
            std::string name;
            bool numeric = true;
        };
        std::vector<StoredField> fields;
        const std::uint32_t fieldCount = readPod<std::uint32_t>(in);
        fields.reserve(fieldCount);
        for (std::uint32_t i = 0; i < fieldCount; ++i) {
            StoredField field;
            field.name = normalizeFieldName(readString(in));
            field.numeric = readPod<std::uint8_t>(in) != 0;
            fields.push_back(std::move(field));
        }

        const std::string target = normalizeFieldName(valueFieldName);
        const std::int64_t limit = maxCells ? std::min(*maxCells, cellCount) : cellCount;
        for (std::int64_t row = 0; row < limit; ++row) {
            BlockCell cell = readBaseCell(in);
            double value = standardCellValue(cell, target);
            bool hasValue = !target.empty() && !std::isnan(value);

            for (const StoredField &field : fields) {
                if (field.numeric) {
                    const double attr = readPod<double>(in);
                    if (includeAttributes) {
                        cell.numericAttributes[field.name] = attr;
                    }
                    if (!hasValue && field.name == target) {
                        value = attr;
                        hasValue = true;
                    }
                } else {
                    const std::string attr = readString(in);
                    if (includeAttributes) {
                        cell.alphaAttributes[field.name] = attr;
                    }
                }
            }

            if (!callback(cell, value, hasValue, row, cellCount)) {
                return false;
            }
        }
        return true;
    }

    static std::optional<BlockCell> readInternalCellAt(const fs::path &path, std::int64_t targetRow)
    {
        if (targetRow < 0) {
            return std::nullopt;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Cannot open internal block model file: " + path.string());
        }
        char magic[8] = {};
        in.read(magic, sizeof(magic));
        if (std::string(magic, magic + 8) != std::string("GVBM0001", 8)) {
            throw std::runtime_error("Invalid internal block model file.");
        }
        const std::uint32_t version = readPod<std::uint32_t>(in);
        if (version != 1) {
            throw std::runtime_error("Unsupported internal block model version.");
        }

        const std::int64_t cellCount = readPod<std::int64_t>(in);
        if (targetRow >= cellCount) {
            return std::nullopt;
        }
        readPod<std::uint8_t>(in);
        readPod<std::uint8_t>(in);
        readPrototype(in);
        readString(in);
        readString(in);

        struct StoredField
        {
            std::string name;
            bool numeric = true;
        };
        std::vector<StoredField> fields;
        const std::uint32_t fieldCount = readPod<std::uint32_t>(in);
        fields.reserve(fieldCount);
        for (std::uint32_t i = 0; i < fieldCount; ++i) {
            StoredField field;
            field.name = readString(in);
            field.numeric = readPod<std::uint8_t>(in) != 0;
            fields.push_back(std::move(field));
        }

        for (std::int64_t row = 0; row <= targetRow; ++row) {
            BlockCell cell = readBaseCell(in);
            for (const StoredField &field : fields) {
                if (field.numeric) {
                    const double value = readPod<double>(in);
                    if (row == targetRow) {
                        cell.numericAttributes[field.name] = value;
                    }
                } else {
                    const std::string value = readString(in);
                    if (row == targetRow) {
                        cell.alphaAttributes[field.name] = value;
                    }
                }
            }
            if (row == targetRow) {
                return cell;
            }
        }
        return std::nullopt;
    }

private:
    static void validateRequiredBlockFields(const DmHeader &h)
    {
        const std::vector<std::string> required = {
            "IJK", "XC", "YC", "ZC", "XINC", "YINC", "ZINC",
            "XMORIG", "YMORIG", "ZMORIG", "NX", "NY", "NZ"};
        for (const auto &name : required) {
            if (!h.hasField(name)) {
                throw std::runtime_error("Not a valid Datamine block model: missing field " + name);
            }
            if (!h.field(name).isNumeric()) {
                throw std::runtime_error("Invalid Datamine block model: field must be numeric: " + name);
            }
        }
    }

    static BlockModelPrototype prototypeFromRecord(const DmRecord &rec, const DmHeader &h)
    {
        BlockModelPrototype p;
        p.xOrigin = rec.number("XMORIG");
        p.yOrigin = rec.number("YMORIG");
        p.zOrigin = rec.number("ZMORIG");
        p.nx = static_cast<int>(std::llround(rec.number("NX")));
        p.ny = static_cast<int>(std::llround(rec.number("NY")));
        p.nz = static_cast<int>(std::llround(rec.number("NZ")));
        p.parentX = parentSizeFromField(h.field("XINC"), rec.number("XINC"));
        p.parentY = parentSizeFromField(h.field("YINC"), rec.number("YINC"));
        p.parentZ = parentSizeFromField(h.field("ZINC"), rec.number("ZINC"));

        const bool hasRotationFields = h.hasField("ANGLE1") && h.hasField("ANGLE2") && h.hasField("ANGLE3")
            && h.hasField("ROTAXIS1") && h.hasField("ROTAXIS2") && h.hasField("ROTAXIS3")
            && h.hasField("X0") && h.hasField("Y0") && h.hasField("Z0");
        if (hasRotationFields) {
            p.rotation.isRotated = true;
            p.rotation.angle1 = rec.number("ANGLE1");
            p.rotation.angle2 = rec.number("ANGLE2");
            p.rotation.angle3 = rec.number("ANGLE3");
            p.rotation.rotAxis1 = rec.number("ROTAXIS1");
            p.rotation.rotAxis2 = rec.number("ROTAXIS2");
            p.rotation.rotAxis3 = rec.number("ROTAXIS3");
            p.rotation.x0 = rec.number("X0");
            p.rotation.y0 = rec.number("Y0");
            p.rotation.z0 = rec.number("Z0");
        }
        return p;
    }

    static double parentSizeFromField(const DmField &f, double recordValue)
    {
        return std::abs(f.defaultNumeric) > 0.0 ? f.defaultNumeric : recordValue;
    }

    static bool isStandardBlockField(const std::string &field)
    {
        static const std::unordered_set<std::string> standard = {
            "IJK", "XC", "YC", "ZC", "XINC", "YINC", "ZINC",
            "XMORIG", "YMORIG", "ZMORIG", "NX", "NY", "NZ",
            "ANGLE1", "ANGLE2", "ANGLE3", "ROTAXIS1", "ROTAXIS2", "ROTAXIS3",
            "X0", "Y0", "Z0"};
        return standard.find(normalizeFieldName(field)) != standard.end();
    }

    static void fillAttributes(const DmRecord &rec, const DmHeader &h, BlockCell &cell)
    {
        for (std::size_t i = 0; i < h.fields.size(); ++i) {
            const auto &f = h.fields[i];
            if (isStandardBlockField(f.name)) {
                continue;
            }
            if (f.isNumeric()) {
                cell.numericAttributes[f.name] = std::get<double>(rec.values[i]);
            } else {
                cell.alphaAttributes[f.name] = std::get<std::string>(rec.values[i]);
            }
        }
    }

    static BlockCell cellFromRecord(const DmRecord &rec, const DmHeader &h, const BlockModelPrototype &prototype)
    {
        BlockCell cell;
        cell.ijk = static_cast<std::int64_t>(std::llround(rec.number("IJK")));
        const auto idx = ijkToParentIndices(cell.ijk, prototype.nx, prototype.ny, prototype.nz);
        cell.i = idx[0];
        cell.j = idx[1];
        cell.k = idx[2];
        cell.xc = rec.number("XC");
        cell.yc = rec.number("YC");
        cell.zc = rec.number("ZC");
        cell.xinc = rec.number("XINC");
        cell.yinc = rec.number("YINC");
        cell.zinc = rec.number("ZINC");
        cell.volume = cell.xinc * cell.yinc * cell.zinc;

        auto different = [](double a, double b) {
            const double scale = std::max({1.0, std::abs(a), std::abs(b)});
            return std::abs(a - b) > 1e-9 * scale;
        };
        cell.isSubcell = different(cell.xinc, prototype.parentX)
            || different(cell.yinc, prototype.parentY)
            || different(cell.zinc, prototype.parentZ);
        return cell;
    }

    template <typename T>
    static void writePod(std::ofstream &out, const T &value)
    {
        out.write(reinterpret_cast<const char *>(&value), sizeof(T));
        if (!out) {
            throw std::runtime_error("Unable to write internal block model file.");
        }
    }

    template <typename T>
    static T readPod(std::ifstream &in)
    {
        T value{};
        in.read(reinterpret_cast<char *>(&value), sizeof(T));
        if (!in) {
            throw std::runtime_error("Unable to read internal block model file.");
        }
        return value;
    }

    static void writeString(std::ofstream &out, const std::string &value)
    {
        writePod(out, static_cast<std::uint32_t>(value.size()));
        if (!value.empty()) {
            out.write(value.data(), static_cast<std::streamsize>(value.size()));
            if (!out) {
                throw std::runtime_error("Unable to write internal block model file.");
            }
        }
    }

    static std::string readString(std::ifstream &in)
    {
        const std::uint32_t size = readPod<std::uint32_t>(in);
        std::string value(size, '\0');
        if (size > 0) {
            in.read(value.data(), static_cast<std::streamsize>(size));
            if (!in) {
                throw std::runtime_error("Unable to read internal block model file.");
            }
        }
        return value;
    }

    static void writePrototype(std::ofstream &out, const BlockModelPrototype &prototype)
    {
        writePod(out, prototype.xOrigin);
        writePod(out, prototype.yOrigin);
        writePod(out, prototype.zOrigin);
        writePod(out, prototype.nx);
        writePod(out, prototype.ny);
        writePod(out, prototype.nz);
        writePod(out, prototype.parentX);
        writePod(out, prototype.parentY);
        writePod(out, prototype.parentZ);
        writePod(out, static_cast<std::uint8_t>(prototype.rotation.isRotated ? 1 : 0));
        writePod(out, prototype.rotation.angle1);
        writePod(out, prototype.rotation.angle2);
        writePod(out, prototype.rotation.angle3);
        writePod(out, prototype.rotation.rotAxis1);
        writePod(out, prototype.rotation.rotAxis2);
        writePod(out, prototype.rotation.rotAxis3);
        writePod(out, prototype.rotation.x0);
        writePod(out, prototype.rotation.y0);
        writePod(out, prototype.rotation.z0);
    }

    static BlockModelPrototype readPrototype(std::ifstream &in)
    {
        BlockModelPrototype prototype;
        prototype.xOrigin = readPod<double>(in);
        prototype.yOrigin = readPod<double>(in);
        prototype.zOrigin = readPod<double>(in);
        prototype.nx = readPod<int>(in);
        prototype.ny = readPod<int>(in);
        prototype.nz = readPod<int>(in);
        prototype.parentX = readPod<double>(in);
        prototype.parentY = readPod<double>(in);
        prototype.parentZ = readPod<double>(in);
        prototype.rotation.isRotated = readPod<std::uint8_t>(in) != 0;
        prototype.rotation.angle1 = readPod<double>(in);
        prototype.rotation.angle2 = readPod<double>(in);
        prototype.rotation.angle3 = readPod<double>(in);
        prototype.rotation.rotAxis1 = readPod<double>(in);
        prototype.rotation.rotAxis2 = readPod<double>(in);
        prototype.rotation.rotAxis3 = readPod<double>(in);
        prototype.rotation.x0 = readPod<double>(in);
        prototype.rotation.y0 = readPod<double>(in);
        prototype.rotation.z0 = readPod<double>(in);
        return prototype;
    }

    static void writeBaseCell(std::ofstream &out, const BlockCell &cell)
    {
        writePod(out, cell.ijk);
        writePod(out, cell.i);
        writePod(out, cell.j);
        writePod(out, cell.k);
        writePod(out, cell.xc);
        writePod(out, cell.yc);
        writePod(out, cell.zc);
        writePod(out, cell.xinc);
        writePod(out, cell.yinc);
        writePod(out, cell.zinc);
        writePod(out, cell.volume);
        writePod(out, static_cast<std::uint8_t>(cell.isSubcell ? 1 : 0));
    }

    static BlockCell readBaseCell(std::ifstream &in)
    {
        BlockCell cell;
        cell.ijk = readPod<std::int64_t>(in);
        cell.i = readPod<int>(in);
        cell.j = readPod<int>(in);
        cell.k = readPod<int>(in);
        cell.xc = readPod<double>(in);
        cell.yc = readPod<double>(in);
        cell.zc = readPod<double>(in);
        cell.xinc = readPod<double>(in);
        cell.yinc = readPod<double>(in);
        cell.zinc = readPod<double>(in);
        cell.volume = readPod<double>(in);
        cell.isSubcell = readPod<std::uint8_t>(in) != 0;
        return cell;
    }

    static double standardCellValue(const BlockCell &cell, const std::string &fieldName)
    {
        if (fieldName == "IJK") {
            return static_cast<double>(cell.ijk);
        }
        if (fieldName == "I") {
            return static_cast<double>(cell.i);
        }
        if (fieldName == "J") {
            return static_cast<double>(cell.j);
        }
        if (fieldName == "K") {
            return static_cast<double>(cell.k);
        }
        if (fieldName == "XC") {
            return cell.xc;
        }
        if (fieldName == "YC") {
            return cell.yc;
        }
        if (fieldName == "ZC") {
            return cell.zc;
        }
        if (fieldName == "XINC") {
            return cell.xinc;
        }
        if (fieldName == "YINC") {
            return cell.yinc;
        }
        if (fieldName == "ZINC") {
            return cell.zinc;
        }
        if (fieldName == "VOLUME") {
            return cell.volume;
        }
        if (fieldName == "IS_SUBCELL") {
            return cell.isSubcell ? 1.0 : 0.0;
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
};

} // namespace visor::datamine
