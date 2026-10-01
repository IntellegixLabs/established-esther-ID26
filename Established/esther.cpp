#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

struct EstherError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Token {
    std::string kind;
    std::string text;
    int line;
};

struct Block {
    std::string category;
    std::string name;
    std::string value;
    std::string type;
    std::vector<Block> children;
};

static std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

static std::vector<std::pair<std::string, int>> splitStatements(const std::string& source) {
    std::vector<std::pair<std::string, int>> result;
    std::istringstream input(source);
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && lower(line.substr(first, 8)) == "comment:") continue;
        bool quoted = false, escaped = false;
        std::size_t start = 0;
        for (std::size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (quoted) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') quoted = false;
            } else if (c == '"') {
                quoted = true;
            } else if (c == '.' && !(i > 0 && i + 1 < line.size() &&
                                     std::isdigit(static_cast<unsigned char>(line[i - 1])) &&
                                     std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
                std::string statement = line.substr(start, i - start);
                auto begin = statement.find_first_not_of(" \t");
                auto end = statement.find_last_not_of(" \t");
                if (begin != std::string::npos) result.emplace_back(statement.substr(begin, end - begin + 1), lineNumber);
                start = i + 1;
            }
        }
        std::string tail = line.substr(start);
        auto begin = tail.find_first_not_of(" \t");
        auto end = tail.find_last_not_of(" \t");
        if (begin != std::string::npos) result.emplace_back(tail.substr(begin, end - begin + 1), lineNumber);
    }
    return result;
}

static std::vector<Token> lexStatement(const std::string& statement, int line) {
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < statement.size();) {
        if (std::isspace(static_cast<unsigned char>(statement[i]))) { ++i; continue; }
        if (statement[i] == '"') {
            ++i;
            std::string text;
            while (i < statement.size() && statement[i] != '"') {
                if (statement[i] == '\\' && i + 1 < statement.size()) {
                    ++i;
                    char escaped = statement[i++];
                    text += escaped == 'n' ? '\n' : escaped;
                } else text += statement[i++];
            }
            if (i == statement.size()) throw EstherError("Unterminated string at line " + std::to_string(line));
            ++i;
            tokens.push_back({"string", text, line});
        } else if (std::isdigit(static_cast<unsigned char>(statement[i]))) {
            std::size_t start = i++;
            while (i < statement.size() && (std::isdigit(static_cast<unsigned char>(statement[i])) || statement[i] == '.')) ++i;
            tokens.push_back({"number", statement.substr(start, i - start), line});
        } else if (std::isalpha(static_cast<unsigned char>(statement[i])) || statement[i] == '_') {
            std::size_t start = i++;
                 while (i < statement.size() && (std::isalnum(static_cast<unsigned char>(statement[i])) || statement[i] == '_' || statement[i] == '-' ||
                     (statement[i] == '/' && i + 1 < statement.size() && std::tolower(static_cast<unsigned char>(statement[i + 1])) == 'o'))) ++i;
            tokens.push_back({"word", lower(statement.substr(start, i - start)), line});
        } else if (std::string("(),:").find(statement[i]) != std::string::npos) {
            tokens.push_back({"punct", std::string(1, statement[i++]), line});
        } else {
            throw EstherError("Unexpected character at line " + std::to_string(line) + ": " + statement.substr(i, 1));
        }
    }
    return tokens;
}

class Parser {
    std::vector<std::pair<std::vector<Token>, int>> statements;
    std::size_t position = 0;

