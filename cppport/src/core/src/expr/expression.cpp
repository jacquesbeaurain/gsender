#include "gs/expr/expression.hpp"

#include "builtins.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gs::expr {
namespace {

// ---- AST -------------------------------------------------------------------

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    enum class Type {
        Literal, Identifier, This, Array, Object, Unary, Binary, Logical, Conditional,
        Member, Call, Assignment, Sequence, Template, TaggedTemplate
    };

    explicit Node(Type t) : type(t) {}

    Type type;
    Value literal;                  // Literal
    std::string name;               // Identifier name, operator, static member name
    bool computed = false;          // Member: a[b] rather than a.b
    std::vector<NodePtr> children;  // operands / elements / arguments
    std::vector<std::string> keys;  // Object keys, Template quasis
};

NodePtr makeNode(Node::Type type) {
    return std::make_unique<Node>(type);
}

struct SyntaxError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ---- Lexer + parser ------------------------------------------------------------

enum class Tok { End, Number, String, TemplateStart, Identifier, Punct };

struct Token {
    Tok type = Tok::End;
    std::string text;
    double number = 0;
};

bool isIdentStart(char c) {
    return str::isAsciiAlpha(c) || c == '_' || c == '$';
}

bool isIdentPart(char c) {
    return isIdentStart(c) || str::isAsciiDigit(c);
}

class Parser {
public:
    explicit Parser(std::string_view source) : src_(source) {}

    NodePtr parseProgram() {
        NodePtr expr = parseExpression();
        if (isPunct(";")) {
            consume();
        }
        if (peek().type != Tok::End) {
            throw SyntaxError("unexpected token");
        }
        return expr;
    }

private:
    // -- lexing --

    void skipSpace() {
        while (pos_ < src_.size()) {
            if (str::isAsciiSpace(src_[pos_])) {
                ++pos_;
            } else if (src_.substr(pos_, 2) == "//") {
                pos_ = src_.size();
            } else if (src_.substr(pos_, 2) == "/*") {
                const std::size_t end = src_.find("*/", pos_ + 2);
                if (end == std::string_view::npos) {
                    throw SyntaxError("unterminated comment");
                }
                pos_ = end + 2;
            } else {
                break;
            }
        }
    }

    const Token& peek() {
        if (!hasToken_) {
            token_ = scan();
            hasToken_ = true;
        }
        return token_;
    }

    Token consume() {
        peek();
        hasToken_ = false;
        return token_;
    }

    bool isPunct(std::string_view p) {
        const Token& t = peek();
        return t.type == Tok::Punct && t.text == p;
    }

    bool isIdent(std::string_view name) {
        const Token& t = peek();
        return t.type == Tok::Identifier && t.text == name;
    }

    void expectPunct(std::string_view p) {
        if (!isPunct(p)) {
            throw SyntaxError("expected " + std::string(p));
        }
        consume();
    }

    Token scan() {
        skipSpace();
        Token t;
        if (pos_ >= src_.size()) {
            t.type = Tok::End;
            return t;
        }
        const char c = src_[pos_];

        if (str::isAsciiDigit(c) || (c == '.' && pos_ + 1 < src_.size() && str::isAsciiDigit(src_[pos_ + 1]))) {
            return scanNumber();
        }
        if (c == '"' || c == '\'') {
            t.type = Tok::String;
            t.text = scanQuoted(c);
            return t;
        }
        if (c == '`') {
            ++pos_;
            t.type = Tok::TemplateStart;
            return t;
        }
        if (isIdentStart(c)) {
            const std::size_t start = pos_;
            while (pos_ < src_.size() && isIdentPart(src_[pos_])) {
                ++pos_;
            }
            t.type = Tok::Identifier;
            t.text = std::string(src_.substr(start, pos_ - start));
            return t;
        }

        static constexpr std::string_view kPuncts[] = {
            ">>>=", "===", "!==", "**=", "<<=", ">>=", ">>>", "...", "==", "!=", "<=", ">=", "&&", "||",
            "??",   "+=",  "-=",  "*=",  "/=",  "%=",  "&=",  "|=",  "^=", "**", "<<", ">>", "?.", "=>",
            "+",    "-",   "*",   "/",   "%",   "<",   ">",   "=",   "!",  "~",  "&",  "|",  "^",  "?",
            ":",    ",",   ".",   "(",   ")",   "[",   "]",   "{",   "}",  ";"};
        for (std::string_view p : kPuncts) {
            if (src_.substr(pos_, p.size()) == p) {
                pos_ += p.size();
                t.type = Tok::Punct;
                t.text = std::string(p);
                return t;
            }
        }
        throw SyntaxError("unexpected character");
    }

