#ifndef PUNPUN_PROJECT_HPP
#define PUNPUN_PROJECT_HPP

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontend.hpp"

namespace ppproject {
namespace fs = std::filesystem;

struct Dependency {
    std::string alias;
    fs::path path;
};

struct Manifest {
    fs::path root;
    std::string name;
    std::string version = "0.1.0";
    std::string edition = "2026";
    fs::path entry = "src/main.pp";
    std::vector<Dependency> dependencies;
    std::string toolchain_profile;
    std::string toolchain_cc;
    std::string toolchain_cxx;
    std::string toolchain_linker;
};

struct PackageNode {
    Manifest manifest;
    std::vector<std::string> dependency_names;
    std::string checksum;
};

inline std::string trim(std::string value) {
    auto blank = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && blank(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && blank(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

inline std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

inline bool valid_name(const std::string &name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) return false;
    for (unsigned char c : name)
        if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
    return true;
}

inline std::string read_text(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Error("error: cannot read project file '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

inline Manifest parse_manifest(const fs::path &root) {
    const fs::path path = root / "Punpun.toml";
    std::ifstream input(path);
    if (!input) throw Error("error: cannot read package manifest '" + path.string() + "'");
    Manifest manifest;
    manifest.root = fs::absolute(root).lexically_normal();
    std::string section;
    std::string raw;
    int line_number = 0;
    while (std::getline(input, raw)) {
        ++line_number;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            throw Error("error: malformed Punpun.toml line " + std::to_string(line_number) + " in '" + path.string() + "'");
        const std::string key = trim(line.substr(0, equals));
        std::string value = trim(line.substr(equals + 1));
        if (section == "package") {
            value = unquote(value);
            if (key == "name") manifest.name = value;
            else if (key == "version") manifest.version = value;
            else if (key == "edition") manifest.edition = value;
            else if (key == "entry") manifest.entry = value;
        } else if (section == "toolchain") {
            value = unquote(value);
            if (key == "profile") manifest.toolchain_profile = value;
            else if (key == "cc") manifest.toolchain_cc = value;
            else if (key == "cxx") manifest.toolchain_cxx = value;
            else if (key == "linker") manifest.toolchain_linker = value;
        } else if (section == "dependencies") {
            if (!valid_name(key)) throw Error("error: invalid dependency name '" + key + "'");
            std::string dependency_path;
            if (!value.empty() && value.front() == '{') {
                const std::size_t path_key = value.find("path");
                const std::size_t path_equals = path_key == std::string::npos ? std::string::npos : value.find('=', path_key);
                if (path_equals != std::string::npos) {
                    std::string tail = trim(value.substr(path_equals + 1));
                    const std::size_t close = tail.rfind('}');
                    if (close != std::string::npos) tail = trim(tail.substr(0, close));
                    if (!tail.empty() && tail.back() == ',') tail.pop_back();
                    dependency_path = unquote(trim(tail));
                }
            } else dependency_path = unquote(value);
            if (dependency_path.empty())
                throw Error("error: dependency '" + key + "' currently requires a local path");
            manifest.dependencies.push_back({key, dependency_path});
        }
    }
    if (!valid_name(manifest.name)) throw Error("error: Punpun.toml requires a valid [package] name");
    if (!fs::is_regular_file(manifest.root / manifest.entry))
        throw Error("error: package '" + manifest.name + "' entry does not exist: '" + (manifest.root / manifest.entry).string() + "'");
    return manifest;
}

inline fs::path find_root(fs::path start) {
    std::error_code ec;
    // A relative single-file input such as "main.pp" has an empty parent_path().
    // Treat that as the current working directory instead of asking
    // std::filesystem::absolute() to canonicalize an empty path.
    if (start.empty()) {
        start = fs::current_path(ec);
        if (ec) throw Error("error: cannot determine current project directory");
    } else if (!fs::is_directory(start, ec)) {
        fs::path parent = start.parent_path();
        if (parent.empty()) {
            parent = fs::current_path(ec);
            if (ec) throw Error("error: cannot determine current project directory");
        }
        start = std::move(parent);
    }
    start = fs::absolute(start).lexically_normal();
    for (;;) {
        if (fs::is_regular_file(start / "Punpun.toml")) return start;
        const fs::path parent = start.parent_path();
        if (parent == start || parent.empty()) break;
        start = parent;
    }
    return {};
}

inline uint64_t hash_append(uint64_t hash, const std::string &value) {
    constexpr uint64_t prime = UINT64_C(1099511628211);
    for (unsigned char c : value) { hash ^= c; hash *= prime; }
    return hash;
}

inline std::string hex_hash(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int i = 15; i >= 0; --i) { result[static_cast<std::size_t>(i)] = digits[value & 0xf]; value >>= 4; }
    return result;
}

inline std::string package_checksum(const Manifest &manifest) {
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_append(hash, manifest.name + "\n" + manifest.version + "\n" + manifest.edition + "\n");
    std::vector<fs::path> files;
    std::error_code error;
    for (fs::recursive_directory_iterator it(manifest.root, error), end; !error && it != end; it.increment(error)) {
        if (it->is_directory() && (it->path().filename() == ".punpun" || it->path().filename() == ".git")) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file() && (it->path().extension() == ".pp" || it->path().filename() == "Punpun.toml"))
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path &file : files) {
        hash = hash_append(hash, fs::relative(file, manifest.root).generic_string());
        hash = hash_append(hash, "\0");
        hash = hash_append(hash, read_text(file));
        hash = hash_append(hash, "\0");
    }
    return hex_hash(hash);
}

class Graph {
  public:
    explicit Graph(fs::path root) : root_(fs::absolute(std::move(root)).lexically_normal()) { load(root_); }

    const Manifest &root_manifest() const { return nodes_.at(root_.string()).manifest; }
    const std::vector<PackageNode> &packages() const { return ordered_; }

    std::unordered_map<std::string, fs::path> module_mappings() const {
        std::unordered_map<std::string, fs::path> result;
        for (const PackageNode &node : ordered_) {
            if (node.manifest.root == root_) continue;
            const fs::path entry = node.manifest.root / node.manifest.entry;
            result[node.manifest.name] = entry;
        }
        // Root aliases are also accepted even when an alias differs from the
        // dependency package's declared name.
        const Manifest &root = root_manifest();
        for (const Dependency &dependency : root.dependencies) {
            const Manifest dep = parse_manifest(fs::weakly_canonical(root.root / dependency.path));
            result[dependency.alias] = dep.root / dep.entry;
        }
        return result;
    }

    std::vector<fs::path> include_paths() const {
        std::vector<fs::path> result;
        for (const PackageNode &node : ordered_) {
            result.push_back(node.manifest.root);
            result.push_back(node.manifest.root / "src");
        }
        return result;
    }

    std::string lockfile() const {
        std::vector<const PackageNode *> sorted;
        for (const PackageNode &node : ordered_) sorted.push_back(&node);
        std::sort(sorted.begin(), sorted.end(), [](const PackageNode *a, const PackageNode *b) {
            return a->manifest.name < b->manifest.name;
        });
        std::ostringstream out;
        out << "# Generated by PunPun 0.5.0-beta. Do not edit by hand.\nversion = 1\n\n";
        for (const PackageNode *node : sorted) {
            out << "[[package]]\n"
                << "name = \"" << node->manifest.name << "\"\n"
                << "version = \"" << node->manifest.version << "\"\n"
                << "path = \"" << node->manifest.root.generic_string() << "\"\n"
                << "checksum = \"" << node->checksum << "\"\n"
                << "dependencies = [";
            for (std::size_t i = 0; i < node->dependency_names.size(); ++i) {
                if (i) out << ", ";
                out << "\"" << node->dependency_names[i] << "\"";
            }
            out << "]\n\n";
        }
        return out.str();
    }

    void write_lockfile() const {
        const fs::path path = root_ / "Punpun.lock";
        const std::string content = lockfile();
        std::string old;
        if (fs::is_regular_file(path)) old = read_text(path);
        if (old == content) return;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw Error("error: cannot write '" + path.string() + "'");
        output << content;
    }

    std::string tree() const {
        std::ostringstream out;
        std::unordered_set<std::string> expanded;
        print_tree(root_, "", true, expanded, out);
        return out.str();
    }

  private:
    fs::path root_;
    std::unordered_map<std::string, PackageNode> nodes_;
    std::vector<PackageNode> ordered_;
    std::unordered_set<std::string> loading_;

    void load(const fs::path &raw_root) {
        std::error_code error;
        const fs::path root = fs::weakly_canonical(raw_root, error);
        const fs::path actual = error ? fs::absolute(raw_root).lexically_normal() : root;
        const std::string key = actual.string();
        if (nodes_.count(key)) return;
        if (!loading_.insert(key).second)
            throw Error("error: package dependency cycle includes '" + actual.string() + "'");
        Manifest manifest = parse_manifest(actual);
        PackageNode node;
        node.manifest = manifest;
        for (const Dependency &dependency : manifest.dependencies) {
            const fs::path dependency_root = actual / dependency.path;
            Manifest dependency_manifest = parse_manifest(dependency_root);
            node.dependency_names.push_back(dependency_manifest.name);
            load(dependency_manifest.root);
        }
        std::sort(node.dependency_names.begin(), node.dependency_names.end());
        node.checksum = package_checksum(manifest);
        loading_.erase(key);
        nodes_[key] = node;
        ordered_.push_back(std::move(node));
    }

    void print_tree(const fs::path &root, const std::string &prefix, bool last,
                    std::unordered_set<std::string> &expanded, std::ostringstream &out) const {
        const std::string key = fs::weakly_canonical(root).string();
        const PackageNode &node = nodes_.at(key);
        if (prefix.empty()) out << node.manifest.name << " v" << node.manifest.version << "\n";
        const bool already = !expanded.insert(key).second;
        if (already) return;
        for (std::size_t i = 0; i < node.manifest.dependencies.size(); ++i) {
            const Dependency &dependency = node.manifest.dependencies[i];
            const fs::path child_root = fs::weakly_canonical(node.manifest.root / dependency.path);
            const PackageNode &child = nodes_.at(child_root.string());
            const bool child_last = i + 1 == node.manifest.dependencies.size();
            out << prefix << (child_last ? "└── " : "├── ") << child.manifest.name << " v" << child.manifest.version
                << " (" << dependency.path.generic_string() << ")\n";
            print_tree(child_root, prefix + (child_last ? "    " : "│   "), child_last, expanded, out);
        }
        (void)last;
    }
};

inline void edit_dependency(const fs::path &root, const std::string &name,
                            const fs::path &dependency_path, bool remove) {
    if (!valid_name(name)) throw Error("error: invalid dependency name '" + name + "'");
    const fs::path manifest_path = root / "Punpun.toml";
    std::ifstream input(manifest_path);
    if (!input) throw Error("error: no Punpun.toml in '" + root.string() + "'");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) lines.push_back(line);
    std::size_t deps = lines.size();
    std::size_t end = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (trim(lines[i]) == "[dependencies]") { deps = i; end = lines.size(); continue; }
        if (deps != lines.size() && i > deps && !trim(lines[i]).empty() && trim(lines[i]).front() == '[') { end = i; break; }
    }
    if (deps == lines.size()) {
        lines.push_back("");
        lines.push_back("[dependencies]");
        deps = lines.size() - 1;
        end = lines.size();
    }
    std::size_t existing = lines.size();
    for (std::size_t i = deps + 1; i < end; ++i) {
        const std::string stripped = trim(lines[i]);
        if (stripped.rfind(name, 0) == 0) {
            const std::size_t equals = stripped.find('=');
            if (equals != std::string::npos && trim(stripped.substr(0, equals)) == name) { existing = i; break; }
        }
    }
    if (remove) {
        if (existing == lines.size()) throw Error("error: dependency '" + name + "' is not in Punpun.toml");
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(existing));
    } else {
        const fs::path absolute_dependency = fs::weakly_canonical(root / dependency_path);
        (void)parse_manifest(absolute_dependency); // validate before mutating manifest
        std::error_code error;
        fs::path relative = fs::relative(absolute_dependency, root, error);
        if (error) relative = absolute_dependency;
        const std::string replacement = name + " = { path = \"" + relative.generic_string() + "\" }";
        if (existing == lines.size()) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(end), replacement);
        else lines[existing] = replacement;
    }
    std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
    if (!output) throw Error("error: cannot update '" + manifest_path.string() + "'");
    for (const std::string &item : lines) output << item << "\n";
}

} // namespace ppproject

#endif