    std::vector<std::string> words(const std::vector<Token>& tokens) const {
        std::vector<std::string> result;
        for (const auto& token : tokens) result.push_back(token.text);
        return result;
    }
    std::string quotedName(const std::vector<Token>& tokens, std::size_t index, int line) const {
        if (index >= tokens.size() || tokens[index].kind != "string") throw EstherError("Expected a quoted name at line " + std::to_string(line));
        return tokens[index].text;
    }
    Block expression(const std::vector<Token>& tokens, int line) const;
    std::vector<Block> sequence(const std::unordered_set<std::string>& stops = {}) {
        std::vector<Block> blocks;
        while (position < statements.size()) {
            const auto& tokens = statements[position].first;
            auto first = tokens.empty() ? "" : tokens[0].text;
            if (stops.count(first)) break;
            if (tokens.size() >= 2 && first == "end" && tokens[1].text == "executable") { ++position; break; }
            blocks.push_back(statement());
        }
        return blocks;
    }
    void consumeEnd(int line, bool publicClass = false) {
        if (position >= statements.size()) throw EstherError("Missing end this for block opened at line " + std::to_string(line));
        auto ws = words(statements[position].first);
        std::vector<std::string> expected = publicClass ? std::vector<std::string>{"end", "this", "that"} : std::vector<std::string>{"end", "this"};
        if (ws.size() < expected.size() || !std::equal(expected.begin(), expected.end(), ws.begin()))
            throw EstherError("Expected end this at line " + std::to_string(statements[position].second));
        ++position;
    }
    Block statement() {
        auto tokens = statements[position].first;
        int line = statements[position].second;
        auto ws = words(tokens);
        ++position;
        auto make = [](std::string category, std::string name = {}, std::string value = {}, std::string type = {}, std::vector<Block> children = {}) {
            return Block{std::move(category), std::move(name), std::move(value), std::move(type), std::move(children)};
        };
        if (ws[0] == "establish" || ws[0] == "let") {
            auto name = quotedName(tokens, 1, line);
            if (tokens.size() < 4 || tokens[2].text != "become") throw EstherError("Expected become at line " + std::to_string(line));
            return make(ws[0], name, {}, {}, {expression(std::vector<Token>(tokens.begin() + 3, tokens.end()), line)});
        }
        if (ws[0] == "change") {
            auto name = quotedName(tokens, 1, line);
            if (tokens.size() < 4 || tokens[2].text != "to") throw EstherError("Expected to at line " + std::to_string(line));
            return make("change", name, {}, {}, {expression(std::vector<Token>(tokens.begin() + 3, tokens.end()), line)});
        }
        if (ws[0] == "say") return make("say", {}, {}, {}, {expression(std::vector<Token>(tokens.begin() + 1, tokens.end()), line)});
        if (ws[0] == "ask") {
            auto read = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) { return t.text == "read"; });
            if (tokens.size() < 2 || tokens[1].kind != "string" || read == tokens.end()) throw EstherError("Malformed ask/read at line " + std::to_string(line));
            return make("read", quotedName(tokens, static_cast<std::size_t>(read - tokens.begin()) + 1, line), tokens[1].text);
        }
        if (ws[0] == "read") {
            if (ws.size() >= 2 && ws[1] == "text") {
                auto file = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) { return t.text == "file"; });
                auto into = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) { return t.text == "into"; });
                if (file == tokens.end() || into == tokens.end()) throw EstherError("Malformed file read at line " + std::to_string(line));
                return make("file_read", quotedName(tokens, static_cast<std::size_t>(into - tokens.begin()) + 1, line), quotedName(tokens, static_cast<std::size_t>(file - tokens.begin()) + 1, line));
            }
            return make("read", quotedName(tokens, 1, line));
        }
        if (ws.size() >= 3 && ws[0] == "grab" && ws[1] == "and" && ws[2] == "use") {
            auto name = quotedName(tokens, 3, line);
            auto from = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) { return t.text == "from"; });
            if (from == tokens.end() || from + 1 == tokens.end()) throw EstherError("Malformed import at line " + std::to_string(line));
            auto index = static_cast<std::size_t>(from - tokens.begin()) + 1;
            return make("dependency", name, tokens[index].text == "the" ? "standard library" : tokens[index].text);
        }
        if (ws.size() >= 2 && ws[0] == "list" && ws[1] == "add") {
            if (tokens.size() < 5 || tokens[2].text != "list" || tokens[4].text != ":") throw EstherError("Malformed list declaration at line " + std::to_string(line));
            auto name = quotedName(tokens, 3, line);
            std::vector<Block> items;
            std::vector<Token> part;
            for (std::size_t i = 5; i <= tokens.size(); ++i) {
                if (i == tokens.size() || tokens[i].text == "," || tokens[i].text == "and") {
                    if (!part.empty()) {
                        if (part.size() == 1 && part[0].kind == "string") items.push_back({"value", {}, part[0].text, "string", {}});
                        else if (part.size() == 1 && part[0].kind == "number") items.push_back(expression(part, line));
                        else {
                            std::string text;
                            for (const auto& token : part) { if (!text.empty()) text += ' '; text += token.text; }
                            items.push_back({"value", {}, text, "string", {}});
                        }
                        part.clear();
                    }
                } else part.push_back(tokens[i]);
            }
            return make("list", name, {}, {}, std::move(items));
        }
        if (ws.size() >= 2 && ws[0] == "list" && (ws[1] == "put" || ws[1] == "remove")) {
            auto value = tokens[2].kind == "string" ? Block{"value", {}, tokens[2].text, "string", {}} : expression(std::vector<Token>{tokens[2]}, line);
            auto marker = ws[1] == "put" ? "in" : "from";
            auto at = std::find_if(tokens.begin() + 3, tokens.end(), [&](const Token& t) { return t.text == marker; });
            if (at == tokens.end()) throw EstherError("Malformed list operation at line " + std::to_string(line));
            auto listName = quotedName(tokens, static_cast<std::size_t>(at - tokens.begin()) + 1, line);
            return make(ws[1] == "put" ? "list_put" : "list_remove", listName, {}, {}, {value});
        }
        if (ws.size() >= 2 && ws[0] == "this" && ws[1] == "if") {
            auto condition = expression(std::vector<Token>(tokens.begin() + 2, tokens.end()), line);
            auto yes = sequence({"otherwise", "end"});
            std::vector<Block> no;
            if (position < statements.size() && statements[position].first[0].text == "otherwise") { ++position; no = sequence({"end"}); }
            consumeEnd(line);
            std::vector<Block> children{condition, make("otherwise", {}, {}, {}, std::move(no))};
            children.insert(children.end(), yes.begin(), yes.end());
            return make("if", {}, {}, {}, std::move(children));
        }
        if (ws.size() >= 3 && ws[0] == "this" && ws[1] == "loop" && ws[2] == "while") {
            auto condition = expression(std::vector<Token>(tokens.begin() + 3, tokens.end()), line);
            auto body = sequence({"end"}); consumeEnd(line);
            body.insert(body.begin(), condition);
            return make("loop", {}, {}, "while", std::move(body));
        }
        if (ws.size() >= 2 && ws[0] == "this" && ws[1] == "try") {
            auto body = sequence({"if", "end"});
            std::vector<Block> failure;
            if (position < statements.size()) {
                auto following = words(statements[position].first);
                if (following.size() >= 3 && following[0] == "if" && following[1] == "that" && following[2] == "fails") {
                    ++position;
                    failure = sequence({"end"});
                }
            }
            consumeEnd(line);
            std::vector<Block> children{make("otherwise", {}, {}, {}, std::move(failure))};
            children.insert(children.end(), body.begin(), body.end());
            return make("try", {}, {}, {}, std::move(children));
        }
        if (ws.size() >= 4 && ws[0] == "this" && ws[1] == "loop" && ws[2] == "for" && ws[3] == "each") {
            auto variable = quotedName(tokens, 4, line);
            auto in = std::find_if(tokens.begin() + 5, tokens.end(), [](const Token& t) { return t.text == "in"; });
            if (in == tokens.end()) throw EstherError("Malformed for-each at line " + std::to_string(line));
            auto index = static_cast<std::size_t>(in - tokens.begin());
            auto listName = quotedName(tokens, index + 1, line);
            auto body = sequence({"end"}); consumeEnd(line);
            return make("loop", variable, listName, "foreach", std::move(body));
        }
        if (ws.size() >= 2 && ws[0] == "this" && ws[1] == "function") {
            auto name = quotedName(tokens, 2, line);
            std::string parameters;
            for (std::size_t i = 3; i < tokens.size(); ++i) if (tokens[i].kind == "string") parameters += tokens[i].text + "\n";
            auto body = sequence({"end"}); consumeEnd(line);
            return make("function", name, parameters, {}, std::move(body));
        }
        if (ws.size() >= 2 && ws[0] == "this" && (ws[1] == "that" || tokens[1].kind == "string")) {
            bool isPublic = ws[1] == "that";
            auto name = quotedName(tokens, isPublic ? 2 : 1, line);
            auto body = sequence({"end"}); consumeEnd(line, isPublic);
            return make(isPublic ? "class-public" : "class", name, {}, {}, std::move(body));
        }
        if (!ws.empty() && ws[0] == "return") return make("return", {}, {}, {}, {expression(std::vector<Token>(tokens.begin() + 1, tokens.end()), line)});
        if (!ws.empty() && ws[0] == "stop") return make("stop");
        if (ws.size() >= 2 && ws[0] == "that" && ws[1] == "function") {
            auto name = quotedName(tokens, 2, line);
            std::vector<Block> args;
            auto with = std::find_if(tokens.begin() + 3, tokens.end(), [](const Token& t) { return t.text == "with"; });
            if (with != tokens.end()) for (auto it = with + 1; it != tokens.end(); ++it)
                if (it->kind == "string" || it->kind == "number") args.push_back(expression(std::vector<Token>{*it}, line));
            return make("call", name, {}, {}, std::move(args));
        }
        if (ws.size() >= 3 && ws[0] == "that" && tokens[2].text == "function") {
            auto object = quotedName(tokens, 1, line);
            auto method = quotedName(tokens, 3, line);
            std::vector<Block> args;
            for (std::size_t i = 5; i < tokens.size(); ++i) if (tokens[i].kind == "string" || tokens[i].kind == "number") args.push_back(expression(std::vector<Token>{tokens[i]}, line));
            return make("method_call", object, method, {}, std::move(args));
        }
        if (ws.size() >= 2 && ws[0] == "write") {
            auto file = std::find_if(tokens.begin(), tokens.end(), [](const Token& t) { return t.text == "file"; });
            if (file == tokens.end()) throw EstherError("Malformed file write at line " + std::to_string(line));
            auto index = static_cast<std::size_t>(file - tokens.begin());
            auto to = std::find_if(tokens.begin() + 1, file, [](const Token& t) { return t.text == "to"; });
            if (to == file) throw EstherError("Expected 'to file' in file write at line " + std::to_string(line));
            return make("file_write", {}, quotedName(tokens, index + 1, line), {}, {expression(std::vector<Token>(tokens.begin() + 1, to), line)});
        }
        if (ws.size() >= 2 && ws[0] == "trash" && ws[1] == "file") return make("trash_file", {}, quotedName(tokens, 2, line));
        if (ws.size() >= 2 && ws[0] == "trash" && ws[1] == "the") return make("trash_variable", quotedName(tokens, 4, line));
        if (ws.size() >= 2 && ws[0] == "paint" && ws[1] == "clear") return make("paint_clear");
        if (ws.size() >= 2 && ws[0] == "paint" && ws[1] == "color" && tokens.size() > 2)
            return make("paint_color", {}, tokens[2].text);
        if (ws.size() >= 2 && ws[0] == "paint" && ws[1] == "draw" && tokens.size() > 2)
            return make("paint_draw", {}, tokens[2].text);
        throw EstherError("Unknown statement at line " + std::to_string(line) + ": " + ws[0]);
    }