    Token scanNumber() {
        Token t;
        t.type = Tok::Number;
        const std::size_t start = pos_;
        if (src_[pos_] == '0' && pos_ + 1 < src_.size() &&
            (src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X' || src_[pos_ + 1] == 'b' || src_[pos_ + 1] == 'B' ||
             src_[pos_ + 1] == 'o' || src_[pos_ + 1] == 'O')) {
            pos_ += 2;
            while (pos_ < src_.size() && std::isalnum(static_cast<unsigned char>(src_[pos_]))) {
                ++pos_;
            }
        } else {
            while (pos_ < src_.size() && str::isAsciiDigit(src_[pos_])) {
                ++pos_;
            }
            if (pos_ < src_.size() && src_[pos_] == '.') {
                ++pos_;
                while (pos_ < src_.size() && str::isAsciiDigit(src_[pos_])) {
                    ++pos_;
                }
            }
            if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
                std::size_t p = pos_ + 1;
                if (p < src_.size() && (src_[p] == '+' || src_[p] == '-')) {
                    ++p;
                }
                if (p < src_.size() && str::isAsciiDigit(src_[p])) {
                    pos_ = p;
                    while (pos_ < src_.size() && str::isAsciiDigit(src_[pos_])) {
                        ++pos_;
                    }
                }
            }
        }
        if (pos_ < src_.size() && isIdentStart(src_[pos_])) {
            throw SyntaxError("identifier directly after number");
        }
        t.text = std::string(src_.substr(start, pos_ - start));
        t.number = js::stringToNumber(t.text);
        if (std::isnan(t.number)) {
            throw SyntaxError("invalid number");
        }
        return t;
    }

    // Reads one escape sequence (after the backslash) and appends it as UTF-8.
    void readEscape(std::string& out) {
        if (pos_ >= src_.size()) {
            throw SyntaxError("unterminated escape");
        }
        const char e = src_[pos_++];
        switch (e) {
            case 'n': out += '\n'; return;
            case 't': out += '\t'; return;
            case 'r': out += '\r'; return;
            case 'b': out += '\b'; return;
            case 'f': out += '\f'; return;
            case 'v': out += '\v'; return;
            case '0': out += '\0'; return;
            case '\r':
                if (pos_ < src_.size() && src_[pos_] == '\n') {
                    ++pos_;
                }
                return;
            case '\n':
                return;
            case 'x':
            case 'u': {
                std::size_t digits = e == 'x' ? 2 : 4;
                unsigned code = 0;
                if (e == 'u' && pos_ < src_.size() && src_[pos_] == '{') {
                    ++pos_;
                    digits = 0;
                    while (pos_ < src_.size() && src_[pos_] != '}') {
                        code = code * 16 + hexValue(src_[pos_++]);
                        ++digits;
                    }
                    if (pos_ >= src_.size() || digits == 0) {
                        throw SyntaxError("bad unicode escape");
                    }
                    ++pos_;
                } else {
                    for (std::size_t i = 0; i < digits; ++i) {
                        if (pos_ >= src_.size()) {
                            throw SyntaxError("bad escape");
                        }
                        code = code * 16 + hexValue(src_[pos_++]);
                    }
                }
                appendUtf8(out, code);
                return;
            }
            default:
                out += e;
                return;
        }
    }

    static unsigned hexValue(char c) {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        throw SyntaxError("bad hex digit");
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    std::string scanQuoted(char quote) {
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= src_.size()) {
                throw SyntaxError("unterminated string");
            }
            const char c = src_[pos_++];
            if (c == quote) {
                return out;
            }
            if (c == '\\') {
                readEscape(out);
            } else if (c == '\n' || c == '\r') {
                throw SyntaxError("newline in string");
            } else {
                out += c;
            }
        }
    }

    // Reads template text up to "${" (returns true) or the closing backtick
    // (returns false). Must only be called with no lookahead token pending.
    bool readTemplateChunk(std::string& out) {
        while (true) {
            if (pos_ >= src_.size()) {
                throw SyntaxError("unterminated template");
            }
            const char c = src_[pos_++];
            if (c == '`') {
                return false;
            }
            if (c == '$' && pos_ < src_.size() && src_[pos_] == '{') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                readEscape(out);
            } else {
                out += c;
            }
        }
    }

    // -- grammar --

    NodePtr parseExpression() {
        NodePtr first = parseAssignment();
        if (!isPunct(",")) {
            return first;
        }
        auto seq = makeNode(Node::Type::Sequence);
        seq->children.push_back(std::move(first));
        while (isPunct(",")) {
            consume();
            seq->children.push_back(parseAssignment());
        }
        return seq;
    }

    NodePtr parseAssignment() {
        NodePtr target = parseConditional();
        const Token& t = peek();
        if (t.type == Tok::Punct &&
            (t.text == "=" || t.text == "+=" || t.text == "-=" || t.text == "*=" || t.text == "/=" ||
             t.text == "%=" || t.text == "**=" || t.text == "<<=" || t.text == ">>=" || t.text == ">>>=" ||
             t.text == "&=" || t.text == "|=" || t.text == "^=")) {
            if (target->type != Node::Type::Identifier && target->type != Node::Type::Member) {
                throw SyntaxError("invalid assignment target");
            }
            auto node = makeNode(Node::Type::Assignment);
            node->name = consume().text;
            node->children.push_back(std::move(target));
            node->children.push_back(parseAssignment());
            return node;
        }
        if (t.type == Tok::Punct && t.text == "=>") {
            throw SyntaxError("arrow functions are not supported");
        }
        return target;
    }

    NodePtr parseConditional() {
        NodePtr test = parseBinary(1);
        if (!isPunct("?")) {
            return test;
        }
        consume();
        auto node = makeNode(Node::Type::Conditional);
        node->children.push_back(std::move(test));
        node->children.push_back(parseAssignment());
        expectPunct(":");
        node->children.push_back(parseAssignment());
        return node;
    }

    // Binary operator precedence (higher binds tighter). 0 = not binary.
    static int precedence(const Token& t) {
        if (t.type != Tok::Punct) {
            return 0;
        }
        const std::string& op = t.text;
        if (op == "??") return 1;
        if (op == "||") return 2;
        if (op == "&&") return 3;
        if (op == "|") return 4;
        if (op == "^") return 5;
        if (op == "&") return 6;
        if (op == "==" || op == "!=" || op == "===" || op == "!==") return 7;
        if (op == "<" || op == ">" || op == "<=" || op == ">=") return 8;
        if (op == "<<" || op == ">>" || op == ">>>") return 9;
        if (op == "+" || op == "-") return 10;
        if (op == "*" || op == "/" || op == "%") return 11;
        if (op == "**") return 12;
        return 0;
    }

    // Precedence climbing: handles operators binding at least `minPrecedence`.
    NodePtr parseBinary(int minPrecedence) {
        NodePtr left = parseUnary();
        while (true) {
            const int prec = precedence(peek());
            if (prec == 0 || prec < minPrecedence) {
                break;
            }
            const std::string op = consume().text;
            // "**" is right-associative; everything else is left-associative.
            NodePtr right = parseBinary(op == "**" ? prec : prec + 1);
            const bool logical = op == "&&" || op == "||" || op == "??";
            auto node = makeNode(logical ? Node::Type::Logical : Node::Type::Binary);
            node->name = op;
            node->children.push_back(std::move(left));
            node->children.push_back(std::move(right));
            left = std::move(node);
        }
        return left;
    }

    NodePtr parseUnary() {
        const Token& t = peek();
        if (t.type == Tok::Punct && (t.text == "+" || t.text == "-" || t.text == "!" || t.text == "~")) {
            auto node = makeNode(Node::Type::Unary);
            node->name = consume().text;
            node->children.push_back(parseUnary());
            return node;
        }
        if (t.type == Tok::Identifier && t.text == "typeof") {
            consume();
            auto node = makeNode(Node::Type::Unary);
            node->name = "typeof";
            node->children.push_back(parseUnary());
            return node;
        }
        return parsePostfix();
    }

    NodePtr parsePostfix() {
        NodePtr expr = parsePrimary();
        while (true) {
            if (isPunct(".") || isPunct("?.")) {
                consume();
                const Token name = consume();
                if (name.type != Tok::Identifier) {
                    throw SyntaxError("expected property name");
                }
                auto member = makeNode(Node::Type::Member);
                member->name = name.text;
                member->children.push_back(std::move(expr));
                expr = std::move(member);
            } else if (isPunct("[")) {
                consume();
                auto member = makeNode(Node::Type::Member);
                member->computed = true;
                member->children.push_back(std::move(expr));
                member->children.push_back(parseExpression());
                expectPunct("]");
                expr = std::move(member);
            } else if (isPunct("(")) {
                consume();
                auto call = makeNode(Node::Type::Call);
                call->children.push_back(std::move(expr));
                if (!isPunct(")")) {
                    call->children.push_back(parseAssignment());
                    while (isPunct(",")) {
                        consume();
                        if (isPunct(")")) {
                            break;
                        }
                        call->children.push_back(parseAssignment());
                    }
                }
                expectPunct(")");
                expr = std::move(call);
            } else if (peek().type == Tok::TemplateStart) {
                consume();
                auto tagged = makeNode(Node::Type::TaggedTemplate);
                tagged->children.push_back(std::move(expr));
                tagged->children.push_back(parseTemplateBody());
                expr = std::move(tagged);
            } else {
                return expr;
            }
        }
    }

    NodePtr parseTemplateBody() {
        auto node = makeNode(Node::Type::Template);
        while (true) {
            std::string chunk;
            const bool hasExpression = readTemplateChunk(chunk);
            node->keys.push_back(std::move(chunk));
            if (!hasExpression) {
                return node;
            }
            node->children.push_back(parseExpression());
            if (!isPunct("}")) {
                throw SyntaxError("expected } in template");
            }
            consume();  // no lookahead: the lexer now sits on template text
        }
    }

    NodePtr parsePrimary() {
        const Token t = consume();
        switch (t.type) {
            case Tok::Number: {
                auto node = makeNode(Node::Type::Literal);
                node->literal = Value(t.number);
                return node;
            }
            case Tok::String: {
                auto node = makeNode(Node::Type::Literal);
                node->literal = Value(t.text);
                return node;
            }
            case Tok::TemplateStart:
                return parseTemplateBody();
            case Tok::Identifier: {
                if (t.text == "true" || t.text == "false") {
                    auto node = makeNode(Node::Type::Literal);
                    node->literal = Value(t.text == "true");
                    return node;
                }
                if (t.text == "null") {
                    auto node = makeNode(Node::Type::Literal);
                    node->literal = Value::null();
                    return node;
                }
                if (t.text == "this") {
                    return makeNode(Node::Type::This);
                }
                if (t.text == "function" || t.text == "new" || t.text == "class" || t.text == "var" ||
                    t.text == "let" || t.text == "const" || t.text == "delete" || t.text == "void" ||
                    t.text == "in" || t.text == "instanceof") {
                    throw SyntaxError("unsupported keyword");
                }
                auto node = makeNode(Node::Type::Identifier);
                node->name = t.text;
                return node;
            }
            case Tok::Punct:
                if (t.text == "(") {
                    NodePtr inner = parseExpression();
                    expectPunct(")");
                    return inner;
                }
                if (t.text == "[") {
                    auto node = makeNode(Node::Type::Array);
                    while (!isPunct("]")) {
                        node->children.push_back(parseAssignment());
                        if (!isPunct(",")) {
                            break;
                        }
                        consume();
                    }
                    expectPunct("]");
                    return node;
                }
                if (t.text == "{") {
                    auto node = makeNode(Node::Type::Object);
                    while (!isPunct("}")) {
                        const Token key = consume();
                        std::string keyText;
                        if (key.type == Tok::Identifier || key.type == Tok::String) {
                            keyText = key.text;
                        } else if (key.type == Tok::Number) {
                            keyText = js::numberToString(key.number);
                        } else {
                            throw SyntaxError("bad object key");
                        }
                        if (isPunct(":")) {
                            consume();
                            node->children.push_back(parseAssignment());
                        } else if (key.type == Tok::Identifier) {
                            auto shorthand = makeNode(Node::Type::Identifier);  // { a } == { a: a }
                            shorthand->name = keyText;
                            node->children.push_back(std::move(shorthand));
                        } else {
                            throw SyntaxError("expected :");
                        }
                        node->keys.push_back(std::move(keyText));
                        if (!isPunct(",")) {
                            break;
                        }
                        consume();
                    }
                    expectPunct("}");
                    return node;
                }
                break;
            default:
                break;
        }
        throw SyntaxError("unexpected token");
    }

    std::string_view src_;
    std::size_t pos_ = 0;
    Token token_;
    bool hasToken_ = false;
};

