#include "xlsx_format.hpp"

#include "quickxlsx/errors.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace quickxlsx::detail::xlsx_internal {
namespace {

constexpr std::string_view office_document_relationship =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument";
constexpr std::string_view worksheet_relationship =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet";
constexpr std::string_view shared_strings_relationship =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings";


bool relationship_type_is(std::string_view type, std::string_view transitional,
                          std::string_view terminal) {
    if (type == transitional) return true;
    const std::size_t slash = type.rfind('/');
    return slash != std::string_view::npos && type.substr(slash + 1) == terminal;
}
std::string_view local_name(const char* name) {
    const std::string_view value(name == nullptr ? "" : name);
    const std::size_t colon = value.find(':');
    return colon == std::string_view::npos ? value : value.substr(colon + 1);
}

pugi::xml_document parse_xml(std::string_view xml, std::string_view part) {
    pugi::xml_document document;
    const pugi::xml_parse_result result = document.load_buffer(xml.data(), xml.size(),
        pugi::parse_default | pugi::parse_ws_pcdata, pugi::encoding_utf8);
    if (!result) {
        throw errors::XLSXParseError("malformed XML in XLSX part '" + std::string(part) +
            "' at byte " + std::to_string(result.offset) + ": " + result.description(),
            std::string(part), 0, result.offset < 0 ? 0 : static_cast<std::size_t>(result.offset));
    }
    return document;
}

pugi::xml_node child_named(pugi::xml_node parent, std::string_view wanted) {
    for (const auto child : parent.children())
        if (local_name(child.name()) == wanted) return child;
    return {};
}

std::string attribute(pugi::xml_node node, std::string_view wanted) {
    for (const auto item : node.attributes())
        if (local_name(item.name()) == wanted) return item.value();
    return {};
}

void append_text_nodes(pugi::xml_node parent, std::string& text) {
    for (const auto child : parent.children()) {
        if (local_name(child.name()) == "t") text += child.child_value();
        else append_text_nodes(child, text);
    }
}

std::string normalize_part(std::string_view base_part, std::string_view target) {
    if (target.empty()) return {};
    std::string clean(target);
    std::replace(clean.begin(), clean.end(), '\\', '/');
    if (clean.front() == '/') clean.erase(clean.begin());
    else clean = (std::filesystem::path(base_part).parent_path() / clean).generic_string();

    std::vector<std::string> segments;
    std::size_t begin = 0;
    while (begin <= clean.size()) {
        const std::size_t end = clean.find('/', begin);
        const std::string segment = clean.substr(begin, end == std::string::npos ? clean.size() - begin : end - begin);
        if (segment == "..") {
            if (segments.empty()) throw errors::FileCorrupted("OOXML relationship escapes package root: " + std::string(target));
            segments.pop_back();
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    std::string result;
    for (const auto& segment : segments) {
        if (!result.empty()) result.push_back('/');
        result += segment;
    }
    return result;
}

std::string relationships_part(std::string_view part) {
    const std::filesystem::path path(part);
    return (path.parent_path() / "_rels" / (path.filename().string() + ".rels")).generic_string();
}

struct Relationship {
    std::string type;
    std::string target;
    bool external = false;
};

std::unordered_map<std::string, Relationship> read_relationships(ZipReader& archive,
                                                                 std::string_view source_part,
                                                                 std::string_view rels_part) {
    const std::string xml = archive.read(rels_part);
    const pugi::xml_document document = parse_xml(xml, rels_part);
    const pugi::xml_node root = document.document_element();
    if (!root || local_name(root.name()) != "Relationships")
        throw errors::MissingRequiredElement("XLSX relationships part '" + std::string(rels_part) +
                                             "' has no Relationships root");
    std::unordered_map<std::string, Relationship> result;
    for (const auto node : root.children()) {
        if (local_name(node.name()) != "Relationship") continue;
        const std::string id = attribute(node, "Id");
        const std::string type = attribute(node, "Type");
        const std::string target = attribute(node, "Target");
        if (id.empty() || type.empty() || target.empty())
            throw errors::MissingRequiredElement("incomplete Relationship in XLSX part '" + std::string(rels_part) + "'");
        const bool external = attribute(node, "TargetMode") == "External";
        auto [position, inserted] = result.emplace(id, Relationship{type,
            external ? target : normalize_part(source_part, target), external});
        if (!inserted) throw errors::FileCorrupted("duplicate relationship Id '" + id + "' in '" + std::string(rels_part) + "'");
    }
    return result;
}

} // namespace

PackageIndex read_package_index(ZipReader& archive) {
    if (!archive.contains("[Content_Types].xml"))
        throw errors::MissingRequiredElement("XLSX package is missing required part '[Content_Types].xml'");
    const auto root_relationships = read_relationships(archive, "", "_rels/.rels");
    std::string workbook_part;
    for (const auto& [id, relationship] : root_relationships) {
        static_cast<void>(id);
        if (relationship_type_is(relationship.type, office_document_relationship, "officeDocument")) {
            if (relationship.external)
                throw errors::UnsupportedFeature("external OOXML officeDocument relationships are not supported");
            workbook_part = relationship.target;
            break;
        }
    }
    if (workbook_part.empty())
        throw errors::MissingRequiredElement("XLSX package has no officeDocument relationship");

    const std::string workbook_xml = archive.read(workbook_part);
    const pugi::xml_document workbook = parse_xml(workbook_xml, workbook_part);
    const pugi::xml_node root = workbook.document_element();
    if (!root || local_name(root.name()) != "workbook")
        throw errors::MissingRequiredElement("XLSX workbook part has no workbook root: " + workbook_part);
    const auto relationships = read_relationships(archive, workbook_part, relationships_part(workbook_part));
    PackageIndex index;
    for (const auto& [id, relationship] : relationships) {
        static_cast<void>(id);
        if (relationship_type_is(relationship.type, shared_strings_relationship, "sharedStrings")) {
            if (relationship.external)
                throw errors::UnsupportedFeature("external OOXML shared strings relationships are not supported");
            index.shared_strings_path = relationship.target;
        }
    }

    const pugi::xml_node sheets = child_named(root, "sheets");
    if (!sheets) throw errors::MissingRequiredElement("XLSX workbook has no sheets element: " + workbook_part);
    for (const auto sheet : sheets.children()) {
        if (local_name(sheet.name()) != "sheet") continue;
        const std::string name = attribute(sheet, "name");
        const std::string relationship_id = attribute(sheet, "id");
        if (name.empty() || relationship_id.empty())
            throw errors::MissingRequiredElement("XLSX sheet is missing name or relationship id in " + workbook_part);
        const auto found = relationships.find(relationship_id);
        if (found == relationships.end())
            throw errors::MissingRequiredElement("XLSX sheet '" + name + "' references missing relationship '" + relationship_id + "'");
        if (!relationship_type_is(found->second.type, worksheet_relationship, "worksheet"))
            throw errors::FileCorrupted("XLSX sheet '" + name + "' relationship does not target a worksheet");
        if (found->second.external)
            throw errors::UnsupportedFeature("external OOXML worksheet relationships are not supported: " + name);
        index.sheets.push_back({name, found->second.target});
    }
    if (index.sheets.empty()) throw errors::MissingRequiredElement("XLSX workbook contains no worksheets");
    return index;
}

std::vector<std::string> read_shared_strings(ZipReader& archive, std::string_view part_path) {
    if (part_path.empty()) return {};
    const std::string xml = archive.read(part_path);
    const pugi::xml_document document = parse_xml(xml, part_path);
    const pugi::xml_node root = document.document_element();
    if (!root || local_name(root.name()) != "sst")
        throw errors::MissingRequiredElement("XLSX shared strings part has no sst root: " + std::string(part_path));

    std::vector<std::string> result;
    for (const auto item : root.children()) {
        if (local_name(item.name()) != "si") continue;
        std::string text;
        append_text_nodes(item, text);
        result.push_back(std::move(text));
    }
    return result;
}

} // namespace quickxlsx::detail::xlsx_internal