public:
    explicit Parser(const std::string& source) {
        for (const auto& [text, line] : splitStatements(source)) statements.emplace_back(lexStatement(text, line), line);
    }
    Block parse() {
        auto blocks = sequence();
        bool executableSeen = false;
        for (const auto& block : blocks) {
            if (block.category == "dependency" && executableSeen)
                throw EstherError("Imports must appear before executable code");
            if (block.category != "dependency") executableSeen = true;
        }
        return {"program", {}, {}, {}, std::move(blocks)};
    }
};

class ExprParser {
    const std::vector<Token>& tokens;
    int line;
    std::size_t pos = 0;
    bool match(const std::vector<std::string>& phrase) {
        if (pos + phrase.size() > tokens.size()) return false;
        for (std::size_t i = 0; i < phrase.size(); ++i) if (tokens[pos + i].text != phrase[i]) return false;
        pos += phrase.size(); return true;
    }
    Block primary() {
        if (pos >= tokens.size()) throw EstherError("Incomplete expression at line " + std::to_string(line));
        auto token = tokens[pos++];
        if (token.kind == "number") return {"value", {}, token.text, "number", {}};
        if (token.kind == "string") return {"value", {}, token.text, "string", {}};
        if (token.text == "true" || token.text == "false") return {"value", {}, token.text, "boolean", {}};
        if (token.text == "(" ) {
            auto value = parseOr();
            if (!match({")"})) throw EstherError("Expected ')' at line " + std::to_string(line));
            return value;
        }
        if (token.text == "the" && match({"remainder", "of"})) {
            auto separator = std::find_if(tokens.begin() + static_cast<std::ptrdiff_t>(pos), tokens.end() - 1,
                [&](const Token& current) {
                    auto index = static_cast<std::size_t>(&current - tokens.data());
                    return current.text == "divided" && tokens[index + 1].text == "by";
                });
            if (separator == tokens.end() - 1) throw EstherError("Expected 'divided by' in remainder expression at line " + std::to_string(line));
            auto split = static_cast<std::size_t>(separator - tokens.begin());
            auto left = ExprParser(std::vector<Token>(tokens.begin() + static_cast<std::ptrdiff_t>(pos), tokens.begin() + static_cast<std::ptrdiff_t>(split)), line).parse();
            auto right = ExprParser(std::vector<Token>(tokens.begin() + static_cast<std::ptrdiff_t>(split + 2), tokens.end()), line).parse();
            pos = tokens.size();
            return {"value", "remainder", {}, "binary", {left, right}};
        }
        if (token.text == "the" && match({"value", "of"})) {
            if (pos >= tokens.size() || tokens[pos].kind != "string") throw EstherError("Expected quoted name at line " + std::to_string(line));
            return {"value", tokens[pos++].text, {}, "variable", {}};
        }
        if (token.text == "item") {
            auto index = primary();
            if (!match({"of"}) || pos >= tokens.size() || tokens[pos].kind != "string") throw EstherError("Malformed list item at line " + std::to_string(line));
            auto name = tokens[pos++].text;
            if (!match({"list"})) throw EstherError("Expected list at line " + std::to_string(line));
            return {"value", name, {}, "list_item", {index}};
        }
        if (token.text == "random" && match({"number", "from"})) {
            auto low = parseOr(); if (!match({"to"})) throw EstherError("Expected to in random number at line " + std::to_string(line));
            return {"value", {}, {}, "random", {low, parseOr()}};
        }
        if (token.text == "a" && match({"new"}) && pos < tokens.size() && tokens[pos].kind == "string")
            return {"value", tokens[pos++].text, {}, "new_instance", {}};
        if (token.text == "that" && match({"function"}) && pos < tokens.size() && tokens[pos].kind == "string") {
            auto name = tokens[pos++].text;
            if (!match({"with"})) throw EstherError("Expected with after function name at line " + std::to_string(line));
            std::vector<Block> args;
            do { args.push_back(primary()); } while (match({"and"}));
            return {"value", name, {}, "call", std::move(args)};
        }
        if (token.text == "not") return {"value", "not", {}, "unary", {primary()}};
        throw EstherError("Expected value at line " + std::to_string(line) + ", got " + token.text);
    }
    Block multiply() {
        auto left = primary();
        while (true) {
            std::string op;
            if (match({"times"})) op = "*";
            else if (match({"divided", "by"})) op = "/";
            else break;
            left = {"value", op, {}, "binary", {left, primary()}};
        }
        return left;
    }
    Block add() {
        auto left = multiply();
        while (true) {
            std::string op;
            if (match({"plus"})) op = "+";
            else if (match({"minus"})) op = "-";
            else break;
            left = {"value", op, {}, "binary", {left, multiply()}};
        }
        return left;
    }
    Block compare() {
        auto left = add();
        std::string op;
        if (match({"is", "greater", "than"})) op = "is greater than";
        else if (match({"is", "less", "than"})) op = "is less than";
        else if (match({"is", "at", "least"})) op = "is at least";
        else if (match({"is", "at", "most"})) op = "is at most";
        else if (match({"is", "not"})) op = "is not";
        else if (match({"is"})) op = "is";
        else return left;
        return {"value", op, {}, "binary", {left, add()}};
    }
    Block parseAnd() { auto left = compare(); while (match({"and"})) left = {"value", "and", {}, "binary", {left, compare()}}; return left; }
    Block parseOr() { auto left = parseAnd(); while (match({"or"})) left = {"value", "or", {}, "binary", {left, parseAnd()}}; return left; }
public:
    ExprParser(const std::vector<Token>& source, int sourceLine) : tokens(source), line(sourceLine) {}
    Block parse() { auto result = parseOr(); if (pos != tokens.size()) throw EstherError("Unexpected token in expression at line " + std::to_string(line)); return result; }
};