// ---- Evaluation ----------------------------------------------------------------

using Result = std::optional<Value>;

Value toPrimitive(const Value& v) {
    if (v.isObject() || v.isArray() || v.isFunction()) {
        return Value(toString(v));
    }
    return v;
}

bool isArrayIndex(std::string_view key, std::size_t& index) {
    if (key.empty() || key.size() > 9) {
        return false;
    }
    std::size_t value = 0;
    for (char c : key) {
        if (!str::isAsciiDigit(c)) {
            return false;
        }
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    if (key.size() > 1 && key.front() == '0') {
        return false;
    }
    index = value;
    return true;
}

// Reads base[key]; nullopt when base is null/undefined (a TypeError in JS).
Result getProperty(const Value& base, const std::string& key) {
    switch (base.kind()) {
        case Value::Kind::Undefined:
        case Value::Kind::Null:
            return std::nullopt;
        case Value::Kind::Object:
            return base.get(key);
        case Value::Kind::Function: {
            const Value* found = base.functionData().properties->find(key);
            if (found) {
                return *found;
            }
            if (key == "name") {
                return Value(base.functionData().name);
            }
            return Value{};
        }
        case Value::Kind::Array: {
            std::size_t index = 0;
            if (isArrayIndex(key, index)) {
                const auto& items = base.arrayData().items;
                return index < items.size() ? items[index] : Value{};
            }
            break;
        }
        default:
            break;
    }
    if (auto member = detail::primitiveMember(base, key)) {
        return *member;
    }
    return Value{};
}

class Evaluator {
public:
    explicit Evaluator(const Value& scope) : scope_(scope) {}

    Result eval(const Node& node) {
        switch (node.type) {
            case Node::Type::Literal:
                return node.literal;
            case Node::Type::Identifier: {
                if (!scope_.has(node.name)) {
                    return std::nullopt;
                }
                Value value = scope_.get(node.name);
                // gSender converts numeric-looking variables (posx = "12.345").
                const double number = toNumber(value);
                if (!std::isnan(number)) {
                    return Value(number);
                }
                return value;
            }
            case Node::Type::This:
                if (scope_.has("this")) {
                    return scope_.get("this");
                }
                return std::nullopt;
            case Node::Type::Array: {
                std::vector<Value> items;
                for (const auto& child : node.children) {
                    Result item = eval(*child);
                    if (!item) {
                        return std::nullopt;
                    }
                    items.push_back(std::move(*item));
                }
                return Value::array(std::move(items));
            }
            case Node::Type::Object: {
                Value object = Value::object();
                for (std::size_t i = 0; i < node.children.size(); ++i) {
                    Result item = eval(*node.children[i]);
                    if (!item) {
                        return std::nullopt;
                    }
                    object.set(node.keys[i], std::move(*item));
                }
                return object;
            }
            case Node::Type::Unary:
                return evalUnary(node);
            case Node::Type::Binary: {
                Result left = eval(*node.children[0]);
                if (!left) {
                    return std::nullopt;
                }
                Result right = eval(*node.children[1]);
                if (!right) {
                    return std::nullopt;
                }
                return binary(node.name, *left, *right);
            }
            case Node::Type::Logical: {
                Result left = eval(*node.children[0]);
                if (!left) {
                    return std::nullopt;
                }
                if (node.name == "&&" && !toBoolean(*left)) {
                    return left;
                }
                if (node.name == "||" && toBoolean(*left)) {
                    return left;
                }
                if (node.name == "??" && !left->isNullish()) {
                    return left;
                }
                return eval(*node.children[1]);
            }
            case Node::Type::Conditional: {
                Result test = eval(*node.children[0]);
                if (!test) {
                    return std::nullopt;
                }
                return eval(*node.children[toBoolean(*test) ? 1 : 2]);
            }
            case Node::Type::Member: {
                Result base = eval(*node.children[0]);
                if (!base) {
                    return std::nullopt;
                }
                std::optional<std::string> key = memberKey(node);
                if (!key) {
                    return std::nullopt;
                }
                return getProperty(*base, *key);
            }
            case Node::Type::Call:
                return evalCall(node);
            case Node::Type::Template: {
                std::string out = node.keys[0];
                for (std::size_t i = 0; i < node.children.size(); ++i) {
                    Result part = eval(*node.children[i]);
                    if (!part) {
                        return std::nullopt;
                    }
                    out += toString(*part);
                    out += node.keys[i + 1];
                }
                return Value(out);
            }
            case Node::Type::TaggedTemplate: {
                Result tag = eval(*node.children[0]);
                if (!tag || !tag->isFunction()) {
                    return std::nullopt;
                }
                const Node& tmpl = *node.children[1];
                std::vector<Value> strings;
                for (const std::string& quasi : tmpl.keys) {
                    strings.emplace_back(quasi);
                }
                std::vector<Value> args{Value::array(std::move(strings))};
                for (const auto& child : tmpl.children) {
                    Result value = eval(*child);
                    if (!value) {
                        return std::nullopt;
                    }
                    args.push_back(std::move(*value));
                }
                return tag->functionData().fn(Value{}, args);
            }
            case Node::Type::Assignment:
            case Node::Type::Sequence:
                // Only top-level %-line assignments are supported, as in gSender.
                return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> memberKey(const Node& member) {
        if (!member.computed) {
            return member.name;
        }
        Result key = eval(*member.children[1]);
        if (!key) {
            return std::nullopt;
        }
        return toString(*key);
    }

    static Result binary(const std::string& op, const Value& l, const Value& r) {
        if (op == "+") {
            const Value lp = toPrimitive(l);
            const Value rp = toPrimitive(r);
            if (lp.isString() || rp.isString()) {
                return Value(toString(lp) + toString(rp));
            }
            return Value(toNumber(lp) + toNumber(rp));
        }
        if (op == "-") return Value(toNumber(l) - toNumber(r));
        if (op == "*") return Value(toNumber(l) * toNumber(r));
        if (op == "/") return Value(toNumber(l) / toNumber(r));
        if (op == "%") return Value(std::fmod(toNumber(l), toNumber(r)));
        if (op == "**") return Value(std::pow(toNumber(l), toNumber(r)));
        if (op == "==") return Value(looseEquals(l, r));
        if (op == "!=") return Value(!looseEquals(l, r));
        if (op == "===") return Value(strictEquals(l, r));
        if (op == "!==") return Value(!strictEquals(l, r));
        if (op == "<" || op == ">" || op == "<=" || op == ">=") {
            const Value lp = toPrimitive(l);
            const Value rp = toPrimitive(r);
            if (lp.isString() && rp.isString()) {
                const int cmp = lp.asString().compare(rp.asString());
                if (op == "<") return Value(cmp < 0);
                if (op == ">") return Value(cmp > 0);
                if (op == "<=") return Value(cmp <= 0);
                return Value(cmp >= 0);
            }
            const double a = toNumber(lp);
            const double b = toNumber(rp);
            if (std::isnan(a) || std::isnan(b)) {
                return Value(false);
            }
            if (op == "<") return Value(a < b);
            if (op == ">") return Value(a > b);
            if (op == "<=") return Value(a <= b);
            return Value(a >= b);
        }
        if (op == "|") return Value(static_cast<double>(toInt32(l) | toInt32(r)));
        if (op == "&") return Value(static_cast<double>(toInt32(l) & toInt32(r)));
        if (op == "^") return Value(static_cast<double>(toInt32(l) ^ toInt32(r)));
        const std::uint32_t shift = static_cast<std::uint32_t>(toInt32(r)) & 31u;
        if (op == "<<") {
            return Value(static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(toInt32(l)) << shift)));
        }
        if (op == ">>") return Value(static_cast<double>(toInt32(l) >> shift));
        if (op == ">>>") return Value(static_cast<double>(static_cast<std::uint32_t>(toInt32(l)) >> shift));
        return std::nullopt;
    }

private:
    Result evalUnary(const Node& node) {
        if (node.name == "typeof") {
            const Node& operand = *node.children[0];
            if (operand.type == Node::Type::Identifier && !scope_.has(operand.name)) {
                return Value("undefined");
            }
            Result value = eval(operand);
            if (!value) {
                return std::nullopt;
            }
            return Value(typeOf(*value));
        }
        Result value = eval(*node.children[0]);
        if (!value) {
            return std::nullopt;
        }
        if (node.name == "+") return Value(toNumber(*value));
        if (node.name == "-") return Value(-toNumber(*value));
        if (node.name == "!") return Value(!toBoolean(*value));
        if (node.name == "~") return Value(static_cast<double>(~toInt32(*value)));
        return std::nullopt;
    }

    Result evalCall(const Node& node) {
        const Node& callee = *node.children[0];
        Value self;
        Result function;
        if (callee.type == Node::Type::Member) {
            Result base = eval(*callee.children[0]);
            if (!base) {
                return std::nullopt;
            }
            std::optional<std::string> key = memberKey(callee);
            if (!key) {
                return std::nullopt;
            }
            function = getProperty(*base, *key);
            self = *base;
        } else {
            function = eval(callee);
        }
        if (!function || !function->isFunction()) {
            return std::nullopt;
        }
        std::vector<Value> args;
        for (std::size_t i = 1; i < node.children.size(); ++i) {
            Result arg = eval(*node.children[i]);
            if (!arg) {
                return std::nullopt;
            }
            args.push_back(std::move(*arg));
        }
        return function->functionData().fn(self, args);
    }

    const Value& scope_;
};

// ---- Assignment ----------------------------------------------------------------

// Property path of an assignment target, e.g. global.state["wcs"] -> [global, state, wcs].
bool assignmentPath(const Node& node, Evaluator& evaluator, std::vector<std::string>& path) {
    if (node.type == Node::Type::Identifier) {
        path.push_back(node.name);
        return true;
    }
    if (node.type == Node::Type::Member) {
        if (!assignmentPath(*node.children[0], evaluator, path)) {
            return false;
        }
        if (!node.computed) {
            path.push_back(node.name);
            return true;
        }
        Result key = evaluator.eval(*node.children[1]);
        path.push_back(key ? toString(*key) : std::string("undefined"));
        return true;
    }
    return false;
}

Value getPath(const Value& root, const std::vector<std::string>& path) {
    Value current = root;
    for (const std::string& key : path) {
        Result next = getProperty(current, key);
        if (!next) {
            return {};
        }
        current = std::move(*next);
    }
    return current;
}

// lodash _.set: creates missing (or non-object) intermediate containers.
void setPath(const Value& root, const std::vector<std::string>& path, const Value& value) {
    Value current = root;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const std::string& key = path[i];
        const bool last = i + 1 == path.size();
        std::size_t index = 0;
        const bool arrayTarget = current.isArray() && isArrayIndex(key, index);

        if (last) {
            if (arrayTarget) {
                auto& items = current.arrayData().items;
                if (items.size() <= index) {
                    items.resize(index + 1);
                }
                items[index] = value;
            } else if (current.isObject()) {
                current.set(key, value);
            }
            return;
        }

        Value next = arrayTarget ? (index < current.arrayData().items.size() ? current.arrayData().items[index] : Value{})
                                 : current.get(key);
        if (!next.isObject() && !next.isArray()) {
            std::size_t ignored = 0;
            next = isArrayIndex(path[i + 1], ignored) ? Value::array() : Value::object();
            if (arrayTarget) {
                auto& items = current.arrayData().items;
                if (items.size() <= index) {
                    items.resize(index + 1);
                }
                items[index] = next;
            } else if (current.isObject()) {
                current.set(key, next);
            } else {
                return;
            }
        }
        current = next;
    }
}