Block Parser::expression(const std::vector<Token>& tokens, int line) const { return ExprParser(tokens, line).parse(); }

struct List;
struct Instance;
using ListPtr = std::shared_ptr<List>;
using InstancePtr = std::shared_ptr<Instance>;
using Value = std::variant<std::monostate, std::int64_t, double, std::string, bool, ListPtr, InstancePtr>;
struct List { std::vector<Value> values; };
struct Instance { std::string className; std::unordered_map<std::string, Value> fields; std::unordered_map<std::string, Block> methods; };

static std::string stringify(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "nothing";
    if (auto p = std::get_if<std::int64_t>(&value)) return std::to_string(*p);
    if (auto p = std::get_if<double>(&value)) { std::ostringstream out; out << *p; return out.str(); }
    if (auto p = std::get_if<std::string>(&value)) return *p;
    if (auto p = std::get_if<bool>(&value)) return *p ? "true" : "false";
    if (auto p = std::get_if<ListPtr>(&value)) return "[list: " + std::to_string((*p)->values.size()) + " items]";
    return "[" + std::get<InstancePtr>(value)->className + " instance]";
}
static double number(const Value& value) {
    if (auto p = std::get_if<std::int64_t>(&value)) return static_cast<double>(*p);
    if (auto p = std::get_if<double>(&value)) return *p;
    throw EstherError("Expected a number, got " + stringify(value));
}
static bool truthy(const Value& value) {
    if (auto p = std::get_if<bool>(&value)) return *p;
    if (auto p = std::get_if<std::int64_t>(&value)) return *p != 0;
    if (auto p = std::get_if<double>(&value)) return *p != 0;
    if (auto p = std::get_if<std::string>(&value)) return !p->empty();
    return !std::holds_alternative<std::monostate>(value);
}

struct ReturnValue { Value value; };
struct StopExecution {};

class Runtime {
    std::vector<std::unordered_map<std::string, Value>> scopes{{}};
    std::unordered_set<std::string> constants, imports, loadedFiles;
    std::unordered_map<std::string, Block> functions, classes;
    fs::path basePath;
    std::mt19937 randomEngine{std::random_device{}()};
    std::function<std::string(const std::string&)> inputFn;
    void assign(const std::string& name, Value value, bool declare = false, bool constant = false) {
        if (declare) {
            if (scopes.back().count(name)) throw EstherError("Name already declared: " + name);
            scopes.back()[name] = std::move(value); if (constant) constants.insert(name); return;
        }
        if (constants.count(name)) throw EstherError("Cannot change constant: " + name);
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) if (it->count(name)) { (*it)[name] = std::move(value); return; }
        throw EstherError("Cannot change undeclared name: " + name);
    }
    Value lookup(const std::string& name) const {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) { auto found = it->find(name); if (found != it->end()) return found->second; }
        throw EstherError("Unknown name: " + name);
    }
    void assignReadValue(const std::string& name, Value value) {
        bool declared = false;
        for (const auto& scope : scopes) if (scope.count(name)) { declared = true; break; }
        assign(name, std::move(value), !declared);
    }
    Value evaluate(const Block& expr) {
        if (expr.type == "number") { if (expr.value.find('.') != std::string::npos) return std::stod(expr.value); return static_cast<std::int64_t>(std::stoll(expr.value)); }
        if (expr.type == "string") return expr.value;
        if (expr.type == "boolean") return expr.value == "true";
        if (expr.type == "variable") return lookup(expr.name);
        if (expr.type == "list_item") {
            auto index = static_cast<std::int64_t>(number(evaluate(expr.children[0]))); auto list = std::get<ListPtr>(lookup(expr.name));
            if (index < 1 || static_cast<std::size_t>(index) > list->values.size()) throw EstherError("List index out of range");
            return list->values[static_cast<std::size_t>(index - 1)];
        }
        if (expr.type == "random") {
            auto low = static_cast<int>(number(evaluate(expr.children[0]))), high = static_cast<int>(number(evaluate(expr.children[1])));
            return static_cast<std::int64_t>(std::uniform_int_distribution<int>(low, high)(randomEngine));
        }
        if (expr.type == "new_instance") return instantiate(expr.name);
        if (expr.type == "call") { std::vector<Value> args; for (const auto& child : expr.children) args.push_back(evaluate(child)); return call(expr.name, args); }
        if (expr.type == "unary") return !truthy(evaluate(expr.children[0]));
        if (expr.type == "binary") {
            Value left = evaluate(expr.children[0]), right = evaluate(expr.children[1]);
            if (expr.name == "+") {
                if (std::holds_alternative<std::string>(left) || std::holds_alternative<std::string>(right)) return stringify(left) + stringify(right);
                double result = number(left) + number(right);
                return std::holds_alternative<std::int64_t>(left) && std::holds_alternative<std::int64_t>(right) ? Value(static_cast<std::int64_t>(result)) : Value(result);
            }
            if (expr.name == "-") {
                double result = number(left) - number(right);
                return std::holds_alternative<std::int64_t>(left) && std::holds_alternative<std::int64_t>(right) ? Value(static_cast<std::int64_t>(result)) : Value(result);
            }
            if (expr.name == "*") {
                double result = number(left) * number(right);
                return std::holds_alternative<std::int64_t>(left) && std::holds_alternative<std::int64_t>(right) ? Value(static_cast<std::int64_t>(result)) : Value(result);
            }
            if (expr.name == "/") { auto divisor = number(right); if (divisor == 0) throw EstherError("Division by zero"); return number(left) / divisor; }
            if (expr.name == "remainder") { auto divisor = number(right); if (divisor == 0) throw EstherError("Division by zero"); return std::fmod(number(left), divisor); }
            if (expr.name == "is") return stringify(left) == stringify(right);
            if (expr.name == "is not") return stringify(left) != stringify(right);
            if (expr.name == "is greater than") return number(left) > number(right);
            if (expr.name == "is less than") return number(left) < number(right);
            if (expr.name == "is at least") return number(left) >= number(right);
            if (expr.name == "is at most") return number(left) <= number(right);
            if (expr.name == "and") return truthy(left) && truthy(right);
            if (expr.name == "or") return truthy(left) || truthy(right);
        }
        throw EstherError("Invalid expression block");
    }
    void executeBlocks(const std::vector<Block>& blocks) { for (const auto& block : blocks) execute(block); }
    void executeLoop(const Block& block) {
        try {
            if (block.type == "while") {
                while (truthy(evaluate(block.children[0]))) executeBlocks(std::vector<Block>(block.children.begin() + 1, block.children.end()));
            } else {
                auto values = std::get<ListPtr>(lookup(block.value))->values;
                for (const auto& value : values) { assign(block.name, value, true); executeBlocks(block.children); scopes.back().erase(block.name); }
            }
        } catch (const StopExecution&) { }
    }
    Value call(const std::string& name, const std::vector<Value>& args) {
        auto found = functions.find(name); if (found == functions.end()) throw EstherError("Unknown function: " + name);
        std::istringstream names(found->second.value); std::vector<std::string> parameters; std::string parameter;
        while (std::getline(names, parameter)) if (!parameter.empty()) parameters.push_back(parameter);
        if (parameters.size() != args.size()) throw EstherError("Wrong number of function arguments: " + name);
        std::unordered_map<std::string, Value> local; for (std::size_t i = 0; i < args.size(); ++i) local[parameters[i]] = args[i];
        scopes.push_back(std::move(local));
        try { executeBlocks(found->second.children); } catch (const ReturnValue& returned) { scopes.pop_back(); return returned.value; }
        scopes.pop_back(); return {};
    }
    InstancePtr instantiate(const std::string& name) {
        auto found = classes.find(name); if (found == classes.end()) throw EstherError("Unknown class: " + name);
        auto instance = std::make_shared<Instance>(); instance->className = name;
        for (const auto& block : found->second.children) if (block.category == "function") instance->methods[block.name] = block;
        scopes.push_back({});
        try { for (const auto& block : found->second.children) if (block.category != "function") execute(block); }
        catch (...) { scopes.pop_back(); throw; }
        instance->fields = std::move(scopes.back()); scopes.pop_back(); return instance;
    }
    void methodCall(const Block& block) {
        auto instance = std::get<InstancePtr>(lookup(block.name)); auto found = instance->methods.find(block.value);
        if (found == instance->methods.end()) throw EstherError("Unknown method: " + block.value);
        std::vector<Value> args; for (const auto& child : block.children) args.push_back(evaluate(child));
        std::istringstream names(found->second.value); std::vector<std::string> parameters; std::string parameter;
        while (std::getline(names, parameter)) if (!parameter.empty()) parameters.push_back(parameter);
        if (parameters.size() != args.size()) throw EstherError("Wrong number of method arguments: " + block.value);
        auto fields = instance->fields; for (std::size_t i = 0; i < args.size(); ++i) fields[parameters[i]] = args[i];
        scopes.push_back(std::move(fields));
        try { executeBlocks(found->second.children); } catch (const ReturnValue&) { }
        instance->fields = std::move(scopes.back()); scopes.pop_back();
    }
    void import(const Block& block) {
        std::string key = block.value + ":" + block.name; if (imports.count(key)) return;
        if (block.value == "standard library") {
            static const std::unordered_set<std::string> modules{"io", "filestream", "paint", "trash"};
            if (!modules.count(block.name)) throw EstherError("Unknown standard library module: " + block.name);
        } else {
            fs::path path = basePath / block.value; if (path.extension() != ".esther") path += ".esther";
            path = fs::absolute(path).lexically_normal();
            if (!loadedFiles.count(path.string())) {
                std::ifstream file(path); if (!file) throw EstherError("Import not found: " + path.string());
                std::stringstream contents; contents << file.rdbuf(); loadedFiles.insert(path.string());
                auto previous = basePath; basePath = path.parent_path();
                try { executeBlocks(Parser(contents.str()).parse().children); } catch (...) { basePath = previous; throw; }
                basePath = previous;
            }
        }
        imports.insert(key);
    }