void runAssignment(const Node& node, const Value& scope) {
    Evaluator evaluator(scope);
    std::vector<std::string> path;
    if (!assignmentPath(*node.children[0], evaluator, path) || path.empty()) {
        return;
    }
    Result right = evaluator.eval(*node.children[1]);
    Value value = right ? *right : Value{};
    if (node.name != "=") {
        const std::string op = node.name.substr(0, node.name.size() - 1);
        Result combined = right ? Evaluator::binary(op, getPath(scope, path), *right) : std::nullopt;
        value = combined ? *combined : Value{};
    }
    setPath(scope, path, value);
}

NodePtr parse(std::string_view source) {
    try {
        return Parser(source).parseProgram();
    } catch (const SyntaxError&) {
        return nullptr;
    }
}

}  // namespace

std::optional<Value> evaluate(std::string_view source, const Value& scope) {
    NodePtr ast = parse(source);
    if (!ast) {
        return std::nullopt;
    }
    Evaluator evaluator(scope);
    return evaluator.eval(*ast);
}

bool evaluateAssignments(std::string_view source, const Value& scope) {
    if (str::trim(source).empty()) {
        return true;
    }
    NodePtr ast = parse(source);
    if (!ast) {
        return false;
    }
    const auto runOne = [&](const Node& node) {
        if (node.type == Node::Type::Assignment) {
            runAssignment(node, scope);
        } else {
            Evaluator evaluator(scope);
            evaluator.eval(node);
        }
    };
    if (ast->type == Node::Type::Sequence) {
        for (const auto& child : ast->children) {
            runOne(*child);
        }
    } else {
        runOne(*ast);
    }
    return true;
}

std::string translateExpressions(std::string_view line, const Value& scope) {
    if (line.find('[') == std::string_view::npos) {
        return std::string(line);
    }
    std::string out;
    out.reserve(line.size());
    std::size_t i = 0;
    while (i < line.size()) {
        const std::size_t open = line.find('[', i);
        if (open == std::string_view::npos) {
            out.append(line.substr(i));
            break;
        }
        const std::size_t close = line.find(']', open + 1);
        // /\[[^\]]+\]/ needs at least one character between the brackets.
        if (close == std::string_view::npos) {
            out.append(line.substr(i));
            break;
        }
        out.append(line.substr(i, open - i));
        if (close == open + 1) {
            out.append("[]");
            i = close + 1;
            continue;
        }
        const std::string_view source = line.substr(open + 1, close - open - 1);
        std::optional<Value> value = evaluate(source, scope);
        if (value && !value->isUndefined()) {
            out.append(toString(*value));
        } else {
            out.append(line.substr(open, close - open + 1));
        }
        i = close + 1;
    }
    return out;
}

}  // namespace gs::expr