public:
    explicit Runtime(fs::path base = fs::current_path(), std::function<std::string(const std::string&)> reader = {})
        : basePath(std::move(base)), inputFn(reader ? std::move(reader) : [](const std::string& prompt) {
            if (!prompt.empty()) std::cout << prompt;
            std::string value; std::getline(std::cin, value); return value;
        }) {}
    void executeProgram(const Block& program) { try { executeBlocks(program.children); } catch (const StopExecution&) { } }
    void execute(const Block& block) {
        const auto& c = block.category;
        if (c == "dependency") import(block);
        else if (c == "establish" || c == "let") assign(block.name, evaluate(block.children[0]), true, c == "establish");
        else if (c == "change") assign(block.name, evaluate(block.children[0]));
        else if (c == "say") std::cout << stringify(evaluate(block.children[0])) << '\n';
        else if (c == "read") {
            auto input = inputFn(block.value);
            try {
                std::size_t used = 0;
                auto integer = std::stoll(input, &used);
                if (used == input.size()) assignReadValue(block.name, static_cast<std::int64_t>(integer));
                else if (input.find('.') != std::string::npos) {
                    used = 0; auto decimal = std::stod(input, &used);
                    if (used == input.size()) assignReadValue(block.name, decimal); else assignReadValue(block.name, input);
                } else assignReadValue(block.name, input);
            } catch (const std::exception&) { assignReadValue(block.name, input); }
        }
        else if (c == "list") { auto list = std::make_shared<List>(); for (const auto& item : block.children) list->values.push_back(evaluate(item)); assign(block.name, list, true); }
        else if (c == "list_put") std::get<ListPtr>(lookup(block.name))->values.push_back(evaluate(block.children[0]));
        else if (c == "list_remove") {
            auto list = std::get<ListPtr>(lookup(block.name)); auto target = stringify(evaluate(block.children[0]));
            auto found = std::find_if(list->values.begin(), list->values.end(), [&](const Value& v) { return stringify(v) == target; });
            if (found == list->values.end()) throw EstherError("Item not found in list: " + block.name); list->values.erase(found);
        }
        else if (c == "file_read") { std::ifstream file(basePath / block.value); if (!file) throw EstherError("Could not read file: " + block.value); std::stringstream content; content << file.rdbuf(); assignReadValue(block.name, content.str()); }
        else if (c == "file_write") { std::ofstream file(basePath / block.value); if (!file) throw EstherError("Could not write file: " + block.value); file << stringify(evaluate(block.children[0])); }
        else if (c == "if") {
            if (truthy(evaluate(block.children[0]))) executeBlocks(std::vector<Block>(block.children.begin() + 2, block.children.end()));
            else executeBlocks(block.children[1].children);
        }
        else if (c == "loop") executeLoop(block);
        else if (c == "function") functions[block.name] = block;
        else if (c == "class" || c == "class-public") classes[block.name] = block;
        else if (c == "call") { std::vector<Value> args; for (const auto& arg : block.children) args.push_back(evaluate(arg)); call(block.name, args); }
        else if (c == "method_call") methodCall(block);
        else if (c == "return") throw ReturnValue{evaluate(block.children[0])};
        else if (c == "stop") throw StopExecution{};
        else if (c == "try") {
            try { executeBlocks(std::vector<Block>(block.children.begin() + 1, block.children.end())); }
            catch (const EstherError&) { executeBlocks(block.children[0].children); }
        }
        else if (c == "trash_variable") { for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) if (it->erase(block.name)) return; throw EstherError("Unknown name: " + block.name); }
        else if (c == "trash_file") { std::error_code error; fs::remove(basePath / block.value, error); if (error) throw EstherError(error.message()); }
        else if (c == "paint_clear") std::cout << "\033[2J\033[H";
        else if (c == "paint_color") { }
        else if (c == "paint_draw") std::cout << block.value << '\n';
        else throw EstherError("Unsupported block category: " + c);
    }
};

static std::string jsonString(const std::string& value) {
    std::string escaped = "\"";
    for (unsigned char character : value) {
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += hex[character >> 4];
                    escaped += hex[character & 0x0f];
                } else escaped += static_cast<char>(character);
        }
    }
    escaped += '"';
    return escaped;
}

static std::string serialize(const Block& block, int depth = 0) {
    std::ostringstream out; std::string pad(static_cast<std::size_t>(depth), ' ');
    out << pad << "{\n" << pad << "  \"category\": " << jsonString(block.category);
    if (!block.name.empty()) out << ",\n" << pad << "  \"name\": " << jsonString(block.name);
    if (!block.type.empty()) out << ",\n" << pad << "  \"type\": " << jsonString(block.type);
    if (!block.value.empty()) {
        out << ",\n" << pad << "  \"value\": ";
        if (block.type == "number" || block.type == "boolean") out << block.value;
        else out << jsonString(block.value);
    }
    if (!block.children.empty()) {
        out << ",\n" << pad << "  \"children\": [";
        for (std::size_t i = 0; i < block.children.size(); ++i) out << (i ? ",\n" : "\n") << serialize(block.children[i], depth + 4);
        out << "\n" << pad << "  ]";
    }
    out << "\n" << pad << "}"; return out.str();
}

int main(int argc, char** argv) {
    if (argc != 3 || (std::string(argv[1]) != "run" && std::string(argv[1]) != "check" && std::string(argv[1]) != "ir")) {
        std::cerr << "Usage: esther {run|check|ir} FILE.esther\n"; return 2;
    }
    fs::path path(argv[2]);
    if (path.extension() != ".esther") { std::cerr << "Source files must use the .esther extension.\n"; return 2; }
    std::ifstream file(path); if (!file) { std::cerr << "Cannot open source file: " << path << '\n'; return 1; }
    try {
        std::stringstream source; source << file.rdbuf(); auto program = Parser(source.str()).parse();
        std::string action(argv[1]);
        if (action == "ir") std::cout << serialize(program) << '\n';
        else if (action == "run") Runtime(fs::absolute(path).parent_path()).executeProgram(program);
        else std::cout << "OK: " << path.string() << '\n';
    } catch (const std::exception& error) { std::cerr << "Established error: " << error.what() << '\n'; return 1; }
    return 0;
}